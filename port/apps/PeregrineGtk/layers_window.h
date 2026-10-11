// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// layers_window.h — the layers panel, the counterpart of LayersWindow.swift.
#pragma once

#include <gtkmm.h>

#include "fv_desk_host.h"

namespace peregrine {

/// The overlay stack top first, each row with a visibility check box, its
/// icon and name and, for layers that support it, an opacity slider.
/// Selecting a row makes that overlay current; Move Up/Down and dragging a
/// row reorder the stack. Rows come from `DeskHost`'s layer snapshot.
class LayersWindow : public Gtk::Window {
 public:
  /// `host` must outlive the window.
  LayersWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  /// Re-reads the rows; the current overlay's row is selected.
  void Reload();
  /// Moves the layer at `from` to `to` and reloads; false when the host
  /// refused.
  bool Move(int from, int to);

  Gtk::ListBox& list() { return list_; }
  Gtk::Button& up_button() { return up_; }
  Gtk::Button& down_button() { return down_; }

 private:
  Gtk::Widget* MakeRow(int row);
  void UpdateButtons();
  void OnSelected();

  fv::desk::DeskHost& host_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 8};
  Gtk::ScrolledWindow scroll_;
  Gtk::ListBox list_;
  Gtk::Box buttons_{Gtk::Orientation::HORIZONTAL, 6};
  Gtk::Button up_{"Move Up"};
  Gtk::Button down_{"Move Down"};
  /// Set while the list is rebuilt, so the selection it restores is not a click.
  bool reloading_ = false;
};

}  // namespace peregrine
