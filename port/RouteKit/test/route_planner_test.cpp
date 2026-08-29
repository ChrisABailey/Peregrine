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

#include "fv_route_planner.h"

#include <gtest/gtest.h>

#include <algorithm>
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

}  // namespace
