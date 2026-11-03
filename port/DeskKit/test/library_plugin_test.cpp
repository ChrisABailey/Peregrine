// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The C plugin API end to end: the rangerings-c example's .pgpkg is installed,
// and its overlay is drawn, hit-tested, edited and saved through the adapter.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "fv_desk_fake.h"
#include "fv_desk_library_plugin.h"
#include "fv_desk_packages.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/properties.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "scratch.h"

namespace {

namespace fs = std::filesystem;
using fv::desk::PackageState;
using fv::desk::test::Scratch;

constexpr char kPackage[] = "com.example.rangerings";
constexpr char kType[] = "com.example.rangerings.rings";
constexpr double kMetresPerNm = 1852.0;

std::string ReadFile(const fs::path& path) {
  std::ostringstream ss;
  ss << std::ifstream(path, std::ios::binary).rdbuf();
  return ss.str();
}

/// Pixels whose colour is mostly red, the example's default ring colour.
int RedPixels(const fv::PixelBuffer& buf) {
  int n = 0;
  for (int y = 0; y < buf.Height(); ++y) {
    const unsigned char* row = buf.Row(y);
    for (int x = 0; x < buf.Width(); ++x)
      if (row[x * 4] > 180 && row[x * 4 + 1] < 90 && row[x * 4 + 2] < 90 && row[x * 4 + 3] > 0) ++n;
  }
  return n;
}

class LibraryPluginTest : public ::testing::Test {
 protected:
  void SetUp() override {
    desk().LoadPackages(fv::desk::PackageDirs{std::string(), scratch_.Path("user")});
    fv::desk::InstallResult r;
    const fv::Status s = desk().InstallPackage(FV_RANGERINGS_PGPKG, &r);
    ASSERT_TRUE(s.ok()) << s.message;
    ASSERT_TRUE(r.live);
    const fv::app::OverlayTypeDesc* type = desk().types().Find(kType);
    ASSERT_NE(type, nullptr);
    overlay_ = type->factory();
    ASSERT_NE(overlay_, nullptr);

    ASSERT_TRUE(proj_.SetSurfaceSize(512, 512).ok());
    ASSERT_TRUE(proj_.SetCenter(fv::GeoPoint{32.7765, -79.9311}).ok());
    ASSERT_TRUE(proj_.SetScale(100000).ok());
  }

  fv::desk::Desk& desk() { return fake_.desk(); }

  /// Draws the overlay on a fresh transparent canvas.
  fv::Status Draw(fv::CpuCanvas* canvas) {
    canvas->Clear(fv::FvColor{0, 0, 0, 0});
    return overlay_->OnDraw(proj_, *canvas);
  }

  /// Surface position of `metres` north of the default centre.
  fv::PixelPoint NorthOfCentre(double metres) const {
    double x = 0, y = 0;
    proj_.GeoToSurface(fv::GeoPoint{32.7765 + metres / 6371008.8 * 180.0 / M_PI, -79.9311}, &x, &y);
    return fv::PixelPoint{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
  }

  std::vector<fv::app::HitItem> HitsAt(fv::PixelPoint p) {
    std::vector<fv::app::HitItem> hits;
    overlay_->AsHitTest()->HitTestPoint(proj_, p, 3.0, hits);
    return hits;
  }

  Scratch scratch_;
  fv::desk::FakeDesk fake_;
  std::shared_ptr<fv::Overlay> overlay_;
  fv::MapProjection proj_;
};

TEST_F(LibraryPluginTest, TheExampleInstallsWithItsTool) {
  const fv::desk::PackageInfo* p = desk().packages()->Find(kPackage);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kLoaded);
  EXPECT_NE(desk().tools().Find("com.example.rangerings.open_sample"), nullptr);
  EXPECT_EQ(overlay_->Name(), "Range Rings");
  EXPECT_NE(overlay_->AsProperties(), nullptr);
  EXPECT_NE(overlay_->AsPersistence(), nullptr);
  EXPECT_NE(overlay_->AsHitTest(), nullptr);
}

TEST_F(LibraryPluginTest, DrawsRingsOnACpuCanvas) {
  fv::CpuCanvas canvas(512, 512);
  const fv::Status s = Draw(&canvas);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_GT(RedPixels(canvas.Buffer()), 500);

  // Ring 1 crosses the meridian one nautical mile north of the centre.
  const fv::PixelPoint ring1 = NorthOfCentre(kMetresPerNm);
  bool ink = false;
  for (int dy = -2; dy <= 2 && !ink; ++dy) {
    const unsigned char* px = canvas.Buffer().Row(ring1.y + dy) + ring1.x * 4;
    ink = px[0] > 180 && px[3] > 0;
  }
  EXPECT_TRUE(ink) << "no ring at (" << ring1.x << ", " << ring1.y << ")";
}

TEST_F(LibraryPluginTest, HitTestFindsTheCentreAndEachRing) {
  const fv::PixelPoint centre = NorthOfCentre(0);
  std::vector<fv::app::HitItem> hits = HitsAt(centre);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].overlay, overlay_.get());
  EXPECT_EQ(hits[0].feature, 0u);
  EXPECT_EQ(hits[0].hint.tool_tip, "Range rings centre");

  hits = HitsAt(NorthOfCentre(2 * kMetresPerNm));
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].feature, 2u);
  EXPECT_EQ(hits[0].hint.tool_tip, "Ring 2: 2 nm");

  EXPECT_TRUE(HitsAt(NorthOfCentre(1.5 * kMetresPerNm)).empty());
}

TEST_F(LibraryPluginTest, PropertiesRoundTripThroughTheSchema) {
  fv::app::Properties* props = overlay_->AsProperties();
  ASSERT_NE(props, nullptr);
  const std::vector<fv::app::PropertySpec>& specs = props->Describe();
  ASSERT_EQ(specs.size(), 4u);
  EXPECT_EQ(specs[0].key, "ring_count");
  EXPECT_EQ(specs[0].type, fv::app::PropertyType::kInt);
  EXPECT_EQ(specs[0].default_value.i, 3);
  EXPECT_EQ(specs[0].max, 10);
  EXPECT_EQ(specs[2].type, fv::app::PropertyType::kColor);
  EXPECT_EQ(specs[2].default_value.color.r, 255);
  EXPECT_EQ(specs[3].type, fv::app::PropertyType::kBool);
  EXPECT_TRUE(specs[3].default_value.b);

  ASSERT_TRUE(props->SetProperty("ring_count", fv::app::PropertyValue::Int(5)).ok());
  EXPECT_EQ(props->GetInt("ring_count", 0), 5);
  ASSERT_TRUE(props->SetProperty("ring_count", fv::app::PropertyValue::Int(99)).ok());
  EXPECT_EQ(props->GetInt("ring_count", 0), 10);  // the plugin clamps

  const fv::FvColor blue{0, 0, 255, 255};
  ASSERT_TRUE(props->SetProperty("line_color", fv::app::PropertyValue::Color(blue)).ok());
  EXPECT_EQ(props->GetColor("line_color", fv::FvColor{}).b, 255);
  EXPECT_EQ(props->GetColor("line_color", fv::FvColor{}).r, 0);

  const fv::Status bad = props->SetProperty("spacing_nm", fv::app::PropertyValue::Double(-1));
  EXPECT_EQ(bad.code, fv::kInvalidArg);
  EXPECT_NE(bad.message.find("spacing must be positive"), std::string::npos) << bad.message;
  EXPECT_EQ(props->SetProperty("spacing_nm", fv::app::PropertyValue::String("2")).code,
            fv::kInvalidArg);
  EXPECT_EQ(props->SetProperty("nope", fv::app::PropertyValue::Int(1)).code, fv::kNotFound);

  // A blue overlay leaves no red ink.
  fv::CpuCanvas canvas(512, 512);
  ASSERT_TRUE(Draw(&canvas).ok());
  EXPECT_EQ(RedPixels(canvas.Buffer()), 0);
}

TEST_F(LibraryPluginTest, OpensAndSavesItsDocument) {
  fv::app::Persistence* doc = overlay_->AsPersistence();
  ASSERT_NE(doc, nullptr);
  const fs::path sample = fs::path(scratch_.Path("user")) / kPackage / "resources" / "charleston.rng";
  ASSERT_TRUE(doc->FileOpen(sample.string()).ok());
  EXPECT_EQ(doc->file_spec(), sample.string());
  EXPECT_EQ(overlay_->AsProperties()->GetInt("ring_count", 0), 4);
  EXPECT_DOUBLE_EQ(overlay_->AsProperties()->GetDouble("spacing_nm", 0), 2.0);

  const std::string out = scratch_.Path("saved.rng");
  ASSERT_TRUE(doc->FileSaveAs(out, 0).ok());
  EXPECT_EQ(ReadFile(out), "center 32.77650000 -79.93110000\nrings 4\nspacing_nm 2\n");
  EXPECT_FALSE(doc->is_dirty());

  const fv::Status missing = doc->FileOpen(scratch_.Path("absent.rng"));
  EXPECT_EQ(missing.code, fv::kIoError);
  EXPECT_NE(missing.message.find("cannot read the file"), std::string::npos) << missing.message;
}

TEST_F(LibraryPluginTest, AClickMovesTheCentreAfterAFrame) {
  fv::MouseEvent e;
  e.x = 100;
  e.y = 120;
  EXPECT_FALSE(overlay_->OnMouseDown(e));  // no frame drawn yet: no view to place it in

  fv::CpuCanvas canvas(512, 512);
  ASSERT_TRUE(Draw(&canvas).ok());
  EXPECT_TRUE(overlay_->OnMouseDown(e));
  EXPECT_TRUE(overlay_->AsPersistence()->is_dirty());
  const std::vector<fv::app::HitItem> hits = HitsAt(fv::PixelPoint{100, 120});
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].feature, 0u);
}

TEST_F(LibraryPluginTest, TheToolOpensTheSampleAsAnOverlay) {
  ASSERT_TRUE(desk().Execute("tools.run.com.example.rangerings.open_sample").ok());
  EXPECT_TRUE(fake_.shell().errors.empty());
  const std::vector<std::shared_ptr<fv::Overlay>> open = desk().overlays().OfType(kType);
  ASSERT_EQ(open.size(), 1u);
  EXPECT_EQ(open[0]->AsProperties()->GetInt("ring_count", 0), 4);
}

// MARK: Refusals

class LibraryRefusalTest : public ::testing::Test {
 protected:
  /// A package holding `library` as its only library, under `platform_key`.
  void WritePackage(const std::string& library, const std::string& platform_key,
                    const std::string& class_name = "rings") {
    const fs::path dir = fs::path(scratch_.Path("user")) / "com.example.refused";
    fs::create_directories(dir / "lib");
    const std::string lib_name = fs::path(library).filename().string();
    fs::copy_file(library, dir / "lib" / lib_name);
    std::ofstream(dir / "peregrine-package.json") << R"({
  "format": 1, "id": "com.example.refused", "name": "Refused", "version": "1.0",
  "requires": { "plugin_api": 1 },
  "libraries": { ")" + platform_key + R"(": "lib/)" + lib_name + R"(" },
  "overlays": [ { "id": "com.example.refused.rings", "display_name": "Refused",
                  "implementation": { "library": ")" + class_name + R"(" } } ]
})";
  }

  /// Scans and loads the user directory, as at a launch.
  const fv::desk::PackageInfo* Launch() {
    manager_ = std::make_unique<fv::desk::PackageManager>(
        fv::desk::PackageDirs{std::string(), scratch_.Path("user")}, types_, tools_);
    std::vector<std::string> warnings;
    manager_->Scan(&warnings);
    manager_->LoadAll();
    return manager_->Find("com.example.refused");
  }

  Scratch scratch_;
  fv::app::OverlayTypeRegistry types_;
  fv::desk::ToolRegistry tools_;
  std::unique_ptr<fv::desk::PackageManager> manager_;
};

TEST_F(LibraryRefusalTest, APluginBuiltForApi2IsUnavailable) {
  WritePackage(FV_API2_PLUGIN, fv::desk::PluginPlatformKeys().front());
  const fv::desk::PackageInfo* p = Launch();
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kUnavailable);
  EXPECT_NE(p->reason.find("built for plugin API 2; this Peregrine hosts API 1"), std::string::npos)
      << p->reason;
  EXPECT_EQ(types_.Find("com.example.refused.rings"), nullptr);
}

TEST_F(LibraryRefusalTest, NoLibraryForThisPlatformIsUnavailable) {
  const std::vector<std::string> mine = fv::desk::PluginPlatformKeys();
  const std::string other = mine.front().rfind("linux", 0) == 0 ? "macos" : "linux-x86_64";
  WritePackage(FV_API2_PLUGIN, other);
  const fv::desk::PackageInfo* p = Launch();
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kUnavailable);
  EXPECT_NE(p->reason.find("no library for this platform"), std::string::npos) << p->reason;
}

TEST_F(LibraryRefusalTest, AnUnknownClassNameFails) {
  // The installed example's library is the one already loaded in this process.
  const fs::path stage = fs::path(FV_RANGERINGS_PGPKG).parent_path() / "stage" / kPackage / "lib";
  fs::path library;
  for (const auto& entry : fs::recursive_directory_iterator(stage))
    if (entry.is_regular_file()) library = entry.path();
  ASSERT_FALSE(library.empty());
  WritePackage(library.string(), fv::desk::PluginPlatformKeys().front(), "squares");
  const fv::desk::PackageInfo* p = Launch();
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kFailed);
  EXPECT_NE(p->reason.find("no overlay class 'squares'"), std::string::npos) << p->reason;
}

TEST(PluginPlatform, SelectsTheMostSpecificLibrary) {
  const std::vector<std::string> keys = fv::desk::PluginPlatformKeys();
  ASSERT_FALSE(keys.empty());
  std::map<std::string, std::string> libs;
  for (const std::string& k : keys) libs[k] = "lib/" + k;
  libs["windows-x86_64"] = "lib/win";
  EXPECT_EQ(fv::desk::SelectPluginLibrary(libs), "lib/" + keys.front());
  libs.erase(keys.front());
  EXPECT_EQ(fv::desk::SelectPluginLibrary(libs), keys.size() > 1 ? "lib/" + keys[1] : "");
}

}  // namespace
