// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

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
