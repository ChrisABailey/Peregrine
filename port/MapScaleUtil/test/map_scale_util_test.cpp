// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Tests for the portable MapScaleUtil (extracted from CMapScaleUtil).
// Pinned values were captured from this port on macOS and preserve the
// original's numeric quirks (documented in fv_map_scale_util.cpp); they
// should match the Windows COM server bit-for-bit.

#include "fv_map_scale_util.h"

#include <gtest/gtest.h>

namespace {

TEST(MapScaleUtilTest, ResolutionToScalePinnedValues) {
  fv::MapScaleUtil u;
  double s = 0;

  ASSERT_EQ(S_OK, u.ResolutionToScale(1.0, MAP_SCALE_KILOMETER, &s));
  EXPECT_DOUBLE_EQ(s, 6714151.0);

  ASSERT_EQ(S_OK, u.ResolutionToScale(100.0, MAP_SCALE_METERS, &s));
  EXPECT_DOUBLE_EQ(s, 671415.0);

  ASSERT_EQ(S_OK, u.ResolutionToScale(5.0, MAP_SCALE_NM, &s));
  EXPECT_DOUBLE_EQ(s, 62173043.0);

  ASSERT_EQ(S_OK, u.ResolutionToScale(1.0, MAP_SCALE_ARC_DEGREES, &s));
  EXPECT_DOUBLE_EQ(s, 742400000.0);

  ASSERT_EQ(S_OK, u.ResolutionToScale(1.0, MAP_SCALE_FEET, &s));
  EXPECT_DOUBLE_EQ(s, 2046.0);
}

TEST(MapScaleUtilTest, DenominatorPassesThrough) {
  fv::MapScaleUtil u;
  double s = 0;
  ASSERT_EQ(S_OK, u.ResolutionToScale(250000.0, MAP_SCALE_DENOMINATOR, &s));
  EXPECT_DOUBLE_EQ(s, 250000.0);
}

TEST(MapScaleUtilTest, WorldScaleIsWindowsLongMax) {
  fv::MapScaleUtil u;
  double s = 0;
  ASSERT_EQ(S_OK, u.ResolutionToScale(1.0, MAP_SCALE_WORLD, &s));
  // 2^31-1: LONG_MAX on Windows (32-bit long), NOT LP64 LONG_MAX
  EXPECT_DOUBLE_EQ(s, 2147483647.0);
}

// Preserved bug: statute miles are converted with the feet factor, so
// 1 mile and 1 foot produce the same scale. If this test ever fails because
// the values differ, someone fixed the bug on one platform only — see the
// NOTE in fv_map_scale_util.cpp.
TEST(MapScaleUtilTest, PreservedMileAsFeetBug) {
  fv::MapScaleUtil u;
  double mile = 0, feet = 0;
  ASSERT_EQ(S_OK, u.ResolutionToScale(1.0, MAP_SCALE_MILE, &mile));
  ASSERT_EQ(S_OK, u.ResolutionToScale(1.0, MAP_SCALE_FEET, &feet));
  EXPECT_DOUBLE_EQ(mile, feet);
  EXPECT_DOUBLE_EQ(mile, 2046.0);
}

TEST(MapScaleUtilTest, InvalidResolutionRejected) {
  fv::MapScaleUtil u;
  double s = 0;
  EXPECT_EQ(E_INVALIDARG, u.ResolutionToScale(0.0, MAP_SCALE_METERS, &s));
  EXPECT_EQ(E_INVALIDARG, u.ResolutionToScale(-1.0, MAP_SCALE_METERS, &s));
}

TEST(MapScaleUtilTest, DegreesPerPixelPinnedValues) {
  fv::MapScaleUtil u;
  double dlat = 0, dlon = 0;

  // equator, 1:1M — lat/lon dpp nearly equal (small WGS-84 eccentricity gap)
  ASSERT_EQ(S_OK, u.GetDegreesPerPixel(0.0, 1000000.0, MAP_SCALE_DENOMINATOR,
                                       &dlat, &dlon));
  EXPECT_NEAR(dlat, 0.00134698275862, 1e-14);
  EXPECT_NEAR(dlon, 0.00137376187162, 1e-14);

  // 60N — longitude pixels cover ~2.3x more degrees
  ASSERT_EQ(S_OK, u.GetDegreesPerPixel(60.0, 1000000.0, MAP_SCALE_DENOMINATOR,
                                       &dlat, &dlon));
  EXPECT_NEAR(dlat, 0.00134698275862, 1e-14);
  EXPECT_NEAR(dlon, 0.00310527196809, 1e-14);

  // beyond 1:10M the ratio is forced to 1 (original behavior)
  ASSERT_EQ(S_OK, u.GetDegreesPerPixel(0.0, 50000000.0, MAP_SCALE_DENOMINATOR,
                                       &dlat, &dlon));
  EXPECT_DOUBLE_EQ(dlat, dlon);
  EXPECT_NEAR(dlat, 0.0670226213262, 1e-13);
}

TEST(MapScaleUtilTest, DegreesPerPixelWorldExact) {
  fv::MapScaleUtil u;
  double dlat = 0, dlon = 0;
  ASSERT_EQ(S_OK, u.GetDegreesPerPixelWorld(1024, 768, &dlat, &dlon));
  EXPECT_DOUBLE_EQ(dlat, 180.0 / 768.0);
  EXPECT_DOUBLE_EQ(dlon, 360.0 / 1024.0);

  EXPECT_EQ(E_INVALIDARG, u.GetDegreesPerPixelWorld(0, 768, &dlat, &dlon));
  EXPECT_EQ(E_INVALIDARG, u.GetDegreesPerPixelWorld(1024, -1, &dlat, &dlon));
}

TEST(MapScaleUtilTest, WorldUnitsRejectedForDegreesPerPixel) {
  fv::MapScaleUtil u;
  double dlat = 0, dlon = 0;
  EXPECT_EQ(E_INVALIDARG, u.GetDegreesPerPixel(0.0, 1.0, MAP_SCALE_WORLD,
                                               &dlat, &dlon));
}

}  // namespace

// Appended: invalid enum values propagate E_INVALIDARG out of
// ResolutionToScale (the COM original threw), unlike Vincenty failures which
// yield scale 0 / S_OK.
TEST(MapScaleUtilTest2, InvalidUnitsEnumRejected) {
  fv::MapScaleUtil u;
  double s = 123;
  EXPECT_EQ(E_INVALIDARG,
            u.ResolutionToScale(1.0, static_cast<MapScaleUnitsEnum>(99), &s));
}
