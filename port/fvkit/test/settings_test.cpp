// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::Settings tests — the registry replacement.
//
// Hermetic: every case is a string or a file this test writes itself. The
// point of a settings file is that a human types into it, so most of what is
// pinned here is what happens when they type something slightly wrong.

#include "fvkit/settings.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

fv::Settings Parse(const std::string& text) {
  fv::Settings s;
  EXPECT_TRUE(s.LoadFromString(text).ok()) << text;
  return s;
}

// --- format ----------------------------------------------------------------

TEST(Settings, SectionsPrefixTheirKeys) {
  const fv::Settings s = Parse(
      "loose = 1\n"
      "[vector]\n"
      "scene_margin = 0.25\n"
      "[geosym]\n"
      "brightness = 10\n");
  EXPECT_TRUE(s.Has("loose"));
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.scene_margin", 0.0), 0.25);
  EXPECT_EQ(s.GetInt("geosym.brightness", 0), 10);
  EXPECT_FALSE(s.Has("scene_margin")) << "a section is a prefix, not a scope";
}

TEST(Settings, CommentsAndBlankLinesAreIgnored) {
  const fv::Settings s = Parse(
      "# a full-line comment\n"
      "\n"
      "   ; another\n"
      "[vector]\n"
      "simplify_pixels = 0.5   # trailing comment\n"
      "scene_margin = 0.25     ; and this kind\n");
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.simplify_pixels", 0.0), 0.5);
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.scene_margin", 0.0), 0.25);
}

TEST(Settings, QuotesKeepSpacesAndHashes) {
  // Real paths have both, which is the reason quoting exists at all.
  const fv::Settings s = Parse(
      "[geosym]\n"
      "data_dir = \"/Volumes/My Disk/Geo # Sym\"\n"
      "other = '/tmp/single quoted'\n");
  EXPECT_EQ(s.GetString("geosym.data_dir"), "/Volumes/My Disk/Geo # Sym");
  EXPECT_EQ(s.GetString("geosym.other"), "/tmp/single quoted");
}

TEST(Settings, KeysAndSectionsAreCaseInsensitive) {
  const fv::Settings s = Parse(
      "[Vector]\n"
      "Scene_Margin = 0.25\n");
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.scene_margin", 0.0), 0.25);
  EXPECT_DOUBLE_EQ(s.GetDouble("VECTOR.SCENE_MARGIN", 0.0), 0.25);
  EXPECT_EQ(s.GetString("vector.scene_margin"), "0.25")
      << "the VALUE keeps its case";
}

TEST(Settings, TheLastAssignmentWins) {
  const fv::Settings s = Parse(
      "[vector]\n"
      "scene_margin = 0.25\n"
      "scene_margin = 0.5\n");
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.scene_margin", 0.0), 0.5);
}

TEST(Settings, UnknownKeysAreKeptNotRejected) {
  // A key no code reads is how a file survives a downgrade.
  const fv::Settings s = Parse(
      "[future]\n"
      "not_invented_yet = 3\n");
  EXPECT_TRUE(s.Has("future.not_invented_yet"));
  ASSERT_EQ(s.Keys().size(), 1u);
  EXPECT_EQ(s.Keys()[0], "future.not_invented_yet");
}

// --- malformed input -------------------------------------------------------

TEST(Settings, AMalformedLineFailsTheWholeLoadWithItsNumber) {
  fv::Settings s;
  const fv::Status st = s.LoadFromString(
      "[vector]\n"
      "scene_margin = 0.25\n"
      "this line has no equals sign\n",
      "test.ini");
  ASSERT_FALSE(st.ok());
  EXPECT_EQ(st.code, fv::kInvalidArg);
  EXPECT_NE(st.message.find("test.ini:3"), std::string::npos) << st.message;
}

TEST(Settings, AFailedLoadDoesNotDisturbWhatWasAlreadyThere) {
  fv::Settings s;
  ASSERT_TRUE(s.LoadFromString("[vector]\nscene_margin = 0.25\n").ok());
  EXPECT_FALSE(s.LoadFromString("junk\n").ok());
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.scene_margin", 0.0), 0.25)
      << "a bad edit must not silently blank the settings";
}

TEST(Settings, AnUnterminatedSectionIsAnError) {
  fv::Settings s;
  EXPECT_FALSE(s.LoadFromString("[vector\nkey = 1\n").ok());
  EXPECT_FALSE(s.LoadFromString("[]\nkey = 1\n").ok());
  EXPECT_FALSE(s.LoadFromString("= 1\n").ok());
}

// --- typed reads -----------------------------------------------------------

TEST(Settings, AnAbsentKeyReturnsTheCallersDefaultSilently) {
  const fv::Settings s = Parse("[vector]\n");
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.nope", 1.5), 1.5);
  EXPECT_EQ(s.GetInt("vector.nope", 7), 7);
  EXPECT_TRUE(s.GetBool("vector.nope", true));
  EXPECT_EQ(s.GetString("vector.nope", "fallback"), "fallback");
  EXPECT_TRUE(s.warnings().empty()) << "absent is not a mistake";
}

TEST(Settings, AValueOfTheWrongTypeWarnsAndFallsBack) {
  // The failure that matters: a typo must neither abort startup nor be
  // silently wrong.
  const fv::Settings s = Parse(
      "[vector]\n"
      "scene_margin = wide\n");
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.scene_margin", 0.25), 0.25);
  ASSERT_EQ(s.warnings().size(), 1u);
  EXPECT_NE(s.warnings()[0].find("vector.scene_margin"), std::string::npos);
  EXPECT_NE(s.warnings()[0].find("wide"), std::string::npos);
}

TEST(Settings, TrailingGarbageOnANumberIsNotAcceptedQuietly) {
  // "0.25px" must not read as 0.25 — that is how a unit mistake survives.
  const fv::Settings s = Parse(
      "[vector]\n"
      "a = 0.25px\n"
      "b = 12abc\n");
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.a", -1.0), -1.0);
  EXPECT_EQ(s.GetInt("vector.b", -1), -1);
  EXPECT_EQ(s.warnings().size(), 2u);
}

TEST(Settings, BoolsAcceptTheUsualSpellings) {
  const fv::Settings s = Parse(
      "[flags]\n"
      "a = true\nb = YES\nc = On\nd = 1\n"
      "e = false\nf = no\ng = OFF\nh = 0\n");
  for (const char* k : {"flags.a", "flags.b", "flags.c", "flags.d"})
    EXPECT_TRUE(s.GetBool(k, false)) << k;
  for (const char* k : {"flags.e", "flags.f", "flags.g", "flags.h"})
    EXPECT_FALSE(s.GetBool(k, true)) << k;
  EXPECT_TRUE(s.warnings().empty());
}

TEST(Settings, SetOverridesTheFileInMemory) {
  fv::Settings s = Parse("[vector]\nscene_margin = 0.25\n");
  s.Set("vector.scene_margin", "0.75");
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.scene_margin", 0.0), 0.75);
}

// --- files and the search path ---------------------------------------------

std::string TempFile(const std::string& body) {
  const char* dir = std::getenv("TMPDIR");
  std::string path = (dir != nullptr ? std::string(dir) : "/tmp/");
  if (path.back() != '/') path += '/';
  path += "fv_settings_test_" + std::to_string(::getpid()) + ".ini";
  std::ofstream f(path.c_str());
  f << body;
  return path;
}

TEST(Settings, LoadReportsTheFileItRead) {
  const std::string path = TempFile("[vector]\nsimplify_pixels = 0.5\n");
  fv::Settings s;
  ASSERT_TRUE(s.Load(path).ok());
  EXPECT_EQ(s.path(), path);
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.simplify_pixels", 0.0), 0.5);
  std::remove(path.c_str());
}

TEST(Settings, AnExplicitlyNamedFileThatIsMissingIsAnError) {
  fv::Settings s;
  const fv::Status st = s.Load("/nonexistent/peregrine.ini");
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code, fv::kNotFound);
}

TEST(Settings, LoadDefaultFindsTheEnvOverride) {
  const std::string path = TempFile("[vector]\nscene_margin = 0.4\n");
  setenv("FVW_SETTINGS", path.c_str(), 1);

  ASSERT_FALSE(fv::DefaultSettingsPaths().empty());
  EXPECT_EQ(fv::DefaultSettingsPaths()[0], path) << "the override comes first";

  fv::Settings s;
  ASSERT_TRUE(s.LoadDefault().ok());
  EXPECT_EQ(s.path(), path);
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.scene_margin", 0.0), 0.4);

  unsetenv("FVW_SETTINGS");
  std::remove(path.c_str());
}

TEST(Settings, LoadDefaultWithNoFileAnywhereIsNotAFailure) {
  setenv("FVW_SETTINGS", "/nonexistent/peregrine.ini", 1);
  // Also neutralize the home-directory candidate, so a developer who really
  // has a settings file does not fail this test.
  const char* saved_home = std::getenv("HOME");
  const std::string home = saved_home != nullptr ? saved_home : "";
  setenv("HOME", "/nonexistent", 1);
  setenv("XDG_CONFIG_HOME", "/nonexistent", 1);

  fv::Settings s;
  EXPECT_TRUE(s.LoadDefault().ok()) << "running unconfigured is normal";
  EXPECT_TRUE(s.path().empty());
  EXPECT_TRUE(s.Keys().empty());
  EXPECT_DOUBLE_EQ(s.GetDouble("vector.scene_margin", 0.25), 0.25);

  unsetenv("FVW_SETTINGS");
  unsetenv("XDG_CONFIG_HOME");
  if (!home.empty()) setenv("HOME", home.c_str(), 1);
}

TEST(Settings, TheSearchPathIsListableForAnErrorMessage) {
  // An app should be able to say "put it in one of these".
  const std::vector<std::string> paths = fv::DefaultSettingsPaths();
  ASSERT_GE(paths.size(), 2u);
  bool saw_cwd = false;
  for (const std::string& p : paths)
    if (p == "peregrine.ini") saw_cwd = true;
  EXPECT_TRUE(saw_cwd);
}

}  // namespace
