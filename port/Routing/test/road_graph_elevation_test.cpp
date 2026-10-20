// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Elevation in the road graph: AddElevation over synthetic DEMs, the
/// GroundSampler filter, the v4 file section, and Kiawah and Atlanta over the
/// DTED cells in testdata.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "fv_road_graph.h"
#include "fvkit/formats/dted.h"

namespace fs = std::filesystem;
using fv::routing::AddElevation;
using fv::routing::ElevationOptions;
using fv::routing::ElevationSampler;
using fv::routing::ElevationStats;
using fv::routing::RoadArc;
using fv::routing::RoadGraph;

namespace {

constexpr double kLat0 = 33.0;
constexpr double kMetresPerDegLat = 111195.0;
constexpr double kPi = 3.14159265358979323846;

double NorthM(double lat) { return (lat - kLat0) * kMetresPerDegLat; }
double LatAt(double north_m) { return kLat0 + north_m / kMetresPerDegLat; }

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

fs::path ScratchDir() {
  const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
  fs::path dir = fs::temp_directory_path() / "fv_routing_test" /
                 (std::string(info->test_suite_name()) + "." + info->name());
  fs::create_directories(dir);
  return dir;
}

/// Writes an OSM file of one way per entry in `ways` (node ids, in order, and
/// extra tags) over nodes due north of kLat0 at the given distances in metres.
std::string WriteOsm(const std::vector<std::pair<int, double>>& nodes,
                     const std::vector<std::pair<std::vector<int>, std::string>>& ways) {
  const fs::path path = ScratchDir() / "elev.osm";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<osm version=\"0.6\">\n";
  for (const auto& n : nodes) {
    char line[160];
    std::snprintf(line, sizeof line, " <node id=\"%d\" lat=\"%.7f\" lon=\"-84.0000000\"/>\n",
                  n.first, LatAt(n.second));
    out << line;
  }
  int id = 100;
  for (const auto& w : ways) {
    out << " <way id=\"" << ++id << "\">";
    for (int r : w.first) out << "<nd ref=\"" << r << "\"/>";
    out << "<tag k=\"highway\" v=\"residential\"/>" << w.second << "</way>\n";
  }
  out << "</osm>\n";
  return path.string();
}

RoadGraph Build(const std::string& osm) {
  RoadGraph g;
  EXPECT_EQ(fv::routing::BuildRoadGraph({osm}, {}, &g, nullptr).code, fv::kOk);
  return g;
}

uint32_t NodeFor(const RoadGraph& g, int64_t osm_id) {
  for (uint32_t i = 0; i < g.node_count(); ++i)
    if (g.node(i).osm_id == osm_id) return i;
  ADD_FAILURE() << "no node " << osm_id;
  return 0;
}

const RoadArc& ArcFrom(const RoadGraph& g, int64_t from, int64_t to) {
  const uint32_t u = NodeFor(g, from), v = NodeFor(g, to);
  for (uint32_t a = g.arc_begin(u); a < g.arc_end(u); ++a)
    if (g.arc(a).target == v) return g.arc(a);
  ADD_FAILURE() << "no arc " << from << " -> " << to;
  return g.arc(0);
}

/// A straight road `length_m` due north as one way per 100 m, so every
/// 100 m is a graph node (OSM ids 1, 2, ...).
std::string StraightRoad(double length_m) {
  std::vector<std::pair<int, double>> nodes;
  std::vector<std::pair<std::vector<int>, std::string>> ways;
  for (int k = 0; k * 100.0 <= length_m; ++k) {
    nodes.push_back({k + 1, k * 100.0});
    if (k > 0) ways.push_back({{k, k + 1}, ""});
  }
  return WriteOsm(nodes, ways);
}

/// The DTED cells in testdata through the same filter fvgraph applies.
ElevationSampler TestDataDem(const RoadGraph& g, std::shared_ptr<fv::DtedElevationSource>* keep) {
  *keep = std::make_shared<fv::DtedElevationSource>((fs::path(TestDataDir()) / "dted").string());
  fv::DtedElevationSource* dem = keep->get();
  ElevationSampler raw = [dem](double lat, double lon, float* m) {
    return dem->GetElevation(fv::GeoPoint{lat, lon}, m).ok();
  };
  const fv::GeoRect b = g.bounds();
  double lat_deg = 0.0, lon_deg = 0.0;
  EXPECT_TRUE(dem->PostSpacing({(b.ll.lat + b.ur.lat) / 2, (b.ll.lon + b.ur.lon) / 2}, &lat_deg,
                               &lon_deg));
  const double margin = 11 * lat_deg;
  const fv::GeoRect box{{b.ll.lat - margin, b.ll.lon - margin},
                        {b.ur.lat + margin, b.ur.lon + margin}};
  return fv::routing::GroundSampler(raw, box, lat_deg, lon_deg);
}

}  // namespace

TEST(RoadGraphElevation, APlaneOfKnownGradeClimbsExactly) {
  RoadGraph g = Build(StraightRoad(1000.0));
  ElevationStats st;
  ASSERT_EQ(AddElevation([](double lat, double, float* m) {
              *m = static_cast<float>(100.0 + 0.05 * NorthM(lat));
              return true;
            }, {}, &g, &st).code, fv::kOk);
  ASSERT_TRUE(g.has_elevation());
  EXPECT_NEAR(g.node_elevation_m(NodeFor(g, 1)), 100.0, 0.05);
  EXPECT_NEAR(g.node_elevation_m(NodeFor(g, 11)), 150.0, 0.05);
  double up = 0.0, down = 0.0;
  for (int k = 1; k <= 10; ++k) {
    const RoadArc& a = ArcFrom(g, k, k + 1);
    up += a.climb_m();
    down += a.descent_m();
    // The twin carries the same edge the other way.
    EXPECT_EQ(ArcFrom(g, k + 1, k).descent_dm, a.climb_dm);
    EXPECT_EQ(ArcFrom(g, k + 1, k).climb_dm, a.descent_dm);
  }
  // Every metre is counted, the part under the hysteresis as the residual.
  EXPECT_NEAR(up, 50.0, 0.5);
  EXPECT_NEAR(down, 0.0, 0.05);
  EXPECT_EQ(st.nodes_sampled, 11);
}

TEST(RoadGraphElevation, HysteresisRejectsDemNoiseAndKeepsTheHills) {
  // One 1200 m arc (shape points only between the ends), sampled every 10 m,
  // with noise of +-1.5 m on every post.
  std::vector<std::pair<int, double>> nodes;
  std::vector<int> refs;
  for (int k = 0; k <= 12; ++k) {
    nodes.push_back({k + 1, k * 100.0});
    refs.push_back(k + 1);
  }
  const std::string osm = WriteOsm(nodes, {{refs, ""}});
  const auto noise = [](double n) {
    const double r = std::sin(std::round(n) * 12.9898) * 43758.5453;
    return 3.0 * (r - std::floor(r)) - 1.5;
  };
  ElevationOptions options;
  options.sample_step_m = 10.0;
  const auto climb = [&](const ElevationSampler& dem, double hysteresis) {
    RoadGraph g = Build(osm);
    EXPECT_EQ(g.node_count(), 2u);
    ElevationOptions o = options;
    o.hysteresis_m = hysteresis;
    EXPECT_EQ(AddElevation(dem, o, &g).code, fv::kOk);
    const RoadArc& a = ArcFrom(g, 1, 13);
    EXPECT_NEAR(a.climb_m() - a.descent_m(),
                g.node_elevation_m(NodeFor(g, 13)) - g.node_elevation_m(NodeFor(g, 1)), 0.11);
    return a.climb_m();
  };

  // Flat ground: every metre of raw climb is noise.
  const ElevationSampler flat = [&](double lat, double, float* m) {
    *m = static_cast<float>(50.0 + noise(NorthM(lat)));
    return true;
  };
  EXPECT_GT(climb(flat, 0.0), 40.0);
  EXPECT_LT(climb(flat, 3.0), 3.0);

  // Three 40 m-high waves: the true climb is 3 x 40 = 120 m.
  const ElevationSampler waves = [&](double lat, double, float* m) {
    const double n = NorthM(lat);
    *m = static_cast<float>(20.0 * std::sin(2.0 * kPi * n / 400.0) + noise(n));
    return true;
  };
  EXPECT_NEAR(climb(waves, 3.0), 120.0, 8.0);
}

TEST(RoadGraphElevation, ABridgeOverAValleyClimbsNothing) {
  // 1 -- 2 ground; 2 -- 3 -- 4 bridge with a junction at 3 mid-span; 4 -- 5
  // ground. The DEM drops 50 m into a valley between 250 m and 420 m north.
  const std::string osm = WriteOsm(
      {{1, 0.0}, {2, 222.0}, {3, 333.0}, {4, 444.0}, {5, 600.0}, {6, 333.0}},
      {{{1, 2}, ""},
       {{2, 3}, "<tag k=\"bridge\" v=\"yes\"/>"},
       {{3, 4}, "<tag k=\"bridge\" v=\"viaduct\"/>"},
       {{4, 5}, "<tag k=\"bridge\" v=\"no\"/>"},
       {{3, 6}, "<tag k=\"tunnel\" v=\"yes\"/>"}});
  RoadGraph g = Build(osm);
  ASSERT_EQ(g.node_count(), 6u);
  EXPECT_TRUE(ArcFrom(g, 2, 3).structure());
  EXPECT_TRUE(ArcFrom(g, 3, 6).structure());
  EXPECT_FALSE(ArcFrom(g, 4, 5).structure());
  EXPECT_FALSE(ArcFrom(g, 1, 2).structure());

  ElevationStats st;
  ASSERT_EQ(AddElevation([](double lat, double, float* m) {
              const double n = NorthM(lat);
              *m = static_cast<float>(n > 250.0 && n < 420.0 ? 0.0 : 50.0 + 0.01 * n);
              return true;
            }, {}, &g, &st).code, fv::kOk);
  const double e2 = g.node_elevation_m(NodeFor(g, 2));
  const double e4 = g.node_elevation_m(NodeFor(g, 4));
  EXPECT_NEAR(e2, 52.2, 0.1);
  EXPECT_NEAR(e4, 54.4, 0.1);
  // Node 3 touches only structures (6 is a dead-end tunnel), so it lies on
  // the straight line between the abutments, and 6 takes 3's height.
  EXPECT_NEAR(g.node_elevation_m(NodeFor(g, 3)), (e2 + e4) / 2, 0.1);
  EXPECT_NEAR(g.node_elevation_m(NodeFor(g, 6)), (e2 + e4) / 2, 0.1);
  EXPECT_EQ(st.nodes_interpolated, 2);
  EXPECT_EQ(st.edges_structure, 3);
  EXPECT_LT(ArcFrom(g, 2, 3).climb_m(), 1.5);
  EXPECT_EQ(ArcFrom(g, 2, 3).descent_dm, 0);
  EXPECT_EQ(ArcFrom(g, 3, 4).descent_dm, 0);
}

TEST(RoadGraphElevation, MissingDemDataIsSkippedOrInterpolated) {
  RoadGraph g = Build(StraightRoad(300.0));
  ElevationStats st;
  // No data north of 150 m: node 3 (200 m) and 4 (300 m) are filled from
  // node 2, and the samples beyond are skipped.
  ASSERT_EQ(AddElevation([](double lat, double, float* m) {
              if (NorthM(lat) > 150.0) return false;
              *m = 10.0f;
              return true;
            }, {}, &g, &st).code, fv::kOk);
  EXPECT_EQ(st.nodes_no_data, 0);
  EXPECT_EQ(st.nodes_interpolated, 2);
  EXPECT_NEAR(g.node_elevation_m(NodeFor(g, 4)), 10.0, 0.01);
  EXPECT_GT(st.samples_missing, 0);
  for (uint32_t a = 0; a < g.arc_count(); ++a) EXPECT_EQ(g.arc(a).climb_dm, 0);
}

TEST(RoadGraphElevation, SurvivesTheSaveLoadRoundTrip) {
  RoadGraph g = Build(StraightRoad(500.0));
  const fs::path plain = ScratchDir() / "plain.fvroad";
  ASSERT_EQ(g.Save(plain.string()).code, fv::kOk);
  RoadGraph loaded;
  ASSERT_EQ(RoadGraph::Load(plain.string(), &loaded).code, fv::kOk);
  EXPECT_FALSE(loaded.has_elevation());
  EXPECT_EQ(loaded.node_elevation_m(0), 0.0);

  ASSERT_EQ(AddElevation([](double lat, double, float* m) {
              *m = static_cast<float>(-20.0 + 0.08 * NorthM(lat));
              return true;
            }, {}, &g).code, fv::kOk);
  const fs::path elev = ScratchDir() / "elev.fvroad";
  ASSERT_EQ(g.Save(elev.string()).code, fv::kOk);
  EXPECT_EQ(fs::file_size(elev), fs::file_size(plain) + 4 * g.arc_count() + 4 * g.node_count());
  ASSERT_EQ(RoadGraph::Load(elev.string(), &loaded).code, fv::kOk);
  ASSERT_TRUE(loaded.has_elevation());
  for (uint32_t i = 0; i < g.node_count(); ++i)
    EXPECT_EQ(loaded.node_elevation_m(i), g.node_elevation_m(i));
  for (uint32_t a = 0; a < g.arc_count(); ++a) {
    EXPECT_EQ(loaded.arc(a).climb_dm, g.arc(a).climb_dm);
    EXPECT_EQ(loaded.arc(a).descent_dm, g.arc(a).descent_dm);
  }
  EXPECT_NEAR(loaded.node_elevation_m(NodeFor(loaded, 1)), -20.0, 0.05);

  // A truncated elevation section is refused, not read past.
  fs::resize_file(elev, fs::file_size(elev) - 2);
  EXPECT_NE(RoadGraph::Load(elev.string(), &loaded).code, fv::kOk);
}

TEST(RoadGraphElevation, GroundSamplerRemovesTowersAndPitsAndKeepsSlopes) {
  constexpr double kPost = 1.0 / 3600.0;
  const double lon0 = -84.0;
  // A 3% slope with a 3x3-post tower (+80 m) and a 3x3-post pit (-100 m).
  const auto plane = [](double lat) { return 100.0 + 0.03 * NorthM(lat); };
  const fv::GeoPoint tower{LatAt(1000.0), lon0 + 30 * kPost};
  const fv::GeoPoint pit{LatAt(2000.0), lon0 + 60 * kPost};
  const auto near = [&](const fv::GeoPoint& c, double lat, double lon) {
    return std::fabs(lat - c.lat) <= 1.5 * kPost && std::fabs(lon - c.lon) <= 1.5 * kPost;
  };
  const ElevationSampler raw = [&](double lat, double lon, float* m) {
    double e = plane(lat);
    if (near(tower, lat, lon)) e += 80.0;
    if (near(pit, lat, lon)) e -= 100.0;
    *m = static_cast<float>(e);
    return true;
  };
  const fv::GeoRect box{{kLat0, lon0}, {LatAt(3000.0), lon0 + 100 * kPost}};
  const ElevationSampler ground = fv::routing::GroundSampler(raw, box, kPost, kPost);
  float e = 0.0f;
  ASSERT_TRUE(ground(tower.lat, tower.lon, &e));
  EXPECT_NEAR(e, plane(tower.lat), 0.5);
  ASSERT_TRUE(ground(pit.lat, pit.lon, &e));
  EXPECT_NEAR(e, plane(pit.lat), 0.5);
  ASSERT_TRUE(ground(LatAt(1500.3), lon0 + 77.6 * kPost, &e));
  EXPECT_NEAR(e, plane(LatAt(1500.3)), 0.05);
  // Outside the box the raw source answers.
  ASSERT_TRUE(ground(tower.lat, lon0 - 1.0, &e));
  EXPECT_NEAR(e, plane(tower.lat), 0.05);
}

TEST(RoadGraphElevation, KiawahIsFlat) {
  const fs::path pbf = fs::path(TestDataDir()) / "OSM" / "kiawah.osm.pbf";
  const fs::path cell = fs::path(TestDataDir()) / "dted" / "w081" / "n32.dt2";
  if (!fs::exists(pbf) || !fs::exists(cell)) GTEST_SKIP() << "no Kiawah OSM or DTED2";
  RoadGraph g = Build(pbf.string());
  std::shared_ptr<fv::DtedElevationSource> dem;
  ElevationStats st;
  ASSERT_EQ(AddElevation(TestDataDem(g, &dem), {}, &g, &st).code, fv::kOk);
  EXPECT_EQ(st.nodes_no_data, 0);
  double hi = -1e9;
  for (uint32_t i = 0; i < g.node_count(); ++i) hi = std::max(hi, g.node_elevation_m(i));
  EXPECT_LT(hi, 15.0);          // measured 11 m: dunes and boardwalk ends
  EXPECT_LT(st.climb_m, 2000.0);  // measured 0.9 km over the whole island
}

TEST(RoadGraphElevation, AtlantaDowntownIsNotItsTowers) {
  const fs::path pbf = fs::path(TestDataDir()) / "OSM" / "atlanta.osm.pbf";
  const fs::path cell = fs::path(TestDataDir()) / "dted" / "w085" / "n33.dt2";
  if (!fs::exists(pbf) || !fs::exists(cell)) GTEST_SKIP() << "no Atlanta OSM or DTED2";
  RoadGraph g = Build(pbf.string());
  std::shared_ptr<fv::DtedElevationSource> dem;
  ElevationStats st;
  ASSERT_EQ(AddElevation(TestDataDem(g, &dem), {}, &g, &st).code, fv::kOk);
  EXPECT_EQ(st.nodes_no_data, 0);
  EXPECT_EQ(st.samples_missing, 0);

  // Peachtree Center and Centennial Olympic Park: on the raw surface model
  // these blocks climbed up to 100 m onto the towers (194% on one arc).
  // About 10 m of the tower bulk survives the filter (measured 18%).
  double steepest = 0.0;
  for (uint32_t u = 0; u < g.node_count(); ++u) {
    const fv::GeoPoint p = g.location(u);
    if (p.lat < 33.754 || p.lat > 33.764 || p.lon < -84.392 || p.lon > -84.384) continue;
    for (uint32_t a = g.arc_begin(u); a < g.arc_end(u); ++a) {
      const RoadArc& arc = g.arc(a);
      if (arc.length_m >= 50.0) steepest = std::max(steepest, arc.climb_m() / arc.length_m);
    }
  }
  EXPECT_LT(steepest, 0.20);

  // The radar-shadow pit at 14th Street over the Downtown Connector read
  // 177 m on the raw posts; the ground there is about 300 m.
  uint32_t n = 0;
  ASSERT_TRUE(g.NearestNode({33.78656, -84.38326}, 30.0, &n));
  EXPECT_GT(g.node_elevation_m(n), 280.0);

  double lo = 1e9, hi = -1e9;
  for (uint32_t i = 0; i < g.node_count(); ++i) {
    lo = std::min(lo, g.node_elevation_m(i));
    hi = std::max(hi, g.node_elevation_m(i));
  }
  EXPECT_GT(lo, 200.0);  // measured 217 m, the Chattahoochee
  EXPECT_LT(hi, 480.0);  // measured 467 m, toward Kennesaw Mountain
  EXPECT_GT(st.climb_m, 150e3);  // measured 189 km one way
  EXPECT_LT(st.climb_m, 230e3);
}
