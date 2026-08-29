// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for ImageLib's CGeoTiff reader (geotiff.cpp) against the real USGS
// DOQ quarter-quads in TestData/geotiff. Complements the GeoTIFFMapServer
// frame reader (already ported): this is the class that actually decodes
// pixels for the ImageLib COM object.

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "stdafx.h"
#include "geotiff.h"

namespace {

std::string DoqPath() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (!d) return {};
  std::string p = std::string(d) + "/geotiff/38076g81.tif";
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) return {};
  fclose(f);
  return p;
}

// A current FAA VFR sectional: LZW-compressed 8-bit palette on Lambert
// Conformal Conic 2SP. Both were rejected until 2026-08-27 — LZW at the
// compression tag (a 1990s decision from the Unisys patent, never revisited),
// and LCC 2SP because the geokey check demanded ProjNatOriginLong, which is
// 1SP's spelling. Note that CGeoTiff::decompress_lzw existed before this and
// was a renamed copy of decompress_packbits: nothing ever reached it.
std::string SectionalPath() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (!d) return {};
  std::string p = std::string(d) + "/geotiff/Atlanta SEC.tif";
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) return {};
  fclose(f);
  return p;
}

TEST(CGeoTiffLzw, LoadsFaaSectional) {
  std::string path = SectionalPath();
  if (path.empty()) GTEST_SKIP() << "no FAA sectional";

  CGeoTiff gt;
  BOOL type_ok = FALSE, geo_present = FALSE, geo_ok = FALSE;
  int image_type = 0, w = 0, h = 0;
  CString type_desc, err;
  ASSERT_EQ(0, gt.load(path.c_str(), type_ok, image_type, type_desc, w, h,
                       geo_present, geo_ok, err))
      << (LPCSTR)err;
  EXPECT_TRUE(type_ok) << (LPCSTR)type_desc;
  EXPECT_TRUE(geo_present);
  EXPECT_TRUE(geo_ok);
  EXPECT_EQ(w, 17951);
  EXPECT_EQ(h, 12354);
}

TEST(CGeoTiffLzw, DecodesSectionalPixels) {
  std::string path = SectionalPath();
  if (path.empty()) GTEST_SKIP() << "no FAA sectional";

  CGeoTiff gt;
  BOOL type_ok, geo_present, geo_ok;
  int image_type, w, h;
  CString type_desc, err;
  ASSERT_EQ(0, gt.load(path.c_str(), type_ok, image_type, type_desc, w, h,
                       geo_present, geo_ok, err));

  // Chart ink over Hartsfield-Jackson: a wrong LZW decode gives either a
  // failure or one flat colour, and a wrong code width gives noise. Ask for
  // the same block twice from different strips and check both.
  const int TW = 256, TH = 256;
  for (int ypos : {h / 3, 2 * h / 3}) {
    std::vector<unsigned char> rgb((size_t)TW * TH * 3, 0);
    ASSERT_EQ(0, gt.get_rgb_subimage(w / 2, ypos, TW, TH, rgb.data(), nullptr))
        << "y=" << ypos;

    // A sectional is a palettised chart: many colours, none dominating the
    // way a failed decode's fill would.
    int seen[256] = {0};
    int most = 0;
    for (int i = 0; i < TW * TH * 3; i += 3) ++seen[rgb[i]];
    int distinct = 0;
    for (int v : seen) {
      if (v) ++distinct;
      if (v > most) most = v;
    }
    EXPECT_GT(distinct, 4) << "y=" << ypos << ": chart should be polychrome";
    EXPECT_LT(most, TW * TH * 99 / 100) << "y=" << ypos << ": one flat colour";
  }
}

// The chart's Lambert grid: KATL must land on the sheet and invert back.
TEST(CGeoTiffLzw, SectionalLambertTransform) {
  std::string path = SectionalPath();
  if (path.empty()) GTEST_SKIP() << "no FAA sectional";

  CGeoTiff gt;
  BOOL type_ok, geo_present, geo_ok;
  int image_type, w, h;
  CString type_desc, err;
  ASSERT_EQ(0, gt.load(path.c_str(), type_ok, image_type, type_desc, w, h,
                       geo_present, geo_ok, err));

  int hp = 0, vp = 0;
  ASSERT_EQ(0, gt.fwd_transform(33.6367, -84.4281, hp, vp));
  EXPECT_GT(hp, 0);
  EXPECT_LT(hp, w);
  EXPECT_GT(vp, 0);
  EXPECT_LT(vp, h);
  double lat = 0, lon = 0;
  ASSERT_EQ(0, gt.inv_transform(hp, vp, lat, lon));
  EXPECT_NEAR(lat, 33.6367, 0.001);
  EXPECT_NEAR(lon, -84.4281, 0.001);
}

TEST(CGeoTiffReader, LoadsRealDoq) {
  std::string path = DoqPath();
  if (path.empty()) GTEST_SKIP() << "no DOQ samples";

  CGeoTiff gt;
  BOOL type_ok = FALSE, geo_present = FALSE, geo_ok = FALSE;
  int image_type = 0, w = 0, h = 0;
  CString type_desc, err;
  ASSERT_EQ(0, gt.load(path.c_str(), type_ok, image_type, type_desc, w, h,
                       geo_present, geo_ok, err))
      << (LPCSTR)err;
  EXPECT_TRUE(type_ok);
  EXPECT_TRUE(geo_present);
  EXPECT_TRUE(geo_ok);
  EXPECT_GT(w, 1000);
  EXPECT_GT(h, 1000);
}

TEST(CGeoTiffReader, DecodesPixels) {
  std::string path = DoqPath();
  if (path.empty()) GTEST_SKIP() << "no DOQ samples";

  CGeoTiff gt;
  BOOL type_ok, geo_present, geo_ok;
  int image_type, w, h;
  CString type_desc, err;
  ASSERT_EQ(0, gt.load(path.c_str(), type_ok, image_type, type_desc, w, h,
                       geo_present, geo_ok, err));

  const int TW = 128, TH = 128;
  std::vector<unsigned char> rgb(TW * TH * 3, 0);
  ASSERT_EQ(0, gt.get_rgb_subimage(w / 2, h / 2, TW, TH, rgb.data(), nullptr));

  long long sum = 0;
  int distinct[256] = {0};
  for (int i = 0; i < TW * TH * 3; i += 3) {
    sum += rgb[i];
    distinct[rgb[i]] = 1;
  }
  double mean = (double)sum / (TW * TH);
  EXPECT_GT(mean, 2.0);
  EXPECT_LT(mean, 253.0);
  int nd = 0;
  for (int v : distinct) nd += v;
  EXPECT_GT(nd, 8) << "orthophoto tile should have many gray levels";
}

TEST(CGeoTiffReader, GeoTransformRoundTrip) {
  std::string path = DoqPath();
  if (path.empty()) GTEST_SKIP() << "no DOQ samples";

  CGeoTiff gt;
  BOOL a, b, c;
  int t, w, h;
  CString d, e;
  ASSERT_EQ(0, gt.load(path.c_str(), a, t, d, w, h, b, c, e));

  double lat = 0, lon = 0;
  ASSERT_EQ(0, gt.inv_transform(w / 2, h / 2, lat, lon));
  // 38N/76W block
  EXPECT_GT(lat, 37.9);
  EXPECT_LT(lat, 39.1);
  EXPECT_GT(lon, -77.1);
  EXPECT_LT(lon, -75.9);

  int x = 0, y = 0;
  ASSERT_EQ(0, gt.fwd_transform(lat, lon, x, y));
  EXPECT_NEAR(x, w / 2, 1);
  EXPECT_NEAR(y, h / 2, 1);
}

}  // namespace
