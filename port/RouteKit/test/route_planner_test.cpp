// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// P5: waypoints in, drawable legs out — over the real Kiawah graph.
//
// What is worth pinning as opposed to what merely passes: the FALLBACK
// BEHAVIOUR. A through route working is the router's test, not this one's;
// what RoutePlanner adds is the decision tree route.py spent forty lines on —
// which failures fall back to per-pair routing, which are reported once, and
// which legs stay straight. Those are the paths a shell will actually hit,
// because a user drops a waypoint in the marsh far more often than they route
// two connected roads.

#include "fv_route_overlay.h"
#include "fv_route_planner.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using fv::GeoPoint;
using fv::RoutePlan;
using fv::RoutePlanner;
using fv::RoutePlanOptions;

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

// The graph is a BUILD ARTIFACT, read by the app and by no other test. It is
// what Pippin's pack carries, so planning over it is planning over what the
// phone will plan over.
std::string KiawahGraph() {
  const std::filesystem::path p =
      std::filesystem::path(TestDataDir()) / "OSM" / "kiawah.fvroad";
  std::error_code ec;
  return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

#define SKIP_WITHOUT_GRAPH()                                        \
  const std::string graph_path = KiawahGraph();                     \
  if (graph_path.empty()) {                                         \
    GTEST_SKIP() << "no TestData/OSM/kiawah.fvroad; run fvgraph build"; \
  }

// The fixture route's first two waypoints: a house on Ruddy Turnstone and a
// point down by the beach club, which is the pair the P1 link check timed.
const GeoPoint kRuddyTurnstone{32.6044007, -80.1083007};
const GeoPoint kBeachClub{32.59297596527702, -80.11767417454368};

// Deep in the Kiawah River, a long way from any road: the out-of-coverage case
// every fallback branch is about.
const GeoPoint kInTheMarsh{32.64, -80.05};

TEST(RoutePlanner, TwoWaypointsOnRoadsGiveAThroughRoute) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);

  RoutePlanOptions opts;
  opts.profile = "foot";
  const RoutePlan walk = planner.Plan({kRuddyTurnstone, kBeachClub}, opts);
  ASSERT_TRUE(walk.found) << walk.status;
  ASSERT_EQ(1u, walk.legs.size());
  EXPECT_GT(walk.legs[0].size(), 2u);
  EXPECT_EQ(0, walk.straight_legs);
  EXPECT_FALSE(walk.is_bicycle);
  EXPECT_NEAR(2160.0, walk.length_m, 200.0);

  opts.profile = "bicycle";
  const RoutePlan ride = planner.Plan({kRuddyTurnstone, kBeachClub}, opts);
  ASSERT_TRUE(ride.found) << ride.status;
  EXPECT_TRUE(ride.is_bicycle);
  // Same island, same two ends, and the ride is quicker than the walk: the
  // profiles reach the router rather than being carried and dropped.
  EXPECT_LT(ride.seconds, walk.seconds);
}

// GD3: the phone plans through RoutePlanner and never sees a routing::Route,
// so the turn list has to ride on the plan — and its distances have to be
// along the line RouteStore::RoutePath() joins, not along some other one.
TEST(RoutePlanner, AThroughRouteCarriesItsTurnList) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  RoutePlanOptions opts;
  opts.profile = "bicycle";

  const RoutePlan plan = planner.Plan({kRuddyTurnstone, kBeachClub}, opts);
  ASSERT_TRUE(plan.found) << plan.status;
  ASSERT_GE(plan.maneuvers.size(), 2u);
  EXPECT_EQ(plan.maneuvers.front().type, fv::nav::ManeuverType::kDepart);
  EXPECT_EQ(plan.maneuvers.back().type, fv::nav::ManeuverType::kArrive);

  // The joined line, duplicates dropped, exactly as RouteStore::RoutePath()
  // builds it. The last maneuver sits at the end of it.
  std::vector<GeoPoint> path;
  for (const auto& leg : plan.legs) {
    for (const GeoPoint& p : leg) {
      if (!path.empty() && path.back().lat == p.lat && path.back().lon == p.lon) continue;
      path.push_back(p);
    }
  }
  ASSERT_GE(path.size(), 2u);
  double joined_m = 0.0;
  for (std::size_t i = 0; i + 1 < path.size(); ++i) {
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    constexpr double kR = 6371008.8;
    const double phi1 = path[i].lat * kDegToRad, phi2 = path[i + 1].lat * kDegToRad;
    const double dphi = (path[i + 1].lat - path[i].lat) * kDegToRad;
    const double dlam = fv::NormalizeLon(path[i + 1].lon - path[i].lon) * kDegToRad;
    const double s1 = std::sin(dphi * 0.5), s2 = std::sin(dlam * 0.5);
    double h = s1 * s1 + std::cos(phi1) * std::cos(phi2) * s2 * s2;
    if (h > 1.0) h = 1.0;
    joined_m += 2.0 * kR * std::asin(std::sqrt(h));
  }
  EXPECT_NEAR(plan.maneuvers.back().distance_m, joined_m, 1.0);
}

// A plan that fell back to straight legs is not one to be guided along.
TEST(RoutePlanner, ThePerPairFallbackCarriesNoTurnList) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  RoutePlanOptions opts;
  opts.profile = "bicycle";
  opts.snap_meters = 200.0;

  const RoutePlan plan = planner.Plan({kRuddyTurnstone, kInTheMarsh, kBeachClub}, opts);
  ASSERT_FALSE(plan.found);
  EXPECT_TRUE(plan.maneuvers.empty());
}

TEST(RoutePlanner, ThreeWaypointsAreOneRouteThroughThem) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  RoutePlanOptions opts;
  opts.profile = "bicycle";

  const GeoPoint third{32.59303776115624, -80.11886651782085};
  const RoutePlan plan =
      planner.Plan({kRuddyTurnstone, kBeachClub, third}, opts);
  ASSERT_TRUE(plan.found) << plan.status;
  // O5d cut back into per-leg pieces: the drawing code wants polylines and a
  // leg is still the unit a user thinks in.
  EXPECT_EQ(2u, plan.legs.size());
  for (const auto& leg : plan.legs) EXPECT_GE(leg.size(), 2u);
}

TEST(RoutePlanner, AWaypointOffTheNetworkFallsBackToThePairs) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  RoutePlanOptions opts;
  opts.profile = "bicycle";
  // The default 2000 m snap is generous; this stop is well outside it, which
  // is what makes RouteVia refuse the whole request.
  opts.snap_meters = 200.0;

  const RoutePlan plan =
      planner.Plan({kRuddyTurnstone, kInTheMarsh, kBeachClub}, opts);
  // Not a through route — and it says so, in both the flag and the sentence.
  EXPECT_FALSE(plan.found);
  EXPECT_EQ(0u, plan.status.find("pairs")) << plan.status;
  // ... but it still drew both legs, straight, rather than nothing at all.
  ASSERT_EQ(2u, plan.legs.size());
  EXPECT_EQ(2, plan.straight_legs);
  EXPECT_EQ(2u, plan.legs[0].size());
  EXPECT_EQ(2u, plan.legs[1].size());
  EXPECT_TRUE(plan.error.ok());
}

TEST(RoutePlanner, AProfileTheRuleFileDoesNotHaveIsReportedOnceAndNotDrawn) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  RoutePlanOptions opts;
  opts.profile = "hovercraft";

  const RoutePlan plan = planner.Plan({kRuddyTurnstone, kBeachClub}, opts);
  EXPECT_FALSE(plan.found);
  // NOT fallen back to: every pair would fail identically, and a screenful of
  // straight lines would read as a routing answer rather than a typo.
  EXPECT_TRUE(plan.legs.empty());
  EXPECT_FALSE(plan.error.ok());
  EXPECT_NE(std::string::npos, plan.status.find("hovercraft")) << plan.status;
  // The bicycle test is on the NAME, so a profile nobody defined is still
  // classified — the line has to be styled before the route is priced.
  EXPECT_FALSE(plan.is_bicycle);
}

TEST(RoutePlanner, FewerThanTwoWaypointsIsAnAnswerAndNotAnEruption) {
  RoutePlanner planner("", "");
  const RoutePlan plan = planner.Plan({GeoPoint{32.6, -80.1}});
  EXPECT_FALSE(plan.found);
  EXPECT_EQ("a route needs two waypoints", plan.status);
  EXPECT_TRUE(plan.legs.empty());
}

TEST(RoutePlanner, AMissingGraphIsReportedByTheFirstPlanThatNeedsIt) {
  // Constructing a planner over a path that is not there is NOT an error: a
  // route overlay that never routes should not pay for a graph, and a shell
  // that configures one at startup should not fail to start over it.
  RoutePlanner planner("/nonexistent/kiawah.fvroad", "");
  EXPECT_FALSE(planner.graph_loaded());

  const RoutePlan plan = planner.Plan({kRuddyTurnstone, kBeachClub});
  EXPECT_FALSE(plan.found);
  EXPECT_FALSE(plan.error.ok());
  EXPECT_TRUE(plan.legs.empty());
}

TEST(RoutePlanner, TheGraphIsLoadedOnceAndKept) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  EXPECT_FALSE(planner.graph_loaded());
  ASSERT_TRUE(planner.EnsureGraph().ok());
  EXPECT_TRUE(planner.graph_loaded());
  // Setting the SAME path again is a no-op, so a shell may call it per frame.
  planner.SetGraphPath(graph_path);
  EXPECT_TRUE(planner.graph_loaded());
  // A different one drops it, because it is a different graph.
  planner.SetGraphPath("/nonexistent/other.fvroad");
  EXPECT_FALSE(planner.graph_loaded());
}

TEST(RoutePlanner, TheStatusLineIsTheOneRoutePyWrites) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  RoutePlanOptions opts;
  opts.profile = "bicycle";
  const RoutePlan plan = planner.Plan({kRuddyTurnstone, kBeachClub}, opts);
  ASSERT_TRUE(plan.found) << plan.status;
  // "<what>: <km> km, <min> min" — the profile names itself, so a user moving
  // between the two shells reads the same sentence.
  EXPECT_EQ(0u, plan.status.find("bicycle: ")) << plan.status;
  EXPECT_NE(std::string::npos, plan.status.find(" km, ")) << plan.status;
  EXPECT_NE(std::string::npos, plan.status.find(" min")) << plan.status;
  // A rule file that loaded cleanly adds no warning.
  EXPECT_EQ(std::string::npos, plan.status.find("[rules:")) << plan.status;
  EXPECT_TRUE(planner.rules_error().empty()) << planner.rules_error();
}

TEST(RoutePlanner, WithNoProfileTheBooleanDecidesTheBicycleTest) {
  // The classification is route.py's: a profile NAME containing "bike" or
  // "cycle" is a bicycle request, and with no profile named the old boolean
  // decides. The rule file owns the profiles, so a user's "bicycle-winter"
  // has to classify too.
  EXPECT_FALSE(fv::IsBicycleRequest("", false));
  EXPECT_TRUE(fv::IsBicycleRequest("", true));
  EXPECT_TRUE(fv::IsBicycleRequest("bicycle", false));
  EXPECT_TRUE(fv::IsBicycleRequest("bicycle-winter", false));
  EXPECT_TRUE(fv::IsBicycleRequest("MountainBike", false));
  // A profile OVERRIDES the boolean, in the router and here.
  EXPECT_FALSE(fv::IsBicycleRequest("foot", true));
}

TEST(RoutePlanner, TheRuleFileNamesItsProfiles) {
  RoutePlanner planner("", FV_ROUTE_RULES_FILE);
  const std::vector<std::string> names = planner.profile_names();
  EXPECT_NE(names.end(), std::find(names.begin(), names.end(), "foot"));
  EXPECT_NE(names.end(), std::find(names.begin(), names.end(), "bicycle"));
}

TEST(RoutePlanner, ABadRuleFileWarnsOnAnAnswerRatherThanReplacingOne) {
  SKIP_WITHOUT_GRAPH();
  // O5c's rule: a rule file that will not parse leaves the builtin weights in
  // force and says why. The route still comes back — saying nothing would be
  // the bug, since the user edited a file and would otherwise see a route that
  // ignored the edit.
  const std::string bad =
      (std::filesystem::temp_directory_path() / "fvroutekit_bad_rules.json")
          .string();
  {
    std::ofstream out(bad);
    out << "{ this is not json";
  }
  RoutePlanner planner(graph_path, bad);
  EXPECT_FALSE(planner.rules_error().empty());

  const RoutePlan plan = planner.Plan({kRuddyTurnstone, kBeachClub});
  EXPECT_TRUE(plan.found) << plan.status;
  EXPECT_NE(std::string::npos, plan.status.find("[rules:")) << plan.status;
  std::remove(bad.c_str());
}

// The beach setting reaches the router, and the plan reports where the
// sand is. Boardwalk 29 to 41 is the pair the router's own beach test rides.
const GeoPoint kBoardwalk29{32.602374, -80.0838337};
const GeoPoint kBoardwalk41{32.6100451, -80.045189};

TEST(RoutePlanner, WheneverPossibleRidesTheBeachAndReportsTheStretch) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  RoutePlanOptions opts;
  opts.profile = "bicycle";

  const RoutePlan road = planner.Plan({kBoardwalk29, kBoardwalk41}, opts);
  ASSERT_TRUE(road.found) << road.status;
  EXPECT_TRUE(road.beach.empty());

  opts.beach = fv::routing::BeachUse::kToSaveTime;
  const RoutePlan time = planner.Plan({kBoardwalk29, kBoardwalk41}, opts);
  ASSERT_TRUE(time.found) << time.status;
  EXPECT_TRUE(time.beach.empty());  // the sand is slower by bike here
  EXPECT_DOUBLE_EQ(road.length_m, time.length_m);

  opts.beach = fv::routing::BeachUse::kWheneverPossible;
  const RoutePlan sand = planner.Plan({kBoardwalk29, kBoardwalk41}, opts);
  ASSERT_TRUE(sand.found) << sand.status;
  ASSERT_EQ(1u, sand.beach.size());
  const fv::RouteBeachStretch& b = sand.beach[0];
  EXPECT_NEAR(3690.0, b.length_m, 50.0);
  EXPECT_GT(b.start_m, 0.0);  // an access arc comes first
  EXPECT_LT(b.start_m + b.length_m, sand.length_m);
  EXPECT_GT(b.enter_s, 0.0);
  EXPECT_LT(b.exit_s, sand.seconds);
  // 10 km/h on the sand, from the shipped class_kph.
  EXPECT_NEAR(b.length_m / (10.0 / 3.6), b.exit_s - b.enter_s, 1.0);

  size_t points = 0;
  for (const auto& leg : sand.legs) points += leg.size();
  points -= sand.legs.size() - 1;  // shared joints
  EXPECT_LT(b.geometry_begin, b.geometry_end);
  EXPECT_LT(b.geometry_end, points);
}

TEST(RoutePlanner, TheDocumentsBeachSettingReachesThePlan) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  fv::RouteOverlay overlay("beach");
  overlay.SetPlanner(&planner);
  ASSERT_TRUE(overlay.FileOpen(FV_ROUTE_BEACH_FIXTURE_FILE).ok());
  ASSERT_TRUE(overlay.FollowRoads());
  EXPECT_EQ(1u, overlay.plan().beach.size());

  // A beach setting named on the call wins over the document's.
  RoutePlanOptions never;
  never.beach = fv::routing::BeachUse::kNever;
  ASSERT_TRUE(overlay.FollowRoads(never));
  EXPECT_TRUE(overlay.plan().beach.empty());
}

// The tide gate. Synthetic table: low 0.0 m at 0, high 2.0 m at 6 h,
// low 0.4 m at 12 h, high 1.6 m at 18 h. The Boardwalk 29 -> 41 beach stretch
// is entered about 52 s after departure and left about 1382 s after it.
fv::nav::TideTable SyntheticTide() {
  fv::nav::TideTable t;
  const fv::Status s = t.Parse(
      "{\"units\": \"m\", \"datum\": \"MLLW\", \"valid_from\": 0, \"valid_until\": 64800,"
      " \"station\": {\"id\": \"1\", \"name\": \"Test\"}, \"extremes\": ["
      "[0, 0.0, \"L\"], [21600, 2.0, \"H\"], [43200, 0.4, \"L\"], [64800, 1.6, \"H\"]]}");
  EXPECT_TRUE(s.ok()) << s.message;
  return t;
}

RoutePlan PlanAtTide(const RoutePlanner& planner, fv::routing::BeachUse beach,
                     const fv::nav::TideTable* table, double depart_s,
                     bool keep_unknown = false) {
  RoutePlanOptions opts;
  opts.profile = "bicycle";
  opts.beach = beach;
  opts.tide.enabled = true;
  opts.tide.keep_unknown = keep_unknown;
  opts.tide.table = table;
  opts.tide.depart_s = depart_s;
  return planner.Plan({kBoardwalk29, kBoardwalk41}, opts);
}

TEST(RoutePlanner, TheTideGateKeepsTheBeachAtLowWater) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  const fv::nav::TideTable tide = SyntheticTide();
  const RoutePlan plan =
      PlanAtTide(planner, fv::routing::BeachUse::kWheneverPossible, &tide, 0);
  ASSERT_TRUE(plan.found) << plan.status;
  ASSERT_EQ(1u, plan.beach.size());
  ASSERT_EQ(1u, plan.beach_verdicts.size());
  EXPECT_EQ(fv::nav::BeachVerdict::kGood, plan.beach_verdicts[0].verdict);
  EXPECT_DOUBLE_EQ(plan.beach[0].enter_s, plan.beach_verdicts[0].enter_at_s);
  EXPECT_EQ(fv::BeachDropped::kNone, plan.beach_dropped);
}

TEST(RoutePlanner, TheTideGateDropsTheBeachAtHighWater) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  const fv::nav::TideTable tide = SyntheticTide();
  RoutePlanOptions never;
  never.profile = "bicycle";
  const RoutePlan road = planner.Plan({kBoardwalk29, kBoardwalk41}, never);

  const RoutePlan plan =
      PlanAtTide(planner, fv::routing::BeachUse::kWheneverPossible, &tide, 21600);
  ASSERT_TRUE(plan.found) << plan.status;
  EXPECT_TRUE(plan.beach.empty());
  EXPECT_TRUE(plan.beach_verdicts.empty());
  EXPECT_EQ(fv::BeachDropped::kHigh, plan.beach_dropped);
  EXPECT_EQ(fv::nav::BeachVerdict::kPoor, plan.beach_dropped_verdict.verdict);
  EXPECT_GT(plan.beach_dropped_verdict.enter_height_m, 1.9);
  EXPECT_GT(plan.beach_dropped_verdict.passable_from_s, 21600);
  EXPECT_DOUBLE_EQ(road.length_m, plan.length_m);  // the road route, unchanged
}

TEST(RoutePlanner, TheTideGateDropsTheBeachJustBeforeTheFlood) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  const fv::nav::TideTable tide = SyntheticTide();
  // Below 0.5 m on arrival (0.5 m is reached at 7200 s), above it before the
  // stretch and its ten-minute margin end.
  const RoutePlan plan =
      PlanAtTide(planner, fv::routing::BeachUse::kWheneverPossible, &tide, 6400);
  ASSERT_TRUE(plan.found) << plan.status;
  EXPECT_TRUE(plan.beach.empty());
  EXPECT_EQ(fv::BeachDropped::kRising, plan.beach_dropped);
  EXPECT_LE(plan.beach_dropped_verdict.enter_height_m, 0.5);
  EXPECT_NEAR(7200.0, plan.beach_dropped_verdict.covered_at_s, 1e-6);
}

TEST(RoutePlanner, TheTideGateDropsTheBeachWithoutATable) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  const RoutePlan none =
      PlanAtTide(planner, fv::routing::BeachUse::kToSaveTime, nullptr, 0);
  ASSERT_TRUE(none.found) << none.status;
  // To Save Time takes no beach on this pair, so there is nothing to gate.
  EXPECT_EQ(fv::BeachDropped::kNone, none.beach_dropped);

  const RoutePlan prefer =
      PlanAtTide(planner, fv::routing::BeachUse::kWheneverPossible, nullptr, 0);
  EXPECT_TRUE(prefer.beach.empty());
  EXPECT_EQ(fv::BeachDropped::kNoTable, prefer.beach_dropped);

  const fv::nav::TideTable tide = SyntheticTide();
  const RoutePlan ended =
      PlanAtTide(planner, fv::routing::BeachUse::kWheneverPossible, &tide, 64000);
  EXPECT_EQ(fv::BeachDropped::kNoTable, ended.beach_dropped);
}

TEST(RoutePlanner, TheTideGateKeepsAnUnknownStretchByDefault) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  EXPECT_TRUE(fv::BeachTideGate{}.keep_unknown);

  const RoutePlan none = PlanAtTide(planner, fv::routing::BeachUse::kWheneverPossible,
                                    nullptr, 0, /*keep_unknown=*/true);
  ASSERT_TRUE(none.found) << none.status;
  ASSERT_EQ(1u, none.beach.size());
  ASSERT_EQ(1u, none.beach_verdicts.size());
  EXPECT_EQ(fv::nav::BeachVerdict::kUnknown, none.beach_verdicts[0].verdict);
  EXPECT_EQ(fv::BeachDropped::kNone, none.beach_dropped);

  // Past the table's end is the same answer; a poor stretch is still dropped.
  const fv::nav::TideTable tide = SyntheticTide();
  const RoutePlan ended = PlanAtTide(planner, fv::routing::BeachUse::kWheneverPossible,
                                     &tide, 64000, /*keep_unknown=*/true);
  EXPECT_EQ(1u, ended.beach.size());
  EXPECT_EQ(fv::BeachDropped::kNone, ended.beach_dropped);
  const RoutePlan high = PlanAtTide(planner, fv::routing::BeachUse::kWheneverPossible,
                                    &tide, 21600, /*keep_unknown=*/true);
  EXPECT_EQ(fv::BeachDropped::kHigh, high.beach_dropped);
}

TEST(RoutePlanner, NeverIsNotGated) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  const fv::nav::TideTable tide = SyntheticTide();
  const RoutePlan plan = PlanAtTide(planner, fv::routing::BeachUse::kNever, &tide, 21600);
  ASSERT_TRUE(plan.found) << plan.status;
  EXPECT_TRUE(plan.beach.empty());
  EXPECT_TRUE(plan.beach_verdicts.empty());
  EXPECT_EQ(fv::BeachDropped::kNone, plan.beach_dropped);
}

TEST(RoutePlanner, TheTideGateOnTheKiawahTable) {
  SKIP_WITHOUT_GRAPH();
  const std::string path = std::string(TestDataDir()) + "/tides/8667062.json";
  if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not fetched";
  fv::nav::TideTable tide;
  ASSERT_TRUE(tide.Load(path).ok());
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);

  // 2026-06-01 onward: the first low and the high after it.
  const auto ex = tide.Extremes(1780272000, 1780272000 + 86400);
  ASSERT_GE(ex.size(), 3u);
  const fv::nav::TideExtreme& low = ex[0].high ? ex[1] : ex[0];
  const fv::nav::TideExtreme& high = ex[0].high ? ex[2] : ex[1];

  const RoutePlan at_low = PlanAtTide(planner, fv::routing::BeachUse::kWheneverPossible,
                                      &tide, low.time_s - 700);
  EXPECT_EQ(1u, at_low.beach.size());
  EXPECT_EQ(fv::BeachDropped::kNone, at_low.beach_dropped);

  const RoutePlan at_high = PlanAtTide(planner, fv::routing::BeachUse::kWheneverPossible,
                                       &tide, high.time_s - 700);
  EXPECT_TRUE(at_high.beach.empty());
  EXPECT_EQ(fv::BeachDropped::kHigh, at_high.beach_dropped);
}

}  // namespace
