// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "attribute_table_window.h"

#include <algorithm>
#include <string>
#include <vector>

#include "dialogs.h"

namespace peregrine {

AttributeTableWindow::AttributeTableWindow(fv::desk::DeskHost& host, Gtk::Window& parent)
    : host_(host), rows_(Gtk::StringList::create(std::vector<Glib::ustring>())), selection_(Gtk::SingleSelection::create(rows_)) {
  set_transient_for(parent);
  set_default_size(560, 320);
  set_size_request(320, 160);
  set_hide_on_close(true);

  filter_.set_placeholder_text("Filter");
  filter_.set_size_request(200, -1);
  filter_.signal_search_changed().connect([this] { SetFilter(filter_.get_text()); });
  count_.add_css_class("dim-label");
  auto* top = ButtonRow({&filter_}, {&count_});

  selection_->set_autoselect(false);
  selection_->set_can_unselect(true);
  selection_->property_selected().signal_changed().connect([this] {
    const guint row = selection_->get_selected();
    if (!reloading_ && row != GTK_INVALID_LIST_POSITION) host_.SelectTableRow(static_cast<int>(row));
  });
  view_.set_model(selection_);
  view_.set_show_row_separators(false);
  view_.add_css_class("data-table");
  view_.signal_activate().connect([this](guint row) { EditRow(static_cast<int>(row)); });
  if (auto sorter = view_.get_sorter())
    sorter->signal_changed().connect([this](Gtk::Sorter::Change) { OnSortChanged(); });
  scroll_.set_child(view_);
  scroll_.set_vexpand(true);
  scroll_.set_has_frame(true);

  reload_.signal_clicked().connect([this] {
    host_.ReloadAttributeTable();
    Reload();
  });
  edit_.signal_clicked().connect([this] {
    const guint row = selection_->get_selected();
    if (row == GTK_INVALID_LIST_POSITION) error_bell();
    else EditRow(static_cast<int>(row));
  });

  box_.set_margin_top(12);
  box_.set_margin_bottom(12);
  box_.set_margin_start(12);
  box_.set_margin_end(12);
  box_.append(*top);
  box_.append(scroll_);
  box_.append(*ButtonRow({&reload_}, {&edit_}));
  set_child(box_);
  CloseOnEscape(*this);
  signal_close_request().connect(
      [this] {
        host_.CloseAttributeTable();
        return false;
      },
      false);
  Reload();
}

/// One column per field; each header sorts by its column. The rows are not
/// sorted by GTK: the header only shows the host's order.
void AttributeTableWindow::RebuildColumns() {
  while (view_.get_columns()->get_n_items() > 0)
    view_.remove_column(std::dynamic_pointer_cast<Gtk::ColumnViewColumn>(
        view_.get_columns()->get_object(0)));
  columns_ = host_.TableColumnCount();
  for (int c = 0; c < columns_; ++c) {
    const bool numeric = host_.TableColumnNumeric(c);
    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([numeric](const Glib::RefPtr<Gtk::ListItem>& item) {
      auto* label = Gtk::make_managed<Gtk::Label>();
      label->set_xalign(numeric ? 1 : 0);
      label->set_ellipsize(Pango::EllipsizeMode::END);
      item->set_child(*label);
    });
    factory->signal_bind().connect([this, c](const Glib::RefPtr<Gtk::ListItem>& item) {
      if (auto* label = dynamic_cast<Gtk::Label*>(item->get_child()))
        label->set_text(host_.TableCell(static_cast<int>(item->get_position()), c));
    });
    auto column = Gtk::ColumnViewColumn::create(host_.TableColumnTitle(c), factory);
    column->set_fixed_width(numeric ? 90 : 150);
    column->set_resizable(true);
    // A custom sorter without a function holds every row equal.
    column->set_sorter(Glib::wrap(GTK_SORTER(gtk_custom_sorter_new(nullptr, nullptr, nullptr))));
    view_.append_column(column);
  }
}

void AttributeTableWindow::Reload() {
  set_title("Attributes — " + host_.AttributeTableTitle());
  if (columns_ != host_.TableColumnCount()) RebuildColumns();
  reloading_ = true;
  std::vector<Glib::ustring> rows(static_cast<size_t>(std::max(0, host_.TableRowCount())));
  rows_->splice(0, rows_->get_n_items(), rows);
  const int selected = host_.SelectedTableRow();
  if (selected >= 0 && selected < static_cast<int>(rows.size())) {
    selection_->set_selected(static_cast<guint>(selected));
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_column_view_scroll_to(view_.gobj(), static_cast<guint>(selected), nullptr,
                              GTK_LIST_SCROLL_NONE, nullptr);
#endif
  } else {
    selection_->unselect_all();
  }
  reloading_ = false;
  count_.set_text(std::to_string(host_.TableRowCount()) + " features");
}

void AttributeTableWindow::OnSortChanged() {
  auto sorter = std::dynamic_pointer_cast<Gtk::ColumnViewSorter>(view_.get_sorter());
  auto column = sorter ? sorter->get_primary_sort_column() : nullptr;
  int index = -1;
  for (guint c = 0; column && c < view_.get_columns()->get_n_items(); ++c)
    if (view_.get_columns()->get_object(c) == column) index = static_cast<int>(c);
  host_.SortTable(index, index < 0 || sorter->get_primary_sort_order() == Gtk::SortType::ASCENDING);
  Reload();
}

void AttributeTableWindow::SetFilter(const std::string& text) {
  host_.SetTableFilter(text);
  Reload();
}

void AttributeTableWindow::SelectRow(int row) {
  if (row >= 0 && row < row_count()) selection_->set_selected(static_cast<guint>(row));
}

void AttributeTableWindow::EditRow(int row) {
  if (!host_.EditTableRow(row).empty()) error_bell();
}

int AttributeTableWindow::row_count() const { return static_cast<int>(rows_->get_n_items()); }

}  // namespace peregrine
