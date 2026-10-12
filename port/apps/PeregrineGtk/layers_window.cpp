// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "layers_window.h"

#include <string>
#include <utility>

#include "main_menu.h"

namespace peregrine {

namespace {

/// A drag payload carrying `value`, a row or entry index.
Glib::RefPtr<Gdk::ContentProvider> IntPayload(int value) {
  GValue v = G_VALUE_INIT;
  g_value_init(&v, G_TYPE_INT);
  g_value_set_int(&v, value);
  GdkContentProvider* provider = gdk_content_provider_new_for_value(&v);
  g_value_unset(&v);
  return Glib::wrap(provider);
}

/// Makes `widget` a drag source of `value`.
void AddDrag(Gtk::Widget& widget, int value) {
  auto drag = Gtk::DragSource::create();
  drag->set_actions(Gdk::DragAction::MOVE);
  drag->signal_prepare().connect([value](double, double) { return IntPayload(value); }, false);
  widget.add_controller(drag);
}

/// The row a drop at `y` lands before: the one under it, or the next one when
/// in its lower half; `count` past the last row.
int DropSlot(Gtk::ListBox& list, double y, int count) {
  Gtk::ListBoxRow* target = list.get_row_at_y(static_cast<int>(y));
  if (!target) return count;
  int slot = target->get_index();
  const double top = target->get_allocation().get_y();
  if (y > top + target->get_height() / 2.0) ++slot;
  return slot;
}

void SetMargins(Gtk::Widget& w, int margin) {
  w.set_margin_top(margin);
  w.set_margin_bottom(margin);
  w.set_margin_start(margin);
  w.set_margin_end(margin);
}

}  // namespace

LayersWindow::LayersWindow(fv::desk::DeskHost& host, Gtk::Window& parent,
                           std::function<void(const std::string&)> execute)
    : host_(host), execute_(std::move(execute)) {
  set_title("Layers");
  set_transient_for(parent);
  set_default_size(380, 360);
  set_size_request(280, 200);
  set_hide_on_close(true);

  // Layers: the drawing order. A row dropped on another lands above it, or
  // below it in its lower half.
  list_.set_selection_mode(Gtk::SelectionMode::SINGLE);
  list_.add_css_class("rich-list");
  list_.signal_selected_rows_changed().connect([this] { OnSelected(); });
  scroll_.set_child(list_);
  scroll_.set_has_frame(true);
  scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  auto drop = Gtk::DropTarget::create(G_TYPE_INT, Gdk::DragAction::MOVE);
  drop->signal_drop().connect(
      [this](const Glib::ValueBase& value, double, double y) {
        if (!G_VALUE_HOLDS_INT(value.gobj())) return false;
        const int from = g_value_get_int(value.gobj());
        const int slot = DropSlot(list_, y, host_.LayerCount());
        const int to = from < slot ? slot - 1 : slot;
        if (to == from || to < 0 || to >= host_.LayerCount()) return false;
        return Move(from, to);
      },
      false);
  list_.add_controller(drop);

  // Sources: a source dropped on a source, or a layer on a layer of its own source.
  sources_.set_selection_mode(Gtk::SelectionMode::SINGLE);
  sources_.add_css_class("rich-list");
  sources_.signal_selected_rows_changed().connect([this] { OnSourceSelected(); });
  sources_scroll_.set_child(sources_);
  sources_scroll_.set_has_frame(true);
  sources_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  auto source_drop = Gtk::DropTarget::create(G_TYPE_INT, Gdk::DragAction::MOVE);
  source_drop->signal_drop().connect(
      [this](const Glib::ValueBase& value, double, double y) {
        if (!G_VALUE_HOLDS_INT(value.gobj())) return false;
        const int from = g_value_get_int(value.gobj());
        Gtk::ListBoxRow* target = sources_.get_row_at_y(static_cast<int>(y));
        return target != nullptr && MoveSourceRow(from, target->get_index());
      },
      false);
  sources_.add_controller(source_drop);
  auto right_click = Gtk::GestureClick::create();
  right_click->set_button(GDK_BUTTON_SECONDARY);
  right_click->signal_pressed().connect([this](int, double x, double y) {
    if (Gtk::ListBoxRow* row = sources_.get_row_at_y(static_cast<int>(y))) ShowMenuAt(row->get_index(), x, y);
  });
  sources_.add_controller(right_click);

  views_.add(sources_scroll_, "sources", "Sources");
  views_.add(scroll_, "layers", "Layers");
  views_.set_vexpand(true);
  views_.property_visible_child_name().signal_changed().connect([this] { UpdateButtons(); });
  switcher_.set_stack(views_);
  switcher_.set_halign(Gtk::Align::CENTER);

  // The menu is parented to the box, which does not lay out popovers.
  menu_.set_has_arrow(false);
  menu_.set_halign(Gtk::Align::START);
  menu_.set_parent(box_);

  up_.signal_clicked().connect([this] {
    if (views_.get_visible_child_name() == "sources") {
      MoveSelectedSourceRow(-1);
      return;
    }
    const Gtk::ListBoxRow* row = list_.get_selected_row();
    if (row && row->get_index() > 0) Move(row->get_index(), row->get_index() - 1);
  });
  down_.signal_clicked().connect([this] {
    if (views_.get_visible_child_name() == "sources") {
      MoveSelectedSourceRow(+1);
      return;
    }
    const Gtk::ListBoxRow* row = list_.get_selected_row();
    if (row) Move(row->get_index(), row->get_index() + 1);
  });
  buttons_.append(up_);
  buttons_.append(down_);

  SetMargins(box_, 12);
  box_.append(switcher_);
  box_.append(views_);
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

LayersWindow::~LayersWindow() { menu_.unparent(); }

void LayersWindow::Reload() {
  reloading_ = true;
  ReloadLayers();
  ReloadSources();
  reloading_ = false;
  UpdateButtons();
}

void LayersWindow::ReloadLayers() {
  while (Gtk::Widget* row = list_.get_first_child()) list_.remove(*row);
  const int count = host_.LayerCount();
  int current = -1;
  for (int i = 0; i < count; ++i) {
    list_.append(*MakeLayerRow(i, false, i));
    if (host_.LayerCurrent(i)) current = i;
  }
  if (current >= 0) list_.select_row(*list_.get_row_at_index(current));
  else list_.unselect_all();
}

void LayersWindow::ReloadSources() {
  while (Gtk::Widget* row = sources_.get_first_child()) sources_.remove(*row);
  entries_.clear();
  int keep = -1, current = -1;
  for (int s = 0; s < host_.SourceCount(); ++s) {
    const std::string key = SourceKey(s);
    const int at = static_cast<int>(entries_.size());
    entries_.push_back({s, -1, -1});
    sources_.append(*MakeSourceRow(s, at));
    if (key == selected_.source && selected_.index < 0) keep = at;
    if (collapsed_.count(key)) {
      if (host_.SourceCurrent(s)) current = at;
      continue;
    }
    for (int i = 0; i < host_.SourceLayerCount(s); ++i) {
      const int row = host_.SourceLayerRow(s, i);
      const int entry = static_cast<int>(entries_.size());
      entries_.push_back({s, row, i});
      sources_.append(*MakeLayerRow(row, true, entry));
      if (key == selected_.source && selected_.index == i) keep = entry;
      if (host_.LayerCurrent(row)) current = entry;
    }
  }
  const int select = keep >= 0 ? keep : current;
  if (select >= 0) sources_.select_row(*sources_.get_row_at_index(select));
  else sources_.unselect_all();
}

std::string LayersWindow::SourceKey(int source) const {
  const std::string path = host_.SourcePath(source);
  return path.empty() ? host_.SourceName(source) : path;
}

std::pair<int, int> LayersWindow::SourceEntry(int index) const {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return {-1, -1};
  return {entries_[index].source, entries_[index].layer};
}

/// One layer: visibility, icon and name (bold when current, a bullet when
/// unsaved), and an opacity slider where the layer has one. Indented under
/// its source in the Sources view.
Gtk::Widget* LayersWindow::MakeLayerRow(int row, bool indented, int drag_value) {
  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  if (indented) box->set_margin_start(28);
  auto* visible = Gtk::make_managed<Gtk::CheckButton>();
  visible->set_active(host_.LayerVisible(row));
  visible->set_tooltip_text("Show or hide");
  visible->signal_toggled().connect([this, row, visible] {
    host_.SetLayerVisible(row, visible->get_active());
    // The source's check box follows its layers.
    Glib::signal_idle().connect_once(sigc::mem_fun(*this, &LayersWindow::Reload));
  });
  box->append(*visible);

  const std::string icon = IconNameFor(host_.LayerIcon(row));
  if (!icon.empty()) box->append(*Gtk::make_managed<Gtk::Image>(icon));
  auto* name = Gtk::make_managed<Gtk::Label>(host_.LayerName(row) + (host_.LayerDirty(row) ? " •" : ""));
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
  AddDrag(*box, drag_value);
  return box;
}

/// One source: an expander, a check box that is mixed while only some of its
/// layers are shown, its icon and its name.
Gtk::Widget* LayersWindow::MakeSourceRow(int source, int drag_value) {
  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  const std::string key = SourceKey(source);
  const bool expanded = collapsed_.count(key) == 0;
  auto* expander = Gtk::make_managed<Gtk::Button>();
  expander->set_icon_name(expanded ? "pan-down-symbolic" : "pan-end-symbolic");
  expander->add_css_class("flat");
  expander->set_tooltip_text(expanded ? "Hide layers" : "Show layers");
  expander->signal_clicked().connect([this, source, expanded] {
    // Tracked, so a window destroyed before the idle runs is not touched.
    Glib::signal_idle().connect_once(
        sigc::track_obj([this, source, expanded] { SetExpanded(source, !expanded); }, *this));
  });
  box->append(*expander);

  auto* visible = Gtk::make_managed<Gtk::CheckButton>();
  const int shown = host_.SourceShown(source);
  visible->set_active(shown == 2);
  visible->set_inconsistent(shown == 1);
  visible->set_tooltip_text("Show or hide every layer");
  visible->signal_toggled().connect([this, source, visible] {
    if (reloading_) return;
    host_.SetSourceVisible(source, visible->get_active());
    Glib::signal_idle().connect_once(sigc::mem_fun(*this, &LayersWindow::Reload));
  });
  box->append(*visible);

  const std::string icon = IconNameFor(host_.SourceIcon(source));
  if (!icon.empty()) box->append(*Gtk::make_managed<Gtk::Image>(icon));
  auto* name = Gtk::make_managed<Gtk::Label>(host_.SourceName(source) + (host_.SourceDirty(source) ? " •" : ""));
  name->set_xalign(0);
  name->set_hexpand(true);
  name->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
  const std::string path = host_.SourcePath(source);
  name->set_tooltip_text(path.empty() ? host_.SourceTypeName(source) : path);
  box->append(*name);
  const int layers = host_.SourceLayerCount(source);
  if (layers > 1) {
    auto* count = Gtk::make_managed<Gtk::Label>(std::to_string(layers) + " layers");
    count->add_css_class("dim-label");
    box->append(*count);
  }
  AddDrag(*box, drag_value);
  return box;
}

void LayersWindow::SetExpanded(int source, bool expanded) {
  const std::string key = SourceKey(source);
  if (expanded) collapsed_.erase(key);
  else collapsed_.insert(key);
  // A hidden layer's selection moves to its source.
  if (!expanded && selected_.source == key) selected_.index = -1;
  Reload();
}

bool LayersWindow::Move(int from, int to) {
  const std::string error = host_.MoveLayer(from, to);
  if (!error.empty()) error_bell();
  Reload();
  return error.empty();
}

/// Moves Sources row `from_entry` to where row `to_entry` is: a source to
/// another source's place, or a layer to another layer's place in the same source.
bool LayersWindow::MoveSourceRow(int from_entry, int to_entry) {
  const int n = static_cast<int>(entries_.size());
  if (from_entry < 0 || from_entry >= n || to_entry < 0 || to_entry >= n || from_entry == to_entry)
    return false;
  const Entry from = entries_[from_entry];
  const Entry to = entries_[to_entry];
  std::string error;
  if (from.layer < 0) {
    if (to.source == from.source) return false;
    selected_ = {SourceKey(from.source), -1};
    error = host_.MoveSource(from.source, to.source);
  } else {
    if (to.layer < 0 || to.source != from.source) {
      error_bell();
      return false;
    }
    selected_ = {SourceKey(from.source), to.index};
    error = host_.MoveLayer(from.layer, to.layer);
  }
  if (!error.empty()) error_bell();
  Reload();
  return error.empty();
}

bool LayersWindow::MoveSelectedSourceRow(int delta) {
  const Gtk::ListBoxRow* row = sources_.get_selected_row();
  if (!row) return false;
  const int index = row->get_index();
  if (index < 0 || index >= static_cast<int>(entries_.size())) return false;
  const Entry e = entries_[index];
  if (e.layer < 0) {
    const int to = e.source + delta;
    if (to < 0 || to >= host_.SourceCount()) return false;
    selected_ = {SourceKey(e.source), -1};
    const std::string error = host_.MoveSource(e.source, to);
    if (!error.empty()) error_bell();
    Reload();
    return error.empty();
  }
  const int to = e.index + delta;
  if (to < 0 || to >= host_.SourceLayerCount(e.source)) return false;
  selected_ = {SourceKey(e.source), to};
  const std::string error = host_.MoveLayer(e.layer, host_.SourceLayerRow(e.source, to));
  if (!error.empty()) error_bell();
  Reload();
  return error.empty();
}

void LayersWindow::ShowMenuAt(int index, double x, double y) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  if (Gtk::ListBoxRow* row = sources_.get_row_at_index(index)) sources_.select_row(*row);
  const Entry e = entries_[index];
  // The menu's commands act on the current layer, so the row is made current first.
  if (e.layer < 0) host_.SelectSource(e.source);
  else host_.SelectLayer(e.layer);

  const bool source = e.layer < 0;
  const int count = source ? host_.SourceMenuEntryCount() : host_.LayerMenuEntryCount();
  auto model = Gio::Menu::create();
  auto section = Gio::Menu::create();
  menu_actions_ = Gio::SimpleActionGroup::create();
  for (int i = 0; i < count; ++i) {
    const fv::desk::HostMenuEntry m = source ? host_.SourceMenuEntryAt(i) : host_.LayerMenuEntryAt(i);
    if (m.kind == fv::desk::kMenuSeparator) {
      if (section->get_n_items() > 0) model->append_section(section);
      section = Gio::Menu::create();
      continue;
    }
    if (m.kind != fv::desk::kMenuCommand) continue;
    const std::string name = ActionNameFor(m.id);
    const std::string id = m.id;
    auto action = menu_actions_->add_action(name, [this, id] { execute_(id); });
    action->set_enabled(host_.IsEnabled(id));
    section->append(m.label, "ctx." + name);
  }
  if (section->get_n_items() > 0) model->append_section(section);
  insert_action_group("ctx", menu_actions_);
  menu_.set_menu_model(model);

  double bx = x, by = y;
  sources_.translate_coordinates(box_, x, y, bx, by);
  menu_.set_pointing_to(Gdk::Rectangle(static_cast<int>(bx), static_cast<int>(by), 1, 1));
  menu_.popup();
}

void LayersWindow::UpdateButtons() {
  if (views_.get_visible_child_name() == "sources") {
    const Gtk::ListBoxRow* selected = sources_.get_selected_row();
    const int index = selected ? selected->get_index() : -1;
    bool up = false, down = false;
    if (index >= 0 && index < static_cast<int>(entries_.size())) {
      const Entry& e = entries_[index];
      const int place = e.layer < 0 ? e.source : e.index;
      const int count = e.layer < 0 ? host_.SourceCount() : host_.SourceLayerCount(e.source);
      up = place > 0;
      down = place + 1 < count;
    }
    up_.set_sensitive(up);
    down_.set_sensitive(down);
    return;
  }
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
  selected_ = Selection();  // the Sources view follows the current layer
  if (host_.LayerCurrent(row)) return;
  host_.SelectLayer(row);
  // Rebuilt after the click has finished with the row it landed on.
  Glib::signal_idle().connect_once(sigc::mem_fun(*this, &LayersWindow::Reload));
}

void LayersWindow::OnSourceSelected() {
  UpdateButtons();
  const Gtk::ListBoxRow* selected = sources_.get_selected_row();
  if (reloading_ || !selected) return;
  const int index = selected->get_index();
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  const Entry e = entries_[index];
  selected_ = {SourceKey(e.source), e.layer < 0 ? -1 : e.index};
  if (e.layer >= 0 ? host_.LayerCurrent(e.layer) : host_.SourceCurrent(e.source)) return;
  if (e.layer >= 0) host_.SelectLayer(e.layer);
  else host_.SelectSource(e.source);
  Glib::signal_idle().connect_once(sigc::mem_fun(*this, &LayersWindow::Reload));
}

}  // namespace peregrine
