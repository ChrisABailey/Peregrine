// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// properties_window.h — the read-only Properties sheet of a layer or a source.
#pragma once

#include <gtkmm.h>

#include "fv_desk_host.h"

namespace peregrine {

/// The sheet `DeskHost` last asked for: a heading per section and, under it,
/// one line per property, its value selectable for copying.
class PropertiesWindow : public Gtk::Window {
 public:
  /// `host` must outlive the window.
  PropertiesWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  /// Re-reads the title and rows.
  void Reload();

  Gtk::Grid& grid() { return grid_; }

 private:
  fv::desk::DeskHost& host_;
  Gtk::ScrolledWindow scroll_;
  Gtk::Grid grid_;
};

}  // namespace peregrine
