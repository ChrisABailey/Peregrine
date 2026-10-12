// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// layer_style_window.h — the style editor: as the Symbology and Labels tabs
/// of a layer's Properties, and as the Style This Feature… dialog, the
/// counterpart of LayerStyleWindow.swift.
#pragma once

#include <gtkmm.h>

#include <functional>
#include <string>

#include "fv_desk_host.h"

namespace peregrine {

/// Renderer and field, the legend of classes with swatches and a symbol
/// editor for the selected class (symbology), and the label settings, over
/// `DeskHost`'s style model. Edits go to the model's draft as they are made.
class LayerStylePanel : public Gtk::Box {
 public:
  enum class Part { kAll, kSymbology, kLabels };
  /// Receives each edit's error message, "" for success.
  using ReportFn = std::function<void(const std::string& error)>;

  /// `host` must outlive the panel.
  LayerStylePanel(fv::desk::DeskHost& host, Part part, ReportFn report);

  /// Re-reads the whole model, keeping the selected legend row where it can.
  void Reload();
  /// Selects legend row `row` and shows its symbol.
  void SelectEntry(int row);

  Gtk::DropDown& renderer() { return renderer_; }
  Gtk::DropDown& field() { return field_; }
  Gtk::Button& classify_button() { return classify_; }
  Gtk::ListBox& legend() { return legend_; }
  /// The symbol editor's grid, rebuilt for the selected entry.
  Gtk::Grid* symbol_grid() { return symbol_grid_; }
  Gtk::DropDown& label_field() { return label_field_; }
  Gtk::SpinButton& label_size() { return label_size_; }

 private:
  void BuildSymbology();
  Gtk::Widget* BuildLabels();
  Gtk::Widget* MakeLegendRow(int i);
  Gtk::Widget* MakePropertyEditor(int k);
  void ReloadSymbol();
  void Report(const std::string& error);
  void ReloadIfPending();
  void ApplyLabel();

  fv::desk::DeskHost& host_;
  Part part_;
  ReportFn report_;
  Gtk::Box renderer_row_{Gtk::Orientation::HORIZONTAL, 6};
  Gtk::DropDown renderer_;
  Gtk::Label by_{"by"};
  Gtk::DropDown field_;
  Gtk::Button classify_{"Classify"};
  Gtk::Box graded_row_{Gtk::Orientation::HORIZONTAL, 6};
  Gtk::DropDown classes_;
  Gtk::DropDown mode_;
  Gtk::DropDown vary_;
  Gtk::ScrolledWindow legend_scroll_;
  Gtk::ListBox legend_;
  Gtk::Frame symbol_frame_{"Symbol"};
  Gtk::Grid* symbol_grid_ = nullptr;
  Gtk::Box entry_buttons_{Gtk::Orientation::HORIZONTAL, 6};
  Gtk::Button add_{"+"};
  Gtk::Button remove_{"−"};
  Gtk::Button clear_override_{"Use Layer Style"};
  Gtk::DropDown label_field_;
  Gtk::SpinButton label_size_;
  int selected_ = 0;
  /// Set while controls are filled from the model, so their signals are not edits.
  bool loading_ = false;
  bool reload_pending_ = false;
};

/// The Style This Feature… dialog: the whole style panel over one feature's
/// symbol, with Apply, OK, Revert and Cancel. Closing it closes the model.
class LayerStyleWindow : public Gtk::Window {
 public:
  /// `host` must outlive the window.
  LayerStyleWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  /// Re-reads the title and the panel.
  void Reload();
  void SelectEntry(int row) { panel_.SelectEntry(row); }
  /// Applies the draft; false, with the message shown, when it failed.
  bool Apply();
  void Ok();
  void Revert();

  LayerStylePanel& panel() { return panel_; }
  /// The message under the controls; "" when hidden.
  std::string message() const;

 private:
  void Report(const std::string& error);

  fv::desk::DeskHost& host_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 10};
  LayerStylePanel panel_;
  Gtk::Label message_;
  Gtk::Button revert_{"Revert"};
  Gtk::Button cancel_{"Cancel"};
  Gtk::Button apply_{"Apply"};
  Gtk::Button ok_{"OK"};
};

}  // namespace peregrine
