// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "main_menu.h"

#include <gdk/gdk.h>

#include <cctype>
#include <cstdlib>
#include <utility>

namespace peregrine {
namespace {

/// A menu or submenu while it is assembled: items grouped into sections at
/// the model's separators. A node with no command is a submenu.
struct Node {
  std::string label;
  std::string command;
  bool checkable = false;
  std::vector<std::vector<Node>> sections;
};

/// GTK menu labels treat '_' as a mnemonic marker; a literal one is doubled.
Glib::ustring MenuLabel(const std::string& label) {
  std::string out;
  out.reserve(label.size());
  for (char c : label) {
    out += c;
    if (c == '_') out += '_';
  }
  return out;
}

/// The GDK key name for DeskKit's named keys; "" for one GDK does not have.
std::string NamedKey(const std::string& key) {
  static const std::map<std::string, std::string> kNames = {
      {"PageUp", "Page_Up"}, {"PageDown", "Page_Down"}, {"Home", "Home"},
      {"End", "End"},        {"Left", "Left"},          {"Right", "Right"},
      {"Up", "Up"},          {"Down", "Down"},          {"Delete", "Delete"},
      {"Escape", "Escape"},  {"Return", "Return"},      {"Tab", "Tab"},
      {"Space", "space"},
  };
  const auto it = kNames.find(key);
  if (it != kNames.end()) return it->second;
  if (key.size() >= 2 && key[0] == 'F') {
    char* end = nullptr;
    const long n = std::strtol(key.c_str() + 1, &end, 10);
    if (*end == '\0' && n >= 1 && n <= 35) return key;
  }
  return std::string();
}

}  // namespace

std::string AcceleratorFor(const std::string& key, unsigned modifiers) {
  std::string name;
  if (key.size() == 1) {
    // Letters are lower-cased: an upper-case keyval would imply Shift.
    const auto c = static_cast<unsigned char>(key[0]);
    const char* n = gdk_keyval_name(gdk_unicode_to_keyval(std::tolower(c)));
    if (n != nullptr) name = n;
  } else {
    name = NamedKey(key);
  }
  if (name.empty()) return std::string();
  std::string accel;
  if (modifiers & fv::desk::kHostPrimary) accel += "<Primary>";
  if (modifiers & fv::desk::kHostControl) accel += "<Control>";
  if (modifiers & fv::desk::kHostShift) accel += "<Shift>";
  if (modifiers & fv::desk::kHostAlt) accel += "<Alt>";
  return accel + name;
}

std::string ActionNameFor(const std::string& command_id) {
  std::string out = command_id;
  for (char& c : out) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.';
    if (!ok) c = '-';
  }
  return out;
}

CommandMenus::CommandMenus(fv::desk::DeskHost& host,
                           std::function<void(const std::string&)> execute)
    : host_(host),
      execute_(std::move(execute)),
      menu_bar_(Gio::Menu::create()),
      actions_(Gio::SimpleActionGroup::create()) {}

std::string CommandMenus::ActionFor(const std::string& command_id) const {
  const auto it = action_by_command_.find(command_id);
  if (it == action_by_command_.end()) return std::string();
  return std::string(kActionPrefix) + "." + it->second;
}

std::string CommandMenus::CommandFor(const std::string& action_name) const {
  const auto it = command_by_action_.find(action_name);
  return it == command_by_action_.end() ? std::string() : it->second;
}

/// Adds the action for one command entry, once per command.
void CommandMenus::AddAction(const fv::desk::HostMenuEntry& e) {
  if (action_by_command_.count(e.id)) return;
  std::string name = ActionNameFor(e.id);
  // Two ids that differ only in characters GAction names cannot hold.
  for (int n = 2; command_by_action_.count(name); ++n)
    name = ActionNameFor(e.id) + "-" + std::to_string(n);
  action_by_command_[e.id] = name;
  command_by_action_[name] = e.id;

  Glib::RefPtr<Gio::SimpleAction> action =
      e.checkable ? Gio::SimpleAction::create_bool(name, host_.IsChecked(e.id))
                  : Gio::SimpleAction::create(name);
  action->set_enabled(host_.IsEnabled(e.id));
  // Handling activate replaces the default toggle of a boolean action; the
  // state follows the host in Refresh.
  const std::string id = e.id;
  action->signal_activate().connect([this, id](const Glib::VariantBase&) { execute_(id); });
  actions_->add_action(action);

  const std::string accel = AcceleratorFor(e.key, e.modifiers);
  if (!accel.empty()) accelerators_.emplace_back(ActionFor(e.id), accel);
}

bool CommandMenus::Rebuild() {
  const uint64_t generation = host_.MenuGeneration();
  if (built_ && generation == generation_) return false;
  built_ = true;
  generation_ = generation;

  for (const auto& entry : command_by_action_) actions_->remove_action(entry.first);
  action_by_command_.clear();
  command_by_action_.clear();
  accelerators_.clear();

  // The entries are a preorder walk; a drop in depth closes submenus.
  std::vector<Node> tops;
  std::vector<Node*> open;
  const int count = host_.MenuEntryCount();
  for (int i = 0; i < count; ++i) {
    const fv::desk::HostMenuEntry e = host_.MenuEntryAt(i);
    if (e.kind == fv::desk::kMenuTop) {
      tops.push_back(Node{e.label, std::string(), false, {{}}});
      open.assign(1, &tops.back());
      // Earlier tops moved; only the newest is open.
      continue;
    }
    while (static_cast<int>(open.size()) > e.depth) open.pop_back();
    if (open.empty()) continue;
    Node* parent = open.back();
    switch (e.kind) {
      case fv::desk::kMenuSubmenu:
        parent->sections.back().push_back(Node{e.label, std::string(), false, {{}}});
        open.push_back(&parent->sections.back().back());
        break;
      case fv::desk::kMenuSeparator:
        parent->sections.emplace_back();
        break;
      case fv::desk::kMenuCommand:
        AddAction(e);
        parent->sections.back().push_back(Node{e.label, e.id, e.checkable, {}});
        break;
      default:
        break;
    }
  }

  std::function<Glib::RefPtr<Gio::Menu>(const Node&)> build = [&](const Node& node) {
    auto menu = Gio::Menu::create();
    for (const std::vector<Node>& items : node.sections) {
      auto section = Gio::Menu::create();
      for (const Node& item : items) {
        if (item.command.empty()) {
          auto sub = build(item);
          if (sub->get_n_items() > 0) section->append_submenu(MenuLabel(item.label), sub);
        } else {
          section->append(MenuLabel(item.label), ActionFor(item.command));
        }
      }
      // Empty sections are dropped, so no menu starts, ends or doubles a rule.
      if (section->get_n_items() > 0) menu->append_section(section);
    }
    return menu;
  };
  menu_bar_->remove_all();
  for (const Node& top : tops) {
    auto menu = build(top);
    if (menu->get_n_items() > 0) menu_bar_->append_submenu(MenuLabel(top.label), menu);
  }

  toolbar_.clear();
  const int tools = host_.ToolbarEntryCount();
  for (int i = 0; i < tools; ++i) {
    const fv::desk::HostMenuEntry e = host_.ToolbarEntryAt(i);
    if (e.kind != fv::desk::kMenuCommand) {
      if (!toolbar_.empty() && !toolbar_.back().command.empty()) toolbar_.push_back(ToolbarItem());
      continue;
    }
    AddAction(e);
    toolbar_.push_back(ToolbarItem{e.id, ActionFor(e.id), e.label, e.icon, e.checkable});
  }
  if (!toolbar_.empty() && toolbar_.back().command.empty()) toolbar_.pop_back();
  return true;
}

void CommandMenus::Refresh() {
  for (const auto& entry : command_by_action_) {
    auto action = std::dynamic_pointer_cast<Gio::SimpleAction>(actions_->lookup_action(entry.first));
    if (!action) continue;
    const bool enabled = host_.IsEnabled(entry.second);
    if (action->get_enabled() != enabled) action->set_enabled(enabled);
    if (g_action_get_state_type(G_ACTION(action->gobj())) == nullptr) continue;
    bool checked = false;
    action->get_state(checked);
    const bool now = host_.IsChecked(entry.second);
    if (checked != now) action->set_state(Glib::Variant<bool>::create(now));
  }
}

}  // namespace peregrine
