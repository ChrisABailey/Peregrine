// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Router tests (O4).
//
// The load-bearing test here is BidirectionalMatchesDijkstraEverywhere: the
// unidirectional search is simple enough to be obviously correct, so it is
// the oracle the two-frontier one is checked against, over hundreds of real
// node pairs. Everything else pins a property a hash cannot see — which way
// a one-way runs, that the drawn line starts where it was asked to, that the
// time metric really minimises time.

#include "fv_router.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fv_road_graph.h"
#include "fv_route_rules.h"

namespace fs = std::filesystem;

namespace {

using fv::routing::RoadGraph;
using fv::routing::Route;
using fv::routing::RouteOptions;
using fv::routing::Router;

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

// Every `map*.osm` export in TestData/OSM, sorted so the merge order is
// stable. ENUMERATED, NOT NAMED: the four exports this started as became three
// when the extract was refreshed, and a hard-coded `map-4.osm` then failed the
// whole suite with kNotFound rather than with anything about routing. The
// ledger's rule for a directory that a person refills — assert structure, not
// a count — applies to the input list as much as to the numbers.
std::vector<std::string> KiawahInputs() {
  std::vector<std::string> in;
  const fs::path dir = fs::path(TestDataDir()) / "OSM";
  std::error_code ec;
  for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
    const std::string name = e.path().filename().string();
    if (e.path().extension() == ".osm" && name.compare(0, 3, "map") == 0)
      in.push_back(e.path().string());
  }
  std::sort(in.begin(), in.end());
  return in;
}

// The Kiawah exports are map TEST DATA and are not in the repository, so a
// clone without them must SKIP these rather than fail them — the published
// tree's guarantee is that it builds and goes green with no map data at all.
// (`TestDataDir()` falls back to a relative "TestData", so in a working tree
// that has the data these still run with no environment set.)
#define SKIP_WITHOUT_KIAWAH()                                     \
  const std::vector<std::string> kiawah_inputs = KiawahInputs();  \
  if (kiawah_inputs.empty())                                      \
  GTEST_SKIP() << "no Kiawah OSM exports in " << TestDataDir() << "/OSM"


// The same four-junction loop the graph tests use: n1 - n3 - n4 - n5 - n1,
// with n3 -> n4 one-way. Going n4 -> n3 must take the long way round.
const char* kLoopOsm = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.7000000" lon="-80.0000000"/>
 <node id="2" lat="32.7000000" lon="-80.0030000"/>
 <node id="6" lat="32.7010000" lon="-80.0070000"/>
 <node id="3" lat="32.7000000" lon="-80.0100000"/>
 <node id="4" lat="32.7050000" lon="-80.0100000"/>
 <node id="5" lat="32.7050000" lon="-80.0000000"/>
 <way id="101">
  <nd ref="1"/><nd ref="2"/><nd ref="6"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="Loop Street"/>
 </way>
 <way id="102">
  <nd ref="3"/><nd ref="4"/>
  <tag k="highway" v="residential"/><tag k="oneway" v="yes"/>
 </way>
 <way id="103">
  <nd ref="4"/><nd ref="5"/><tag k="highway" v="residential"/>
 </way>
 <way id="104">
  <nd ref="5"/><nd ref="1"/><tag k="highway" v="residential"/>
 </way>
 <way id="105">
  <nd ref="1"/><nd ref="4"/><tag k="highway" v="footway"/>
 </way>
</osm>
)";

// Three ways from n1 to n3, and a bicycle has a different favourite than a
// car does. Straightest (so shortest) is the path, but it says bicycle=no;
// the cycleway is next; the residential detour through n2 is longest and is
// the only one a car may use at all.
const char* kCycleOsm = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.7000000" lon="-80.0000000"/>
 <node id="2" lat="32.7100000" lon="-80.0100000"/>
 <node id="3" lat="32.7000000" lon="-80.0200000"/>
 <node id="4" lat="32.7020000" lon="-80.0100000"/>
 <node id="5" lat="32.7000000" lon="-80.0100000"/>
 <way id="201">
  <nd ref="1"/><nd ref="2"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="The Long Way"/>
 </way>
 <way id="202">
  <nd ref="1"/><nd ref="4"/><nd ref="3"/>
  <tag k="highway" v="cycleway"/><tag k="name" v="The Bike Path"/>
 </way>
 <way id="203">
  <nd ref="1"/><nd ref="5"/><nd ref="3"/>
  <tag k="highway" v="path"/><tag k="bicycle" v="no"/>
  <tag k="name" v="No Bikes"/>
 </way>
</osm>
)";

// A gated-community shape (O5b). Two ways from n1 to n3: the straight one is
// `access=private`, the dogleg through n2 is public and about 40% longer. n5
// hangs off n3 behind a private service road — the only way in — and n6 is
// behind a gate that also says bicycle=no.
const char* kPrivateOsm = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.7000000" lon="-80.0000000"/>
 <node id="2" lat="32.7100000" lon="-80.0100000"/>
 <node id="3" lat="32.7000000" lon="-80.0200000"/>
 <node id="4" lat="32.7000000" lon="-80.0100000"/>
 <node id="5" lat="32.6950000" lon="-80.0200000"/>
 <node id="6" lat="32.6900000" lon="-80.0200000"/>
 <way id="401">
  <nd ref="1"/><nd ref="2"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="Public Way"/>
 </way>
 <way id="402">
  <nd ref="1"/><nd ref="4"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="Gated Lane"/>
  <tag k="access" v="private"/>
 </way>
 <way id="403">
  <nd ref="3"/><nd ref="5"/>
  <tag k="highway" v="residential"/><tag k="name" v="Cul De Sac"/>
  <tag k="access" v="private"/>
 </way>
 <way id="404">
  <nd ref="5"/><nd ref="6"/>
  <tag k="highway" v="residential"/><tag k="name" v="No Bikes Gate"/>
  <tag k="access" v="private"/><tag k="bicycle" v="no"/>
 </way>
</osm>
)";

// The shape a "via" point makes visible (O5d). W - J - E is a straight road;
// N hangs north of J; and E is joined to N by a way that swings a long way
// south, so the DIRECT edge E->N is four times the length of E->J->N.
//
// Asked for W, via E, to N, a router that treats the two pairs independently
// drives out to E, turns round, and comes back through J — the complaint this
// fixture exists to reproduce. n7 is a spur with no other exit, for the case
// where the U-turn cannot be given up.
//
// NOTE the spur hangs off J and not off E, and that is load-bearing. The ban
// is on a TURN — in along an arc, straight back out along it — so a spur at
// the stop is an escape from it: the route drives out to the dead end, turns
// round THERE, and comes back through the stop having made two legal turns.
// That is the right reading of "do not turn round at the stop" and the wrong
// fixture for testing it (measured: it was 4.9 km against the 8.1 km detour,
// so the search took it).
const char* kViaOsm = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.7000000" lon="-80.0200000"/>
 <node id="2" lat="32.7000000" lon="-80.0100000"/>
 <node id="3" lat="32.7000000" lon="-80.0000000"/>
 <node id="4" lat="32.7100000" lon="-80.0100000"/>
 <node id="5" lat="32.6800000" lon="-79.9900000"/>
 <node id="7" lat="32.7050000" lon="-80.0130000"/>
 <way id="501">
  <nd ref="1"/><nd ref="2"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="Main Street"/>
 </way>
 <way id="502">
  <nd ref="2"/><nd ref="4"/>
  <tag k="highway" v="residential"/><tag k="name" v="North Lane"/>
 </way>
 <way id="503">
  <nd ref="3"/><nd ref="5"/><nd ref="4"/>
  <tag k="highway" v="residential"/><tag k="name" v="The Long Way South"/>
 </way>
 <way id="504">
  <nd ref="2"/><nd ref="7"/>
  <tag k="highway" v="residential"/><tag k="name" v="Dead End"/>
 </way>
</osm>
)";

fs::path ScratchDir() {
  fs::path dir = fs::temp_directory_path() / "fv_routing_test";
  fs::create_directories(dir);
  return dir;
}

std::string WriteLoop() {
  const fs::path path = ScratchDir() / "router-loop.osm";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << kLoopOsm;
  out.close();
  return path.string();
}

RoadGraph BuildLoop(bool honor_oneway = true) {
  RoadGraph g;
  fv::routing::RoadGraphBuildOptions options;
  options.honor_oneway = honor_oneway;
  const fv::Status s = fv::routing::BuildRoadGraph({WriteLoop()}, options, &g, nullptr);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  return g;
}

// Kiawah is a gated resort island, and this fixture deliberately builds it
// --ignore-access: the oracle tests below pin invariants that hold only where
// the cost IS the quantity reported — "the distance metric minimises metres",
// "a turn restriction never speeds the clock up" — and O5b's private penalty
// is a weight, so on the strict graph those statements are simply not the
// claim any more (the same reason the bicycle profile has its own tests).
// Private access is covered on the strict graph under RouterPrivateAccess.
RoadGraph BuildKiawah() {
  RoadGraph g;
  fv::routing::RoadGraphBuildOptions options;
  options.honor_access = false;
  const fv::Status s = fv::routing::BuildRoadGraph(KiawahInputs(), options, &g, nullptr);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  return g;
}

RoadGraph BuildCycleFixture(bool cycle_only = false) {
  const fs::path path = ScratchDir() / "router-cycle.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << kCycleOsm;
  }
  RoadGraph g;
  fv::routing::RoadGraphBuildOptions options;
  options.cycle_only = cycle_only;
  const fv::Status s = fv::routing::BuildRoadGraph({path.string()}, options, &g, nullptr);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  return g;
}

RoadGraph BuildViaFixture() {
  const fs::path path = ScratchDir() / "router-via.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << kViaOsm;
  }
  RoadGraph g;
  const fv::Status s = fv::routing::BuildRoadGraph({path.string()}, {}, &g, nullptr);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  return g;
}

RoadGraph BuildPrivateFixture() {
  const fs::path path = ScratchDir() / "router-private.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << kPrivateOsm;
  }
  RoadGraph g;
  const fv::Status s = fv::routing::BuildRoadGraph({path.string()}, {}, &g, nullptr);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  return g;
}

uint32_t NodeForOsmId(const RoadGraph& g, int64_t osm_id) {
  for (uint32_t i = 0; i < g.node_count(); ++i) {
    if (g.node(i).osm_id == osm_id) return i;
  }
  ADD_FAILURE() << "no graph node for OSM id " << osm_id;
  return 0;
}

TEST(Router, OneWayIsHonouredInBothDirections) {
  const RoadGraph g = BuildLoop();
  const Router router(g);
  const uint32_t n3 = NodeForOsmId(g, 3);
  const uint32_t n4 = NodeForOsmId(g, 4);

  RouteOptions options;  // driving
  Route down;
  ASSERT_EQ(router.RouteNodes(n3, n4, options, &down).code, fv::kOk);
  ASSERT_TRUE(down.found);
  // Straight down the one-way: two vertices, one arc.
  EXPECT_EQ(down.nodes.size(), 2u);
  EXPECT_NEAR(down.length_m, 556.0, 15.0);

  Route up;
  ASSERT_EQ(router.RouteNodes(n4, n3, options, &up).code, fv::kOk);
  ASSERT_TRUE(up.found);
  // n4 -> n5 -> n1 -> n3, which is the rest of the loop.
  EXPECT_EQ(up.nodes.size(), 4u);
  EXPECT_GT(up.length_m, down.length_m * 3.0);
}

TEST(Router, HonorOnewayOffMakesTheLinkTwoWay) {
  const RoadGraph g = BuildLoop(/*honor_oneway=*/false);
  const Router router(g);
  Route up;
  ASSERT_EQ(router.RouteNodes(NodeForOsmId(g, 4), NodeForOsmId(g, 3), {}, &up).code, fv::kOk);
  ASSERT_TRUE(up.found);
  EXPECT_EQ(up.nodes.size(), 2u);
}

TEST(Router, DrivingExcludesFootwaysAndWalkingUsesThem) {
  const RoadGraph g = BuildLoop();
  const Router router(g);
  const uint32_t n1 = NodeForOsmId(g, 1);
  const uint32_t n4 = NodeForOsmId(g, 4);

  // Way 105 is a footway shortcut straight from n1 to n4. Both routes ask
  // for the SHORTEST, not the quickest, so that the test turns on what each
  // profile MAY use rather than on the two profiles' different clocks.
  RouteOptions drive;
  drive.metric = fv::routing::RouteMetric::kDistance;
  Route by_car;
  ASSERT_EQ(router.RouteNodes(n1, n4, drive, &by_car).code, fv::kOk);
  ASSERT_TRUE(by_car.found);
  EXPECT_GT(by_car.nodes.size(), 2u);

  RouteOptions walk;
  walk.driving = false;
  walk.metric = fv::routing::RouteMetric::kDistance;
  Route on_foot;
  ASSERT_EQ(router.RouteNodes(n1, n4, walk, &on_foot).code, fv::kOk);
  ASSERT_TRUE(on_foot.found);
  EXPECT_EQ(on_foot.nodes.size(), 2u);
  EXPECT_LT(on_foot.length_m, by_car.length_m);
}

TEST(Router, GeometryStartsAndEndsAtTheRequestedNodesAndIsContinuous) {
  const RoadGraph g = BuildLoop();
  const Router router(g);
  const uint32_t n4 = NodeForOsmId(g, 4);
  const uint32_t n3 = NodeForOsmId(g, 3);

  Route route;
  ASSERT_EQ(router.RouteNodes(n4, n3, {}, &route).code, fv::kOk);
  ASSERT_TRUE(route.found);
  ASSERT_GE(route.geometry.size(), 4u);

  EXPECT_NEAR(route.geometry.front().lat, g.location(n4).lat, 1e-9);
  EXPECT_NEAR(route.geometry.front().lon, g.location(n4).lon, 1e-9);
  EXPECT_NEAR(route.geometry.back().lat, g.location(n3).lat, 1e-9);
  EXPECT_NEAR(route.geometry.back().lon, g.location(n3).lon, 1e-9);

  // No jumps: consecutive drawn points are all short hops, and their total
  // agrees with the reported length. This is what catches an arc emitted
  // backwards — the line would double back and the sum would blow up.
  double walked = 0.0;
  for (size_t i = 1; i < route.geometry.size(); ++i) {
    const double step = fv::routing::GreatCircleMeters(
        route.geometry[i - 1].lat, route.geometry[i - 1].lon, route.geometry[i].lat,
        route.geometry[i].lon);
    EXPECT_LT(step, 1200.0) << "gap at drawn point " << i;
    walked += step;
  }
  EXPECT_NEAR(walked, route.length_m, route.length_m * 0.01);

  // The leg that runs through the shape points must be the named one.
  bool saw_named = false;
  for (const fv::routing::RouteLeg& leg : route.legs) {
    if (leg.name == "Loop Street") saw_named = true;
    EXPECT_EQ(leg.klass, "residential");
  }
  EXPECT_TRUE(saw_named);
}

TEST(Router, SameNodeIsAZeroLengthRoute) {
  const RoadGraph g = BuildLoop();
  const Router router(g);
  Route route;
  ASSERT_EQ(router.RouteNodes(3, 3, {}, &route).code, fv::kOk);
  EXPECT_TRUE(route.found);
  EXPECT_EQ(route.length_m, 0.0);
  EXPECT_EQ(route.geometry.size(), 1u);
  EXPECT_EQ(route.nodes.size(), 1u);
}

TEST(Router, OutOfRangeNodesAreRejected) {
  const RoadGraph g = BuildLoop();
  const Router router(g);
  Route route;
  EXPECT_EQ(router.RouteNodes(0, g.node_count(), {}, &route).code, fv::kInvalidArg);
  EXPECT_EQ(router.RouteNodes(g.node_count(), 0, {}, &route).code, fv::kInvalidArg);
}

TEST(Router, SnapFailureIsOutOfCoverageNotAnEmptyRoute) {
  const RoadGraph g = BuildLoop();
  const Router router(g);
  RouteOptions options;
  options.snap_meters = 200.0;
  Route route;
  const fv::Status s =
      router.Route(fv::GeoPoint{40.0, -80.0}, fv::GeoPoint{32.70, -80.00}, options, &route);
  EXPECT_EQ(s.code, fv::kOutOfCoverage);
  EXPECT_FALSE(route.found);
}

TEST(Router, GeographicEndpointsSnapAndReportTheirOffset) {
  const RoadGraph g = BuildLoop();
  const Router router(g);
  RouteOptions options;
  options.snap_meters = 300.0;

  Route route;
  // A little off n1 and a little off n4.
  ASSERT_EQ(router.Route(fv::GeoPoint{32.7001, -80.0001}, fv::GeoPoint{32.7049, -80.0101},
                         options, &route)
                .code,
            fv::kOk);
  ASSERT_TRUE(route.found);
  EXPECT_EQ(g.node(route.start_node).osm_id, 1);
  EXPECT_EQ(g.node(route.end_node).osm_id, 4);
  EXPECT_GT(route.start_offset_m, 0.0);
  EXPECT_LT(route.start_offset_m, 50.0);
  EXPECT_LT(route.end_offset_m, 50.0);
}

TEST(Router, DisconnectedIsAnAnswerNotAnError) {
  // Two islands with no link between them.
  const fs::path path = ScratchDir() / "islands.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.70" lon="-80.00"/>
 <node id="2" lat="32.70" lon="-80.01"/>
 <node id="3" lat="33.70" lon="-80.00"/>
 <node id="4" lat="33.70" lon="-80.01"/>
 <way id="1"><nd ref="1"/><nd ref="2"/><tag k="highway" v="residential"/></way>
 <way id="2"><nd ref="3"/><nd ref="4"/><tag k="highway" v="residential"/></way>
</osm>
)";
  }
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({path.string()}, {}, &g, nullptr).code, fv::kOk);
  ASSERT_EQ(g.node_count(), 4u);

  const Router router(g);
  Route route;
  const fv::Status s = router.RouteNodes(NodeForOsmId(g, 1), NodeForOsmId(g, 3), {}, &route);
  EXPECT_EQ(s.code, fv::kOk);
  EXPECT_FALSE(route.found);

  // And the same through the unidirectional search.
  RouteOptions plain;
  plain.bidirectional = false;
  Route again;
  EXPECT_EQ(router.RouteNodes(NodeForOsmId(g, 1), NodeForOsmId(g, 3), plain, &again).code,
            fv::kOk);
  EXPECT_FALSE(again.found);
}

// ---------------------------------------------------------------------------
// Turn restrictions (O5)
// ---------------------------------------------------------------------------

// The same crossroads the graph tests use: C(1) in the middle, arms S(2),
// N(3), W(4), E(5), and a bypass W -> N so every corner stays reachable when
// a turn is taken away. `relations` is spliced in before </osm>.
std::string CrossroadsOsm(const std::string& relations) {
  return R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.7000000" lon="-80.0000000"/>
 <node id="2" lat="32.6900000" lon="-80.0000000"/>
 <node id="3" lat="32.7100000" lon="-80.0000000"/>
 <node id="4" lat="32.7000000" lon="-80.0100000"/>
 <node id="5" lat="32.7000000" lon="-79.9900000"/>
 <way id="301"><nd ref="2"/><nd ref="1"/>
  <tag k="highway" v="residential"/><tag k="name" v="South Road"/></way>
 <way id="302"><nd ref="1"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="North Road"/></way>
 <way id="303"><nd ref="1"/><nd ref="4"/>
  <tag k="highway" v="residential"/><tag k="name" v="West Road"/></way>
 <way id="304"><nd ref="1"/><nd ref="5"/>
  <tag k="highway" v="residential"/><tag k="name" v="East Road"/></way>
 <way id="305"><nd ref="4"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="Bypass"/></way>
)" + relations + "</osm>\n";
}

std::string RestrictionRelation(const char* value, int to_way) {
  std::string r = " <relation id=\"900\">\n";
  r += "  <member type=\"way\" ref=\"301\" role=\"from\"/>\n";
  r += "  <member type=\"node\" ref=\"1\" role=\"via\"/>\n";
  r += "  <member type=\"way\" ref=\"" + std::to_string(to_way) + "\" role=\"to\"/>\n";
  r += "  <tag k=\"type\" v=\"restriction\"/>\n";
  r += std::string("  <tag k=\"restriction\" v=\"") + value + "\"/>\n";
  r += " </relation>\n";
  return r;
}

RoadGraph BuildCrossroads(const char* name, const std::string& relations) {
  const fs::path path = ScratchDir() / name;
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << CrossroadsOsm(relations);
  }
  RoadGraph g;
  const fv::Status s = fv::routing::BuildRoadGraph({path.string()}, {}, &g, nullptr);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  return g;
}

// Both searches must give the same answer, always — a restriction that only
// one of them honours is worse than one neither does.
std::vector<uint32_t> RouteBothWays(const Router& router, uint32_t s, uint32_t t,
                                    RouteOptions options) {
  options.bidirectional = true;
  Route bidi;
  EXPECT_EQ(router.RouteNodes(s, t, options, &bidi).code, fv::kOk);
  options.bidirectional = false;
  Route plain;
  EXPECT_EQ(router.RouteNodes(s, t, options, &plain).code, fv::kOk);

  EXPECT_EQ(bidi.found, plain.found);
  if (bidi.found && plain.found) {
    EXPECT_NEAR(bidi.seconds, plain.seconds, std::max(1e-6, plain.seconds * 1e-9));
    EXPECT_EQ(bidi.nodes, plain.nodes);
  }
  return bidi.found ? bidi.nodes : std::vector<uint32_t>();
}

TEST(RouterTurnRestrictions, NoLeftTurnSendsTheDriverRoundTheBypass) {
  const RoadGraph g = BuildCrossroads("router-cross-noleft.osm",
                                      RestrictionRelation("no_left_turn", 303));
  ASSERT_EQ(g.restriction_count(), 1u);
  const Router router(g);
  const uint32_t s = NodeForOsmId(g, 2), c = NodeForOsmId(g, 1);
  const uint32_t n = NodeForOsmId(g, 3), w = NodeForOsmId(g, 4);

  // S -> W is one left turn at C, and that turn is what the sign forbids.
  const std::vector<uint32_t> restricted = RouteBothWays(router, s, w, {});
  ASSERT_EQ(restricted.size(), 4u);
  EXPECT_EQ(restricted[0], s);
  EXPECT_EQ(restricted[1], c);
  EXPECT_EQ(restricted[2], n);
  EXPECT_EQ(restricted[3], w);

  // Turn the restriction off in the query and the direct turn comes back.
  RouteOptions ignore;
  ignore.honor_turn_restrictions = false;
  const std::vector<uint32_t> direct = RouteBothWays(router, s, w, ignore);
  ASSERT_EQ(direct.size(), 3u);
  EXPECT_EQ(direct[1], c);
  EXPECT_EQ(direct[2], w);

  // The detour really is longer, so the router took it under protest.
  Route a, b;
  ASSERT_EQ(router.RouteNodes(s, w, {}, &a).code, fv::kOk);
  ASSERT_EQ(router.RouteNodes(s, w, ignore, &b).code, fv::kOk);
  EXPECT_GT(a.length_m, b.length_m);
}

TEST(RouterTurnRestrictions, TheSameTurnIsLegalFromADifferentApproach) {
  const RoadGraph g = BuildCrossroads("router-cross-approach.osm",
                                      RestrictionRelation("no_left_turn", 303));
  const Router router(g);
  // The sign is on the southern approach. Coming from the east, turning into
  // West Road is the same piece of tarmac and perfectly legal.
  const std::vector<uint32_t> nodes =
      RouteBothWays(router, NodeForOsmId(g, 5), NodeForOsmId(g, 4), {});
  ASSERT_EQ(nodes.size(), 3u);
  EXPECT_EQ(nodes[1], NodeForOsmId(g, 1));
}

TEST(RouterTurnRestrictions, WalkingAndCyclingAreNotBoundByThem) {
  const RoadGraph g = BuildCrossroads("router-cross-walk.osm",
                                      RestrictionRelation("no_left_turn", 303));
  const Router router(g);
  const uint32_t s = NodeForOsmId(g, 2), w = NodeForOsmId(g, 4);

  RouteOptions walking;
  walking.driving = false;
  EXPECT_EQ(RouteBothWays(router, s, w, walking).size(), 3u);

  RouteOptions cycling;
  cycling.cycle_only = true;
  EXPECT_EQ(RouteBothWays(router, s, w, cycling).size(), 3u);
}

TEST(RouterTurnRestrictions, OnlyStraightOnForcesTheRouteToDoubleBack) {
  const RoadGraph g = BuildCrossroads("router-cross-only.osm",
                                      RestrictionRelation("only_straight_on", 302));
  ASSERT_EQ(g.restriction_count(), 1u);
  const Router router(g);
  const uint32_t s = NodeForOsmId(g, 2), c = NodeForOsmId(g, 1);
  const uint32_t n = NodeForOsmId(g, 3), e = NodeForOsmId(g, 5);

  // Arriving from the south the only legal exit is north, so reaching East
  // Road means going up to N, turning round, and coming back through C — a
  // path that visits C TWICE, in two different states. A search that labelled
  // junctions rather than arrivals could not express this route at all.
  const std::vector<uint32_t> nodes = RouteBothWays(router, s, e, {});
  ASSERT_EQ(nodes.size(), 5u);
  EXPECT_EQ(nodes[0], s);
  EXPECT_EQ(nodes[1], c);
  EXPECT_EQ(nodes[2], n);
  EXPECT_EQ(nodes[3], c);
  EXPECT_EQ(nodes[4], e);

  // The drawn line has to agree with that: it starts at S, ends at E, and
  // passes through C's coordinate twice.
  Route route;
  ASSERT_EQ(router.RouteNodes(s, e, {}, &route).code, fv::kOk);
  ASSERT_FALSE(route.geometry.empty());
  EXPECT_NEAR(route.geometry.front().lat, g.location(s).lat, 1e-9);
  EXPECT_NEAR(route.geometry.back().lon, g.location(e).lon, 1e-9);
  int at_c = 0;
  for (const fv::GeoPoint& p : route.geometry) {
    if (std::fabs(p.lat - g.location(c).lat) < 1e-9 &&
        std::fabs(p.lon - g.location(c).lon) < 1e-9) {
      ++at_c;
    }
  }
  EXPECT_EQ(at_c, 2);
}

TEST(RouterTurnRestrictions, EveryPairAgreesBetweenTheTwoSearches) {
  const RoadGraph g = BuildCrossroads("router-cross-pairs.osm",
                                      RestrictionRelation("only_straight_on", 302));
  const Router router(g);
  for (uint32_t s = 0; s < g.node_count(); ++s) {
    for (uint32_t t = 0; t < g.node_count(); ++t) {
      if (s == t) continue;
      const std::vector<uint32_t> nodes = RouteBothWays(router, s, t, {});
      ASSERT_FALSE(nodes.empty()) << "the crossroads is connected: " << s << " -> " << t;
      EXPECT_EQ(nodes.front(), s);
      EXPECT_EQ(nodes.back(), t);
    }
  }
}

TEST(RouterTurnRestrictions, KiawahsOwnRestrictionsChangeARouteAndNothingElseDoes) {
  SKIP_WITHOUT_KIAWAH();
  // The four exports carry five real restrictions. Build the graph twice —
  // once reading them, once not — and the two routers must agree everywhere
  // except where a sign actually bites.
  RoadGraph with_signs;
  fv::routing::RoadGraphBuildOptions options;
  options.honor_access = false;
  ASSERT_EQ(fv::routing::BuildRoadGraph(KiawahInputs(), options, &with_signs, nullptr).code,
            fv::kOk);
  ASSERT_GT(with_signs.restriction_count(), 0u);

  options.honor_turn_restrictions = false;
  RoadGraph without_signs;
  ASSERT_EQ(fv::routing::BuildRoadGraph(KiawahInputs(), options, &without_signs, nullptr).code,
            fv::kOk);
  ASSERT_EQ(without_signs.restriction_count(), 0u);
  // Reading relations must not have changed the road network itself.
  ASSERT_EQ(with_signs.node_count(), without_signs.node_count());
  ASSERT_EQ(with_signs.arc_count(), without_signs.arc_count());

  const Router restricted(with_signs);
  const Router unrestricted(without_signs);

  int compared = 0;
  for (uint32_t i = 0; i < 200; ++i) {
    const uint32_t s = (i * 7919u) % with_signs.node_count();
    const uint32_t t = (i * 104729u + 17u) % with_signs.node_count();
    if (s == t) continue;

    Route a, b;
    ASSERT_EQ(restricted.RouteNodes(s, t, {}, &a).code, fv::kOk);
    ASSERT_EQ(unrestricted.RouteNodes(s, t, {}, &b).code, fv::kOk);
    // Taking turns away can never make a pair reachable that was not, and on
    // this network it never disconnects one either.
    ASSERT_EQ(a.found, b.found) << s << " -> " << t;
    if (!a.found) continue;
    ++compared;
    // A restriction can only ever cost time; it cannot find a shortcut.
    EXPECT_GE(a.seconds, b.seconds - std::max(1e-6, b.seconds * 1e-9)) << s << " -> " << t;
  }
  EXPECT_GT(compared, 50);
}

// Kiawah's own five restrictions are too sparse for a random pair to meet
// one, and the two frontiers label a restricted junction DIFFERENTLY (by the
// arc in, by the arc out), so their meeting rule is the part of O5 a real
// extract barely exercises. Saturate the graph with synthetic restrictions
// instead and hold the two searches against each other again.
TEST(RouterTurnRestrictions, BidirectionalMatchesDijkstraOnASaturatedGraph) {
  SKIP_WITHOUT_KIAWAH();
  RoadGraph g = BuildKiawah();
  ASSERT_GT(g.node_count(), 1000u);

  // At every eighth junction of degree 3 or more, forbid the turn from its
  // first arc onto its second — an arbitrary but reproducible choice, and one
  // that leaves the other exits open so the network stays connected.
  std::vector<fv::routing::TurnRestriction>& restrictions = g.mutable_restrictions();
  for (uint32_t v = 0; v < g.node_count(); v += 8) {
    if (g.arc_end(v) - g.arc_begin(v) < 3) continue;
    fv::routing::TurnRestriction r;
    r.via_node = v;
    r.from_arc = g.arc_begin(v);
    r.to_arc = g.arc_begin(v) + 1;
    r.kind = (v % 16 == 0) ? fv::routing::TurnRestrictionKind::kNo
                           : fv::routing::TurnRestrictionKind::kOnly;
    restrictions.push_back(r);
  }
  g.Finalize();
  ASSERT_GT(g.restriction_count(), 50u);
  EXPECT_GT(g.state_count(), g.node_count());

  const Router router(g);
  RouteOptions bidi;
  RouteOptions plain;
  plain.bidirectional = false;

  int routed = 0, differed_from_unrestricted = 0;
  // Named, not a temporary: Router keeps a reference to the graph it is given.
  const RoadGraph unrestricted_graph = BuildKiawah();
  const Router unrestricted_router(unrestricted_graph);
  for (uint32_t i = 0; i < 300; ++i) {
    const uint32_t s = (i * 7919u) % g.node_count();
    const uint32_t t = (i * 104729u + 17u) % g.node_count();
    if (s == t) continue;

    Route a, b;
    ASSERT_EQ(router.RouteNodes(s, t, bidi, &a).code, fv::kOk);
    ASSERT_EQ(router.RouteNodes(s, t, plain, &b).code, fv::kOk);
    ASSERT_EQ(a.found, b.found) << "disagreement on reachability " << s << " -> " << t;
    if (!a.found) continue;
    ++routed;
    EXPECT_NEAR(a.seconds, b.seconds, std::max(1e-6, b.seconds * 1e-9))
        << "cost differs " << s << " -> " << t;
    EXPECT_EQ(a.nodes.front(), s);
    EXPECT_EQ(a.nodes.back(), t);

    // And the restrictions have to be doing something, or this proves nothing.
    Route free_route;
    ASSERT_EQ(unrestricted_router.RouteNodes(s, t, bidi, &free_route).code, fv::kOk);
    if (free_route.found) {
      EXPECT_GE(a.seconds, free_route.seconds - std::max(1e-6, free_route.seconds * 1e-9));
      if (a.seconds > free_route.seconds + 1e-6) ++differed_from_unrestricted;
    }
  }
  EXPECT_GT(routed, 50) << "the fixture must actually connect most pairs";
  EXPECT_GT(differed_from_unrestricted, 0)
      << "no route was bent by a restriction, so nothing here was tested";
}

TEST(Router, BidirectionalMatchesDijkstraEverywhere) {
  SKIP_WITHOUT_KIAWAH();
  const RoadGraph g = BuildKiawah();
  ASSERT_GT(g.node_count(), 1000u);
  const Router router(g);

  RouteOptions bidi;
  RouteOptions plain;
  plain.bidirectional = false;

  int routed = 0;
  int64_t bidi_expanded = 0, plain_expanded = 0;
  // A spread of pairs across the whole graph, deterministic on purpose.
  for (uint32_t i = 0; i < 200; ++i) {
    const uint32_t s = (i * 7919u) % g.node_count();
    const uint32_t t = (i * 104729u + 17u) % g.node_count();
    if (s == t) continue;

    Route a, b;
    ASSERT_EQ(router.RouteNodes(s, t, bidi, &a).code, fv::kOk);
    ASSERT_EQ(router.RouteNodes(s, t, plain, &b).code, fv::kOk);
    ASSERT_EQ(a.found, b.found) << "disagreement on reachability " << s << " -> " << t;
    if (!a.found) continue;

    ++routed;
    // Same optimum. Both accumulate the same floats in the same order per
    // arc, but the arc order differs, so allow a relative epsilon.
    EXPECT_NEAR(a.seconds, b.seconds, std::max(1e-6, b.seconds * 1e-9))
        << "cost differs " << s << " -> " << t;
    EXPECT_NEAR(a.length_m, b.length_m, std::max(1e-3, b.length_m * 1e-6));
    EXPECT_EQ(a.nodes.front(), s);
    EXPECT_EQ(a.nodes.back(), t);
    EXPECT_EQ(a.nodes.size(), b.nodes.size());

    bidi_expanded += a.nodes_expanded;
    plain_expanded += b.nodes_expanded;
  }
  EXPECT_GT(routed, 50) << "the fixture must actually connect most pairs";

  // The whole reason the two-frontier search exists.
  EXPECT_LT(bidi_expanded, plain_expanded);
}

TEST(Router, EachMetricMinimisesItsOwnQuantity) {
  SKIP_WITHOUT_KIAWAH();
  const RoadGraph g = BuildKiawah();
  const Router router(g);

  RouteOptions by_time;
  RouteOptions by_distance;
  by_distance.metric = fv::routing::RouteMetric::kDistance;

  // 200 pairs, not the 60 this started with: the 2026-08-12 extract refresh
  // widened the graph faster than it connected it, so the same generator now
  // lands about a quarter of its pairs in one component instead of a third —
  // 15 comparisons out of 60, below the floor. The floor is what makes this
  // test mean anything (an invariant nothing exercises always holds), so the
  // sample got bigger rather than the floor smaller. Same loop and same floor
  // as BidirectionalMatchesDijkstraEverywhere above, which is the shape any
  // future refresh should keep.
  int compared = 0;
  for (uint32_t i = 0; i < 200; ++i) {
    const uint32_t s = (i * 7919u) % g.node_count();
    const uint32_t t = (i * 104729u + 17u) % g.node_count();
    if (s == t) continue;
    Route fast, short_route;
    ASSERT_EQ(router.RouteNodes(s, t, by_time, &fast).code, fv::kOk);
    ASSERT_EQ(router.RouteNodes(s, t, by_distance, &short_route).code, fv::kOk);
    if (!fast.found) continue;
    ASSERT_TRUE(short_route.found);
    ++compared;
    // Each metric is optimal for its own quantity, so neither can beat the
    // other at its own game (equality when the two pick the same path).
    EXPECT_LE(fast.seconds, short_route.seconds + 1e-6);
    EXPECT_LE(short_route.length_m, fast.length_m + 1e-3);
  }
  EXPECT_GT(compared, 40);
}

TEST(Router, RoutesOverASavedAndReloadedGraph) {
  SKIP_WITHOUT_KIAWAH();
  const RoadGraph built = BuildKiawah();
  const fs::path path = ScratchDir() / "kiawah.fvroad";
  ASSERT_EQ(built.Save(path.string()).code, fv::kOk);

  RoadGraph loaded;
  ASSERT_EQ(RoadGraph::Load(path.string(), &loaded).code, fv::kOk);

  const Router from_memory(built);
  const Router from_disk(loaded);
  int checked = 0;
  for (uint32_t i = 0; i < 25; ++i) {
    const uint32_t s = (i * 7919u) % built.node_count();
    const uint32_t t = (i * 104729u + 17u) % built.node_count();
    if (s == t) continue;
    Route a, b;
    ASSERT_EQ(from_memory.RouteNodes(s, t, {}, &a).code, fv::kOk);
    ASSERT_EQ(from_disk.RouteNodes(s, t, {}, &b).code, fv::kOk);
    ASSERT_EQ(a.found, b.found);
    if (!a.found) continue;
    ++checked;
    EXPECT_DOUBLE_EQ(a.seconds, b.seconds);
    EXPECT_EQ(a.nodes, b.nodes);
  }
  EXPECT_GT(checked, 5);
}

TEST(Router, LegsSummariseTheRouteWithoutLosingLength) {
  SKIP_WITHOUT_KIAWAH();
  const RoadGraph g = BuildKiawah();
  const Router router(g);

  // The long haul across the island, snapped from geographic points the way
  // the app will ask for it.
  RouteOptions options;
  options.snap_meters = 3000.0;
  Route route;
  ASSERT_EQ(
      router.Route(fv::GeoPoint{32.590, -80.130}, fv::GeoPoint{32.640, -80.005}, options, &route)
          .code,
      fv::kOk);
  ASSERT_TRUE(route.found);
  EXPECT_GT(route.length_m, 5000.0);

  double leg_total = 0.0;
  double leg_seconds = 0.0;
  for (const fv::routing::RouteLeg& leg : route.legs) {
    EXPECT_FALSE(leg.klass.empty());
    leg_total += leg.length_m;
    leg_seconds += leg.seconds;
  }
  EXPECT_NEAR(leg_total, route.length_m, 1.0);
  EXPECT_NEAR(leg_seconds, route.seconds, 1e-6);
  EXPECT_LT(route.legs.size(), route.nodes.size());
}

// A bicycle rides what a car may not, and skips what it may not itself.
TEST(Router, CycleOnlyPrefersTheCyclewayAndRefusesBicycleNo) {
  const RoadGraph g = BuildCycleFixture();
  const Router router(g);
  const uint32_t n1 = NodeForOsmId(g, 1);
  const uint32_t n3 = NodeForOsmId(g, 3);

  RouteOptions drive;
  Route by_car;
  ASSERT_EQ(router.RouteNodes(n1, n3, drive, &by_car).code, fv::kOk);
  ASSERT_TRUE(by_car.found);
  ASSERT_FALSE(by_car.legs.empty());
  EXPECT_EQ(by_car.legs.front().name, "The Long Way");

  RouteOptions bike;
  bike.cycle_only = true;
  Route by_bike;
  ASSERT_EQ(router.RouteNodes(n1, n3, bike, &by_bike).code, fv::kOk);
  ASSERT_TRUE(by_bike.found);
  ASSERT_FALSE(by_bike.legs.empty());
  // Not "No Bikes", even though it is the shortest of the three.
  EXPECT_EQ(by_bike.legs.front().name, "The Bike Path");
  EXPECT_LT(by_bike.length_m, by_car.length_m);
}

// The bicycle filter is a property of the QUERY, not only of the build: the
// same two graphs must give the same bicycle route.
TEST(Router, CycleOnlyRoutesTheSameOnAGeneralAndOnACycleOnlyGraph) {
  const RoadGraph general = BuildCycleFixture(false);
  const RoadGraph bikes_only = BuildCycleFixture(true);
  // The build dropped the bicycle=no path outright.
  EXPECT_LT(bikes_only.arc_count(), general.arc_count());

  RouteOptions bike;
  bike.cycle_only = true;
  Route a, b;
  ASSERT_EQ(Router(general)
                .RouteNodes(NodeForOsmId(general, 1), NodeForOsmId(general, 3), bike, &a)
                .code,
            fv::kOk);
  ASSERT_EQ(Router(bikes_only)
                .RouteNodes(NodeForOsmId(bikes_only, 1), NodeForOsmId(bikes_only, 3), bike, &b)
                .code,
            fv::kOk);
  ASSERT_TRUE(a.found);
  ASSERT_TRUE(b.found);
  EXPECT_NEAR(a.length_m, b.length_m, 1.0);
  EXPECT_NEAR(a.seconds, b.seconds, 1e-6);
}

// Same argument as the bicycle: legs do not go faster because the street they
// are on is posted at 35 km/h. A walking route is timed at walking pace.
TEST(Router, WalkingReportsSecondsAtWalkingSpeedNotTheDrivingClock) {
  const RoadGraph g = BuildCycleFixture();
  const Router router(g);
  const uint32_t n1 = NodeForOsmId(g, 1);
  const uint32_t n3 = NodeForOsmId(g, 3);

  RouteOptions walk;
  walk.driving = false;
  Route on_foot;
  ASSERT_EQ(router.RouteNodes(n1, n3, walk, &on_foot).code, fv::kOk);
  ASSERT_TRUE(on_foot.found);
  EXPECT_NEAR(on_foot.seconds, on_foot.length_m / (5.0 / 3.6), 1e-6);

  double leg_seconds = 0.0;
  for (const fv::routing::RouteLeg& leg : on_foot.legs) leg_seconds += leg.seconds;
  EXPECT_NEAR(leg_seconds, on_foot.seconds, 1e-6);

  // And it is genuinely slower than driving the same ground would be.
  RouteOptions drive;
  Route by_car;
  ASSERT_EQ(router.RouteNodes(n1, n3, drive, &by_car).code, fv::kOk);
  ASSERT_TRUE(by_car.found);
  EXPECT_GT(on_foot.seconds, by_car.seconds);
}

// Reported duration is bike time, not the driving clock the arcs carry.
TEST(Router, CycleOnlyReportsSecondsAtBicycleSpeed) {
  const RoadGraph g = BuildCycleFixture();
  const Router router(g);
  RouteOptions bike;
  bike.cycle_only = true;
  Route route;
  ASSERT_EQ(router.RouteNodes(NodeForOsmId(g, 1), NodeForOsmId(g, 3), bike, &route).code, fv::kOk);
  ASSERT_TRUE(route.found);
  EXPECT_NEAR(route.seconds, route.length_m / (15.0 / 3.6), 1e-6);

  double leg_seconds = 0.0;
  for (const fv::routing::RouteLeg& leg : route.legs) leg_seconds += leg.seconds;
  EXPECT_NEAR(leg_seconds, route.seconds, 1e-6);
}

// ---------------------------------------------------------------------------
// Private access (O5b)
// ---------------------------------------------------------------------------

TEST(RouterPrivateAccess, APrivateShortcutLosesToAPublicDetour) {
  const RoadGraph g = BuildPrivateFixture();
  const Router router(g);
  const uint32_t n1 = NodeForOsmId(g, 1);
  const uint32_t n3 = NodeForOsmId(g, 3);

  Route penalised;
  ASSERT_EQ(router.RouteNodes(n1, n3, RouteOptions{}, &penalised).code, fv::kOk);
  ASSERT_TRUE(penalised.found);
  ASSERT_FALSE(penalised.legs.empty());
  EXPECT_EQ(penalised.legs.front().name, "Public Way");

  // ... and the shortcut is genuinely THERE, not deleted: price it at par and
  // the search takes it, which is what tells the two failures apart.
  RouteOptions free_gate;
  free_gate.private_penalty = 1.0;
  Route direct;
  ASSERT_EQ(router.RouteNodes(n1, n3, free_gate, &direct).code, fv::kOk);
  ASSERT_TRUE(direct.found);
  ASSERT_FALSE(direct.legs.empty());
  EXPECT_EQ(direct.legs.front().name, "Gated Lane");
  EXPECT_LT(direct.length_m, penalised.length_m);
}

// The defect this exists for: on a gated community every street is private,
// so deleting them left driving with no network at all and a two-point route
// simply failed.
TEST(RouterPrivateAccess, AddressBehindTheGateIsStillReachable) {
  const RoadGraph g = BuildPrivateFixture();
  const Router router(g);
  Route route;
  ASSERT_EQ(
      router.RouteNodes(NodeForOsmId(g, 1), NodeForOsmId(g, 5), RouteOptions{}, &route).code,
      fv::kOk);
  ASSERT_TRUE(route.found);
  // It had to use the cul-de-sac; there is no other way to n5.
  bool used_private = false;
  for (const fv::routing::RouteLeg& leg : route.legs) {
    if (leg.name == "Cul De Sac") used_private = true;
  }
  EXPECT_TRUE(used_private);

  // The penalty is a preference, not a clock: what is reported is the real
  // drive, the same rule the bicycle class weights follow.
  double leg_seconds = 0.0, leg_metres = 0.0;
  for (const fv::routing::RouteLeg& leg : route.legs) {
    leg_seconds += leg.seconds;
    leg_metres += leg.length_m;
  }
  EXPECT_NEAR(leg_seconds, route.seconds, 1e-6);
  EXPECT_NEAR(leg_metres, route.length_m, 1e-3);
  EXPECT_NEAR(route.seconds, route.length_m / (35.0 * 1000.0 / 3600.0), 1.0);
}

// `private` softening must not soften a per-mode `no`. Kiawah enforces
// bicycle=no — bicycles belong on the private cycleways — and that is exactly
// what --ignore-access threw away along with the private, which is why it was
// never the fix.
TEST(RouterPrivateAccess, APerModeDenialStillBitesOnAPrivateWay) {
  const RoadGraph g = BuildPrivateFixture();
  const Router router(g);
  const uint32_t n5 = NodeForOsmId(g, 5);
  const uint32_t n6 = NodeForOsmId(g, 6);

  Route by_car;
  ASSERT_EQ(router.RouteNodes(n5, n6, RouteOptions{}, &by_car).code, fv::kOk);
  EXPECT_TRUE(by_car.found);

  RouteOptions bike;
  bike.cycle_only = true;
  Route by_bike;
  ASSERT_EQ(router.RouteNodes(n5, n6, bike, &by_bike).code, fv::kOk);
  EXPECT_FALSE(by_bike.found);
}

// Kiawah, honouring access as it now is: the app's own default route (Ruddy
// Turnstone to beach access 12) has to come back. Before O5b this graph had
// no connected driving network and the app shipped an --ignore-access one to
// hide it.
TEST(RouterPrivateAccess, KiawahsDefaultRouteWorksOnTheStrictGraph) {
  SKIP_WITHOUT_KIAWAH();
  RoadGraph g;
  const fv::Status s = fv::routing::BuildRoadGraph(KiawahInputs(), {}, &g, nullptr);
  ASSERT_EQ(s.code, fv::kOk) << s.message;
  const Router router(g);

  const fv::GeoPoint from{32.6044007, -80.1083007};  // Ruddy Turnstone
  const fv::GeoPoint to{32.5957369, -80.1097501};    // Boardwalk 12 at Eugenia
  Route route;
  ASSERT_EQ(router.Route(from, to, RouteOptions{}, &route).code, fv::kOk);
  ASSERT_TRUE(route.found);
  EXPECT_LT(route.start_offset_m, 1.0);  // both ends are exact junctions
  EXPECT_LT(route.end_offset_m, 1.0);
  EXPECT_GT(route.length_m, 1000.0);
  EXPECT_LT(route.length_m, 20000.0);
}

// The penalty is new cost structure, and the two searches must still agree
// under it — an asymmetry between the frontiers would show up here first.
TEST(RouterPrivateAccess, BidirectionalMatchesDijkstraWithThePenaltyApplied) {
  SKIP_WITHOUT_KIAWAH();
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph(KiawahInputs(), {}, &g, nullptr).code, fv::kOk);
  const Router router(g);

  int compared = 0;
  for (uint32_t s = 0; s < g.node_count(); s += 43) {
    for (uint32_t t = 3; t < g.node_count(); t += 97) {
      RouteOptions bi;
      RouteOptions uni;
      uni.bidirectional = false;
      Route a, b;
      ASSERT_EQ(router.RouteNodes(s, t, bi, &a).code, fv::kOk);
      ASSERT_EQ(router.RouteNodes(s, t, uni, &b).code, fv::kOk);
      ASSERT_EQ(a.found, b.found) << s << " -> " << t;
      if (!a.found) continue;
      ++compared;
      EXPECT_NEAR(a.seconds, b.seconds, std::max(1e-6, b.seconds * 1e-9)) << s << " -> " << t;
      EXPECT_NEAR(a.length_m, b.length_m, 1e-3) << s << " -> " << t;
    }
  }
  EXPECT_GT(compared, 50);
}

// ---------------------------------------------------------------------------
// Ordered stops (O5d)
// ---------------------------------------------------------------------------

// Both searches must agree on a via route as they do on a plain one. This is
// the test that would catch a seed applied to the forward frontier and not to
// the meeting, which is exactly the mistake the shape of the code invites.
Route RouteViaBothWays(const Router& router, const std::vector<uint32_t>& stops,
                       RouteOptions options) {
  options.bidirectional = true;
  Route bidi;
  EXPECT_EQ(router.RouteNodesVia(stops, options, &bidi).code, fv::kOk);
  options.bidirectional = false;
  Route plain;
  EXPECT_EQ(router.RouteNodesVia(stops, options, &plain).code, fv::kOk);

  EXPECT_EQ(bidi.found, plain.found);
  if (bidi.found && plain.found) {
    EXPECT_NEAR(bidi.seconds, plain.seconds, std::max(1e-6, plain.seconds * 1e-9));
    EXPECT_EQ(bidi.nodes, plain.nodes);
    EXPECT_EQ(bidi.u_turn_stops, plain.u_turn_stops);
  }
  return bidi;
}

// The seam has to cost nothing: two stops is the same request as a route, so
// it must come back the same route, node for node.
TEST(RouterVia, TwoStopsIsExactlyTheTwoPointRoute) {
  SKIP_WITHOUT_KIAWAH();
  const RoadGraph g = BuildKiawah();
  const Router router(g);
  int compared = 0;
  for (uint32_t s = 0; s < g.node_count(); s += 211) {
    for (uint32_t t = 5; t < g.node_count(); t += 307) {
      Route pair, via;
      ASSERT_EQ(router.RouteNodes(s, t, {}, &pair).code, fv::kOk);
      ASSERT_EQ(router.RouteNodesVia({s, t}, {}, &via).code, fv::kOk);
      ASSERT_EQ(pair.found, via.found) << s << " -> " << t;
      if (!pair.found) continue;
      ++compared;
      EXPECT_EQ(pair.nodes, via.nodes) << s << " -> " << t;
      EXPECT_NEAR(pair.length_m, via.length_m, 1e-6);
      EXPECT_NEAR(pair.seconds, via.seconds, 1e-6);
      EXPECT_TRUE(via.u_turn_stops.empty());
      // Both stops are reported even when there is nothing in between.
      ASSERT_EQ(via.stop_geometry_index.size(), 2u);
      EXPECT_EQ(via.stop_geometry_index[0], 0u);
      EXPECT_EQ(via.stop_geometry_index[1], via.geometry.size() - 1);
    }
  }
  EXPECT_GT(compared, 20);
}

// The complaint this whole slice is about: asked to go via a point, the route
// drives out to it, turns round, and comes back the way it came.
TEST(RouterVia, AStopIsPassedThroughRatherThanTurnedRoundAt) {
  const RoadGraph g = BuildViaFixture();
  const Router router(g);
  const uint32_t w = NodeForOsmId(g, 1), j = NodeForOsmId(g, 2);
  const uint32_t e = NodeForOsmId(g, 3), n = NodeForOsmId(g, 4);

  // Routed as two independent pairs, the second leg's cheapest move is
  // straight back down the road the first arrived on.
  Route leg1, leg2;
  ASSERT_EQ(router.RouteNodes(w, e, {}, &leg1).code, fv::kOk);
  ASSERT_EQ(router.RouteNodes(e, n, {}, &leg2).code, fv::kOk);
  ASSERT_TRUE(leg1.found && leg2.found);
  ASSERT_EQ(leg2.nodes.size(), 3u);
  EXPECT_EQ(leg2.nodes[1], j) << "the fixture is meant to invite the U-turn";

  // Through-routed, it does not: the long way south is four times as far and
  // is taken anyway, because a stop is a place the route goes THROUGH.
  const Route via = RouteViaBothWays(router, {w, e, n}, {});
  ASSERT_TRUE(via.found);
  EXPECT_TRUE(via.u_turn_stops.empty()) << "there was another way out";
  EXPECT_EQ(std::count(via.nodes.begin(), via.nodes.end(), j), 1)
      << "J is passed once, on the way out";
  EXPECT_GT(via.length_m, leg1.length_m + leg2.length_m);

  // And the preference is a preference: turn it off and the U-turn is back.
  RouteOptions allow;
  allow.allow_u_turn_at_stops = true;
  const Route turned = RouteViaBothWays(router, {w, e, n}, allow);
  ASSERT_TRUE(turned.found);
  EXPECT_EQ(std::count(turned.nodes.begin(), turned.nodes.end(), j), 2);
  EXPECT_NEAR(turned.length_m, leg1.length_m + leg2.length_m, 1e-6);
  EXPECT_TRUE(turned.u_turn_stops.empty()) << "nothing HAD to U-turn; it was allowed to";
}

// A stop at the end of a cul-de-sac has no other way out, so the preference
// yields — and says which stop it yielded at rather than doing it silently.
TEST(RouterVia, AStopWithNoOtherWayOutKeepsItsUTurnAndReportsIt) {
  const RoadGraph g = BuildViaFixture();
  const Router router(g);
  const uint32_t w = NodeForOsmId(g, 1), j = NodeForOsmId(g, 2);
  const uint32_t spur = NodeForOsmId(g, 7), n = NodeForOsmId(g, 4);

  const Route via = RouteViaBothWays(router, {w, spur, n}, {});
  ASSERT_TRUE(via.found);
  ASSERT_EQ(via.u_turn_stops.size(), 1u);
  EXPECT_EQ(via.u_turn_stops[0], 1u) << "leg 1 leaves stop 1, the spur";
  // Out to the spur and back to J, which is the only thing it could do.
  EXPECT_EQ(std::count(via.nodes.begin(), via.nodes.end(), j), 2);

  // Allowing U-turns outright reaches the same route, but nothing "had to".
  RouteOptions allow;
  allow.allow_u_turn_at_stops = true;
  const Route same = RouteViaBothWays(router, {w, spur, n}, allow);
  ASSERT_TRUE(same.found);
  EXPECT_EQ(same.nodes, via.nodes);
  EXPECT_TRUE(same.u_turn_stops.empty());
}

// The other half of "not independent pairs", and the one a driver would be
// fined for: two legs joined at a junction make a turn, and a sign at that
// junction has to bind it. Nothing but the state carried across the stop
// makes this happen.
TEST(RouterVia, SignageBindsOnTheTurnMadeAtAStop) {
  const RoadGraph g = BuildCrossroads("router-cross-via.osm",
                                      RestrictionRelation("no_left_turn", 303));
  ASSERT_EQ(g.restriction_count(), 1u);
  const Router router(g);
  const uint32_t s = NodeForOsmId(g, 2), c = NodeForOsmId(g, 1);
  const uint32_t nn = NodeForOsmId(g, 3), w = NodeForOsmId(g, 4);

  // Stopping AT the junction and then leaving it is the forbidden left turn,
  // and routed as two pairs it would be made: leg two starts at C knowing
  // nothing about what arrived there.
  Route second;
  ASSERT_EQ(router.RouteNodes(c, w, {}, &second).code, fv::kOk);
  ASSERT_EQ(second.nodes.size(), 2u) << "on its own the turn is unconstrained";

  const Route via = RouteViaBothWays(router, {s, c, w}, {});
  ASSERT_TRUE(via.found);
  ASSERT_EQ(via.nodes.size(), 4u);
  EXPECT_EQ(via.nodes[1], c);
  EXPECT_EQ(via.nodes[2], nn) << "round the bypass, as the sign says";
  EXPECT_EQ(via.nodes[3], w);

  // The signs are not the U-turn preference and do not answer to it.
  RouteOptions allow;
  allow.allow_u_turn_at_stops = true;
  const Route still = RouteViaBothWays(router, {s, c, w}, allow);
  ASSERT_TRUE(still.found);
  EXPECT_EQ(still.nodes, via.nodes);

  // --ignore-turns still takes them away, on a via route as on a plain one.
  RouteOptions ignore;
  ignore.honor_turn_restrictions = false;
  const Route unsigned_route = RouteViaBothWays(router, {s, c, w}, ignore);
  ASSERT_TRUE(unsigned_route.found);
  EXPECT_EQ(unsigned_route.nodes.size(), 3u);
}

// A stop is a request, not a road feature: dropping one in the middle of a
// street must not cut that street's leg summary in two.
TEST(RouterVia, AStopMidRoadDoesNotSplitTheRoadsLeg) {
  const RoadGraph g = BuildViaFixture();
  const Router router(g);
  const uint32_t w = NodeForOsmId(g, 1), j = NodeForOsmId(g, 2), e = NodeForOsmId(g, 3);

  Route straight;
  ASSERT_EQ(router.RouteNodes(w, e, {}, &straight).code, fv::kOk);
  ASSERT_EQ(straight.legs.size(), 1u);

  const Route via = RouteViaBothWays(router, {w, j, e}, {});
  ASSERT_TRUE(via.found);
  ASSERT_EQ(via.legs.size(), 1u) << "one road, one leg, stop or no stop";
  EXPECT_EQ(via.legs[0].name, "Main Street");
  EXPECT_NEAR(via.length_m, straight.length_m, 1e-6);
  EXPECT_NEAR(via.seconds, straight.seconds, 1e-6);
}

// stop_geometry_index is what a UI marks the stops with, so it has to point at
// the stop's own position and not near it.
TEST(RouterVia, EachStopIndexesItsOwnPointInTheGeometry) {
  const RoadGraph g = BuildViaFixture();
  const Router router(g);
  const std::vector<uint32_t> stops = {NodeForOsmId(g, 1), NodeForOsmId(g, 2),
                                       NodeForOsmId(g, 3), NodeForOsmId(g, 4)};
  const Route via = RouteViaBothWays(router, stops, {});
  ASSERT_TRUE(via.found);
  ASSERT_EQ(via.stop_geometry_index.size(), stops.size());
  EXPECT_EQ(via.stop_geometry_index.front(), 0u);
  EXPECT_EQ(via.stop_geometry_index.back(), via.geometry.size() - 1);
  for (size_t i = 0; i < stops.size(); ++i) {
    ASSERT_LT(via.stop_geometry_index[i], via.geometry.size()) << "stop " << i;
    const fv::GeoPoint& at = via.geometry[via.stop_geometry_index[i]];
    const fv::GeoPoint want = g.location(stops[i]);
    EXPECT_NEAR(at.lat, want.lat, 1e-9) << "stop " << i;
    EXPECT_NEAR(at.lon, want.lon, 1e-9) << "stop " << i;
  }
  EXPECT_EQ(via.stop_nodes, stops);
}

// All or nothing, and the "nothing" names the pair that broke it — otherwise
// a caller cannot tell a bad stop from a bad request.
TEST(RouterVia, AnUnreachablePairNamesItselfAndVoidsTheRoute) {
  const fs::path path = ScratchDir() / "via-islands.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.70" lon="-80.00"/>
 <node id="2" lat="32.70" lon="-80.01"/>
 <node id="3" lat="33.70" lon="-80.00"/>
 <node id="4" lat="33.70" lon="-80.01"/>
 <way id="1"><nd ref="1"/><nd ref="2"/><tag k="highway" v="residential"/></way>
 <way id="2"><nd ref="3"/><nd ref="4"/><tag k="highway" v="residential"/></way>
</osm>
)";
  }
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({path.string()}, {}, &g, nullptr).code, fv::kOk);
  const Router router(g);
  const uint32_t a = NodeForOsmId(g, 1), b = NodeForOsmId(g, 2);
  const uint32_t c = NodeForOsmId(g, 3);

  Route route;
  ASSERT_EQ(router.RouteNodesVia({a, b, c}, {}, &route).code, fv::kOk)
      << "unreachable is an answer, not an error";
  EXPECT_FALSE(route.found);
  EXPECT_EQ(route.unreachable_leg, 1u) << "leg 1 is b -> c, the one that crosses";
  EXPECT_TRUE(route.geometry.empty()) << "half a through route is not a through route";
  EXPECT_EQ(route.stop_nodes.size(), 3u) << "which stops were asked for is still worth knowing";

  Route ok;
  ASSERT_EQ(router.RouteNodesVia({a, b, a}, {}, &ok).code, fv::kOk);
  EXPECT_TRUE(ok.found);
  EXPECT_EQ(ok.unreachable_leg, Route::kNoLeg);
}

TEST(RouterVia, StopsThatSnapNowhereNameThemselves) {
  const RoadGraph g = BuildViaFixture();
  const Router router(g);
  const std::vector<fv::GeoPoint> stops = {
      {32.7000, -80.0200}, {40.0000, -70.0000}, {32.7100, -80.0100}};
  Route route;
  const fv::Status s = router.RouteVia(stops, {}, &route);
  EXPECT_EQ(s.code, fv::kOutOfCoverage);
  EXPECT_NE(s.message.find("stop 1"), std::string::npos) << s.message;
}

TEST(RouterVia, FewerThanTwoStopsIsARequestError) {
  const RoadGraph g = BuildViaFixture();
  const Router router(g);
  Route route;
  EXPECT_EQ(router.RouteNodesVia({}, {}, &route).code, fv::kInvalidArg);
  EXPECT_EQ(router.RouteNodesVia({0}, {}, &route).code, fv::kInvalidArg);
  EXPECT_EQ(router.RouteVia({{32.70, -80.02}}, {}, &route).code, fv::kInvalidArg);
}

// Two stops on the same node is a legal request (a user double-clicked), and
// the leg between them is zero-length rather than an error.
TEST(RouterVia, RepeatedStopsAreAZeroLengthLeg) {
  const RoadGraph g = BuildViaFixture();
  const Router router(g);
  const uint32_t w = NodeForOsmId(g, 1), j = NodeForOsmId(g, 2);

  Route plain, doubled;
  ASSERT_EQ(router.RouteNodesVia({w, j}, {}, &plain).code, fv::kOk);
  ASSERT_EQ(router.RouteNodesVia({w, j, j}, {}, &doubled).code, fv::kOk);
  ASSERT_TRUE(plain.found && doubled.found);
  EXPECT_EQ(doubled.nodes, plain.nodes);
  EXPECT_NEAR(doubled.length_m, plain.length_m, 1e-6);
  ASSERT_EQ(doubled.stop_geometry_index.size(), 3u);
  EXPECT_EQ(doubled.stop_geometry_index[1], doubled.stop_geometry_index[2]);
}

// Geographic entry point, and the through route it gives has to be the same
// one the node-level call gives for the nodes those points snap to.
TEST(RouterVia, GeographicStopsSnapAndReportEveryOffset) {
  const RoadGraph g = BuildViaFixture();
  const Router router(g);
  const std::vector<fv::GeoPoint> stops = {
      {32.70005, -80.02010}, {32.70000, -80.00000}, {32.70990, -80.01000}};
  Route route;
  ASSERT_EQ(router.RouteVia(stops, {}, &route).code, fv::kOk);
  ASSERT_TRUE(route.found);
  ASSERT_EQ(route.stop_offsets_m.size(), 3u);
  for (double off : route.stop_offsets_m) EXPECT_LT(off, 50.0);
  EXPECT_GT(route.stop_offsets_m[0], 0.0) << "that point is not exactly on the junction";
  EXPECT_EQ(route.start_offset_m, route.stop_offsets_m.front());
  EXPECT_EQ(route.end_offset_m, route.stop_offsets_m.back());

  Route by_node;
  ASSERT_EQ(router.RouteNodesVia(route.stop_nodes, {}, &by_node).code, fv::kOk);
  EXPECT_EQ(by_node.nodes, route.nodes);
}

// The oracle rule, applied to the new code path over a real network: the two
// searches must agree about a through route however many stops it has.
TEST(RouterVia, BothSearchesAgreeOnKiawahThroughRoutes) {
  SKIP_WITHOUT_KIAWAH();
  const RoadGraph g = BuildKiawah();
  const Router router(g);
  // Stops taken from NEARBY indices on purpose: four nodes drawn from all
  // over the island are usually not mutually reachable (Kiawah's extract has
  // several components), and an unreachable pair tests nothing here.
  int compared = 0;
  for (uint32_t s = 0; s + 61 < g.node_count(); s += 53) {
    const std::vector<uint32_t> stops = {s, s + 7, s + 29, s + 61};
    const Route via = RouteViaBothWays(router, stops, {});
    if (!via.found) continue;
    ++compared;
    // Whatever the stops did to it, the line is still one line: consecutive
    // stop indices never go backwards, and the last one is the end of it.
    ASSERT_EQ(via.stop_geometry_index.size(), stops.size());
    for (size_t i = 1; i < via.stop_geometry_index.size(); ++i) {
      EXPECT_LE(via.stop_geometry_index[i - 1], via.stop_geometry_index[i]);
    }
    EXPECT_EQ(via.stop_geometry_index.back(), via.geometry.size() - 1);
  }
  EXPECT_GT(compared, 5);
}

// ---------------------------------------------------------------------------
// Avoiding tolls and ferries (O5e)
// ---------------------------------------------------------------------------

// The bay from the graph tests: three ways from the west shore (node 1) to the
// east (node 2), separable by drive time and in a known order —
//   Toll Bridge     9.4 km at 80 km/h  ~ 7 min   (tolled)
//   Bay Ferry      14.5 km, duration 00:45      (ferry, via an island stop)
//   Long Way Round 67 km at 65 km/h    ~ 62 min  (free, ordinary road)
// so which of the three a query takes says exactly what it refused.
const char* kBayOsm = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.7000000" lon="-80.0000000"/>
 <node id="2" lat="32.7000000" lon="-79.9000000"/>
 <node id="3" lat="32.7500000" lon="-79.9500000"/>
 <node id="4" lat="32.4000000" lon="-79.9500000"/>
 <node id="5" lat="32.7510000" lon="-79.9500000"/>
 <way id="201">
  <nd ref="1"/><nd ref="2"/>
  <tag k="highway" v="primary"/><tag k="name" v="Toll Bridge"/>
  <tag k="maxspeed" v="80"/><tag k="toll" v="yes"/>
 </way>
 <way id="202">
  <nd ref="1"/><nd ref="3"/><nd ref="2"/>
  <tag k="route" v="ferry"/><tag k="name" v="Bay Ferry"/>
  <tag k="duration" v="00:45"/>
 </way>
 <way id="203">
  <nd ref="1"/><nd ref="4"/><nd ref="2"/>
  <tag k="highway" v="secondary"/><tag k="name" v="Long Way Round"/>
 </way>
 <way id="204">
  <nd ref="3"/><nd ref="5"/>
  <tag k="highway" v="service"/><tag k="name" v="Island Landing"/>
 </way>
</osm>
)";

RoadGraph BuildBayFixture() {
  const fs::path path = ScratchDir() / "router-bay.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << kBayOsm;
  }
  RoadGraph g;
  const fv::Status s = fv::routing::BuildRoadGraph({path.string()}, {}, &g, nullptr);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  return g;
}

// Which crossing a route took, by the name of the first leg it drives.
std::string CrossingTaken(const Route& r) {
  return r.legs.empty() ? std::string("(none)") : r.legs.front().name;
}

Route CrossTheBay(const Router& router, const RoadGraph& g, const RouteOptions& options) {
  Route route;
  EXPECT_EQ(router.RouteNodes(NodeForOsmId(g, 1), NodeForOsmId(g, 2), options, &route).code,
            fv::kOk);
  return route;
}

TEST(RouterAvoid, NoPreferenceTakesTheFastestCrossingWhateverItIs) {
  const RoadGraph g = BuildBayFixture();
  const Router router(g);
  const Route route = CrossTheBay(router, g, RouteOptions{});
  ASSERT_TRUE(route.found);
  // Defaults are 1.0 / 1.0: a toll costs nothing and a ferry costs nothing,
  // so the bridge wins on the clock alone. This is what O5e must not change
  // for a caller who never asks for anything.
  EXPECT_EQ(CrossingTaken(route), "Toll Bridge");
}

TEST(RouterAvoid, ExcludingTollsAndFerriesEachLeavesTheOtherAlone) {
  const RoadGraph g = BuildBayFixture();
  const Router router(g);

  RouteOptions no_toll;
  no_toll.toll_penalty = fv::routing::kAvoidExcluded;
  const Route ferried = CrossTheBay(router, g, no_toll);
  ASSERT_TRUE(ferried.found);
  EXPECT_EQ(CrossingTaken(ferried), "Bay Ferry");

  RouteOptions no_ferry;
  no_ferry.ferry_penalty = fv::routing::kAvoidExcluded;
  const Route bridged = CrossTheBay(router, g, no_ferry);
  ASSERT_TRUE(bridged.found);
  EXPECT_EQ(CrossingTaken(bridged), "Toll Bridge");

  RouteOptions neither;
  neither.toll_penalty = fv::routing::kAvoidExcluded;
  neither.ferry_penalty = fv::routing::kAvoidExcluded;
  const Route round = CrossTheBay(router, g, neither);
  ASSERT_TRUE(round.found);
  EXPECT_EQ(CrossingTaken(round), "Long Way Round");
  // Refusing both is a real cost, not a formality.
  EXPECT_GT(round.seconds, ferried.seconds);
  EXPECT_GT(ferried.seconds, bridged.seconds);
}

// A penalty is a price, not a wall — the distinction O5b drew for `private`
// and the reason both knobs are numbers rather than flags.
TEST(RouterAvoid, APenaltyBuysADetourAndAHugeOneStillIsNotADeletion) {
  const RoadGraph g = BuildBayFixture();
  const Router router(g);

  // 1.5x on the bridge is not enough: 7 min x 1.5 still beats 45 min.
  RouteOptions mild;
  mild.toll_penalty = 1.5;
  EXPECT_EQ(CrossingTaken(CrossTheBay(router, g, mild)), "Toll Bridge");

  // 10x is: 70 min of penalised bridge against 45 of ferry.
  RouteOptions steep;
  steep.toll_penalty = 10.0;
  EXPECT_EQ(CrossingTaken(CrossTheBay(router, g, steep)), "Bay Ferry");

  // Price BOTH out of reach and the road round the bay wins — but the bridge
  // is still there: it is the only crossing on a graph that has no other, and
  // an enormous penalty must still route over it rather than fail.
  RouteOptions both;
  both.toll_penalty = 1000.0;
  both.ferry_penalty = 1000.0;
  EXPECT_EQ(CrossingTaken(CrossTheBay(router, g, both)), "Long Way Round");
}

// Excluding a ferry can disconnect the network, and that is the answer, not a
// bug: the island's only link to anywhere is the boat.
TEST(RouterAvoid, RefusingTheFerryCanLeaveAnIslandUnreachable) {
  const RoadGraph g = BuildBayFixture();
  const Router router(g);
  const uint32_t shore = NodeForOsmId(g, 1), island = NodeForOsmId(g, 5);

  Route by_boat;
  ASSERT_EQ(router.RouteNodes(shore, island, RouteOptions{}, &by_boat).code, fv::kOk);
  EXPECT_TRUE(by_boat.found);

  RouteOptions no_ferry;
  no_ferry.ferry_penalty = fv::routing::kAvoidExcluded;
  Route stranded;
  // Ok with found == false: an unreachable destination is an answer.
  ASSERT_EQ(router.RouteNodes(shore, island, no_ferry, &stranded).code, fv::kOk);
  EXPECT_FALSE(stranded.found);

  // A penalty, however large, does not strand anybody.
  RouteOptions expensive;
  expensive.ferry_penalty = 1e6;
  Route still_there;
  ASSERT_EQ(router.RouteNodes(shore, island, expensive, &still_there).code, fv::kOk);
  EXPECT_TRUE(still_there.found);
}

// The oracle rule for the new filter: an exclusion is an ARC filter, so both
// frontiers see it and the two searches must still agree.
TEST(RouterAvoid, BothSearchesAgreeWithEveryCombinationOfAvoidances) {
  const RoadGraph g = BuildBayFixture();
  const Router router(g);
  const double kSettings[] = {1.0, 3.0, 25.0, fv::routing::kAvoidExcluded};
  for (double toll : kSettings) {
    for (double ferry : kSettings) {
      RouteOptions bidi;
      bidi.toll_penalty = toll;
      bidi.ferry_penalty = ferry;
      RouteOptions uni = bidi;
      uni.bidirectional = false;
      for (uint32_t s = 0; s < g.node_count(); ++s) {
        for (uint32_t t = 0; t < g.node_count(); ++t) {
          Route a, b;
          ASSERT_EQ(router.RouteNodes(s, t, bidi, &a).code, fv::kOk);
          ASSERT_EQ(router.RouteNodes(s, t, uni, &b).code, fv::kOk);
          ASSERT_EQ(a.found, b.found) << s << "->" << t << " toll " << toll << " ferry " << ferry;
          if (a.found) EXPECT_NEAR(a.seconds, b.seconds, 1e-6) << s << "->" << t;
        }
      }
    }
  }
}

// A ferry is timed by the boat, whatever the traveller's own speed. Before
// O5e a walking profile would have "walked" the crossing at 5 km/h — three
// hours for a 45-minute sailing.
TEST(RouterAvoid, AFerryTakesItsOwnDurationUnderEveryProfile) {
  const RoadGraph g = BuildBayFixture();
  const Router router(g);
  const uint32_t shore = NodeForOsmId(g, 1), island = NodeForOsmId(g, 5);

  // Refuse the bridge so every profile has to take the boat.
  RouteOptions drive;
  drive.toll_penalty = fv::routing::kAvoidExcluded;
  RouteOptions walk = drive;
  walk.driving = false;
  RouteOptions cycle = drive;
  cycle.cycle_only = true;

  Route driven, walked, cycled;
  ASSERT_EQ(router.RouteNodes(shore, island, drive, &driven).code, fv::kOk);
  ASSERT_EQ(router.RouteNodes(shore, island, walk, &walked).code, fv::kOk);
  ASSERT_EQ(router.RouteNodes(shore, island, cycle, &cycled).code, fv::kOk);
  ASSERT_TRUE(driven.found);
  ASSERT_TRUE(walked.found);
  ASSERT_TRUE(cycled.found);

  // The crossing is the bulk of each route (the slipway is 111 m), and all
  // three report the same minutes for it.
  EXPECT_NEAR(walked.seconds, driven.seconds, 120.0);
  EXPECT_NEAR(cycled.seconds, driven.seconds, 120.0);
  // ... and that is the boat's own clock, not anybody's walking speed. At
  // 5 km/h the 7.3 km first leg alone would be over 5000 s.
  EXPECT_LT(walked.seconds, 2000.0);
}

// The same claim down the OTHER cost path. `ArcCost` has a branch for a rule
// -file profile that once reached RouteProfile::Seconds directly, past the
// one place the ferry rule lives — so the search costed the crossing at the
// profile's flat speed while the finished route reported the boat's, and the
// two disagreed by a factor of four. The flag-based test above cannot see it:
// it leaves options.profile null.
TEST(RouterAvoid, AProfileCostsAFerryByTheBoatsClockToo) {
  const RoadGraph g = BuildBayFixture();
  const Router router(g);

  // A walking profile, whose flat 5 km/h is nothing like the ferry's speed.
  std::shared_ptr<const fv::routing::RouteRules> rules;
  ASSERT_EQ(fv::routing::RouteRules::Parse(
                R"({"version":1,"default_profile":"foot","profiles":{"foot":{
                    "mode":"foot","speed":{"source":"fixed","kph":5.0},
                    "metric":"time","unlisted_classes":1.0}}})",
                "<test>", &rules)
                .code,
            fv::kOk);
  RouteOptions o;
  ASSERT_EQ(fv::routing::SelectProfile(rules, "foot", &o).code, fv::kOk);

  // Shore to shore, where the ferry and the bridge compete and the two costs
  // give DIFFERENT answers. On foot: the 45-minute crossing is 2700 s, and
  // walking the 9.4 km bridge is 6739 s — so the boat wins. Cost the crossing
  // at 5 km/h instead and it becomes 10440 s, and the walker is sent over the
  // bridge. Which leg comes back is therefore the whole assertion.
  Route route;
  ASSERT_EQ(router.RouteNodes(NodeForOsmId(g, 1), NodeForOsmId(g, 2), o, &route).code, fv::kOk);
  ASSERT_TRUE(route.found);
  ASSERT_FALSE(route.legs.empty());
  EXPECT_EQ(route.legs.front().name, "Bay Ferry")
      << "a profile costed the crossing at its own flat speed";

  // And what the route reports is the same quantity the search minimised.
  double leg_seconds = 0.0;
  for (const fv::routing::RouteLeg& leg : route.legs) leg_seconds += leg.seconds;
  EXPECT_NEAR(leg_seconds, route.seconds, 1e-6);
  EXPECT_NEAR(route.seconds, 2700.0, 90.0);
}

}  // namespace
