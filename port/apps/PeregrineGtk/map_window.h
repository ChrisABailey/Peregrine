// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// map_window.h — one map window, the counterpart of MapWindowController.swift.
#pragma once

#include <gtkmm.h>

#include <memory>
#include <string>

#include "fv_desk_host.h"
#include "map_canvas.h"

namespace peregrine {

/// Releases the reference `DeskHost::Create` returned.
struct DeskHostRelease {
  void operator()(fv::desk::DeskHost* host) const { fv_desk_host_release(host); }
};

/// The map widget over a status bar showing the scale, the series drawn, the
/// ladder's message and the position under the cursor.
class MapWindow : public Gtk::ApplicationWindow {
 public:
  MapWindow();
  ~MapWindow() override;

  fv::desk::DeskHost& host() { return *host_; }
  MapCanvas& canvas() { return canvas_; }

  /// Opens a catalog; on failure shows the error and keeps the current one.
  bool OpenCatalog(const std::string& path);
  /// Moves the camera; a `scale` of 0 keeps the current one.
  void GoTo(double lat, double lon, double scale);
  void RefreshStatus();
  /// Renders the current view and writes the window's content (map and
  /// status bar) to `path` as a PNG, then prints the status line to stderr.
  /// Returns the error message, or "".
  std::string WriteSnapshot(const std::string& path);

 private:
  void Handle(const fv::desk::HostTick& tick);
  void CatalogDidChange();
  bool OnCloseRequest();
  void PresentError(const std::string& message, const std::string& detail);
  void PresentNotice(const std::string& notice);

  // Declared first so the host outlives the widgets that call it.
  std::unique_ptr<fv::desk::DeskHost, DeskHostRelease> host_;
  MapCanvas canvas_;
  Gtk::HeaderBar header_;
  Gtk::Box content_{Gtk::Orientation::VERTICAL};
  Gtk::Box status_bar_{Gtk::Orientation::HORIZONTAL, 16};
  Gtk::Label scale_label_;
  Gtk::Label product_label_;
  Gtk::Label message_label_;
  Gtk::Label position_label_;
};

}  // namespace peregrine
