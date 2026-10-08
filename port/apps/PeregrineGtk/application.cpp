// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "application.h"

#include <iostream>
#include <utility>

#include "map_window.h"

namespace peregrine {

Glib::RefPtr<Application> Application::create(fv::desk::LaunchOptions options) {
  return Glib::make_refptr_for_instance<Application>(new Application(std::move(options)));
}

// Non-unique: a second launch (or a --shot run beside a running app) gets its
// own window and options rather than being forwarded to the first instance.
Application::Application(fv::desk::LaunchOptions options)
    : Gtk::Application("org.peregrine.Peregrine", Gio::Application::Flags::NON_UNIQUE),
      options_(std::move(options)) {}

Application::~Application() = default;

void Application::on_activate() {
  if (window_) {
    window_->present();
    return;
  }
  window_ = std::make_unique<MapWindow>();
  add_window(*window_);
  window_->present();

  if (!options_.catalog.empty() && window_->OpenCatalog(options_.catalog) &&
      options_.has_center) {
    window_->GoTo(options_.center_lat, options_.center_lon, options_.center_scale);
  }
  if (!options_.shot.empty())
    window_->canvas().add_tick_callback(sigc::mem_fun(*this, &Application::ShotWhenReady));
}

/// Writes the `--shot` PNG on the first frame after the map has its size,
/// then quits.
bool Application::ShotWhenReady(const Glib::RefPtr<Gdk::FrameClock>&) {
  if (window_->canvas().get_width() <= 0) return G_SOURCE_CONTINUE;
  const std::string error = window_->WriteSnapshot(options_.shot);
  if (!error.empty()) {
    std::cerr << "--shot: " << error << "\n";
    exit_status_ = 1;
  }
  Glib::signal_idle().connect_once([this] { quit(); });
  return G_SOURCE_REMOVE;
}

}  // namespace peregrine
