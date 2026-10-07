// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_menu_model.h — the menu bar as data, generated from the commands.
///
/// A layout names commands, separators, submenus and expansion slots (every
/// command under an id prefix, such as one radio item per map group). The
/// model resolves the layout against the registry; an item whose command is
/// not registered is left out, and so is a submenu left empty. Enabled and
/// checked state is not part of the model: a shell asks the registry when it
/// shows a menu.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fv_desk_commands.h"

namespace fv {
namespace desk {

/// One resolved menu entry: a command, a separator (no id, no children) or a
/// submenu (children, no id).
struct MenuItem {
  std::string command_id;
  std::string label;
  std::string icon;
  Shortcut shortcut;
  std::vector<MenuItem> children;

  bool is_separator() const { return command_id.empty() && children.empty(); }
  bool operator==(const MenuItem& o) const;
  bool operator!=(const MenuItem& o) const { return !(*this == o); }
};

struct Menu {
  std::string id;     ///< "file", "map", "overlay", "editor"
  std::string title;
  std::vector<MenuItem> items;

  bool operator==(const Menu& o) const {
    return id == o.id && title == o.title && items == o.items;
  }
  bool operator!=(const Menu& o) const { return !(*this == o); }
};

/// One entry of a layout.
struct MenuSlot {
  enum class Kind { kCommand, kSeparator, kExpand, kSubmenu };
  Kind kind = Kind::kSeparator;
  std::string ref;    ///< command id (kCommand) or id prefix (kExpand)
  std::string title;  ///< kSubmenu
  std::vector<MenuSlot> children;

  static MenuSlot Cmd(std::string id);
  static MenuSlot Sep();
  static MenuSlot Expand(std::string prefix);
  static MenuSlot Sub(std::string title, std::vector<MenuSlot> children);
};

struct MenuSpec {
  std::string id;
  std::string title;
  std::vector<MenuSlot> items;
};

/// File · Map · Overlay (port/desktop-plan.md §1a). The ‹Editor› menu is
/// supplied per editor to `MenuModel::Rebuild`.
std::vector<MenuSpec> DefaultMenuLayout();

class MenuModel {
 public:
  explicit MenuModel(const CommandRegistry& commands,
                     std::vector<MenuSpec> layout = DefaultMenuLayout());

  /// Resolves the layout, plus `editor_menu` after it when non-null. When
  /// the result differs from the current menus, replaces them, bumps
  /// `Generation()`, calls the change handler and returns true.
  bool Rebuild(const MenuSpec* editor_menu = nullptr);

  const std::vector<Menu>& Menus() const { return menus_; }
  /// The menu with `id`, or null.
  const Menu* Find(const std::string& id) const;
  uint64_t Generation() const { return generation_; }

  /// Called after a rebuild that changed the menus.
  void SetOnChange(std::function<void()> fn) { on_change_ = std::move(fn); }

 private:
  Menu Resolve(const MenuSpec& spec) const;
  std::vector<MenuItem> ResolveSlots(const std::vector<MenuSlot>& slots) const;

  const CommandRegistry& commands_;
  std::vector<MenuSpec> layout_;
  std::vector<Menu> menus_;
  uint64_t generation_ = 0;
  std::function<void()> on_change_;
};

}  // namespace desk
}  // namespace fv
