// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_commands.h — every user action as a named command.
///
/// Menus, the toolbar, context menus and a command palette are generated from
/// the registry; a shell invokes an action only through `Execute(id)`.
/// Shortcuts are written once in a platform-neutral spelling and mapped per OS.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace desk {

/// Modifier bits of a shortcut. `kPrimary` is Command on macOS and Ctrl on
/// Linux and Windows; `kControl` is the Control key where it is distinct
/// from the primary modifier (macOS).
enum Modifier : unsigned {
  kPrimary = 1u << 0,
  kShift = 1u << 1,
  kAlt = 1u << 2,
  kControl = 1u << 3,
};

/// A key plus modifiers. `key` is one printable character ("S", "+", "0")
/// or a named key ("PageUp", "PageDown", "Delete", "Escape", "F1"…).
struct Shortcut {
  std::string key;
  unsigned modifiers = 0;

  bool empty() const { return key.empty(); }
  bool operator==(const Shortcut& o) const {
    return key == o.key && modifiers == o.modifiers;
  }
  bool operator!=(const Shortcut& o) const { return !(*this == o); }

  /// "Primary+Shift+S"; empty for no shortcut.
  std::string ToString() const;
  /// Parses ToString()'s spelling. Modifier names are case-insensitive; a
  /// single-letter key is upper-cased. False on an unknown modifier or a
  /// missing key.
  static bool Parse(const std::string& text, Shortcut* out);
};

/// One user action.
struct Command {
  std::string id;     ///< "<menu>.<verb>[.<arg>]", e.g. "map.group.raster"
  std::string label;  ///< menu text, without an ellipsis convention applied
  std::string icon;   ///< symbolic name; the shell maps it to art
  Shortcut shortcut;
  /// Null means always enabled / never checked.
  std::function<bool()> enabled;
  std::function<bool()> checked;
  std::function<void()> action;
};

/// The command table, in registration order.
class CommandRegistry {
 public:
  /// Rejects an empty id, a duplicate id and a null action. A shortcut
  /// already held by another command is dropped from the new one and the
  /// collision recorded in `warnings()`.
  Status Register(Command c);
  /// Removes one command. False if it was not registered.
  bool Remove(const std::string& id);
  /// Removes every command whose id starts with `prefix`; returns how many.
  size_t RemovePrefix(const std::string& prefix);

  const Command* Find(const std::string& id) const;
  /// The command bound to `s`, or null.
  const Command* FindByShortcut(const Shortcut& s) const;
  /// Ids starting with `prefix`, in registration order.
  std::vector<std::string> IdsWithPrefix(const std::string& prefix) const;
  std::vector<std::string> Ids() const { return IdsWithPrefix(""); }

  /// False for an unknown command.
  bool IsEnabled(const std::string& id) const;
  bool IsChecked(const std::string& id) const;

  /// Runs the action. kNotFound for an unknown id, kUnsupported when the
  /// command is disabled.
  Status Execute(const std::string& id) const;

  /// Bumped on every Register / Remove.
  uint64_t Generation() const { return generation_; }
  const std::vector<std::string>& warnings() const { return warnings_; }

 private:
  std::vector<std::unique_ptr<Command>> order_;
  std::unordered_map<std::string, Command*> by_id_;
  uint64_t generation_ = 0;
  std::vector<std::string> warnings_;
};

}  // namespace desk
}  // namespace fv
