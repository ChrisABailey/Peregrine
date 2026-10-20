// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Elevation in the route cost: the profile's hills on synthetic DEMs, the
/// direction of a step taken by the reverse search, hill_penalty as a cost-only
/// preference, and the `hills` object in a rule file.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fv_road_graph.h"
#include "fv_route_rules.h"
#include "fv_router.h"

namespace fs = std::filesystem;
using fv::routing::AddElevation;
using fv::routing::ElevationOptions;
using fv::routing::ElevationSampler;
using fv::routing::RoadGraph;
using fv::routing::Route;
using fv::routing::RouteOptions;
using fv::routing::Router;
using fv::routing::RouteRules;

namespace {

constexpr double kLat0 = 33.0;
constexpr double kLon0 = -84.0;
constexpr double kMetresPerDegLat = 111195.0;
constexpr double kPi = 3.14159265358979323846;
const double kMetresPerDegLon = kMetresPerDegLat * std::cos(kLat0 * kPi / 180.0);

double NorthM(double lat) { return (lat - kLat0) * kMetresPerDegLat; }
double EastM(double lon) { return (lon - kLon0) * kMetresPerDegLon; }

fs::path ScratchDir() {
  const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
  fs::path dir = fs::temp_directory_path() / "fv_routing_test" /
                 (std::string(info->test_suite_name()) + "." + info->name());
  fs::create_directories(dir);
  return dir;
}

struct Node {
  int id;
  double north_m;
  double east_m;
};

/// Builds a graph of residential ways (each a list of node ids) over nodes
/// placed in metres north and east of (kLat0, kLon0), then adds elevation from
/// `z(north_m, east_m)`. A null `z` leaves the graph without elevation.
RoadGraph BuildHilly(const std::vector<Node>& nodes, const std::vector<std::vector<int>>& ways,
                     double (*z)(double north_m, double east_m)) {
  const fs::path path = ScratchDir() / "hills.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<osm version=\"0.6\">\n";
    for (const Node& n : nodes) {
      char line[160];
      std::snprintf(line, sizeof line, " <node id=\"%d\" lat=\"%.8f\" lon=\"%.8f\"/>\n", n.id,
                    kLat0 + n.north_m / kMetresPerDegLat, kLon0 + n.east_m / kMetresPerDegLon);
      out << line;
    }
    int id = 1000;
    for (const auto& w : ways) {
      out << " <way id=\"" << ++id << "\">";
      for (int r : w) out << "<nd ref=\"" << r << "\"/>";
      out << "<tag k=\"highway\" v=\"residential\"/></way>\n";
    }
    out << "</osm>\n";
  }
  RoadGraph g;
  EXPECT_EQ(fv::routing::BuildRoadGraph({path.string()}, {}, &g, nullptr).code, fv::kOk);
  if (z != nullptr) {
    const ElevationSampler sample = [z](double lat, double lon, float* m) {
      *m = static_cast<float>(z(NorthM(lat), EastM(lon)));
      return true;
    };
    ElevationOptions eo;
    eo.sample_step_m = 10.0;
    EXPECT_EQ(AddElevation(sample, eo, &g).code, fv::kOk);
  }
  return g;
}

uint32_t NodeFor(const RoadGraph& g, int64_t osm_id) {
  for (uint32_t i = 0; i < g.node_count(); ++i)
    if (g.node(i).osm_id == osm_id) return i;
  ADD_FAILURE() << "no node " << osm_id;
  return 0;
}

/// A straight road due north, one node every 100 m (ids 1, 2, ...).
RoadGraph StraightNorth(double length_m, double (*z)(double, double)) {
  std::vector<Node> nodes;
  std::vector<std::vector<int>> ways;
  for (int k = 0; k * 100.0 <= length_m; ++k) {
    nodes.push_back({k + 1, k * 100.0, 0.0});
    if (k > 0) ways.push_back({k, k + 1});
  }
  return BuildHilly(nodes, ways, z);
}

RouteOptions Profile(const char* name) {
  RouteOptions o;
  EXPECT_EQ(fv::routing::SelectProfile(RouteRules::Builtin(), name, &o).code, fv::kOk);
  return o;
}

double RouteSeconds(const RoadGraph& g, int from, int to, const RouteOptions& o,
                    Route* out = nullptr) {
  Route r;
  EXPECT_EQ(Router(g).RouteNodes(NodeFor(g, from), NodeFor(g, to), o, &r).code, fv::kOk);
  EXPECT_TRUE(r.found);
  if (out != nullptr) *out = r;
  return r.seconds;
}

double Grade5(double north, double) { return 0.05 * north; }
double Grade10(double north, double) { return 0.10 * north; }
double Grade20(double north, double) { return 0.20 * north; }

/// Rolling hills, tens of metres high, with no symmetry a route could hide in.
double Rolling(double north, double east) {
  return 25.0 * std::sin(north / 170.0) * std::cos(east / 230.0) + 0.04 * east +
         12.0 * std::sin((north + 2.0 * east) / 410.0);
}

/// A 30 m round hill centred 300 m north of the origin.
double Bump(double north, double east) {
  const double dn = north - 300.0, de = east;
  return 30.0 * std::exp(-(dn * dn + de * de) / (2.0 * 110.0 * 110.0));
}

}  // namespace

// Naismith up, Langmuir down: 1 km at 5% is 50 m of climb.
TEST(RouterElevation, FootClimbAndGentleDescent) {
  const RoadGraph g = StraightNorth(1000.0, Grade5);
  ASSERT_TRUE(g.has_elevation());
  const RouteOptions foot = Profile("foot");
  EXPECT_NEAR(RouteSeconds(g, 1, 11, foot), 720.0 + 50.0 * 6.0, 0.01);
  EXPECT_NEAR(RouteSeconds(g, 11, 1, foot), 720.0 - 50.0 * 2.0, 0.01);
}

TEST(RouterElevation, SteepDescentCostsTime) {
  const RoadGraph g = StraightNorth(500.0, Grade20);
  const RouteOptions foot = Profile("foot");
  EXPECT_NEAR(RouteSeconds(g, 1, 6, foot), 360.0 + 100.0 * 6.0, 0.01);
  EXPECT_NEAR(RouteSeconds(g, 6, 1, foot), 360.0 + 100.0 * 2.0, 0.01);
}

// 1 km down at 10% on a bike: 240 s flat less 200 s of credit is under the
// 40 km/h floor of 90 s.
TEST(RouterElevation, DescentIsFlooredAtMaxKph) {
  const RoadGraph g = StraightNorth(1000.0, Grade10);
  EXPECT_NEAR(RouteSeconds(g, 11, 1, Profile("bicycle")), 1000.0 / (40.0 / 3.6), 0.01);
}

TEST(RouterElevation, NoElevationIsFlat) {
  const RoadGraph g = StraightNorth(1000.0, nullptr);
  ASSERT_FALSE(g.has_elevation());
  EXPECT_NEAR(RouteSeconds(g, 1, 11, Profile("foot")), 720.0, 0.01);
  EXPECT_NEAR(RouteSeconds(g, 11, 1, Profile("foot")), 720.0, 0.01);
}

// The car profile has no hills: elevation changes nothing.
TEST(RouterElevation, CarIgnoresHills) {
  const RoadGraph flat = StraightNorth(1000.0, nullptr);
  const RoadGraph hilly = StraightNorth(1000.0, Grade10);
  const RouteOptions car = Profile("car");
  EXPECT_DOUBLE_EQ(RouteSeconds(hilly, 1, 11, car), RouteSeconds(flat, 1, 11, car));
}

// The reverse search walks arcs against their stored order; the reported time
// must climb what those arcs descend. 100 pairs on a rolling grid, both ways.
TEST(RouterElevation, BidirectionalMatchesDijkstraOnHills) {
  constexpr int kSide = 10;
  std::vector<Node> nodes;
  std::vector<std::vector<int>> ways;
  auto id = [](int r, int c) { return 1 + r * kSide + c; };
  for (int r = 0; r < kSide; ++r)
    for (int c = 0; c < kSide; ++c) nodes.push_back({id(r, c), r * 120.0, c * 120.0});
  for (int r = 0; r < kSide; ++r)
    for (int c = 0; c < kSide; ++c) {
      if (c + 1 < kSide) ways.push_back({id(r, c), id(r, c + 1)});
      if (r + 1 < kSide) ways.push_back({id(r, c), id(r + 1, c)});
      if (r + 1 < kSide && c + 1 < kSide && (r + c) % 3 == 0)
        ways.push_back({id(r, c), id(r + 1, c + 1)});
    }
  const RoadGraph g = BuildHilly(nodes, ways, Rolling);
  ASSERT_TRUE(g.has_elevation());
  const Router router(g);

  uint32_t seed = 12345u;
  auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  int asymmetric = 0;
  for (const char* name : {"foot", "bicycle"}) {
    RouteOptions o = Profile(name);
    for (int i = 0; i < 50; ++i) {
      const int s = 1 + static_cast<int>(next() % (kSide * kSide));
      const int t = 1 + static_cast<int>(next() % (kSide * kSide));
      if (s == t) continue;
      o.bidirectional = true;
      Route bidi, plain, back;
      ASSERT_EQ(router.RouteNodes(NodeFor(g, s), NodeFor(g, t), o, &bidi).code, fv::kOk);
      ASSERT_EQ(router.RouteNodes(NodeFor(g, t), NodeFor(g, s), o, &back).code, fv::kOk);
      o.bidirectional = false;
      ASSERT_EQ(router.RouteNodes(NodeFor(g, s), NodeFor(g, t), o, &plain).code, fv::kOk);
      ASSERT_TRUE(bidi.found && plain.found);
      EXPECT_NEAR(bidi.seconds, plain.seconds, 1e-6) << name << " " << s << " -> " << t;
      EXPECT_EQ(bidi.nodes, plain.nodes) << name << " " << s << " -> " << t;
      if (std::fabs(bidi.seconds - back.seconds) > 1.0) ++asymmetric;
    }
  }
  // The hills are in effect: most routes take longer one way than the other.
  EXPECT_GT(asymmetric, 50);
}

// Straight over a 30 m hill (600 m) or round it on the flat (1400 m). The hill
// wins on time; a high hill_penalty sends the route round, and the reported
// clock for that route is the flat walk, unpenalised.
TEST(RouterElevation, HillPenaltyIsCostNotClock) {
  const std::vector<Node> nodes = {{1, 0.0, 0.0},   {2, 300.0, 0.0},   {3, 600.0, 0.0},
                                   {4, 0.0, 700.0}, {5, 600.0, 700.0}};
  const RoadGraph g = BuildHilly(nodes, {{1, 2, 3}, {1, 4}, {4, 5}, {5, 3}}, Bump);
  RouteOptions foot = Profile("foot");
  Route over;
  const double over_s = RouteSeconds(g, 1, 3, foot, &over);
  EXPECT_NEAR(over.length_m, 600.0, 1.0);
  EXPECT_GT(over_s, 432.0 + 100.0);  // climbs most of the 30 m
  EXPECT_LT(over_s, 1900.0 / (5.0 / 3.6));

  foot.hill_penalty = 20.0;
  Route round;
  const double round_s = RouteSeconds(g, 1, 3, foot, &round);
  EXPECT_GT(round_s, over_s);
  EXPECT_NEAR(round.length_m, 2000.0, 2.0);
  // Nearly flat out there; what little climb there is is reported unscaled.
  EXPECT_NEAR(round_s, round.length_m / (5.0 / 3.6), 15.0);
}

// The excess of hill_penalty over 1.0 is priced per metre climbed, whatever
// the class: inside the class weight a cycleway climb would be half price.
TEST(RouterElevation, AvoidHillsExcessIgnoresClassWeight) {
  RouteOptions bike = Profile("bicycle");
  fv::routing::RoadArc arc;
  arc.length_m = 200.0f;
  arc.climb_dm = 100;  // 10 m
  for (fv::routing::RoadClass k :
       {fv::routing::RoadClass::kCycleway, fv::routing::RoadClass::kResidential}) {
    arc.klass = k;
    bike.hill_penalty = 1.0;
    const double neutral = fv::routing::ArcCost(arc, bike);
    bike.hill_penalty = 4.0;
    EXPECT_NEAR(fv::routing::ArcCost(arc, bike) - neutral, 3.0 * 10.0 * 6.0, 1e-6);
    bike.hill_penalty = 0.5;  // below neutral reads as neutral
    EXPECT_DOUBLE_EQ(fv::routing::ArcCost(arc, bike), neutral);
  }
  // A descent is not penalised.
  std::swap(arc.climb_dm, arc.descent_dm);
  bike.hill_penalty = 1.0;
  const double down = fv::routing::ArcCost(arc, bike);
  bike.hill_penalty = 4.0;
  EXPECT_DOUBLE_EQ(fv::routing::ArcCost(arc, bike), down);
}

TEST(RouterElevation, RuleFileHills) {
  const std::string walk =
      R"("walk":{"mode":"foot","speed":{"source":"fixed","kph":5.0},"unlisted_classes":1.0,)"
      R"("hills":{"climb_s_per_m":6.0,"descent_s_per_m":2.0,"max_kph":7.0}})";
  auto parse = [](const std::string& profiles, std::shared_ptr<const RouteRules>* out) {
    return RouteRules::Parse(R"({"version":1,"default_profile":"walk","profiles":{)" + profiles +
                                 "}}",
                             "test", out);
  };
  std::shared_ptr<const RouteRules> rules;
  ASSERT_EQ(parse(walk + R"(,"hike":{"extends":"walk","hills":{"climb_s_per_m":8.0}})", &rules)
                .code,
            fv::kOk);
  const fv::routing::RouteProfile* hike = rules->Find("hike");
  ASSERT_NE(hike, nullptr);
  EXPECT_DOUBLE_EQ(hike->hills.climb_s_per_m, 8.0);
  EXPECT_DOUBLE_EQ(hike->hills.descent_s_per_m, 2.0);  // inherited
  EXPECT_DOUBLE_EQ(hike->hills.max_kph, 7.0);
  EXPECT_DOUBLE_EQ(hike->hills.steep_grade, 0.12);

  std::shared_ptr<const RouteRules> bad;
  EXPECT_NE(parse(walk + R"(,"x":{"extends":"walk","hills":{"climb":1.0}})", &bad).code, fv::kOk);
  EXPECT_NE(parse(walk + R"(,"x":{"extends":"walk","hills":{"climb_s_per_m":-1.0}})", &bad).code,
            fv::kOk);
  EXPECT_NE(parse(walk + R"(,"x":{"extends":"walk","hills":{"max_kph":0.0}})", &bad).code,
            fv::kOk);
}

// The route's climb, descent and profile on a 5% plane, both ways.
TEST(RouterElevation, ProfileOnAPlane) {
  const RoadGraph g = StraightNorth(1000.0, Grade5);
  Route up, down;
  RouteSeconds(g, 1, 11, Profile("foot"), &up);
  RouteSeconds(g, 11, 1, Profile("foot"), &down);
  ASSERT_TRUE(up.has_elevation);
  EXPECT_NEAR(up.climb_m, 50.0, 0.11);
  EXPECT_NEAR(up.descent_m, 0.0, 0.11);
  EXPECT_NEAR(down.climb_m, 0.0, 0.11);
  EXPECT_NEAR(down.descent_m, 50.0, 0.11);
  ASSERT_EQ(up.profile.size(), 11u);
  for (size_t k = 0; k < up.profile.size(); ++k) {
    EXPECT_NEAR(up.profile[k].along_m, 100.0 * k, 0.5);
    EXPECT_NEAR(up.profile[k].elev_m, 5.0 * k, 0.11);
  }
  EXPECT_NEAR(down.profile.front().elev_m, 50.0, 0.11);
  EXPECT_NEAR(down.profile.back().elev_m, 0.0, 0.11);
}

// Over a hill whose top is mid-route, climb and descent are both counted, and
// their difference is the end-to-end change.
TEST(RouterElevation, ProfileOverAHill) {
  const RoadGraph g = StraightNorth(600.0, Bump);
  Route r;
  RouteSeconds(g, 1, 7, Profile("bicycle"), &r);
  ASSERT_GE(r.profile.size(), 7u);
  const double peak = r.profile[3].elev_m;
  EXPECT_NEAR(peak, 30.0, 0.11);
  EXPECT_NEAR(r.climb_m, peak - r.profile.front().elev_m, 0.3);
  EXPECT_NEAR(r.descent_m, peak - r.profile.back().elev_m, 0.3);
  EXPECT_NEAR(r.climb_m - r.descent_m, r.profile.back().elev_m - r.profile.front().elev_m,
              0.11);
}

// Ends that snap mid-road start and finish the profile part-way along an arc.
TEST(RouterElevation, ProfileFromMidRoadEnds) {
  const RoadGraph g = StraightNorth(1000.0, Grade5);
  const fv::GeoPoint from{kLat0 + 150.0 / kMetresPerDegLat, kLon0};
  const fv::GeoPoint to{kLat0 + 850.0 / kMetresPerDegLat, kLon0};
  Route r;
  ASSERT_EQ(Router(g).Route(from, to, Profile("foot"), &r).code, fv::kOk);
  ASSERT_TRUE(r.found);
  EXPECT_NEAR(r.climb_m, 35.0, 0.2);
  EXPECT_NEAR(r.descent_m, 0.0, 0.11);
  ASSERT_GE(r.profile.size(), 2u);
  EXPECT_DOUBLE_EQ(r.profile.front().along_m, 0.0);
  EXPECT_NEAR(r.profile.front().elev_m, 7.5, 0.11);
  EXPECT_NEAR(r.profile.back().elev_m, 42.5, 0.11);
  EXPECT_NEAR(r.profile.back().along_m, r.length_m, 1e-6);
}

TEST(RouterElevation, NoElevationNoProfile) {
  const RoadGraph g = StraightNorth(1000.0, nullptr);
  Route r;
  RouteSeconds(g, 1, 11, Profile("foot"), &r);
  EXPECT_FALSE(r.has_elevation);
  EXPECT_EQ(r.climb_m, 0.0);
  EXPECT_EQ(r.descent_m, 0.0);
  EXPECT_TRUE(r.profile.empty());
}

// A multi-stop route on rolling hills: one profile for the whole route, in
// order, ending at its length, with climb minus descent equal to the change.
TEST(RouterElevation, ViaProfileIsOneSeries) {
  constexpr int kSide = 8;
  std::vector<Node> nodes;
  std::vector<std::vector<int>> ways;
  auto id = [](int r, int c) { return 1 + r * kSide + c; };
  for (int r = 0; r < kSide; ++r)
    for (int c = 0; c < kSide; ++c) nodes.push_back({id(r, c), r * 120.0, c * 120.0});
  for (int r = 0; r < kSide; ++r)
    for (int c = 0; c < kSide; ++c) {
      if (c + 1 < kSide) ways.push_back({id(r, c), id(r, c + 1)});
      if (r + 1 < kSide) ways.push_back({id(r, c), id(r + 1, c)});
    }
  const RoadGraph g = BuildHilly(nodes, ways, Rolling);
  auto at = [](double north, double east) {
    return fv::GeoPoint{kLat0 + north / kMetresPerDegLat, kLon0 + east / kMetresPerDegLon};
  };
  Route r;
  ASSERT_EQ(Router(g)
                .RouteVia({at(0, 0), at(840, 60), at(300, 840), at(840, 840)},
                          Profile("bicycle"), &r)
                .code,
            fv::kOk);
  ASSERT_TRUE(r.found);
  ASSERT_GE(r.profile.size(), 2u);
  for (size_t k = 1; k < r.profile.size(); ++k)
    EXPECT_GE(r.profile[k].along_m, r.profile[k - 1].along_m);
  EXPECT_NEAR(r.profile.back().along_m, r.length_m, 1e-6);
  EXPECT_GT(r.climb_m, 20.0);
  EXPECT_GT(r.descent_m, 20.0);
  EXPECT_NEAR(r.climb_m - r.descent_m, r.profile.back().elev_m - r.profile.front().elev_m,
              0.2);
}
