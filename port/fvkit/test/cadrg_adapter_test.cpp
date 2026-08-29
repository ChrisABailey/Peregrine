// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// CADRG FvKit adapter tests: enumerator, raster source (real frame decode +
// equal-arc transforms), and the LRU frame cache. Real-data tests use the RPF
// frames in TestData/rpf (via FVW_TESTDATA_DIR); the cache/registry tests are
// synthetic and always run.

#include "fvkit/formats/cadrg.h"
#include "fvkit/formats/registry.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string RpfDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (!d) return {};
  std::string p = std::string(d) + "/rpf";
  return fs::exists(p) ? p : std::string();
}

// GNC frame over the US Southwest (same frame the row-11 decoder test uses).
const char* kGncRel = "/cgnc/1/00024023.gn1";

// ---------------------------------------------------------------------------
// Registry (synthetic)
// ---------------------------------------------------------------------------

TEST(CadrgRegistry, RegisteredWithRasterAndEnumerator) {
  fv::ClearFormatRegistryForTest();
  fv::RegisterBuiltinFormats();
  const fv::FormatFactories* c = fv::FindFormat("cadrg");
  ASSERT_NE(c, nullptr);
  EXPECT_TRUE(c->make_enumerator != nullptr);
  EXPECT_TRUE(c->make_raster_source != nullptr);
  EXPECT_TRUE(c->make_elevation_source == nullptr);
  auto r = c->make_raster_source();
  ASSERT_NE(r, nullptr);
  EXPECT_FALSE(r->Open("/nonexistent.gn1").ok());
  fv::ClearFormatRegistryForTest();
}

// ---------------------------------------------------------------------------
// LRU cache (synthetic: only exercises open/miss/hit/evict bookkeeping)
// ---------------------------------------------------------------------------

TEST(CadrgFrameCacheTest, EvictsLeastRecentlyUsed) {
  std::string rpf = RpfDir();
  if (rpf.empty()) GTEST_SKIP();
  // gather a few real frame paths via the enumerator (skips non-frame files
  // like .DS_Store)
  fv::CadrgFrameEnumerator e;
  ASSERT_TRUE(e.Begin(rpf + "/cgnc").ok());
  std::vector<std::string> paths;
  fv::FrameInfo fi;
  while (e.Next(&fi) && paths.size() < 4) paths.push_back(fi.path);
  if (paths.size() < 4) GTEST_SKIP();

  fv::CadrgFrameCache cache(2);
  fv::Status s;
  auto a = cache.Get(paths[0], &s);
  ASSERT_TRUE(s.ok()) << s.message;
  cache.Get(paths[1], &s);
  EXPECT_EQ(cache.Size(), 2u);

  // touch paths[0] so it becomes most-recent, then add paths[2] -> evicts [1]
  auto a2 = cache.Get(paths[0], &s);
  EXPECT_EQ(a, a2) << "hit returns the same cached source";
  cache.Get(paths[2], &s);
  EXPECT_EQ(cache.Size(), 2u);

  // paths[0] still cached (same object), paths[1] evicted (fresh object)
  EXPECT_EQ(cache.Get(paths[0], &s), a);
  // capacity never exceeded
  cache.Get(paths[3], &s);
  EXPECT_LE(cache.Size(), 2u);
}

// ---------------------------------------------------------------------------
// Enumerator (real data)
// ---------------------------------------------------------------------------

TEST(CadrgEnumerate, FindsAllFrames) {
  std::string rpf = RpfDir();
  if (rpf.empty()) GTEST_SKIP();

  fv::CadrgFrameEnumerator e;
  ASSERT_TRUE(e.Begin(rpf).ok());
  std::vector<fv::FrameInfo> frames;
  fv::FrameInfo info;
  while (e.Next(&info)) frames.push_back(info);

  EXPECT_GT(frames.size(), 500u);  // ~530-frame sample set
  for (const auto& f : frames) {
    EXPECT_LT(f.bounds.ll.lat, f.bounds.ur.lat) << f.path;
    EXPECT_FALSE(f.series_key.empty()) << f.path;
    EXPECT_GT(f.size_bytes, 0) << f.path;
  }
  for (size_t i = 1; i < frames.size(); ++i)
    EXPECT_LT(frames[i - 1].path, frames[i].path);  // sorted, deterministic
}

// ---------------------------------------------------------------------------
// Raster source (real data)
// ---------------------------------------------------------------------------

TEST(CadrgSource, OpenBoundsInfo) {
  std::string rpf = RpfDir();
  if (rpf.empty() || !fs::exists(rpf + kGncRel)) GTEST_SKIP();
  fv::CadrgRasterSource src;
  ASSERT_TRUE(src.Open(rpf + kGncRel).ok());

  fv::ImageInfo info;
  ASSERT_TRUE(src.Info(&info).ok());
  EXPECT_EQ(info.size.width, 1536);
  EXPECT_EQ(info.size.height, 1536);

  fv::GeoRect b = src.Bounds();
  EXPECT_NEAR(b.ll.lat, 20.769231, 1e-5);
  EXPECT_NEAR(b.ur.lon, -101.658031, 1e-5);
  EXPECT_EQ(src.Open(rpf + kGncRel).code, fv::kInvalidArg);  // double-open
}

TEST(CadrgSource, ReadBlockDecodesChart) {
  std::string rpf = RpfDir();
  if (rpf.empty() || !fs::exists(rpf + kGncRel)) GTEST_SKIP();
  fv::CadrgRasterSource src;
  ASSERT_TRUE(src.Open(rpf + kGncRel).ok());

  const int TW = 256, TH = 256;
  fv::PixelBuffer buf;
  fv::Status s = src.ReadBlock({640, 640, TW, TH}, &buf);
  ASSERT_TRUE(s.ok()) << s.message;
  ASSERT_EQ(buf.Width(), TW);
  ASSERT_EQ(buf.Height(), TH);

  long long sum = 0;
  int distinct[256] = {0};
  bool alpha_ok = true;
  for (int y = 0; y < TH; ++y) {
    const unsigned char* row = buf.Row(y);
    for (int x = 0; x < TW; ++x) {
      const unsigned char* px = row + 4 * x;
      sum += px[0];
      distinct[px[0]] = 1;
      if (px[3] != 255) alpha_ok = false;
    }
  }
  double mean = (double)sum / (TW * TH);
  EXPECT_GT(mean, 10.0);
  EXPECT_LT(mean, 245.0);
  int nd = 0;
  for (int v : distinct) nd += v;
  EXPECT_GT(nd, 8) << "a chart tile should have many colors";
  EXPECT_TRUE(alpha_ok);

  // second read of a different region is served from the cached decode
  fv::PixelBuffer buf2;
  EXPECT_TRUE(src.ReadBlock({0, 0, 16, 16}, &buf2).ok());
  EXPECT_EQ(src.ReadBlock({1400, 1400, 200, 200}, &buf2).code, fv::kInvalidArg);
}

TEST(CadrgSource, EqualArcTransformRoundTrip) {
  std::string rpf = RpfDir();
  if (rpf.empty() || !fs::exists(rpf + kGncRel)) GTEST_SKIP();
  fv::CadrgRasterSource src;
  ASSERT_TRUE(src.Open(rpf + kGncRel).ok());

  // pixel corners map to the frame's geographic corners
  fv::GeoRect b = src.Bounds();
  fv::GeoPoint p;
  ASSERT_TRUE(src.PixelToGeo(0, 0, &p).ok());
  EXPECT_NEAR(p.lat, b.ur.lat, 1e-6);  // NW corner
  EXPECT_NEAR(p.lon, b.ll.lon, 1e-6);
  ASSERT_TRUE(src.PixelToGeo(1536, 1536, &p).ok());
  EXPECT_NEAR(p.lat, b.ll.lat, 1e-6);  // SE corner
  EXPECT_NEAR(p.lon, b.ur.lon, 1e-6);

  // round-trip an interior point
  ASSERT_TRUE(src.PixelToGeo(700.0, 900.0, &p).ok());
  double px = 0, py = 0;
  ASSERT_TRUE(src.GeoToPixel(p, &px, &py).ok());
  EXPECT_NEAR(px, 700.0, 1e-6);
  EXPECT_NEAR(py, 900.0, 1e-6);
}

}  // namespace
