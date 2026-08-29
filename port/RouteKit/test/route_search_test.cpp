// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Global search, S1: the route's half, and the two-overlay acceptance
// (port/search-plan-COMPLETE.md).
//
// The plan's own acceptance criterion for S1 is a query that crosses a stack:
// one search call, two overlay types that have never heard of each other, and
// a caller that names neither the field the point overlay stores a name in nor
// the field the route overlay does. That test is at the bottom of this file
// and everything above it exists to make its failure legible.
//
// It lives in RouteKit rather than beside the session's own tests because the
// dependency only points one way -- RouteKit links fvkit, and fvkit must not
// link RouteKit -- so this is the lowest layer that can put a route and a
// point document in one stack.

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fv_route_overlay.h"
#include "fvkit/app/search.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/point_overlay.h"

namespace {

using fv::GeoPoint;
using fv::GeoRect;
using fv::MapPoint;
using fv::OverlayManager;
using fv::PointOverlay;
using fv::RouteOverlay;
using fv::RouteWaypoint;
using fv::app::SearchQuery;
using fv::app::SearchResult;
using fv::app::SearchSession;

// The demo route's two ends, and a via between them. Real Kiawah coordinates,
// because the rest of the port's fixtures are and a coordinate that means
// something is one somebody can check on a chart.
std::shared_ptr<RouteOverlay> MakeRoute(const std::string& name) {
  auto o = std::make_shared<RouteOverlay>(name);
  o->SetWaypoints({{"Ruddy Turnstone", {32.6044007, -80.1083007}},
                   {"Governors Drive", {32.6100000, -80.0900000}},
                   {"Beach Club", {32.5956008, -80.0655000}}});
  return o;
}

const SearchResult* FindTitle(const std::vector<SearchResult>& r,
                              const std::string& title) {
  for (const SearchResult& s : r) {
    if (s.title == title) return &s;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// The route provider
// ---------------------------------------------------------------------------

TEST(RouteSearch, FindsAWaypointByNameAndNamesItBackTheWayAPickDoes) {
  auto route = MakeRoute("Kiawah loop");
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(route).ok());

  SearchQuery q;
  q.text = "rud tur";  // the token-prefix contract, on a waypoint label
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("Ruddy Turnstone", r[0].title);
  EXPECT_EQ("waypoint \xc2\xb7 Kiawah loop", r[0].detail);
  EXPECT_EQ(route.get(), r[0].overlay);
  EXPECT_DOUBLE_EQ(32.6044007, r[0].position.lat);
  // The SAME minted id HitTestPoint uses, so one translation serves both.
  EXPECT_EQ("Ruddy Turnstone", route->LabelForFeature(r[0].feature));
}

TEST(RouteSearch, TheRouteItselfIsARowWithTheWholeRouteAsItsBounds) {
  auto route = MakeRoute("Kiawah loop");
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(route).ok());

  SearchQuery q;
  q.text = "kiawah";
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("Kiawah loop", r[0].title);
  EXPECT_EQ("route \xc2\xb7 3 waypoints", r[0].detail);
  // Feature 0 IS the route: minted waypoint ids start at 1, so zero was free.
  EXPECT_EQ(0u, r[0].feature);
  EXPECT_EQ("", route->LabelForFeature(0));
  // The box the waypoints fit in -- this is what "go there" frames, and the
  // only reason SearchResult carries bounds at all.
  EXPECT_NEAR(32.5956008, r[0].bounds.ll.lat, 1e-9);
  EXPECT_NEAR(32.6100000, r[0].bounds.ur.lat, 1e-9);
  EXPECT_NEAR(-80.1083007, r[0].bounds.ll.lon, 1e-9);
  EXPECT_NEAR(-80.0655000, r[0].bounds.ur.lon, 1e-9);
  // The representative point is the middle of that box, not whichever end the
  // file happens to start with.
  EXPECT_NEAR((32.5956008 + 32.6100000) / 2.0, r[0].position.lat, 1e-9);
}

TEST(RouteSearch, AnUnsavedRouteAnswersToTheNameTheUserCanSee) {
  // A route that has never been to disk is findable by the name in the overlay
  // list, because the constructor seeds the document's name from the overlay's
  // -- so "the name the user sees" and "the name the document carries" are the
  // same string from the first frame, and the provider's fallback to Name() is
  // there only for a document that came back from disk without one.
  auto route = MakeRoute("Route1");
  ASSERT_EQ("Route1", route->doc().name());
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(route).ok());

  SearchQuery q;
  q.text = "route1";
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("Route1", r[0].title);
  EXPECT_EQ(0u, r[0].feature);
}

TEST(RouteSearch, AnEmptyRouteIsNotFindable) {
  // Every field that matters is derived from the waypoints; a row with an
  // invented position would still sort somewhere, which is worse than no row.
  auto route = std::make_shared<RouteOverlay>("Kiawah loop");
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(route).ok());

  SearchQuery q;
  q.text = "kiawah";
  EXPECT_TRUE(SearchSession(mgr).Search(q).empty());
}

TEST(RouteSearch, TheAreaTestForARouteIsItsBoxAndForAWaypointItsPoint) {
  auto route = MakeRoute("Kiawah loop");
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(route).ok());

  // A box around the WEST end only. The route runs through it, so the route
  // answers; only the waypoint inside it does.
  SearchQuery q;
  q.area = GeoRect{{32.600, -80.115}, {32.607, -80.105}};
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(2u, r.size());
  EXPECT_NE(nullptr, FindTitle(r, "Kiawah loop"));
  EXPECT_NE(nullptr, FindTitle(r, "Ruddy Turnstone"));
  EXPECT_EQ(nullptr, FindTitle(r, "Beach Club"));
}

TEST(RouteSearch, ItAnswersWithNothingDrawnAndNothingVisible) {
  // The contrast with this class's own HitTestPoint, and the reason search is
  // a separate capability: a hit is about the frame that was drawn, and this
  // is about the document. Nothing here has ever been rendered.
  auto route = MakeRoute("Kiawah loop");
  route->SetVisible(false);
  ASSERT_FALSE(route->has_projection());
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(route).ok());

  SearchQuery q;
  q.text = "beach";
  EXPECT_EQ(1u, SearchSession(mgr).Search(q).size());
  q.visible_only = true;
  EXPECT_TRUE(SearchSession(mgr).Search(q).empty());
}

TEST(RouteSearch, ItIsCancellableBetweenWaypoints) {
  auto route = MakeRoute("Kiawah loop");
  std::atomic<bool> cancel{true};
  std::vector<SearchResult> out;
  route->Search(SearchQuery{}, cancel, out);
  // The route's own row is appended before the walk starts, which is honest:
  // the cap and the flag are polled per WAYPOINT, and there is exactly one
  // row that is not one.
  EXPECT_EQ(1u, out.size());
  EXPECT_EQ("Kiawah loop", out[0].title);
}

// ---------------------------------------------------------------------------
// The acceptance: one query, two overlay types, no field names
// ---------------------------------------------------------------------------

TEST(RouteSearch, OneQueryFindsTheWaypointAndThePointOfTheSameName) {
  // "Ruddy Turnstone" is a waypoint of the route AND a surveyed point in the
  // `.fvpoints` document. One overlay spells its label `label`, the other
  // spells it `name`, and the caller below names neither.
  auto route = MakeRoute("Kiawah loop");
  auto points = std::make_shared<PointOverlay>("Points");
  MapPoint p;
  p.id = 42;
  p.name = "Ruddy Turnstone";
  p.category = "junction";
  p.position = GeoPoint{32.6044007, -80.1083007};
  points->SetPoints({p});

  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(route).ok());
  ASSERT_TRUE(mgr.Add(points).ok());  // added second, so topmost

  SearchQuery q;
  q.text = "ruddy turnstone";
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(2u, r.size());

  // Both are exact matches at the same coordinate, so the stack decides: the
  // point overlay is on top. What tells the two rows apart is `detail`, which
  // is the whole job that field has.
  EXPECT_EQ(points.get(), r[0].overlay);
  EXPECT_EQ("point \xc2\xb7 junction", r[0].detail);
  EXPECT_EQ(42u, r[0].feature);
  EXPECT_EQ(route.get(), r[1].overlay);
  EXPECT_EQ("waypoint \xc2\xb7 Kiawah loop", r[1].detail);

  // And the caller can go from either row back to the thing itself, through
  // the same flat provenance a pick uses.
  ASSERT_NE(nullptr, points->Find((int64_t)r[0].feature));
  EXPECT_EQ("Ruddy Turnstone", points->Find((int64_t)r[0].feature)->name);
  EXPECT_EQ("Ruddy Turnstone", route->LabelForFeature(r[1].feature));
}

TEST(RouteSearch, ASpatialQueryAcrossTheStackIsOrderedByDistance) {
  auto route = MakeRoute("Kiawah loop");
  auto points = std::make_shared<PointOverlay>("Points");
  MapPoint p;
  p.id = 1;
  p.name = "Beach Club kiosk";
  // 30 m or so north of the route's east end.
  p.position = GeoPoint{32.5958708, -80.0655000};
  points->SetPoints({p});

  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(points).ok());
  ASSERT_TRUE(mgr.Add(route).ok());

  SearchQuery q;
  q.near = GeoPoint{32.5956008, -80.0655000};  // the waypoint itself
  q.radius_m = 200.0;
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  // The east waypoint (0 m), the kiosk (~30 m), and the route's own row --
  // whose representative point is the middle of the route, far outside 200 m,
  // so the radius cuts it. The other two waypoints are kilometres away.
  ASSERT_EQ(2u, r.size());
  EXPECT_EQ("Beach Club", r[0].title);
  EXPECT_EQ("Beach Club kiosk", r[1].title);
}

}  // namespace
