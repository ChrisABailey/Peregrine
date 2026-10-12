// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// layers_window.h — the layer manager, the counterpart of LayersWindow.swift.
#pragma once

#include <gtkmm.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "fv_desk_host.h"

namespace peregrine {

/// Two views of the overlay stack, switched at the top. Sources lists each
/// data source with its layers under it; Layers lists every layer in drawing
/// order. Rows have a visibility check box (mixed on a source with some
/// layers shown), an icon, a name and, on layers that support it, an opacity
/// slider. Selecting a row makes its layer current; Move Up/Down and dragging
/// reorder sources, layers within a source, or (in Layers) any layer. A right
/// click shows the row's menu from DeskKit. Rows come from `DeskHost`'s snapshot.
class LayersWindow : public Gtk::Window {
 public:
  /// `host` must outlive the window; `execute` runs a menu's command.
  LayersWindow(fv::desk::DeskHost& host, Gtk::Window& parent,
               std::function<void(const std::string&)> execute);
  ~LayersWindow() override;

  /// Re-reads both views; the selection is kept where it still exists, else
  /// the current layer's row is selected.
  void Reload();
  /// Moves layer `from` to `to` in drawing order and reloads; false when refused.
  bool Move(int from, int to);
  /// Moves the selected Sources row by `delta` (-1 up, +1 down) among its
  /// siblings: a source among sources, a layer within its source.
  bool MoveSelectedSourceRow(int delta);
  /// Shows or hides the layers of source `source` in the Sources view.
  void SetExpanded(int source, bool expanded);
  /// Selects Sources row `index` and opens its menu at (`x`, `y`) in the list.
  void ShowMenuAt(int index, double x, double y);

  /// "sources" or "layers".
  void ShowView(const std::string& name) { views_.set_visible_child(name); }
  Gtk::ListBox& list() { return list_; }
  Gtk::ListBox& sources_list() { return sources_; }
  Gtk::PopoverMenu& menu() { return menu_; }
  Gtk::Button& up_button() { return up_; }
  Gtk::Button& down_button() { return down_; }
  /// The source and layer row (-1 for the source's own row) of Sources row `index`.
  std::pair<int, int> SourceEntry(int index) const;

 private:
  /// What a Sources row shows: a source, or one of its layers.
  struct Entry {
    int source = -1;
    int layer = -1;  ///< row in the layer snapshot; -1 for the source row
    int index = -1;  ///< the layer's place in its source
  };
  /// Which Sources row to select after a reload.
  struct Selection {
    std::string source;  ///< `SourceKey`
    int index = -1;      ///< a layer's place in the source; -1 for the source row
  };

  void ReloadLayers();
  void ReloadSources();
  Gtk::Widget* MakeLayerRow(int row, bool indented, int drag_value);
  Gtk::Widget* MakeSourceRow(int source, int drag_value);
  /// The path, or the name of a source without one.
  std::string SourceKey(int source) const;
  bool MoveSourceRow(int from_entry, int to_entry);
  void UpdateButtons();
  void OnSelected();
  void OnSourceSelected();

  fv::desk::DeskHost& host_;
  std::function<void(const std::string&)> execute_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 8};
  Gtk::StackSwitcher switcher_;
  Gtk::Stack views_;
  Gtk::ScrolledWindow scroll_;
  Gtk::ListBox list_;
  Gtk::ScrolledWindow sources_scroll_;
  Gtk::ListBox sources_;
  Gtk::PopoverMenu menu_;
  Glib::RefPtr<Gio::SimpleActionGroup> menu_actions_;
  Gtk::Box buttons_{Gtk::Orientation::HORIZONTAL, 6};
  Gtk::Button up_{"Move Up"};
  Gtk::Button down_{"Move Down"};
  std::vector<Entry> entries_;
  std::set<std::string> collapsed_;
  Selection selected_;
  /// Set while the lists are rebuilt, so the selection they restore is not a click.
  bool reloading_ = false;
};

}  // namespace peregrine
