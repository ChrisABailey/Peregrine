// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// attribute_table_window.h — the attribute table, the counterpart of
/// AttributeTableWindow.swift.
#pragma once

#include <gtkmm.h>

#include "fv_desk_host.h"

namespace peregrine {

/// Every feature of the current layer, one column per field. Clicking a
/// header sorts, the search entry filters, selecting a row selects the
/// feature on the map and activating a row (double-click, Enter) edits it.
/// Closing the window closes the model.
class AttributeTableWindow : public Gtk::Window {
 public:
  /// `host` must outlive the window.
  AttributeTableWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  /// Re-reads the rows and selects the selected feature's row.
  void Reload();
  /// Filters the rows by `text`, as typing in the search entry does.
  void SetFilter(const std::string& text);
  /// Selects `row` as a click would.
  void SelectRow(int row);
  /// Opens the feature form on `row`, as a double-click does.
  void EditRow(int row);

  Gtk::ColumnView& view() { return view_; }
  /// Rows shown.
  int row_count() const;
  /// "12 features".
  std::string count_text() const { return count_.get_text(); }

 private:
  void RebuildColumns();
  void OnSortChanged();

  fv::desk::DeskHost& host_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 8};
  Gtk::SearchEntry filter_;
  Gtk::Label count_;
  Gtk::ScrolledWindow scroll_;
  Gtk::ColumnView view_;
  Glib::RefPtr<Gtk::StringList> rows_;
  Glib::RefPtr<Gtk::SingleSelection> selection_;
  Gtk::Button reload_{"Reload"};
  Gtk::Button edit_{"Edit…"};
  int columns_ = -1;
  /// Set while the rows are rebuilt, so the selection they restore is not a click.
  bool reloading_ = false;
};

}  // namespace peregrine
