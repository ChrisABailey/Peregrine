// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// layer_fields_panel.h — the Fields tab of a layer's Properties, the
/// counterpart of LayerFieldsWindow.swift.
#pragma once

#include <gtkmm.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "fv_desk_host.h"

namespace peregrine {

/// The field list with add, delete and reorder, and an editor for the
/// selected field, over `DeskHost`'s fields model. Edits go to the draft as
/// each control is left; the owner applies it.
class LayerFieldsPanel : public Gtk::Box {
 public:
  /// Receives each edit's error message, "" for success.
  using ReportFn = std::function<void(const std::string& error)>;

  /// `host` must outlive the panel.
  LayerFieldsPanel(fv::desk::DeskHost& host, ReportFn report);

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
  /// Writes the editor when a text control changed since it was last written.
  void CommitPending() {
    if (editor_dirty_) CommitEditor();
  }

  Gtk::ListBox& list() { return list_; }
  Gtk::Entry& name_entry() { return name_; }
  Gtk::Entry& title_entry() { return title_; }
  Gtk::DropDown& type_drop_down() { return type_; }
  Gtk::DropDown& role_drop_down() { return role_; }

 private:
  void ReloadEditor();
  void Report(const std::string& error);
  void ReloadIfPending();
  void Move(int step);
  void AddRow(const std::string& title, Gtk::Widget& control);
  void ShowRow(Gtk::Widget& control, bool shown);
  void WatchEntry(Gtk::Entry& entry);

  fv::desk::DeskHost& host_;
  ReportFn report_;
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
  int selected_ = 0;
  /// Set while controls are filled from the model, so their signals are not edits.
  bool loading_ = false;
  /// A text control changed since the editor was last written to the model.
  bool editor_dirty_ = false;
  bool reload_pending_ = false;
};

}  // namespace peregrine
