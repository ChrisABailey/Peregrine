// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_commands.h"

#include <algorithm>
#include <cctype>

namespace fv {
namespace desk {
namespace {

struct ModName {
  unsigned bit;
  const char* name;
};
// Spelling order of ToString().
constexpr ModName kMods[] = {
    {kPrimary, "Primary"}, {kControl, "Control"}, {kAlt, "Alt"}, {kShift, "Shift"}};

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

std::string Shortcut::ToString() const {
  if (key.empty()) return std::string();
  std::string out;
  for (const ModName& m : kMods) {
    if (modifiers & m.bit) {
      out += m.name;
      out += '+';
    }
  }
  return out + key;
}

bool Shortcut::Parse(const std::string& text, Shortcut* out) {
  if (out == nullptr || text.empty()) return false;
  Shortcut s;
  size_t start = 0;
  // The key is everything after the last '+' that is not itself the key, so
  // "Primary++" binds the plus key.
  while (true) {
    const size_t plus = text.find('+', start);
    if (plus == std::string::npos || plus == text.size() - 1) {
      s.key = text.substr(start);
      break;
    }
    if (plus == start) return false;  // empty modifier name
    const std::string name = Lower(text.substr(start, plus - start));
    bool known = false;
    for (const ModName& m : kMods) {
      if (name == Lower(m.name)) {
        s.modifiers |= m.bit;
        known = true;
      }
    }
    if (!known) return false;
    start = plus + 1;
  }
  if (s.key.empty()) return false;
  if (s.key.size() == 1)
    s.key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s.key[0])));
  *out = s;
  return true;
}

Status CommandRegistry::Register(Command c) {
  if (c.id.empty()) return Status::Error(kInvalidArg, "command id is empty");
  if (by_id_.count(c.id)) return Status::Error(kInvalidArg, "command '" + c.id + "' already registered");
  if (!c.action) return Status::Error(kInvalidArg, "command '" + c.id + "' has no action");
  if (!c.shortcut.empty()) {
    if (const Command* holder = FindByShortcut(c.shortcut)) {
      warnings_.push_back("shortcut " + c.shortcut.ToString() + " of '" + c.id +
                          "' is already held by '" + holder->id + "'");
      c.shortcut = Shortcut();
    }
  }
  auto owned = std::make_unique<Command>(std::move(c));
  by_id_[owned->id] = owned.get();
  order_.push_back(std::move(owned));
  ++generation_;
  return Status::Ok();
}

bool CommandRegistry::Remove(const std::string& id) {
  auto it = by_id_.find(id);
  if (it == by_id_.end()) return false;
  const Command* target = it->second;
  by_id_.erase(it);
  order_.erase(std::find_if(order_.begin(), order_.end(),
                            [target](const std::unique_ptr<Command>& p) { return p.get() == target; }));
  ++generation_;
  return true;
}

size_t CommandRegistry::RemovePrefix(const std::string& prefix) {
  size_t n = 0;
  for (const std::string& id : IdsWithPrefix(prefix)) n += Remove(id) ? 1 : 0;
  return n;
}

const Command* CommandRegistry::Find(const std::string& id) const {
  auto it = by_id_.find(id);
  return it == by_id_.end() ? nullptr : it->second;
}

const Command* CommandRegistry::FindByShortcut(const Shortcut& s) const {
  if (s.empty()) return nullptr;
  for (const auto& c : order_)
    if (c->shortcut == s) return c.get();
  return nullptr;
}

std::vector<std::string> CommandRegistry::IdsWithPrefix(const std::string& prefix) const {
  std::vector<std::string> out;
  for (const auto& c : order_)
    if (c->id.compare(0, prefix.size(), prefix) == 0) out.push_back(c->id);
  return out;
}

bool CommandRegistry::IsEnabled(const std::string& id) const {
  const Command* c = Find(id);
  return c != nullptr && (!c->enabled || c->enabled());
}

bool CommandRegistry::IsChecked(const std::string& id) const {
  const Command* c = Find(id);
  return c != nullptr && c->checked && c->checked();
}

Status CommandRegistry::Execute(const std::string& id) const {
  const Command* c = Find(id);
  if (c == nullptr) return Status::Error(kNotFound, "no command '" + id + "'");
  if (c->enabled && !c->enabled())
    return Status::Error(kUnsupported, "command '" + id + "' is disabled");
  // Copy: the action may re-register commands (a menu rebuild) and free *c.
  const std::function<void()> action = c->action;
  action();
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
