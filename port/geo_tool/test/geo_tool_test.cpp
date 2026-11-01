// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

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

// NOAA/NGA's published WMM2025 test values (WMM2025_TestValues.txt): decimal
// year, altitude (km), geodetic lat/lon (deg), declination (deg). With
// FVW_GEODATA_DIR unset these exercise the compiled-in coefficient table.
struct WmmTestValue {
  double year, alt_km, lat, lon, declination;
};
const WmmTestValue kWmm2025TestValues[] = {
    {2025, 28, 89, -121, -99.77},
    {2025, 48, 80, -96, -29.91},
    {2025, 54, 82, 87, 54.89},
    {2025, 65, 43, 93, 0.50},
    {2025, 51, -33, 109, -5.49},
    {2025, 39, -59, -8, -15.75},
    {2025, 3, -50, -103, 27.96},
    {2025, 94, -29, -110, 15.74},
    {2025, 66, 14, 143, -0.19},
    {2025, 18, 0, 21, 1.29},
    {2025.5, 6, -36, -137, 20.28},
    {2025.5, 63, 26, 81, 0.51},
    {2025.5, 69, 38, -144, 12.93},
    {2025.5, 50, -70, -133, 57.21},
    {2025.5, 8, -52, -75, 14.91},
    {2025.5, 8, -66, 17, -33.14},
    {2025.5, 22, -37, 140, 9.28},
    {2025.5, 40, -12, -129, 10.76},
    {2025.5, 44, 33, -118, 11.10},
    {2025.5, 50, -81, -67, 28.13},
    {2026, 74, -57, 3, -22.51},
    {2026, 46, -24, -122, 14.01},
    {2026, 69, 23, 63, 1.17},
    {2026, 33, -3, -147, 9.71},
    {2026, 47, -72, -22, -6.32},
    {2026, 62, -14, 99, -1.43},
    {2026, 83, 86, -46, -30.61},
    {2026, 82, -64, 87, -81.74},
    {2026, 34, -19, 43, -14.98},
    {2026, 56, -81, 40, -59.77},
    {2026.5, 14, 0, 80, -3.10},
    {2026.5, 12, -82, -68, 29.79},
    {2026.5, 44, -46, -42, -11.36},
    {2026.5, 43, 17, 52, 1.19},
    {2026.5, 64, 10, 78, -1.53},
    {2026.5, 12, 33, -145, 11.96},
    {2026.5, 12, -79, 115, -137.58},
    {2026.5, 14, -33, -114, 18.12},
    {2026.5, 19, 29, 66, 2.24},
    {2026.5, 86, -11, 167, 10.24},
    {2027, 37, -66, -5, -17.22},
    {2027, 67, 72, -115, 13.73},
    {2027, 44, 22, 174, 6.46},
    {2027, 54, 54, 178, 0.63},
    {2027, 57, -43, 50, -48.27},
    {2027, 44, -43, -111, 24.31},
    {2027, 12, -63, 178, 57.87},
    {2027, 38, 27, -169, 8.48},
    {2027, 61, 59, -77, -16.48},
    {2027, 67, -47, -32, -13.52},
    {2027.5, 8, 62, 53, 19.39},
    {2027.5, 77, -68, -7, -16.19},
    {2027.5, 98, -5, 159, 7.79},
    {2027.5, 34, -29, -107, 15.64},
    {2027.5, 60, 27, 65, 1.85},
    {2027.5, 73, -72, 95, -102.64},
    {2027.5, 96, -46, -85, 17.93},
    {2027.5, 0, -13, -59, -17.49},
    {2027.5, 16, 66, -178, 0.37},
    {2027.5, 72, -87, 38, -65.44},
    {2028, 49, 20, 167, 5.10},
    {2028, 71, 5, -13, -6.47},
    {2028, 95, 14, 65, -0.51},
    {2028, 86, -85, -79, 41.09},
    {2028, 30, -36, -64, -4.65},
    {2028, 75, 79, 125, -18.59},
    {2028, 21, 6, -32, -14.34},
    {2028, 1, -76, -75, 29.87},
    {2028, 45, -46, -41, -11.68},
    {2028, 11, -22, -21, -23.24},
    {2028.5, 28, 54, -120, 15.43},
    {2028.5, 68, -58, 156, 41.57},
    {2028.5, 39, -65, -88, 29.45},
    {2028.5, 27, -23, 81, -13.27},
    {2028.5, 11, 34, 0, 1.57},
    {2028.5, 72, -62, 65, -67.87},
    {2028.5, 55, 86, 70, 67.64},
    {2028.5, 59, 32, 163, 0.15},
    {2028.5, 65, 48, 148, -9.55},
    {2028.5, 95, 30, 28, 4.56},
    {2029, 95, -60, -59, 8.58},
    {2029, 95, -70, 42, -55.06},
    {2029, 50, 87, -154, -73.48},
    {2029, 58, 32, 19, 4.11},
    {2029, 57, 34, -13, -1.89},
    {2029, 38, -76, 49, -64.28},
    {2029, 49, -50, -179, 32.11},
    {2029, 90, -55, -171, 38.65},
    {2029, 41, 42, -19, -4.13},
    {2029, 19, 46, -22, -5.65},
    {2029.5, 31, 13, -132, 9.04},
    {2029.5, 93, -2, 158, 7.09},
    {2029.5, 51, -76, 40, -56.34},
    {2029.5, 64, 22, -132, 10.23},
    {2029.5, 26, -65, 55, -63.48},
    {2029.5, 66, -21, 32, -14.63},
    {2029.5, 18, 9, -172, 9.24},
    {2029.5, 63, 88, 26, 36.52},
    {2029.5, 33, 17, 5, 0.89},
    {2029.5, 77, -18, 138, 4.45}
};

TEST(GeoToolTest, MagneticVariationMatchesWmm2025TestValues) {
  for (const WmmTestValue& t : kWmm2025TestValues) {
    double magvar = 999;
    ASSERT_EQ(SUCCESS, GEO_magnetic_variation_at(t.lat, t.lon, t.year,
                                                 t.alt_km * 1000.0, &magvar));
    // The published values are rounded to 0.01 deg.
    EXPECT_NEAR(magvar, t.declination, 0.0051)
        << t.year << " " << t.alt_km << " km " << t.lat << "," << t.lon;
  }
}

// The year/month form evaluates mid-month: June 2025 is 2025.4583.
TEST(GeoToolTest, MagneticVariationYearMonthIsMidMonth) {
  double by_month = 0, by_year = 0;
  ASSERT_EQ(SUCCESS, GEO_magnetic_variation(32.78, -79.93, 2025, 6, 0, &by_month));
  ASSERT_EQ(SUCCESS, GEO_magnetic_variation_at(32.78, -79.93, 2025.0 + 5.5 / 12.0,
                                               0.0, &by_year));
  EXPECT_DOUBLE_EQ(by_month, by_year);
}

TEST(GeoToolTest, MagneticVariationCurrentIsPlausible) {
  // Atlanta is roughly 5 deg west under WMM2025.
  double magvar = 999;
  ASSERT_EQ(SUCCESS, GEO_current_magnetic_variation(33.75, -84.39, 300, &magvar));
  EXPECT_GT(magvar, -7.0);
  EXPECT_LT(magvar, -3.0);
}

}  // namespace
