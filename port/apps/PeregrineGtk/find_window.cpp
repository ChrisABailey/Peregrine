// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "find_window.h"

#include "dialogs.h"

namespace peregrine {
namespace {

/// Whether the map is searched in the view only; kept for the session, on by default.
bool g_view_only = true;

}  // namespace

FindWindow::FindWindow(fv::desk::DeskHost& host, Gtk::Window& parent)
    : host_(host),
      rows_(Gtk::StringList::create(std::vector<Glib::ustring>())),
      selection_(Gtk::SingleSelection::create(rows_)) {
  set_title("Go To / Find");
  set_transient_for(parent);
  set_default_size(680, 440);
  set_size_request(420, 260);
  set_hide_on_close(true);

  field_.set_placeholder_text("33.74 -84.39, 33 44 55 N 84 23 17 W, MGRS, or a name");
  field_.set_hexpand(true);
  field_.set_search_delay(200);
  field_.signal_search_changed().connect([this] { RunSearch(); });
  field_.signal_activate().connect([this] { Activate(); });
  // A search entry takes Escape for itself; here it closes, as the Close button does.
  field_.signal_stop_search().connect([this] { close(); });
  auto keys = Gtk::EventControllerKey::create();
  keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  keys->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType) {
        if (keyval != GDK_KEY_Down || lines_.empty()) return false;
        for (int i = 0; i < line_count(); ++i) {
          if (RowAt(i) < 0) continue;
          SelectLine(i);
          view_.grab_focus();
          break;
        }
        return true;
      },
      false);
  field_.add_controller(keys);

  view_only_.set_active(g_view_only);
  view_only_.signal_toggled().connect([this] {
    g_view_only = view_only_.get_active();
    RunSearch();
  });

  selection_->set_autoselect(false);
  selection_->set_can_unselect(true);
  selection_->property_selected().signal_changed().connect([this] { OnSelectionChanged(); });
  view_.set_model(selection_);
  view_.add_css_class("data-table");
  view_.signal_activate().connect([this](guint line) {
    if (RowAt(static_cast<int>(line)) >= 0) host_.GoToSearchRow(RowAt(static_cast<int>(line)));
  });
  AddColumn("Name", 230, 0);
  AddColumn("Kind", 200, 1);
  AddColumn("From", 100, 2);
  AddColumn("Distance", 80, 3);
  scroll_.set_child(view_);
  scroll_.set_vexpand(true);
  scroll_.set_has_frame(true);

  status_.add_css_class("dim-label");
  status_.set_ellipsize(Pango::EllipsizeMode::END);
  status_.set_xalign(0);
  status_.set_hexpand(true);
  close_.signal_clicked().connect([this] { close(); });
  go_.add_css_class("suggested-action");
  go_.set_sensitive(false);
  go_.signal_clicked().connect([this] { GoToSelection(); });

  box_.set_margin_top(12);
  box_.set_margin_bottom(12);
  box_.set_margin_start(16);
  box_.set_margin_end(16);
  box_.append(field_);
  box_.append(view_only_);
  box_.append(scroll_);
  box_.append(*ButtonRow({&status_}, {&close_, &go_}));
  set_child(box_);
  CloseOnEscape(*this);
}

/// Adds a list column showing field `which` of a result: 0 title, 1 detail,
/// 2 source, 3 distance. A heading line shows its title in the first column.
void FindWindow::AddColumn(const char* title, int width, int which) {
  auto factory = Gtk::SignalListItemFactory::create();
  factory->signal_setup().connect([which](const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_xalign(which == 3 ? 1 : 0);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    item->set_child(*label);
  });
  factory->signal_bind().connect([this, which](const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
    const auto line = static_cast<size_t>(item->get_position());
    if (label == nullptr || line >= lines_.size()) return;
    const Line& l = lines_[line];
    if (l.row < 0) {
      label->set_markup(which == 0 ? "<b>" + Glib::Markup::escape_text(l.heading) + "</b>" : "");
      return;
    }
    switch (which) {
      case 0: label->set_text(host_.SearchRowTitle(l.row)); break;
      case 1: label->set_text(host_.SearchRowDetail(l.row)); break;
      case 2: label->set_text(host_.SearchRowSource(l.row)); break;
      default: label->set_text(host_.SearchRowDistanceText(l.row)); break;
    }
  });
  auto column = Gtk::ColumnViewColumn::create(title, factory);
  column->set_fixed_width(width);
  column->set_resizable(true);
  if (which == 0) column->set_expand(true);
  view_.append_column(column);
}

void FindWindow::FocusField() {
  field_.grab_focus();
  field_.select_region(0, -1);
}

void FindWindow::SearchFor(const std::string& text) {
  field_.set_text(text);
  RunSearch();
}

void FindWindow::SetViewOnly(bool view_only) { view_only_.set_active(view_only); }

void FindWindow::Activate() {
  RunSearch();
  if (host_.SearchRowCount() == 1) host_.GoToSearchRow(0);
}

void FindWindow::RunSearch() {
  const int n = host_.Search(field_.get_text(), view_only_.get_active());
  lines_.clear();
  int group = -1;
  for (int i = 0; i < n; ++i) {
    const int g = host_.SearchRowGroup(i);
    if (g != group) {
      group = g;
      lines_.push_back({host_.SearchRowGroupTitle(i), -1});
    }
    lines_.push_back({"", i});
  }
  adjusting_ = true;
  std::vector<Glib::ustring> rows(lines_.size());
  rows_->splice(0, rows_->get_n_items(), rows);
  selection_->unselect_all();
  adjusting_ = false;
  last_selected_ = -1;
  status_.set_text(host_.SearchStatus());
  go_.set_sensitive(false);
}

int FindWindow::RowAt(int line) const {
  return line >= 0 && line < line_count() ? lines_[static_cast<size_t>(line)].row : -1;
}

std::string FindWindow::heading_at(int line) const {
  return line >= 0 && line < line_count() && RowAt(line) < 0 ? lines_[static_cast<size_t>(line)].heading
                                                             : std::string();
}

int FindWindow::selected_line() const {
  const guint line = selection_->get_selected();
  return line == GTK_INVALID_LIST_POSITION ? -1 : static_cast<int>(line);
}

/// Steps the selection off a heading, onward in the direction it moved, and
/// enables Go To on a result.
void FindWindow::OnSelectionChanged() {
  if (adjusting_) return;
  int line = selected_line();
  if (line >= 0 && RowAt(line) < 0) {
    const int step = line < last_selected_ ? -1 : 1;
    int next = line + step;
    if (RowAt(next) < 0) next = line - step;
    adjusting_ = true;
    if (RowAt(next) >= 0) selection_->set_selected(static_cast<guint>(next));
    else selection_->unselect_all();
    adjusting_ = false;
    line = selected_line();
  }
  last_selected_ = line;
  go_.set_sensitive(RowAt(line) >= 0);
}

void FindWindow::SelectLine(int line) {
  if (line >= 0 && line < line_count()) selection_->set_selected(static_cast<guint>(line));
}

void FindWindow::GoToSelection() {
  const int row = RowAt(selected_line());
  if (row >= 0) host_.GoToSearchRow(row);
  else error_bell();
}

}  // namespace peregrine
