// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// dialogs_test.cpp — the GTK answers to the host's questions, the Map Data
/// Sources window and the job progress dialog, driven through a real
/// DeskHost and a shown map window. Needs a display (run under xvfb-run).

#include "dialogs.h"
#include "map_window.h"

#include <gtest/gtest.h>
#include <gtkmm/init.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace peregrine {
namespace {

/// Runs the main loop until `done` or about `seconds` have passed.
bool SpinUntil(const std::function<bool()>& done, double seconds = 10) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  while (!done()) {
    if (std::chrono::steady_clock::now() > end) return false;
    if (!g_main_context_iteration(nullptr, FALSE))
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}

/// A scratch directory removed at the end of the test.
struct Scratch {
  fs::path dir;
  Scratch() {
    dir = fs::temp_directory_path() /
          ("peregrine_gtk_dialogs_" + std::to_string(::getpid()) + "_" +
           ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(dir);
    fs::create_directories(dir);
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

class DialogsTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    // The user settings and the log go to the scratch home, not the user's.
    const fs::path home = fs::temp_directory_path() /
                          ("peregrine_gtk_dialogs_home_" + std::to_string(::getpid()));
    fs::create_directories(home);
    setenv("XDG_CONFIG_HOME", (home / "config").c_str(), 1);
    setenv("XDG_STATE_HOME", (home / "state").c_str(), 1);
    setenv("XDG_DATA_HOME", (home / "data").c_str(), 1);
    setenv("GSK_RENDERER", "cairo", 0);
    have_display_ = gtk_init_check();
    if (have_display_) Gtk::init_gtkmm_internals();
  }

  void SetUp() override {
    if (!have_display_) GTEST_SKIP() << "no display; run under xvfb-run";
  }

  static bool have_display_;
};

bool DialogsTest::have_display_ = false;

TEST_F(DialogsTest, ChoiceDialogReturnsTheButtonFromANestedLoop) {
  Gtk::Window parent;
  parent.present();
  ChoiceDialog d(parent, "Message", "Detail", {"A", "B", "C"}, 2, 1);
  Glib::signal_idle().connect_once([&d] { d.Respond(2); });
  EXPECT_EQ(d.Run(), 2);
  EXPECT_FALSE(d.get_visible());

  // Closing the window is the cancel button.
  Glib::signal_idle().connect_once([&d] { d.close(); });
  EXPECT_EQ(d.Run(), 1);
}

TEST_F(DialogsTest, ChoiceDialogOffersRows) {
  Gtk::Window parent;
  ChoiceDialog d(parent, "Select", std::string(), {"Cancel", "OK"}, 1, 0);
  EXPECT_EQ(d.selected_row(), -1);
  d.SetRows({"first", "second"});
  EXPECT_EQ(d.selected_row(), 0);
  d.SetRows({});
  EXPECT_EQ(d.selected_row(), -1);
}

TEST_F(DialogsTest, FileRequestsCarryTheirFilters) {
  MapWindow window;
  fv::desk::DeskHost& host = window.host();
  struct Seen {
    fv::desk::DeskHost* host;
    fv::desk::HostRequestKind kind = fv::desk::kRequestNone;
    std::vector<std::string> names;
    std::vector<std::string> filters;
  } seen{&host};
  host.SetRequestHandler(
      [](void* context) {
        auto& s = *static_cast<Seen*>(context);
        s.kind = s.host->PendingRequest().kind;
        auto store = RequestFilters(*s.host);
        if (!store) return;
        for (guint i = 0; i < store->get_n_items(); ++i) {
          s.names.push_back(store->get_item(i)->get_name());
          s.filters.push_back(Glib::wrap(gtk_file_filter_to_gvariant(store->get_item(i)->gobj()))
                                  .print());
        }
      },
      &seen);
  EXPECT_EQ(host.Execute("map.catalog_open"), "");
  EXPECT_EQ(seen.kind, fv::desk::kRequestChooseOpen);
  ASSERT_EQ(seen.names.size(), 2u);
  EXPECT_EQ(seen.names[0], "Peregrine Map Catalog (*.sqlite)");
  EXPECT_NE(seen.filters[0].find("'*.sqlite'"), std::string::npos) << seen.filters[0];
  EXPECT_NE(seen.filters[1].find("'*.db'"), std::string::npos) << seen.filters[1];
  host.SetRequestHandler(nullptr, nullptr);
}

TEST(AskSave, ButtonsMapToTheHostsAnswers) {
  // Left to right: Don't Save, Cancel, Save; the host takes 0 save, 1 discard, 2 cancel.
  EXPECT_EQ(AskSaveAnswer(0), 1);
  EXPECT_EQ(AskSaveAnswer(1), 2);
  EXPECT_EQ(AskSaveAnswer(2), 0);
  EXPECT_EQ(AskSaveAnswer(7), 2);
}

TEST_F(DialogsTest, DataSourcesGenerateCoverageWithProgress) {
  const char* data = std::getenv("FVW_TESTDATA_DIR");
  if (!data || !fs::is_directory(fs::path(data) / "geotiff"))
    GTEST_SKIP() << "FVW_TESTDATA_DIR has no sample data";
  Scratch scratch;
  const std::string catalog = (scratch.dir / "built.sqlite").string();

  MapWindow window;
  window.present();
  fv::desk::DeskHost& host = window.host();
  // Answers the new-catalog save request as the file dialog would.
  struct Answer {
    fv::desk::DeskHost* host;
    std::string path;
    int asked = 0;
  } answer{&host, catalog};
  host.SetRequestHandler(
      [](void* context) {
        auto& a = *static_cast<Answer*>(context);
        ++a.asked;
        a.host->AnswerPath(a.path);
        a.host->AnswerIndex(0);
      },
      &answer);

  window.Execute("map.sources");
  EXPECT_EQ(answer.asked, 1);
  ASSERT_TRUE(SpinUntil([&] { return window.data_sources_window() != nullptr; }));
  DataSourcesWindow& sources = *window.data_sources_window();
  EXPECT_EQ(host.CatalogPath(), catalog);
  EXPECT_EQ(sources.list().get_row_at_index(0), nullptr);

  sources.AddFolders({fs::absolute(data).string()});
  ASSERT_NE(sources.list().get_row_at_index(0), nullptr);
  EXPECT_EQ(sources.list().get_row_at_index(1), nullptr);

  sources.Generate();
  EXPECT_TRUE(host.JobActive());
  EXPECT_FALSE(sources.get_visible());
  bool saw_progress = false;
  ASSERT_TRUE(SpinUntil(
      [&] {
        saw_progress |= window.job_dialog() != nullptr;
        return !host.JobActive();
      },
      120));
  ASSERT_TRUE(SpinUntil([&] { return window.job_dialog() == nullptr; }));
  EXPECT_TRUE(saw_progress);
  ASSERT_TRUE(SpinUntil([&] { return !host.StatusProduct().empty(); }));
  host.SetRequestHandler(nullptr, nullptr);
}

}  // namespace
}  // namespace peregrine
