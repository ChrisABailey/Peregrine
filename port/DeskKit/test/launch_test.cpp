// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include "fv_desk_launch.h"

using fv::desk::LaunchOptions;
using fv::desk::ParseLaunchOptions;

namespace {

TEST(LaunchOptions, NoArgumentsIsEmpty) {
  const LaunchOptions o = ParseLaunchOptions({"peregrine"});
  EXPECT_EQ(o.catalog, "");
  EXPECT_EQ(o.shot, "");
  EXPECT_FALSE(o.has_center);
  EXPECT_TRUE(ParseLaunchOptions({}).catalog.empty());
}

TEST(LaunchOptions, ReadsEveryOption) {
  const LaunchOptions o = ParseLaunchOptions(
      {"peregrine", "--catalog", "/data/maps.sqlite", "--center", "38.9,-77.04,250000",
       "--shot", "out.png"});
  EXPECT_EQ(o.catalog, "/data/maps.sqlite");
  EXPECT_EQ(o.shot, "out.png");
  ASSERT_TRUE(o.has_center);
  EXPECT_DOUBLE_EQ(o.center_lat, 38.9);
  EXPECT_DOUBLE_EQ(o.center_lon, -77.04);
  EXPECT_DOUBLE_EQ(o.center_scale, 250000);
}

TEST(LaunchOptions, CenterWithoutScaleKeepsTheScale) {
  const LaunchOptions o = ParseLaunchOptions({"p", "--center", "10,20"});
  ASSERT_TRUE(o.has_center);
  EXPECT_DOUBLE_EQ(o.center_lat, 10);
  EXPECT_DOUBLE_EQ(o.center_lon, 20);
  EXPECT_EQ(o.center_scale, 0);
}

TEST(LaunchOptions, MalformedCenterIsIgnored) {
  for (const char* bad : {"10", "10,", "10,x", "1,2,3,4", "", ",20", "nan,1"}) {
    EXPECT_FALSE(ParseLaunchOptions({"p", "--center", bad}).has_center) << bad;
  }
}

TEST(LaunchOptions, UnknownOptionsAndMissingValuesAreIgnored) {
  const LaunchOptions o =
      ParseLaunchOptions({"p", "--gapplication-service", "-v", "--catalog", "a.sqlite", "--shot"});
  EXPECT_EQ(o.catalog, "a.sqlite");
  EXPECT_EQ(o.shot, "");
}

TEST(LaunchOptions, LaterOptionWins) {
  const LaunchOptions o = ParseLaunchOptions({"p", "--catalog", "a", "--catalog", "b"});
  EXPECT_EQ(o.catalog, "b");
}

}  // namespace
