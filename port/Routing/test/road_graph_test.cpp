// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Road graph build/format tests (O4).
//
// Two fixtures. A hand-written four-way loop with one one-way link, where
// every distance and every shape point is known by construction — that is
// where the directional assertions live (a round-trip hash proves nothing
// about which way an arc's geometry runs). Then the real Kiawah Island
// exports, for scale and for the tag cases synthetic data never has.

#include "fv_road_graph.h"

#include "fvkit/nav/road_snap.h"  // ProjectOntoSegment, for the brute-force oracle

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using fv::routing::RoadClass;
using fv::routing::RoadGraph;

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

std::string KiawahPath(const char* name) {
  return (fs::path(TestDataDir()) / "OSM" / name).string();
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


fs::path ScratchDir() {
  fs::path dir = fs::temp_directory_path() / "fv_routing_test";
  fs::create_directories(dir);
  return dir;
}

// A closed loop of four junctions with two shape points on one side and a
// one-way link on another:
//
//   n1 --w1(n2,n6 interior)-- n3
//    |                         | w2, one-way n3 -> n4
//   n5 -------- w3 ---------- n4          (w4 closes n5 -> n1)
//
// Every coordinate is chosen so the two shape points are asymmetric: an arc
// walked the wrong way round yields them in the wrong order and the test
// catches it.
std::string LoopOsm() {
  return R"(<?xml version="1.0" encoding="UTF-8"?>
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
  <tag k="name" v="One Way Lane"/><tag k="maxspeed" v="25 mph"/>
 </way>
 <way id="103">
  <nd ref="4"/><nd ref="5"/>
  <tag k="highway" v="residential"/>
 </way>
 <way id="104">
  <nd ref="5"/><nd ref="1"/>
  <tag k="highway" v="residential"/>
 </way>
</osm>
)";
}

std::string WriteLoopOsm() {
  const fs::path path = ScratchDir() / "loop.osm";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << LoopOsm();
  out.close();
  return path.string();
}

// Finds the graph node carrying a given OSM node id.
uint32_t NodeForOsmId(const RoadGraph& g, int64_t osm_id) {
  for (uint32_t i = 0; i < g.node_count(); ++i) {
    if (g.node(i).osm_id == osm_id) return i;
  }
  ADD_FAILURE() << "no graph node for OSM id " << osm_id;
  return 0;
}

// The arc leaving `u` for `v`, or null.
const fv::routing::RoadArc* ArcBetween(const RoadGraph& g, uint32_t u, uint32_t v) {
  for (uint32_t a = g.arc_begin(u); a < g.arc_end(u); ++a) {
    if (g.arc(a).target == v) return &g.arc(a);
  }
  return nullptr;
}

TEST(RoadClassTable, MapsTagsAndSeparatesDriveableClasses) {
  EXPECT_EQ(fv::routing::RoadClassFromHighwayTag("motorway"), RoadClass::kMotorway);
  EXPECT_EQ(fv::routing::RoadClassFromHighwayTag("residential"), RoadClass::kResidential);
  EXPECT_EQ(fv::routing::RoadClassFromHighwayTag("footway"), RoadClass::kFootway);
  // `highway=road` is OSM's "someone saw a road here"; it is still a road.
  EXPECT_EQ(fv::routing::RoadClassFromHighwayTag("road"), RoadClass::kUnclassified);
  // Not a line anything travels along.
  EXPECT_EQ(fv::routing::RoadClassFromHighwayTag("bus_stop"), RoadClass::kNone);
  EXPECT_EQ(fv::routing::RoadClassFromHighwayTag(""), RoadClass::kNone);

  EXPECT_TRUE(fv::routing::IsDriveable(RoadClass::kResidential));
  EXPECT_FALSE(fv::routing::IsDriveable(RoadClass::kFootway));
  EXPECT_FALSE(fv::routing::IsDriveable(RoadClass::kSteps));
  EXPECT_FALSE(fv::routing::IsDriveable(RoadClass::kNone));

  EXPECT_GT(fv::routing::DefaultSpeedKph(RoadClass::kMotorway),
            fv::routing::DefaultSpeedKph(RoadClass::kResidential));
  EXPECT_STREQ(fv::routing::RoadClassName(RoadClass::kMotorwayLink), "motorway_link");
}

TEST(GreatCircle, KnownDistancesAndSymmetry) {
  // One degree of latitude is ~111.2 km on the WGS-84 mean sphere.
  EXPECT_NEAR(fv::routing::GreatCircleMeters(32.0, -80.0, 33.0, -80.0), 111195.0, 50.0);
  // A degree of longitude shrinks with the cosine of the latitude.
  EXPECT_NEAR(fv::routing::GreatCircleMeters(32.7, -80.0, 32.7, -79.0),
              111195.0 * std::cos(32.7 * M_PI / 180.0), 100.0);
  EXPECT_EQ(fv::routing::GreatCircleMeters(32.7, -80.0, 32.7, -80.0), 0.0);
  EXPECT_DOUBLE_EQ(fv::routing::GreatCircleMeters(32.0, -80.0, 33.0, -79.0),
                   fv::routing::GreatCircleMeters(33.0, -79.0, 32.0, -80.0));
}

TEST(RoadGraphBuild, NodesOnlyAtJunctionsShapePointsInBetween) {
  const std::string path = WriteLoopOsm();
  RoadGraph g;
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph({path}, {}, &g, &stats).code, fv::kOk);

  // n2 and n6 are used by one way each and are not way ends, so they are
  // shape points, not vertices. Four ways sharing four junctions = 4 edges.
  EXPECT_EQ(g.node_count(), 4u);
  EXPECT_EQ(stats.edges, 4);
  EXPECT_EQ(g.arc_count(), 8u);  // every edge is stored twice
  EXPECT_EQ(g.geometry_count(), 2u);
  EXPECT_EQ(stats.ways_kept, 4);
  EXPECT_EQ(stats.nodes_resolved, stats.nodes_needed);
  EXPECT_EQ(stats.edges_dropped_unresolved, 0);

  // Coordinates survive the trip through the int32 1e-7 fixed point.
  const uint32_t n1 = NodeForOsmId(g, 1);
  EXPECT_NEAR(g.location(n1).lat, 32.7, 1e-9);
  EXPECT_NEAR(g.location(n1).lon, -80.0, 1e-9);
}

TEST(RoadGraphBuild, ArcGeometryRunsFromOwnerToTarget) {
  const std::string path = WriteLoopOsm();
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({path}, {}, &g, nullptr).code, fv::kOk);

  const uint32_t n1 = NodeForOsmId(g, 1);
  const uint32_t n3 = NodeForOsmId(g, 3);
  const fv::routing::RoadArc* forward = ArcBetween(g, n1, n3);
  const fv::routing::RoadArc* backward = ArcBetween(g, n3, n1);
  ASSERT_NE(forward, nullptr);
  ASSERT_NE(backward, nullptr);
  ASSERT_EQ(forward->geom_count, 2u);
  ASSERT_EQ(backward->geom_count, 2u);

  // Leaving n1 (lon -80.000) for n3 (lon -80.010), the shape points must come
  // out n2 (-80.003) then n6 (-80.007) — westward. The twin arc must yield
  // them the other way round. A shared golden hash cannot see this.
  EXPECT_NEAR(g.arc_point(*forward, 0).lon, -80.003, 1e-6);
  EXPECT_NEAR(g.arc_point(*forward, 1).lon, -80.007, 1e-6);
  EXPECT_NEAR(g.arc_point(*backward, 0).lon, -80.007, 1e-6);
  EXPECT_NEAR(g.arc_point(*backward, 1).lon, -80.003, 1e-6);
  // n6 is the one off the parallel; it must not be confused with n2.
  EXPECT_NEAR(g.arc_point(*forward, 1).lat, 32.701, 1e-6);
  EXPECT_NEAR(g.arc_point(*forward, 0).lat, 32.700, 1e-6);
}

TEST(RoadGraphBuild, OnewayFlagsAreMirroredAcrossTheTwinArcs) {
  const std::string path = WriteLoopOsm();
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({path}, {}, &g, nullptr).code, fv::kOk);

  const uint32_t n3 = NodeForOsmId(g, 3);
  const uint32_t n4 = NodeForOsmId(g, 4);
  const fv::routing::RoadArc* out = ArcBetween(g, n3, n4);
  const fv::routing::RoadArc* in = ArcBetween(g, n4, n3);
  ASSERT_NE(out, nullptr);
  ASSERT_NE(in, nullptr);

  // Way 102 is `oneway=yes` and runs n3 -> n4.
  EXPECT_TRUE(out->forward());
  EXPECT_FALSE(out->backward());
  // The twin at n4 says the same fact the other way round: nothing may leave
  // n4 along it, but something may arrive.
  EXPECT_FALSE(in->forward());
  EXPECT_TRUE(in->backward());

  // A two-way edge sets both bits on both twins.
  const uint32_t n5 = NodeForOsmId(g, 5);
  const fv::routing::RoadArc* two_way = ArcBetween(g, n4, n5);
  ASSERT_NE(two_way, nullptr);
  EXPECT_TRUE(two_way->forward());
  EXPECT_TRUE(two_way->backward());
}

TEST(RoadGraphBuild, MaxSpeedInMilesPerHourIsConverted) {
  const std::string path = WriteLoopOsm();
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({path}, {}, &g, nullptr).code, fv::kOk);

  const fv::routing::RoadArc* posted =
      ArcBetween(g, NodeForOsmId(g, 3), NodeForOsmId(g, 4));
  ASSERT_NE(posted, nullptr);
  EXPECT_EQ(posted->speed_kph, 40);  // 25 mph = 40.2 km/h

  // An unposted residential road falls back to the class default.
  const fv::routing::RoadArc* unposted =
      ArcBetween(g, NodeForOsmId(g, 4), NodeForOsmId(g, 5));
  ASSERT_NE(unposted, nullptr);
  EXPECT_EQ(unposted->speed_kph, fv::routing::DefaultSpeedKph(RoadClass::kResidential));
}

TEST(RoadGraphBuild, NamesAreInternedAndZeroMeansUnnamed) {
  const std::string path = WriteLoopOsm();
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({path}, {}, &g, nullptr).code, fv::kOk);

  EXPECT_EQ(g.name(0), "");
  const fv::routing::RoadArc* named = ArcBetween(g, NodeForOsmId(g, 1), NodeForOsmId(g, 3));
  ASSERT_NE(named, nullptr);
  EXPECT_EQ(g.name(named->name), "Loop Street");

  const fv::routing::RoadArc* unnamed = ArcBetween(g, NodeForOsmId(g, 4), NodeForOsmId(g, 5));
  ASSERT_NE(unnamed, nullptr);
  EXPECT_EQ(unnamed->name, 0u);
}

TEST(RoadGraphBuild, EdgeLengthsFollowTheShapePoints) {
  const std::string path = WriteLoopOsm();
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({path}, {}, &g, nullptr).code, fv::kOk);

  // n3 -> n4 is a straight 0.005 degrees of latitude.
  const fv::routing::RoadArc* straight =
      ArcBetween(g, NodeForOsmId(g, 3), NodeForOsmId(g, 4));
  ASSERT_NE(straight, nullptr);
  EXPECT_NEAR(straight->length_m, fv::routing::GreatCircleMeters(32.700, -80.01, 32.705, -80.01),
              1.0);

  // n1 -> n3 detours north through n6, so it must be LONGER than the chord.
  const fv::routing::RoadArc* bent = ArcBetween(g, NodeForOsmId(g, 1), NodeForOsmId(g, 3));
  ASSERT_NE(bent, nullptr);
  const double chord = fv::routing::GreatCircleMeters(32.700, -80.000, 32.700, -80.010);
  EXPECT_GT(bent->length_m, chord);
  EXPECT_LT(bent->length_m, chord * 1.5);
}

TEST(RoadGraphFormat, SaveLoadRoundTripsEverything) {
  const std::string osm = WriteLoopOsm();
  RoadGraph built;
  ASSERT_EQ(fv::routing::BuildRoadGraph({osm}, {}, &built, nullptr).code, fv::kOk);

  const fs::path out = ScratchDir() / "loop.fvroad";
  ASSERT_EQ(built.Save(out.string()).code, fv::kOk);

  RoadGraph loaded;
  ASSERT_EQ(RoadGraph::Load(out.string(), &loaded).code, fv::kOk);

  ASSERT_EQ(loaded.node_count(), built.node_count());
  ASSERT_EQ(loaded.arc_count(), built.arc_count());
  ASSERT_EQ(loaded.geometry_count(), built.geometry_count());
  for (uint32_t i = 0; i < built.node_count(); ++i) {
    EXPECT_EQ(loaded.node(i).osm_id, built.node(i).osm_id);
    EXPECT_EQ(loaded.node(i).lat_e7, built.node(i).lat_e7);
    EXPECT_EQ(loaded.node(i).lon_e7, built.node(i).lon_e7);
    EXPECT_EQ(loaded.arc_begin(i), built.arc_begin(i));
  }
  for (uint32_t a = 0; a < built.arc_count(); ++a) {
    EXPECT_EQ(loaded.arc(a).target, built.arc(a).target);
    EXPECT_EQ(loaded.arc(a).flags, built.arc(a).flags);
    EXPECT_EQ(loaded.arc(a).speed_kph, built.arc(a).speed_kph);
    EXPECT_EQ(loaded.arc(a).klass, built.arc(a).klass);
    EXPECT_FLOAT_EQ(loaded.arc(a).length_m, built.arc(a).length_m);
    EXPECT_EQ(loaded.name(loaded.arc(a).name), built.name(built.arc(a).name));
  }
  EXPECT_NEAR(loaded.bounds().ll.lat, built.bounds().ll.lat, 1e-9);
  EXPECT_NEAR(loaded.bounds().ur.lon, built.bounds().ur.lon, 1e-9);
}

TEST(RoadGraphFormat, RejectsGarbageAndTruncation) {
  const fs::path bad = ScratchDir() / "not-a-graph.fvroad";
  {
    std::ofstream out(bad, std::ios::binary | std::ios::trunc);
    out << "this is not a road graph at all, not even close";
  }
  RoadGraph g;
  EXPECT_EQ(RoadGraph::Load(bad.string(), &g).code, fv::kInvalidArg);

  // A valid header whose counts run past the end of the file must be
  // rejected before anything reserves memory on their say-so.
  const std::string osm = WriteLoopOsm();
  RoadGraph built;
  ASSERT_EQ(fv::routing::BuildRoadGraph({osm}, {}, &built, nullptr).code, fv::kOk);
  const fs::path good = ScratchDir() / "loop-trunc.fvroad";
  ASSERT_EQ(built.Save(good.string()).code, fv::kOk);
  const auto size = fs::file_size(good);
  fs::resize_file(good, size / 2);
  EXPECT_EQ(RoadGraph::Load(good.string(), &g).code, fv::kIoError);

  EXPECT_EQ(RoadGraph::Load((ScratchDir() / "absent.fvroad").string(), &g).code, fv::kIoError);
}

TEST(RoadGraphIndex, NearestNodeAgreesWithBruteForce) {
  SKIP_WITHOUT_KIAWAH();
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph(KiawahInputs(), {}, &g, nullptr).code, fv::kOk);
  ASSERT_GT(g.node_count(), 1000u);

  const fv::GeoRect b = g.bounds();
  int checked = 0;
  for (int i = 0; i < 40; ++i) {
    fv::GeoPoint p;
    p.lat = b.ll.lat + (b.ur.lat - b.ll.lat) * ((i * 7) % 40) / 40.0;
    p.lon = b.ll.lon + (b.ur.lon - b.ll.lon) * ((i * 13) % 40) / 40.0;

    double best = 1e18;
    uint32_t best_node = 0;
    for (uint32_t n = 0; n < g.node_count(); ++n) {
      const fv::GeoPoint q = g.location(n);
      const double d = fv::routing::GreatCircleMeters(p.lat, p.lon, q.lat, q.lon);
      if (d < best) { best = d; best_node = n; }
    }

    uint32_t got = 0;
    double got_m = 0.0;
    const bool found = g.NearestNode(p, 1e7, &got, &got_m);
    ASSERT_TRUE(found);
    // Ties are possible in principle; compare the distance, not the index.
    EXPECT_NEAR(got_m, best, 1e-6) << "at " << p.lat << "," << p.lon;
    EXPECT_EQ(got, best_node);
    ++checked;
  }
  EXPECT_EQ(checked, 40);
}

TEST(RoadGraphIndex, NearestNodeHonoursTheRadius) {
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({WriteLoopOsm()}, {}, &g, nullptr).code, fv::kOk);

  uint32_t node = 0;
  double meters = 0.0;
  // A point ~200 km away is inside no sensible snap radius.
  EXPECT_FALSE(g.NearestNode(fv::GeoPoint{34.5, -80.0}, 500.0, &node, &meters));
  // Right on top of n1.
  ASSERT_TRUE(g.NearestNode(fv::GeoPoint{32.7000, -80.0000}, 50.0, &node, &meters));
  EXPECT_EQ(g.node(node).osm_id, 1);
  EXPECT_LT(meters, 1.0);
}

// --- point-to-segment snapping (§1d) ---------------------------------------

TEST(RoadGraphArcSnap, SnapsToTheRoadBesideYouAndNotToItsFarEndpoints) {
  RoadGraph g;
  ASSERT_TRUE(fv::routing::BuildRoadGraph({WriteLoopOsm()}, {}, &g).ok());

  // Twenty metres north of way 103's midpoint, which runs due west along
  // 32.705 from n5 to n4. Both of its endpoints are most of a kilometre away,
  // which is exactly the case §1d is about: standing ON a road and being
  // attached to a junction nowhere near it.
  const fv::GeoPoint p{32.7052, -80.0050};

  uint32_t node = 0;
  double node_m = 0.0;
  ASSERT_TRUE(g.NearestNode(p, 2000.0, &node, &node_m));
  EXPECT_GT(node_m, 400.0);

  RoadGraph::ArcSnap snap;
  ASSERT_TRUE(g.NearestArcPoint(p, 2000.0, &snap));
  EXPECT_LT(snap.distance_m, 30.0);
  EXPECT_FALSE(snap.at_node());
  // Halfway along, whichever end the arc happens to be stored at.
  EXPECT_NEAR(snap.t(), 0.5, 0.02);
  EXPECT_NEAR(snap.point.lat, 32.7050, 1e-4);
  EXPECT_NEAR(snap.point.lon, -80.0050, 1e-4);

  const uint32_t n4 = NodeForOsmId(g, 4), n5 = NodeForOsmId(g, 5);
  EXPECT_TRUE((snap.from == n4 && snap.to == n5) || (snap.from == n5 && snap.to == n4));
  EXPECT_EQ(g.ArcSource(snap.arc), snap.from);
  EXPECT_EQ(g.arc(snap.arc).target, snap.to);
}

TEST(RoadGraphArcSnap, AgreesWithBruteForceOverTheWholeShape) {
  SKIP_WITHOUT_KIAWAH();
  RoadGraph g;
  ASSERT_TRUE(fv::routing::BuildRoadGraph(kiawah_inputs, {}, &g).ok());
  ASSERT_GT(g.arc_count(), 0u);

  // Every arc's shape, walked the slow way. The graph indexes one arc per
  // undirected road; the twin is the same tarmac, so a brute force over ALL
  // arcs can legitimately name the other one — the DISTANCE is what has to
  // agree, and it is compared to a millimetre.
  auto brute = [&g](const fv::GeoPoint& p) {
    double best = std::numeric_limits<double>::infinity();
    std::vector<fv::GeoPoint> shape;
    for (uint32_t a = 0; a < g.arc_count(); ++a) {
      shape.clear();
      g.ArcShape(a, &shape);
      for (size_t i = 0; i + 1 < shape.size(); ++i) {
        fv::SegmentProjection sp;
        fv::ProjectOntoSegment(p, shape[i], shape[i + 1], &sp);
        best = std::min(best, sp.distance_m);
      }
    }
    return best;
  };

  const fv::GeoRect b = g.bounds();
  uint32_t seed = 987654321u;
  auto rnd = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) / 16777216.0;
  };
  int checked = 0;
  for (int i = 0; i < 60; ++i) {
    const fv::GeoPoint p{b.ll.lat + rnd() * (b.ur.lat - b.ll.lat),
                         b.ll.lon + rnd() * (b.ur.lon - b.ll.lon)};
    const double want = brute(p);
    RoadGraph::ArcSnap snap;
    const bool got = g.NearestArcPoint(p, 1500.0, &snap);
    if (want > 1500.0) {
      EXPECT_FALSE(got) << "point " << i << " has no road within 1500 m";
      continue;
    }
    ASSERT_TRUE(got) << "point " << i << " should have found a road at " << want;
    EXPECT_NEAR(snap.distance_m, want, 1e-3) << "point " << i;
    // The projected point really is `along_m` along the arc it names.
    std::vector<fv::GeoPoint> shape;
    g.ArcShape(snap.arc, &shape);
    ASSERT_GE(shape.size(), 2u);
    EXPECT_NEAR(fv::routing::GreatCircleMeters(shape.front().lat, shape.front().lon,
                                               snap.point.lat, snap.point.lon),
                snap.along_m, snap.along_m + 1.0);
    EXPECT_GE(snap.along_m, -1e-6);
    EXPECT_LE(snap.along_m, snap.length_m + 1e-6);
    ++checked;
  }
  EXPECT_GT(checked, 0);
}

TEST(RoadGraphArcSnap, TheFilterWidensPastARoadItRefuses) {
  RoadGraph g;
  ASSERT_TRUE(fv::routing::BuildRoadGraph({WriteLoopOsm()}, {}, &g).ok());
  const fv::GeoPoint p{32.7052, -80.0050};

  RoadGraph::ArcSnap open;
  ASSERT_TRUE(g.NearestArcPoint(p, 2000.0, &open));

  // Refuse the road it would have picked. The answer must be a DIFFERENT road
  // that is further away, not nothing and not the refused one clamped.
  const uint32_t refused = open.arc;
  const uint32_t twin = g.ArcBetween(open.to, open.from);
  RoadGraph::ArcSnap other;
  ASSERT_TRUE(g.NearestArcPoint(p, 2000.0, &other,
                                [&](const fv::routing::RoadArc& a) {
                                  const uint32_t idx =
                                      static_cast<uint32_t>(&a - &g.arc(0));
                                  return idx != refused && idx != twin;
                                }));
  EXPECT_NE(other.arc, refused);
  EXPECT_GT(other.distance_m, open.distance_m);

  // And a filter that refuses everything says so rather than falling back.
  EXPECT_FALSE(g.NearestArcPoint(p, 2000.0, &other,
                                 [](const fv::routing::RoadArc&) { return false; }));
}

TEST(RoadGraphArcSnap, APointOffTheEndOfARoadLandsOnItsEndpoint) {
  RoadGraph g;
  ASSERT_TRUE(fv::routing::BuildRoadGraph({WriteLoopOsm()}, {}, &g).ok());

  // Due south of n1, which is a corner of the loop: every road runs away from
  // it, so the projection clamps to the node and says so.
  const uint32_t n1 = NodeForOsmId(g, 1);
  const fv::GeoPoint here = g.location(n1);
  const fv::GeoPoint p{here.lat - 0.0002, here.lon};

  RoadGraph::ArcSnap snap;
  ASSERT_TRUE(g.NearestArcPoint(p, 500.0, &snap));
  EXPECT_TRUE(snap.at_node());
  EXPECT_TRUE(snap.from == n1 || snap.to == n1);
  EXPECT_NEAR(snap.point.lat, here.lat, 1e-7);
  EXPECT_NEAR(snap.point.lon, here.lon, 1e-7);
}

TEST(RoadGraphArcSnap, TheRadiusIsHonouredAndAnEmptyGraphAnswersNothing) {
  RoadGraph g;
  ASSERT_TRUE(fv::routing::BuildRoadGraph({WriteLoopOsm()}, {}, &g).ok());
  const fv::GeoPoint far{32.9000, -80.0050};  // ~21 km north of the loop
  RoadGraph::ArcSnap snap;
  EXPECT_FALSE(g.NearestArcPoint(far, 500.0, &snap));
  EXPECT_TRUE(g.NearestArcPoint(far, 30000.0, &snap));

  RoadGraph empty;
  empty.Finalize();
  EXPECT_FALSE(empty.NearestArcPoint(fv::GeoPoint{0.0, 0.0}, 1000.0, &snap));
}

TEST(RoadGraphArcSnap, SurvivesTheSaveLoadRoundTrip) {
  RoadGraph built;
  ASSERT_TRUE(fv::routing::BuildRoadGraph({WriteLoopOsm()}, {}, &built).ok());
  const fs::path path = ScratchDir() / "arcsnap.fvroad";
  ASSERT_TRUE(built.Save(path.string()).ok());
  RoadGraph loaded;
  ASSERT_TRUE(RoadGraph::Load(path.string(), &loaded).ok());

  // The index is derived in Finalize, so a loaded graph has to have it —
  // nothing about it is in the file.
  const fv::GeoPoint p{32.7052, -80.0050};
  RoadGraph::ArcSnap a, b;
  ASSERT_TRUE(built.NearestArcPoint(p, 2000.0, &a));
  ASSERT_TRUE(loaded.NearestArcPoint(p, 2000.0, &b));
  EXPECT_EQ(a.arc, b.arc);
  EXPECT_NEAR(a.distance_m, b.distance_m, 1e-9);
  EXPECT_NEAR(a.along_m, b.along_m, 1e-9);
}

TEST(RoadGraphBuild, MergesInputsOnSharedNodeIds) {
  SKIP_WITHOUT_KIAWAH();
  RoadGraph one;
  ASSERT_EQ(fv::routing::BuildRoadGraph({KiawahPath("map.osm")}, {}, &one, nullptr).code, fv::kOk);

  // The load-bearing property: identity is the OSM node ID, not the file. The
  // same export listed twice must produce exactly the same graph, not two
  // copies of it. (The four exports do overlap — the API returns a way that
  // crosses the bbox in full — so this is not a hypothetical.)
  RoadGraph twice;
  ASSERT_EQ(
      fv::routing::BuildRoadGraph({KiawahPath("map.osm"), KiawahPath("map.osm")}, {}, &twice,
                                  nullptr)
          .code,
      fv::kOk);
  EXPECT_EQ(twice.node_count(), one.node_count());
  EXPECT_EQ(twice.arc_count(), one.arc_count());
  EXPECT_EQ(twice.geometry_count(), one.geometry_count());

  RoadGraph all;
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph(KiawahInputs(), {}, &all, &stats).code, fv::kOk);
  EXPECT_GT(all.node_count(), one.node_count());

  // Structure, not a pinned total (the ledger's whole-directory rule).
  EXPECT_EQ(all.arc_count(), 2 * static_cast<uint32_t>(stats.edges));
  EXPECT_GT(stats.ways_kept, 500);
  EXPECT_EQ(stats.nodes_resolved, stats.nodes_needed);

  // Every arc index the router will dereference must be in range.
  for (uint32_t n = 0; n < all.node_count(); ++n) {
    EXPECT_LE(all.arc_begin(n), all.arc_end(n));
    for (uint32_t a = all.arc_begin(n); a < all.arc_end(n); ++a) {
      const fv::routing::RoadArc& arc = all.arc(a);
      EXPECT_LT(arc.target, all.node_count());
      EXPECT_LE(arc.geom_begin + arc.geom_count, all.geometry_count());
      EXPECT_LT(arc.name, 100000u);
      EXPECT_GT(arc.speed_kph, 0);
      EXPECT_TRUE(arc.forward() || arc.backward());
    }
  }
}

// Kiawah is a gated resort island: most of its streets are `access=private`.
// Until O5b that deleted them — 51 ways, 340 driveable arcs, i.e. the island's
// road network — and the diagnostic that mattered was that ZERO arcs carried
// kArcNoMotorVehicle afterwards: the roads were not marked unusable, they were
// absent, so the flags said nothing was wrong.
TEST(RoadGraphBuild, PrivateWaysStayInTheGraphCarryingTheirGate) {
  SKIP_WITHOUT_KIAWAH();
  fv::routing::RoadGraphBuildOptions strict;
  fv::routing::RoadGraphBuildStats strict_stats;
  RoadGraph strict_graph;
  ASSERT_EQ(
      fv::routing::BuildRoadGraph(KiawahInputs(), strict, &strict_graph, &strict_stats).code,
      fv::kOk);

  fv::routing::RoadGraphBuildOptions permissive;
  permissive.honor_access = false;
  fv::routing::RoadGraphBuildStats permissive_stats;
  RoadGraph permissive_graph;
  ASSERT_EQ(fv::routing::BuildRoadGraph(KiawahInputs(), permissive, &permissive_graph,
                                        &permissive_stats)
                .code,
            fv::kOk);

  // The private streets are all still here: the strict build differs from the
  // permissive one only by the handful of ways barred outright to everybody.
  EXPECT_EQ(strict_stats.ways_kept, permissive_stats.ways_kept - strict_stats.ways_dropped_access);
  EXPECT_LT(strict_stats.ways_dropped_access, 20);

  // And they are marked, per mode, rather than silently gone. Lower bounds
  // only: TestData grows (the whole-directory-total rule).
  EXPECT_GT(strict_stats.arcs_private_motor_vehicle, 200);  // measured 256
  EXPECT_EQ(permissive_stats.arcs_private_motor_vehicle, 0);

  // `bicycle=no` is real on this island and must keep biting — it is what
  // --ignore-access throws away along with the `private`, which is why the
  // two are not interchangeable.
  EXPECT_GT(strict_stats.arcs_no_bicycle, 0);
  EXPECT_EQ(permissive_stats.arcs_no_bicycle, 0);

  // The car network survives honouring access, which is the whole point.
  int drivable = 0;
  for (uint32_t i = 0; i < strict_graph.arc_count(); ++i) {
    const fv::routing::RoadArc& arc = strict_graph.arc(i);
    if (fv::routing::IsDriveable(arc.klass) && arc.motor_vehicle_allowed()) ++drivable;
  }
  EXPECT_GT(drivable, 1000);
}

// The private bits live in the byte the arc word had spare through O5, so
// they have to survive a save/load that never bumped its version number.
TEST(RoadGraphFormat, PrivateBitsSurviveTheRoundTrip) {
  SKIP_WITHOUT_KIAWAH();
  RoadGraph g;
  fv::routing::RoadGraphBuildOptions options;
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph(KiawahInputs(), options, &g, &stats).code, fv::kOk);
  const fs::path path = ScratchDir() / "private-bits.fvroad";
  ASSERT_EQ(g.Save(path.string()).code, fv::kOk);

  RoadGraph back;
  ASSERT_EQ(RoadGraph::Load(path.string(), &back).code, fv::kOk);
  ASSERT_EQ(back.arc_count(), g.arc_count());
  int64_t priv = 0;
  for (uint32_t i = 0; i < g.arc_count(); ++i) {
    EXPECT_EQ(back.arc(i).flags, g.arc(i).flags) << "arc " << i;
    if (back.arc(i).motor_vehicle_private()) ++priv;
  }
  EXPECT_EQ(priv, stats.arcs_private_motor_vehicle);
  // The other fields still land where they always did — the extra bits went
  // into spare space, they did not move anything.
  EXPECT_EQ(back.arc(0).klass, g.arc(0).klass);
  EXPECT_EQ(back.arc(0).speed_kph, g.arc(0).speed_kph);
}

// `access=private` is a default, not the last word: a mode key overrides it.
// Kiawah's leisure-trail network is exactly that shape — private to drive,
// bicycle=yes — and reading `access` as final deleted every trail on the
// island from the bicycle graph.
TEST(RoadGraphBuild, ModeAccessTagsOverrideTheGenericAccessTag) {
  const char* kOsm = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.7000000" lon="-80.0000000"/>
 <node id="2" lat="32.7000000" lon="-80.0100000"/>
 <node id="3" lat="32.7050000" lon="-80.0000000"/>
 <node id="4" lat="32.7050000" lon="-80.0100000"/>
 <node id="5" lat="32.7100000" lon="-80.0000000"/>
 <node id="6" lat="32.7100000" lon="-80.0100000"/>
 <node id="7" lat="32.7150000" lon="-80.0000000"/>
 <node id="8" lat="32.7150000" lon="-80.0100000"/>
 <way id="301">
  <nd ref="1"/><nd ref="2"/>
  <tag k="highway" v="cycleway"/><tag k="name" v="Trail"/>
  <tag k="access" v="private"/><tag k="bicycle" v="yes"/>
  <tag k="foot" v="yes"/><tag k="motor_vehicle" v="no"/>
 </way>
 <way id="302">
  <nd ref="3"/><nd ref="4"/>
  <tag k="highway" v="service"/><tag k="name" v="Gated"/>
  <tag k="access" v="private"/>
 </way>
 <way id="303">
  <nd ref="5"/><nd ref="6"/>
  <tag k="highway" v="tertiary"/><tag k="name" v="Parkway"/>
  <tag k="access" v="private"/><tag k="bicycle" v="no"/>
  <tag k="foot" v="no"/><tag k="motor_vehicle" v="yes"/>
 </way>
 <way id="304">
  <nd ref="7"/><nd ref="8"/>
  <tag k="highway" v="footway"/><tag k="name" v="Boardwalk"/>
  <tag k="vehicle" v="no"/>
 </way>
</osm>
)";
  const fs::path path = ScratchDir() / "access-modes.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << kOsm;
  }
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({path.string()}, {}, &g, nullptr).code, fv::kOk);

  std::map<std::string, const fv::routing::RoadArc*> by_name;
  for (uint32_t i = 0; i < g.arc_count(); ++i) {
    by_name[g.name(g.arc(i).name)] = &g.arc(i);
  }

  // "Gated" is `access=private` and nothing else. It is NOT barred — private
  // is permission the router lacks, not an absent road — so it stays, marked
  // private to all three modes. (Before O5b this way was deleted, which on a
  // gated community deleted the street network.)
  ASSERT_EQ(by_name.count("Gated"), 1u);
  EXPECT_TRUE(by_name["Gated"]->motor_vehicle_allowed());
  EXPECT_TRUE(by_name["Gated"]->bicycle_allowed());
  EXPECT_TRUE(by_name["Gated"]->foot_allowed());
  EXPECT_TRUE(by_name["Gated"]->motor_vehicle_private());
  EXPECT_TRUE(by_name["Gated"]->bicycle_private());
  EXPECT_TRUE(by_name["Gated"]->foot_private());

  ASSERT_EQ(by_name.count("Trail"), 1u);
  EXPECT_TRUE(by_name["Trail"]->bicycle_allowed());
  EXPECT_TRUE(by_name["Trail"]->foot_allowed());
  EXPECT_FALSE(by_name["Trail"]->motor_vehicle_allowed());
  // An explicit mode `yes` beats the generic `private` outright: a bike on
  // this trail is not a guest, it is the intended traffic.
  EXPECT_FALSE(by_name["Trail"]->bicycle_private());
  EXPECT_FALSE(by_name["Trail"]->foot_private());

  ASSERT_EQ(by_name.count("Parkway"), 1u);
  EXPECT_FALSE(by_name["Parkway"]->bicycle_allowed());
  EXPECT_FALSE(by_name["Parkway"]->foot_allowed());
  EXPECT_TRUE(by_name["Parkway"]->motor_vehicle_allowed());
  EXPECT_FALSE(by_name["Parkway"]->motor_vehicle_private());

  // `vehicle` is bicycle AND motor_vehicle, so it denies both — and says
  // nothing about walking, which is the whole point of a boardwalk.
  ASSERT_EQ(by_name.count("Boardwalk"), 1u);
  EXPECT_FALSE(by_name["Boardwalk"]->bicycle_allowed());
  EXPECT_FALSE(by_name["Boardwalk"]->motor_vehicle_allowed());
  EXPECT_TRUE(by_name["Boardwalk"]->foot_allowed());
}

// ---------------------------------------------------------------------------
// Turn restrictions (O5)
// ---------------------------------------------------------------------------

// A plain crossroads: C in the middle with arms S, N, W and E, plus a bypass
// from W round to N so there is always a legal way to get anywhere. Every arm
// is its own way, which is what a restriction relation names.
//
//        N(3)
//         |    \  w305 (bypass)
//   W(4)--C(1)--E(5)
//         |
//        S(2)
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

// from way 301 (arriving from the south), via node 1, to `to_way`.
std::string RestrictionRelation(const char* value, int to_way, const char* except = nullptr) {
  std::string r = " <relation id=\"900\">\n";
  r += "  <member type=\"way\" ref=\"301\" role=\"from\"/>\n";
  r += "  <member type=\"node\" ref=\"1\" role=\"via\"/>\n";
  r += "  <member type=\"way\" ref=\"" + std::to_string(to_way) + "\" role=\"to\"/>\n";
  r += "  <tag k=\"type\" v=\"restriction\"/>\n";
  r += std::string("  <tag k=\"restriction\" v=\"") + value + "\"/>\n";
  if (except != nullptr) r += std::string("  <tag k=\"except\" v=\"") + except + "\"/>\n";
  r += " </relation>\n";
  return r;
}

std::string WriteCrossroads(const char* name, const std::string& relations) {
  const fs::path path = ScratchDir() / name;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << CrossroadsOsm(relations);
  out.close();
  return path.string();
}

TEST(RoadGraphRestrictions, NoLeftTurnResolvesOntoTheViaNodesArcs) {
  const std::string osm =
      WriteCrossroads("cross-noleft.osm", RestrictionRelation("no_left_turn", 303));
  RoadGraph g;
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph({osm}, {}, &g, &stats).code, fv::kOk);

  EXPECT_EQ(stats.restrictions_seen, 1);
  EXPECT_EQ(stats.restrictions_applied, 1);
  EXPECT_EQ(stats.restrictions_unresolved, 0);
  ASSERT_EQ(g.restriction_count(), 1u);

  const uint32_t c = NodeForOsmId(g, 1);
  const uint32_t s = NodeForOsmId(g, 2);
  const uint32_t w = NodeForOsmId(g, 4);
  const uint32_t n = NodeForOsmId(g, 3);

  const fv::routing::TurnRestriction& r = g.restriction(0);
  EXPECT_EQ(r.via_node, c);
  EXPECT_EQ(r.kind, fv::routing::TurnRestrictionKind::kNo);
  // `from` is the arc of the via node facing back down the way the driver
  // came in on — that is what "how did I arrive" resolves to at the junction.
  EXPECT_EQ(r.from_arc, g.ArcBetween(c, s));
  EXPECT_EQ(r.to_arc, g.ArcBetween(c, w));

  EXPECT_TRUE(g.node_restricted(c));
  EXPECT_FALSE(g.node_restricted(n));

  // The forbidden turn, and the three that are not.
  EXPECT_FALSE(g.TurnAllowed(c, g.ArcBetween(c, s), g.ArcBetween(c, w)));
  EXPECT_TRUE(g.TurnAllowed(c, g.ArcBetween(c, s), g.ArcBetween(c, n)));
  EXPECT_TRUE(g.TurnAllowed(c, g.ArcBetween(c, n), g.ArcBetween(c, w)));
  // Neither end of a route is a turn.
  EXPECT_TRUE(g.TurnAllowed(c, fv::routing::kNoArc, g.ArcBetween(c, w)));
}

TEST(RoadGraphRestrictions, OnlyStraightOnForbidsEveryOtherExit) {
  const std::string osm =
      WriteCrossroads("cross-only.osm", RestrictionRelation("only_straight_on", 302));
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({osm}, {}, &g, nullptr).code, fv::kOk);
  ASSERT_EQ(g.restriction_count(), 1u);
  EXPECT_EQ(g.restriction(0).kind, fv::routing::TurnRestrictionKind::kOnly);

  const uint32_t c = NodeForOsmId(g, 1);
  const uint32_t s = NodeForOsmId(g, 2);
  const uint32_t from = g.ArcBetween(c, s);
  EXPECT_TRUE(g.TurnAllowed(c, from, g.ArcBetween(c, NodeForOsmId(g, 3))));   // straight
  EXPECT_FALSE(g.TurnAllowed(c, from, g.ArcBetween(c, NodeForOsmId(g, 4))));  // left
  EXPECT_FALSE(g.TurnAllowed(c, from, g.ArcBetween(c, NodeForOsmId(g, 5))));  // right
  EXPECT_FALSE(g.TurnAllowed(c, from, from));                                 // U-turn
  // An only_* binds the approach it names and no other.
  const uint32_t other = g.ArcBetween(c, NodeForOsmId(g, 5));
  EXPECT_TRUE(g.TurnAllowed(c, other, g.ArcBetween(c, NodeForOsmId(g, 4))));
}

TEST(RoadGraphRestrictions, ExceptMotorcarAndBuildOptionBothSuppressIt) {
  const std::string with_except =
      WriteCrossroads("cross-except.osm",
                      RestrictionRelation("no_left_turn", 303, "bicycle;motorcar"));
  RoadGraph g;
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph({with_except}, {}, &g, &stats).code, fv::kOk);
  EXPECT_EQ(stats.restrictions_seen, 1);
  EXPECT_EQ(stats.restrictions_excepted, 1);
  EXPECT_EQ(g.restriction_count(), 0u);

  // An except list that does not cover a car leaves the restriction standing.
  const std::string bicycle_only =
      WriteCrossroads("cross-except-bike.osm",
                      RestrictionRelation("no_left_turn", 303, "bicycle"));
  RoadGraph g2;
  ASSERT_EQ(fv::routing::BuildRoadGraph({bicycle_only}, {}, &g2, nullptr).code, fv::kOk);
  EXPECT_EQ(g2.restriction_count(), 1u);

  // And the build option turns the whole reading off.
  fv::routing::RoadGraphBuildOptions options;
  options.honor_turn_restrictions = false;
  RoadGraph g3;
  fv::routing::RoadGraphBuildStats stats3;
  ASSERT_EQ(fv::routing::BuildRoadGraph({bicycle_only}, options, &g3, &stats3).code, fv::kOk);
  EXPECT_EQ(g3.restriction_count(), 0u);
  EXPECT_EQ(stats3.restrictions_seen, 0);
  // Turning restrictions off must not change the graph in any other way.
  EXPECT_EQ(g3.node_count(), g2.node_count());
  EXPECT_EQ(g3.arc_count(), g2.arc_count());
}

TEST(RoadGraphRestrictions, ViaWayIsCountedAndNotApplied) {
  std::string relation =
      " <relation id=\"901\">\n"
      "  <member type=\"way\" ref=\"301\" role=\"from\"/>\n"
      "  <member type=\"way\" ref=\"302\" role=\"via\"/>\n"
      "  <member type=\"way\" ref=\"303\" role=\"to\"/>\n"
      "  <tag k=\"type\" v=\"restriction\"/>\n"
      "  <tag k=\"restriction\" v=\"no_u_turn\"/>\n"
      " </relation>\n";
  const std::string osm = WriteCrossroads("cross-viaway.osm", relation);
  RoadGraph g;
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph({osm}, {}, &g, &stats).code, fv::kOk);
  EXPECT_EQ(stats.restrictions_seen, 1);
  EXPECT_EQ(stats.restrictions_via_way, 1);
  EXPECT_EQ(stats.restrictions_applied, 0);
  EXPECT_EQ(g.restriction_count(), 0u);
}

TEST(RoadGraphRestrictions, RelationNamingAWayThatIsNotInTheGraphIsUnresolved) {
  // Way 999 does not exist, so there is no arc at the via node to hang the
  // restriction on. That is data, not a build failure.
  const std::string osm =
      WriteCrossroads("cross-missing.osm", RestrictionRelation("no_right_turn", 999));
  RoadGraph g;
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph({osm}, {}, &g, &stats).code, fv::kOk);
  EXPECT_EQ(stats.restrictions_seen, 1);
  EXPECT_EQ(stats.restrictions_unresolved, 1);
  EXPECT_EQ(g.restriction_count(), 0u);
}

TEST(RoadGraphRestrictions, TheKiawahExportsResolveEveryRestrictionTheyCarry) {
  SKIP_WITHOUT_KIAWAH();
  RoadGraph g;
  fv::routing::RoadGraphBuildOptions options;
  options.honor_access = false;  // the gated-resort caveat, as elsewhere
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph(KiawahInputs(), options, &g, &stats).code, fv::kOk);

  // map.osm carries five `type=restriction` relations (two no_left_turn,
  // three no_u_turn) and the other three exports carry none. Real data, so
  // this is a lower bound plus an exactness claim about what it resolves to,
  // not a total over a directory that may grow.
  EXPECT_GE(stats.restrictions_seen, 5);
  EXPECT_EQ(stats.restrictions_seen, stats.restrictions_applied +
                                        stats.restrictions_via_way +
                                        stats.restrictions_unresolved +
                                        stats.restrictions_excepted);
  EXPECT_EQ(g.restriction_count(), static_cast<uint32_t>(stats.restrictions_applied));

  // Every one names arcs of its own via node, in range, both ways round.
  for (uint32_t i = 0; i < g.restriction_count(); ++i) {
    const fv::routing::TurnRestriction& r = g.restriction(i);
    ASSERT_LT(r.via_node, g.node_count());
    EXPECT_GE(r.from_arc, g.arc_begin(r.via_node));
    EXPECT_LT(r.from_arc, g.arc_end(r.via_node));
    EXPECT_GE(r.to_arc, g.arc_begin(r.via_node));
    EXPECT_LT(r.to_arc, g.arc_end(r.via_node));
    EXPECT_TRUE(g.node_restricted(r.via_node));
    EXPECT_FALSE(g.TurnAllowed(r.via_node, r.from_arc, r.to_arc));
  }
}

TEST(RoadGraphFormat, RestrictionsSurviveTheRoundTripAndVersionOneStillLoads) {
  const std::string osm =
      WriteCrossroads("cross-format.osm", RestrictionRelation("no_left_turn", 303));
  RoadGraph built;
  ASSERT_EQ(fv::routing::BuildRoadGraph({osm}, {}, &built, nullptr).code, fv::kOk);
  ASSERT_EQ(built.restriction_count(), 1u);

  const fs::path out = ScratchDir() / "cross.fvroad";
  ASSERT_EQ(built.Save(out.string()).code, fv::kOk);
  RoadGraph loaded;
  ASSERT_EQ(RoadGraph::Load(out.string(), &loaded).code, fv::kOk);
  ASSERT_EQ(loaded.restriction_count(), 1u);
  EXPECT_EQ(loaded.restriction(0).via_node, built.restriction(0).via_node);
  EXPECT_EQ(loaded.restriction(0).from_arc, built.restriction(0).from_arc);
  EXPECT_EQ(loaded.restriction(0).to_arc, built.restriction(0).to_arc);
  EXPECT_EQ(loaded.restriction(0).kind, built.restriction(0).kind);
  EXPECT_EQ(loaded.state_count(), built.state_count());
  // The via node is split into one state per arc plus one for "arrived along
  // nothing"; every other node is still exactly itself.
  const uint32_t degree =
      built.arc_end(built.restriction(0).via_node) - built.arc_begin(built.restriction(0).via_node);
  EXPECT_EQ(built.state_count(), built.node_count() + degree + 1);

  // A version-1 file has no restriction table and no count field at all. Make
  // one out of the version-2 bytes: version is the word after the 8-byte
  // magic, and the count is the last word of the header.
  std::string bytes;
  {
    std::ifstream in(out, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  ASSERT_GT(bytes.size(), 36u);
  bytes[8] = 1;
  bytes.erase(32, 4);  // the restriction count
  bytes.resize(bytes.size() - 16);  // and the one restriction record
  const fs::path v1 = ScratchDir() / "cross-v1.fvroad";
  {
    std::ofstream o(v1, std::ios::binary | std::ios::trunc);
    o.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  RoadGraph old;
  ASSERT_EQ(RoadGraph::Load(v1.string(), &old).code, fv::kOk);
  EXPECT_EQ(old.restriction_count(), 0u);
  EXPECT_EQ(old.node_count(), built.node_count());
  EXPECT_EQ(old.arc_count(), built.arc_count());
  EXPECT_EQ(old.state_count(), old.node_count());
}

// ---------------------------------------------------------------------------
// Ferries and tolls (O5e)
// ---------------------------------------------------------------------------

// A bay with three ways across it, chosen so the three are separable by time
// alone and in a known order: a short tolled bridge (fastest), a ferry calling
// at a mid-bay island, and a long free road round the head of the bay
// (slowest). All three share the two shore nodes, so the graph stays connected
// whichever of them a query refuses.
//
//              I(32.75,-79.95) -- slipway -- S
//       ferry /  duration 00:45  \ ferry
//   A(32.70,-80.00) ==== toll bridge ==== B(32.70,-79.90)
//       \                                /
//            L(32.40,-79.95), secondary
//
// The island's slipway is what makes I a junction, so the ferry way is cut
// into TWO edges — which is the case that shows one duration becoming one
// speed for the whole crossing rather than per edge.
const char* BayOsm() {
  return R"(<?xml version="1.0" encoding="UTF-8"?>
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
}

std::string WriteBayOsm() {
  const fs::path path = ScratchDir() / "bay.osm";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << BayOsm();
  out.close();
  return path.string();
}

// All three crossings run between the same two shore nodes — the mid-bay
// points are used once each and so stay shape points, exactly as they should.
// That makes them PARALLEL edges, which ArcBetween cannot tell apart, so the
// bay's tests find an arc by the name of the way it came from.
const fv::routing::RoadArc* ArcNamed(const RoadGraph& g, const std::string& name) {
  for (uint32_t i = 0; i < g.arc_count(); ++i) {
    if (g.name(g.arc(i).name) == name) return &g.arc(i);
  }
  return nullptr;
}

int CountNamed(const RoadGraph& g, const std::string& name) {
  int n = 0;
  for (uint32_t i = 0; i < g.arc_count(); ++i) {
    if (g.name(g.arc(i).name) == name) ++n;
  }
  return n;
}

TEST(RoadGraphFerry, RouteFerryWaysBecomeArcsOfTheirOwnClass) {
  RoadGraph g;
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph({WriteBayOsm()}, {}, &g, &stats).code, fv::kOk);

  // The ferry has no `highway` tag at all, so before O5e it was not in the
  // graph and "avoid the ferry" had nothing to avoid.
  EXPECT_EQ(stats.ways_ferry, 1);
  EXPECT_EQ(stats.ferries_timed, 1);
  EXPECT_EQ(stats.arcs_ferry, 4);  // two legs either side of the island, both ways
  EXPECT_EQ(stats.arcs_toll, 2);   // one bridge edge, both directions

  const fv::routing::RoadArc* leg = ArcNamed(g, "Bay Ferry");
  ASSERT_NE(leg, nullptr);
  EXPECT_EQ(leg->klass, RoadClass::kFerry);
  EXPECT_TRUE(leg->is_ferry());
  EXPECT_FALSE(leg->tolled());
  EXPECT_EQ(g.name(leg->name), "Bay Ferry");
  // A boat goes both ways and carries a car unless told otherwise.
  EXPECT_TRUE(leg->forward());
  EXPECT_TRUE(leg->backward());
  EXPECT_TRUE(leg->motor_vehicle_allowed());
  EXPECT_TRUE(fv::routing::IsDriveable(RoadClass::kFerry));

  // `highway=ferry` is not a tag OSM uses, and must not become this class by
  // the back door — only `route=ferry` does.
  EXPECT_EQ(fv::routing::RoadClassFromHighwayTag("ferry"), RoadClass::kNone);
  EXPECT_EQ(fv::routing::RoadClassFromName("ferry"), RoadClass::kFerry);
  EXPECT_STREQ(fv::routing::RoadClassName(RoadClass::kFerry), "ferry");
}

TEST(RoadGraphFerry, TollIsABitOnAnOrdinaryRoadNotAClass) {
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({WriteBayOsm()}, {}, &g, nullptr).code, fv::kOk);
  const fv::routing::RoadArc* bridge = ArcNamed(g, "Toll Bridge");
  ASSERT_NE(bridge, nullptr);
  // A tolled motorway is still a motorway: the class, the speed and the name
  // are untouched, and only the bit is added.
  EXPECT_TRUE(bridge->tolled());
  EXPECT_FALSE(bridge->is_ferry());
  EXPECT_EQ(bridge->klass, RoadClass::kPrimary);
  EXPECT_EQ(bridge->speed_kph, 80);
}

// The speed of a crossing is its own duration over its own length, and the
// whole way gets that one speed however many edges it is cut into.
TEST(RoadGraphFerry, DurationSetsTheSpeedOverTheWaysWholeLength) {
  RoadGraph g;
  ASSERT_EQ(fv::routing::BuildRoadGraph({WriteBayOsm()}, {}, &g, nullptr).code, fv::kOk);
  const uint32_t a = NodeForOsmId(g, 1), b = NodeForOsmId(g, 2), f = NodeForOsmId(g, 3);
  const fv::routing::RoadArc* west = ArcBetween(g, a, f);
  const fv::routing::RoadArc* east = ArcBetween(g, f, b);
  ASSERT_NE(west, nullptr);
  ASSERT_NE(east, nullptr);
  EXPECT_EQ(west->speed_kph, east->speed_kph) << "one boat, one speed";

  const double total_km = (west->length_m + east->length_m) / 1000.0;
  const double implied = total_km / (2700.0 / 3600.0);  // 00:45
  EXPECT_NEAR(west->speed_kph, implied, 1.0);           // measured 19 km/h
  EXPECT_NE(west->speed_kph, fv::routing::DefaultSpeedKph(RoadClass::kFerry))
      << "the class default should have been displaced by the duration";

  // And the two legs together take the duration the tag stated.
  EXPECT_NEAR(west->travel_seconds() + east->travel_seconds(), 2700.0, 90.0);
}

TEST(RoadGraphFerry, DurationShorthandsAllMeanTheSameThing) {
  // OSM writes `duration` as hh:mm:ss and shortens it — and the trap is that
  // the shortest form is MINUTES, so "45" and "00:45" are the same crossing.
  const char* kForms[] = {"00:45", "45", "0:45:00", "00:45:00"};
  uint8_t speeds[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; ++i) {
    std::string osm = BayOsm();
    const std::string from = "<tag k=\"duration\" v=\"00:45\"/>";
    const std::string to = std::string("<tag k=\"duration\" v=\"") + kForms[i] + "\"/>";
    osm.replace(osm.find(from), from.size(), to);
    const fs::path path = ScratchDir() / "bay-duration.osm";
    {
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out << osm;
    }
    RoadGraph g;
    ASSERT_EQ(fv::routing::BuildRoadGraph({path.string()}, {}, &g, nullptr).code, fv::kOk)
        << kForms[i];
    const fv::routing::RoadArc* leg = ArcNamed(g, "Bay Ferry");
    ASSERT_NE(leg, nullptr) << kForms[i];
    speeds[i] = leg->speed_kph;
  }
  EXPECT_EQ(speeds[0], speeds[1]) << "a bare number is minutes, not seconds";
  EXPECT_EQ(speeds[0], speeds[2]);
  EXPECT_EQ(speeds[0], speeds[3]);
}

TEST(RoadGraphFerry, AnUnparsableOrAbsentDurationFallsBackToTheClassDefault) {
  const char* kBad[] = {"", "about an hour", "::", "-5"};
  for (const char* form : kBad) {
    std::string osm = BayOsm();
    const std::string from = "<tag k=\"duration\" v=\"00:45\"/>";
    const std::string to = std::string("<tag k=\"duration\" v=\"") + form + "\"/>";
    osm.replace(osm.find(from), from.size(), to);
    const fs::path path = ScratchDir() / "bay-bad-duration.osm";
    {
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out << osm;
    }
    RoadGraph g;
    fv::routing::RoadGraphBuildStats stats;
    ASSERT_EQ(fv::routing::BuildRoadGraph({path.string()}, {}, &g, &stats).code, fv::kOk)
        << form;
    // Still a ferry, still in the graph — just at the class's assumed speed.
    EXPECT_EQ(stats.ways_ferry, 1) << form;
    EXPECT_EQ(stats.ferries_timed, 0) << form;
    const fv::routing::RoadArc* leg = ArcNamed(g, "Bay Ferry");
    ASSERT_NE(leg, nullptr) << form;
    EXPECT_EQ(leg->speed_kph, fv::routing::DefaultSpeedKph(RoadClass::kFerry)) << form;
  }
}

TEST(RoadGraphFerry, IncludeFerriesOffReproducesThePreO5eGraph) {
  fv::routing::RoadGraphBuildOptions no_ferries;
  no_ferries.include_ferries = false;
  RoadGraph g;
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph({WriteBayOsm()}, no_ferries, &g, &stats).code, fv::kOk);
  EXPECT_EQ(stats.ways_ferry, 0);
  EXPECT_EQ(stats.arcs_ferry, 0);
  // The way carried no `highway` tag, so it is simply not there.
  EXPECT_EQ(CountNamed(g, "Bay Ferry"), 0);
  for (uint32_t i = 0; i < g.arc_count(); ++i) EXPECT_FALSE(g.arc(i).is_ferry());
  // The island keeps its slipway and is now an island in the graph too — a
  // component of its own, which is the honest picture of a bay with no boat.
  EXPECT_EQ(CountNamed(g, "Island Landing"), 2);
  // The toll bit is not a ferry thing and is unaffected.
  EXPECT_EQ(stats.arcs_toll, 2);
}

TEST(RoadGraphFormat, FerryClassAndTollBitSurviveTheRoundTrip) {
  RoadGraph g;
  fv::routing::RoadGraphBuildStats stats;
  ASSERT_EQ(fv::routing::BuildRoadGraph({WriteBayOsm()}, {}, &g, &stats).code, fv::kOk);
  const fs::path path = ScratchDir() / "ferry-toll.fvroad";
  ASSERT_EQ(g.Save(path.string()).code, fv::kOk);

  RoadGraph back;
  ASSERT_EQ(RoadGraph::Load(path.string(), &back).code, fv::kOk);
  ASSERT_EQ(back.arc_count(), g.arc_count());
  int64_t ferries = 0, tolled = 0;
  for (uint32_t i = 0; i < g.arc_count(); ++i) {
    EXPECT_EQ(back.arc(i).flags, g.arc(i).flags) << "arc " << i;
    EXPECT_EQ(back.arc(i).klass, g.arc(i).klass) << "arc " << i;
    EXPECT_EQ(back.arc(i).speed_kph, g.arc(i).speed_kph) << "arc " << i;
    if (back.arc(i).is_ferry()) ++ferries;
    if (back.arc(i).tolled()) ++tolled;
  }
  // The toll bit went into bit 9, above the O5b access bits and still inside
  // the arc word's spare byte, so no field moved to make room for it.
  EXPECT_EQ(ferries, stats.arcs_ferry);
  EXPECT_EQ(tolled, stats.arcs_toll);
}

TEST(RoadGraphBuild, EmptyAndMissingInputsAreErrors) {
  RoadGraph g;
  EXPECT_EQ(fv::routing::BuildRoadGraph({}, {}, &g, nullptr).code, fv::kInvalidArg);
  EXPECT_EQ(fv::routing::BuildRoadGraph({KiawahPath("nope.osm")}, {}, &g, nullptr).code,
            fv::kIoError);
  EXPECT_EQ(fv::routing::BuildRoadGraph({KiawahPath("map.osm")}, {}, nullptr, nullptr).code,
            fv::kInvalidArg);
}

}  // namespace
