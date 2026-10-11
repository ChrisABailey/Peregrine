// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "application.h"

#include <iostream>
#include <utility>

#include "map_window.h"

namespace peregrine {
namespace {

bool IsMapGroup(const std::string& command_id) { return command_id.rfind("map.group.", 0) == 0; }

}  // namespace

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
  window_->UpdateMenus();
  window_->present();

  bool groups_run = false;
  if (!options_.catalog.empty() && window_->OpenCatalog(options_.catalog)) {
    groups_run = true;
    // A map group is chosen before the camera, so `--center`'s scale settles
    // on that group's ladder rather than the one open at launch.
    for (const std::string& id : options_.run)
      if (IsMapGroup(id)) Run(id);
    if (options_.has_center)
      window_->GoTo(options_.center_lat, options_.center_lon, options_.center_scale);
  }
  for (const std::string& path : options_.open) {
    const std::string error = window_->host().OpenOverlayFile(path);
    if (!error.empty()) std::cerr << "--open " << path << ": " << error << "\n";
  }
  // After --open, so a command can act on the overlay just opened.
  for (const std::string& id : options_.run)
    if (!(groups_run && IsMapGroup(id))) Run(id);
  if (!options_.shot.empty())
    window_->canvas().add_tick_callback(sigc::mem_fun(*this, &Application::ShotWhenReady));
}

/// Executes a `--run` command, reporting a failure on stderr.
void Application::Run(const std::string& command_id) {
  const std::string error = window_->host().Execute(command_id);
  if (!error.empty()) std::cerr << "--run " << command_id << ": " << error << "\n";
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
