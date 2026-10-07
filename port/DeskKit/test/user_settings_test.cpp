// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include <fstream>

#include "fv_desk_user_settings.h"
#include "fvkit/settings.h"
#include "scratch.h"

using fv::desk::UserSettings;
using fv::desk::test::Scratch;

namespace {

TEST(UserSettings, MissingFileIsAnEmptyStore) {
  Scratch dir;
  UserSettings u;
  EXPECT_TRUE(u.Load(dir.Path("none.json")).ok());
  EXPECT_TRUE(u.Keys().empty());
  EXPECT_FALSE(u.dirty());
}

TEST(UserSettings, SaveAndLoadRoundTrip) {
  Scratch dir;
  const std::string path = dir.Path("deep/er/user-settings.json");
  UserSettings u;
  u.Set("Grid.Line_Color", "#FF000080");
  u.Set("osm.style", "/path/with \"quotes\" and # hash");
  EXPECT_TRUE(u.dirty());
  ASSERT_TRUE(u.Save(path).ok());
  EXPECT_FALSE(u.dirty());

  UserSettings back;
  ASSERT_TRUE(back.Load(path).ok());
  EXPECT_EQ(back.Keys(), (std::vector<std::string>{"grid.line_color", "osm.style"}));
  EXPECT_EQ(back.Get("GRID.line_color"), "#FF000080");
  EXPECT_EQ(back.Get("osm.style"), "/path/with \"quotes\" and # hash");
}

TEST(UserSettings, MalformedFileLeavesTheStoreAlone) {
  Scratch dir;
  const std::string path = dir.Path("bad.json");
  std::ofstream(path) << "{\"values\": {\"a\": 3}}";
  UserSettings u;
  u.Set("keep", "1");
  EXPECT_FALSE(u.Load(path).ok());
  EXPECT_EQ(u.Get("keep"), "1");
  std::ofstream(path) << "{\"version\": 99}";
  EXPECT_EQ(u.Load(path).code, fv::kUnsupported);
}

TEST(UserSettings, OnlyAChangeCounts) {
  UserSettings u;
  u.Set("a", "1");
  const uint64_t gen = u.Generation();
  u.Set("A", "1");
  EXPECT_EQ(u.Generation(), gen);
  u.Set("b", "");
  EXPECT_GT(u.Generation(), gen);
  EXPECT_TRUE(u.Has("b"));
}

TEST(UserSettings, AppliedValuesWinOverTheIniFile) {
  fv::Settings ini;
  ASSERT_TRUE(ini.LoadFromString("[grid]\nline_color = #FFFFFF60\nlabels = true\n").ok());
  UserSettings u;
  u.Set("grid.line_color", "#00FF00FF");
  u.ApplyTo(ini);
  EXPECT_EQ(ini.GetString("grid.line_color"), "#00FF00FF");
  EXPECT_TRUE(ini.GetBool("grid.labels", false));
}

TEST(UserSettings, CaptureAndErasePrefix) {
  fv::Settings ini;
  ASSERT_TRUE(ini.LoadFromString("[grid]\na = 1\nb = 2\n[osm]\nc = 3\n").ok());
  UserSettings u;
  u.CaptureFrom(ini, "grid.");
  EXPECT_EQ(u.Keys(), (std::vector<std::string>{"grid.a", "grid.b"}));
  u.Set("gridx.z", "9");
  EXPECT_EQ(u.ErasePrefix("grid."), 2u);
  EXPECT_EQ(u.Keys(), (std::vector<std::string>{"gridx.z"}));
}

TEST(UserSettings, DefaultPathIsInTheUserConfigDirectory) {
  const std::string p = fv::desk::DefaultUserSettingsPath();
  ASSERT_FALSE(p.empty());
  EXPECT_NE(p.find("user-settings.json"), std::string::npos);
  EXPECT_NE(p.find("eregrine"), std::string::npos);
}

}  // namespace
