// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// application.h — the application, the counterpart of AppDelegate.swift.
#pragma once

#include <gtkmm.h>

#include <memory>

#include "fv_desk_launch.h"

namespace peregrine {

class MapWindow;

/// One map window, set up from the launch options: `--catalog` opens a
/// catalog, `--center` moves the camera, `--shot` writes the window once the
/// first frame is drawn and quits.
class Application : public Gtk::Application {
 public:
  static Glib::RefPtr<Application> create(fv::desk::LaunchOptions options);
  ~Application() override;

 protected:
  explicit Application(fv::desk::LaunchOptions options);
  void on_activate() override;

 private:
  bool ShotWhenReady(const Glib::RefPtr<Gdk::FrameClock>& clock);

  fv::desk::LaunchOptions options_;
  std::unique_ptr<MapWindow> window_;
  int exit_status_ = 0;

 public:
  /// Non-zero when `--shot` failed.
  int exit_status() const { return exit_status_; }
};

}  // namespace peregrine
