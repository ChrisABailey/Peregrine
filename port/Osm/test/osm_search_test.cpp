// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// VectorMapOverlay over the REAL Kiawah pyramid (search-plan-COMPLETE.md, S2).
//
// port/fvkit/test/vector_map_overlay_test.cpp pins everything the wrapper
// decides above the source seam, against a fake that returns six features.
// What only a real pyramid can pin is the other half of S2's "done when":
//
//   * "Ruddy Turnstone" in a viewport-sized query finds the ROAD in the tiles
//     and the POINT in a .fvpoints overlay through ONE SearchSession, with the
//     caller knowing that one of them spells its label `name:latin` and the
//     other `name`;
//   * THE TILE BUDGET. A query over the whole island cannot be answered at
//     z14 within the source's 64-tile allowance, so the zoom steps coarser,
//     and the coarse answer walks a FIFTH of the features for three quarters
//     of the names. That is the plan's "the pyramid is a relevance function
//     for free", pinned as a measured property of the delivered cut. What
//     this cut is too small to show is the other half of that story — at 70
//     z14 tiles for the whole island, even a cul-de-sac survives into z13, so
//     the test says only what it can see.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fv_osm_name_index.h"
#include "fv_osm_vector_source.h"
#include "fvkit/detail/sqlite.h"
#include "fvkit/app/search.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/overlay/vector_map_overlay.h"

namespace fs = std::filesystem;

namespace {

std::string KiawahPath() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/OSM/kiawah.mbtiles";
  return fs::is_regular_file(p) ? p : std::string();
}

// The one z14 tile Ruddy Turnstone lies in, shrunk to something a phone screen
// would show.
fv::GeoRect Viewport() {
  return fv::GeoRect{{32.6030, -80.1120}, {32.6180, -80.0930}};
}

// The whole delivered cut (its `bounds` metadata).
fv::GeoRect WholeIsland() {
  return fv::GeoRect{{32.55, -80.17}, {32.67, -79.97}};
}

bool HasTitle(const std::vector<fv::app::SearchResult>& hits,
              const std::string& title) {
  return std::any_of(hits.begin(), hits.end(),
                     [&](const fv::app::SearchResult& r) {
                       return r.title == title;
                     });
}

}  // namespace

#define SKIP_WITHOUT_KIAWAH()                 \
  const std::string kiawah = KiawahPath();    \
  if (kiawah.empty()) GTEST_SKIP() << "no Kiawah mbtiles test data"

#define OPEN_KIAWAH(var)                                     \
  auto var = std::make_shared<fv::OsmVectorSource>();        \
  {                                                          \
    const fv::Status s = var->Open(kiawah);                  \
    ASSERT_TRUE(s.ok()) << s.message;                        \
  }

TEST(OsmSearch, OneSessionFindsTheRoadInTheTilesAndThePointBesideIt) {
  SKIP_WITHOUT_KIAWAH();
  OPEN_KIAWAH(src);

  fv::OverlayManager manager;
  auto points = std::make_shared<fv::PointOverlay>("Points");
  fv::MapPoint mp;
  mp.name = "Ruddy Turnstone";  // a `.fvpoints` row spells its label `name`
  mp.category = "landmark";
  mp.position = fv::GeoPoint{32.6100, -80.1000};
  points->AddPoint(mp);
  ASSERT_TRUE(manager.Add(points).ok());

  // The delivered Kiawah cut carries `name:latin` and no plain `name` — which
  // the caller below never learns, and never has to.
  auto map = std::make_shared<fv::VectorMapOverlay>("Kiawah", src);
  ASSERT_TRUE(manager.Add(map).ok());

  fv::app::SearchSession session(manager);
  fv::app::SearchQuery q;
  q.area = Viewport();
  q.text = "ruddy turnstone";
  const std::vector<fv::app::SearchResult> hits = session.Search(q);

  ASSERT_GE(hits.size(), 2u);
  bool from_map = false, from_points = false;
  for (const fv::app::SearchResult& r : hits) {
    EXPECT_EQ("Ruddy Turnstone", r.title);
    if (r.overlay == map.get()) from_map = true;
    if (r.overlay == points.get()) from_points = true;
  }
  EXPECT_TRUE(from_map) << "the road in the tiles";
  EXPECT_TRUE(from_points) << "the point in the document";

  // ONE ROW FOR THE ROAD, not one per way segment the cutter emitted. This is
  // the merge doing its job over real data rather than over a fixture.
  size_t map_rows = 0;
  for (const fv::app::SearchResult& r : hits) {
    if (r.overlay == map.get()) ++map_rows;
  }
  EXPECT_EQ(1u, map_rows);

  // A viewport-sized query is answered at the bottom of the pyramid: nothing
  // was capped, so the road is missing from a coarser answer because the
  // CUTTER dropped it, not because this test asked for too much.
  EXPECT_EQ(14, src->last_query_zoom());
  EXPECT_FALSE(src->last_query_zoom_capped());
}

TEST(OsmSearch, TheRowNamesARealFeatureAndTheSourceCanDescribeIt) {
  SKIP_WITHOUT_KIAWAH();
  OPEN_KIAWAH(src);

  fv::VectorMapOverlay map("Kiawah", src);
  fv::app::SearchQuery q;
  q.area = Viewport();
  q.text = "ruddy turnstone";
  std::vector<fv::app::SearchResult> hits;
  map.Search(q, std::atomic<bool>{false}, hits);
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("transportation_name \xc2\xb7 minor", hits[0].detail);

  // The 128-bit ref, minted down to a uint64 and back again.
  fv::FeatureRef ref;
  ASSERT_TRUE(map.FeatureRefFor(hits[0].feature, &ref));
  EXPECT_TRUE(ref.valid());

  fv::FeatureDescription d;
  const fv::Status s = map.DescribeFeature(hits[0].feature, &d);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ("transportation_name", d.layer_name);
  EXPECT_FALSE(d.attributes.empty());

  // The same query again gives the same number: the mint is kept, so a result
  // a shell held on to still names the same road.
  std::vector<fv::app::SearchResult> again;
  map.Search(q, std::atomic<bool>{false}, again);
  ASSERT_EQ(1u, again.size());
  EXPECT_EQ(hits[0].feature, again[0].feature);
}

TEST(OsmSearch, TheTileBudgetStepsTheZoomCoarserAndThatIsTheRelevanceRule) {
  SKIP_WITHOUT_KIAWAH();
  OPEN_KIAWAH(src);

  fv::VectorMapOverlay map("Kiawah", src);
  fv::app::SearchQuery q;
  q.area = WholeIsland();
  q.max_results = 0;  // an export, not a search box: count everything
  std::vector<fv::app::SearchResult> coarse;
  map.Search(q, std::atomic<bool>{false}, coarse);

  // The island does not fit in the source's default 64-tile allowance at the
  // bottom of the pyramid, so the scan stepped coarser rather than reading
  // every tile there is. NOTHING IN THE OVERLAY DID THIS: it passes no scale,
  // which is what "the source's budget is the one number that owns how much
  // pyramid a search reads" means in practice.
  EXPECT_TRUE(src->last_query_zoom_capped());
  EXPECT_EQ(13, src->last_query_zoom());
  EXPECT_LE(src->last_query_tiles_read(), 64u);
  const size_t coarse_features = map.last_search_features();
  const size_t coarse_rows = map.last_search_results();
  EXPECT_TRUE(HasTitle(coarse, "Kiawah Island Parkway"));

  // Now the same question with the budget raised, which is the ONLY thing that
  // changes: z14, the whole island, every tile the cut has there.
  src->SetMaxTilesPerQuery(512);
  std::vector<fv::app::SearchResult> fine;
  map.Search(q, std::atomic<bool>{false}, fine);
  EXPECT_EQ(14, src->last_query_zoom());
  EXPECT_FALSE(src->last_query_zoom_capped());

  // THE PYRAMID IS THE RELEVANCE FUNCTION, and this is it as a number rather
  // than as a paragraph: the capped answer walked a FIFTH of the features and
  // still came back with three quarters of the names, because a coarse level
  // holds what the cutter thought mattered at that scale. Nominatim buys
  // importance ranking with enormous effort; this costs nothing and is honest
  // about being crude.
  EXPECT_LT(coarse_features * 3, map.last_search_features());
  EXPECT_LT(coarse_rows, map.last_search_results());
  EXPECT_GT(coarse_rows * 2, map.last_search_results());
  EXPECT_TRUE(HasTitle(fine, "Kiawah Island Parkway"));

  // A cul-de-sac survives into z13 in a cut this small (the whole island is
  // 70 tiles at the bottom), so this pack cannot pin "the coarse answer drops
  // the small roads" -- only that it costs a fraction as much to get. What it
  // DOES pin is that the two answers agree about the road they both hold.
  fv::app::SearchQuery one = q;
  one.text = "ruddy turnstone";
  std::vector<fv::app::SearchResult> named;
  map.Search(one, std::atomic<bool>{false}, named);
  ASSERT_EQ(1u, named.size());
  EXPECT_EQ("Ruddy Turnstone", named[0].title);
}

TEST(OsmSearch, ASpatialOnlyQueryIsTheNamedThingsInTheArea) {
  SKIP_WITHOUT_KIAWAH();
  OPEN_KIAWAH(src);

  fv::VectorMapOverlay map("Kiawah", src);
  fv::app::SearchQuery q;
  q.area = Viewport();
  q.max_results = 0;
  std::vector<fv::app::SearchResult> hits;
  map.Search(q, std::atomic<bool>{false}, hits);

  ASSERT_FALSE(hits.empty());
  for (const fv::app::SearchResult& r : hits) {
    EXPECT_FALSE(r.title.empty());
    EXPECT_FALSE(r.detail.empty());
    EXPECT_EQ(0, r.match_quality);  // nothing to rank by but distance
  }
  // Most of a real tile is unnamed geometry, and none of it is a row.
  EXPECT_LT(map.last_search_results(), map.last_search_features() / 4);
}

TEST(OsmSearch, NoAreaFindsNothingInAPackWithNoIndex) {
  SKIP_WITHOUT_KIAWAH();
  OPEN_KIAWAH(src);

  fv::VectorMapOverlay map("Kiawah", src);
  ASSERT_FALSE(src->HasNameIndex()) << "the delivered cut carries no index";
  fv::app::SearchQuery q;
  q.text = "ruddy turnstone";  // the whole pack, with no index to read
  std::vector<fv::app::SearchResult> hits;
  map.Search(q, std::atomic<bool>{false}, hits);
  EXPECT_TRUE(hits.empty());
  EXPECT_EQ(0u, map.last_search_features());
  EXPECT_FALSE(map.last_search_used_index());
}

// ---------------------------------------------------------------------------
// S3 — the name index, built into a copy of the real pack
// ---------------------------------------------------------------------------
//
// port/fvkit/test/vector_map_overlay_test.cpp pins the CHOICE between the two
// tiers over a fake index. What only the real pyramid can pin is that a built
// index says what the scan says — and, where it does not, that the difference
// is the one the design predicted: the index has read levels a live search
// cannot afford.

namespace {

// The pack is copied, indexed once per test binary, and the copy is what these
// tests read. The delivered TestData cut is never written to: an index is a
// derived artefact and a test that left one behind would silently change what
// every OTHER test in this file sees.
class IndexedPack {
 public:
  static const IndexedPack& Get() {
    static IndexedPack pack;
    return pack;
  }
  const std::string& path() const { return path_; }
  const fv::osm::NameIndexBuildStats& stats() const { return stats_; }
  bool ok() const { return ok_; }

 private:
  IndexedPack() {
    const std::string src = KiawahPath();
    if (src.empty()) return;
    const fs::path dir = fs::temp_directory_path() / "fv_osm_name_index_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path dst = dir / "kiawah.mbtiles";
    fs::remove(dst, ec);
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) return;
    path_ = dst.string();
    fv::osm::NameIndexBuildOptions options;
    ok_ = fv::osm::BuildNameIndex(path_, options, &stats_, nullptr).ok();
  }

  std::string path_;
  fv::osm::NameIndexBuildStats stats_;
  bool ok_ = false;
};

std::vector<std::string> TitlesOf(
    const std::vector<fv::app::SearchResult>& hits) {
  std::vector<std::string> out;
  for (const fv::app::SearchResult& r : hits) out.push_back(r.title);
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace

#define OPEN_INDEXED(var)                                              \
  const IndexedPack& pack = IndexedPack::Get();                        \
  if (pack.path().empty()) GTEST_SKIP() << "no Kiawah mbtiles test data"; \
  ASSERT_TRUE(pack.ok()) << "building the name index failed";          \
  auto var = std::make_shared<fv::OsmVectorSource>();                  \
  {                                                                    \
    const fv::Status s = var->Open(pack.path());                       \
    ASSERT_TRUE(s.ok()) << s.message;                                  \
  }                                                                    \
  ASSERT_TRUE(var->HasNameIndex())

TEST(OsmNameIndex, TheWholePackAnswersWithNoAreaAtAll) {
  OPEN_INDEXED(src);

  fv::VectorMapOverlay map("Kiawah", src);
  fv::app::SearchQuery q;
  q.text = "ruddy turnstone";  // no area — S2's one unanswerable question
  std::vector<fv::app::SearchResult> hits;
  map.Search(q, std::atomic<bool>{false}, hits);

  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("Ruddy Turnstone", hits[0].title);
  EXPECT_EQ("transportation_name \xc2\xb7 minor", hits[0].detail);
  EXPECT_TRUE(map.last_search_used_index());
  // NOT ONE TILE WAS READ to answer it.
  EXPECT_EQ(0u, src->last_query_tiles_read());
}

TEST(OsmNameIndex, ARowFoundWithoutReadingATileIsStillDescribable) {
  OPEN_INDEXED(src);

  fv::VectorMapOverlay map("Kiawah", src);
  fv::app::SearchQuery q;
  q.text = "ruddy turnstone";
  std::vector<fv::app::SearchResult> hits;
  map.Search(q, std::atomic<bool>{false}, hits);
  ASSERT_EQ(1u, hits.size());

  // The index stored z/x/y and a layer NAME; the source interned them back
  // into a live ref, and identify reads exactly the one tile it needs.
  fv::FeatureDescription d;
  const fv::Status s = map.DescribeFeature(hits[0].feature, &d);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ("transportation_name", d.layer_name);
  EXPECT_FALSE(d.attributes.empty());
  bool named = false;
  for (const fv::FeatureAttribute& a : d.attributes) {
    if (a.raw == "Ruddy Turnstone") named = true;
  }
  EXPECT_TRUE(named) << "the tile the index pointed at holds the named road";
}

TEST(OsmNameIndex, TheIndexAndTheTileScanAgreeAboutAViewport) {
  OPEN_INDEXED(src);

  fv::VectorMapOverlay map("Kiawah", src);
  fv::app::SearchQuery q;
  q.area = Viewport();
  q.text = "ruddy";
  q.max_results = 0;

  std::vector<fv::app::SearchResult> indexed;
  map.Search(q, std::atomic<bool>{false}, indexed);
  ASSERT_TRUE(map.last_search_used_index());

  map.SetUseNameIndex(false);
  std::vector<fv::app::SearchResult> scanned;
  map.Search(q, std::atomic<bool>{false}, scanned);
  ASSERT_FALSE(map.last_search_used_index());

  // THE POINT OF THE SHARED MERGE (fvkit/vector/feature_rows.h): the builder
  // ran the same scan this test just ran, so where both tiers can see the same
  // data they say the same thing — same rows, same names, not "roughly".
  EXPECT_EQ(TitlesOf(scanned), TitlesOf(indexed));
  EXPECT_FALSE(indexed.empty());
}

TEST(OsmNameIndex, TheIndexKnowsWhatACappedScanCannotAfford) {
  OPEN_INDEXED(src);

  fv::VectorMapOverlay map("Kiawah", src);
  fv::app::SearchQuery q;
  q.area = WholeIsland();
  q.text = "kiawah island";
  q.max_results = 0;

  std::vector<fv::app::SearchResult> indexed;
  map.Search(q, std::atomic<bool>{false}, indexed);

  map.SetUseNameIndex(false);
  std::vector<fv::app::SearchResult> scanned;
  map.Search(q, std::atomic<bool>{false}, scanned);
  // The island does not fit at z14 inside the source's 64-tile allowance, so
  // the live scan is answered from z13 — where the cutter kept the roads and
  // dropped the POIs.
  EXPECT_TRUE(src->last_query_zoom_capped());
  EXPECT_EQ(13, src->last_query_zoom());

  // The index was built by walking EVERY level with the budget lifted, so it
  // holds the z14 POIs the live scan could not afford to look for. This is the
  // sentence the whole session exists for, as a number.
  EXPECT_GT(indexed.size(), scanned.size());
  bool poi = false;
  for (const fv::app::SearchResult& r : indexed) {
    if (r.detail.compare(0, 3, "poi") == 0) poi = true;
  }
  EXPECT_TRUE(poi) << "a z14 POI the capped scan never reached";
}

TEST(OsmNameIndex, ProminenceIsWhatSurvivesACap) {
  OPEN_INDEXED(src);

  fv::VectorMapOverlay map("Kiawah", src);
  fv::app::SearchQuery q;
  q.text = "kiawah";
  q.max_results = 1;  // one row out of the many named for the island
  std::vector<fv::app::SearchResult> hits;
  map.Search(q, std::atomic<bool>{false}, hits);

  ASSERT_EQ(1u, hits.size());
  // MIN_ZOOM IS THE RELEVANCE FUNCTION, precomputed: the town is drawn many
  // levels above any street named after it, so it is what one row means.
  // Nominatim buys this with importance modelling; the cutter had already
  // decided it.
  EXPECT_EQ("Kiawah Island", hits[0].title);
  EXPECT_EQ("place \xc2\xb7 town", hits[0].detail);
}

TEST(OsmNameIndex, EveryPieceOfOneRoadIsStillOneRowAcrossTheWholePyramid) {
  OPEN_INDEXED(src);

  fv::VectorMapOverlay map("Kiawah", src);
  fv::app::SearchQuery q;
  q.text = "ruddy turnstone";
  std::vector<fv::app::SearchResult> hits;
  map.Search(q, std::atomic<bool>{false}, hits);
  // The road is cut at every tile seam AND appears at two zooms; one row.
  EXPECT_EQ(1u, hits.size());

  const IndexedPack& p = IndexedPack::Get();
  EXPECT_GT(p.stats().pieces, p.stats().names);
  EXPECT_EQ(0, p.stats().unresolved);
}

TEST(OsmNameIndex, WithoutTheFtsMirrorThePackStillAnswers) {
  const IndexedPack& built = IndexedPack::Get();
  if (built.path().empty()) GTEST_SKIP() << "no Kiawah mbtiles test data";
  ASSERT_TRUE(built.ok());

  // A copy with the FTS5 mirror removed — which is also what a SQLite built
  // without the module sees, since an FTS table it cannot open is a table it
  // cannot read.
  const fs::path dst =
      fs::temp_directory_path() / "fv_osm_name_index_test" / "no_fts.mbtiles";
  std::error_code ec;
  fs::remove(dst, ec);
  fs::copy_file(built.path(), dst, fs::copy_options::overwrite_existing, ec);
  ASSERT_FALSE(ec) << ec.message();
  {
    fv::detail::SqliteDb db;
    ASSERT_TRUE(db.Open(dst.string()).ok());
    ASSERT_TRUE(db.Exec("DROP TABLE search_names_fts").ok());
  }

  fv::osm::NameIndexReader reader;
  ASSERT_TRUE(reader.Open(dst.string()).ok());
  EXPECT_FALSE(reader.uses_fts());
  EXPECT_GT(reader.row_count(), 0);

  auto src = std::make_shared<fv::OsmVectorSource>();
  ASSERT_TRUE(src->Open(dst.string()).ok());
  ASSERT_TRUE(src->HasNameIndex());
  fv::VectorMapOverlay map("Kiawah", src);
  fv::app::SearchQuery q;
  q.text = "rud tur";  // the token rule, over a plain table scan
  std::vector<fv::app::SearchResult> hits;
  map.Search(q, std::atomic<bool>{false}, hits);
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("Ruddy Turnstone", hits[0].title);
  EXPECT_TRUE(map.last_search_used_index());
}

TEST(OsmNameIndex, RebuildingReplacesRatherThanAppends) {
  const IndexedPack& built = IndexedPack::Get();
  if (built.path().empty()) GTEST_SKIP() << "no Kiawah mbtiles test data";
  ASSERT_TRUE(built.ok());

  const fs::path dst =
      fs::temp_directory_path() / "fv_osm_name_index_test" / "rebuilt.mbtiles";
  std::error_code ec;
  fs::remove(dst, ec);
  fs::copy_file(built.path(), dst, fs::copy_options::overwrite_existing, ec);
  ASSERT_FALSE(ec) << ec.message();

  // A pack that ALREADY has an index, rebuilt: the builder scans tiles rather
  // than asking the pack about itself, so the answer is the same size and not
  // twice it.
  fv::osm::NameIndexBuildStats again;
  const fv::Status s = fv::osm::BuildNameIndex(dst.string(), {}, &again, nullptr);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(built.stats().names, again.names);
}
