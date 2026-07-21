// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Tests for fv::DtedCell. Two layers:
//  1. Synthetic cell: a minimal but structurally valid DTED file with known
//     post values — always runs, verifies indexing, byte order,
//     signed-magnitude decode, units, and bounds handling.
//  2. Real data: TestData/dted/w106/n40.DT3 (via FVW_TESTDATA_DIR), skipped
//     if absent. Pins real elevations near 40N 106W (Colorado Rockies) as
//     plausibility + regression values.

#include "fv_dted_cell.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const unsigned kUhl = 80, kDsi = 648, kAcc = 2700, kDrd = 8, kTrlr = 4;

// Build a synthetic 21x21-post cell (minimum the parser accepts) whose post
// at (lon_index i, lat_index j) has elevation 100*i + j, plus a few special
// posts, written as big-endian signed magnitude.
std::string WriteSyntheticCell(int posts = 21) {
  auto path = std::filesystem::temp_directory_path() /
              ("fv_dted_" + std::to_string(::getpid()) + ".dt0");
  std::vector<unsigned char> f(kUhl + kDsi + kAcc, ' ');
  // UHL: only the fields the reader parses need to be real
  f[0] = 'U'; f[1] = 'H'; f[2] = 'L'; f[3] = '1';
  char n[5];
  snprintf(n, sizeof n, "%04d", posts);
  for (int k = 0; k < 4; ++k) {
    f[47 + k] = n[k];  // num lon lines
    f[51 + k] = n[k];  // num lat points
  }
  // data records: one column per lon index
  for (int i = 0; i < posts; ++i) {
    std::vector<unsigned char> rec(kDrd, 0);
    rec[0] = 0xAA;  // sentinel (not checked by reader)
    for (int j = 0; j < posts; ++j) {
      int elev = 100 * i + j;
      if (i == 3 && j == 4) elev = -515;      // negative: signed magnitude
      if (i == 5 && j == 5) elev = -32767;    // PARTIAL_DTED_ELEVATION (void)
      unsigned short sm;
      if (elev < 0)
        sm = (unsigned short)(0x8000 | (-elev));
      else
        sm = (unsigned short)elev;
      rec.push_back((unsigned char)(sm >> 8));    // big-endian
      rec.push_back((unsigned char)(sm & 0xFF));
    }
    for (unsigned k = 0; k < kTrlr; ++k) rec.push_back(0);
    f.insert(f.end(), rec.begin(), rec.end());
  }
  FILE* fp = fopen(path.string().c_str(), "wb");
  fwrite(f.data(), 1, f.size(), fp);
  fclose(fp);
  return path.string();
}

class SyntheticDted : public ::testing::Test {
 protected:
  void SetUp() override {
    m_path = WriteSyntheticCell();
    // synthetic cell claims SW corner 10N 20E
    ASSERT_TRUE(m_cell.Open(m_path, 10.0, 20.0));
  }
  void TearDown() override {
    m_cell.Close();
    remove(m_path.c_str());
  }
  // lat/lon of post (i, j) in a 21-post cell: spacing 1/20 degree
  static double Lat(int j) { return 10.0 + j / 20.0; }
  static double Lon(int i) { return 20.0 + i / 20.0; }

  std::string m_path;
  fv::DtedCell m_cell;
};

TEST_F(SyntheticDted, HeaderParsed) {
  EXPECT_EQ(m_cell.EwPosts(), 21);
  EXPECT_EQ(m_cell.NsPosts(), 21);
  EXPECT_DOUBLE_EQ(m_cell.SwLat(), 10.0);
  EXPECT_DOUBLE_EQ(m_cell.SwLon(), 20.0);
}

TEST_F(SyntheticDted, ExactPostLookup) {
  long e = 0;
  // corners and interior
  ASSERT_TRUE(m_cell.GetElevation(Lat(0), Lon(0), fv::DTED_ELEVATION_METERS, e));
  EXPECT_EQ(e, 0);
  ASSERT_TRUE(m_cell.GetElevation(Lat(20), Lon(20), fv::DTED_ELEVATION_METERS, e));
  EXPECT_EQ(e, 100 * 20 + 20);
  ASSERT_TRUE(m_cell.GetElevation(Lat(7), Lon(12), fv::DTED_ELEVATION_METERS, e));
  EXPECT_EQ(e, 100 * 12 + 7);
}

TEST_F(SyntheticDted, NearestNeighborRounding) {
  long e = 0;
  // 40% of the way from post 7 to 8 -> still post 7; 60% -> post 8
  ASSERT_TRUE(m_cell.GetElevation(Lat(7) + 0.4 / 20.0, Lon(12),
                                  fv::DTED_ELEVATION_METERS, e));
  EXPECT_EQ(e, 100 * 12 + 7);
  ASSERT_TRUE(m_cell.GetElevation(Lat(7) + 0.6 / 20.0, Lon(12),
                                  fv::DTED_ELEVATION_METERS, e));
  EXPECT_EQ(e, 100 * 12 + 8);
}

TEST_F(SyntheticDted, SignedMagnitudeDecode) {
  long e = 0;
  ASSERT_TRUE(m_cell.GetElevation(Lat(4), Lon(3), fv::DTED_ELEVATION_METERS, e));
  EXPECT_EQ(e, -515);
}

TEST_F(SyntheticDted, PartialVoidPassesThroughInFeet) {
  long e = 0;
  ASSERT_TRUE(m_cell.GetElevation(Lat(5), Lon(5), fv::DTED_ELEVATION_FEET, e));
  EXPECT_EQ(e, fv::PARTIAL_DTED_ELEVATION);  // NOT converted to feet
}

TEST_F(SyntheticDted, FeetConversion) {
  long e = 0;
  ASSERT_TRUE(m_cell.GetElevation(Lat(0), Lon(10), fv::DTED_ELEVATION_FEET, e));
  EXPECT_EQ(e, (long)(1000 * 3.28083989501));  // post = 100*10+0 = 1000 m
}

TEST_F(SyntheticDted, OutOfBoundsRejected) {
  long e = 0;
  EXPECT_FALSE(m_cell.GetElevation(11.5, 20.5, fv::DTED_ELEVATION_METERS, e));
  EXPECT_FALSE(m_cell.GetElevation(10.5, 21.5, fv::DTED_ELEVATION_METERS, e));
  // small negative offsets clamp to SW corner (round-off tolerance)
  ASSERT_TRUE(m_cell.GetElevation(10.0 - 1e-9, 20.0 - 1e-9,
                                  fv::DTED_ELEVATION_METERS, e));
  EXPECT_EQ(e, 0);
}

TEST(DtedCellErrors, MissingAndBogusFiles) {
  fv::DtedCell cell;
  EXPECT_FALSE(cell.Open("/nonexistent/n00.dt0", 0, 0));
  // too-short file
  auto p = std::filesystem::temp_directory_path() / "fv_dted_short.dt0";
  FILE* fp = fopen(p.string().c_str(), "wb");
  fputs("UHL1 not a real header", fp);
  fclose(fp);
  EXPECT_FALSE(cell.Open(p.string(), 0, 0));
  remove(p.string().c_str());
}

// ---- Real data (skipped when TestData is absent) ----
// DTED level 3 is out of scope (prototype format, see fv_dted_cell.cpp);
// these tests look for standard .dt1/.dt2 cells under TestData/dted and
// skip until one is provided.

class RealDtedCell : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* dir = getenv("FVW_TESTDATA_DIR");
    if (dir == nullptr) GTEST_SKIP() << "FVW_TESTDATA_DIR not set";
    namespace fs = std::filesystem;
    std::string root = std::string(dir) + "/dted";
    if (!fs::exists(root)) GTEST_SKIP() << "no dted directory";
    for (const auto& e : fs::recursive_directory_iterator(root)) {
      if (!e.is_regular_file()) continue;
      std::string ext = e.path().extension().string();
      for (auto& c : ext) c = (char)tolower((unsigned char)c);
      if (ext == ".dt1" || ext == ".dt2") {
        m_path = e.path().string();
        // SW corner from the .../wDDD/nNN.dtX path convention
        std::string stem = e.path().stem().string();
        std::string lonpart = e.path().parent_path().filename().string();
        double lat = atof(stem.c_str() + 1);
        double lon = atof(lonpart.c_str() + 1);
        if (stem[0] == 's' || stem[0] == 'S') lat = -lat;
        if (lonpart[0] == 'w' || lonpart[0] == 'W') lon = -lon;
        m_sw_lat = lat;
        m_sw_lon = lon;
        return;
      }
    }
    GTEST_SKIP() << "no .dt1/.dt2 sample cells present (DT3 is out of scope)";
  }
  std::string m_path;
  double m_sw_lat = 0, m_sw_lon = 0;
};

TEST_F(RealDtedCell, OpensAndAnswersPlausibly) {
  fv::DtedCell cell;
  ASSERT_TRUE(cell.Open(m_path, m_sw_lat, m_sw_lon)) << m_path;
  EXPECT_GE(cell.EwPosts(), 21);
  EXPECT_GE(cell.NsPosts(), 21);

  long e = 0;
  int answered = 0;
  for (double dlat = 0.05; dlat < 1.0; dlat += 0.2)
    for (double dlon = 0.05; dlon < 1.0; dlon += 0.2)
      if (cell.GetElevation(m_sw_lat + dlat, m_sw_lon + dlon,
                            fv::DTED_ELEVATION_METERS, e)) {
        if (e != fv::PARTIAL_DTED_ELEVATION) {
          EXPECT_GT(e, -500);
          EXPECT_LT(e, 9000);
        }
        ++answered;
      }
  EXPECT_GT(answered, 10);
}

// Pinned values from TestData/dted/w082/n31.dt1 (standard DTED level 1,
// 1201x1201 posts; Georgia coastal plain — low elevations). Captured on
// this port 2026-07-11; cross-check against the Windows build eventually.
TEST(RealDted1Pins, KnownElevations) {
  const char* dir = getenv("FVW_TESTDATA_DIR");
  if (dir == nullptr) GTEST_SKIP();
  std::string path = std::string(dir) + "/dted/w082/n31.dt1";
  if (!std::filesystem::exists(path)) GTEST_SKIP() << path;

  fv::DtedCell cell;
  ASSERT_TRUE(cell.Open(path, 31.0, -82.0));
  EXPECT_EQ(cell.EwPosts(), 1201);
  EXPECT_EQ(cell.NsPosts(), 1201);

  struct { double lat, lon; long meters; } pins[] = {
      {31.5, -81.5, 9},
      {31.25, -81.75, 16},
      {31.75, -81.25, 0},
      {31.0, -82.0, 25},
  };
  for (const auto& p : pins) {
    long e = 0;
    ASSERT_TRUE(cell.GetElevation(p.lat, p.lon, fv::DTED_ELEVATION_METERS, e));
    EXPECT_EQ(e, p.meters) << p.lat << "," << p.lon;
  }
}

}  // namespace
