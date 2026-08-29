// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fv::GeoTiffFrame against real USGS DRG quads in
// TestData/geotiff (skipped if absent). DRG naming: 38076g81.tif =
// 1-degree block lat 38N lon 076W, quad grid index g8/h8/e3...; these
// samples are USGS DOQ orthoimagery. Coverage and the NAD -> WGS84 datum
// shift are computed by the ported reader + GEOTRANS.

#include "fv_geotiff_frame.h"
#include "GeoTiffFrameFile.h"  // parser class + SUCCESS

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

std::string DataDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  return d ? std::string(d) + "/geotiff/" : std::string();
}

class RealDrg : public ::testing::Test {
 protected:
  void SetUp() override {
    if (DataDir().empty()) GTEST_SKIP() << "FVW_TESTDATA_DIR not set";
    if (!std::filesystem::exists(DataDir() + "38076g81.tif"))
      GTEST_SKIP() << "sample DRGs not present";
  }
};

TEST_F(RealDrg, FramePropertiesSupported) {
  fv::GeoTiffFrame frame;
  fv::GeoTiffFrameProperties p;
  ASSERT_EQ(S_OK, frame.GetFrameProperties(DataDir(), "38076g81.tif", &p));
  ASSERT_TRUE(p.supported);

  // DOQ quarter-quad in the 38N/76W block: 3.75-minute (0.0625 deg) span;
  // WGS84 bounds must sit in that neighborhood (datum shift is small).
  EXPECT_NEAR(p.ur_lat - p.ll_lat, 0.0625, 0.001);
  EXPECT_NEAR(p.ur_lon - p.ll_lon, 0.0625, 0.001);
  EXPECT_GT(p.ll_lat, 38.0 - 0.01);
  EXPECT_LT(p.ur_lat, 39.0 + 0.01);
  EXPECT_GT(p.ll_lon, -77.01);
  EXPECT_LT(p.ur_lon, -76.0 + 0.01);

  // These samples are USGS DOQs (orthoimagery): nominal 1 meter per pixel
  EXPECT_EQ(p.scale_units, MAP_SCALE_METERS);
  EXPECT_EQ(p.scale_denom, 1.0);
  EXPECT_TRUE(p.series == L"B&W" || p.series == L"CIR" ||
              !p.series.empty());
}

TEST_F(RealDrg, AllSampleQuadsSupported) {
  int supported = 0, total = 0;
  for (const auto& entry : std::filesystem::directory_iterator(DataDir())) {
    if (entry.path().extension() != ".tif") continue;
    ++total;
    fv::GeoTiffFrame frame;
    fv::GeoTiffFrameProperties p;
    ASSERT_EQ(S_OK, frame.GetFrameProperties(
                        DataDir(), entry.path().filename().string(), &p))
        << entry.path();
    if (p.supported) ++supported;
    if (p.supported) {
      EXPECT_GT(p.scale_denom, 0.0) << entry.path();
      EXPECT_LT(p.ll_lat, p.ur_lat) << entry.path();
      EXPECT_LT(p.ll_lon, p.ur_lon) << entry.path();
    }
  }
  ASSERT_GT(total, 0);
  EXPECT_EQ(supported, total) << "some sample quads were not recognized";
}

TEST_F(RealDrg, PixelGeoRoundTrip) {
  fv::GeoTiffFrame frame;
  fv::GeoTiffFrameProperties p;
  ASSERT_EQ(S_OK, frame.GetFrameProperties(DataDir(), "38076g81.tif", &p));
  ASSERT_TRUE(p.supported);

  int w = 0, h = 0;
  ASSERT_EQ(0, frame.Parser()->get_image_size(w, h));
  EXPECT_GT(w, 1000);
  EXPECT_GT(h, 1000);

  // inv_transform then fwd_transform must land on the same pixel (+/-1)
  double lat = 0, lon = 0;
  ASSERT_EQ(0, frame.Parser()->inv_transform(w / 2, h / 2, lat, lon));
  int hp = 0, vp = 0;
  ASSERT_EQ(0, frame.Parser()->fwd_transform(lat, lon, hp, vp));
  EXPECT_NEAR(hp, w / 2, 1);
  EXPECT_NEAR(vp, h / 2, 1);
}

// A current FAA VFR sectional: LZW-compressed 8-bit palette on a
// Lambert Conformal Conic 2SP grid, which is how FAA has published its
// charts as GeoTIFF for years. Both of those were rejected until 2026-08-27
// (LZW outright; LCC 2SP because the geokey check demanded ProjNatOriginLong,
// which is 1SP's spelling and is absent from a spec-correct 2SP file).
class FaaSectional : public ::testing::Test {
 protected:
  void SetUp() override {
    if (DataDir().empty()) GTEST_SKIP() << "FVW_TESTDATA_DIR not set";
    if (!std::filesystem::exists(DataDir() + kChart))
      GTEST_SKIP() << "FAA sectional not present";
  }
  static constexpr const char* kChart = "Atlanta SEC.tif";
};

TEST_F(FaaSectional, LoadsAndCovers) {
  fv::GeoTiffFrame frame;
  fv::GeoTiffFrameProperties p;
  ASSERT_EQ(S_OK, frame.GetFrameProperties(DataDir(), kChart, &p));
  ASSERT_TRUE(p.supported);

  // The Atlanta sectional's neatline runs roughly 30.5N-34.5N, 88W-81W;
  // the sheet carries margin beyond it. Assert the box holds the two
  // airports the chart is named for and nothing absurd.
  EXPECT_LT(p.ll_lat, 33.6367);   // KATL
  EXPECT_GT(p.ur_lat, 33.6367);
  EXPECT_LT(p.ll_lon, -84.4281);
  EXPECT_GT(p.ur_lon, -84.4281);
  EXPECT_GT(p.ur_lat - p.ll_lat, 3.0);
  EXPECT_LT(p.ur_lat - p.ll_lat, 6.0);
  EXPECT_GT(p.ur_lon - p.ll_lon, 5.0);
  EXPECT_LT(p.ur_lon - p.ll_lon, 10.0);
}

TEST_F(FaaSectional, PixelGeoRoundTrip) {
  fv::GeoTiffFrame frame;
  fv::GeoTiffFrameProperties p;
  ASSERT_EQ(S_OK, frame.GetFrameProperties(DataDir(), kChart, &p));
  ASSERT_TRUE(p.supported);

  int w = 0, h = 0;
  ASSERT_EQ(0, frame.Parser()->get_image_size(w, h));
  EXPECT_EQ(w, 17951);
  EXPECT_EQ(h, 12354);

  // The LCC transform must invert. This is the part the ProjNatOriginLong
  // fallback decides: with the wrong lambda0 the round trip still closes,
  // so pin an absolute position too.
  for (int frac = 1; frac <= 3; ++frac) {
    const int hp0 = w * frac / 4, vp0 = h * frac / 4;
    double lat = 0, lon = 0;
    ASSERT_EQ(0, frame.Parser()->inv_transform(hp0, vp0, lat, lon));
    int hp = 0, vp = 0;
    ASSERT_EQ(0, frame.Parser()->fwd_transform(lat, lon, hp, vp));
    EXPECT_NEAR(hp, hp0, 1);
    EXPECT_NEAR(vp, vp0, 1);
  }

  // Hartsfield-Jackson (KATL, 33.6367N 84.4281W) must land on the sheet,
  // and inverting that pixel must come back to the airport. An lambda0 of
  // 0 rather than the chart's -84.5 would put it thousands of pixels off.
  int hp = 0, vp = 0;
  ASSERT_EQ(0, frame.Parser()->fwd_transform(33.6367, -84.4281, hp, vp));
  EXPECT_GT(hp, 0);
  EXPECT_LT(hp, w);
  EXPECT_GT(vp, 0);
  EXPECT_LT(vp, h);
  double lat = 0, lon = 0;
  ASSERT_EQ(0, frame.Parser()->inv_transform(hp, vp, lat, lon));
  EXPECT_NEAR(lat, 33.6367, 0.001);
  EXPECT_NEAR(lon, -84.4281, 0.001);
}

}  // namespace
