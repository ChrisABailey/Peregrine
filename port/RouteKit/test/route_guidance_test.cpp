// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// GD2 over the real thing: testdata/kiawah_cycle.gpx replayed against a route
// planned on the Kiawah graph, which is the test the guidance plan named.
//
// The rules themselves are pinned on synthetic fixes in fvkit's own test. What
// only a real ride can show is whether the machine SURVIVES one: a receiver
// that wanders, a rider who does not follow the planned line exactly, and 1705
// fixes of it. The assertions are therefore about shape and sanity rather than
// about an exact event at an exact second.

#include "fvkit/nav/guidance.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "fv_route_maneuvers.h"
#include "fv_route_planner.h"
#include "fvkit/nav/gpx.h"

namespace {

using fv::GeoPoint;
using fv::ManeuversOf;
using fv::PositionFix;
using fv::RoutePlanner;
using fv::nav::Guidance;
using fv::nav::GuidanceEvent;
using fv::nav::GuidanceEventType;
using fv::nav::GuidanceRing;
using fv::nav::Maneuver;

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

std::string FileOrEmpty(const std::string& relative) {
  const std::filesystem::path p = std::filesystem::path(TestDataDir()) / relative;
  std::error_code ec;
  return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

#define SKIP_WITHOUT_RIDE()                                                  \
  const std::string graph_path = FileOrEmpty("OSM/kiawah.fvroad");            \
  const std::string gpx_path = FileOrEmpty("kiawah_cycle.gpx");               \
  if (graph_path.empty() || gpx_path.empty()) {                               \
    GTEST_SKIP() << "need TestData/OSM/kiawah.fvroad and kiawah_cycle.gpx";   \
  }

// The ride, and a route planned between the ends of it. Routing to where the
// rider actually finished is what makes the two comparable at all.
struct Ride {
  std::vector<PositionFix> fixes;
  std::vector<GeoPoint> geometry;
  std::vector<Maneuver> maneuvers;
};

bool LoadRide(const std::string& graph_path, const std::string& gpx_path, Ride* out) {
  fv::GpxDocument document;
  if (!fv::ReadGpxFile(gpx_path, &document).ok()) return false;
  out->fixes = fv::FlattenGpxFixes(document);
  if (out->fixes.size() < 100) return false;

  GeoPoint start;
  start.lat = out->fixes.front().lat;
  start.lon = out->fixes.front().lon;
  GeoPoint finish;
  finish.lat = out->fixes.back().lat;
  finish.lon = out->fixes.back().lon;

  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  if (!planner.EnsureGraph().ok()) return false;
  fv::routing::Router router(*planner.graph());
  fv::routing::RouteOptions options;
  options.cycle_only = true;
  fv::routing::Route route;
  if (!router.RouteVia({start, finish}, options, &route).ok() || !route.found) return false;

  out->geometry = route.geometry;
  out->maneuvers = ManeuversOf(route);
  return out->maneuvers.size() >= 2;
}

// Everything the ride said, in order.
std::vector<GuidanceEvent> Replay(Guidance* guidance, const Ride& ride) {
  std::vector<GuidanceEvent> all;
  for (const PositionFix& fix : ride.fixes) {
    const std::vector<GuidanceEvent> e = guidance->OnFix(fix);
    all.insert(all.end(), e.begin(), e.end());
  }
  return all;
}

int CountOf(const std::vector<GuidanceEvent>& all, GuidanceEventType type) {
  int n = 0;
  for (const GuidanceEvent& e : all)
    if (e.type == type) ++n;
  return n;
}

}  // namespace

// The machine runs a whole ride without ever saying the same thing twice about
// the same corner, and reaches the end.
TEST(RouteGuidance, TheRealRideIsGuidedEndToEnd) {
  SKIP_WITHOUT_RIDE();
  Ride ride;
  ASSERT_TRUE(LoadRide(graph_path, gpx_path, &ride));

  Guidance guidance;
  guidance.SetRoute(ride.geometry, ride.maneuvers);
  const std::vector<GuidanceEvent> all = Replay(&guidance, ride);

  // No maneuver hears the same ring twice, over 1705 fixes.
  std::vector<int> heads_up(ride.maneuvers.size(), 0);
  std::vector<int> act_now(ride.maneuvers.size(), 0);
  for (const GuidanceEvent& e : all) {
    if (e.type != GuidanceEventType::kApproach) continue;
    ASSERT_LT(e.maneuver_index, ride.maneuvers.size());
    if (e.ring == GuidanceRing::kHeadsUp) ++heads_up[e.maneuver_index];
    if (e.ring == GuidanceRing::kActNow) ++act_now[e.maneuver_index];
  }
  for (std::size_t i = 0; i < ride.maneuvers.size(); ++i) {
    EXPECT_LE(heads_up[i], 1) << "maneuver " << i;
    EXPECT_LE(act_now[i], 1) << "maneuver " << i;
  }

  EXPECT_GT(CountOf(all, GuidanceEventType::kApproach), 0);
  EXPECT_EQ(CountOf(all, GuidanceEventType::kArrived), 1);
  EXPECT_TRUE(guidance.state().valid);
}

// This rider stayed on the planned line the whole way, so the tolerances have
// to be provoked: a stretch in the middle of the real ride is moved 80 m
// sideways and put back. One departure, one return, and the receiver's own
// wander over the other 1600 fixes must not add any of its own.
TEST(RouteGuidance, ADetourIsOneDepartureAndOneReturn) {
  SKIP_WITHOUT_RIDE();
  Ride ride;
  ASSERT_TRUE(LoadRide(graph_path, gpx_path, &ride));

  // 80 m north, well beyond the 25 m tolerance, for 200 fixes.
  const std::size_t from = ride.fixes.size() / 3;
  const std::size_t to = from + 200;
  ASSERT_LT(to, ride.fixes.size());
  Ride detoured = ride;
  for (std::size_t i = from; i < to; ++i)
    detoured.fixes[i].lat += 80.0 / 111320.0;

  Guidance guidance;
  guidance.SetRoute(detoured.geometry, detoured.maneuvers);
  const std::vector<GuidanceEvent> all = Replay(&guidance, detoured);

  EXPECT_EQ(CountOf(all, GuidanceEventType::kOffRoute), 1);
  EXPECT_EQ(CountOf(all, GuidanceEventType::kRejoined), 1);

  // Off route comes first, and nothing is announced between the two.
  bool silent = false;
  int announced_while_silent = 0;
  for (const GuidanceEvent& e : all) {
    if (e.type == GuidanceEventType::kOffRoute) silent = true;
    else if (e.type == GuidanceEventType::kRejoined) silent = false;
    else if (silent) ++announced_while_silent;
  }
  EXPECT_EQ(announced_while_silent, 0);
  EXPECT_FALSE(silent) << "the ride ended off route";
}

// The undetoured ride, for contrast: a receiver wandering along a line it is
// genuinely following must never trip the tolerance at all.
TEST(RouteGuidance, TheRiderWhoFollowedTheRouteIsNeverSilenced) {
  SKIP_WITHOUT_RIDE();
  Ride ride;
  ASSERT_TRUE(LoadRide(graph_path, gpx_path, &ride));

  Guidance guidance;
  guidance.SetRoute(ride.geometry, ride.maneuvers);
  const std::vector<GuidanceEvent> all = Replay(&guidance, ride);

  EXPECT_EQ(CountOf(all, GuidanceEventType::kOffRoute), 0);
  EXPECT_EQ(CountOf(all, GuidanceEventType::kRejoined), 0);
}

// Every countdown a banner would draw is a real distance to a real corner.
TEST(RouteGuidance, TheCountdownIsAlwaysAheadOfTheRider) {
  SKIP_WITHOUT_RIDE();
  Ride ride;
  ASSERT_TRUE(LoadRide(graph_path, gpx_path, &ride));

  Guidance guidance;
  guidance.SetRoute(ride.geometry, ride.maneuvers);
  for (const PositionFix& fix : ride.fixes) {
    guidance.OnFix(fix);
    const auto& state = guidance.state();
    if (!state.valid || !state.has_next) continue;
    EXPECT_GE(state.distance_to_next_m, 0.0);
    EXPECT_LE(state.distance_to_next_m, state.remaining_m + 1.0);
    EXPECT_LT(state.next_index, ride.maneuvers.size());
  }
}
