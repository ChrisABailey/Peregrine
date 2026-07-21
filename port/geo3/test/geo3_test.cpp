// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Port-specific tests for geo3 on POSIX. The module's original pinned-value
// tests (fvw_core/geo3/geotrans_unittest.cpp) are compiled into this binary
// too and run via --gtest_also_run_disabled_tests with MSPCCS_DATA set.
//
// This file covers what the port added:
//  * the fv_compat sscanf_s implementation (hand-rolled; needs its own pins)
//  * CGeoTrans location-string parsing, which exercises sscanf_s, CharUpper
//    and the in-memory format-preference fallback end to end.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#ifndef _WIN32
#include "fv_compat.h"  // must precede geotrans.h (Win32 typedefs)
#endif

#include "geotrans.h"

#ifndef _WIN32

TEST(SscanfS, NumericPassthrough) {
  int a = 0, b = 0;
  double d = 0;
  EXPECT_EQ(sscanf_s("12 34 5.5", "%d %d %lf", &a, &b, &d), 3);
  EXPECT_EQ(a, 12);
  EXPECT_EQ(b, 34);
  EXPECT_DOUBLE_EQ(d, 5.5);
}

TEST(SscanfS, StringWithSizeTruncates) {
  char buf[4];
  EXPECT_EQ(sscanf_s("hello world", "%s", buf, 4u), 1);
  EXPECT_STREQ(buf, "hel");  // truncated to size-1, null-terminated
}

TEST(SscanfS, CharSetsFromGeo3) {
  // The exact pattern geo3.cpp uses to split "DD MM SS.S N, DDD MM SS.S W".
  char str_lat[32], n_s[5], comma[5], str_lon[32], e_w[5];
  int n = sscanf_s("33 45 12.5 N , 084 23 45.6 W",
                   "%[ 0123456789.\260\'\"]%[ NnSs]%[ ,]"
                   "%[ 0123456789.\260\'\"]%[ EWew]",
                   str_lat, 32u, n_s, 5u, comma, 5u, str_lon, 32u, e_w, 5u);
  EXPECT_EQ(n, 5);
  EXPECT_STREQ(str_lat, "33 45 12.5 ");
  EXPECT_STREQ(n_s, "N ");
  EXPECT_STREQ(comma, ", ");
  EXPECT_STREQ(str_lon, "084 23 45.6 ");
  EXPECT_STREQ(e_w, "W");
}

TEST(SscanfS, PartialMatchReturnsCount) {
  char s[8];
  int v = 0;
  EXPECT_EQ(sscanf_s("abc xyz", "%s %d", s, 8u, &v), 1);
  EXPECT_STREQ(s, "abc");
}

TEST(SscanfS, SuppressionNotCounted) {
  int v = 0;
  EXPECT_EQ(sscanf_s("skip 42", "%*s %d", &v), 1);
  EXPECT_EQ(v, 42);
}

TEST(SscanfS, CharConversionNoTerminator) {
  char c[2] = {'x', 'x'};
  EXPECT_EQ(sscanf_s("AB", "%c", c, 2u), 1);
  EXPECT_EQ(c[0], 'A');
  EXPECT_EQ(c[1], 'x');  // %c must not null-terminate
}

TEST(SscanfS, LiteralAndPercentMatching) {
  int a = 0, b = 0;
  EXPECT_EQ(sscanf_s("3:4", "%d:%d", &a, &b), 2);
  EXPECT_EQ(a, 3);
  EXPECT_EQ(b, 4);
  EXPECT_EQ(sscanf_s("50%", "%d%%", &a), 1);
  EXPECT_EQ(a, 50);
  EXPECT_EQ(sscanf_s("3;4", "%d:%d", &a, &b), 1);  // literal mismatch stops
}

TEST(SscanfS, EmptyInputReturnsEOF) {
  int v;
  EXPECT_EQ(sscanf_s("", "%d", &v), EOF);
}
#endif  // !_WIN32

namespace {

// End-to-end: location-string parsing through CGeoTrans (hits sscanf_s,
// CharUpper, format preferences and the GEOTRANS conversion service).
TEST(Geo3Port, LocationStringToGeo) {
  CGeoTrans geo_trans;
  degrees_t lat = 0, lon = 0;
  char new_location[96] = {0};

  ASSERT_EQ(SUCCESS,
            geo_trans.DLL_location_to_geo("33 45 12.5N 084 23 45.6W", "WGS84",
                                          lat, lon, new_location, TRUE));
  EXPECT_NEAR(lat, 33.0 + 45.0 / 60.0 + 12.5 / 3600.0, 1e-9);
  EXPECT_NEAR(lon, -(84.0 + 23.0 / 60.0 + 45.6 / 3600.0), 1e-9);
}

TEST(Geo3Port, MgrsStringToGeoRoundTrip) {
  CGeoTrans geo_trans;
  degrees_t lat = 0, lon = 0;
  char new_location[96] = {0};

  // MGRS for (40N, 89W) pinned by the original unit tests.
  ASSERT_EQ(SUCCESS,
            geo_trans.DLL_location_to_geo("16TCK2927429672", "WGS84", lat, lon,
                                          new_location, TRUE));
  EXPECT_NEAR(lat, 40.0, 1e-4);
  EXPECT_NEAR(lon, -89.0, 1e-4);
}

}  // namespace
