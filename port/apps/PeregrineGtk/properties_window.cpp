// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "properties_window.h"

#include <string>

namespace peregrine {

PropertiesWindow::PropertiesWindow(fv::desk::DeskHost& host, Gtk::Window& parent) : host_(host) {
  set_transient_for(parent);
  set_default_size(460, 380);
  set_hide_on_close(true);
  grid_.set_row_spacing(4);
  grid_.set_column_spacing(12);
  grid_.set_margin_top(12);
  grid_.set_margin_bottom(12);
  grid_.set_margin_start(16);
  grid_.set_margin_end(16);
  scroll_.set_child(grid_);
  scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  set_child(scroll_);

  auto keys = Gtk::EventControllerKey::create();
  keys->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType) {
        if (keyval != GDK_KEY_Escape) return false;
        close();
        return true;
      },
      false);
  add_controller(keys);
  // A selectable label given the initial focus selects its whole text.
  signal_map().connect([this] { Glib::signal_idle().connect_once(sigc::mem_fun(*this, &Gtk::Window::unset_focus)); });
  Reload();
}

void PropertiesWindow::Reload() {
  set_title(host_.LayerPropertiesTitle());
  while (Gtk::Widget* child = grid_.get_first_child()) grid_.remove(*child);
  std::string section;
  int line = 0;
  for (int i = 0; i < host_.LayerPropertyCount(); ++i) {
    if (i == 0 || host_.LayerPropertySection(i) != section) {
      section = host_.LayerPropertySection(i);
      auto* heading = Gtk::make_managed<Gtk::Label>(section);
      heading->set_xalign(0);
      heading->add_css_class("heading");
      if (line > 0) heading->set_margin_top(10);
      grid_.attach(*heading, 0, line++, 2, 1);
    }
    auto* key = Gtk::make_managed<Gtk::Label>(host_.LayerPropertyKey(i));
    key->set_xalign(1);
    key->set_valign(Gtk::Align::START);
    key->add_css_class("dim-label");
    auto* value = Gtk::make_managed<Gtk::Label>(host_.LayerPropertyValue(i));
    value->set_xalign(0);
    value->set_hexpand(true);
    value->set_wrap(true);
    value->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
    value->set_selectable(true);
    grid_.attach(*key, 0, line);
    grid_.attach(*value, 1, line++);
  }
}

}  // namespace peregrine
