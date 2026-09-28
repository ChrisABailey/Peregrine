// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Riding a beach route: the turn list onto and off the sand, the follow-mode
// snap along the water, and the sand casing on the drawn line. The Kiawah
// cases run over the shipped graph and skip without it.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fv_road_network.h"
#include "fv_route_maneuvers.h"
#include "fv_route_overlay.h"
#include "fv_route_planner.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/nav/road_snap.h"

namespace {

using fv::GeoPoint;
using fv::RoutePlan;
using fv::RoutePlanner;
using fv::RoutePlanOptions;
using fv::nav::Maneuver;
using fv::nav::ManeuverType;
using fv::routing::RoadClass;

constexpr double kPi = 3.14159265358979323846;
constexpr double kMetersPerDegLat = 6371008.8 * kPi / 180.0;

std::string KiawahGraph() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  const std::filesystem::path p =
      std::filesystem::path(env != nullptr ? env : "TestData") / "OSM" / "kiawah.fvroad";
  std::error_code ec;
  return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

#define SKIP_WITHOUT_GRAPH()                                            \
  const std::string graph_path = KiawahGraph();                         \
  if (graph_path.empty()) {                                             \
    GTEST_SKIP() << "no TestData/OSM/kiawah.fvroad; run fvgraph build"; \
  }

const GeoPoint kBoardwalk29{32.602374, -80.0838337};
const GeoPoint kBoardwalk41{32.6100451, -80.045189};

RoutePlan PlanBeachRide(RoutePlanner& planner, fv::routing::BeachUse use) {
  RoutePlanOptions opts;
  opts.profile = "bicycle";
  opts.beach = use;
  return planner.Plan({kBoardwalk29, kBoardwalk41}, opts);
}

/// The plan's legs joined end to end, shared joints dropped: the line the
/// beach stretches index.
std::vector<GeoPoint> JoinedLine(const RoutePlan& plan) {
  std::vector<GeoPoint> out;
  for (const std::vector<GeoPoint>& leg : plan.legs) {
    for (size_t j = out.empty() ? 0 : 1; j < leg.size(); ++j) out.push_back(leg[j]);
  }
  return out;
}

double Bearing(const GeoPoint& a, const GeoPoint& b) {
  const double east = (b.lon - a.lon) * std::cos(a.lat * kPi / 180.0);
  const double north = b.lat - a.lat;
  return fv::NormalizeHeadingDeg(std::atan2(east, north) * 180.0 / kPi);
}

GeoPoint Offset(const GeoPoint& p, double east_m, double north_m) {
  return {p.lat + north_m / kMetersPerDegLat,
          p.lon + east_m / (kMetersPerDegLat * std::cos(p.lat * kPi / 180.0))};
}

/// Points every `step_m` along `line`, each with the bearing of its segment.
struct Sample {
  GeoPoint at;
  double bearing_deg;
};
std::vector<Sample> Resample(const std::vector<GeoPoint>& line, double step_m) {
  std::vector<Sample> out;
  double carry = 0.0;
  for (size_t i = 0; i + 1 < line.size(); ++i) {
    const double len = fv::routing::GreatCircleMeters(line[i].lat, line[i].lon, line[i + 1].lat, line[i + 1].lon);
    const double brg = Bearing(line[i], line[i + 1]);
    for (double d = carry; d < len; d += step_m) {
      const double t = d / len;
      out.push_back({{line[i].lat + t * (line[i + 1].lat - line[i].lat),
                      line[i].lon + t * (line[i + 1].lon - line[i].lon)},
                     brg});
    }
    carry = std::fmod(carry - len, step_m);
    if (carry < 0) carry += step_m;
  }
  return out;
}

/// Deterministic uniform noise in [-1, 1].
struct Lcg {
  uint32_t s = 12345;
  double Next() {
    s = s * 1664525u + 1013904223u;
    return (s >> 8) / double(1u << 24) * 2.0 - 1.0;
  }
};

/// Rides `truth` with `noise_m` of scatter at bicycle speed through Pippin's
/// snap arrangement (every arc admitted) and returns the class of each
/// snapped fix's arc.
std::vector<RoadClass> RideAndSnap(const std::shared_ptr<const fv::routing::RoadGraph>& graph,
                                   const std::vector<Sample>& truth, double noise_m) {
  fv::routing::RoadNetworkOptions options;
  options.filter = fv::routing::RoadSnapFilter::kAll;
  auto network = std::make_shared<fv::routing::RoadGraphNetwork>(graph, options);
  fv::RoadSnapper snapper(network);
  Lcg rng;
  std::vector<RoadClass> out;
  for (size_t i = 0; i < truth.size(); ++i) {
    const GeoPoint p = Offset(truth[i].at, noise_m * rng.Next(), noise_m * rng.Next());
    fv::PositionFix f;
    f.SetPosition(p.lat, p.lon);
    f.speed_mps = 4.0;
    f.has_speed = true;
    f.hdop = 1.0;
    f.has_hdop = true;
    const fv::SnappedFix s =
        snapper.Snap(f, i > 0 ? truth[i - 1].bearing_deg : 0.0, i > 0);
    if (s.snapped) out.push_back(graph->arc(static_cast<uint32_t>(s.arc)).klass);
  }
  return out;
}

}  // namespace

// --- the turn list -----------------------------------------------------------

// Onto the sand and off it, named: "left onto Beach", then "left onto
// Boardwalk 41". The unnamed access arcs at either end are not turns of their own.
TEST(BeachRide, TheTurnsAreOntoTheBeachAndOntoTheBoardwalkOff) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  const RoutePlan plan = PlanBeachRide(planner, fv::routing::BeachUse::kWheneverPossible);
  ASSERT_TRUE(plan.found) << plan.status;
  ASSERT_EQ(1u, plan.beach.size());

  const std::vector<Maneuver>& m = plan.maneuvers;
  ASSERT_EQ(4u, m.size());
  EXPECT_EQ(ManeuverType::kDepart, m[0].type);
  EXPECT_EQ("Boardwalk 29", m[0].road);
  EXPECT_EQ(ManeuverType::kLeft, m[1].type);
  EXPECT_EQ("Beach", m[1].road);
  EXPECT_EQ("beach", m[1].klass);
  // The turn is where the sand starts, not where the boardwalk ends.
  EXPECT_NEAR(plan.beach[0].start_m, m[1].distance_m, 1.0);
  EXPECT_EQ(ManeuverType::kLeft, m[2].type);
  EXPECT_EQ("Boardwalk 41", m[2].road);
  EXPECT_NEAR(plan.beach[0].start_m + plan.beach[0].length_m, m[2].distance_m, 1.0);
  EXPECT_EQ(ManeuverType::kArrive, m[3].type);
}

namespace {

/// A hand-built route: a boardwalk, an access arc, and the beach, heading east
/// then turning north onto the sand. Lengths in metres.
fv::routing::Route BoardwalkToBeach(double boardwalk_m, double access_m) {
  fv::routing::Route r;
  r.found = true;
  const GeoPoint o{32.60, -80.08};
  r.geometry = {o, Offset(o, boardwalk_m, 0), Offset(o, boardwalk_m + access_m, 0),
                Offset(o, boardwalk_m + access_m, 200)};
  auto leg = [](const char* name, const char* klass, uint32_t begin) {
    fv::routing::RouteLeg l;
    l.name = name;
    l.klass = klass;
    l.geometry_begin = begin;
    return l;
  };
  r.legs = {leg("Boardwalk 29", "path", 0), leg("", "beach_access", 1), leg("Beach", "beach", 2)};
  return r;
}

}  // namespace

// The access leg folds into the boardwalk before it, so the turn is onto the beach.
TEST(BeachRide, TheAccessArcFoldsIntoTheWayBeforeTheBeach) {
  const fv::nav::RouteShape shape = fv::ManeuverShapeOf(BoardwalkToBeach(60, 30));
  ASSERT_EQ(2u, shape.legs.size());
  EXPECT_EQ("Boardwalk 29", shape.legs[0].name);
  EXPECT_EQ("Beach", shape.legs[1].name);
  EXPECT_EQ(2u, shape.legs[1].geometry_begin);
}

// Leaving the sand, the access leg takes the way it leads to.
TEST(BeachRide, TheAccessArcFoldsIntoTheWayAfterTheBeach) {
  fv::routing::Route r = BoardwalkToBeach(60, 30);
  std::reverse(r.geometry.begin(), r.geometry.end());
  r.legs[0].name = "Beach";
  r.legs[0].klass = "beach";
  r.legs[1].geometry_begin = 1;
  r.legs[2].name = "Boardwalk 29";
  r.legs[2].klass = "path";
  r.legs[2].geometry_begin = 2;
  const fv::nav::RouteShape shape = fv::ManeuverShapeOf(r);
  ASSERT_EQ(2u, shape.legs.size());
  EXPECT_EQ("Boardwalk 29", shape.legs[1].name);
  EXPECT_EQ(1u, shape.legs[1].geometry_begin);
}

// A ride that starts a few metres from the sand: the turn onto it is inside
// end_margin_m and not announced, so the departure names the beach instead.
TEST(BeachRide, AStartBesideTheSandDepartsOntoTheBeach) {
  const std::vector<Maneuver> m = fv::ManeuversOf(BoardwalkToBeach(4, 11));
  ASSERT_EQ(2u, m.size());
  EXPECT_EQ(ManeuverType::kDepart, m[0].type);
  EXPECT_EQ("Beach", m[0].road);

  // Far enough back, the turn is announced and the departure is the boardwalk.
  const std::vector<Maneuver> far = fv::ManeuversOf(BoardwalkToBeach(40, 11));
  ASSERT_EQ(3u, far.size());
  EXPECT_EQ("Boardwalk 29", far[0].road);
  EXPECT_EQ(ManeuverType::kLeft, far[1].type);
  EXPECT_EQ("Beach", far[1].road);
}

// --- the snap ----------------------------------------------------------------

// A ride along the water snaps to the beach run, not to a path behind the dunes.
TEST(BeachRide, ARideAlongTheWaterSnapsToTheBeach) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  const RoutePlan plan = PlanBeachRide(planner, fv::routing::BeachUse::kWheneverPossible);
  ASSERT_TRUE(plan.found) << plan.status;
  ASSERT_EQ(1u, plan.beach.size());
  const std::vector<GeoPoint> line = JoinedLine(plan);
  const std::vector<GeoPoint> sand(line.begin() + plan.beach[0].geometry_begin,
                                   line.begin() + plan.beach[0].geometry_end + 1);

  const std::vector<Sample> truth = Resample(sand, 4.0);
  ASSERT_GT(truth.size(), 800u);
  const std::vector<RoadClass> got = RideAndSnap(planner.graph(), truth, 8.0);
  ASSERT_GT(got.size(), truth.size() * 95 / 100);
  size_t on_beach = 0;
  for (RoadClass k : got) on_beach += k == RoadClass::kBeach;
  EXPECT_GT(on_beach, got.size() * 98 / 100) << on_beach << " of " << got.size();
}

// A rider on Beachwalker Drive, the road nearest the beach at the island's
// west end, stays on it and is never put on the sand.
TEST(BeachRide, ARiderOnBeachwalkerDriveIsNotPulledOntoTheSand) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  ASSERT_TRUE(planner.EnsureGraph().ok());
  const auto graph = planner.graph();

  std::vector<Sample> truth;
  for (uint32_t u = 0; u < graph->node_count(); ++u) {
    for (uint32_t ai = graph->arc_begin(u); ai < graph->arc_end(u); ++ai) {
      const fv::routing::RoadArc& a = graph->arc(ai);
      if (a.target < u || graph->name(a.name) != "Beachwalker Drive") continue;
      std::vector<GeoPoint> shape{graph->location(u)};
      for (uint32_t g = 0; g < a.geom_count; ++g) shape.push_back(graph->arc_point(a, g));
      shape.push_back(graph->location(a.target));
      for (const Sample& s : Resample(shape, 4.0)) truth.push_back(s);
    }
  }
  ASSERT_GT(truth.size(), 100u) << "no Beachwalker Drive in the graph";

  const std::vector<RoadClass> got = RideAndSnap(graph, truth, 8.0);
  ASSERT_GT(got.size(), truth.size() * 95 / 100);
  for (RoadClass k : got) {
    EXPECT_NE(RoadClass::kBeach, k);
    EXPECT_NE(RoadClass::kBeachAccess, k);
  }
}

// --- the drawn line ----------------------------------------------------------

namespace {

size_t CountColour(const fv::PixelBuffer& buf, const fv::FvColor& c) {
  size_t n = 0;
  for (int y = 0; y < buf.Height(); ++y) {
    const unsigned char* row = buf.Row(y);
    for (int x = 0; x < buf.Width(); ++x) {
      const unsigned char* p = row + 4 * x;
      n += p[0] == c.r && p[1] == c.g && p[2] == c.b;
    }
  }
  return n;
}

size_t SandPixels(RoutePlanner& planner, fv::RouteBeach use) {
  fv::RouteOverlay ov("beach");
  ov.SetPlanner(&planner);
  ov.SetWaypoints({{"BW29", kBoardwalk29}, {"BW41", kBoardwalk41}});
  ov.SetProfile("bicycle");
  ov.SetBeach(use);
  if (!ov.FollowRoads()) return 0;
  ov.SetShowLabels(false);
  ov.SetShowStatus(false);
  fv::MapProjection proj;
  proj.SetSurfaceSize(800, 400);
  proj.SetCenter({32.6062, -80.0645});
  proj.SetScale(25000.0);
  fv::CpuCanvas canvas(800, 400);
  if (!ov.OnDraw(proj, canvas).ok()) return 0;
  return CountColour(canvas.Buffer(), fv::RouteOverlay::kBeachCasingColor);
}

}  // namespace

// The beach stretch wears the sand casing; a route with no beach has none.
TEST(BeachRide, TheBeachStretchIsDrawnWithASandCasing) {
  SKIP_WITHOUT_GRAPH();
  RoutePlanner planner(graph_path, FV_ROUTE_RULES_FILE);
  EXPECT_GT(SandPixels(planner, fv::RouteBeach::kWheneverPossible), 1000u);
  EXPECT_EQ(0u, SandPixels(planner, fv::RouteBeach::kNever));
}
