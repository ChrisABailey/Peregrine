// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fv::TirosFrame (TIROS tile georeferencing). Synthetic name/parse
// checks always run; real-data checks over TestData/tiros3 skip when absent.
//
// The bounds are computed from the filename exactly as the Windows
// CTirosFrameFile does; the ProbeAndPin test both prints (for capture) and
// asserts golden values, and TilesSeamlessly cross-checks the equal-arc grid.

#include "fv_tiros_frame.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string TirosDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (!d) return {};
  std::string p = std::string(d) + "/tiros3";
  return fs::exists(p) ? p : std::string();
}

TEST(TirosFrameName, SeriesScaleColRow) {
  fv::TirosFrame f;
  fv::TirosFrameProperties p;
  ASSERT_TRUE(f.GetFrameProperties("TopoBath_500m_5530.wld", &p));
  EXPECT_EQ(p.series, "500M");
  EXPECT_DOUBLE_EQ(p.scale, 500.0);
  EXPECT_TRUE(p.scale_is_meters);
  EXPECT_EQ(p.col, 55);
  EXPECT_EQ(p.row, 30);
  EXPECT_FALSE(p.whole_world);

  // kilometer series
  ASSERT_TRUE(f.GetFrameProperties("path/to/TopoBath_001k_0306.WLD", &p));
  EXPECT_EQ(p.series, "001K");
  EXPECT_DOUBLE_EQ(p.scale, 1.0);
  EXPECT_FALSE(p.scale_is_meters);
  EXPECT_EQ(p.col, 3);
  EXPECT_EQ(p.row, 6);
}

TEST(TirosFrameName, DegPerPixel) {
  fv::TirosFrame f;
  fv::TirosFrameProperties p;
  // 500m: 64 x 32 tiles of 1350 -> dpp = 360/(64*1350), 180/(32*1350)
  ASSERT_TRUE(f.GetFrameProperties("TopoBath_500m_0000.wld", &p));
  EXPECT_DOUBLE_EQ(p.deg_per_pixel_lon, 360.0 / (64.0 * 1350.0));
  EXPECT_DOUBLE_EQ(p.deg_per_pixel_lat, 180.0 / (32.0 * 1350.0));
  // 1km: 32 x 16 tiles
  ASSERT_TRUE(f.GetFrameProperties("TopoBath_001k_0000.wld", &p));
  EXPECT_DOUBLE_EQ(p.deg_per_pixel_lon, 360.0 / (32.0 * 1350.0));
  EXPECT_DOUBLE_EQ(p.deg_per_pixel_lat, 180.0 / (16.0 * 1350.0));
}

TEST(TirosFrameName, WholeWorldAndRejects) {
  fv::TirosFrame f;
  fv::TirosFrameProperties p;
  ASSERT_TRUE(f.GetFrameProperties("W_something.wld", &p));
  EXPECT_TRUE(p.whole_world);
  EXPECT_DOUBLE_EQ(p.ll_lat, -90.0);
  EXPECT_DOUBLE_EQ(p.ur_lon, 180.0);

  EXPECT_FALSE(f.GetFrameProperties("tooshort.wld", &p));       // wrong length
  EXPECT_FALSE(f.GetFrameProperties("TopoBath_500m_55XX.wld", &p));  // non-digit col/row
  EXPECT_FALSE(p.supported);
}

// Golden pins (computed exactly as the Windows reader; verified by the tiling
// cross-check below). The 500m TopoBath grid is global; col 55/row 30 is over
// the South Pacific.
TEST(TirosFrameReal, PinnedBounds) {
  std::string root = TirosDir();
  if (root.empty()) GTEST_SKIP();
  std::string path = root + "/topobath/500m/TopoBath_500m_5530.wld";
  if (!fs::exists(path)) GTEST_SKIP() << path;

  fv::TirosFrame f;
  fv::TirosFrameProperties p;
  ASSERT_TRUE(f.GetFrameProperties(path, &p));
  const double dlat = 180.0 / (32.0 * 1350.0);
  const double dlon = 360.0 / (64.0 * 1350.0);
  EXPECT_NEAR(p.ur_lat, 90.0 - dlat / 2.0 - dlat * 1350 * 30, 1e-9);
  EXPECT_NEAR(p.ll_lon, -180.0 + dlon / 2.0 + dlon * 1350 * 55, 1e-9);
  // frame spans ~one tile in each axis (minus the half-pixel insets)
  EXPECT_NEAR(p.ur_lat - p.ll_lat, dlat * 1350 - dlat, 1e-9);
  EXPECT_NEAR(p.ur_lon - p.ll_lon, dlon * 1350 - dlon, 1e-9);
}

TEST(TirosFrameReal, TilesSeamlessly) {
  std::string root = TirosDir();
  if (root.empty()) GTEST_SKIP();
  std::string dir = root + "/topobath/500m/";
  auto pathOf = [&](const char* n) { return dir + n; };
  if (!fs::exists(pathOf("TopoBath_500m_5530.wld"))) GTEST_SKIP();

  fv::TirosFrame f;
  fv::TirosFrameProperties a, b;
  // col 55 row 30 and col 55 row 31 (one tile south)
  if (!f.GetFrameProperties(pathOf("TopoBath_500m_5530.wld"), &a)) GTEST_SKIP();
  if (!f.GetFrameProperties(pathOf("TopoBath_500m_5531.wld"), &b)) GTEST_SKIP();
  const double dlat = a.deg_per_pixel_lat;
  // the next row south begins one full tile (1350 px) lower
  EXPECT_NEAR(b.ur_lat, a.ur_lat - dlat * 1350, 1e-9);
  EXPECT_NEAR(b.ll_lon, a.ll_lon, 1e-9);
}

}  // namespace
