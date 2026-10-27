// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// find_window.h — Map ▸ Go To / Find…, the counterpart of FindWindow.swift.
#pragma once

#include <gtkmm.h>

#include <string>
#include <vector>

#include "fv_desk_host.h"

namespace peregrine {

/// One field that takes a coordinate or a name. A coordinate (decimal, DMS,
/// MGRS) is the only answer when the text parses as one; otherwise the
/// overlays, the road network and the base map on screen are searched
/// (`DeskHost::Search`) and listed under a heading per group. Typing searches
/// after a short pause; Enter searches at once and goes there when there is a
/// single answer; activating a row goes to it.
class FindWindow : public Gtk::Window {
 public:
  /// `host` must outlive the window.
  FindWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  /// Puts the cursor in the field with its text selected, ready to type over.
  void FocusField();
  /// Sets the field's text and searches at once, as typing then Enter does
  /// without the go-to.
  void SearchFor(const std::string& text);
  /// Sets "Search the map in this view only" and searches again.
  void SetViewOnly(bool view_only);
  /// Selects list line `line` as a click would; a heading line is not selectable.
  void SelectLine(int line);
  /// Goes to the selected result, as the Go To button does.
  void GoToSelection();
  /// Enter in the field: searches now, then goes to a single answer.
  void Activate();

  /// Lines listed: one heading per group, then its results.
  int line_count() const { return static_cast<int>(lines_.size()); }
  /// The heading's title on a heading line, "" on a result line.
  std::string heading_at(int line) const;
  /// The selected line, or -1.
  int selected_line() const;
  /// The line under the list.
  std::string status_text() const { return status_.get_text(); }
  Gtk::Button& go_button() { return go_; }

 private:
  /// A list line: a group heading, or result `row` of the host's last search.
  struct Line {
    std::string heading;
    int row = -1;
  };

  void RunSearch();
  void OnSelectionChanged();
  /// The search row of `line`, or -1 for a heading or out of range.
  int RowAt(int line) const;
  void AddColumn(const char* title, int width, int which);

  fv::desk::DeskHost& host_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 8};
  Gtk::SearchEntry field_;
  Gtk::CheckButton view_only_{"Search the map in this view only"};
  Gtk::ScrolledWindow scroll_;
  Gtk::ColumnView view_;
  Glib::RefPtr<Gtk::StringList> rows_;
  Glib::RefPtr<Gtk::SingleSelection> selection_;
  Gtk::Label status_;
  Gtk::Button close_{"Close"};
  Gtk::Button go_{"Go To"};
  std::vector<Line> lines_;
  /// The line selected before the current change, to step over a heading in
  /// the direction the selection moved.
  int last_selected_ = -1;
  /// Set while the selection is moved off a heading, so that move is not handled again.
  bool adjusting_ = false;
};

}  // namespace peregrine
