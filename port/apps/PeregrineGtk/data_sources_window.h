// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// data_sources_window.h — the Map Data Sources window, the counterpart of
/// DataSourcesWindow.swift.
#pragma once

#include <gtkmm.h>

#include <functional>
#include <string>
#include <vector>

#include "fv_desk_host.h"

namespace peregrine {

/// The directories Generate Coverage scans, with Add…, Remove and Generate
/// Coverage. The list lives in the catalog; the window re-reads it from the
/// host after every change.
class DataSourcesWindow : public Gtk::Window {
 public:
  /// Shows an error from the core over the given window.
  using ErrorFn =
      std::function<void(const std::string& message, const std::string& detail, Gtk::Window&)>;

  /// `host` must outlive the window.
  DataSourcesWindow(fv::desk::DeskHost& host, Gtk::Window& parent, ErrorFn present_error);

  /// Re-reads the list and the buttons' state.
  void Reload();

  /// Adds each directory, stopping at the first error, then reloads.
  void AddFolders(const std::vector<std::string>& paths);
  /// Removes the selected directory.
  void RemoveSelected();
  /// Starts Generate Coverage and closes; the map window's progress dialog
  /// takes over.
  void Generate();

  /// The list's rows, for tests.
  Gtk::ListBox& list() { return list_; }

 private:
  void ChooseFolders();
  void UpdateButtons();

  fv::desk::DeskHost& host_;
  ErrorFn present_error_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 12};
  Gtk::Label intro_;
  Gtk::ScrolledWindow scroll_;
  Gtk::ListBox list_;
  Gtk::Box buttons_{Gtk::Orientation::HORIZONTAL, 8};
  Gtk::Button add_{"Add…"};
  Gtk::Button remove_{"Remove"};
  Gtk::Button close_{"Close"};
  Gtk::Button generate_{"Generate Coverage"};
};

}  // namespace peregrine
