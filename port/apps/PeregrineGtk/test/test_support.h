// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// test_support.h — a main loop spinner, a scratch directory and a fixture
/// that skips without a display, for the GTK shell's window tests.
#pragma once

#include <gtest/gtest.h>
#include <gtkmm.h>
#include <gtkmm/init.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

namespace peregrine {
namespace test {

/// Runs the main loop until `done` or about `seconds` have passed.
inline bool SpinUntil(const std::function<bool()>& done, double seconds = 10) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  while (!done()) {
    if (std::chrono::steady_clock::now() > end) return false;
    if (!g_main_context_iteration(nullptr, FALSE))
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}

/// Runs whatever the main loop has pending, idle callbacks included.
inline void Drain() {
  for (int i = 0; i < 1000 && g_main_context_iteration(nullptr, FALSE); ++i) {
  }
}

/// A scratch directory removed at the end of the test.
struct Scratch {
  std::filesystem::path dir;
  Scratch() {
    dir = std::filesystem::temp_directory_path() /
          ("peregrine_gtk_" + std::to_string(::getpid()) + "_" +
           ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
  }
  ~Scratch() {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  std::string Path(const std::string& name) const { return (dir / name).string(); }
};

/// Initialises GTK once with the user's settings, state and log redirected
/// to a scratch home; a test skips when there is no display.
class DisplayTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    if (initialised_) return;
    initialised_ = true;
    const std::filesystem::path home = std::filesystem::temp_directory_path() /
                                       ("peregrine_gtk_home_" + std::to_string(::getpid()));
    std::filesystem::create_directories(home);
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

  static inline bool initialised_ = false;
  static inline bool have_display_ = false;
};

}  // namespace test
}  // namespace peregrine
