// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The road network as a search provider (port/search-plan-COMPLETE.md, S4).
//
// WHAT THESE PIN, and none of it is a pixel. The road graph is the ROUTABLE
// answer to "where is X" — the same road is usually in the vector pack too,
// and both rows are shown — so what has to be true is that it answers the
// same question the other providers answer, in the same words, over a data
// structure that is shaped nothing like theirs:
//
//   * the name table is the index, and an unnamed arc is never a row;
//   * ONE ROAD IS ONE ROW even though OSM split the way at every junction and
//     the graph then stored every edge twice, from both ends;
//   * an area query goes through the grid and catches a road that merely
//     CROSSES the box with both endpoints outside it;
//   * a query with neither text nor an area answers nothing, deliberately;
//   * the cap is applied after a ranking, and the ranking is the road class —
//     which is what makes a capped search useful rather than arbitrary;
//   * the id a result carries is the one a CLICK carries, so identify does not
//     care which of the two asked.
//
// The fixture is a hand-built graph: "Ruddy Turnstone" cut into three edges
// the way a real extract cuts it, a cycleway spur, an unnamed service road,
// and two "Main Street"s of different classes a mile apart.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fv_road_graph_overlay.h"
#include "fvkit/app/search.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/point_overlay.h"

namespace {

using fv::GeoPoint;
using fv::GeoRect;
using fv::MapPoint;
using fv::OverlayManager;
using fv::PointOverlay;
using fv::RoadArcInfo;
using fv::RoadGraphOverlay;
using fv::app::SearchQuery;
using fv::app::SearchResult;
using fv::app::SearchSession;
using fv::routing::kArcBackward;
using fv::routing::kArcForward;
using fv::routing::RoadArc;
using fv::routing::RoadClass;
using fv::routing::RoadGraph;
using fv::routing::RoadNode;

int32_t E7(double d) { return static_cast<int32_t>(d * 1e7 + (d < 0 ? -0.5 : 0.5)); }

struct Edge {
  uint32_t u = 0;
  uint32_t v = 0;
  RoadClass klass = RoadClass::kResidential;
  uint32_t name = 0;
};

// The CSR assembly BuildRoadGraph does, so the provider meets the arc layout
// it will meet in the field rather than a friendlier one written for a test.
std::shared_ptr<RoadGraph> MakeGraph(const std::vector<RoadNode>& nodes,
                                     const std::vector<Edge>& edges,
                                     const std::vector<std::string>& names) {
  auto g = std::make_shared<RoadGraph>();
  g->mutable_nodes() = nodes;
  g->mutable_names() = names;

  std::vector<uint32_t>& begin = g->mutable_arc_begin();
  begin.assign(nodes.size() + 1, 0);
  for (const Edge& e : edges) {
    ++begin[e.u + 1];
    ++begin[e.v + 1];
  }
  for (size_t i = 1; i < begin.size(); ++i) begin[i] += begin[i - 1];

  std::vector<RoadArc>& arcs = g->mutable_arcs();
  arcs.resize(edges.size() * 2);
  std::vector<uint32_t> fill(begin.begin(), begin.end());
  for (const Edge& e : edges) {
    RoadArc a;
    a.klass = e.klass;
    a.name = e.name;
    a.speed_kph = 40;
    const GeoPoint p{nodes[e.u].lat_e7 * 1e-7, nodes[e.u].lon_e7 * 1e-7};
    const GeoPoint q{nodes[e.v].lat_e7 * 1e-7, nodes[e.v].lon_e7 * 1e-7};
    a.length_m = static_cast<float>(
        fv::routing::GreatCircleMeters(p.lat, p.lon, q.lat, q.lon));
    a.flags = static_cast<uint16_t>(kArcForward | kArcBackward);

    RoadArc fwd = a;
    fwd.target = e.v;
    arcs[fill[e.u]++] = fwd;
    RoadArc rev = a;
    rev.target = e.u;
    arcs[fill[e.v]++] = rev;
  }
  g->Finalize();
  return g;
}

// Kiawah-ish. 0.001 degrees of longitude is about 94 m here, so the three
// pieces of Ruddy Turnstone are a real street's length and the two Main
// Streets are a kilometre and two kilometres north of it.
//
//   n0 -- n1 -- n2 -- n3    "Ruddy Turnstone", residential, THREE edges
//   n2 -- n4                "Beach Walk", cycleway
//   n1 -- n5                unnamed service road
//   n6 -- n7                "Main Street", primary
//   n8 -- n9                "Main Street", service
enum : uint32_t { kUnnamed = 0, kTurnstone = 1, kBeachWalk = 2, kMain = 3 };

std::shared_ptr<RoadGraph> MakeStreets() {
  const std::vector<RoadNode> nodes = {
      {E7(32.6000), E7(-80.1150), 1000},  // n0
      {E7(32.6000), E7(-80.1140), 1001},  // n1
      {E7(32.6000), E7(-80.1130), 1002},  // n2
      {E7(32.6000), E7(-80.1120), 1003},  // n3
      {E7(32.6010), E7(-80.1130), 1004},  // n4
      {E7(32.5990), E7(-80.1140), 1005},  // n5
      {E7(32.6100), E7(-80.1150), 1006},  // n6
      {E7(32.6100), E7(-80.1130), 1007},  // n7
      {E7(32.6200), E7(-80.1150), 1008},  // n8
      {E7(32.6200), E7(-80.1130), 1009},  // n9
  };
  const std::vector<std::string> names = {"", "Ruddy Turnstone", "Beach Walk",
                                          "Main Street"};
  const std::vector<Edge> edges = {
      {0, 1, RoadClass::kResidential, kTurnstone},
      {1, 2, RoadClass::kResidential, kTurnstone},
      {2, 3, RoadClass::kResidential, kTurnstone},
      {2, 4, RoadClass::kCycleway, kBeachWalk},
      {1, 5, RoadClass::kService, kUnnamed},
      {6, 7, RoadClass::kPrimary, kMain},
      {8, 9, RoadClass::kService, kMain},
  };
  return MakeGraph(nodes, edges, names);
}

std::shared_ptr<RoadGraphOverlay> MakeOverlay() {
  auto o = std::make_shared<RoadGraphOverlay>();
  o->SetGraph(MakeStreets());
  return o;
}

std::vector<SearchResult> Ask(RoadGraphOverlay& o, const SearchQuery& q) {
  std::atomic<bool> cancel{false};
  std::vector<SearchResult> out;
  o.Search(q, cancel, out);
  return out;
}

const SearchResult* FindTitle(const std::vector<SearchResult>& r,
                              const std::string& title) {
  for (const SearchResult& s : r) {
    if (s.title == title) return &s;
  }
  return nullptr;
}

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

std::string KiawahGraphPath() {
  const std::filesystem::path p =
      std::filesystem::path(TestDataDir()) / "OSM" / "kiawah.fvroad";
  std::error_code ec;
  return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

}  // namespace

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

TEST(RoadGraphSearch, ThreeEdgesOfOneStreetAreOneRow) {
  auto o = MakeOverlay();
  SearchQuery q;
  q.text = "ruddy turnstone";
  const std::vector<SearchResult> r = Ask(*o, q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("Ruddy Turnstone", r[0].title);
  EXPECT_EQ("road \xc2\xb7 residential", r[0].detail);
  EXPECT_EQ(0, r[0].match_quality);  // the whole string
  EXPECT_EQ(o.get(), r[0].overlay);
  // The bounds is the union of the three pieces: the whole street, which is
  // what "go there" frames.
  EXPECT_NEAR(-80.1150, r[0].bounds.ll.lon, 1e-9);
  EXPECT_NEAR(-80.1120, r[0].bounds.ur.lon, 1e-9);
  // The anchor is a point ON the road, not the centre of anything.
  EXPECT_NEAR(32.6000, r[0].position.lat, 1e-6);
}

TEST(RoadGraphSearch, TokenPrefixFindsItAndSaysSoInTheQuality) {
  auto o = MakeOverlay();
  SearchQuery q;
  q.text = "rud tur";  // the contract TextMatchQuality owns, not this provider
  const std::vector<SearchResult> r = Ask(*o, q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("Ruddy Turnstone", r[0].title);
  EXPECT_EQ(2, r[0].match_quality);
}

TEST(RoadGraphSearch, AnUnnamedArcIsNeverARow) {
  auto o = MakeOverlay();
  SearchQuery q;
  q.area = GeoRect{{32.5980, -80.1160}, {32.6020, -80.1110}};
  const std::vector<SearchResult> r = Ask(*o, q);
  // The service road out of n1 is in the box and has no name. Two rows, both
  // named: an unnamed road is not a choice anybody can make.
  ASSERT_EQ(2u, r.size());
  EXPECT_TRUE(FindTitle(r, "Ruddy Turnstone") != nullptr);
  EXPECT_TRUE(FindTitle(r, "Beach Walk") != nullptr);
}

TEST(RoadGraphSearch, TwoStreetsOfOneNameAndDifferentClassesAreTwoRows) {
  auto o = MakeOverlay();
  SearchQuery q;
  q.text = "Main Street";
  const std::vector<SearchResult> r = Ask(*o, q);
  ASSERT_EQ(2u, r.size());
  // RANKED BY CLASS, which is the graph's own free relevance function: the
  // primary is the Main Street somebody typing "main" meant.
  EXPECT_EQ("road \xc2\xb7 primary", r[0].detail);
  EXPECT_EQ("road \xc2\xb7 service", r[1].detail);
}

TEST(RoadGraphSearch, TheCapIsAppliedAfterTheRanking) {
  auto o = MakeOverlay();
  SearchQuery q;
  q.text = "Main Street";
  q.max_results = 1;
  const std::vector<SearchResult> r = Ask(*o, q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("road \xc2\xb7 primary", r[0].detail);
}

// ---------------------------------------------------------------------------
// The two arcs of one edge
// ---------------------------------------------------------------------------

TEST(RoadGraphSearch, MergingOffGivesThePiecesAndNotTheirMirrors) {
  auto o = MakeOverlay();
  o->SetSearchMergeGapMeters(-1.0);  // raw pieces
  SearchQuery q;
  q.text = "ruddy turnstone";
  const std::vector<SearchResult> r = Ask(*o, q);
  // THREE, not six. The graph stores every edge from both ends, and merging
  // would have hidden a doubled answer rather than preventing one.
  EXPECT_EQ(3u, r.size());
}

// ---------------------------------------------------------------------------
// Area
// ---------------------------------------------------------------------------

TEST(RoadGraphSearch, ARoadCrossingTheBoxIsInItEvenWithBothEndsOutside) {
  auto o = MakeOverlay();
  SearchQuery q;
  // Strictly inside the n0-n1 edge: neither endpoint is in this box.
  q.area = GeoRect{{32.5999, -80.11480}, {32.6001, -80.11420}};
  const std::vector<SearchResult> r = Ask(*o, q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("Ruddy Turnstone", r[0].title);
}

TEST(RoadGraphSearch, AnAreaQueryDoesNotReachTheNextStreetOver) {
  auto o = MakeOverlay();
  SearchQuery q;
  q.area = GeoRect{{32.5999, -80.11480}, {32.6001, -80.11420}};
  EXPECT_TRUE(FindTitle(Ask(*o, q), "Main Street") == nullptr);
}

TEST(RoadGraphSearch, TextAndAreaTogetherAreAnAnd) {
  auto o = MakeOverlay();
  SearchQuery q;
  q.text = "main";
  q.area = GeoRect{{32.6090, -80.1160}, {32.6110, -80.1120}};  // the primary only
  const std::vector<SearchResult> r = Ask(*o, q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("road \xc2\xb7 primary", r[0].detail);
}

TEST(RoadGraphSearch, NeitherTextNorAreaFindsNothing) {
  auto o = MakeOverlay();
  SearchQuery q;
  EXPECT_TRUE(Ask(*o, q).empty());
  EXPECT_EQ(0u, o->last_search_arcs());
}

TEST(RoadGraphSearch, NoGraphIsNotAnError) {
  RoadGraphOverlay o;
  SearchQuery q;
  q.text = "anything";
  EXPECT_TRUE(Ask(o, q).empty());
}

// ---------------------------------------------------------------------------
// Cancellation and the budget
// ---------------------------------------------------------------------------

TEST(RoadGraphSearch, ACancelledSearchReturns) {
  auto o = MakeOverlay();
  SearchQuery q;
  q.text = "ruddy turnstone";
  std::atomic<bool> cancel{true};
  std::vector<SearchResult> out;
  o->Search(q, cancel, out);
  EXPECT_TRUE(out.empty());
}

TEST(RoadGraphSearch, TheArcBudgetStopsTheWalk) {
  auto o = MakeOverlay();
  o->SetSearchArcBudget(1);
  SearchQuery q;
  q.text = "ruddy turnstone";
  const std::vector<SearchResult> r = Ask(*o, q);
  EXPECT_EQ(1u, o->last_search_arcs());
  // One arc examined is at most one row, and the honest answer is whatever it
  // found rather than an error: the ranking means what survives is the most
  // important thing seen, not a random one.
  EXPECT_LE(r.size(), 1u);
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

TEST(RoadGraphSearch, TheIdIsTheOneAClickCarries) {
  auto o = MakeOverlay();
  SearchQuery q;
  q.text = "beach walk";
  const std::vector<SearchResult> r = Ask(*o, q);
  ASSERT_EQ(1u, r.size());
  ASSERT_TRUE(RoadGraphOverlay::FeatureIsArc(r[0].feature));
  const RoadArcInfo info =
      o->ArcInfoFor(RoadGraphOverlay::FeatureIndex(r[0].feature));
  ASSERT_TRUE(info.valid);
  EXPECT_EQ("Beach Walk", info.name);
  EXPECT_EQ("cycleway", info.road_class);
}

// ---------------------------------------------------------------------------
// Through the stack
// ---------------------------------------------------------------------------

TEST(RoadGraphSearch, TheGraphAnswersBesideAPointDocumentInOneQuery) {
  auto roads = MakeOverlay();
  auto points = std::make_shared<PointOverlay>("Points");
  MapPoint mp;
  mp.name = "Ruddy Turnstone";
  mp.position = GeoPoint{32.6000, -80.1140};
  points->AddPoint(mp);
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(points).ok());
  ASSERT_TRUE(mgr.Add(roads).ok());

  SearchQuery q;
  q.text = "ruddy turnstone";
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  // TWO ANSWERS AND BOTH ARE TRUE — the plan's "cross-provider dedup is a
  // heuristic swamp" in one assertion. The caller tells them apart by
  // `detail`, and by which overlay each came from.
  ASSERT_EQ(2u, r.size());
  bool saw_road = false, saw_point = false;
  for (const SearchResult& s : r) {
    if (s.overlay == roads.get()) saw_road = true;
    if (s.overlay == points.get()) saw_point = true;
  }
  EXPECT_TRUE(saw_road);
  EXPECT_TRUE(saw_point);
}

// ---------------------------------------------------------------------------
// The real island
// ---------------------------------------------------------------------------

TEST(RoadGraphSearch, KiawahsOwnGraphAnswersWithOneRowPerStreet) {
  const std::string path = KiawahGraphPath();
  if (path.empty()) GTEST_SKIP() << "no kiawah.fvroad in TestData";
  auto g = std::make_shared<fv::routing::RoadGraph>();
  ASSERT_TRUE(fv::routing::RoadGraph::Load(path, g.get()).ok());

  RoadGraphOverlay o;
  o.SetGraph(g);
  SearchQuery q;
  q.text = "ruddy turnstone";
  const std::vector<SearchResult> r = Ask(o, q);
  // The extract splits the street at every junction; the answer is the street.
  ASSERT_FALSE(r.empty());
  EXPECT_EQ("Ruddy Turnstone", r[0].title);
  EXPECT_EQ(1u, r.size());
  EXPECT_GT(o.last_search_arcs(), r.size());
}
