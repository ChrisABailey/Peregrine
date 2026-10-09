// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// map_window.h — one map window, the counterpart of MapWindowController.swift.
#pragma once

#include <gtkmm.h>

#include <memory>
#include <string>
#include <vector>

#include "data_sources_window.h"
#include "dialogs.h"
#include "fv_desk_host.h"
#include "main_menu.h"
#include "map_canvas.h"

namespace peregrine {

/// Releases the reference `DeskHost::Create` returned.
struct DeskHostRelease {
  void operator()(fv::desk::DeskHost* host) const { fv_desk_host_release(host); }
};

/// The map widget over a status bar showing the scale, the series drawn, the
/// ladder's message and the position under the cursor, under the menu bar and
/// with the toolbar in the header bar, both generated from DeskKit's model.
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
  /// Executes a DeskKit command; shows its error, and closes the window once
  /// the quit command has closed every document.
  void Execute(const std::string& command_id);
  /// Regenerates the menu bar, toolbar and shortcuts when the model changed.
  void UpdateMenus();
  /// Help ▸ Show Log: opens today's log file in the default viewer.
  void ShowLog();
  /// Brings up the Map Data Sources window, reusing it when it is showing.
  void ShowDataSources();
  /// The Map Data Sources window, or null when it is not open.
  DataSourcesWindow* data_sources_window() { return data_sources_.get(); }
  /// The progress dialog of a running job, or null.
  JobDialog* job_dialog() { return job_dialog_.get(); }
  /// Renders the current view and writes the window's content (map and
  /// status bar) to `path` as a PNG, then prints the status line to stderr.
  /// Returns the error message, or "".
  std::string WriteSnapshot(const std::string& path);

 private:
  void Handle(const fv::desk::HostTick& tick);
  void CatalogDidChange();
  void UpdateJob();
  void AnswerRequest();
  bool OnCloseRequest();
  void PresentError(const std::string& message, const std::string& detail);
  void PresentError(const std::string& message, const std::string& detail, Gtk::Window& parent);
  void PresentNotice(const std::string& notice);
  void RebuildToolbar();

  // Declared first so the host outlives the widgets that call it.
  std::unique_ptr<fv::desk::DeskHost, DeskHostRelease> host_;
  MapCanvas canvas_;
  CommandMenus menus_;
  Gtk::HeaderBar header_;
  Gtk::Box toolbar_{Gtk::Orientation::HORIZONTAL, 2};
  std::vector<ToolbarItem> toolbar_items_;
  std::vector<std::string> accelerated_actions_;
  Gtk::Box content_{Gtk::Orientation::VERTICAL};
  Gtk::PopoverMenuBar menu_bar_;
  Gtk::Box status_bar_{Gtk::Orientation::HORIZONTAL, 16};
  Gtk::Label scale_label_;
  Gtk::Label product_label_;
  Gtk::Label message_label_;
  Gtk::Label position_label_;
  std::unique_ptr<JobDialog> job_dialog_;
  std::unique_ptr<DataSourcesWindow> data_sources_;
};

}  // namespace peregrine
