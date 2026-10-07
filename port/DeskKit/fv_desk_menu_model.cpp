// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_menu_model.h"

namespace fv {
namespace desk {
namespace {

MenuItem ItemFor(const Command& c) {
  MenuItem m;
  m.command_id = c.id;
  m.label = c.label;
  m.icon = c.icon;
  m.shortcut = c.shortcut;
  return m;
}

/// Drops leading, trailing and repeated separators.
void TidySeparators(std::vector<MenuItem>* items) {
  std::vector<MenuItem> out;
  for (MenuItem& m : *items) {
    if (m.is_separator() && (out.empty() || out.back().is_separator())) continue;
    out.push_back(std::move(m));
  }
  if (!out.empty() && out.back().is_separator()) out.pop_back();
  *items = std::move(out);
}

}  // namespace

bool MenuItem::operator==(const MenuItem& o) const {
  return command_id == o.command_id && label == o.label && icon == o.icon &&
         shortcut == o.shortcut && children == o.children;
}

MenuSlot MenuSlot::Cmd(std::string id) {
  MenuSlot s;
  s.kind = Kind::kCommand;
  s.ref = std::move(id);
  return s;
}

MenuSlot MenuSlot::Sep() { return MenuSlot(); }

MenuSlot MenuSlot::Expand(std::string prefix) {
  MenuSlot s;
  s.kind = Kind::kExpand;
  s.ref = std::move(prefix);
  return s;
}

MenuSlot MenuSlot::Sub(std::string title, std::vector<MenuSlot> children) {
  MenuSlot s;
  s.kind = Kind::kSubmenu;
  s.title = std::move(title);
  s.children = std::move(children);
  return s;
}

std::vector<MenuSpec> DefaultMenuLayout() {
  using S = MenuSlot;
  return {
      {"file", "File",
       {S::Cmd("file.save"), S::Cmd("file.save_as"), S::Cmd("file.close"), S::Sep(),
        S::Cmd("file.open_workspace"), S::Cmd("file.save_workspace"), S::Sep(),
        S::Cmd("file.export_image"), S::Sep(), S::Cmd("app.quit")}},
      {"map", "Map",
       {S::Expand("map.group."), S::Sep(), S::Sub("Projection", {S::Expand("map.projection.")}),
        S::Cmd("map.zoom_in"), S::Cmd("map.zoom_out"), S::Cmd("map.goto"),
        S::Cmd("map.recenter"), S::Sep(), S::Cmd("map.catalog_build"),
        S::Cmd("map.sources"), S::Sep(), S::Cmd("map.options")}},
      {"overlay", "Overlay",
       {S::Expand("overlay.toggle."), S::Sep(), S::Sub("New", {S::Expand("overlay.new.")}),
        S::Cmd("overlay.open"), S::Sep(), S::Expand("overlay.instance."), S::Sep(),
        S::Cmd("overlay.options")}},
  };
}

MenuModel::MenuModel(const CommandRegistry& commands, std::vector<MenuSpec> layout)
    : commands_(commands), layout_(std::move(layout)) {}

std::vector<MenuItem> MenuModel::ResolveSlots(const std::vector<MenuSlot>& slots) const {
  std::vector<MenuItem> out;
  for (const MenuSlot& s : slots) {
    switch (s.kind) {
      case MenuSlot::Kind::kSeparator:
        out.emplace_back();
        break;
      case MenuSlot::Kind::kCommand:
        if (const Command* c = commands_.Find(s.ref)) out.push_back(ItemFor(*c));
        break;
      case MenuSlot::Kind::kExpand:
        for (const std::string& id : commands_.IdsWithPrefix(s.ref))
          out.push_back(ItemFor(*commands_.Find(id)));
        break;
      case MenuSlot::Kind::kSubmenu: {
        MenuItem sub;
        sub.label = s.title;
        sub.children = ResolveSlots(s.children);
        if (!sub.children.empty()) out.push_back(std::move(sub));
        break;
      }
    }
  }
  TidySeparators(&out);
  return out;
}

Menu MenuModel::Resolve(const MenuSpec& spec) const {
  return Menu{spec.id, spec.title, ResolveSlots(spec.items)};
}

bool MenuModel::Rebuild(const MenuSpec* editor_menu) {
  std::vector<Menu> next;
  for (const MenuSpec& spec : layout_) next.push_back(Resolve(spec));
  if (editor_menu != nullptr) next.push_back(Resolve(*editor_menu));
  if (next == menus_ && generation_ > 0) return false;
  menus_ = std::move(next);
  ++generation_;
  if (on_change_) on_change_();
  return true;
}

const Menu* MenuModel::Find(const std::string& id) const {
  for (const Menu& m : menus_)
    if (m.id == id) return &m;
  return nullptr;
}

}  // namespace desk
}  // namespace fv
