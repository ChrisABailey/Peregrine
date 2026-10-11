// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// layer_fields_window.h — the Layer Fields dialog, the counterpart of
/// LayerFieldsWindow.swift.
#pragma once

#include <gtkmm.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "fv_desk_host.h"

namespace peregrine {

/// The field list with add, delete and reorder, and an editor for the
/// selected field, over `DeskHost`'s fields model. Edits go to the draft;
/// Apply asks first when the change loses values, then changes the layer,
/// and OK applies and closes. Closing the window closes the model.
class LayerFieldsWindow : public Gtk::Window {
 public:
  /// Asked before an apply that loses values, with the loss as sentences;
  /// true to go ahead. The default asks with a modal dialog.
  using ConfirmFn = std::function<bool(const std::string& loss)>;

  /// `host` must outlive the window.
  LayerFieldsWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  /// Re-reads the field list, keeping the selection where it can.
  void Reload();
  /// Selects field `i` and fills the editor from it.
  void SelectField(int i);
  /// The selected field's index, or -1.
  int selected() const;
  /// Adds a field and selects it, with the name entry focused.
  void AddField();
  /// Writes every editor control to the selected field.
  void CommitEditor();
  /// Applies the draft, asking first when values would be lost; false when
  /// it was refused, cancelled or blocked by a shown error.
  bool Apply();
  void Ok();
  void Revert();
  void set_confirm(ConfirmFn confirm) { confirm_ = std::move(confirm); }

  Gtk::ListBox& list() { return list_; }
  Gtk::Entry& name_entry() { return name_; }
  Gtk::Entry& title_entry() { return title_; }
  Gtk::DropDown& type_drop_down() { return type_; }
  Gtk::DropDown& role_drop_down() { return role_; }
  /// The message under the controls; "" when hidden.
  std::string message() const;

 private:
  void ReloadEditor();
  void Report(const std::string& error);
  void ReloadIfPending();
  void Move(int step);
  void AddRow(const std::string& title, Gtk::Widget& control);
  void ShowRow(Gtk::Widget& control, bool shown);
  void WatchEntry(Gtk::Entry& entry);

  fv::desk::DeskHost& host_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 10};
  Gtk::ScrolledWindow list_scroll_;
  Gtk::ListBox list_;
  Gtk::Button add_{"+"};
  Gtk::Button remove_{"−"};
  Gtk::Button up_{"↑"};
  Gtk::Button down_{"↓"};
  Gtk::Grid editor_;
  int editor_rows_ = 0;
  /// Each control's row label, so a row is hidden whole.
  std::vector<std::pair<Gtk::Widget*, Gtk::Widget*>> row_labels_;
  Gtk::Entry name_;
  Gtk::Entry title_;
  Gtk::DropDown type_;
  Gtk::DropDown role_;
  Gtk::Entry unit_;
  Gtk::CheckButton required_{"Required"};
  Gtk::Entry default_;
  Gtk::Entry min_;
  Gtk::Entry max_;
  Gtk::ScrolledWindow choices_scroll_;
  Gtk::TextView choices_;
  Gtk::Entry description_;
  Gtk::Label origin_;
  Gtk::Label message_;
  Gtk::Button revert_{"Revert"};
  Gtk::Button cancel_{"Cancel"};
  Gtk::Button apply_{"Apply"};
  Gtk::Button ok_{"OK"};
  ConfirmFn confirm_;
  int selected_ = 0;
  /// Set while controls are filled from the model, so their signals are not edits.
  bool loading_ = false;
  /// A text control changed since the editor was last written to the model.
  bool editor_dirty_ = false;
  bool reload_pending_ = false;
};

}  // namespace peregrine
