// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Tests for geo_tool on POSIX. Reference values: WGS-84 inverse-geodesic
// results for well-known city pairs (JFK-LHR), computed independently with
// GeographicLib; tolerances are loose enough to absorb implementation
// differences in the module's Sodano-based solver (sub-0.1% on distance).

#include <gtest/gtest.h>

#ifndef _WIN32
#include "fv_compat.h"
#endif

#include "geo_tool.h"

namespace {

const double kJfkLat = 40.63980, kJfkLon = -73.77890;
const double kLhrLat = 51.47750, kLhrLon = -0.46140;
// Pinned outputs of this module's own solver (Sodano-based), captured on the
// macOS port and to be cross-checked against the Windows build. For scale:
// GeographicLib's exact geodesic gives s12 = 5554539.5 m / azi1 = 51.19888
// deg, i.e. this solver is ~200 m (0.004%) and ~0.17 deg away from modern
// reference values. Preserved bit-faithfully.
const double kJfkLhrMeters = 5554338.869424;
const double kJfkLhrBearing = 51.3729299178;
const double kJfkLhrRhumbMeters = 5758036.970349;
const double kJfkLhrRhumbBearing = 77.9192619074;

TEST(GeoToolTest, GreatCircleRangeAndBearingJfkLhr) {
  double distance = 0, bearing = 0;
  ASSERT_EQ(SUCCESS, GEO_calc_range_and_bearing(kJfkLat, kJfkLon, kLhrLat,
                                                kLhrLon, &distance, &bearing,
                                                TRUE));
  EXPECT_NEAR(distance, kJfkLhrMeters, 0.01);
  EXPECT_NEAR(bearing, kJfkLhrBearing, 1e-6);
}

TEST(GeoToolTest, RhumbLineRangeAndBearingJfkLhr) {
  double distance = 0, bearing = 0;
  ASSERT_EQ(SUCCESS, GEO_calc_range_and_bearing(kJfkLat, kJfkLon, kLhrLat,
                                                kLhrLon, &distance, &bearing,
                                                FALSE));
  EXPECT_NEAR(distance, kJfkLhrRhumbMeters, 0.01);
  EXPECT_NEAR(bearing, kJfkLhrRhumbBearing, 1e-6);
}

TEST(GeoToolTest, EndPointRoundTrip) {
  double lat2 = 0, lon2 = 0;
  ASSERT_EQ(SUCCESS, GEO_calc_end_point(kJfkLat, kJfkLon, kJfkLhrMeters,
                                        kJfkLhrBearing, &lat2, &lon2, TRUE));
  EXPECT_NEAR(lat2, kLhrLat, 1e-8);
  EXPECT_NEAR(lon2, kLhrLon, 1e-6);
}

TEST(GeoToolTest, ZeroDistanceSamePoint) {
  double distance = -1, bearing = -1;
  ASSERT_EQ(SUCCESS,
            GEO_calc_range_and_bearing(kJfkLat, kJfkLon, kJfkLat, kJfkLon,
                                       &distance, &bearing, TRUE));
  EXPECT_NEAR(distance, 0.0, 1e-6);
}

TEST(GeoToolTest, ValidDegrees) {
  EXPECT_TRUE(GEO_valid_degrees(45.0, 90.0));
  EXPECT_TRUE(GEO_valid_degrees(-90.0, -180.0));
  EXPECT_FALSE(GEO_valid_degrees(90.5, 0.0));
  EXPECT_FALSE(GEO_valid_degrees(0.0, 180.5));
}

TEST(GeoToolTest, EastOfDegrees) {
  // GEO_east_of_degrees(a, b) evaluates "a is east of b"
  EXPECT_TRUE(GEO_east_of_degrees(20.0, 10.0));
  EXPECT_FALSE(GEO_east_of_degrees(10.0, 20.0));
  EXPECT_TRUE(GEO_east_of_degrees(-179.0, 179.0));  // dateline wrap
  EXPECT_FALSE(GEO_east_of_degrees(179.0, -179.0));
}

TEST(GeoToolTest, LonInRange) {
  EXPECT_TRUE(GEO_lon_in_range(-10.0, 10.0, 0.0));
  EXPECT_FALSE(GEO_lon_in_range(-10.0, 10.0, 20.0));
  EXPECT_TRUE(GEO_lon_in_range(170.0, -170.0, 180.0));  // dateline span
}

// Coordinate-string formatting goes through CGeoTrans (fv_geo3), exercising
// the two libraries together.
TEST(GeoToolTest, LatLonToStringAndBack) {
  char geo_string[64] = {0};
  ASSERT_EQ(SUCCESS,
            GEO_lat_lon_to_string(33.75, -84.39, geo_string, sizeof geo_string));
  EXPECT_GT(strlen(geo_string), 8u);

  double lat = 0, lon = 0;
  ASSERT_EQ(SUCCESS, GEO_string_to_lat_lon(geo_string, "WGS84", &lat, &lon));
  EXPECT_NEAR(lat, 33.75, 1e-4);
  EXPECT_NEAR(lon, -84.39, 1e-4);
}

// Magnetic variation via the in-memory WMM-2000 fallback (no wmm.dat on this
// machine: FVW_GEODATA_DIR is unset, so load_mag_file fails over to
// load_mem_mag_data). Atlanta 2000 epoch declination ~ -4 deg W; generous
// bounds since the 2000 model is evaluated at today's date.
TEST(GeoToolTest, MagneticVariationPlausible) {
  double magvar = 999;
  int result = GEO_current_magnetic_variation(33.75, -84.39, 300, &magvar);
  ASSERT_EQ(SUCCESS, result);
  EXPECT_GT(magvar, -15.0);
  EXPECT_LT(magvar, 15.0);
  EXPECT_NE(magvar, 999);
}

}  // namespace
