// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_tools.h"

#include <filesystem>

#include <map>
#include <mutex>

#include "fv_desk_desk.h"

namespace fv {
namespace desk {
namespace {

struct BuiltinToolTable {
  std::mutex mutex;
  std::map<std::string, ToolRun> entries;
};

BuiltinToolTable& BuiltinTools() {
  static BuiltinToolTable table;
  return table;
}

}  // namespace

Status ToolContext::OpenFile(const std::string& path) const {
  const std::string ext = std::filesystem::path(path).extension().string();
  const app::OverlayTypeDesc* type = desk_.types().FindByExtension(ext);
  if (type == nullptr) return Status::Error(kUnsupported, "no overlay type opens " + path);
  switch (desk_.session().OpenFile(type->id, path)) {
    case app::FlowResult::kDone: return Status::Ok();
    case app::FlowResult::kCanceled: return Status::Error(kInterrupted, "cancelled");
    default: return Status::Error(kIoError, "could not open " + path);
  }
}

void ToolContext::RequestRedraw() const { desk_.shell().RequestInvalidate(); }

Status ToolRegistry::Register(ToolDesc desc) {
  if (desc.id.empty()) return Status::Error(kInvalidArg, "tool with no id");
  if (desc.title.empty()) return Status::Error(kInvalidArg, "tool '" + desc.id + "' has no title");
  if (!desc.run) return Status::Error(kInvalidArg, "tool '" + desc.id + "' has nothing to run");
  if (by_id_.count(desc.id) != 0)
    return Status::Error(kInvalidArg, "tool '" + desc.id + "' is already registered");
  order_.push_back(std::make_unique<ToolDesc>(std::move(desc)));
  ToolDesc* d = order_.back().get();
  by_id_.emplace(d->id, d);
  return Status::Ok();
}

const ToolDesc* ToolRegistry::Find(const std::string& id) const {
  auto it = by_id_.find(id);
  return it == by_id_.end() ? nullptr : it->second;
}

std::vector<const ToolDesc*> ToolRegistry::All() const {
  std::vector<const ToolDesc*> v;
  v.reserve(order_.size());
  for (const auto& d : order_) v.push_back(d.get());
  return v;
}

Status RegisterBuiltinTool(const std::string& name, ToolRun run) {
  if (name.empty()) return Status::Error(kInvalidArg, "builtin tool with no name");
  if (!run) return Status::Error(kInvalidArg, "builtin tool '" + name + "' has nothing to run");
  BuiltinToolTable& t = BuiltinTools();
  std::lock_guard<std::mutex> lock(t.mutex);
  if (!t.entries.emplace(name, std::move(run)).second)
    return Status::Error(kInvalidArg, "builtin tool '" + name + "' is already registered");
  return Status::Ok();
}

const ToolRun* FindBuiltinTool(const std::string& name) {
  BuiltinToolTable& t = BuiltinTools();
  std::lock_guard<std::mutex> lock(t.mutex);
  // std::map nodes are stable, so the pointer outlives the lock.
  auto it = t.entries.find(name);
  return it == t.entries.end() ? nullptr : &it->second;
}

bool UnregisterBuiltinTool(const std::string& name) {
  BuiltinToolTable& t = BuiltinTools();
  std::lock_guard<std::mutex> lock(t.mutex);
  return t.entries.erase(name) != 0;
}

}  // namespace desk
}  // namespace fv
