// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Tests for the portable geoid module (port/geoid/fv_geoid.{h,cpp}).
//
// No real EGM data is required: tests generate synthetic grid files whose
// values are a plane in grid coordinates, f(row, col) = 2*row + 0.5*col.
// Bilinear interpolation reproduces a plane exactly, so expected values are
// computed analytically from the grid geometry:
//   offset_x = wrapped_lon * cells_per_degree, offset_y = (90 - lat) * cells_per_degree
//   expected = (2*row_eval + 0.5*offset_x) * 3.28083989501   [meters -> feet]
// where row_eval models the original CGeoid in-cell latitude inversion that
// this port preserves (see the NOTE in fv_geoid.cpp): the cell is evaluated at
// post_y + (1 - frac(offset_y)) rather than post_y + frac(offset_y).

#include "fv_geoid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const double kMetersToFeet = 3.28083989501;

class GeoidTest : public ::testing::Test {
 protected:
  void SetUp() override {
    m_dir = std::filesystem::temp_directory_path() /
            ("fv_geoid_test_" + std::to_string(::getpid()));
    std::filesystem::create_directories(m_dir);
  }
  void TearDown() override { std::filesystem::remove_all(m_dir); }

  void WriteGrid(const char* name, float spacing, size_t rows, size_t cols,
                 size_t truncate_to_elevs = SIZE_MAX) {
    FILE* fp = std::fopen((m_dir / name).string().c_str(), "wb");
    ASSERT_NE(fp, nullptr);
    float header[6] = {-90.0f, 90.0f, 0.0f, 360.0f, spacing, spacing};
    ASSERT_EQ(std::fwrite(header, sizeof(float), 6, fp), 6u);
    size_t limit = rows * cols;
    if (truncate_to_elevs != SIZE_MAX && truncate_to_elevs < limit)
      limit = truncate_to_elevs;
    std::vector<float> row_buf(cols);
    size_t written = 0;
    for (size_t r = 0; r < rows && written < limit; ++r) {
      size_t n = 0;
      for (size_t c = 0; c < cols && written < limit; ++c, ++written)
        row_buf[n++] = 2.0f * r + 0.5f * c;
      ASSERT_EQ(std::fwrite(row_buf.data(), sizeof(float), n, fp), n);
    }
    std::fclose(fp);
  }

  void WriteEgm96() { WriteGrid("egm96.dat", 0.25f, 721, 1441); }
  void WriteEgm84() { WriteGrid("egm84.dat", 0.5f, 361, 721); }

  static double ExpectedFeet(double lat, double lon, double cells_per_deg,
                             double num_col, double num_row) {
    double offset_x = (lon < 0.0 ? lon + 360.0 : lon) * cells_per_deg;
    double offset_y = (90.0 - lat) * cells_per_deg;
    double post_x = std::floor(offset_x);
    if (post_x + 1 == num_col) post_x--;
    double post_y = std::floor(offset_y);
    if (post_y + 1 == num_row) post_y--;
    // Original blend: lower + delta_y*(upper - lower), i.e. the cell is
    // evaluated at row post_y + (1 - delta_y).
    double row_eval = post_y + 1.0 - (offset_y - post_y);
    return (2.0 * row_eval + 0.5 * offset_x) * kMetersToFeet;
  }

  std::string DataDir() { return m_dir.string(); }

  std::filesystem::path m_dir;
};

TEST_F(GeoidTest, Egm96InterpolationMatchesPlane) {
  WriteEgm96();
  fv::GeoidCalculator geoid(DataDir());  // EGM96 is the default datum

  struct {
    double lat, lon;
  } pts[] = {
      {0.0, 0.0},        // origin
      {33.75, -84.39},   // Atlanta; exercises negative-longitude wrap
      {45.125, 90.375},  // exactly on grid posts
      {12.34, 56.78},    // arbitrary interior point
      {89.9, 179.9},     // near north pole / dateline
      {-90.0, 0.0},      // south edge: exercises post_y clamp
      {50.0, 180.0},     // east edge of valid longitude
  };
  for (const auto& p : pts) {
    double delta = 0;
    int err_code = -1;
    std::string err_msg;
    EXPECT_EQ(geoid.get_geoid_delta(p.lat, p.lon, &delta, &err_code, &err_msg),
              0)
        << "lat=" << p.lat << " lon=" << p.lon << " msg=" << err_msg;
    EXPECT_EQ(err_code, 0);
    EXPECT_NEAR(delta, ExpectedFeet(p.lat, p.lon, 4.0, 1441, 721), 1e-6)
        << "lat=" << p.lat << " lon=" << p.lon;
  }
}

TEST_F(GeoidTest, Egm84UsesHalfDegreeGrid) {
  WriteEgm84();
  fv::GeoidCalculator geoid(DataDir());
  int err_code = -1;
  std::string err_msg;
  ASSERT_EQ(geoid.set_vertical_datum(1984, &err_code, &err_msg), 0);

  double delta = 0;
  ASSERT_EQ(geoid.get_geoid_delta(33.75, -84.39, &delta, &err_code, &err_msg),
            0)
      << err_msg;
  EXPECT_NEAR(delta, ExpectedFeet(33.75, -84.39, 2.0, 721, 361), 1e-6);
}

TEST_F(GeoidTest, MissingFileReportsOpenError) {
  fv::GeoidCalculator geoid(DataDir());  // dir exists but has no egm96.dat
  double delta = 0;
  int err_code = 0;
  std::string err_msg;
  EXPECT_NE(geoid.get_geoid_delta(0, 0, &delta, &err_code, &err_msg), 0);
  EXPECT_EQ(err_code, fv::GEOID_FILE_OPEN_ERROR);
  EXPECT_NE(err_msg.find("egm96.dat"), std::string::npos);
}

TEST_F(GeoidTest, BadHeaderReportsInitializeError) {
  WriteGrid("egm96.dat", 0.5f, 721, 1441);  // wrong spacing for EGM96
  fv::GeoidCalculator geoid(DataDir());
  double delta = 0;
  int err_code = 0;
  std::string err_msg;
  EXPECT_NE(geoid.get_geoid_delta(0, 0, &delta, &err_code, &err_msg), 0);
  EXPECT_EQ(err_code, fv::GEOID_INITIALIZE_ERROR);
}

TEST_F(GeoidTest, TruncatedFileReportsInitializeError) {
  WriteGrid("egm96.dat", 0.25f, 721, 1441, /*truncate_to_elevs=*/1000);
  fv::GeoidCalculator geoid(DataDir());
  double delta = 0;
  int err_code = 0;
  std::string err_msg;
  EXPECT_NE(geoid.get_geoid_delta(0, 0, &delta, &err_code, &err_msg), 0);
  EXPECT_EQ(err_code, fv::GEOID_INITIALIZE_ERROR);
}

TEST_F(GeoidTest, OutOfRangeCoordinatesReportErrors) {
  WriteEgm96();
  fv::GeoidCalculator geoid(DataDir());
  double delta = 0;
  int err_code = 0;
  std::string err_msg;

  EXPECT_NE(geoid.get_geoid_delta(90.5, 0, &delta, &err_code, &err_msg), 0);
  EXPECT_EQ(err_code, fv::GEOID_LAT_ERROR);

  EXPECT_NE(geoid.get_geoid_delta(0, -180.5, &delta, &err_code, &err_msg), 0);
  EXPECT_EQ(err_code, fv::GEOID_LON_ERROR);

  EXPECT_NE(geoid.get_geoid_delta(91, 181, &delta, &err_code, &err_msg), 0);
  EXPECT_EQ(err_code, fv::GEOID_LAT_ERROR | fv::GEOID_LON_ERROR);
}

TEST_F(GeoidTest, InvalidDatumYearRejected) {
  fv::GeoidCalculator geoid(DataDir());
  int err_code = 0;
  std::string err_msg;
  EXPECT_NE(geoid.set_vertical_datum(2020, &err_code, &err_msg), 0);
  EXPECT_EQ(err_code, fv::GEOID_DATUM_CODE_ERROR);
}

}  // namespace
