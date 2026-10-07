// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include "fv_desk_menu_model.h"

using fv::desk::Command;
using fv::desk::CommandRegistry;
using fv::desk::Menu;
using fv::desk::MenuItem;
using fv::desk::MenuModel;
using fv::desk::MenuSlot;
using fv::desk::MenuSpec;

namespace {

void Add(CommandRegistry& r, const std::string& id, std::function<bool()> enabled = nullptr) {
  Command c;
  c.id = id;
  c.label = "L:" + id;
  c.action = [] {};
  c.enabled = std::move(enabled);
  ASSERT_TRUE(r.Register(c).ok());
}

std::vector<std::string> Shape(const std::vector<MenuItem>& items) {
  std::vector<std::string> out;
  for (const MenuItem& m : items) {
    if (m.is_separator()) out.push_back("-");
    else if (!m.children.empty()) out.push_back(m.label + "{" + std::to_string(m.children.size()) + "}");
    else out.push_back(m.command_id);
  }
  return out;
}

TEST(MenuModel, LeavesOutWhatIsNotRegistered) {
  CommandRegistry r;
  Add(r, "a");
  Add(r, "x.2");
  Add(r, "x.1");
  using S = MenuSlot;
  MenuModel m(r, {{"m", "M",
                   {S::Sep(), S::Cmd("a"), S::Sep(), S::Sep(), S::Cmd("missing"),
                    S::Sub("Empty", {S::Cmd("missing")}), S::Sep(), S::Expand("x."), S::Sep()}}});
  ASSERT_TRUE(m.Rebuild());
  const Menu* menu = m.Find("m");
  ASSERT_NE(menu, nullptr);
  EXPECT_EQ(Shape(menu->items), (std::vector<std::string>{"a", "-", "x.2", "x.1"}));
  EXPECT_EQ(menu->items[0].label, "L:a");
}

TEST(MenuModel, RebuildSignalsOnlyARealChange) {
  CommandRegistry r;
  bool enabled = true;
  Add(r, "a", [&enabled] { return enabled; });
  MenuModel m(r, {{"m", "M", {MenuSlot::Expand("")}}});
  int changes = 0;
  m.SetOnChange([&changes] { ++changes; });
  EXPECT_TRUE(m.Rebuild());
  EXPECT_EQ(changes, 1);
  // Enabled state is read live by the shell; it is not a structural change.
  enabled = false;
  EXPECT_FALSE(m.Rebuild());
  EXPECT_EQ(changes, 1);
  Add(r, "b");
  const uint64_t gen = m.Generation();
  EXPECT_TRUE(m.Rebuild());
  EXPECT_EQ(changes, 2);
  EXPECT_GT(m.Generation(), gen);
}

TEST(MenuModel, EditorMenuComesLast) {
  CommandRegistry r;
  Add(r, "file.save");
  Add(r, "editor.tool.0");
  MenuModel m(r);
  MenuSpec editor{"editor", "Route", {MenuSlot::Cmd("editor.tool.0")}};
  ASSERT_TRUE(m.Rebuild(&editor));
  ASSERT_EQ(m.Menus().back().id, "editor");
  EXPECT_EQ(m.Menus().back().title, "Route");
  EXPECT_TRUE(m.Rebuild());
  EXPECT_EQ(m.Find("editor"), nullptr);
}

TEST(MenuModel, DefaultLayoutHasTheThreeMenus) {
  CommandRegistry r;
  MenuModel m(r);
  m.Rebuild();
  ASSERT_EQ(m.Menus().size(), 3u);
  EXPECT_EQ(m.Menus()[0].title, "File");
  EXPECT_EQ(m.Menus()[1].title, "Map");
  EXPECT_EQ(m.Menus()[2].title, "Overlay");
  EXPECT_TRUE(m.Menus()[1].items.empty());
}

}  // namespace
