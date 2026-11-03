// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_tools.h — the Tools menu's registry (port/extensions-plan.md §4).
///
/// A tool is a command that is not an overlay: an importer, a converter, a
/// setup window. Each registered tool becomes the command `tools.run.<id>`
/// under Tools. Code in the process registers a tool's behaviour under a name
/// with `RegisterBuiltinTool`; a package's `tools` entry refers to it by that
/// name, as an overlay entry refers to a builtin overlay.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace desk {

class Desk;

/// What a running tool may reach. Builtin and native tools get the Desk; the
/// C and Python kinds see a narrower surface built over this one.
class ToolContext {
 public:
  explicit ToolContext(Desk& desk) : desk_(desk) {}
  Desk& desk() const { return desk_; }

  /// Opens `path` as an overlay of the type that owns its extension.
  /// kUnsupported when no type does, kInterrupted when the user cancels,
  /// kIoError when the open fails.
  Status OpenFile(const std::string& path) const;
  /// Asks the shell to repaint the map.
  void RequestRedraw() const;

 private:
  Desk& desk_;
};

/// A tool's behaviour. Runs on the UI thread; a failure is reported to the user.
using ToolRun = std::function<Status(ToolContext&)>;

struct ToolDesc {
  std::string id;           ///< unique; a package's tool starts with "<package id>."
  std::string title;        ///< menu text
  std::string icon;         ///< a file path or a symbolic name
  std::string description;
  std::string submenu;      ///< tools sharing a submenu title are grouped under it
  std::string package_id;   ///< empty for a tool registered by code
  ToolRun run;              ///< required
};

/// Owns the tool descriptors and hands out stable pointers.
class ToolRegistry {
 public:
  ToolRegistry() = default;
  ToolRegistry(const ToolRegistry&) = delete;
  ToolRegistry& operator=(const ToolRegistry&) = delete;

  /// kInvalidArg for an empty id or title, a null `run` or an id already taken.
  Status Register(ToolDesc desc);
  const ToolDesc* Find(const std::string& id) const;
  std::vector<const ToolDesc*> All() const;  ///< registration order
  size_t size() const { return order_.size(); }

 private:
  std::vector<std::unique_ptr<ToolDesc>> order_;
  std::unordered_map<std::string, ToolDesc*> by_id_;
};

/// Adds `run` to the process-wide builtin tool table under `name`.
/// kInvalidArg for an empty name, a null `run` or a name already taken.
Status RegisterBuiltinTool(const std::string& name, ToolRun run);
/// The behaviour registered under `name`, or null.
const ToolRun* FindBuiltinTool(const std::string& name);
/// Removes `name` from the table; for tests. False when absent.
bool UnregisterBuiltinTool(const std::string& name);

}  // namespace desk
}  // namespace fv
