// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// TIROS FvKit adapter tests: registry, enumerator, and raster source (real
// JPEG decode + equal-arc transforms) over TestData/tiros3 (skipped absent).

#include "fvkit/formats/registry.h"
#include "fvkit/formats/tiros.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string TirosDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (!d) return {};
  std::string p = std::string(d) + "/tiros3";
  return fs::exists(p) ? p : std::string();
}

const char* kTileRel = "/topobath/500m/TopoBath_500m_5530.wld";

TEST(TirosRegistry, Registered) {
  fv::ClearFormatRegistryForTest();
  fv::RegisterBuiltinFormats();
  const fv::FormatFactories* t = fv::FindFormat("tiros");
  ASSERT_NE(t, nullptr);
  EXPECT_TRUE(t->make_enumerator != nullptr);
  EXPECT_TRUE(t->make_raster_source != nullptr);
  auto keys = fv::RegisteredFormatKeys();
  EXPECT_EQ(keys.size(), 7u);  // cadrg, dted, dted-shaded, geotiff, gpkg, tiros, vpf
  fv::ClearFormatRegistryForTest();
}

TEST(TirosEnumerate, FindsTiles) {
  std::string root = TirosDir();
  if (root.empty()) GTEST_SKIP();
  fv::TirosFrameEnumerator e;
  ASSERT_TRUE(e.Begin(root).ok());
  std::vector<fv::FrameInfo> frames;
  fv::FrameInfo info;
  while (e.Next(&info)) frames.push_back(info);

  EXPECT_GT(frames.size(), 100u);
  for (const auto& f : frames) {
    EXPECT_LT(f.bounds.ll.lat, f.bounds.ur.lat) << f.path;
    EXPECT_FALSE(f.series_key.empty());
  }
  for (size_t i = 1; i < frames.size(); ++i)
    EXPECT_LT(frames[i - 1].path, frames[i].path);  // sorted
}

TEST(TirosSource, DecodeAndTransform) {
  std::string root = TirosDir();
  if (root.empty() || !fs::exists(root + kTileRel)) GTEST_SKIP();
  fv::TirosRasterSource src;
  ASSERT_TRUE(src.Open(root + kTileRel).ok());

  fv::ImageInfo info;
  ASSERT_TRUE(src.Info(&info).ok());
  EXPECT_EQ(info.size.width, 1350);
  EXPECT_EQ(info.size.height, 1350);

  const int TW = 256, TH = 256;
  fv::PixelBuffer buf;
  ASSERT_TRUE(src.ReadBlock({500, 500, TW, TH}, &buf).ok());
  ASSERT_EQ(buf.Width(), TW);
  long long sum = 0;
  bool alpha_ok = true;
  int distinct[256] = {0};
  for (int y = 0; y < TH; ++y) {
    const unsigned char* row = buf.Row(y);
    for (int x = 0; x < TW; ++x) {
      const unsigned char* px = row + 4 * x;
      sum += px[0];
      distinct[px[0]] = 1;
      if (px[3] != 255) alpha_ok = false;
    }
  }
  EXPECT_TRUE(alpha_ok);
  double mean = (double)sum / (TW * TH);
  EXPECT_GT(mean, 2.0);
  EXPECT_LT(mean, 253.0);
  int nd = 0;
  for (int v : distinct) nd += v;
  EXPECT_GT(nd, 4);

  // out-of-tile rejected
  EXPECT_EQ(src.ReadBlock({1300, 0, 100, 10}, &buf).code, fv::kInvalidArg);

  // equal-arc corners + round-trip
  fv::GeoRect b = src.Bounds();
  fv::GeoPoint p;
  ASSERT_TRUE(src.PixelToGeo(0, 0, &p).ok());
  EXPECT_NEAR(p.lat, b.ur.lat, 1e-9);  // NW
  EXPECT_NEAR(p.lon, b.ll.lon, 1e-9);
  ASSERT_TRUE(src.PixelToGeo(600, 700, &p).ok());
  double px = 0, py = 0;
  ASSERT_TRUE(src.GeoToPixel(p, &px, &py).ok());
  EXPECT_NEAR(px, 600, 1e-6);
  EXPECT_NEAR(py, 700, 1e-6);
}

}  // namespace
