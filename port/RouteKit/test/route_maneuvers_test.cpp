// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// GD1 over the real Kiawah graph: the half of the turn list that only a routed
// route can pin. The angle arithmetic is fvkit's own test; what is checked
// here is that a router's legs land where the builder thinks they do — a leg's
// `geometry_begin` naming the junction it was joined at, not an index off by
// one road — and that the instructions a rider would actually be given down
// Ruddy Turnstone are the shape of a ride rather than a corner every 30 m.

#include "fv_route_maneuvers.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "fv_route_doc.h"
#include "fv_route_planner.h"

namespace {

using fv::GeoPoint;
using fv::ManeuversOf;
using fv::ManeuverShapeOf;
using fv::RoutePlanner;
using fv::nav::Maneuver;
using fv::nav::ManeuverType;

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

std::string KiawahGraph() {
  const std::filesystem::path p =
      std::filesystem::path(TestDataDir()) / "OSM" / "kiawah.fvroad";
  std::error_code ec;
  return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

#define SKIP_WITHOUT_GRAPH()                                            \
  const std::string graph_path = KiawahGraph();                         \
  if (graph_path.empty()) {                                             \
    GTEST_SKIP() << "no TestData/OSM/kiawah.fvroad; run fvgraph build"; \
  }

const GeoPoint kRuddyTurnstone{32.6044007, -80.1083007};
const GeoPoint kBeachClub{32.59297596527702, -80.11767417454368};

// The route the rest of the file asks questions of.
bool RideToTheBeachClub(const std::string& graph_path, fv::routing::Route* out) {
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  if (!planner.EnsureGraph().ok()) return false;
  fv::routing::Router router(*planner.graph());
  fv::routing::RouteOptions options;
  options.cycle_only = true;
  return router.RouteVia({kRuddyTurnstone, kBeachClub}, options, out).ok() && out->found;
}

}  // namespace

// Every leg boundary is a real index into the drawn line, in order, and the
// point it names is the one both legs share.
TEST(RouteManeuvers, LegBoundariesIndexTheGeometry) {
  SKIP_WITHOUT_GRAPH();
  fv::routing::Route route;
  ASSERT_TRUE(RideToTheBeachClub(graph_path, &route));
  ASSERT_GT(route.legs.size(), 1u);

  EXPECT_EQ(route.legs.front().geometry_begin, 0u);
  uint32_t previous = 0;
  for (size_t i = 1; i < route.legs.size(); ++i) {
    const uint32_t begin = route.legs[i].geometry_begin;
    EXPECT_GE(begin, previous) << "leg " << i << " starts before the one before it";
    EXPECT_LT(begin, route.geometry.size());
    previous = begin;
  }
  EXPECT_EQ(ManeuverShapeOf(route).legs.size(), route.legs.size());
}

// A leg's own length is the distance from its boundary to the next one. This
// is the check that would fail if `geometry_begin` were off by an arc.
TEST(RouteManeuvers, ALegsLengthMatchesTheLineBetweenItsBoundaries) {
  SKIP_WITHOUT_GRAPH();
  fv::routing::Route route;
  ASSERT_TRUE(RideToTheBeachClub(graph_path, &route));

  const std::vector<Maneuver> m = ManeuversOf(route);
  ASSERT_GE(m.size(), 2u);
  // Depart at zero, arrive at the route's own length: the along-route
  // distances the banner counts down are the router's metres, not a second
  // measurement that drifts from them.
  EXPECT_DOUBLE_EQ(m.front().distance_m, 0.0);
  // One metre over two kilometres: the maneuver distances and the router's
  // own length are the same measurement, not two that nearly agree.
  EXPECT_NEAR(m.back().distance_m, route.length_m, 1.0);
}

TEST(RouteManeuvers, TheRideHasTurnsAndTheyAreSpacedLikeRoads) {
  SKIP_WITHOUT_GRAPH();
  fv::routing::Route route;
  ASSERT_TRUE(RideToTheBeachClub(graph_path, &route));

  const std::vector<Maneuver> m = ManeuversOf(route);
  ASSERT_GE(m.size(), 3u) << "a 2 km ride across the island with no turn at all";
  EXPECT_EQ(m.front().type, ManeuverType::kDepart);
  EXPECT_EQ(m.back().type, ManeuverType::kArrive);

  for (size_t i = 1; i + 1 < m.size(); ++i) {
    EXPECT_GE(std::fabs(m[i].turn_deg), 20.0)
        << ManeuverTypeName(m[i].type) << " at " << m[i].distance_m << " m";
    EXPECT_NE(m[i].type, ManeuverType::kDepart);
    EXPECT_NE(m[i].type, ManeuverType::kArrive);
    EXPECT_GT(m[i].distance_m, m[i - 1].distance_m);
  }
  // The legs a route reports are name runs and most of them are not corners.
  EXPECT_LT(m.size(), route.legs.size() + 2u);
}

// Both ends of this route are mid-road snaps, and the fixture's last named way
// is joined half a metre before the destination. That is the case end_margin_m
// exists for, and it is worth pinning on the real route rather than only on a
// built one.
TEST(RouteManeuvers, NothingIsAnnouncedOnTopOfTheDestination) {
  SKIP_WITHOUT_GRAPH();
  fv::routing::Route route;
  ASSERT_TRUE(RideToTheBeachClub(graph_path, &route));

  const std::vector<Maneuver> m = ManeuversOf(route);
  ASSERT_GE(m.size(), 2u);
  const double end = m.back().distance_m;
  for (size_t i = 1; i + 1 < m.size(); ++i) {
    EXPECT_GE(m[i].distance_m, 20.0);
    EXPECT_LE(m[i].distance_m, end - 20.0);
  }

  // Turned off, the route does have a corner in that last half metre — so the
  // margin is dropping something real rather than describing a route with no
  // such case in it.
  fv::nav::ManeuverSettings keep_everything;
  keep_everything.end_margin_m = 0.0;
  EXPECT_GT(ManeuversOf(route, keep_everything).size(), m.size());
}

// A route that was not found is an empty shape, not a crash and not a
// one-instruction list.
TEST(RouteManeuvers, ARouteThatWasNotFoundHasNoInstructions) {
  fv::routing::Route none;
  EXPECT_TRUE(ManeuverShapeOf(none).geometry.empty());
  EXPECT_TRUE(ManeuversOf(none).empty());
}

namespace {

/// A route planned on `graph_path` the way RoutePlanner prices it, with the
/// options it used.
struct Planned {
  RoutePlanner planner;
  fv::routing::RouteOptions options;
  fv::routing::Route route;
  explicit Planned(const std::string& graph_path, const std::string& rules)
      : planner(graph_path, rules) {}
};

bool PlanLikeThePlanner(const std::vector<GeoPoint>& stops, const fv::RoutePlanOptions& in,
                        Planned* p) {
  if (!p->planner.EnsureGraph().ok()) return false;
  if (!p->planner.BuildOptions(in, &p->options).ok()) return false;
  fv::routing::Router router(*p->planner.graph());
  return router.RouteVia(stops, p->options, &p->route).ok() && p->route.found;
}

/// Plans a `.fvrte` fixture on `graph_path` with `rules`.
bool PlanFixture(const std::string& graph_path, const std::string& rules,
                 const std::string& fvrte, Planned* p) {
  fv::RouteDoc doc;
  if (!doc.Read(fvrte).ok()) return false;
  std::vector<GeoPoint> stops;
  for (const fv::RouteWaypoint& w : doc.waypoints()) stops.push_back(w.position);
  fv::RoutePlanOptions in;
  in.profile = doc.profile();
  if (doc.avoid_hills()) in.avoid_hills = true;
  return PlanLikeThePlanner(stops, in, p);
}

/// True when `m` has a maneuver within `tol_m` of `along_m`.
bool HasManeuverNear(const std::vector<Maneuver>& m, double along_m, double tol_m) {
  for (size_t i = 1; i + 1 < m.size(); ++i)
    if (std::fabs(m[i].distance_m - along_m) <= tol_m) return true;
  return false;
}

}  // namespace

// Junction candidates add turns without losing any leg-boundary turn, and on
// the Kiawah fixtures the list grows by no more than about a quarter.
TEST(RouteManeuvers, KiawahJunctionsAddFewTurns) {
  SKIP_WITHOUT_GRAPH();
  size_t before_total = 0;
  size_t after_total = 0;
  for (const char* fixture : {FV_ROUTE_FIXTURE_FILE, FV_ROUTE_BEACH_FIXTURE_FILE}) {
    Planned p(graph_path, FV_ROUTE_RULES_FILE);
    ASSERT_TRUE(PlanFixture(graph_path, FV_ROUTE_RULES_FILE, fixture, &p)) << fixture;
    const std::vector<Maneuver> before = ManeuversOf(p.route);
    const std::vector<Maneuver> after = ManeuversOf(p.route, *p.planner.graph(), p.options);
    for (size_t i = 1; i + 1 < before.size(); ++i)
      EXPECT_TRUE(HasManeuverNear(after, before[i].distance_m, 0.01))
          << fixture << ": lost the turn at " << before[i].distance_m << " m";
    before_total += before.size();
    after_total += after.size();
  }
  EXPECT_LE(after_total * 4, before_total * 5);
}

// The 2026-11-08 Atlanta ride (port/pippin-reroute-plan.md section 2): the two
// unannounced junction turns become maneuvers and none of the leg-boundary
// turns is lost.
TEST(RouteManeuvers, AtlantaRideAnnouncesTheJunctionTurns) {
  const std::filesystem::path graph =
      std::filesystem::path(TestDataDir()) / "OSM" / "atlanta.fvroad";
  if (!std::filesystem::exists(graph)) GTEST_SKIP() << "no TestData/OSM/atlanta.fvroad";
  Planned p(graph.string(), FV_ROUTE_CITY_RULES_FILE);
  fv::RoutePlanOptions in;
  in.profile = "bicycle";
  in.avoid_hills = true;
  ASSERT_TRUE(PlanLikeThePlanner({{33.78421411913466, -84.44998056736293},
                                  {33.822726949999996, -84.4314115}},
                                 in, &p));
  EXPECT_NEAR(p.route.length_m, 8990.0, 30.0);

  const std::vector<Maneuver> before = ManeuversOf(p.route);
  const std::vector<Maneuver> after = ManeuversOf(p.route, *p.planner.graph(), p.options);
  EXPECT_EQ(before.size(), 15u);
  for (size_t i = 1; i + 1 < before.size(); ++i)
    EXPECT_TRUE(HasManeuverNear(after, before[i].distance_m, 0.01))
        << "lost the turn at " << before[i].distance_m << " m";
  EXPECT_TRUE(HasManeuverNear(after, 2705.0, 15.0)) << "no turn onto the service road";
  EXPECT_TRUE(HasManeuverNear(after, 4800.0, 15.0)) << "no turn onto the unclassified road";
}
