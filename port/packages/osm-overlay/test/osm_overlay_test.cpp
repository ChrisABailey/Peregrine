// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The peregrine.osm package end to end, against the shared core: it loads from
// the system packages directory, draws the catalog's OSM coverage over the map,
// and its own style sheet option changes what it draws.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

#include "fv_desk_fake.h"
#include "fv_desk_packages.h"
#include "fv_desk_vector_map.h"
#include "fv_osm_format.h"
#include "fvkit/app/manager_host.h"
#include "fvkit/app/properties.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/raster.h"
#include "scratch.h"

namespace {

constexpr char kPackage[] = "peregrine.osm";
constexpr char kType[] = "peregrine.osm.overlay";

std::string KiawahMbtiles() {
  const char* root = std::getenv("FVW_TESTDATA_DIR");
  if (root == nullptr) return std::string();
  const std::string p = std::string(root) + "/OSM/kiawah.mbtiles";
  return std::filesystem::exists(p) ? p : std::string();
}

/// Pixels that differ between two renders of the same size.
int Changed(const fv::PixelBuffer& a, const fv::PixelBuffer& b) {
  int n = 0;
  for (int y = 0; y < a.Height(); ++y) {
    const unsigned char* ra = a.Row(y);
    const unsigned char* rb = b.Row(y);
    for (int x = 0; x < a.Width() * 4; x += 4)
      if (ra[x] != rb[x] || ra[x + 1] != rb[x + 1] || ra[x + 2] != rb[x + 2]) ++n;
  }
  return n;
}

class OsmOverlayTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mbtiles_ = KiawahMbtiles();
    if (mbtiles_.empty()) GTEST_SKIP() << "no testdata/OSM/kiawah.mbtiles";
    fv::ClearFormatRegistryForTest();
    fv::RegisterBuiltinFormats();
    fv::RegisterOsmFormat();
    catalog_ = std::make_shared<fv::Catalog>();
    ASSERT_TRUE(catalog_->Open(scratch_.Path("catalog.sqlite")).ok());
    int64_t src = 0;
    int added = 0;
    ASSERT_TRUE(catalog_->AddDataSource(mbtiles_, "osm", 0, &src).ok());
    ASSERT_TRUE(catalog_->Scan(src, &added).ok());

    // The base map under the overlay is the catalog's OSM in the core's sheet.
    fv::desk::VectorMapConfig config = fv::desk::CurrentVectorMapConfig();
    config.osm_style = FV_BASE_STYLE_JSON;
    fv::desk::SetVectorMapConfig(config);
    desk().LoadPackages(fv::desk::PackageDirs{FV_PACKAGES_DIR, scratch_.Path("user")});
    auto& mv = desk().map_view();
    mv.SetViewport(mv.View().WithCamera(fv::GeoPoint{32.607, -80.08}, 25000, 0));
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  fv::desk::Desk& desk() { return fake_.desk(); }

  fv::PixelBuffer Render() {
    fv::PixelBuffer buf;
    const fv::Status s = fake_.Render(&buf);
    EXPECT_TRUE(s.ok()) << s.message;
    return buf;
  }

  std::string mbtiles_;
  fv::desk::test::Scratch scratch_;
  std::shared_ptr<fv::Catalog> catalog_;
  fv::desk::FakeDesk fake_{400, 300};
};

TEST_F(OsmOverlayTest, LoadsFromTheSystemDirectoryWithItsOptions) {
  const fv::desk::PackageInfo* p = desk().packages()->Find(kPackage);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, fv::desk::PackageState::kLoaded) << p->reason;
  const fv::app::OverlayTypeDesc* type = desk().types().Find(kType);
  ASSERT_NE(type, nullptr);
  std::shared_ptr<fv::Overlay> o = type->factory();
  ASSERT_NE(o->AsProperties(), nullptr);
  fv::app::PropertyValue style;
  ASSERT_TRUE(o->AsProperties()->GetProperty("style", &style).ok());
  EXPECT_EQ(std::filesystem::path(style.s).filename(), "osm-overlay.json");
  EXPECT_TRUE(std::filesystem::exists(style.s));
}

TEST_F(OsmOverlayTest, DrawsTheCatalogCoverageAndFollowsItsStyleOption) {
  desk().SetCatalog(catalog_);
  const fv::PixelBuffer bare = Render();
  ASSERT_TRUE(desk().Execute(std::string("overlay.toggle.") + kType).ok());
  std::shared_ptr<fv::Overlay> osm = desk().overlays().FirstOfType(kType);
  ASSERT_NE(osm, nullptr);
  EXPECT_TRUE(fake_.shell().errors.empty())
      << (fake_.shell().errors.empty() ? "" : fake_.shell().errors[0].message);
  ASSERT_NE(osm->AsHostAware(), nullptr);
  EXPECT_TRUE(osm->AsHostAware()->HostStatus().ok());
  const fv::PixelBuffer overlay = Render();
  const int drawn = Changed(bare, overlay);
  EXPECT_GT(drawn, 500);

  // A different sheet, set as Overlay ▸ Options would, changes the drawing.
  ASSERT_TRUE(osm->AsProperties()
                  ->SetProperty("style", fv::app::PropertyValue::Path(FV_ALT_STYLE_JSON))
                  .ok());
  EXPECT_GT(Changed(overlay, Render()), 500);
  EXPECT_FALSE(osm->AsProperties()
                   ->SetProperty("style", fv::app::PropertyValue::Path("/no/such/style.json"))
                   .ok());
  // An empty path is the package's own sheet again.
  ASSERT_TRUE(osm->AsProperties()->SetProperty("style", fv::app::PropertyValue::Path("")).ok());
  EXPECT_EQ(Changed(overlay, Render()), 0);
}

TEST_F(OsmOverlayTest, WithoutCoverageTheDeskReportsIt) {
  ASSERT_TRUE(desk().Execute(std::string("overlay.toggle.") + kType).ok());
  std::shared_ptr<fv::Overlay> osm = desk().overlays().FirstOfType(kType);
  ASSERT_NE(osm, nullptr);
  EXPECT_EQ(osm->AsHostAware()->HostStatus().code, fv::kNotFound);
  // The coverage arrives with the catalog; the host rebinds the overlay.
  desk().SetCatalog(catalog_);
  EXPECT_TRUE(osm->AsHostAware()->HostStatus().ok());
}

TEST_F(OsmOverlayTest, ManagerHostAnswersCoverageFromItsCatalog) {
  fv::OverlayManager manager;
  fv::app::ManagerHost host(manager);
  EXPECT_TRUE(host.Coverage("osm", fv::GeoRect::World()).empty());
  host.SetCatalog(catalog_);
  const std::vector<fv::CoverageRow> rows = host.Coverage("osm", fv::GeoRect::World());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(std::filesystem::path(rows[0].path).filename(), "kiawah.mbtiles");
  EXPECT_TRUE(host.Coverage("enc", fv::GeoRect::World()).empty());
  EXPECT_TRUE(host.Coverage("osm", fv::GeoRect{{10, 10}, {11, 11}}).empty());
}

}  // namespace
