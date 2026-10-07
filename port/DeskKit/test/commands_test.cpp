// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include "fv_desk_commands.h"

using fv::desk::Command;
using fv::desk::CommandRegistry;
using fv::desk::Shortcut;

namespace {

Command Make(const std::string& id, std::function<void()> action = [] {}) {
  Command c;
  c.id = id;
  c.label = id;
  c.action = std::move(action);
  return c;
}

TEST(Shortcut, RoundTripsThroughText) {
  Shortcut s;
  ASSERT_TRUE(Shortcut::Parse("primary+shift+s", &s));
  EXPECT_EQ(s.key, "S");
  EXPECT_EQ(s.modifiers, fv::desk::kPrimary | fv::desk::kShift);
  EXPECT_EQ(s.ToString(), "Primary+Shift+S");
  Shortcut back;
  ASSERT_TRUE(Shortcut::Parse(s.ToString(), &back));
  EXPECT_EQ(back, s);

  ASSERT_TRUE(Shortcut::Parse("Primary++", &s));
  EXPECT_EQ(s.key, "+");
  ASSERT_TRUE(Shortcut::Parse("PageUp", &s));
  EXPECT_EQ(s.key, "PageUp");
  EXPECT_EQ(s.modifiers, 0u);

  EXPECT_FALSE(Shortcut::Parse("Hyper+S", &s));
  EXPECT_FALSE(Shortcut::Parse("", &s));
  EXPECT_FALSE(Shortcut::Parse("+S", &s));
}

TEST(CommandRegistry, RegisterRejectsBadCommands) {
  CommandRegistry r;
  EXPECT_TRUE(r.Register(Make("a")).ok());
  EXPECT_FALSE(r.Register(Make("a")).ok());
  EXPECT_FALSE(r.Register(Make("")).ok());
  Command no_action;
  no_action.id = "b";
  EXPECT_FALSE(r.Register(no_action).ok());
  EXPECT_EQ(r.Ids(), std::vector<std::string>{"a"});
}

TEST(CommandRegistry, ASecondHolderOfAShortcutLosesIt) {
  CommandRegistry r;
  Command a = Make("a"), b = Make("b");
  Shortcut::Parse("Primary+S", &a.shortcut);
  b.shortcut = a.shortcut;
  ASSERT_TRUE(r.Register(a).ok());
  ASSERT_TRUE(r.Register(b).ok());
  EXPECT_EQ(r.FindByShortcut(a.shortcut)->id, "a");
  EXPECT_TRUE(r.Find("b")->shortcut.empty());
  ASSERT_EQ(r.warnings().size(), 1u);
}

TEST(CommandRegistry, ExecuteHonoursEnabled) {
  CommandRegistry r;
  int runs = 0;
  bool on = false;
  Command c = Make("go", [&runs] { ++runs; });
  c.enabled = [&on] { return on; };
  ASSERT_TRUE(r.Register(c).ok());
  EXPECT_EQ(r.Execute("go").code, fv::kUnsupported);
  EXPECT_EQ(runs, 0);
  on = true;
  EXPECT_TRUE(r.Execute("go").ok());
  EXPECT_EQ(runs, 1);
  EXPECT_EQ(r.Execute("nope").code, fv::kNotFound);
  EXPECT_FALSE(r.IsEnabled("nope"));
  EXPECT_FALSE(r.IsChecked("go"));
}

TEST(CommandRegistry, PrefixesKeepRegistrationOrder) {
  CommandRegistry r;
  for (const char* id : {"map.group.b", "map.zoom_in", "map.group.a", "overlay.x"})
    ASSERT_TRUE(r.Register(Make(id)).ok());
  EXPECT_EQ(r.IdsWithPrefix("map.group."),
            (std::vector<std::string>{"map.group.b", "map.group.a"}));
  const uint64_t gen = r.Generation();
  EXPECT_EQ(r.RemovePrefix("map.group."), 2u);
  EXPECT_GT(r.Generation(), gen);
  EXPECT_EQ(r.Ids(), (std::vector<std::string>{"map.zoom_in", "overlay.x"}));
  EXPECT_FALSE(r.Remove("map.group.a"));
}

TEST(CommandRegistry, AnActionMayRemoveItsOwnCommand) {
  CommandRegistry r;
  int runs = 0;
  ASSERT_TRUE(r.Register(Make("self", [&r, &runs] {
                 r.Remove("self");
                 ++runs;
               })).ok());
  EXPECT_TRUE(r.Execute("self").ok());
  EXPECT_EQ(runs, 1);
  EXPECT_EQ(r.Find("self"), nullptr);
}

}  // namespace
