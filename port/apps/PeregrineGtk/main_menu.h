// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// main_menu.h — the menu bar and toolbar generated from DeskKit's menu
/// model, the counterpart of MainMenu.swift and Toolbar.swift.
#pragma once

#include <giomm.h>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "fv_desk_host.h"

namespace peregrine {

/// The GTK accelerator ("<Primary><Shift>s") for a DeskKit shortcut, or ""
/// when the key has no GTK spelling. Primary is Ctrl on Linux.
/// @param key DeskKit's key spelling ("S", "=", "PageUp", "F5").
/// @param modifiers `fv::desk::HostModifier` bits.
std::string AcceleratorFor(const std::string& key, unsigned modifiers);

/// A valid GAction name for a command id: characters other than letters,
/// digits, '-' and '.' become '-'.
std::string ActionNameFor(const std::string& command_id);

/// One toolbar entry: a command, or a gap between groups (empty `command`).
struct ToolbarItem {
  std::string command;
  std::string action;  ///< detailed action name, "cmd.map.zoom-in"
  std::string label;
  std::string icon;    ///< DeskKit's symbolic icon name
  bool checkable = false;

  bool operator==(const ToolbarItem& o) const {
    return command == o.command && label == o.label && icon == o.icon &&
           checkable == o.checkable;
  }
};

/// The menu bar model, the toolbar entries and one action per command in
/// either, in the action group `kActionPrefix`. Activating an action runs
/// `execute` with the command id; a checkable command's action carries a
/// boolean state that `Refresh` copies from the host.
class CommandMenus {
 public:
  /// The prefix the window inserts `actions()` under.
  static constexpr const char* kActionPrefix = "cmd";

  CommandMenus(fv::desk::DeskHost& host, std::function<void(const std::string&)> execute);

  /// Re-reads the host's menu bar and toolbar when its menu generation moved,
  /// replacing the menu model and the actions. True when it rebuilt.
  bool Rebuild();
  /// Copies every action's enabled and checked state from the host.
  void Refresh();

  /// File, Map, Overlay and the active editor's menu; separators are sections.
  Glib::RefPtr<Gio::Menu> menu_bar() const { return menu_bar_; }
  Glib::RefPtr<Gio::SimpleActionGroup> actions() const { return actions_; }
  const std::vector<ToolbarItem>& toolbar() const { return toolbar_; }

  /// The detailed action name ("cmd.file.save") for a command, or "".
  std::string ActionFor(const std::string& command_id) const;
  /// The command an action name (without prefix) runs, or "".
  std::string CommandFor(const std::string& action_name) const;
  /// (detailed action name, accelerator) for every command with a shortcut.
  const std::vector<std::pair<std::string, std::string>>& accelerators() const {
    return accelerators_;
  }

 private:
  void AddAction(const fv::desk::HostMenuEntry& e);

  fv::desk::DeskHost& host_;
  std::function<void(const std::string&)> execute_;
  Glib::RefPtr<Gio::Menu> menu_bar_;
  Glib::RefPtr<Gio::SimpleActionGroup> actions_;
  std::vector<ToolbarItem> toolbar_;
  std::vector<std::pair<std::string, std::string>> accelerators_;
  std::map<std::string, std::string> action_by_command_;
  std::map<std::string, std::string> command_by_action_;
  uint64_t generation_ = 0;
  bool built_ = false;
};

}  // namespace peregrine
