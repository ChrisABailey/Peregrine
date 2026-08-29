// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Snap-to-road over a REAL graph (nav plan MM5).
//
// `port/fvkit/nav/test/nav_road_snap_test.cpp` pins the snapper's rules over a
// fake network of straight lines, which is where a scoring rule belongs. This
// file is the other half: the adapter that turns an O4 `RoadGraph` into that
// network, and the whole thing driven along a genuine routed track through
// Kiawah with noise on it — which is the only place a tagging shape nobody
// thought of, or a road stored the other way round, can show up.
//
// THE LOAD-BEARING TEST IS RecoversTheRoadFromANoisyTrack, and it is written
// as a COMPARISON rather than as a threshold: the same track, the same noise
// and the same seed, snapped once with hysteresis and once without, so what it
// asserts is that the mechanism does what it is there for. A bare "the error
// is under N metres" would pass with the hysteresis deleted.

#include "fv_road_network.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "fv_road_graph.h"
#include "fv_router.h"
#include "fvkit/nav/road_snap.h"

namespace fs = std::filesystem;

namespace {

using fv::GeoPoint;
using fv::PositionFix;
using fv::RoadSnapper;
using fv::SnappedFix;
using fv::routing::RoadGraph;
using fv::routing::RoadGraphNetwork;
using fv::routing::RoadNetworkOptions;
using fv::routing::RoadSnapFilter;
using fv::routing::Route;
using fv::routing::RouteOptions;
using fv::routing::Router;

constexpr double kMetersPerDegLat = 6371008.8 * 3.14159265358979323846 / 180.0;

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

// ENUMERATED, NOT NAMED — the ledger's rule, and the same helper the other
// Routing tests carry: the extract has been re-exported with a different
// number of files twice already.
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

#define SKIP_WITHOUT_KIAWAH()                                    \
  const std::vector<std::string> kiawah_inputs = KiawahInputs(); \
  if (kiawah_inputs.empty())                                     \
  GTEST_SKIP() << "no Kiawah OSM exports in " << TestDataDir() << "/OSM"

fs::path ScratchDir() {
  fs::path dir = fs::temp_directory_path() / "fv_routing_test";
  fs::create_directories(dir);
  return dir;
}

// A crossroads with a footway beside one arm, which is the shape the class
// filter exists for. Way 101 runs west to east through the junction n3 with a
// shape point; way 102 is one-way NORTH out of it; way 104 is a footway lying
// 20 m north of way 101 and parallel to it.
const char* kSnapOsm = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.6000000" lon="-80.0900000"/>
 <node id="2" lat="32.6000000" lon="-80.0850000"/>
 <node id="3" lat="32.6000000" lon="-80.0800000"/>
 <node id="4" lat="32.6050000" lon="-80.0800000"/>
 <node id="5" lat="32.6001800" lon="-80.0900000"/>
 <node id="6" lat="32.6001800" lon="-80.0800000"/>
 <way id="101">
  <nd ref="1"/><nd ref="2"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="Marsh Road"/>
 </way>
 <way id="102">
  <nd ref="3"/><nd ref="4"/>
  <tag k="highway" v="residential"/><tag k="oneway" v="yes"/>
  <tag k="name" v="North Lane"/>
 </way>
 <way id="104">
  <nd ref="5"/><nd ref="6"/>
  <tag k="highway" v="footway"/><tag k="name" v="Boardwalk"/>
 </way>
</osm>
)";

std::shared_ptr<RoadGraph> BuildFrom(const char* osm, const char* filename) {
  const fs::path path = ScratchDir() / filename;
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << osm;
  }
  auto graph = std::make_shared<RoadGraph>();
  fv::routing::RoadGraphBuildOptions options;
  const fv::Status s =
      BuildRoadGraph({path.string()}, options, graph.get(), nullptr);
  EXPECT_TRUE(s.ok()) << s.message;
  return graph;
}

GeoPoint Offset(const GeoPoint& p, double east_m, double north_m) {
  const double cos_lat = std::cos(p.lat * 3.14159265358979323846 / 180.0);
  return GeoPoint{p.lat + north_m / kMetersPerDegLat,
                  p.lon + east_m / (kMetersPerDegLat * cos_lat)};
}

double MetersBetween(const GeoPoint& a, const GeoPoint& b) {
  const double cos_lat = std::cos(a.lat * 3.14159265358979323846 / 180.0);
  const double dx = fv::NormalizeLon(b.lon - a.lon) * kMetersPerDegLat * cos_lat;
  const double dy = (b.lat - a.lat) * kMetersPerDegLat;
  return std::sqrt(dx * dx + dy * dy);
}

// A deterministic noise source. A test that drifts a track has to drift it the
// SAME way every run, or a failure is not reproducible and a threshold is not
// a threshold.
struct Lcg {
  uint64_t state = 0x2026081712345678ull;
  double Next() {  // uniform in [-1, 1)
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<double>((state >> 11) & 0x1FFFFFFFFFFFFFull) /
               static_cast<double>(0x10000000000000ull) -
           1.0;
  }
};

// Walks a route's drawn geometry and emits a point every `step_m`, so the
// track is sampled the way a receiver samples it rather than at whatever
// spacing OSM's shape points happen to have.
std::vector<GeoPoint> Resample(const std::vector<GeoPoint>& line, double step_m) {
  std::vector<GeoPoint> out;
  if (line.size() < 2) return out;
  out.push_back(line.front());
  double carry = 0.0;
  for (size_t i = 1; i < line.size(); ++i) {
    const double seg = MetersBetween(line[i - 1], line[i]);
    if (seg <= 0.0) continue;
    double t = (step_m - carry) / seg;
    while (t <= 1.0) {
      out.push_back(GeoPoint{line[i - 1].lat + t * (line[i].lat - line[i - 1].lat),
                             line[i - 1].lon + t * (line[i].lon - line[i - 1].lon)});
      t += step_m / seg;
    }
    carry = std::fmod(carry + seg, step_m);
  }
  return out;
}

PositionFix DrivingFixAt(const GeoPoint& p) {
  PositionFix f;
  f.SetPosition(p.lat, p.lon);
  f.speed_mps = 11.0;  // ~25 mph, the island's limit
  f.has_speed = true;
  return f;
}

// ---------------------------------------------------------------------------
// The adapter
// ---------------------------------------------------------------------------

TEST(RoadGraphNetwork, OffersOneCandidatePerRoadRatherThanPerArc) {
  auto graph = BuildFrom(kSnapOsm, "snap-cross.osm");
  RoadGraphNetwork net(graph);

  // The middle of Marsh Road, 5 m north of it. The graph holds this road as
  // two mirrored arcs; a snapper offered both would call every street
  // ambiguous with itself.
  const GeoPoint on_road{32.6000000, -80.0850000};
  std::vector<fv::RoadCandidate> hits;
  net.QueryNear(Offset(on_road, 0.0, 5.0), 15.0, &hits);

  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].name, "Marsh Road");
  EXPECT_NEAR(hits[0].distance_m, 5.0, 0.5);
  EXPECT_FALSE(hits[0].one_way);
  // The road runs east-west, so its stored direction is one or the other.
  const double b = hits[0].bearing_deg;
  EXPECT_TRUE(std::fabs(b - 90.0) < 1.0 || std::fabs(b - 270.0) < 1.0) << b;
}

TEST(RoadGraphNetwork, ReportsAOneWayRoadsDirectionOfTravelWhicheverEndItIsStoredAt) {
  auto graph = BuildFrom(kSnapOsm, "snap-cross.osm");
  RoadGraphNetwork net(graph);

  // Half way up North Lane, which OSM says may only be driven NORTH. Which of
  // its two nodes the graph stores the arc at is an internal detail; the
  // direction a vehicle travels it in is not.
  const GeoPoint on_lane{32.6025000, -80.0800000};
  std::vector<fv::RoadCandidate> hits;
  net.QueryNear(on_lane, 20.0, &hits);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].name, "North Lane");
  EXPECT_TRUE(hits[0].one_way);
  EXPECT_NEAR(hits[0].bearing_deg, 0.0, 1.0);
}

TEST(RoadGraphNetwork, KeepsACarOffTheFootwayAndAWalkerOnIt) {
  auto graph = BuildFrom(kSnapOsm, "snap-cross.osm");
  // 2 m south of the boardwalk, 18 m north of the road it runs beside.
  const GeoPoint beside_boardwalk = Offset(GeoPoint{32.6000000, -80.0850000}, 0.0, 18.0);

  RoadNetworkOptions driving;  // the default filter
  RoadGraphNetwork car(graph, driving);
  std::vector<fv::RoadCandidate> hits;
  car.QueryNear(beside_boardwalk, 25.0, &hits);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].name, "Marsh Road");  // NOT the footway 2 m away

  RoadNetworkOptions all;
  all.filter = RoadSnapFilter::kAll;
  RoadGraphNetwork walker(graph, all);
  hits.clear();
  walker.QueryNear(beside_boardwalk, 25.0, &hits);
  EXPECT_EQ(hits.size(), 2u);
  EXPECT_GT(walker.indexed_arcs(), car.indexed_arcs());
}

TEST(RoadGraphNetwork, AnswersNothingWithNoGraphAndAfterTheGraphIsTakenAway) {
  RoadGraphNetwork empty;
  std::vector<fv::RoadCandidate> hits;
  empty.QueryNear(GeoPoint{32.6, -80.08}, 50.0, &hits);
  EXPECT_TRUE(hits.empty());
  EXPECT_EQ(empty.indexed_arcs(), 0u);

  RoadGraphNetwork net(BuildFrom(kSnapOsm, "snap-cross.osm"));
  ASSERT_GT(net.indexed_arcs(), 0u);
  net.SetGraph(nullptr);
  EXPECT_EQ(net.indexed_arcs(), 0u);
  net.QueryNear(GeoPoint{32.6, -80.085}, 50.0, &hits);
  EXPECT_TRUE(hits.empty());
}

TEST(RoadGraphNetwork, AnswersNothingFarOutsideItsOwnBounds) {
  RoadGraphNetwork net(BuildFrom(kSnapOsm, "snap-cross.osm"));
  std::vector<fv::RoadCandidate> hits;
  // A degree away. The index sweep clamps to the grid, so without the bounds
  // check this would project onto the nearest edge cell's roads and report a
  // distance of 100 km as if it were a candidate.
  net.QueryNear(GeoPoint{33.6, -80.085}, 50.0, &hits);
  EXPECT_TRUE(hits.empty());
}

TEST(RoadGraphNetwork, WalksTheRoadsShapeRatherThanTheChordBetweenItsEnds) {
  // A road that doglegs: the chord between its ends passes 40 m from the
  // corner, so a snapper reading only the endpoints would put a ship standing
  // ON the corner 40 m off its own road.
  const char* kDogleg = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.6000000" lon="-80.0900000"/>
 <node id="2" lat="32.6008000" lon="-80.0850000"/>
 <node id="3" lat="32.6000000" lon="-80.0800000"/>
 <way id="101">
  <nd ref="1"/><nd ref="2"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="Dogleg Drive"/>
 </way>
</osm>
)";
  RoadGraphNetwork net(BuildFrom(kDogleg, "snap-dogleg.osm"));
  std::vector<fv::RoadCandidate> hits;
  net.QueryNear(GeoPoint{32.6008000, -80.0850000}, 20.0, &hits);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_LT(hits[0].distance_m, 1.0);
  // ... and `along_m` is measured along the shape, so it is past the first leg.
  EXPECT_GT(hits[0].along_m, 400.0);
  EXPECT_LT(hits[0].along_m, hits[0].length_m);
}

// ---------------------------------------------------------------------------
// The snapper over the adapter, on a synthetic street
// ---------------------------------------------------------------------------

TEST(RoadSnapOverGraph, PutsTheShipOnTheRoadAndNamesIt) {
  auto net = std::make_shared<RoadGraphNetwork>(BuildFrom(kSnapOsm, "snap-cross.osm"));
  RoadSnapper snapper(net);

  const GeoPoint truth{32.6000000, -80.0850000};
  const SnappedFix s = snapper.Snap(DrivingFixAt(Offset(truth, 0.0, 6.0)));
  ASSERT_TRUE(s.snapped);
  EXPECT_EQ(s.road_name, "Marsh Road");
  EXPECT_LT(MetersBetween(s.position, truth), 1.0);
  EXPECT_NEAR(s.offset_m, 6.0, 0.5);
  EXPECT_GT(s.confidence, 0.5);
}

// ---------------------------------------------------------------------------
// Kiawah
// ---------------------------------------------------------------------------

TEST(RoadSnapKiawah, IndexesTheIslandsDriveableRoads) {
  SKIP_WITHOUT_KIAWAH();
  auto graph = std::make_shared<RoadGraph>();
  fv::routing::RoadGraphBuildOptions options;
  ASSERT_TRUE(BuildRoadGraph(kiawah_inputs, options, graph.get(), nullptr).ok());

  RoadGraphNetwork car(graph);
  RoadNetworkOptions all;
  all.filter = RoadSnapFilter::kAll;
  RoadGraphNetwork every(graph, all);

  EXPECT_GT(car.indexed_arcs(), 100u);
  // The island's cycleways and boardwalks are a large fraction of its ways,
  // which is exactly why a car must not snap to them.
  EXPECT_GT(every.indexed_arcs(), car.indexed_arcs());
  // One road per undirected edge: the graph holds two arcs for each.
  EXPECT_LE(every.indexed_arcs(), graph->arc_count() / 2 + 1);
}

TEST(RoadSnapKiawah, RecoversTheRoadFromANoisyTrack) {
  SKIP_WITHOUT_KIAWAH();
  auto graph = std::make_shared<RoadGraph>();
  fv::routing::RoadGraphBuildOptions build;
  ASSERT_TRUE(BuildRoadGraph(kiawah_inputs, build, graph.get(), nullptr).ok());

  // A drive across the island: two nodes far apart in the graph's own bounds.
  const fv::GeoRect b = graph->bounds();
  Router router(*graph);
  RouteOptions options;
  Route route;
  ASSERT_TRUE(router
                  .Route(GeoPoint{b.ll.lat + 0.25 * (b.ur.lat - b.ll.lat),
                                  b.ll.lon + 0.15 * (b.ur.lon - b.ll.lon)},
                         GeoPoint{b.ll.lat + 0.75 * (b.ur.lat - b.ll.lat),
                                  b.ll.lon + 0.85 * (b.ur.lon - b.ll.lon)},
                         options, &route)
                  .ok());
  ASSERT_TRUE(route.found);
  const std::vector<GeoPoint> truth = Resample(route.geometry, 15.0);
  ASSERT_GT(truth.size(), 40u);

  // The receiver: 12 m of horizontal scatter, which is an honest bad day for a
  // phone under tree cover and more than the width of the roads it is on.
  const double kNoiseM = 12.0;
  std::vector<GeoPoint> noisy;
  Lcg rng;
  noisy.reserve(truth.size());
  for (const GeoPoint& p : truth)
    noisy.push_back(Offset(p, kNoiseM * rng.Next(), kNoiseM * rng.Next()));

  auto network = std::make_shared<RoadGraphNetwork>(graph);

  struct Result {
    double raw_error = 0.0;
    double snapped_error = 0.0;
    int snapped = 0;
    int arc_changes = 0;
  };
  auto Drive = [&](const fv::RoadSnapSettings& settings) {
    RoadSnapper snapper(network);
    snapper.SetSettings(settings);
    Result r;
    uint64_t prev_arc = fv::kNoRoadArc;
    for (size_t i = 0; i < noisy.size(); ++i) {
      PositionFix f = DrivingFixAt(noisy[i]);
      // The heading a HeadingResolver would have had: the bearing of the leg
      // just travelled, one fix stale, exactly as MovingMapOverlay hands it.
      double heading = 0.0;
      bool have = false;
      if (i > 0) {
        const double east =
            fv::NormalizeLon(truth[i].lon - truth[i - 1].lon) * kMetersPerDegLat *
            std::cos(truth[i].lat * 3.14159265358979323846 / 180.0);
        const double north = (truth[i].lat - truth[i - 1].lat) * kMetersPerDegLat;
        heading = fv::NormalizeHeadingDeg(std::atan2(east, north) * 180.0 /
                                          3.14159265358979323846);
        have = true;
      }
      const SnappedFix s = snapper.Snap(f, heading, have);
      r.raw_error += MetersBetween(noisy[i], truth[i]);
      if (!s.snapped) continue;
      ++r.snapped;
      r.snapped_error += MetersBetween(s.position, truth[i]);
      if (prev_arc != fv::kNoRoadArc && s.arc != prev_arc) ++r.arc_changes;
      prev_arc = s.arc;
    }
    return r;
  };

  const Result with = Drive(fv::RoadSnapSettings());

  fv::RoadSnapSettings bare;  // nearest edge, the thing MM5 is not
  bare.stay_bonus_m = 0.0;
  bare.connected_bonus_m = 0.0;
  bare.heading_penalty_m = 0.0;
  const Result without = Drive(bare);

  // Nearly every fix finds a road (measured: 1295 of 1295)...
  EXPECT_GT(with.snapped, static_cast<int>(0.95 * noisy.size()));

  // ... and lands much nearer the road actually driven than the receiver did:
  // 9.3 m of scatter comes back as 6.0.
  //
  // AND THAT RESIDUAL IS THE RIGHT NUMBER RATHER THAN A DISAPPOINTING ONE.
  // Snapping removes the ACROSS-track error and leaves the ALONG-track one
  // untouched — a fix 10 m up the road projects onto the road 10 m up it — so
  // what is left is the mean of one axis of the noise (6 m for +/-12 uniform),
  // which is exactly what is measured. Removing that as well needs a motion
  // model rather than a projection, i.e. MM5b's Viterbi matcher, and this
  // number is the honest reason it stays optional.
  const double raw_mean = with.raw_error / noisy.size();
  const double snapped_mean = with.snapped_error / with.snapped;
  EXPECT_GT(raw_mean, 7.0);  // the noise really is on
  EXPECT_LT(snapped_mean, 7.0);
  EXPECT_LT(snapped_mean, raw_mean * 0.75);

  // THE MECHANISM: hysteresis plus the heading term change road far less often
  // than nearest-edge does over the identical track (measured: 109 against
  // 163). A drive across the island genuinely turns a few dozen times; the
  // bare snapper adds flapping on top of every one of them.
  EXPECT_LT(with.arc_changes, without.arc_changes);
}

TEST(RoadSnapKiawah, HoldsItsRoadWhileTheShipIsStoppedAtAJunction) {
  SKIP_WITHOUT_KIAWAH();
  auto graph = std::make_shared<RoadGraph>();
  fv::routing::RoadGraphBuildOptions build;
  ASSERT_TRUE(BuildRoadGraph(kiawah_inputs, build, graph.get(), nullptr).ok());
  auto network = std::make_shared<RoadGraphNetwork>(graph);

  // Find a junction: a node with three or more driveable arcs.
  uint32_t junction = 0;
  bool found = false;
  for (uint32_t n = 0; n < graph->node_count() && !found; ++n) {
    int driveable = 0;
    for (uint32_t a = graph->arc_begin(n); a < graph->arc_end(n); ++a)
      if (IsDriveable(graph->arc(a).klass)) ++driveable;
    if (driveable >= 3) {
      junction = n;
      found = true;
    }
  }
  ASSERT_TRUE(found);

  RoadSnapper snapper(network);
  // Roll up to the junction along one road, then sit there while the receiver
  // scatters. Every scattered fix is within a few metres of two or three
  // roads at once, which is the case that makes an unheld snapper walk the
  // ship up and down each of them in turn.
  const GeoPoint j = graph->location(junction);
  const SnappedFix arrived = snapper.Snap(DrivingFixAt(Offset(j, 0.0, -25.0)));
  ASSERT_TRUE(arrived.snapped);
  const uint64_t road = arrived.arc;

  Lcg rng;
  for (int i = 0; i < 20; ++i) {
    PositionFix stopped;
    const GeoPoint p = Offset(j, 8.0 * rng.Next(), 8.0 * rng.Next());
    stopped.SetPosition(p.lat, p.lon);
    stopped.speed_mps = 0.1;
    stopped.has_speed = true;
    const SnappedFix s = snapper.Snap(stopped);
    ASSERT_TRUE(s.snapped);
    EXPECT_EQ(s.arc, road) << "fix " << i;
    EXPECT_TRUE(s.held);
    EXPECT_FALSE(s.has_bearing);
  }
}

}  // namespace
