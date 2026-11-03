// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The native plugin kind end to end, against the shared core: the
// rangerings-cpp example installs, its editor places rings by click through
// FakeDesk, and a plugin stamped with another build id is never loaded.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

#include "fv_desk_fake.h"
#include "fv_desk_library_plugin.h"
#include "fv_desk_native_plugin.h"
#include "fv_desk_packages.h"
#include "fvkit/app/properties.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/raster.h"
#include "scratch.h"

namespace {

namespace fs = std::filesystem;
using fv::desk::Menu;
using fv::desk::MenuItem;
using fv::desk::PackageState;
using fv::desk::test::Scratch;

constexpr char kPackage[] = "com.example.nativerings";
constexpr char kType[] = "com.example.nativerings.rings";
constexpr char kTool[] = "com.example.nativerings.at_center";

/// Pixels whose colour is mostly red, the example's ring colour.
int RedPixels(const fv::PixelBuffer& buf) {
  int n = 0;
  for (int y = 0; y < buf.Height(); ++y) {
    const unsigned char* row = buf.Row(y);
    for (int x = 0; x < buf.Width(); ++x)
      if (row[x * 4] > 180 && row[x * 4 + 1] < 90 && row[x * 4 + 2] < 90 && row[x * 4 + 3] > 0) ++n;
  }
  return n;
}

class NativePluginTest : public ::testing::Test {
 protected:
  void SetUp() override {
    desk().LoadPackages(fv::desk::PackageDirs{std::string(), scratch_.Path("user")});
    fv::desk::InstallResult r;
    const fv::Status s = desk().InstallPackage(FV_NATIVERINGS_PGPKG, &r);
    ASSERT_TRUE(s.ok()) << s.message;
    ASSERT_TRUE(r.live);
    auto& mv = desk().map_view();
    mv.SetViewport(mv.View().WithCamera(fv::GeoPoint{32.7765, -79.9311}, 100000, 0));
  }

  fv::desk::Desk& desk() { return fake_.desk(); }

  int RenderRed() {
    fv::PixelBuffer buf;
    EXPECT_TRUE(fake_.Render(&buf).ok());
    return RedPixels(buf);
  }

  /// The editor palette's commands by label.
  std::map<std::string, std::string> EditorTools() {
    std::map<std::string, std::string> by_label;
    const Menu* editor = desk().menus().Find("editor");
    if (editor == nullptr) return by_label;
    for (const MenuItem& item : editor->items)
      if (!item.is_separator()) by_label[item.label] = item.command_id;
    return by_label;
  }

  Scratch scratch_;
  fv::desk::FakeDesk fake_{400, 300};
};

TEST_F(NativePluginTest, TheExampleCarriesThisBuildId) {
  std::string id;
  ASSERT_TRUE(fv::desk::ReadNativeBuildId(FV_NATIVERINGS_LIB, &id).ok());
  EXPECT_EQ(id, fv::desk::NativeHostBuildId());
}

TEST_F(NativePluginTest, InstallsLiveWithItsEditorAndTool) {
  const fv::desk::PackageInfo* p = desk().packages()->Find(kPackage);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kLoaded) << p->reason;
  const fv::app::OverlayTypeDesc* type = desk().types().Find(kType);
  ASSERT_NE(type, nullptr);
  EXPECT_TRUE(type->editor_factory);
  EXPECT_NE(desk().tools().Find(kTool), nullptr);
}

TEST_F(NativePluginTest, TheEditorPlacesRingsWhereClicked) {
  ASSERT_TRUE(fake_.Run({std::string("editor.mode.") + kType}).ok());
  ASSERT_TRUE(desk().Editing());
  std::shared_ptr<fv::Overlay> rings = desk().overlays().FirstOfType(kType);
  ASSERT_NE(rings, nullptr);
  std::map<std::string, std::string> tools = EditorTools();
  ASSERT_EQ(tools.count("Place Rings"), 1u);
  ASSERT_EQ(tools.count("Clear Rings"), 1u);
  EXPECT_TRUE(desk().commands().IsEnabled(tools["Place Rings"]));
  EXPECT_FALSE(desk().commands().IsEnabled(tools["Clear Rings"]));
  EXPECT_EQ(RenderRed(), 0);

  // Without Place Rings armed a click is not the overlay's.
  EXPECT_FALSE(desk().OverlayPointerDown(200, 150));
  ASSERT_TRUE(desk().Execute(tools["Place Rings"]).ok());
  EXPECT_TRUE(desk().commands().IsChecked(tools["Place Rings"]));
  ASSERT_TRUE(desk().OverlayPointerDown(200, 150));
  desk().OverlayPointerUp(200, 150);
  EXPECT_FALSE(desk().commands().IsChecked(tools["Place Rings"]));
  EXPECT_TRUE(desk().commands().IsEnabled(tools["Clear Rings"]));
  const int three = RenderRed();
  EXPECT_GT(three, 200);

  // The properties page is the overlay's own, as for a built-in.
  fv::app::Properties* props = rings->AsProperties();
  ASSERT_NE(props, nullptr);
  ASSERT_TRUE(props->SetProperty("ring_count", fv::app::PropertyValue::Int(5)).ok());
  EXPECT_GT(RenderRed(), three);

  ASSERT_TRUE(desk().Execute(tools["Clear Rings"]).ok());
  EXPECT_EQ(RenderRed(), 0);
}

TEST_F(NativePluginTest, TheToolAddsRingsAtTheViewCentre) {
  // With no rings overlay open the tool fails, and the Desk reports it.
  ASSERT_TRUE(desk().Execute(std::string("tools.run.") + kTool).ok());
  ASSERT_EQ(fake_.shell().errors.size(), 1u);
  EXPECT_EQ(fake_.shell().errors[0].code, fv::kNotFound);
  ASSERT_TRUE(fake_.Run({std::string("editor.mode.") + kType}).ok());
  const int before = fake_.shell().invalidate_calls;
  ASSERT_TRUE(desk().Execute(std::string("tools.run.") + kTool).ok());
  EXPECT_GT(fake_.shell().invalidate_calls, before);
  EXPECT_GT(RenderRed(), 200);
}

TEST(NativeRefusal, AnotherBuildIdIsRefusedBeforeLoading) {
  std::string id;
  ASSERT_TRUE(fv::desk::ReadNativeBuildId(FV_STALE_NATIVE, &id).ok());
  EXPECT_NE(id, fv::desk::NativeHostBuildId());

  Scratch scratch;
  const fs::path dir = scratch.Path("user") + "/com.example.stale";
  fs::create_directories(dir / "lib");
  const std::string lib_name = fs::path(FV_STALE_NATIVE).filename().string();
  fs::copy_file(FV_STALE_NATIVE, dir / "lib" / lib_name);
  std::ofstream(dir / "peregrine-package.json") << R"({
  "format": 1, "id": "com.example.stale", "name": "Stale", "version": "1.0",
  "libraries": { ")" + fv::desk::PluginPlatformKeys().front() + R"(": "lib/)" + lib_name + R"(" },
  "overlays": [ { "id": "com.example.stale.rings", "display_name": "Stale",
                  "implementation": { "native": "rings" } } ]
})";

  fv::app::OverlayTypeRegistry types;
  fv::desk::ToolRegistry tools;
  fv::desk::PackageManager manager(fv::desk::PackageDirs{std::string(), scratch.Path("user")},
                                   types, tools);
  std::vector<std::string> warnings;
  manager.Scan(&warnings);
  manager.LoadAll();
  const fv::desk::PackageInfo* p = manager.Find("com.example.stale");
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kUnavailable);
  EXPECT_NE(p->reason.find("built for Peregrine " + id + "; this is " + fv::desk::NativeHostBuildId()),
            std::string::npos)
      << p->reason;
  EXPECT_EQ(types.Find("com.example.stale.rings"), nullptr);
  // Its static initialiser never ran.
  EXPECT_EQ(std::getenv("FV_STALE_NATIVE_LOADED"), nullptr);
}

TEST(NativeRefusal, AFileWithoutABuildIdIsNotNative) {
  Scratch scratch;
  const std::string path = scratch.Path("plain.dylib");
  std::ofstream(path) << "not a plugin";
  std::string id;
  EXPECT_EQ(fv::desk::ReadNativeBuildId(path, &id).code, fv::kNotFound);
}

}  // namespace
