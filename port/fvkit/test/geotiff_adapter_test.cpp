// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// GeoTIFF FvKit adapter + format registry tests. All real-data (the USGS
// DOQ quarter-quads in TestData/geotiff, via FVW_TESTDATA_DIR — skipped if
// absent); needs MSPCCS_DATA for the GEOTRANS datum shift, as with
// fv_geotiff_frame_test. Registry tests are synthetic and always run.

#include "fvkit/formats/geotiff.h"
#include "fvkit/formats/registry.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string TestDataGeoTiff() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  std::string p = std::string(d) + "/geotiff";
  if (!fs::exists(p)) return {};
  return p;
}

const char* kPinnedQuad = "38076g81.tif";  // same frame the reader tests pin

// ---------------------------------------------------------------------------
// Registry (synthetic, always runs)
// ---------------------------------------------------------------------------

class RegistryTest : public ::testing::Test {
 protected:
  void SetUp() override { fv::ClearFormatRegistryForTest(); }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }
};

TEST_F(RegistryTest, BuiltinsRegisterIdempotently) {
  fv::RegisterBuiltinFormats();
  fv::RegisterBuiltinFormats();  // idempotent, no duplicate error
  auto keys = fv::RegisteredFormatKeys();
  ASSERT_EQ(keys.size(), 5u);
  EXPECT_EQ(keys[0], "cadrg");    // map order: sorted
  EXPECT_EQ(keys[1], "dted");
  EXPECT_EQ(keys[2], "geotiff");
  EXPECT_EQ(keys[3], "gpkg");
  EXPECT_EQ(keys[4], "tiros");

  const fv::FormatFactories* dted = fv::FindFormat("dted");
  ASSERT_NE(dted, nullptr);
  EXPECT_TRUE(dted->make_enumerator != nullptr);
  EXPECT_TRUE(dted->make_elevation_source != nullptr);
  EXPECT_TRUE(dted->make_raster_source == nullptr);  // no raster surface

  const fv::FormatFactories* gtif = fv::FindFormat("geotiff");
  ASSERT_NE(gtif, nullptr);
  EXPECT_TRUE(gtif->make_enumerator != nullptr);
  EXPECT_TRUE(gtif->make_raster_source != nullptr);
  EXPECT_TRUE(gtif->make_elevation_source == nullptr);

  EXPECT_EQ(fv::FindFormat("vpf"), nullptr);
}

TEST_F(RegistryTest, RejectsDuplicatesAndEmptyKey) {
  fv::FormatFactories f;
  f.format_key = "";
  EXPECT_EQ(fv::RegisterFormat(f).code, fv::kInvalidArg);
  f.format_key = "dted";
  EXPECT_TRUE(fv::RegisterFormat(f).ok());
  EXPECT_EQ(fv::RegisterFormat(f).code, fv::kInvalidArg);
}

TEST_F(RegistryTest, FactoriesConstructWorkingObjects) {
  fv::RegisterBuiltinFormats();
  auto e = fv::FindFormat("dted")->make_enumerator();
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->Begin("/nonexistent").code, fv::kNotFound);
  auto r = fv::FindFormat("geotiff")->make_raster_source();
  ASSERT_NE(r, nullptr);
  EXPECT_FALSE(r->Open("/nonexistent.tif").ok());
}

// ---------------------------------------------------------------------------
// Enumerator (real data)
// ---------------------------------------------------------------------------

TEST(GeoTiffEnumerate, AllQuadsRecognized) {
  std::string root = TestDataGeoTiff();
  if (root.empty()) GTEST_SKIP();

  fv::GeoTiffFrameEnumerator e;
  ASSERT_TRUE(e.Begin(root).ok());
  std::vector<fv::FrameInfo> frames;
  fv::FrameInfo info;
  while (e.Next(&info)) frames.push_back(info);

  // 15 samples, all recognized (mirrors geotiff_frame_test's supported ==
  // total): the 14 Chesapeake quads + chocta.tif (Choctawhatchee Bay, FL,
  // arrived with the full-tree copy 2026-07-16; generic GeoTIFF series)
  ASSERT_EQ(frames.size(), 15u);
  for (const auto& f : frames) {
    EXPECT_LT(f.bounds.ll.lat, f.bounds.ur.lat) << f.path;
    EXPECT_LT(f.bounds.ll.lon, f.bounds.ur.lon) << f.path;
    EXPECT_FALSE(f.series_key.empty()) << f.path;
    EXPECT_GT(f.size_bytes, 0) << f.path;
    std::string base = f.path.substr(f.path.find_last_of('/') + 1);
    if (base[0] == '3' || base[0] == 'f') {
      // 38N/76-77W block: 3.75' quarter-quads plus the wider f-prefixed
      // full quads (f38076a1 reaches -78.06)
      EXPECT_GT(f.bounds.ll.lat, 37.9) << f.path;
      EXPECT_LT(f.bounds.ur.lat, 39.1) << f.path;
      EXPECT_GT(f.bounds.ll.lon, -78.1) << f.path;
      EXPECT_LT(f.bounds.ur.lon, -75.8) << f.path;
    } else {
      // chocta.tif: Choctawhatchee Bay, ~30.4N -86.6W
      EXPECT_NEAR(f.bounds.ll.lat, 30.35, 0.05) << f.path;
      EXPECT_NEAR(f.bounds.ur.lon, -86.48, 0.05) << f.path;
    }
  }

  // deterministic, sorted by path
  for (size_t i = 1; i < frames.size(); ++i)
    EXPECT_LT(frames[i - 1].path, frames[i].path);
}

// ---------------------------------------------------------------------------
// Raster source (real data)
// ---------------------------------------------------------------------------

TEST(GeoTiffSource, OpenInfoBounds) {
  std::string root = TestDataGeoTiff();
  if (root.empty()) GTEST_SKIP();

  fv::GeoTiffRasterSource src;
  fv::Status s = src.Open(root + "/" + kPinnedQuad);
  ASSERT_TRUE(s.ok()) << s.message;

  fv::ImageInfo info;
  ASSERT_TRUE(src.Info(&info).ok());
  EXPECT_GT(info.size.width, 1000);
  EXPECT_GT(info.size.height, 1000);

  fv::GeoRect b = src.Bounds();
  EXPECT_GT(b.ll.lat, 37.9);
  EXPECT_LT(b.ur.lat, 39.1);
  EXPECT_TRUE(b.Contains({(b.ll.lat + b.ur.lat) / 2, (b.ll.lon + b.ur.lon) / 2}));

  // double-open rejected
  EXPECT_EQ(src.Open(root + "/" + kPinnedQuad).code, fv::kInvalidArg);
}

TEST(GeoTiffSource, ReadBlockRgba) {
  std::string root = TestDataGeoTiff();
  if (root.empty()) GTEST_SKIP();

  fv::GeoTiffRasterSource src;
  ASSERT_TRUE(src.Open(root + "/" + kPinnedQuad).ok());
  fv::ImageInfo info;
  ASSERT_TRUE(src.Info(&info).ok());

  const int TW = 128, TH = 128;
  fv::PixelBuffer buf;
  fv::PixelRect rect{info.size.width / 2, info.size.height / 2, TW, TH};
  fv::Status s = src.ReadBlock(rect, &buf);
  ASSERT_TRUE(s.ok()) << s.message;
  ASSERT_EQ(buf.Width(), TW);
  ASSERT_EQ(buf.Height(), TH);
  ASSERT_GE(buf.StrideBytes(), TW * 4);

  // orthophoto: plausible mean, many gray levels, opaque alpha, and the
  // grayscale DOQ replicates R=G=B (same content checks as the reader test)
  long long sum = 0;
  int distinct[256] = {0};
  bool alpha_ok = true, gray_ok = true;
  for (int y = 0; y < TH; ++y) {
    const unsigned char* row = buf.Row(y);
    for (int x = 0; x < TW; ++x) {
      const unsigned char* px = row + 4 * x;
      sum += px[0];
      distinct[px[0]] = 1;
      if (px[3] != 255) alpha_ok = false;
      if (px[0] != px[1] || px[1] != px[2]) gray_ok = false;
    }
  }
  double mean = (double)sum / (TW * TH);
  EXPECT_GT(mean, 2.0);
  EXPECT_LT(mean, 253.0);
  int nd = 0;
  for (int v : distinct) nd += v;
  EXPECT_GT(nd, 8);
  EXPECT_TRUE(alpha_ok);
  EXPECT_TRUE(gray_ok) << "B&W DOQ should decode with R==G==B";

  // out-of-image rects rejected, no partial fills
  EXPECT_EQ(src.ReadBlock({-1, 0, TW, TH}, &buf).code, fv::kInvalidArg);
  EXPECT_EQ(src.ReadBlock({info.size.width - 10, 0, TW, TH}, &buf).code,
            fv::kInvalidArg);
  EXPECT_EQ(src.ReadBlock({0, 0, 0, 1}, &buf).code, fv::kInvalidArg);
}

TEST(GeoTiffSource, TransformRoundTrip) {
  std::string root = TestDataGeoTiff();
  if (root.empty()) GTEST_SKIP();

  fv::GeoTiffRasterSource src;
  ASSERT_TRUE(src.Open(root + "/" + kPinnedQuad).ok());
  fv::ImageInfo info;
  ASSERT_TRUE(src.Info(&info).ok());

  // whole-pixel center on purpose: the legacy transforms are pixel-indexed
  double cx = (double)(info.size.width / 2), cy = (double)(info.size.height / 2);
  fv::GeoPoint p;
  ASSERT_TRUE(src.PixelToGeo(cx, cy, &p).ok());
  EXPECT_TRUE(src.Bounds().Contains(p));

  double px = 0, py = 0;
  ASSERT_TRUE(src.GeoToPixel(p, &px, &py).ok());
  EXPECT_NEAR(px, cx, 1.0);
  EXPECT_NEAR(py, cy, 1.0);
}

}  // namespace
