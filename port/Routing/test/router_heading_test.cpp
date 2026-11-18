/**
 * Tests for RouteOptions::start_heading_deg: the soft U-turn penalty at the
 * first stop, on a synthetic block where the short way is behind the traveller.
 */
#include "fv_router.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include "fv_road_graph.h"

namespace {

namespace fs = std::filesystem;
using fv::GeoPoint;
using fv::routing::RoadGraph;
using fv::routing::Route;
using fv::routing::RouteMetric;
using fv::routing::RouteOptions;
using fv::routing::Router;

// Main runs west to east, A (1) - B (2) - C (3), 0.005 degrees of longitude
// (469 m) per block. The loop C - D - E - A runs 55 m north of it, so going
// round from east of B to west of it is about a kilometre further. A short
// dead-end spur leaves B to the south-east, making B a junction.
const char* kBlockOsm = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.6000000" lon="-80.0100000"/>
 <node id="2" lat="32.6000000" lon="-80.0050000"/>
 <node id="3" lat="32.6000000" lon="-80.0000000"/>
 <node id="4" lat="32.6005000" lon="-80.0000000"/>
 <node id="5" lat="32.6005000" lon="-80.0100000"/>
 <node id="6" lat="32.5997000" lon="-80.0046000"/>
 <way id="100">
  <nd ref="1"/><nd ref="2"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="Main"/>
 </way>
 <way id="101">
  <nd ref="3"/><nd ref="4"/><nd ref="5"/><nd ref="1"/>
  <tag k="highway" v="residential"/><tag k="name" v="Loop"/>
 </way>
 <way id="102">
  <nd ref="2"/><nd ref="6"/>
  <tag k="highway" v="residential"/><tag k="name" v="Spur"/>
 </way>
</osm>
)";

// Main as above, A - B - C, but the only way on from C is a loop of two
// roads between C and G, about 180 m round. (One closed way would be a
// self-loop, which the graph does not keep.)
const char* kLollipopOsm = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.6000000" lon="-80.0100000"/>
 <node id="2" lat="32.6000000" lon="-80.0050000"/>
 <node id="3" lat="32.6000000" lon="-80.0000000"/>
 <node id="4" lat="32.6005000" lon="-80.0000000"/>
 <node id="7" lat="32.6003000" lon="-79.9994000"/>
 <node id="6" lat="32.5997000" lon="-80.0046000"/>
 <way id="100">
  <nd ref="1"/><nd ref="2"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="Main"/>
 </way>
 <way id="101">
  <nd ref="3"/><nd ref="4"/><nd ref="7"/>
  <tag k="highway" v="residential"/><tag k="name" v="Loop"/>
 </way>
 <way id="103">
  <nd ref="7"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="Loop"/>
 </way>
 <way id="102">
  <nd ref="2"/><nd ref="6"/>
  <tag k="highway" v="residential"/><tag k="name" v="Spur"/>
 </way>
</osm>
)";

RoadGraph BuildBlock(const char* osm = kBlockOsm) {
  const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
  const fs::path dir = fs::temp_directory_path() / "fv_routing_test" /
                       (std::string(info->test_suite_name()) + "." + info->name());
  fs::create_directories(dir);
  const fs::path path = dir / "block.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << osm;
  }
  RoadGraph g;
  const fv::Status s = fv::routing::BuildRoadGraph({path.string()}, {}, &g, nullptr);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  return g;
}

// Distance metric, so a penalty of S seconds costs S * 50 / 3.6 metres.
RouteOptions Options(double heading_deg, double u_turn_seconds, bool bidirectional) {
  RouteOptions o;
  o.metric = RouteMetric::kDistance;
  o.start_heading_deg = heading_deg;
  o.start_u_turn_seconds = u_turn_seconds;
  o.bidirectional = bidirectional;
  return o;
}

Route RouteOf(const RoadGraph& g, const GeoPoint& from, const GeoPoint& to,
              const RouteOptions& o) {
  Router router(g);
  Route r;
  const fv::Status s = router.Route(from, to, o, &r);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  EXPECT_TRUE(r.found);
  return r;
}

constexpr double kNoHeading = std::numeric_limits<double>::quiet_NaN();
constexpr double kEast = 90.0;
constexpr double kWest = 270.0;

const GeoPoint kRiderMidBlock{32.6, -80.0025};  // on Main between B and C
const GeoPoint kWestOfB{32.6, -80.0075};        // on Main between A and B
const GeoPoint kAtB{32.6, -80.0050};

class RouterHeading : public ::testing::TestWithParam<bool> {};

// Short way 469 m behind, the way round about 1.52 km: the turn-round costs
// 469 m plus the penalty, so it wins below about 75 s and loses above.
TEST_P(RouterHeading, MidRoadStartTurnsRoundOnlyWhenGoingRoundCostsMore) {
  const RoadGraph g = BuildBlock();
  const bool bidi = GetParam();

  const Route free = RouteOf(g, kRiderMidBlock, kWestOfB, Options(kNoHeading, 45.0, bidi));
  EXPECT_NEAR(free.length_m, 469.0, 5.0);
  EXPECT_FALSE(free.start_u_turn);

  const Route cheap = RouteOf(g, kRiderMidBlock, kWestOfB, Options(kEast, 30.0, bidi));
  EXPECT_NEAR(cheap.length_m, 469.0, 5.0);
  EXPECT_TRUE(cheap.start_u_turn);

  const Route dear = RouteOf(g, kRiderMidBlock, kWestOfB, Options(kEast, 150.0, bidi));
  EXPECT_NEAR(dear.length_m, 1516.0, 10.0);
  EXPECT_FALSE(dear.start_u_turn);
  EXPECT_EQ(dear.legs.front().name, "Main");
  EXPECT_EQ(dear.legs[1].name, "Loop");

  // Heading toward the destination: the short way is ahead and free.
  const Route ahead = RouteOf(g, kRiderMidBlock, kWestOfB, Options(kWest, 150.0, bidi));
  EXPECT_NEAR(ahead.length_m, 469.0, 5.0);
  EXPECT_FALSE(ahead.start_u_turn);
}

// Start and destination on the same road, 141 m apart with the destination
// behind. The way round is about 1.84 km.
TEST_P(RouterHeading, SameRoadDestinationBehindIsPricedAgainstTheWayRound) {
  const RoadGraph g = BuildBlock();
  const bool bidi = GetParam();
  const GeoPoint behind{32.6, -80.0040};

  const Route cheap = RouteOf(g, kRiderMidBlock, behind, Options(kEast, 30.0, bidi));
  EXPECT_NEAR(cheap.length_m, 141.0, 3.0);
  EXPECT_TRUE(cheap.start_u_turn);

  const Route dear = RouteOf(g, kRiderMidBlock, behind, Options(kEast, 150.0, bidi));
  EXPECT_NEAR(dear.length_m, 1844.0, 15.0);
  EXPECT_FALSE(dear.start_u_turn);
}

// Going ahead to the next junction and turning round there is still a
// U-turn, and costs the penalty plus the ride to the junction and back.
TEST_P(RouterHeading, AUTurnAtTheFirstJunctionPaysThePenaltyToo) {
  const RoadGraph g = BuildBlock();
  const bool bidi = GetParam();
  // 50 s is 694 m: without a charge at C, 469 m to C and back would beat both
  // the 1163 m turn-round and the 1516 m way round.
  const GeoPoint near_c{32.6, -80.0005};
  const Route r = RouteOf(g, near_c, kWestOfB, Options(kEast, 50.0, bidi));
  EXPECT_FALSE(r.start_u_turn);
  EXPECT_EQ(r.legs[1].name, "Loop");
}

// A start on the junction at B: 234 m behind, about 1.75 km round. The
// dead-end spur sets off 42 degrees from the heading, so it is free, but
// turning round at its end is a U-turn at a first junction.
TEST_P(RouterHeading, JunctionStartPricesEachFirstArc) {
  const RoadGraph g = BuildBlock();
  const bool bidi = GetParam();

  const Route cheap = RouteOf(g, kAtB, kWestOfB, Options(kEast, 30.0, bidi));
  EXPECT_NEAR(cheap.length_m, 234.0, 5.0);
  EXPECT_TRUE(cheap.start_u_turn);

  const Route dear = RouteOf(g, kAtB, kWestOfB, Options(kEast, 200.0, bidi));
  EXPECT_NEAR(dear.length_m, 1750.0, 15.0);
  EXPECT_FALSE(dear.start_u_turn);
}

// Going ahead, round the loop and back past the start reaches the start's own
// behind-end state more cheaply than its penalised seed. The path keeps the
// loop rather than being read back as the turn-round.
TEST_P(RouterHeading, ALoopBackPastTheStartIsKeptWhole) {
  const RoadGraph g = BuildBlock(kLollipopOsm);
  const bool bidi = GetParam();
  const Route r = RouteOf(g, kRiderMidBlock, kWestOfB, Options(kEast, 150.0, bidi));
  EXPECT_FALSE(r.start_u_turn);
  bool loop = false;
  for (const auto& leg : r.legs) loop = loop || leg.name == "Loop";
  EXPECT_TRUE(loop);
  EXPECT_GT(r.length_m, 1000.0);
}

// No heading is the route as it was, whatever the penalty says.
TEST_P(RouterHeading, NoHeadingIgnoresThePenalty) {
  const RoadGraph g = BuildBlock();
  const bool bidi = GetParam();
  RouteOptions plain;
  plain.metric = RouteMetric::kDistance;
  plain.bidirectional = bidi;
  for (const GeoPoint& from : {kRiderMidBlock, kAtB}) {
    const Route a = RouteOf(g, from, kWestOfB, plain);
    const Route b = RouteOf(g, from, kWestOfB, Options(kNoHeading, 1000.0, bidi));
    EXPECT_EQ(a.length_m, b.length_m);
    EXPECT_EQ(a.nodes, b.nodes);
    EXPECT_EQ(a.geometry.size(), b.geometry.size());
  }
}

INSTANTIATE_TEST_SUITE_P(Search, RouterHeading, ::testing::Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& p) {
                           return p.param ? "Bidirectional" : "Dijkstra";
                         });

}  // namespace
