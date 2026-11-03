// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

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
  EXPECT_EQ(keys.size(), 9u);  // cadrg, dted, dted-shaded, geotiff, gpkg, lidar, lidar-shaded, tiros, vpf
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

  ASSERT_FALSE(frames.empty());
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
  ASSERT_TRUE(src.PixelToGeo(info.size.width, info.size.height, &p).ok());
  EXPECT_NEAR(p.lat, b.ll.lat, 1e-9);  // SE corner of the last pixel
  EXPECT_NEAR(p.lon, b.ur.lon, 1e-9);
}

/// Tiles of one series tile the world: neighbours share an edge, with no
/// one-pixel gap between them, and the 16 km pair spans -180..180 exactly.
TEST(TirosEnumerate, NeighboursAbut) {
  std::string root = TirosDir();
  if (root.empty() || !fs::exists(root + "/topobath/16km") ||
      !fs::exists(root + "/topobath/8km/TopoBath_008k_0101.wld"))
    GTEST_SKIP();
  fv::TirosFrameEnumerator e;
  ASSERT_TRUE(e.Begin(root + "/topobath/16km").ok());
  std::vector<fv::FrameInfo> f;
  fv::FrameInfo info;
  while (e.Next(&info)) f.push_back(info);
  ASSERT_EQ(f.size(), 2u);  // sorted: col 00, then col 01
  EXPECT_NEAR(f[0].bounds.ll.lon, -180.0, 1e-9);
  EXPECT_NEAR(f[0].bounds.ur.lon, f[1].bounds.ll.lon, 1e-9);
  EXPECT_NEAR(f[1].bounds.ur.lon, 180.0, 1e-9);
  EXPECT_NEAR(f[0].bounds.ur.lat, 90.0, 1e-9);
  EXPECT_NEAR(f[0].bounds.ll.lat, -90.0, 1e-9);

  ASSERT_TRUE(e.Begin(root + "/topobath/8km").ok());
  f.clear();
  while (e.Next(&info)) f.push_back(info);
  const fv::FrameInfo* r0 = nullptr;
  const fv::FrameInfo* r1 = nullptr;
  for (const auto& x : f) {
    if (x.path.find("_0100.") != std::string::npos) r0 = &x;  // col 01 row 00
    if (x.path.find("_0101.") != std::string::npos) r1 = &x;  // col 01 row 01
  }
  ASSERT_TRUE(r0 && r1);
  EXPECT_NEAR(r0->bounds.ll.lat, r1->bounds.ur.lat, 1e-9);
  EXPECT_NEAR(r0->bounds.ll.lat, 0.0, 1e-9);

  // The source agrees with the catalog row.
  fv::TirosRasterSource src;
  ASSERT_TRUE(src.Open(r0->path).ok());
  fv::GeoRect b = src.Bounds();
  EXPECT_NEAR(b.ll.lon, r0->bounds.ll.lon, 1e-12);
  EXPECT_NEAR(b.ur.lat, r0->bounds.ur.lat, 1e-12);
}

}  // namespace
