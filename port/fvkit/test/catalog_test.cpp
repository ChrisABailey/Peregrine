// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// FvKit L2 catalog tests. Synthetic tests (always run) use a test-registered
// format with a stub enumerator — including the antimeridian split/de-dupe
// the contracts (D2) require. Real-data tests scan TestData via the builtin
// formats and pin known counts/selections (skip when TestData is absent).

#include "fvkit/catalog/catalog.h"
#include "fvkit/detail/sqlite.h"
#include "fvkit/formats/registry.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "fv_map_enums.h"

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

// One series_key, two ground resolutions — the shape of a directory of
// generic GeoTIFFs, where "Color" says what the pixels are and the geodata
// says how big they are. FalconView keys tblMapSeries on
// (scale, scale_units, series_name), so this is TWO map series.
class TwoScaleStubEnumerator : public fv::IFrameEnumerator {
 public:
  fv::Status Begin(const std::string& dir) override {
    next_ = 0;
    frames_.clear();
    fv::FrameInfo fine;  // 1 metre orthoimagery
    fine.path = dir + "/doq.stub";
    fine.bounds = fv::GeoRect{{38.5, -77.4}, {38.6, -77.3}};
    fine.series_key = "Color";
    fine.scale = 1.0;
    fine.scale_units = MAP_SCALE_METERS;
    fine.size_bytes = 1;
    fv::FrameInfo coarse;  // a scanned sectional over the same ground
    coarse.path = dir + "/sectional.stub";
    coarse.bounds = fv::GeoRect{{36.0, -80.0}, {38.0, -78.0}};
    coarse.series_key = "Color";
    coarse.scale = 50.0;
    coarse.scale_units = MAP_SCALE_METERS;
    coarse.size_bytes = 1;
    fv::FrameInfo chart;  // a DRG, scale as a denominator
    chart.path = dir + "/drg.stub";
    chart.bounds = fv::GeoRect{{30.3, -86.7}, {30.5, -86.4}};
    chart.series_key = "Color";
    chart.scale = 30000.0;
    chart.scale_units = MAP_SCALE_DENOMINATOR;
    chart.size_bytes = 1;
    frames_ = {fine, coarse, chart};
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

TEST(CatalogSeriesIdentity, OneKeyAtThreeScalesIsThreeSeries) {
  fv::ClearFormatRegistryForTest();
  fv::FormatFactories f;
  f.format_key = "twoscale";
  f.make_enumerator = [] { return std::make_shared<TwoScaleStubEnumerator>(); };
  ASSERT_TRUE(fv::RegisterFormat(f).ok());

  fv::Catalog cat;
  ASSERT_TRUE(cat.Open(":memory:").ok());
  int64_t src = 0;
  int added = 0;
  ASSERT_TRUE(cat.AddDataSource("/twoscale/root", "twoscale", 0, &src).ok());
  ASSERT_TRUE(cat.Scan(src, &added).ok());
  ASSERT_EQ(added, 3);

  std::vector<fv::SeriesRow> series;
  ASSERT_TRUE(cat.Series(&series).ok());
  ASSERT_EQ(series.size(), 3u) << "the scale is part of the series identity";
  for (const auto& r : series) EXPECT_EQ(r.series_key, "Color");

  // Each series reports its OWN scale — the defect this test exists for was
  // all three frames landing in one row that reported whichever came first.
  std::map<std::string, fv::SeriesRow> by_name;
  for (const auto& r : series) by_name[r.display_name] = r;
  ASSERT_EQ(by_name.count("Color 1 meter"), 1u);
  ASSERT_EQ(by_name.count("Color 50 meter"), 1u);
  ASSERT_EQ(by_name.count("Color 1:30 K"), 1u);
  EXPECT_DOUBLE_EQ(by_name["Color 1 meter"].scale, 1.0);
  EXPECT_DOUBLE_EQ(by_name["Color 50 meter"].scale, 50.0);
  EXPECT_DOUBLE_EQ(by_name["Color 1:30 K"].scale_denom, 30000.0);
  // ...and the normalized denominators order the way the resolutions do.
  EXPECT_LT(by_name["Color 1 meter"].scale_denom,
            by_name["Color 50 meter"].scale_denom);

  // The frames went to the right series, one each.
  for (const auto& entry : by_name) {
    std::vector<fv::CoverageRow> rows;
    ASSERT_TRUE(cat.SelectByGeoRect({{-90.0, -180.0}, {90.0, 180.0}}, &rows,
                                    entry.second.id)
                    .ok());
    EXPECT_EQ(rows.size(), 1u) << entry.first;
  }
  fv::ClearFormatRegistryForTest();
}

// A schema-1 catalog on disk is REBUILT, not converted: its map_series rows
// merged frames of different scales and nothing in the file remembers which
// frame had which, so the only honest recovery is to rescan. The data sources
// survive that, which is what makes the rescan possible.
TEST(CatalogSeriesIdentity, OldSchemaIsRebuiltAndSaysSo) {
  fs::path db = fs::temp_directory_path() /
                "fvkit_catalog_schema1_test.sqlite";
  fs::remove(db);
  {
    fv::detail::SqliteDb raw;
    ASSERT_TRUE(raw.Open(db.string()).ok());
    ASSERT_TRUE(
        raw.Exec(
               "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);"
               "INSERT INTO meta VALUES('schema_version','1');"
               "CREATE TABLE data_sources("
               "  id INTEGER PRIMARY KEY, path TEXT NOT NULL,"
               "  format TEXT NOT NULL, priority INTEGER DEFAULT 0,"
               "  UNIQUE(path, format));"
               "INSERT INTO data_sources(path, format) "
               "  VALUES('/twoscale/root','twoscale');"
               "CREATE TABLE map_series("
               "  id INTEGER PRIMARY KEY, format TEXT NOT NULL,"
               "  series_key TEXT NOT NULL, scale REAL, scale_units INTEGER,"
               "  scale_denom REAL, UNIQUE(format, series_key));"
               "INSERT INTO map_series(format, series_key, scale, scale_units,"
               "  scale_denom) VALUES('twoscale','Color',1.0,4,6714.0);"
               "CREATE TABLE coverage("
               "  id INTEGER PRIMARY KEY, data_source_id INTEGER NOT NULL,"
               "  series_id INTEGER NOT NULL, path TEXT NOT NULL,"
               "  ll_lat REAL, ll_lon REAL, ur_lat REAL, ur_lon REAL,"
               "  size_bytes INTEGER);"
               "CREATE VIRTUAL TABLE coverage_rtree"
               "  USING rtree(id, min_lon, max_lon, min_lat, max_lat);")
            .ok());
  }

  fv::ClearFormatRegistryForTest();
  fv::FormatFactories f;
  f.format_key = "twoscale";
  f.make_enumerator = [] { return std::make_shared<TwoScaleStubEnumerator>(); };
  ASSERT_TRUE(fv::RegisterFormat(f).ok());

  fv::Catalog cat;
  ASSERT_TRUE(cat.Open(db.string()).ok());
  EXPECT_TRUE(cat.NeedsRescan());
  std::vector<fv::SeriesRow> series;
  ASSERT_TRUE(cat.Series(&series).ok());
  EXPECT_TRUE(series.empty()) << "the stale merged series must not survive";

  // The data source did survive, so the caller can put the coverage back.
  int added = 0;
  ASSERT_TRUE(cat.Scan(1, &added).ok());
  EXPECT_EQ(added, 3);
  ASSERT_TRUE(cat.Series(&series).ok());
  EXPECT_EQ(series.size(), 3u);

  // Re-opening the rebuilt file is a normal open.
  {
    fv::Catalog again;
    ASSERT_TRUE(again.Open(db.string()).ok());
    EXPECT_FALSE(again.NeedsRescan());
    ASSERT_TRUE(again.Series(&series).ok());
    EXPECT_EQ(series.size(), 3u);
  }
  fv::ClearFormatRegistryForTest();
  fs::remove(db);
}

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

// The GeoTIFF directory is the real case that made the scale part of the
// series identity: TestData holds "Color" sheets at 0.3, 0.6, 1, 10 and 50
// metres per pixel — the last of them Atlanta SEC.tif, a scanned 1:500 K
// sectional — and under schema 1 all of them collapsed into one series that
// reported 0.3 metre because the first file scanned was a DOQQ.
//
// The assertion is derived from the enumerator rather than from a literal
// inventory (TestData grows), and it is the invariant itself: every frame in
// a series reports that series' own scale.
TEST(CatalogReal, EveryFrameInASeriesSharesThatSeriesScale) {
  std::string td = TestDataDir();
  if (td.empty() || !fs::exists(td + "/geotiff")) GTEST_SKIP();

  fv::ClearFormatRegistryForTest();
  fv::RegisterBuiltinFormats();

  // what the format itself says about each file
  const fv::FormatFactories* fmt = fv::FindFormat("geotiff");
  ASSERT_NE(fmt, nullptr);
  auto e = fmt->make_enumerator();
  ASSERT_TRUE(e->Begin(td + "/geotiff").ok());
  std::map<std::string, fv::FrameInfo> by_path;
  std::map<std::string, std::set<double>> scales_per_key;
  fv::FrameInfo info;
  while (e->Next(&info)) {
    by_path[info.path] = info;
    scales_per_key[info.series_key].insert(info.scale);
  }
  ASSERT_FALSE(by_path.empty());

  // This data really does hold one key at several scales — if a TestData
  // refresh ever removes that, this test stops testing anything and should
  // say so rather than passing quietly.
  size_t multi = 0;
  for (const auto& kv : scales_per_key)
    if (kv.second.size() > 1) ++multi;
  EXPECT_GT(multi, 0u) << "no GeoTIFF series_key spans two scales in TestData";

  fv::Catalog cat;
  ASSERT_TRUE(cat.Open(":memory:").ok());
  int64_t src = 0;
  int added = 0;
  ASSERT_TRUE(cat.AddDataSource(td + "/geotiff", "geotiff", 0, &src).ok());
  ASSERT_TRUE(cat.Scan(src, &added).ok());
  EXPECT_EQ(added, (int)by_path.size());

  std::vector<fv::SeriesRow> series;
  ASSERT_TRUE(cat.Series(&series).ok());
  std::set<std::string> names;
  for (const auto& r : series) {
    EXPECT_TRUE(names.insert(r.display_name).second)
        << "display_name must identify a series: " << r.display_name;
    std::vector<fv::CoverageRow> rows;
    ASSERT_TRUE(
        cat.SelectByGeoRect({{-90.0, -180.0}, {90.0, 180.0}}, &rows, r.id).ok());
    EXPECT_FALSE(rows.empty()) << r.display_name;
    for (const auto& c : rows) {
      auto it = by_path.find(c.path);
      ASSERT_NE(it, by_path.end()) << c.path;
      EXPECT_DOUBLE_EQ(it->second.scale, r.scale)
          << c.path << " filed under " << r.display_name;
      EXPECT_EQ(it->second.scale_units, r.scale_units) << c.path;
    }
  }
  fv::ClearFormatRegistryForTest();
}

}  // namespace
