// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The route document's life in the app, tested on the mac.
//
// RouteKit's own tests cover the overlay: a document round-trips byte for
// byte, moving a waypoint drops the road it was computed from, a pick agrees
// with the picture. None of that is about an app. This file tests the
// sentence a simulator run would otherwise carry alone:
//
//     kill and relaunch the app; the route is still there
//
// where "there" means with its roads, which is what does not happen if a
// shell only calls `FileOpen`. That asymmetry — the document holds the
// waypoints, the graph holds the geometry — is why `RouteStore` exists.

#include "PPRouteStore.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fv_route_edit.h"
#include "fvkit/canvas/cpu_canvas.h"

namespace {

using fv::GeoPoint;
using fv::RouteWaypoint;
using pippin::RouteSnapshot;
using pippin::RouteStore;

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

// The graph the PACK carries, which is what a phone plans over. A build
// artifact, so a tree that has never run `fvgraph build` skips rather than
// fails — the same bargain P5's tests strike.
std::string KiawahGraph() {
  const std::filesystem::path p =
      std::filesystem::path(TestDataDir()) / "OSM" / "kiawah.fvroad";
  std::error_code ec;
  return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

#define SKIP_WITHOUT_GRAPH()                                            \
  const std::string graph = KiawahGraph();                              \
  if (graph.empty()) {                                                  \
    GTEST_SKIP() << "no TestData/OSM/kiawah.fvroad; run fvgraph build"; \
  }

std::string RulesFile() { return FV_ROUTE_RULES_FILE; }

// A `Documents/` of our own, thrown away with the test. Named after the test
// so a failure leaves something identifiable behind when it is not.
class TempDocument {
 public:
  explicit TempDocument(const std::string& name) {
    dir_ = std::filesystem::temp_directory_path() /
           ("pippin_route_" + name + "_" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
    std::filesystem::create_directories(dir_, ec);
  }
  ~TempDocument() {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::string path() const { return (dir_ / "current.fvrte").string(); }
  bool exists() const {
    std::error_code ec;
    return std::filesystem::exists(path(), ec);
  }

 private:
  std::filesystem::path dir_;
};

// The pair the P1 link check timed: a house on Ruddy Turnstone and the beach
// club, 2.16 km apart and connected by real road on this graph.
const GeoPoint kRuddyTurnstone{32.6044007, -80.1083007};
const GeoPoint kBeachClub{32.59297596527702, -80.11767417454368};

std::vector<RouteWaypoint> TheUsualPair() {
  return {RouteWaypoint{"Start", kRuddyTurnstone},
          RouteWaypoint{"End", kBeachClub}};
}

// --- P21's fixtures: a frame, and where a waypoint landed on it -------------
//
// The drag is answered out of what was DRAWN — `RouteOverlay::HitTestPoint`'s
// own rule — so every one of these tests has to put a frame on a canvas first.
// That is not ceremony: an overlay that has never drawn correctly grabs
// nothing, and one of the tests below is exactly that case.

fv::MapProjection KiawahProj() {
  fv::MapProjection p;
  p.SetSurfaceSize(800, 600);
  p.SetCenter({32.5987, -80.1130});
  p.SetScale(50000.0);
  return p;
}

// Labels off throughout: they are on by default for a route and a label costs
// a TTF on the filesystem. None of these tests is about text.
void DrawOnce(RouteStore& store, const fv::MapProjection& proj) {
  store.overlay()->SetShowLabels(false);
  fv::CpuCanvas canvas(proj.SurfaceSize().width, proj.SurfaceSize().height);
  ASSERT_TRUE(store.overlay()->OnDraw(proj, canvas).ok());
}

fv::PixelPoint PixelOf(const fv::MapProjection& proj, const GeoPoint& g) {
  double x = 0, y = 0;
  EXPECT_TRUE(proj.GeoToSurface(g, &x, &y).ok());
  return fv::PixelPoint{static_cast<int>(x + 0.5), static_cast<int>(y + 0.5)};
}

GeoPoint PositionOfWaypoint(const RouteSnapshot& snap,
                            const std::string& label) {
  for (const RouteWaypoint& w : snap.waypoints) {
    if (w.label == label) return w.position;
  }
  return GeoPoint{0, 0};
}

// ---------------------------------------------------------------------------
// A launch with nothing saved
// ---------------------------------------------------------------------------

TEST(RouteStore, AFirstLaunchWithNoDocumentIsNotAnError) {
  TempDocument doc("first_launch");
  RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());

  const fv::Status s = store.LoadAtLaunch();
  EXPECT_TRUE(s.ok()) << s.message;

  const RouteSnapshot snap = store.Snapshot();
  EXPECT_FALSE(snap.exists);
  EXPECT_TRUE(snap.waypoints.empty());
  EXPECT_FALSE(snap.calculated);
  // And nothing was created just by looking.
  EXPECT_FALSE(doc.exists());
}

TEST(RouteStore, ADocumentThatWillNotParseIsReportedAndTheAppStillComesUp) {
  TempDocument doc("bad_document");
  {
    std::ofstream out(doc.path());
    out << "{ this is not a route";
  }
  RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());

  const fv::Status s = store.LoadAtLaunch();
  EXPECT_FALSE(s.ok());
  EXPECT_FALSE(s.message.empty());

  // Reported, not fatal: there is a store, it has no route in it, and the
  // shell above can put the message on screen and carry on.
  const RouteSnapshot snap = store.Snapshot();
  EXPECT_FALSE(snap.exists);
  EXPECT_TRUE(snap.waypoints.empty());
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

TEST(RouteStore, SettingWaypointsWritesTheDocument) {
  TempDocument doc("writes");
  RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());

  EXPECT_FALSE(doc.exists());
  store.SetWaypoints(TheUsualPair(), "bicycle");
  EXPECT_TRUE(doc.exists());
  EXPECT_TRUE(store.last_write_error().empty()) << store.last_write_error();
}

TEST(RouteStore, AHalfBuiltRouteIsPersistedAndIsNotAnError) {
  TempDocument doc("half_built");
  {
    RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());
    const RouteSnapshot snap = store.SetWaypoints(
        {RouteWaypoint{"Start", kRuddyTurnstone}}, "bicycle");

    // One waypoint is what a route being built looks like: it exists, it is
    // not calculated, and the planner says why in words a sheet can show.
    EXPECT_TRUE(snap.exists);
    EXPECT_EQ(1u, snap.waypoints.size());
    EXPECT_FALSE(snap.calculated);
    EXPECT_EQ("a route needs two waypoints", snap.status);
  }

  // And it comes back, so the sheet re-opens on the half the user had built.
  RouteStore reopened("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());
  ASSERT_TRUE(reopened.LoadAtLaunch().ok());
  const RouteSnapshot snap = reopened.Snapshot();
  ASSERT_EQ(1u, snap.waypoints.size());
  EXPECT_EQ("Start", snap.waypoints[0].label);
}

TEST(RouteStore, ClearRemovesTheDocumentSoARelaunchHasNoRoute) {
  TempDocument doc("clear");
  {
    RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());
    store.SetWaypoints(TheUsualPair(), "bicycle");
    ASSERT_TRUE(doc.exists());

    const RouteSnapshot snap = store.Clear();
    EXPECT_FALSE(snap.exists);
    EXPECT_FALSE(snap.calculated);
    // The FILE is gone, not merely emptied — of two spellings of "no route",
    // a phone is left holding the one that is not a file.
    EXPECT_FALSE(doc.exists());
  }

  RouteStore reopened("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());
  EXPECT_TRUE(reopened.LoadAtLaunch().ok());
  EXPECT_FALSE(reopened.Snapshot().exists);
}

TEST(RouteStore, ClearingARouteThatWasNeverSavedDoesNotFail) {
  TempDocument doc("clear_empty");
  RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());
  const RouteSnapshot snap = store.Clear();
  EXPECT_FALSE(snap.exists);
  EXPECT_TRUE(store.last_write_error().empty()) << store.last_write_error();
}

// ---------------------------------------------------------------------------
// The plan, and what a relaunch does to it
// ---------------------------------------------------------------------------

TEST(RouteStore, WithNoGraphTheWaypointsSurviveAndTheReasonIsCarried) {
  TempDocument doc("no_graph");
  RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());

  const RouteSnapshot snap = store.SetWaypoints(TheUsualPair(), "bicycle");
  EXPECT_TRUE(snap.exists);
  EXPECT_EQ(2u, snap.waypoints.size());
  EXPECT_FALSE(snap.calculated);
  // The planner's own sentence about the missing graph, carried out to the
  // sheet — which is the reason the overlay's on-canvas status line is off.
  EXPECT_FALSE(snap.status.empty());
}

TEST(RouteStore, ARouteFollowsRoadsAndTheStoreTimesIt) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("follows_roads");
  RouteStore store(graph, RulesFile(), doc.path());

  const RouteSnapshot snap = store.SetWaypoints(TheUsualPair(), "bicycle");
  EXPECT_TRUE(snap.calculated);
  EXPECT_TRUE(snap.complete);
  EXPECT_EQ(0, snap.straight_legs);
  EXPECT_TRUE(snap.is_bicycle);
  EXPECT_GT(snap.length_m, 1500.0);
  EXPECT_LT(snap.length_m, 4000.0);
  EXPECT_GT(snap.seconds, 0.0);

  // Planning happens on the queue that draws, so what it costs is how long a
  // frame was not being drawn. The number is measured, not assumed — this is
  // the assertion that it is a real one.
  EXPECT_GT(store.last_plan_ms(), 0.0);
}

TEST(RouteStore, ARouteSurvivesARelaunchWITHItsRoads) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("relaunch");

  double length = 0.0;
  size_t legs = 0;
  {
    RouteStore store(graph, RulesFile(), doc.path());
    const RouteSnapshot snap = store.SetWaypoints(TheUsualPair(), "bicycle");
    ASSERT_TRUE(snap.calculated);
    length = snap.length_m;
    legs = store.overlay()->plan().legs.size();
    ASSERT_GT(legs, 0u);
  }

  // A COLD START: a new store over the same three paths, which is what the
  // second launch of the app is.
  RouteStore relaunched(graph, RulesFile(), doc.path());
  ASSERT_TRUE(relaunched.LoadAtLaunch().ok());

  const RouteSnapshot snap = relaunched.Snapshot();
  EXPECT_EQ(2u, snap.waypoints.size());
  // The point of the whole class: `FileOpen` alone would leave `calculated`
  // false and the user looking at straight legs where their road was.
  EXPECT_TRUE(snap.calculated);
  EXPECT_TRUE(snap.complete);
  EXPECT_EQ(legs, relaunched.overlay()->plan().legs.size());
  // Replanned rather than reloaded, and it is the same answer: the graph and
  // the rules did not change, so the geometry the document did not carry comes
  // back identical.
  EXPECT_DOUBLE_EQ(length, snap.length_m);
}

TEST(RouteStore, TheProfileRoundTripsAndDecidesHowTheLineIsDrawn) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("profile");
  {
    RouteStore store(graph, RulesFile(), doc.path());
    const RouteSnapshot snap = store.SetWaypoints(TheUsualPair(), "foot");
    EXPECT_EQ("foot", snap.profile);
    // Not a bicycle route, so the overlay draws it solid rather than dashed.
    EXPECT_FALSE(snap.is_bicycle);
  }

  RouteStore relaunched(graph, RulesFile(), doc.path());
  ASSERT_TRUE(relaunched.LoadAtLaunch().ok());
  const RouteSnapshot snap = relaunched.Snapshot();
  EXPECT_EQ("foot", snap.profile);
  // The REPLAN priced it as a walk too, without the caller saying so a second
  // time: the document's own profile is the default for a replan (P5's rule).
  EXPECT_FALSE(snap.is_bicycle);
  EXPECT_TRUE(snap.calculated);
}

TEST(RouteStore, AWalkAndARideOverTheSamePairCostDifferentTime) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("modes");
  RouteStore store(graph, RulesFile(), doc.path());

  const RouteSnapshot walk = store.SetWaypoints(TheUsualPair(), "foot");
  const double walk_s = walk.seconds;
  const RouteSnapshot ride = store.SetWaypoints(TheUsualPair(), "bicycle");

  ASSERT_TRUE(walk.calculated);
  ASSERT_TRUE(ride.calculated);
  // The pack's own two modes, and the reason the segmented control exists.
  EXPECT_GT(walk_s, ride.seconds);
}

TEST(RouteStore, MovingAWaypointReplansAndRewritesTheDocument) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("moved");
  RouteStore store(graph, RulesFile(), doc.path());

  const RouteSnapshot first = store.SetWaypoints(TheUsualPair(), "bicycle");
  ASSERT_TRUE(first.calculated);

  // The end moved a few hundred metres up the island.
  std::vector<RouteWaypoint> moved = TheUsualPair();
  moved[1].position = GeoPoint{32.5985, -80.1145};
  const RouteSnapshot second = store.SetWaypoints(moved, "bicycle");

  EXPECT_TRUE(second.calculated);
  EXPECT_NE(first.length_m, second.length_m);

  // And the document on disk is the NEW one — every change is written, so a
  // relaunch cannot come back with the route before last.
  RouteStore relaunched(graph, RulesFile(), doc.path());
  ASSERT_TRUE(relaunched.LoadAtLaunch().ok());
  EXPECT_DOUBLE_EQ(second.length_m, relaunched.Snapshot().length_m);
}

TEST(RouteStore, AWaypointInTheRiverStaysAStraightLegRatherThanAHole) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("river");
  RouteStore store(graph, RulesFile(), doc.path());

  // Well out to sea, 2.6 km from the nearest graph node and so far outside
  // `snap_meters` (500 m) that nothing can be snapped to.
  //
  // The point must be genuinely out of range. A point in the Kiawah River at
  // 32.6350,-80.0600 is 392 m from a node, inside the snap radius, so it
  // always snapped and the leg failed only because that node was one a
  // bicycle may not use. Profile-aware snapping now finds a legal node within
  // range and the leg routes, which is correct for a point 392 m from a road
  // and leaves the contract in this test's name — a waypoint with no road
  // near it degrades to a straight leg rather than holing the route —
  // untested.
  std::vector<RouteWaypoint> stops = {
      RouteWaypoint{"Start", kRuddyTurnstone},
      RouteWaypoint{"Nowhere", GeoPoint{32.6440, -79.9920}},
      RouteWaypoint{"End", kBeachClub}};
  const RouteSnapshot snap = store.SetWaypoints(std::move(stops), "bicycle");

  // Something is drawn, and it says out loud that it is the lesser answer.
  EXPECT_TRUE(snap.calculated);
  EXPECT_FALSE(snap.complete);
  EXPECT_GT(snap.straight_legs, 0);
  EXPECT_FALSE(snap.status.empty());
}

// ---------------------------------------------------------------------------
// The rule file
// ---------------------------------------------------------------------------

TEST(RouteStore, TheRuleFilesProfilesAreOfferedToAUi) {
  TempDocument doc("profiles");
  RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());

  const std::vector<std::string> names = store.ProfileNames();
  EXPECT_TRUE(store.rules_error().empty()) << store.rules_error();
  // The two the app has a segmented control for.
  EXPECT_NE(names.end(), std::find(names.begin(), names.end(), "foot"));
  EXPECT_NE(names.end(), std::find(names.begin(), names.end(), "bicycle"));
}

TEST(RouteStore, ABadRuleFileLeavesTheBuiltinWeightsRoutingAndSaysWhy) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("bad_rules");
  RouteStore store(graph, "/nonexistent/route-weights.json", doc.path());

  // `fv::RoutePlanner`'s contract, restated where a shell would meet it: a
  // missing rule file is not an error, it is the builtin weights.
  const RouteSnapshot snap = store.SetWaypoints(TheUsualPair(), "");
  EXPECT_TRUE(snap.calculated);
}

// ---------------------------------------------------------------------------
// The roads under the ship
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The line itself
// ---------------------------------------------------------------------------

// The trip computer measures distance-to-go along the road geometry, so the
// store has to hand out the planned line and not the waypoints. On this pair
// the difference is the whole point: two waypoints 2.16 km apart, with a few
// hundred road vertices between them.
TEST(RouteStore, ThePlannedLineIsHandedOutForTheTripComputer) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("route_path");
  RouteStore store(graph, RulesFile(), doc.path());

  EXPECT_TRUE(store.RoutePath().empty()) << "no plan yet";

  const RouteSnapshot snap = store.SetWaypoints(TheUsualPair(), "bicycle");
  ASSERT_TRUE(snap.calculated);

  const std::vector<GeoPoint> path = store.RoutePath();
  ASSERT_GT(path.size(), 2u) << "the waypoints, not the road";

  // The line runs between the waypoints it was asked for.
  EXPECT_NEAR(path.front().lat, kRuddyTurnstone.lat, 1e-3);
  EXPECT_NEAR(path.front().lon, kRuddyTurnstone.lon, 1e-3);
  EXPECT_NEAR(path.back().lat, kBeachClub.lat, 1e-3);
  EXPECT_NEAR(path.back().lon, kBeachClub.lon, 1e-3);

  // No repeated vertex anywhere, which is what the joint-dropping in
  // `RoutePath` is for. A zero-length segment is harmless to every consumer
  // this has today, and is checked because that is a claim about code not yet
  // written.
  for (std::size_t i = 1; i < path.size(); ++i) {
    EXPECT_FALSE(path[i].lat == path[i - 1].lat && path[i].lon == path[i - 1].lon)
        << "duplicate vertex at " << i;
  }

  // And its length is the length the planner reported, which is the number
  // the sheet shows: the same line, measured two ways.
  double walked = 0.0;
  for (std::size_t i = 1; i < path.size(); ++i) {
    const double dlat = (path[i].lat - path[i - 1].lat) * 111195.0;
    const double dlon = (path[i].lon - path[i - 1].lon) * 111195.0 *
                        std::cos(path[i].lat * 3.14159265358979323846 / 180.0);
    walked += std::sqrt(dlat * dlat + dlon * dlon);
  }
  EXPECT_NEAR(walked, snap.length_m, 0.02 * snap.length_m);
}

TEST(RouteStore, ClearingTheRouteLeavesNoLine) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("route_path_clear");
  RouteStore store(graph, RulesFile(), doc.path());
  ASSERT_TRUE(store.SetWaypoints(TheUsualPair(), "bicycle").calculated);
  ASSERT_FALSE(store.RoutePath().empty());

  store.Clear();
  EXPECT_TRUE(store.RoutePath().empty());
}

TEST(RouteStore, TheSnapperAndTheRouterShareOneGraph) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("network");
  RouteStore store(graph, RulesFile(), doc.path());

  // Nothing has loaded a graph yet, and nothing should have: a rider who
  // never presses GPS and never routes does not pay for one.
  EXPECT_EQ(nullptr, store.road_graph());

  fv::Status status;
  const std::shared_ptr<const fv::routing::RoadGraphNetwork> net =
      store.EnsureRoadNetwork(&status);
  ASSERT_NE(nullptr, net) << status.message;
  EXPECT_TRUE(status.ok()) << status.message;

  // The point of the whole arrangement: the network indexes the graph the
  // router routes over, not a second copy of it.
  EXPECT_EQ(store.road_graph().get(), net->graph().get());

  // A route planned afterwards keeps using that same graph.
  const RouteSnapshot snap = store.SetWaypoints(TheUsualPair(), "bicycle");
  ASSERT_TRUE(snap.calculated);
  EXPECT_EQ(store.road_graph().get(), net->graph().get());

  // And a second call is the same object rather than a second index — this is
  // what makes it safe to call on every entry into GPS mode.
  EXPECT_EQ(net.get(), store.EnsureRoadNetwork().get());
}

TEST(RouteStore, TheNetworkAdmitsThePathsARiderIsActuallyOn) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("network_filter");
  RouteStore store(graph, RulesFile(), doc.path());
  const std::shared_ptr<const fv::routing::RoadGraphNetwork> net =
      store.EnsureRoadNetwork();
  ASSERT_NE(nullptr, net);

  // `kAll`, not the network's own `kDriveable` default — the header says why.
  // Measured as the thing that matters rather than as the enum: a driveable
  // filter drops every footway and cycleway on the island, so the two counts
  // differ, and this is the one that keeps them.
  fv::routing::RoadGraphNetwork driveable(
      store.road_graph(), fv::routing::RoadNetworkOptions{});
  EXPECT_GT(net->indexed_arcs(), driveable.indexed_arcs());

  // And it answers the snapper's question at the house the link check uses.
  std::vector<fv::RoadCandidate> found;
  net->QueryNear(kRuddyTurnstone, 60.0, &found);
  EXPECT_FALSE(found.empty());
}

TEST(RouteStore, APackWithNoGraphSimplyDoesNotSnap) {
  TempDocument doc("no_graph");
  RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());

  fv::Status status;
  EXPECT_EQ(nullptr, store.EnsureRoadNetwork(&status));
  // Reported rather than thrown: a moving map handed no network draws a ship
  // that is not on a road, which is exactly what MM5 calls the feature off.
  EXPECT_FALSE(status.ok());
  EXPECT_FALSE(status.message.empty());
}

// ---------------------------------------------------------------------------
// P11 — the pick button says WHERE, not what the numbers are
// ---------------------------------------------------------------------------
//
// Every coordinate below was read out of `kiawah.fvroad` itself rather than
// picked off a map, so each one is a fact about the fixture: the distances in
// the comments are what `QueryNear` measures there.

// Kiawah's own streets, at the places the graph puts them.
constexpr GeoPoint kOnSawgrassLane{32.622846, -80.056766};   // 1.9 m, named
constexpr GeoPoint kOnAnUnnamedCycleway{32.601818, -80.087917};  // 0.0 m
constexpr GeoPoint kOnTurtleBeachLane{32.601854, -80.087917};    // 0.7 m
constexpr GeoPoint kWellOntoTheCycleway{32.601710, -80.087917};  // 2.0 m
constexpr GeoPoint kOnTheParkway{32.600862, -80.129532};     // 0.0 m, no bikes
constexpr GeoPoint kOutToSea{32.560000, -80.100000};

TEST(RouteStore, ThePickButtonNamesTheStreetAPointIsOn) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("describe_named");
  RouteStore store(graph, RulesFile(), doc.path());

  const pippin::PlaceDescription place =
      store.DescribePoint(kOnSawgrassLane, 50.0, "bicycle");
  EXPECT_TRUE(place.usable);
  EXPECT_TRUE(place.named);
  EXPECT_EQ("Sawgrass Lane", place.name);
  EXPECT_LT(place.distance_m, 5.0);
}

TEST(RouteStore, AnUnnamedRoadIsNamedByItsClass) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("describe_class");
  RouteStore store(graph, RulesFile(), doc.path());

  const pippin::PlaceDescription place =
      store.DescribePoint(kOnAnUnnamedCycleway, 50.0, "bicycle");
  EXPECT_TRUE(place.usable);
  EXPECT_FALSE(place.named);
  EXPECT_EQ("cycleway", place.name);
}

TEST(RouteStore, AClassNameReachesTheButtonWithoutItsUnderscores) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("describe_underscore");
  RouteStore store(graph, RulesFile(), doc.path());

  // The slip road off Bohicket Road at the island's gate — `tertiary_link`,
  // unnamed, and the one place in this fixture where the raw tag value would
  // put an underscore on a button. A car may use it; the bicycle profile does
  // not list the class at all, which is the next test's subject.
  const pippin::PlaceDescription place =
      store.DescribePoint(GeoPoint{32.607941, -80.150624}, 50.0, "car");
  EXPECT_TRUE(place.usable);
  EXPECT_EQ("tertiary link", place.name);
}

TEST(RouteStore, TheButtonWillNotNameARoadTheProfileWouldRefuse) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("describe_usable");
  RouteStore store(graph, RulesFile(), doc.path());

  // Standing ON Kiawah Island Parkway, which carries `bicycle=no` and `foot=no`
  // for its whole length — the island put a cycleway beside it instead, 8 m
  // away here. THIS IS THE WHOLE POINT OF THE FEATURE: the snap index is built
  // at `kAll`, so the nearest candidate IS the parkway, and a button naming it
  // would promise a road the very next replan routes around.
  const pippin::PlaceDescription riding =
      store.DescribePoint(kOnTheParkway, 50.0, "bicycle");
  EXPECT_TRUE(riding.usable);
  EXPECT_EQ("cycleway", riding.name);
  EXPECT_NE("Kiawah Island Parkway", riding.name);

  // And the same point priced for a car is the parkway, which is what makes
  // this a test of the PROFILE rather than of a hidden preference for paths.
  store.ForgetNamedPlace();
  const pippin::PlaceDescription driving =
      store.DescribePoint(kOnTheParkway, 50.0, "car");
  EXPECT_TRUE(driving.usable);
  EXPECT_EQ("Kiawah Island Parkway", driving.name);
  EXPECT_LT(driving.distance_m, 1.0);
}

TEST(RouteStore, NothingUsableInRangeIsALabelAndNotABlock) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("describe_none");
  RouteStore store(graph, RulesFile(), doc.path());

  // The Atlantic, a mile off the beach. `usable` false is the whole answer —
  // there is no error, no status and nothing for a caller to handle, because
  // the pick is still taken: a rider aiming at a beach means it.
  const pippin::PlaceDescription sea =
      store.DescribePoint(kOutToSea, 50.0, "bicycle");
  EXPECT_FALSE(sea.usable);
  EXPECT_TRUE(sea.name.empty());
  EXPECT_EQ(fv::kNoRoadArc, sea.arc);

  // Not only the sea. The gate slip road above is dry land with a road on it,
  // and the bicycle profile does not list `tertiary_link`, so a rider gets
  // the same answer there, which is the honest one.
  const pippin::PlaceDescription slip =
      store.DescribePoint(GeoPoint{32.607941, -80.150624}, 50.0, "bicycle");
  EXPECT_FALSE(slip.usable);
}

TEST(RouteStore, ADescribedPlaceDoesNotBlinkBetweenTwoRoadsMetresApart) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("describe_hysteresis");
  RouteStore store(graph, RulesFile(), doc.path());
  ASSERT_DOUBLE_EQ(10.0, store.describe_stay_bonus_m());

  // Turtle Beach Lane and the cycleway that runs beside it, 3 m apart. On the
  // cycleway the cycleway wins outright.
  EXPECT_EQ("cycleway",
            store.DescribePoint(kOnAnUnnamedCycleway, 50.0, "bicycle").name);

  // Two metres north the LANE is nearer (0.7 m against the cycleway's 4.0),
  // and a caller that took the nearest would flip the button here — which is
  // a drift of two metres under a crosshair the rider is not even moving on
  // purpose. The margin keeps the answer.
  const pippin::PlaceDescription kept =
      store.DescribePoint(kOnTurtleBeachLane, 50.0, "bicycle");
  EXPECT_EQ("cycleway", kept.name);

  // The SAME point with no memory is the lane, so the margin is what changed
  // the answer and not the geometry.
  store.ForgetNamedPlace();
  EXPECT_EQ("Turtle Beach Lane",
            store.DescribePoint(kOnTurtleBeachLane, 50.0, "bicycle").name);

  // And the margin is a margin, not a lock: far enough onto the cycleway that
  // the lane is 13 m further away, the answer moves. (The rider has ridden off
  // the lane by then, and a button still naming it would be wrong rather than
  // steady.)
  EXPECT_EQ("cycleway",
            store.DescribePoint(kWellOntoTheCycleway, 50.0, "bicycle").name);
}

TEST(RouteStore, LeavingTheRoadsAltogetherForgetsTheRoadThatWasNamed) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("describe_forget");
  RouteStore store(graph, RulesFile(), doc.path());

  EXPECT_EQ("Sawgrass Lane",
            store.DescribePoint(kOnSawgrassLane, 50.0, "bicycle").name);
  EXPECT_FALSE(store.DescribePoint(kOutToSea, 50.0, "bicycle").usable);
  // Back on land somewhere else. A memory that survived the sea would give
  // Sawgrass Lane a head start on a street a mile away from it.
  EXPECT_EQ("cycleway",
            store.DescribePoint(kOnAnUnnamedCycleway, 50.0, "bicycle").name);
}

TEST(RouteStore, ASheetsLookupDoesNotDisturbTheCrosshairsMemory) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("describe_no_remember");
  RouteStore store(graph, RulesFile(), doc.path());

  // The crosshair is on the cycleway.
  EXPECT_EQ("cycleway",
            store.DescribePoint(kOnAnUnnamedCycleway, 50.0, "bicycle").name);

  // A row in the sheet asks about a stop a mile away, as every open sheet
  // does. It gets its answer...
  EXPECT_EQ("Sawgrass Lane",
            store.DescribePoint(kOnSawgrassLane, 50.0, "bicycle", false).name);

  // ...and the crosshair's own memory is untouched, so the button does not
  // change its mind because a sheet was drawn behind it.
  EXPECT_EQ("cycleway",
            store.DescribePoint(kOnTurtleBeachLane, 50.0, "bicycle").name);
}

TEST(RouteStore, APackWithNoGraphStillPicks) {
  TempDocument doc("describe_no_graph");
  RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());

  // The same answer as the open sea, and correctly so: neither one has a road
  // on it. The button reads "No usable path" and the point is still taken.
  const pippin::PlaceDescription place =
      store.DescribePoint(kOnSawgrassLane, 50.0, "bicycle");
  EXPECT_FALSE(place.usable);
  EXPECT_TRUE(place.name.empty());
}

TEST(RouteStore, AProfileTheRulesDoNotDefineNamesNothing) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("describe_bad_profile");
  RouteStore store(graph, RulesFile(), doc.path());

  // The route itself reports this, once, on its own status line (P5's rule).
  // A label is not the place to say it twice — but naming a road the rules
  // cannot price would be a guess, so this declines instead.
  EXPECT_FALSE(store.DescribePoint(kOnSawgrassLane, 50.0, "unicycle").usable);
}


// ---------------------------------------------------------------------------
// Dragging a waypoint
// ---------------------------------------------------------------------------
//
// The edit is `fv::RouteEditSession` and RouteKit's own tests pin it: a still
// hand is not a drag, a whole drag is one undo entry, a dropped waypoint
// takes a marker's exact coordinate. What is pinned here is
// only what this class adds, and it is the two halves a session cannot know
// about: the replan, and the write.

TEST(RouteStoreDrag, AWaypointUnderAFingerIsFoundByItsLabel) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("grab");
  RouteStore store(graph, RulesFile(), doc.path());
  ASSERT_TRUE(store.SetWaypoints(TheUsualPair(), "bicycle").calculated);

  const fv::MapProjection proj = KiawahProj();
  DrawOnce(store, proj);

  const RouteStore::WaypointHit hit =
      store.WaypointNear(PixelOf(proj, kBeachClub), 12.0);
  ASSERT_TRUE(hit.found);
  EXPECT_EQ("End", hit.label);
  EXPECT_LT(hit.distance_px, 2.0);
}

TEST(RouteStoreDrag, APressOnOpenWaterGrabsNothing) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("grab_miss");
  RouteStore store(graph, RulesFile(), doc.path());
  ASSERT_TRUE(store.SetWaypoints(TheUsualPair(), "bicycle").calculated);

  const fv::MapProjection proj = KiawahProj();
  DrawOnce(store, proj);
  EXPECT_FALSE(store.WaypointNear(fv::PixelPoint{5, 5}, 12.0).found);
}

TEST(RouteStoreDrag, AnOverlayThatHasNeverDrawnGrabsNothing) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("grab_undrawn");
  RouteStore store(graph, RulesFile(), doc.path());
  ASSERT_TRUE(store.SetWaypoints(TheUsualPair(), "bicycle").calculated);

  // No frame. There is a route in the document and none of it is on a screen,
  // so there is nothing a finger could be on top of.
  const fv::MapProjection proj = KiawahProj();
  EXPECT_FALSE(store.WaypointNear(PixelOf(proj, kBeachClub), 12.0).found);
}

TEST(RouteStoreDrag, ADragMovesTheWaypointAndTheReleaseRewritesTheDocument) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("drag_write");
  RouteStore store(graph, RulesFile(), doc.path());
  const RouteSnapshot before = store.SetWaypoints(TheUsualPair(), "bicycle");
  ASSERT_TRUE(before.calculated);

  const fv::MapProjection proj = KiawahProj();
  DrawOnce(store, proj);

  const fv::PixelPoint press = PixelOf(proj, kBeachClub);
  ASSERT_TRUE(store.BeginWaypointDrag("End", press));
  EXPECT_TRUE(store.dragging_waypoint());
  EXPECT_EQ("End", store.dragged_waypoint());

  const fv::PixelPoint release{press.x + 40, press.y - 60};
  EXPECT_TRUE(store.DragWaypointTo(release));
  const RouteSnapshot after = store.EndWaypointDrag(release);
  EXPECT_FALSE(store.dragging_waypoint());

  // It moved, and it is still a followed route rather than the straight legs
  // `SetWaypoints` left behind on the way.
  const GeoPoint moved = PositionOfWaypoint(after, "End");
  EXPECT_NE(kBeachClub.lat, moved.lat);
  EXPECT_TRUE(after.calculated);
  // The other end did not budge.
  EXPECT_DOUBLE_EQ(kRuddyTurnstone.lat,
                   PositionOfWaypoint(after, "Start").lat);

  // And it is on disk, which is the half `RouteEditSession` cannot do: a
  // route dragged into shape and not saved is one the next launch has not
  // got.
  RouteStore relaunched(graph, RulesFile(), doc.path());
  ASSERT_TRUE(relaunched.LoadAtLaunch().ok());
  EXPECT_DOUBLE_EQ(moved.lat,
                   PositionOfWaypoint(relaunched.Snapshot(), "End").lat);
}

TEST(RouteStoreDrag, AGrabAndLetGoIsNotAnEditAndWritesNothing) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("drag_nomove");
  RouteStore store(graph, RulesFile(), doc.path());
  ASSERT_TRUE(store.SetWaypoints(TheUsualPair(), "bicycle").calculated);
  ASSERT_TRUE(doc.exists());

  const fv::MapProjection proj = KiawahProj();
  DrawOnce(store, proj);
  const fv::PixelPoint press = PixelOf(proj, kBeachClub);

  // Delete the file the setup wrote. If the release writes, it comes back —
  // which is the whole assertion, and it is stronger than an mtime on a
  // filesystem whose stamps are coarser than this test is fast.
  std::error_code ec;
  std::filesystem::remove(doc.path(), ec);
  ASSERT_FALSE(doc.exists());

  ASSERT_TRUE(store.BeginWaypointDrag("End", press));
  store.DragWaypointTo(press);  // a still hand: inside the editor's threshold
  const RouteSnapshot after = store.EndWaypointDrag(press);

  EXPECT_FALSE(doc.exists());
  EXPECT_DOUBLE_EQ(kBeachClub.lat, PositionOfWaypoint(after, "End").lat);
}

TEST(RouteStoreDrag, ACancelledDragPutsItBackAndLeavesTheRouteFollowingRoads) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("drag_cancel");
  RouteStore store(graph, RulesFile(), doc.path());
  const RouteSnapshot before = store.SetWaypoints(TheUsualPair(), "bicycle");
  ASSERT_TRUE(before.calculated);

  const fv::MapProjection proj = KiawahProj();
  DrawOnce(store, proj);
  const fv::PixelPoint press = PixelOf(proj, kBeachClub);

  ASSERT_TRUE(store.BeginWaypointDrag("End", press));
  ASSERT_TRUE(store.DragWaypointTo(fv::PixelPoint{press.x + 40, press.y - 60}));
  const RouteSnapshot after = store.CancelWaypointDrag();

  EXPECT_DOUBLE_EQ(kBeachClub.lat, PositionOfWaypoint(after, "End").lat);
  EXPECT_DOUBLE_EQ(kBeachClub.lon, PositionOfWaypoint(after, "End").lon);
  // And the roads are back. Every move on the way out dropped the plan, so a
  // cancel that only restored the waypoints would draw straight legs,
  // indistinguishable on screen from a cancel that failed.
  EXPECT_TRUE(after.calculated);
  EXPECT_DOUBLE_EQ(before.length_m, after.length_m);
}

TEST(RouteStoreDrag, TheHaloIsOnTheGrabbedWaypointAndOnlyWhileItIsHeld) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("drag_halo");
  RouteStore store(graph, RulesFile(), doc.path());
  ASSERT_TRUE(store.SetWaypoints(TheUsualPair(), "bicycle").calculated);

  const fv::MapProjection proj = KiawahProj();
  DrawOnce(store, proj);
  const fv::PixelPoint press = PixelOf(proj, kBeachClub);

  EXPECT_TRUE(store.overlay()->selected().empty());
  ASSERT_TRUE(store.BeginWaypointDrag("End", press));
  EXPECT_EQ("End", store.overlay()->selected());
  store.EndWaypointDrag(press);
  EXPECT_TRUE(store.overlay()->selected().empty());
}

TEST(RouteStoreDrag, WithNoBudgetTheDragShowsStraightLegsAndTheReleaseDoesNot) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("drag_budget_zero");
  RouteStore store(graph, RulesFile(), doc.path());
  ASSERT_TRUE(store.SetWaypoints(TheUsualPair(), "bicycle").calculated);

  // 0 is "never follow live" — the desktop editor's own behaviour, and the
  // setting a bigger graph would want.
  store.set_drag_replan_budget_ms(0.0);

  const fv::MapProjection proj = KiawahProj();
  DrawOnce(store, proj);
  const fv::PixelPoint press = PixelOf(proj, kBeachClub);

  ASSERT_TRUE(store.BeginWaypointDrag("End", press));
  EXPECT_FALSE(store.drag_follows_roads());
  ASSERT_TRUE(store.DragWaypointTo(fv::PixelPoint{press.x + 40, press.y - 60}));
  // Mid-drag the plan is gone, which is what straight legs are drawn from.
  EXPECT_FALSE(store.Snapshot().calculated);

  // And the release follows the roads regardless. Whatever the drag showed,
  // what is committed and written is a real route.
  const RouteSnapshot after =
      store.EndWaypointDrag(fv::PixelPoint{press.x + 40, press.y - 60});
  EXPECT_TRUE(after.calculated);
}

TEST(RouteStoreDrag, OneOverrunEndsLiveFollowingForTheRestOfThatDrag) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("drag_overrun");
  RouteStore store(graph, RulesFile(), doc.path());
  ASSERT_TRUE(store.SetWaypoints(TheUsualPair(), "bicycle").calculated);

  // A budget no real plan can meet, but not zero — so the drag STARTS
  // following and gives up on the first measurement, which is the path the
  // give-up rule actually takes on a graph that is too big.
  store.set_drag_replan_budget_ms(1e-9);

  const fv::MapProjection proj = KiawahProj();
  DrawOnce(store, proj);
  const fv::PixelPoint press = PixelOf(proj, kBeachClub);

  ASSERT_TRUE(store.BeginWaypointDrag("End", press));
  EXPECT_TRUE(store.drag_follows_roads());
  ASSERT_TRUE(store.DragWaypointTo(fv::PixelPoint{press.x + 20, press.y - 30}));
  EXPECT_FALSE(store.drag_follows_roads());
  // It does not come back on three pixels later.
  ASSERT_TRUE(store.DragWaypointTo(fv::PixelPoint{press.x + 23, press.y - 33}));
  EXPECT_FALSE(store.drag_follows_roads());

  EXPECT_TRUE(
      store.EndWaypointDrag(fv::PixelPoint{press.x + 23, press.y - 33})
          .calculated);
}

TEST(RouteStoreDrag, ADragOfSomethingThatIsNotThereIsRefused) {
  SKIP_WITHOUT_GRAPH();
  TempDocument doc("drag_ghost");
  RouteStore store(graph, RulesFile(), doc.path());
  ASSERT_TRUE(store.SetWaypoints(TheUsualPair(), "bicycle").calculated);

  EXPECT_FALSE(store.BeginWaypointDrag("Via 7", fv::PixelPoint{100, 100}));
  EXPECT_FALSE(store.dragging_waypoint());
  // And the calls that follow a refused grab are all no-ops rather than
  // crashes — a shell whose gesture began before its hit test came back is
  // allowed to be one step behind.
  EXPECT_FALSE(store.DragWaypointTo(fv::PixelPoint{110, 110}));
  EXPECT_TRUE(store.EndWaypointDrag(fv::PixelPoint{110, 110}).calculated);
}

TEST(RouteStoreDrag, TheSnapToleranceReachesTheEditor) {
  TempDocument doc("drag_snap_tol");
  RouteStore store("/nonexistent/kiawah.fvroad", RulesFile(), doc.path());
  // The pack spells "no snapping" as 0 and the editor uses the same spelling,
  // so the shell's `[pick] snap_tolerance` needs no translation on the way.
  store.SetDragSnapTolerancePx(0.0);
  EXPECT_DOUBLE_EQ(0.0, store.overlay()->edit().snap_tolerance_px());
  store.SetDragSnapTolerancePx(36.0);
  EXPECT_DOUBLE_EQ(36.0, store.overlay()->edit().snap_tolerance_px());
}

}  // namespace
