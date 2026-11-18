// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// RoutePlanner::PlanDetour on the 2026-11-08 Atlanta ride
/// (port/pippin-reroute-plan.md section 2). The off-route fixes and headings
/// are taken from ride-2026-11-08-1210.gpx; the Atlanta tests skip without
/// testdata/OSM/atlanta.fvroad.

#include "fv_route_planner.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <filesystem>
#include <string>

namespace {

using fv::DetourPick;
using fv::DetourPlan;
using fv::DetourRequest;
using fv::GeoPoint;
using fv::RoutePlan;
using fv::RoutePlanner;
using fv::RoutePlanOptions;

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env ? env : "testdata";
}

const GeoPoint kStart{33.78421411913466, -84.44998056736293};
const GeoPoint kEnd{33.822726949999996, -84.4314115};

/// The section 2 route: bicycle, avoid hills, City Rules.
struct AtlantaRide {
  explicit AtlantaRide(const std::string& graph)
      : planner(graph, FV_ROUTE_CITY_RULES_FILE) {
    options.profile = "bicycle";
    options.avoid_hills = true;
    route = planner.Plan(waypoints, options);
  }
  RoutePlanner planner;
  RoutePlanOptions options;
  std::vector<GeoPoint> waypoints{kStart, kEnd};
  RoutePlan route;
};

#define ATLANTA_OR_SKIP(ride)                                                      \
  const std::filesystem::path graph_path =                                         \
      std::filesystem::path(TestDataDir()) / "OSM" / "atlanta.fvroad";             \
  if (!std::filesystem::exists(graph_path))                                        \
    GTEST_SKIP() << "no testdata/OSM/atlanta.fvroad";                              \
  AtlantaRide ride(graph_path.string());                                           \
  ASSERT_TRUE(ride.route.found) << ride.route.status

DetourRequest Request(GeoPoint at, double heading, double last_along_m) {
  DetourRequest r;
  r.position = at;
  r.heading_deg = heading;
  r.last_along_m = last_along_m;
  return r;
}

/// The point nearest `along_m` metres along a plan's legs joined, by vertex.
GeoPoint VertexAlong(const RoutePlan& plan, double along_m) {
  const double k = 6371008.8 * 3.14159265358979323846 / 180.0;
  double at = 0.0;
  const GeoPoint* prev = nullptr;
  for (const auto& leg : plan.legs) {
    for (const GeoPoint& p : leg) {
      if (prev) {
        const double dx = (p.lon - prev->lon) * k * std::cos(p.lat * 3.14159265358979323846 / 180.0);
        const double dy = (p.lat - prev->lat) * k;
        at += std::hypot(dx, dy);
      }
      prev = &p;
      if (at >= along_m) return p;
    }
  }
  return *prev;
}

}  // namespace

// Straight on past the right turn onto Pine St, 125 m up Marietta Rd: on
// along Marietta and Laurel Ave beats going back to Pine St.
TEST(RouteDetour, AtlantaPineStreetReroutesAhead) {
  ATLANTA_OR_SKIP(ride);
  const DetourPlan d = ride.planner.PlanDetour(
      Request({33.7970794, -84.4399671}, 333.0, 2347.0), ride.waypoints, ride.route,
      ride.options);
  // Measured: reroute 6222 m, turn-round 6776 m (125 m back), 6647 m left.
  ASSERT_TRUE(d.reroute.found) << d.reroute.status;
  ASSERT_TRUE(d.turn_around.found) << d.turn_around.status;
  EXPECT_EQ(d.pick, DetourPick::kReroute);
  EXPECT_FALSE(d.reroute_starts_with_u_turn);
  EXPECT_NEAR(d.remaining_m, ride.route.length_m - 2347.0, 5.0);
  EXPECT_LT(d.extra_m, 0.0);
  EXPECT_NEAR(d.back_m, 125.0, 20.0);
  ASSERT_FALSE(d.reroute.roads.empty());
  EXPECT_EQ(d.reroute.roads.front().name, "Marietta Road Northwest");
  EXPECT_FALSE(d.reroute.maneuvers.empty());
  ASSERT_GE(d.turn_around.roads.size(), 2u);
  EXPECT_EQ(d.turn_around.roads[1].name, "Pine Street Northwest");
}

// Across the line at 3.42 km on the unnamed path and 90 m beyond it: the best
// way on starts by turning round, so turning round is the pick.
TEST(RouteDetour, AtlantaRejoinCrossingTurnsAround) {
  ATLANTA_OR_SKIP(ride);
  const DetourPlan d = ride.planner.PlanDetour(
      Request({33.8013988, -84.4428205}, 320.0, 3420.0), ride.waypoints, ride.route,
      ride.options);
  // Measured: both 5615 m, 177 m back, 5574 m left.
  ASSERT_TRUE(d.reroute.found) << d.reroute.status;
  ASSERT_TRUE(d.turn_around.found) << d.turn_around.status;
  EXPECT_TRUE(d.reroute_starts_with_u_turn);
  EXPECT_EQ(d.pick, DetourPick::kTurnAround);
  EXPECT_NEAR(d.turn_around.length_m, d.reroute.length_m, 1.0);
  EXPECT_GT(d.back_m, 90.0);
  EXPECT_LT(d.back_m, 250.0);
}

// Straight on past the left onto the Whetstone Creek PATH, 110 m beyond it:
// Westside Blvd and McArthur Blvd rejoin the path, 120 m shorter than going
// back, so the reroute is the pick.
TEST(RouteDetour, AtlantaWhetstoneReroutesAhead) {
  ATLANTA_OR_SKIP(ride);
  const DetourPlan d = ride.planner.PlanDetour(
      Request({33.8100760, -84.4539332}, 325.0, 5210.0), ride.waypoints, ride.route,
      ride.options);
  // Measured: reroute 3842 m, turn-round 3962 m (143 m back), 3784 m left.
  ASSERT_TRUE(d.reroute.found) << d.reroute.status;
  ASSERT_TRUE(d.turn_around.found) << d.turn_around.status;
  EXPECT_EQ(d.pick, DetourPick::kReroute);
  EXPECT_NEAR(d.extra_m, 58.0, 30.0);
  EXPECT_LT(d.reroute.length_m, d.turn_around.length_m);
  EXPECT_NEAR(d.back_m, 143.0, 30.0);
  ASSERT_GE(d.reroute.roads.size(), 2u);
  EXPECT_EQ(d.reroute.roads[1].name, "Westside Boulevard Northwest");

  // Turning round wins once it is no longer longer than the margin allows.
  DetourRequest r = Request({33.8100760, -84.4539332}, 325.0, 5210.0);
  r.turn_around_margin_m = -200.0;
  EXPECT_EQ(ride.planner.PlanDetour(r, ride.waypoints, ride.route, ride.options).pick,
            DetourPick::kTurnAround);
}

// A waypoint at or before the last on-route fix is dropped; the destination
// is always kept.
TEST(RouteDetour, DropsTheWaypointsAlreadyPassed) {
  ATLANTA_OR_SKIP(ride);
  const GeoPoint via = VertexAlong(ride.route, 4000.0);
  const std::vector<GeoPoint> waypoints{kStart, via, kEnd};
  const RoutePlan route = ride.planner.Plan(waypoints, ride.options);
  ASSERT_TRUE(route.found) << route.status;
  ASSERT_EQ(route.legs.size(), 2u);

  const DetourPlan before = ride.planner.PlanDetour(
      Request({33.7970794, -84.4399671}, 333.0, 2347.0), waypoints, route, ride.options);
  ASSERT_EQ(before.stops_ahead.size(), 2u);
  EXPECT_DOUBLE_EQ(before.stops_ahead[0].lat, via.lat);
  EXPECT_DOUBLE_EQ(before.stops_ahead[1].lat, kEnd.lat);
  EXPECT_EQ(before.reroute.legs.size(), 2u);

  const DetourPlan after = ride.planner.PlanDetour(
      Request({33.8100760, -84.4539332}, 325.0, 5210.0), waypoints, route, ride.options);
  ASSERT_EQ(after.stops_ahead.size(), 1u);
  EXPECT_DOUBLE_EQ(after.stops_ahead[0].lat, kEnd.lat);
}

// Without a heading nothing is "back": no turn-round plan, and the reroute
// carries no penalty.
TEST(RouteDetour, NoHeadingOnlyReroutes) {
  ATLANTA_OR_SKIP(ride);
  const DetourPlan d = ride.planner.PlanDetour(
      Request({33.8013988, -84.4428205}, std::numeric_limits<double>::quiet_NaN(), 3420.0),
      ride.waypoints, ride.route, ride.options);
  ASSERT_TRUE(d.reroute.found) << d.reroute.status;
  EXPECT_FALSE(d.turn_around.found);
  EXPECT_FALSE(d.reroute_starts_with_u_turn);
  EXPECT_EQ(d.pick, DetourPick::kReroute);
  EXPECT_DOUBLE_EQ(d.back_m, 0.0);
}

// No route to leave is no detour, and nothing is loaded to say so.
TEST(RouteDetour, NoRouteIsNoDetour) {
  RoutePlanner planner("", "");
  const DetourPlan d =
      planner.PlanDetour(Request(kStart, 0.0, 0.0), {kStart, kEnd}, RoutePlan{}, {});
  EXPECT_EQ(d.pick, DetourPick::kNone);
  EXPECT_FALSE(d.reroute.found);
  EXPECT_FALSE(d.turn_around.found);
  EXPECT_FALSE(planner.graph_loaded());
}
