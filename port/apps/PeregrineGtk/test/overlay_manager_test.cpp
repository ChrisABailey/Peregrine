// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tools ▸ Overlay Manager and the ENC options page's import buttons, driven
/// through the map window: the C example package installs after its
/// confirmation and adds its tool to the menus; disable shows the restart
/// banner; remove asks first.

#include <filesystem>
#include <string>

#include "map_window.h"
#include "test_support.h"

namespace peregrine {
namespace {

namespace fs = std::filesystem;
using test::Scratch;
using test::SpinUntil;

constexpr char kPackage[] = "com.example.rangerings";

/// A map window over an empty user packages directory, shown so its ticks run.
class OverlayManagerTest : public test::DisplayTest {
 protected:
  void SetUp() override {
    test::DisplayTest::SetUp();
    if (IsSkipped()) return;
    const char* data = std::getenv("XDG_DATA_HOME");
    ASSERT_NE(data, nullptr);
    packages_ = fs::path(data) / "peregrine" / "packages";
    fs::remove_all(packages_);
    window_ = std::make_unique<MapWindow>();
    window_->present();
  }
  void TearDown() override {
    window_.reset();
    std::error_code ec;
    fs::remove_all(packages_, ec);
  }

  MapWindow& window() { return *window_; }
  fv::desk::DeskHost& host() { return window_->host(); }

  /// Opens the manager through its command, as the Tools menu does.
  OverlayManagerWindow& OpenManager() {
    window().Execute("tools.overlay_manager");
    EXPECT_TRUE(SpinUntil([&] { return window().overlay_manager_window() != nullptr; }));
    return *window().overlay_manager_window();
  }

  /// The row of `id` in the manager, or -1.
  static int RowOf(const OverlayManagerWindow& m, const std::string& id) {
    for (size_t i = 0; i < m.rows().size(); ++i)
      if (m.rows()[i].id == id) return static_cast<int>(i);
    return -1;
  }

  fs::path packages_;
  std::unique_ptr<MapWindow> window_;
};

TEST_F(OverlayManagerTest, ToolsMenuOpensTheManager) {
  EXPECT_TRUE(host().IsEnabled("tools.overlay_manager"));
  OverlayManagerWindow& m = OpenManager();
  EXPECT_EQ(m.get_title(), "Overlay Manager");
  EXPECT_TRUE(m.rows().empty());
  EXPECT_NE(m.detail_text().find("No overlay packages are installed"), std::string::npos);
  EXPECT_FALSE(m.enable_button().get_sensitive());
  EXPECT_FALSE(m.remove_button().get_sensitive());
  EXPECT_FALSE(m.banner().get_visible());
}

TEST_F(OverlayManagerTest, InstallAsksThenLoadsLive) {
  OverlayManagerWindow& m = OpenManager();
  // Cancel installs nothing.
  Glib::signal_idle().connect_once([&m] { m.confirmation()->Respond(0); });
  m.ConfirmInstall(FV_RANGERINGS_PGPKG);
  ASSERT_NE(m.confirmation(), nullptr);
  EXPECT_EQ(RowOf(m, kPackage), -1);

  Glib::signal_idle().connect_once([&m] { m.confirmation()->Respond(1); });
  m.ConfirmInstall(FV_RANGERINGS_PGPKG);
  const int row = RowOf(m, kPackage);
  ASSERT_GE(row, 0);
  EXPECT_EQ(m.rows()[row].state, "Loaded");
  EXPECT_EQ(m.rows()[row].location, "User");
  EXPECT_TRUE(fs::is_directory(packages_ / kPackage));
  // The installed package is selected and described.
  EXPECT_NE(m.detail_text().find("Runs code: C library"), std::string::npos);
  EXPECT_NE(m.detail_text().find("Range Rings"), std::string::npos);
  EXPECT_TRUE(m.remove_button().get_sensitive());
  EXPECT_EQ(m.enable_button().get_label(), "Disable");
  // Its overlay type and tool registered live.
  EXPECT_TRUE(host().OpensAsOverlay("charleston.rng"));
  EXPECT_TRUE(host().IsEnabled("tools.run.com.example.rangerings.open_sample"));
}

TEST_F(OverlayManagerTest, DisableShowsTheRestartBannerAndRemoveAsks) {
  OverlayManagerWindow& m = OpenManager();
  m.Install(FV_RANGERINGS_PGPKG);
  ASSERT_TRUE(m.Select(kPackage));
  m.ToggleEnabled();
  const int row = RowOf(m, kPackage);
  ASSERT_GE(row, 0);
  EXPECT_FALSE(m.rows()[row].enabled);
  EXPECT_EQ(m.rows()[row].pending, "Disabled at relaunch");
  EXPECT_TRUE(m.banner().get_visible());
  EXPECT_EQ(m.enable_button().get_label(), "Enable");

  Glib::signal_idle().connect_once([&m] { m.confirmation()->Respond(0); });
  m.RemoveSelected();
  EXPECT_GE(RowOf(m, kPackage), 0);
  EXPECT_EQ(m.rows()[RowOf(m, kPackage)].pending, "Disabled at relaunch");

  Glib::signal_idle().connect_once([&m] { m.confirmation()->Respond(1); });
  m.RemoveSelected();
  const int after = RowOf(m, kPackage);
  // A loaded package is removed at the next launch; an unloaded one at once.
  if (after >= 0) EXPECT_FALSE(m.rows()[after].pending.empty());
}

TEST_F(OverlayManagerTest, OpeningAPackageFileShowsTheConfirmation) {
  Glib::signal_idle().connect_once([this] {
    ASSERT_NE(window().overlay_manager_window(), nullptr);
    window().overlay_manager_window()->confirmation()->Respond(1);
  });
  window().InstallPackage(FV_RANGERINGS_PGPKG);
  ASSERT_NE(window().overlay_manager_window(), nullptr);
  EXPECT_TRUE(window().overlay_manager_window()->get_visible());
  EXPECT_GE(RowOf(*window().overlay_manager_window(), kPackage), 0);
}

TEST_F(OverlayManagerTest, NotAPackageIsRefusedWithoutAsking) {
  Scratch scratch;
  OverlayManagerWindow& m = OpenManager();
  m.ConfirmInstall(scratch.Path("missing.pgpkg"));
  EXPECT_EQ(m.confirmation(), nullptr);
  EXPECT_TRUE(m.rows().empty());
}

TEST_F(OverlayManagerTest, EncPageButtonsImportThroughTheCore) {
  Scratch scratch;
  // Import needs a catalog file; Map Data Sources creates one through a save request.
  struct Answer {
    fv::desk::DeskHost* host;
    std::string catalog;
    int directories = 0;
  } answer{&host(), scratch.Path("built.sqlite")};
  host().SetRequestHandler(
      [](void* context) {
        auto& a = *static_cast<Answer*>(context);
        if (a.host->PendingRequest().kind == fv::desk::kRequestChooseDirectory) {
          ++a.directories;  // no answer: cancelled
          return;
        }
        a.host->AnswerPath(a.catalog);
        a.host->AnswerIndex(0);
      },
      &answer);
  window().Execute("map.sources");
  ASSERT_EQ(host().CatalogPath(), answer.catalog);

  window().Execute("map.options");
  ASSERT_TRUE(SpinUntil([&] { return window().options_window(fv::desk::kOptionsMap) != nullptr; }));
  OptionsWindow& options = *window().options_window(fv::desk::kOptionsMap);
  int page = -1;
  for (int p = 0; p < host().OptionsPageCount(fv::desk::kOptionsMap); ++p)
    if (host().OptionsActionCount(fv::desk::kOptionsMap, p) > 0) page = p;
  ASSERT_GE(page, 0) << "no options page has buttons";
  options.SelectPage(page);
  Gtk::Button* zip = options.action_button("enc.import_zip");
  Gtk::Button* folder = options.action_button("enc.import_folder");
  ASSERT_NE(zip, nullptr);
  ASSERT_NE(folder, nullptr);
  EXPECT_EQ(zip->get_label(), "Import Zip…");
  EXPECT_EQ(folder->get_label(), "Import Folder…");
  EXPECT_TRUE(folder->get_sensitive());

  // A cancelled folder choice starts nothing.
  options.RunAction("enc.import_folder");
  EXPECT_EQ(answer.directories, 1);
  EXPECT_FALSE(host().JobActive());
  host().SetRequestHandler(nullptr, nullptr);
  options.close();
}

}  // namespace
}  // namespace peregrine
