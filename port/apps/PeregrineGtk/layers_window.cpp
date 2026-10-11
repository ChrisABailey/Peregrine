// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "layers_window.h"

#include <string>

#include "main_menu.h"

namespace peregrine {

LayersWindow::LayersWindow(fv::desk::DeskHost& host, Gtk::Window& parent) : host_(host) {
  set_title("Layers");
  set_transient_for(parent);
  set_default_size(360, 300);
  set_size_request(280, 160);
  set_hide_on_close(true);

  list_.set_selection_mode(Gtk::SelectionMode::SINGLE);
  list_.add_css_class("rich-list");
  list_.signal_selected_rows_changed().connect([this] { OnSelected(); });
  scroll_.set_child(list_);
  scroll_.set_vexpand(true);
  scroll_.set_has_frame(true);
  scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

  // A row dropped on another lands above it, or below it in its lower half.
  auto drop = Gtk::DropTarget::create(G_TYPE_INT, Gdk::DragAction::MOVE);
  drop->signal_drop().connect(
      [this](const Glib::ValueBase& value, double, double y) {
        if (!G_VALUE_HOLDS_INT(value.gobj())) return false;
        const int from = g_value_get_int(value.gobj());
        Gtk::ListBoxRow* target = list_.get_row_at_y(static_cast<int>(y));
        int above = host_.LayerCount();
        if (target) {
          above = target->get_index();
          const double top = target->get_allocation().get_y();
          if (y > top + target->get_height() / 2.0) ++above;
        }
        const int to = from < above ? above - 1 : above;
        if (to == from || to < 0 || to >= host_.LayerCount()) return false;
        return Move(from, to);
      },
      false);
  list_.add_controller(drop);

  up_.signal_clicked().connect([this] {
    const Gtk::ListBoxRow* row = list_.get_selected_row();
    if (row && row->get_index() > 0) Move(row->get_index(), row->get_index() - 1);
  });
  down_.signal_clicked().connect([this] {
    const Gtk::ListBoxRow* row = list_.get_selected_row();
    if (row) Move(row->get_index(), row->get_index() + 1);
  });
  buttons_.append(up_);
  buttons_.append(down_);

  box_.set_margin_top(12);
  box_.set_margin_bottom(12);
  box_.set_margin_start(12);
  box_.set_margin_end(12);
  box_.append(scroll_);
  box_.append(buttons_);
  set_child(box_);

  auto keys = Gtk::EventControllerKey::create();
  keys->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType) {
        if (keyval != GDK_KEY_Escape) return false;
        close();
        return true;
      },
      false);
  add_controller(keys);
  Reload();
}

void LayersWindow::Reload() {
  reloading_ = true;
  while (Gtk::Widget* row = list_.get_first_child()) list_.remove(*row);
  const int count = host_.LayerCount();
  int current = -1;
  for (int i = 0; i < count; ++i) {
    list_.append(*MakeRow(i));
    if (host_.LayerCurrent(i)) current = i;
  }
  if (current >= 0) list_.select_row(*list_.get_row_at_index(current));
  else list_.unselect_all();
  reloading_ = false;
  UpdateButtons();
}

/// One row: visibility, icon and name (bold when current, a bullet when
/// unsaved), and an opacity slider where the layer has one.
Gtk::Widget* LayersWindow::MakeRow(int row) {
  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  auto* visible = Gtk::make_managed<Gtk::CheckButton>();
  visible->set_active(host_.LayerVisible(row));
  visible->set_tooltip_text("Show or hide");
  visible->signal_toggled().connect(
      [this, row, visible] { host_.SetLayerVisible(row, visible->get_active()); });
  box->append(*visible);

  const std::string icon = IconNameFor(host_.LayerIcon(row));
  if (!icon.empty()) box->append(*Gtk::make_managed<Gtk::Image>(icon));
  auto* name = Gtk::make_managed<Gtk::Label>(host_.LayerName(row) +
                                             (host_.LayerDirty(row) ? " •" : ""));
  name->set_xalign(0);
  name->set_hexpand(true);
  name->set_ellipsize(Pango::EllipsizeMode::END);
  name->set_tooltip_text(host_.LayerTypeName(row));
  if (host_.LayerCurrent(row)) name->add_css_class("heading");
  box->append(*name);

  if (host_.LayerHasOpacity(row)) {
    auto* opacity = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);
    opacity->set_range(0, 100);
    opacity->set_increments(1, 10);
    opacity->set_draw_value(false);
    opacity->set_size_request(110, -1);
    opacity->set_value(host_.LayerOpacity(row));
    opacity->set_tooltip_text("Opacity " + std::to_string(host_.LayerOpacity(row)) + "%");
    opacity->signal_value_changed().connect([this, row, opacity] {
      const int percent = static_cast<int>(opacity->get_value() + 0.5);
      host_.SetLayerOpacity(row, percent);
      opacity->set_tooltip_text("Opacity " + std::to_string(percent) + "%");
    });
    box->append(*opacity);
  }

  auto drag = Gtk::DragSource::create();
  drag->set_actions(Gdk::DragAction::MOVE);
  drag->signal_prepare().connect(
      [row](double, double) {
        GValue value = G_VALUE_INIT;
        g_value_init(&value, G_TYPE_INT);
        g_value_set_int(&value, row);
        GdkContentProvider* provider = gdk_content_provider_new_for_value(&value);
        g_value_unset(&value);
        return Glib::wrap(provider);
      },
      false);
  box->add_controller(drag);
  return box;
}

bool LayersWindow::Move(int from, int to) {
  const std::string error = host_.MoveLayer(from, to);
  if (!error.empty()) error_bell();
  Reload();
  return error.empty();
}

void LayersWindow::UpdateButtons() {
  const Gtk::ListBoxRow* selected = list_.get_selected_row();
  const int row = selected ? selected->get_index() : -1;
  up_.set_sensitive(row > 0);
  down_.set_sensitive(row >= 0 && row + 1 < host_.LayerCount());
}

void LayersWindow::OnSelected() {
  UpdateButtons();
  const Gtk::ListBoxRow* selected = list_.get_selected_row();
  if (reloading_ || !selected) return;
  const int row = selected->get_index();
  if (host_.LayerCurrent(row)) return;
  host_.SelectLayer(row);
  // Rebuilt after the click has finished with the row it landed on.
  Glib::signal_idle().connect_once(sigc::mem_fun(*this, &LayersWindow::Reload));
}

}  // namespace peregrine
