// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// FvKit L2 catalog tests. Synthetic tests (always run) use a test-registered
// format with a stub enumerator — including the antimeridian split/de-dupe
// the contracts (D2) require. Real-data tests scan TestData via the builtin
// formats and pin known counts/selections (skip when TestData is absent).

#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string TestDataDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  return d ? d : "";
}

// Frames a registered format's own enumerator yields over `dir` — the count
// Catalog::Scan is expected to reproduce as rows. Requires the builtin
// formats to be registered.
int CountFrames(const std::string& format_key, const std::string& dir) {
  const fv::FormatFactories* f = fv::FindFormat(format_key);
  if (f == nullptr || f->make_enumerator == nullptr) return -1;
  auto e = f->make_enumerator();
  if (!e->Begin(dir).ok()) return -1;
  int n = 0;
  fv::FrameInfo info;
  while (e->Next(&info)) ++n;
  return n;
}

// ---------------------------------------------------------------------------
// Synthetic format: three frames, one crossing the antimeridian
// ---------------------------------------------------------------------------

class StubEnumerator : public fv::IFrameEnumerator {
 public:
  fv::Status Begin(const std::string& dir) override {
    next_ = 0;
    frames_.clear();
    fv::FrameInfo a;  // normal frame near Hawaii
    a.path = dir + "/a.stub";
    a.bounds = fv::GeoRect{{20.0, -160.0}, {25.0, -155.0}};
    a.series_key = "STUB1M";
    a.scale = 1000000.0;
    a.scale_units = 0;  // denominator
    a.size_bytes = 1;
    fv::FrameInfo b;  // crosses the antimeridian (Fiji-ish)
    b.path = dir + "/b.stub";
    b.bounds = fv::GeoRect{{-20.0, 175.0}, {-15.0, -178.0}};
    b.series_key = "STUB1M";
    b.scale = 1000000.0;
    b.scale_units = 0;
    b.size_bytes = 1;
    fv::FrameInfo c;  // far away (Norway), different series
    c.path = dir + "/c.stub";
    c.bounds = fv::GeoRect{{60.0, 5.0}, {65.0, 10.0}};
    c.series_key = "STUB5M";
    c.scale = 5000000.0;
    c.scale_units = 0;
    c.size_bytes = 1;
    frames_ = {a, b, c};
    return fv::Status::Ok();
  }
  bool Next(fv::FrameInfo* info) override {
    if (next_ >= frames_.size()) return false;
    *info = frames_[next_++];
    return true;
  }

 private:
  std::vector<fv::FrameInfo> frames_;
  size_t next_ = 0;
};

class CatalogSynthetic : public ::testing::Test {
 protected:
  void SetUp() override {
    fv::ClearFormatRegistryForTest();
    fv::FormatFactories f;
    f.format_key = "stub";
    f.make_enumerator = [] { return std::make_shared<StubEnumerator>(); };
    ASSERT_TRUE(fv::RegisterFormat(f).ok());
    ASSERT_TRUE(cat_.Open(":memory:").ok());
    ASSERT_TRUE(cat_.AddDataSource("/stub/root", "stub", 0, &src_).ok());
    int added = 0;
    ASSERT_TRUE(cat_.Scan(src_, &added).ok());
    ASSERT_EQ(added, 3);
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  fv::Catalog cat_;
  int64_t src_ = 0;
};

TEST_F(CatalogSynthetic, SeriesRegistered) {
  std::vector<fv::SeriesRow> series;
  ASSERT_TRUE(cat_.Series(&series).ok());
  ASSERT_EQ(series.size(), 2u);
  EXPECT_EQ(series[0].series_key, "STUB1M");
  EXPECT_DOUBLE_EQ(series[0].scale_denom, 1000000.0);
  EXPECT_EQ(series[1].series_key, "STUB5M");
}

TEST_F(CatalogSynthetic, SelectSimple) {
  std::vector<fv::CoverageRow> rows;
  // viewport over Hawaii frame only
  ASSERT_TRUE(cat_.SelectByGeoRect({{21.0, -159.0}, {22.0, -158.0}}, &rows).ok());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_TRUE(rows[0].path.find("a.stub") != std::string::npos);
  // empty region
  ASSERT_TRUE(cat_.SelectByGeoRect({{0.0, 0.0}, {1.0, 1.0}}, &rows).ok());
  EXPECT_TRUE(rows.empty());
}

TEST_F(CatalogSynthetic, AntimeridianCoverageFoundFromBothSides) {
  std::vector<fv::CoverageRow> rows;
  // east side of the dateline (west piece of frame b)
  ASSERT_TRUE(cat_.SelectByGeoRect({{-19.0, 176.0}, {-16.0, 177.0}}, &rows).ok());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_TRUE(rows[0].path.find("b.stub") != std::string::npos);
  EXPECT_TRUE(rows[0].bounds.CrossesAntimeridian());
  // west side of the dateline (east piece of frame b)
  ASSERT_TRUE(cat_.SelectByGeoRect({{-19.0, -179.5}, {-16.0, -178.5}}, &rows).ok());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_TRUE(rows[0].path.find("b.stub") != std::string::npos);
}

TEST_F(CatalogSynthetic, CrossingQueryDeduplicates) {
  std::vector<fv::CoverageRow> rows;
  // query itself crosses the dateline: hits BOTH r-tree pieces of frame b —
  // must come back exactly once (contracts D2 de-dupe)
  fv::GeoRect q{{-19.0, 179.0}, {-16.0, -179.0}};
  ASSERT_TRUE(q.CrossesAntimeridian());
  ASSERT_TRUE(cat_.SelectByGeoRect(q, &rows).ok());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_TRUE(rows[0].path.find("b.stub") != std::string::npos);
}

TEST_F(CatalogSynthetic, RescanReplacesAndRemoveClears) {
  int added = 0;
  ASSERT_TRUE(cat_.Scan(src_, &added).ok());  // rescan: replace, not append
  EXPECT_EQ(added, 3);
  std::vector<fv::CoverageRow> rows;
  ASSERT_TRUE(cat_.SelectByGeoRect(fv::GeoRect::World(), &rows).ok());
  EXPECT_EQ(rows.size(), 3u);

  ASSERT_TRUE(cat_.RemoveDataSource(src_).ok());
  ASSERT_TRUE(cat_.SelectByGeoRect(fv::GeoRect::World(), &rows).ok());
  EXPECT_TRUE(rows.empty());
}

TEST_F(CatalogSynthetic, BestSeriesForScale) {
  fv::SeriesRow best;
  ASSERT_TRUE(cat_.BestSeriesForScale(900000.0, &best).ok());
  EXPECT_EQ(best.series_key, "STUB1M");
  ASSERT_TRUE(cat_.BestSeriesForScale(4000000.0, &best).ok());
  EXPECT_EQ(best.series_key, "STUB5M");
  EXPECT_EQ(cat_.BestSeriesForScale(-1.0, &best).code, fv::kInvalidArg);
}

// ---------------------------------------------------------------------------
// Real data: scan TestData through the builtin formats
// ---------------------------------------------------------------------------

TEST(CatalogReal, ScanAndSelectAtlanta) {
  std::string td = TestDataDir();
  if (td.empty() || !fs::exists(td + "/rpf")) GTEST_SKIP();

  fv::ClearFormatRegistryForTest();
  fv::RegisterBuiltinFormats();
  fv::Catalog cat;
  ASSERT_TRUE(cat.Open(":memory:").ok());

  int64_t rpf = 0, gtif = 0, dted = 0;
  int n_rpf = 0, n_gtif = 0, n_dted = 0;
  ASSERT_TRUE(cat.AddDataSource(td + "/rpf", "cadrg", 0, &rpf).ok());
  ASSERT_TRUE(cat.Scan(rpf, &n_rpf).ok());
  EXPECT_GT(n_rpf, 500);  // ~523 RPF frames
  // Scan must insert exactly one row per frame the format's own enumerator
  // yields. Comparing against the enumerator instead of a literal keeps this
  // honest as TestData grows (geotiff 15 -> 29, dted 24 -> 26 on 2026-07-23).
  ASSERT_TRUE(cat.AddDataSource(td + "/geotiff", "geotiff", 0, &gtif).ok());
  ASSERT_TRUE(cat.Scan(gtif, &n_gtif).ok());
  EXPECT_EQ(n_gtif, CountFrames("geotiff", td + "/geotiff"));
  ASSERT_TRUE(cat.AddDataSource(td + "/dted", "dted", 0, &dted).ok());
  ASSERT_TRUE(cat.Scan(dted, &n_dted).ok());
  EXPECT_EQ(n_dted, CountFrames("dted", td + "/dted"));
  EXPECT_GT(n_gtif, 0);
  EXPECT_GT(n_dted, 0);

  // Atlanta viewport -> LFC coverage (the frame the pan demo uses)
  fv::GeoRect atlanta{{33.6, -84.5}, {33.9, -84.2}};
  std::vector<fv::CoverageRow> rows;
  ASSERT_TRUE(cat.SelectByGeoRect(atlanta, &rows).ok());
  ASSERT_FALSE(rows.empty());
  bool lfc = false;
  for (const auto& r : rows) {
    EXPECT_TRUE(r.bounds.Intersects(atlanta)) << r.path;
    if (r.series_key == "LFC") lfc = true;
  }
  EXPECT_TRUE(lfc) << "expected an LFC frame over Atlanta";

  // BestSeriesForScale across the whole scanned catalog
  fv::SeriesRow best;
  ASSERT_TRUE(cat.BestSeriesForScale(500000.0, &best).ok());
  EXPECT_EQ(best.series_key, "LFC");
  ASSERT_TRUE(cat.BestSeriesForScale(5000000.0, &best).ok());
  EXPECT_EQ(best.series_key, "GNC");

  // DTED (scale 0) must never win a scale query
  std::vector<fv::SeriesRow> series;
  ASSERT_TRUE(cat.Series(&series).ok());
  for (const auto& s : series)
    if (s.format == "dted") EXPECT_DOUBLE_EQ(s.scale_denom, 0.0);

  fv::ClearFormatRegistryForTest();
}

}  // namespace
