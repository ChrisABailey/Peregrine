// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The vector base map: DNC, ENC and OSM drawn by BaseMapRenderer from a
// catalog over testdata (skipped without it), the configuration errors that
// name their settings key, and the Map ▸ Options pages that set it.

#include "fv_desk_vector_map.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

#include "fv_desk_base_map.h"
#include "fv_desk_fake.h"
#include "fv_desk_map_options.h"
#include "fv_enc_format.h"
#include "fv_osm_format.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"
#include "fvkit/vector/mariner.h"
#include "fvkit/tools/png_write.h"
#include "scratch.h"

using fv::app::PropertyValue;
using fv::desk::BaseMapRenderer;
using fv::desk::VectorMapConfig;
using fv::desk::test::Scratch;

namespace {

std::string TestData(const std::string& rel) {
  const char* root = std::getenv("FVW_TESTDATA_DIR");
  if (root == nullptr) return std::string();
  const std::string p = std::string(root) + "/" + rel;
  return std::filesystem::exists(p) ? p : std::string();
}

/// Restores an empty vector configuration at scope exit.
struct ConfigGuard {
  ~ConfigGuard() { fv::desk::SetVectorMapConfig(VectorMapConfig()); }
};

/// A catalog of `dir` scanned as `format`, and a view of one of its series.
struct VectorFixture {
  Scratch scratch;
  std::shared_ptr<fv::Catalog> catalog;
  fv::view::LadderProduct product;
  fv::GeoRect bounds;

  /// Catalogs `dir`; picks `series_key`, or the format's largest-scale
  /// series when it is empty.
  void Load(const std::string& dir, const std::string& format, const std::string& series_key) {
    fv::ClearFormatRegistryForTest();
    fv::RegisterBuiltinFormats();
    fv::RegisterEncFormat();
    fv::RegisterOsmFormat();
    catalog = std::make_shared<fv::Catalog>();
    ASSERT_TRUE(catalog->Open(scratch.Path("catalog.sqlite")).ok());
    int64_t src = 0;
    int added = 0;
    ASSERT_TRUE(catalog->AddDataSource(dir, format, 0, &src).ok());
    ASSERT_TRUE(catalog->Scan(src, &added).ok());
    ASSERT_GT(added, 0);
    std::vector<fv::SeriesRow> series;
    ASSERT_TRUE(catalog->Series(&series).ok());
    const fv::SeriesRow* pick = nullptr;
    for (const fv::SeriesRow& s : series) {
      if (s.format != format) continue;
      if (!series_key.empty() ? s.series_key == series_key
                              : pick == nullptr || (s.scale_denom > 0 && s.scale_denom < pick->scale_denom))
        pick = &s;
    }
    ASSERT_NE(pick, nullptr) << series_key;
    product.series_id = pick->id;
    product.format = pick->format;
    product.series_key = pick->series_key;
    product.scale_denom = pick->scale_denom > 0 ? pick->scale_denom : 50000;
    std::vector<fv::CoverageRow> rows;
    ASSERT_TRUE(catalog->SelectByGeoRect(fv::GeoRect::World(), &rows, pick->id).ok());
    ASSERT_FALSE(rows.empty());
    bounds = rows.front().bounds;
  }

  fv::view::Viewport ViewAt(const fv::GeoPoint& center, double scale) const {
    return fv::view::Viewport::Make(center, scale).WithSurface(200, 150, 2.0, 0.25);
  }

  fv::GeoPoint Middle() const {
    return {(bounds.ll.lat + bounds.ur.lat) / 2, (bounds.ll.lon + bounds.ur.lon) / 2};
  }

  ~VectorFixture() { fv::ClearFormatRegistryForTest(); }
};

/// Pixels that differ from the top-left one, which is the ground colour
/// wherever nothing was drawn there.
int Inked(const fv::CpuCanvas& c) {
  if (const char* d = std::getenv("FV_DEBUG_PNG_DIR"))
    fv::WritePng(c.Buffer(), std::string(d) + "/" +
                                 ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".png");
  const fv::PixelBuffer& b = c.Buffer();
  const unsigned char* px = b.Data();
  const size_t size = static_cast<size_t>(b.Width()) * b.Height() * 4;
  int n = 0;
  for (size_t i = 0; i < size; i += 4)
    n += px[i] != px[0] || px[i + 1] != px[1] || px[i + 2] != px[2];
  return n;
}

TEST(MarinerText, ParsesDepthsAndSwitches) {
  std::optional<double> d;
  EXPECT_TRUE(fv::desk::ParseMarinerDepth(" 12.5 ", &d));
  EXPECT_EQ(d, 12.5);
  EXPECT_TRUE(fv::desk::ParseMarinerDepth("", &d));
  EXPECT_FALSE(d.has_value());
  EXPECT_FALSE(fv::desk::ParseMarinerDepth("-1", &d));
  EXPECT_FALSE(fv::desk::ParseMarinerDepth("10m", &d));
  std::optional<bool> b;
  EXPECT_TRUE(fv::desk::ParseMarinerFlag("On", &b));
  EXPECT_EQ(b, true);
  EXPECT_TRUE(fv::desk::ParseMarinerFlag("0", &b));
  EXPECT_EQ(b, false);
  EXPECT_TRUE(fv::desk::ParseMarinerFlag(" ", &b));
  EXPECT_FALSE(b.has_value());
  EXPECT_FALSE(fv::desk::ParseMarinerFlag("maybe", &b));
}

TEST(MarinerText, UnsetKeysKeepTheProductDefault) {
  fv::desk::MarinerOverrides o;
  o.safety_contour = 12;
  o.two_shades = true;
  fv::MarinerSettings m;
  m.shallow_contour = 3;
  o.ApplyTo(&m);
  EXPECT_EQ(m.safety_contour, 12);
  EXPECT_EQ(m.shallow_contour, 3);
  EXPECT_TRUE(m.two_shades);
  EXPECT_FALSE(m.shallow_pattern);
}

TEST(VectorMapOptions, SettingsLoadReachesTheConfig) {
  ConfigGuard guard;
  fv::desk::FakeDesk fake;
  fake.settings().Set("geosym.data_dir", "/data/geosym");
  fake.settings().Set("geosym.contrast", "250");
  fake.settings().Set("enc.data_dir", "/data/enc");
  fake.settings().Set("enc.show_meta_objects", "true");
  fake.settings().Set("mariner.safety_contour", "8");
  fake.settings().Set("mariner.shallow_pattern", "off");
  fake.settings().Set("osm.style", "/data/style.json");
  fake.desk().SettingsLoaded();
  const VectorMapConfig c = fv::desk::CurrentVectorMapConfig();
  EXPECT_EQ(c.geosym_dir, "/data/geosym");
  EXPECT_EQ(c.geosym_contrast, 100);  // clamped
  EXPECT_EQ(c.enc_dir, "/data/enc");
  EXPECT_TRUE(c.enc_show_meta);
  EXPECT_EQ(c.mariner.safety_contour, 8.0);
  EXPECT_FALSE(c.mariner.deep_contour.has_value());
  EXPECT_EQ(c.mariner.shallow_pattern, false);
  EXPECT_EQ(c.osm_style, "/data/style.json");
}

TEST(VectorMapOptions, PagesFollowTheGroupTableAndRejectBadDepths) {
  ConfigGuard guard;
  fv::desk::FakeDesk fake;
  auto model = fake.desk().MapOptions();
  std::vector<std::string> ids;
  for (const auto& p : model->pages()) ids.push_back(p->id());
  EXPECT_EQ(ids, (std::vector<std::string>{"elevation", "osm", "enc", "dnc"}));
  fv::desk::OptionsPage* enc = model->Page("enc");
  ASSERT_NE(enc, nullptr);
  EXPECT_EQ(enc->Set("mariner.safety_contour", PropertyValue::String("deep")).code,
            fv::kInvalidArg);
  ASSERT_TRUE(enc->Set("mariner.safety_contour", PropertyValue::String("15")).ok());
  ASSERT_TRUE(fake.desk().ApplyMapOptions(*model).ok());
  EXPECT_EQ(fake.settings().GetString("mariner.safety_contour"), "15");
  EXPECT_EQ(fv::desk::CurrentVectorMapConfig().mariner.safety_contour, 15.0);
}

TEST(VectorBaseMap, MissingAssetsNameTheirSettingsKey) {
  const std::string dir = TestData("vpf/dnc17");
  if (dir.empty()) GTEST_SKIP() << "no testdata/vpf/dnc17";
  ConfigGuard guard;
  VectorFixture f;
  ASSERT_NO_FATAL_FAILURE(f.Load(dir, "vpf", "h1707300"));
  BaseMapRenderer r;
  r.SetCatalog(f.catalog);
  const auto v = f.ViewAt(f.Middle(), 50000);
  fv::CpuCanvas canvas(v.PixelWidth(), v.PixelHeight());
  const fv::Status s = r.Render(v, f.product, canvas);
  EXPECT_FALSE(s.ok());
  EXPECT_NE(s.message.find("geosym.data_dir"), std::string::npos) << s.message;
}

TEST(VectorBaseMap, DrawsDncThroughGeoSym) {
  const std::string dir = TestData("vpf/dnc17");
  const std::string geosym = TestData("GeoSymbol");
  if (dir.empty() || geosym.empty()) GTEST_SKIP() << "no testdata/vpf/dnc17 or GeoSymbol";
  ConfigGuard guard;
  VectorMapConfig c;
  c.geosym_dir = std::filesystem::path(geosym).parent_path().string();
  fv::desk::SetVectorMapConfig(c);
  VectorFixture f;
  ASSERT_NO_FATAL_FAILURE(f.Load(dir, "vpf", "h1707300"));
  EXPECT_TRUE(BaseMapRenderer::CanDraw("vpf"));
  BaseMapRenderer r;
  r.SetCatalog(f.catalog);
  // Monomoy and the Cape Cod shore, the GeoSym golden's view.
  const auto v = f.ViewAt({41.70, -69.90}, 300000);
  fv::CpuCanvas canvas(v.PixelWidth(), v.PixelHeight());
  const fv::Status s = r.Render(v, f.product, canvas);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_GT(Inked(canvas), v.PixelWidth() * v.PixelHeight() / 20);
}

TEST(VectorBaseMap, DrawsAnEncBandThroughS52) {
  const std::string dir = TestData("enc");
  if (dir.empty()) GTEST_SKIP() << "no testdata/enc";
  ConfigGuard guard;
  VectorMapConfig c;
  c.enc_dir = dir;
  fv::desk::SetVectorMapConfig(c);
  VectorFixture f;
  ASSERT_NO_FATAL_FAILURE(f.Load(dir, "enc", ""));
  BaseMapRenderer r;
  r.SetCatalog(f.catalog);
  // Charleston harbour, in the harbour band.
  const auto v = f.ViewAt({32.765, -79.91}, 40000);
  fv::CpuCanvas canvas(v.PixelWidth(), v.PixelHeight());
  const fv::Status s = r.Render(v, f.product, canvas);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_GT(Inked(canvas), v.PixelWidth() * v.PixelHeight() / 20);
}

TEST(VectorBaseMap, DrawsOsmWithItsStyleSheet) {
  const std::string file = TestData("OSM/kiawah.mbtiles");
  if (file.empty()) GTEST_SKIP() << "no testdata/OSM/kiawah.mbtiles";
  ConfigGuard guard;
  VectorMapConfig c;
  c.osm_style = FV_OSM_STYLE_JSON;
  fv::desk::SetVectorMapConfig(c);
  VectorFixture f;
  ASSERT_NO_FATAL_FAILURE(f.Load(file, "osm", ""));
  BaseMapRenderer r;
  r.SetCatalog(f.catalog);
  const auto v = f.ViewAt(f.Middle(), 25000);
  fv::CpuCanvas canvas(v.PixelWidth(), v.PixelHeight());
  const fv::Status s = r.Render(v, f.product, canvas);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_GT(Inked(canvas), v.PixelWidth() * v.PixelHeight() / 20);
}

}  // namespace
