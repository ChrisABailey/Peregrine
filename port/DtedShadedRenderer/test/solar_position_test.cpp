// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Tests for fv::SolarAzimuthElevation (the NOAA solar-position replacement for
// SLAC). All hermetic — no data files. Expected values are grounded in
// astronomy (solstice/equinox declination, sun-rises-in-the-east), not in the
// function's own output, so they actually pin correctness.

#include "fv_solar_position.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

using fv::SolarAzimuthElevation;
using fv::SolarPosition;

// Angular distance to the nearest of 0/360 (for "sun is due north" checks).
double DistToNorth(double az) { return std::min(az, 360.0 - az); }

TEST(SolarPosition, SummerSolsticeNoonAtEquatorSunIsNorthAndTilted) {
  // 2001-06-21 solar noon at lon 0: declination ~ +23.44, so at the equator the
  // sun sits 23.44 deg south of the zenith -> elevation ~ 66.5, to the NORTH.
  SolarPosition p = SolarAzimuthElevation(2001, 6, 21, 12.0, 0.0, 0.0);
  EXPECT_NEAR(p.elevation_deg, 66.5, 0.4);
  EXPECT_LT(DistToNorth(p.azimuth_deg), 5.0);
}

TEST(SolarPosition, WinterSolsticeNoonAtEquatorSunIsSouth) {
  // 2001-12-21 solar noon: declination ~ -23.44 -> elevation ~ 66.5, to the
  // SOUTH (azimuth ~ 180).
  SolarPosition p = SolarAzimuthElevation(2001, 12, 21, 12.0, 0.0, 0.0);
  EXPECT_NEAR(p.elevation_deg, 66.5, 0.5);
  EXPECT_NEAR(p.azimuth_deg, 180.0, 5.0);
}

TEST(SolarPosition, SolsticeNoonAtTropicOfCancerNearZenith) {
  // Sun overhead the Tropic of Cancer at N-summer solstice noon.
  SolarPosition p = SolarAzimuthElevation(2001, 6, 21, 12.0, 23.44, 0.0);
  EXPECT_GT(p.elevation_deg, 88.0);
}

TEST(SolarPosition, SunRisesInTheEastSetsInTheWest) {
  // Mid-morning at lon 0 (08:00 UTC, solar noon ~12:00): sun up, eastern half.
  SolarPosition morning = SolarAzimuthElevation(2001, 3, 20, 8.0, 0.0, 0.0);
  EXPECT_GT(morning.elevation_deg, 0.0);
  EXPECT_LT(morning.azimuth_deg, 180.0);
  // Mid-afternoon (16:00 UTC): sun up, western half (az > 180).
  SolarPosition afternoon = SolarAzimuthElevation(2001, 3, 20, 16.0, 0.0, 0.0);
  EXPECT_GT(afternoon.elevation_deg, 0.0);
  EXPECT_GT(afternoon.azimuth_deg, 180.0);
}

TEST(SolarPosition, NightGivesNegativeElevation) {
  // Local midnight at lon 0 (00:00 UTC): sun below the horizon.
  SolarPosition p = SolarAzimuthElevation(2001, 3, 20, 0.0, 0.0, 0.0);
  EXPECT_LT(p.elevation_deg, 0.0);
}

TEST(SolarPosition, OutputsAlwaysInRange) {
  for (int hour = 0; hour < 24; ++hour) {
    for (double lat = -80; lat <= 80; lat += 20) {
      SolarPosition p = SolarAzimuthElevation(2020, 9, 15, hour, lat, -75.0);
      EXPECT_GE(p.azimuth_deg, 0.0);
      EXPECT_LT(p.azimuth_deg, 360.0);
      EXPECT_GE(p.elevation_deg, -90.0);
      EXPECT_LE(p.elevation_deg, 90.0);
    }
  }
}

}  // namespace
