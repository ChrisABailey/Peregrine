// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "application.h"

#include <cctype>
#include <iostream>
#include <utility>

#include "map_window.h"

namespace peregrine {
namespace {

bool IsMapGroup(const std::string& command_id) { return command_id.rfind("map.group.", 0) == 0; }

/// True for an overlay package file, which installs rather than opens.
bool IsPackageFile(const std::string& path) {
  constexpr char kSuffix[] = ".pgpkg";
  constexpr size_t n = sizeof kSuffix - 1;
  if (path.size() < n) return false;
  std::string tail = path.substr(path.size() - n);
  for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return tail == kSuffix;
}

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
  // Files from the desktop launcher; the window is up, so an install can ask.
  if (!options_.files.empty() && options_.shot.empty()) {
    Glib::signal_idle().connect_once([this] {
      for (const std::string& path : options_.files) OpenFile(path);
    });
  }
  if (!options_.shot.empty())
    window_->canvas().add_tick_callback(sigc::mem_fun(*this, &Application::ShotWhenReady));
}

/// Opens a file the way the macOS app opens one from Finder: a .pgpkg on the
/// Overlay Manager's install confirmation, an overlay file as an overlay,
/// anything else as a catalog.
void Application::OpenFile(const std::string& path) {
  if (IsPackageFile(path)) {
    window_->InstallPackage(path);
    return;
  }
  fv::desk::DeskHost& host = window_->host();
  if (host.OpensAsOverlay(path)) {
    const std::string error = host.OpenOverlayFile(path);
    if (!error.empty()) std::cerr << path << ": " << error << "\n";
    return;
  }
  window_->OpenCatalog(path);
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
  // A window a `--run` command opens takes the main window's focus, and its
  // content waits one frame for a new allocation; snapshot on a later frame.
  constexpr int kShotAttempts = 10;
  if (!error.empty() && ++shot_attempts_ < kShotAttempts) return G_SOURCE_CONTINUE;
  if (!error.empty()) {
    std::cerr << "--shot: " << error << "\n";
    exit_status_ = 1;
  }
  Glib::signal_idle().connect_once([this] { quit(); });
  return G_SOURCE_REMOVE;
}

}  // namespace peregrine
