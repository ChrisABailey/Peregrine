// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// DTED FvKit adapter tests. Two layers, same pattern as dted_cell_test.cpp:
//  1. Synthetic root: a temp dted tree with one known-value cell — always
//     runs; verifies path->bounds mapping, meters/NaN conversion (D4), and
//     out-of-coverage behavior.
//  2. Real data via FVW_TESTDATA_DIR (skipped if absent): enumerates the
//     22-cell TestData tree (incl. the case-odd W084 dir and the prototype
//     .DT3 the reader rejects) and re-pins the n31.dt1 elevations through
//     the FvKit surface as floats.

#include "fvkit/formats/dted.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const unsigned kUhl = 80, kDsi = 648, kAcc = 2700, kDrd = 8, kTrlr = 4;

// Same synthetic cell as dted_cell_test.cpp: 21x21 posts, post (i, j) =
// 100*i + j, with a negative post at (3,4) and a void (-32767) at (5,5).
void WriteSyntheticCell(const fs::path& path, int posts = 21) {
  std::vector<unsigned char> f(kUhl + kDsi + kAcc, ' ');
  f[0] = 'U'; f[1] = 'H'; f[2] = 'L'; f[3] = '1';
  char n[5];
  snprintf(n, sizeof n, "%04d", posts);
  for (int k = 0; k < 4; ++k) {
    f[47 + k] = n[k];
    f[51 + k] = n[k];
  }
  for (int i = 0; i < posts; ++i) {
    std::vector<unsigned char> rec(kDrd, 0);
    for (int j = 0; j < posts; ++j) {
      int elev = 100 * i + j;
      if (i == 3 && j == 4) elev = -515;
      if (i == 5 && j == 5) elev = -32767;  // void
      unsigned short sm = elev < 0 ? (unsigned short)(0x8000 | (-elev))
                                   : (unsigned short)elev;
      rec.push_back((unsigned char)(sm >> 8));
      rec.push_back((unsigned char)(sm & 0xFF));
    }
    for (unsigned k = 0; k < kTrlr; ++k) rec.push_back(0);
    f.insert(f.end(), rec.begin(), rec.end());
  }
  FILE* fp = fopen(path.string().c_str(), "wb");
  ASSERT_NE(fp, nullptr);
  fwrite(f.data(), 1, f.size(), fp);
  fclose(fp);
}

class SyntheticDtedRoot : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = fs::temp_directory_path() /
            ("fvkit_dted_" + std::to_string(::getpid()));
    // SW corner 10N 20E, encoded in the PATH (that's what's under test)
    fs::create_directories(root_ / "e020");
    WriteSyntheticCell(root_ / "e020" / "n10.dt0");
  }
  void TearDown() override { fs::remove_all(root_); }

  // lat/lon of post (i, j): 21 posts spaced 1/20 degree from 10N 20E
  static double Lat(int j) { return 10.0 + j / 20.0; }
  static double Lon(int i) { return 20.0 + i / 20.0; }

  fs::path root_;
};

TEST_F(SyntheticDtedRoot, EnumeratorFindsCellWithPathBounds) {
  fv::DtedFrameEnumerator e;
  ASSERT_TRUE(e.Begin(root_.string()).ok());
  fv::FrameInfo info;
  ASSERT_TRUE(e.Next(&info));
  EXPECT_EQ(info.series_key, "DTED0");
  EXPECT_DOUBLE_EQ(info.bounds.ll.lat, 10.0);
  EXPECT_DOUBLE_EQ(info.bounds.ll.lon, 20.0);
  EXPECT_DOUBLE_EQ(info.bounds.ur.lat, 11.0);
  EXPECT_DOUBLE_EQ(info.bounds.ur.lon, 21.0);
  EXPECT_GT(info.size_bytes, 0);
  EXPECT_TRUE(info.edition.empty());
  EXPECT_FALSE(e.Next(&info));  // exactly one frame
}

TEST_F(SyntheticDtedRoot, ElevationMetersAndBounds) {
  fv::DtedElevationSource src(root_.string());
  fv::GeoRect b = src.Bounds();
  EXPECT_DOUBLE_EQ(b.ll.lat, 10.0);
  EXPECT_DOUBLE_EQ(b.ur.lon, 21.0);

  float e = 0;
  ASSERT_TRUE(src.GetElevation({Lat(7), Lon(12)}, &e).ok());
  EXPECT_FLOAT_EQ(e, 1207.0f);  // post (12, 7)
  ASSERT_TRUE(src.GetElevation({Lat(4), Lon(3)}, &e).ok());
  EXPECT_FLOAT_EQ(e, -515.0f);  // signed magnitude
}

TEST_F(SyntheticDtedRoot, VoidPostIsNaNWithOkStatus) {
  fv::DtedElevationSource src(root_.string());
  float e = 0;
  fv::Status s = src.GetElevation({Lat(5), Lon(5)}, &e);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_TRUE(std::isnan(e));  // -32767 -> NaN (contracts D4)
}

TEST_F(SyntheticDtedRoot, OutOfCoverageAndBadArgs) {
  fv::DtedElevationSource src(root_.string());
  float e = 0;
  EXPECT_EQ(src.GetElevation({45.0, 20.5}, &e).code, fv::kOutOfCoverage);
  EXPECT_EQ(src.GetElevation({10.5, 0.0}, &e).code, fv::kOutOfCoverage);
  EXPECT_EQ(src.GetElevation({10.5, 20.5}, nullptr).code, fv::kInvalidArg);
}

TEST(DtedAdapterErrors, MissingRootReported) {
  fv::DtedFrameEnumerator e;
  EXPECT_EQ(e.Begin("/nonexistent/dted/root").code, fv::kNotFound);
  fv::DtedElevationSource src("/nonexistent/dted/root");
  float elev = 0;
  EXPECT_EQ(src.GetElevation({10.5, 20.5}, &elev).code, fv::kNotFound);
}

// ---------------------------------------------------------------------------
// Real data
// ---------------------------------------------------------------------------

std::string TestDataDted() {
  const char* dir = getenv("FVW_TESTDATA_DIR");
  if (dir == nullptr) return {};
  std::string path = std::string(dir) + "/dted";
  if (!fs::exists(path)) return {};
  return path;
}

TEST(RealDtedEnumerate, TwentyTwoCells) {
  std::string root = TestDataDted();
  if (root.empty()) GTEST_SKIP();

  fv::DtedFrameEnumerator e;
  ASSERT_TRUE(e.Begin(root).ok());
  std::vector<fv::FrameInfo> frames;
  fv::FrameInfo info;
  while (e.Next(&info)) frames.push_back(info);

  // 21 .dt1 cells (w081-W084 x n30-n35) + the w106 prototype .DT3
  ASSERT_EQ(frames.size(), 22u);
  int dt1 = 0, dt3 = 0;
  for (const auto& f : frames) {
    if (f.series_key == "DTED1") ++dt1;
    if (f.series_key == "DTED3") ++dt3;
    EXPECT_GT(f.size_bytes, 0) << f.path;
  }
  EXPECT_EQ(dt1, 21);
  EXPECT_EQ(dt3, 1);

  // spot-check path->bounds on the pinned cell
  auto it = std::find_if(frames.begin(), frames.end(), [](const fv::FrameInfo& f) {
    return f.path.find("w082") != std::string::npos &&
           f.path.find("n31") != std::string::npos;
  });
  ASSERT_NE(it, frames.end());
  EXPECT_DOUBLE_EQ(it->bounds.ll.lat, 31.0);
  EXPECT_DOUBLE_EQ(it->bounds.ll.lon, -82.0);
  EXPECT_DOUBLE_EQ(it->bounds.ur.lat, 32.0);
  EXPECT_DOUBLE_EQ(it->bounds.ur.lon, -81.0);

  // deterministic order
  fv::DtedFrameEnumerator e2;
  ASSERT_TRUE(e2.Begin(root).ok());
  for (const auto& f : frames) {
    ASSERT_TRUE(e2.Next(&info));
    EXPECT_EQ(info.path, f.path);
  }
}

TEST(RealDtedSource, PinnedElevationsThroughFvKit) {
  std::string root = TestDataDted();
  if (root.empty()) GTEST_SKIP();

  fv::DtedElevationSource src(root);

  // Union box: lat [30, 41] x lon [-106, -80] (n35 in W084, n40 in w106)
  fv::GeoRect b = src.Bounds();
  EXPECT_DOUBLE_EQ(b.ll.lat, 30.0);
  EXPECT_DOUBLE_EQ(b.ll.lon, -106.0);
  EXPECT_DOUBLE_EQ(b.ur.lat, 41.0);
  EXPECT_DOUBLE_EQ(b.ur.lon, -80.0);

  // Same pins as dted_cell_test.cpp RealDted1Pins, through the adapter
  struct { double lat, lon; float meters; } pins[] = {
      {31.5, -81.5, 9.0f},
      {31.25, -81.75, 16.0f},
      {31.75, -81.25, 0.0f},
      {31.0, -82.0, 25.0f},
  };
  for (const auto& p : pins) {
    float e = 0;
    fv::Status s = src.GetElevation({p.lat, p.lon}, &e);
    ASSERT_TRUE(s.ok()) << p.lat << "," << p.lon << ": " << s.message;
    EXPECT_FLOAT_EQ(e, p.meters) << p.lat << "," << p.lon;
  }

  // uppercase W084 dir must still resolve (NTFS-style case-insensitivity)
  float e = 0;
  fv::Status s = src.GetElevation({35.5, -83.5}, &e);
  EXPECT_TRUE(s.ok()) << s.message;

  // outside all cells
  EXPECT_EQ(src.GetElevation({50.0, -82.0}, &e).code, fv::kOutOfCoverage);

  // the prototype .DT3 is indexed but unreadable in this snapshot:
  // kIoError (file exists), NOT kOutOfCoverage
  EXPECT_EQ(src.GetElevation({40.5, -105.5}, &e).code, fv::kIoError);
}

}  // namespace
