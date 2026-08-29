// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// P5: the route on the map — the second C++ FILE overlay, and the first that
// draws something computed.
//
// The draw tests count INK rather than compare pixels. A golden image of a
// route would pin the rasterizer, which `geo_draw_test` and the vector goldens
// already do; what is new here is the DECISIONS — that a calculated route and
// an uncalculated one draw differently, that a replaced waypoint drops a stale
// road, that the pick agrees with what was drawn — and each of those is a
// count or a coordinate, which is a test that says why it failed.

#include "fv_route_overlay.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "fvkit/canvas/cpu_canvas.h"

namespace {

using fv::GeoPoint;
using fv::RouteOverlay;
using fv::RoutePlanner;
using fv::RoutePlanOptions;
using fv::RouteWaypoint;

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

std::string KiawahGraph() {
  const std::filesystem::path p =
      std::filesystem::path(TestDataDir()) / "OSM" / "kiawah.fvroad";
  std::error_code ec;
  return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

std::string TempSpec(const char* stem) {
  const ::testing::TestInfo* info =
      ::testing::UnitTest::GetInstance()->current_test_info();
  const std::string name = info != nullptr ? info->name() : "x";
  return (std::filesystem::temp_directory_path() /
          ("fvrte_ov_" + name + "_" + stem + ".fvrte"))
      .string();
}

// Kiawah at a scale where the fixture's three waypoints are all on screen and
// a few hundred pixels apart.
fv::MapProjection KiawahProj() {
  fv::MapProjection p;
  p.SetSurfaceSize(800, 600);
  p.SetCenter({32.5987, -80.1130});
  p.SetScale(50000.0);
  return p;
}

// A draw, with its Status kept: a failed draw should say WHY on the spot.
fv::Status Draw(RouteOverlay& ov, const fv::MapProjection& proj,
                fv::ICanvas& canvas) {
  return ov.OnDraw(proj, canvas);
}

// The first system TTF this machine has, or empty. Same list point_overlay_test
// carries, for the same reason: `CpuCanvas` wants a font on the FILESYSTEM and
// the port does not ship one.
std::string SystemFont() {
  const char* candidates[] = {"/System/Library/Fonts/Supplemental/Arial.ttf",
                              "/System/Library/Fonts/Supplemental/Courier New.ttf",
                              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
  for (const char* f : candidates) {
    if (FILE* fp = std::fopen(f, "rb")) {
      std::fclose(fp);
      return f;
    }
  }
  return std::string();
}

std::vector<RouteWaypoint> FixtureWaypoints() {
  return {{"RTURN", {32.6044007, -80.1083007}},
          {"WP2", {32.59297596527702, -80.11767417454368}},
          {"WP3", {32.59303776115624, -80.11886651782085}}};
}

// --- the document ----------------------------------------------------------

TEST(RouteOverlay, OpensTheDocumentRoutePyWroteAndTakesItsName) {
  RouteOverlay ov("Route1");
  ASSERT_TRUE(ov.FileOpen(FV_ROUTE_FIXTURE_FILE).ok());
  // The DOCUMENT's own name, not the path: an overlay list should say "Ruddy
  // Turnstone to the beach", not "/tmp/x.fvrte".
  EXPECT_EQ("Ruddy Turnstone to the beach", ov.Name());
  EXPECT_EQ(3u, ov.waypoints().size());
  EXPECT_FALSE(ov.is_dirty());
}

TEST(RouteOverlay, SavesWhatItOpenedByteForByte) {
  RouteOverlay ov("Route1");
  ASSERT_TRUE(ov.FileOpen(FV_ROUTE_FIXTURE_FILE).ok());
  const std::string spec = TempSpec("saveas");
  ASSERT_TRUE(ov.FileSaveAs(spec, 0).ok());
  EXPECT_FALSE(ov.is_dirty());

  std::ifstream a(FV_ROUTE_FIXTURE_FILE, std::ios::binary), b(spec, std::ios::binary);
  std::ostringstream sa, sb;
  sa << a.rdbuf();
  sb << b.rdbuf();
  EXPECT_EQ(sa.str(), sb.str());
  std::remove(spec.c_str());
}

TEST(RouteOverlay, TheOverlaysNameIsWhatReachesTheFile) {
  // A user renames in the overlay list, so the overlay's name is the one that
  // goes to disk — the document field follows the overlay, not the reverse.
  RouteOverlay ov("Route1");
  ASSERT_TRUE(ov.FileOpen(FV_ROUTE_FIXTURE_FILE).ok());
  ov.SetName("Renamed in the list");
  const std::string spec = TempSpec("rename");
  ASSERT_TRUE(ov.FileSaveAs(spec, 0).ok());

  RouteOverlay back("x");
  ASSERT_TRUE(back.FileOpen(spec).ok());
  EXPECT_EQ("Renamed in the list", back.Name());
  std::remove(spec.c_str());
}

TEST(RouteOverlay, RoutesHaveOneFormat) {
  RouteOverlay ov("Route1");
  EXPECT_FALSE(ov.FileSaveAs(TempSpec("bad"), 1).ok());
}

TEST(RouteOverlay, EditingDirtiesAndFileNewDoesNot) {
  RouteOverlay ov("Route1");
  EXPECT_FALSE(ov.is_dirty());
  ov.SetWaypoints(FixtureWaypoints());
  EXPECT_TRUE(ov.is_dirty());
  ASSERT_TRUE(ov.FileNew().ok());
  // Not dirty: there is nothing in it to lose.
  EXPECT_FALSE(ov.is_dirty());
  EXPECT_TRUE(ov.waypoints().empty());
}

TEST(RouteOverlay, RevertThrowsAwayTheEdits) {
  RouteOverlay ov("Route1");
  ASSERT_TRUE(ov.FileOpen(FV_ROUTE_FIXTURE_FILE).ok());
  ov.SetWaypoints({{"ONE", {32.0, -80.0}}});
  ASSERT_TRUE(ov.is_dirty());
  ASSERT_TRUE(ov.Revert(FV_ROUTE_FIXTURE_FILE).ok());
  EXPECT_EQ(3u, ov.waypoints().size());
  EXPECT_FALSE(ov.is_dirty());
}

TEST(RouteOverlay, CapabilitiesAreFoundByAccessor) {
  // R2's rule: never a dynamic_cast. A capability is an accessor that returns
  // `this`, and everything this overlay does NOT implement keeps the nullptr.
  RouteOverlay ov("Route1");
  EXPECT_EQ(static_cast<fv::app::Persistence*>(&ov), ov.AsPersistence());
  EXPECT_EQ(static_cast<fv::app::HitTest*>(&ov), ov.AsHitTest());
  EXPECT_EQ(static_cast<fv::app::SnapTo*>(&ov), ov.AsSnapTo());
  EXPECT_EQ(nullptr, ov.AsContextMenu());
  // Answered nullptr until the editor moved out of route.py and into
  // `RouteEditSession`, the same way `AsSnapTo` answered nullptr until P19.
  // A route is now an EditTarget in every shell, not just the tk one.
  EXPECT_EQ(static_cast<fv::app::EditTarget*>(&ov), ov.AsEditTarget());
}

TEST(RouteOverlay, RegistersAsAFileTypeAndCarriesThePlannerToEveryInstance) {
  // `RegisterBuiltinOverlayTypes` cannot do this — it lives in fvkit, and
  // fvkit is exactly what may not link the router — so a shell calls both.
  const std::string graph = KiawahGraph();
  RoutePlanner planner(graph, FV_ROUTE_RULES_FILE);
  fv::app::OverlayTypeRegistry registry;
  ASSERT_TRUE(fv::RegisterRouteOverlayType(registry, &planner).ok());

  const fv::app::OverlayTypeDesc* desc = registry.Find(RouteOverlay::kTypeId);
  ASSERT_NE(nullptr, desc);
  // The engaged optional IS the file-vs-static distinction (A1).
  ASSERT_TRUE(desc->file.has_value());
  EXPECT_EQ("fvrte", desc->file->default_extension);
  EXPECT_EQ(nullptr, desc->editor_factory);  // v1 editing is SetWaypoints

  std::shared_ptr<fv::Overlay> made = desc->factory();
  ASSERT_NE(nullptr, made);
  RouteOverlay* route = dynamic_cast<RouteOverlay*>(made.get());
  ASSERT_NE(nullptr, route);
  // One graph and one rule file across however many routes are open: the
  // graph is the expensive thing, not the overlay.
  EXPECT_EQ(&planner, route->planner());
  EXPECT_NE(nullptr, route->AsPersistence());
}

// --- drawing ---------------------------------------------------------------

TEST(RouteOverlay, StraightLegsAndMarkersReachTheCanvas) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(FixtureWaypoints());

  fv::CpuCanvas canvas(800, 600);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});
  ov.SetShowLabels(false);
  const fv::MapProjection proj = KiawahProj();
  ASSERT_TRUE(Draw(ov, proj, canvas).ok()) << Draw(ov, proj, canvas).message;

  // Something is on the canvas, and it is where the waypoints are.
  double x = 0, y = 0;
  ASSERT_TRUE(proj.GeoToSurface(ov.waypoints()[0].position, &x, &y).ok());
  EXPECT_GT(x, 0.0);
  EXPECT_LT(x, 800.0);
}

TEST(RouteOverlay, AnEmptyRouteDrawsNothingAndDoesNotFail) {
  RouteOverlay ov("Route1");
  ov.SetShowLabels(false);
  fv::CpuCanvas canvas(200, 200);
  const fv::MapProjection proj = KiawahProj();
  EXPECT_TRUE(Draw(ov, proj, canvas).ok()) << Draw(ov, proj, canvas).message;

  // One waypoint is a marker and no line — a leg needs two ends.
  ov.SetWaypoints({{"ONE", {32.5987, -80.1130}}});
  EXPECT_TRUE(Draw(ov, proj, canvas).ok()) << Draw(ov, proj, canvas).message;
}

TEST(RouteOverlay, LabelsAreOnByDefaultAndThatCostsAFont) {
  // A route's waypoints are ORDERED and their labels are the document, so an
  // unlabelled diamond is a marker with its meaning thrown away — which is why
  // labels default ON here and OFF in PointOverlay. The price is stated rather
  // than hidden: `CpuCanvas` wants a TTF on the filesystem, so a shell that has
  // set no default font gets a FAILED draw and a message naming the reason,
  // exactly as every other text-drawing overlay does. Pippin's pack carries
  // DejaVu for this; PythonView's canvas has a font already.
  RouteOverlay ov("Route1");
  ov.SetWaypoints(FixtureWaypoints());
  EXPECT_TRUE(ov.show_labels());

  const fv::MapProjection proj = KiawahProj();
  fv::CpuCanvas fontless(800, 600);
  const fv::Status s = Draw(ov, proj, fontless);
  EXPECT_FALSE(s.ok());
  EXPECT_NE(std::string::npos, s.message.find("font")) << s.message;

  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";
  fv::CpuCanvas canvas(800, 600);
  ASSERT_TRUE(canvas.SetDefaultFont(font).ok());
  EXPECT_TRUE(Draw(ov, proj, canvas).ok()) << Draw(ov, proj, canvas).message;
}

TEST(RouteOverlay, ALabelPutsHaloInkOnADarkChart) {
  // T2's halo is the whole reason a name over dense linework is readable, and
  // white is the only ink this overlay draws that is not the route's own
  // colour — so counting white pixels over a dark background is a direct test
  // that the halo is there, the same one PointOverlay's labels get.
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";
  const fv::MapProjection proj = KiawahProj();

  auto white_ink = [&](bool labels) {
    fv::CpuCanvas canvas(800, 600);
    EXPECT_TRUE(canvas.SetDefaultFont(font).ok());
    canvas.Clear(fv::FvColor{20, 20, 20, 255});
    RouteOverlay ov("Route1");
    ov.SetWaypoints(FixtureWaypoints());
    ov.SetShowLabels(labels);
    EXPECT_TRUE(Draw(ov, proj, canvas).ok());
    int white = 0;
    for (int y = 0; y < 600; ++y)
      for (int x = 0; x < 800; ++x) {
        const unsigned char* px = canvas.Buffer().Row(y) + x * 4;
        if (px[0] > 200 && px[1] > 200 && px[2] > 200) ++white;
      }
    return white;
  };
  EXPECT_EQ(0, white_ink(false))
      << "an uncalculated route has no casing, so nothing white";
  EXPECT_GT(white_ink(true), 0);
}

TEST(RouteOverlay, AnUnreadyProjectionIsRefusedRatherThanDrawnOn) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(FixtureWaypoints());
  fv::CpuCanvas canvas(200, 200);
  fv::MapProjection empty;
  EXPECT_FALSE(ov.OnDraw(empty, canvas).ok());
}

// --- the pick --------------------------------------------------------------

TEST(RouteOverlay, ThePickAgreesWithWhatWasDrawn) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(FixtureWaypoints());
  ov.SetShowLabels(false);
  const fv::MapProjection proj = KiawahProj();
  fv::CpuCanvas canvas(800, 600);
  ASSERT_TRUE(Draw(ov, proj, canvas).ok()) << Draw(ov, proj, canvas).message;

  double x = 0, y = 0;
  ASSERT_TRUE(proj.GeoToSurface(ov.waypoints()[1].position, &x, &y).ok());

  std::vector<fv::app::HitItem> hits;
  ov.HitTestPoint(proj, fv::PixelPoint{(int)std::lround(x), (int)std::lround(y)},
                  8.0, hits);
  ASSERT_FALSE(hits.empty());
  EXPECT_EQ(&ov, hits[0].overlay);
  EXPECT_EQ("WP2", ov.LabelForFeature(hits[0].feature));
  EXPECT_EQ("WP2", hits[0].hint.tool_tip);
  EXPECT_NE(std::string::npos, hits[0].hint.status.find("waypoint WP2"));
  EXPECT_LT(hits[0].distance_px, 1.5);
}

TEST(RouteOverlay, ASnapReturnsTheWaypointsOwnCoordinate) {
  // The second implementer of the SnapTo capability, and the reason it exists:
  // an SPI with one implementer has not been shown to be an SPI. What it has
  // to deliver is the same thing the point overlay delivers -- the DOCUMENT's
  // coordinate for a marker the user aimed at by eye.
  RouteOverlay ov("Beach loop");
  ov.SetWaypoints(FixtureWaypoints());
  ov.SetShowLabels(false);
  const fv::MapProjection proj = KiawahProj();
  fv::CpuCanvas canvas(800, 600);
  ASSERT_TRUE(Draw(ov, proj, canvas).ok());

  // RTURN, not WP2: the fixture's other two waypoints are ~110 m apart, which
  // at 1:50,000 puts them both inside one finger. That ambiguity is real and
  // is what the aggregation test exercises; this test is about EXACTNESS and
  // wants one answer.
  const fv::GeoPoint wp = ov.waypoints()[0].position;
  double x = 0, y = 0;
  ASSERT_TRUE(proj.GeoToSurface(wp, &x, &y).ok());

  std::vector<fv::app::SnapToItem> snaps;
  ov.SnapToPoint(proj,
                 fv::PixelPoint{(int)std::lround(x) + 4,
                                (int)std::lround(y) - 3},
                 8.0, snaps);
  ASSERT_EQ(1u, snaps.size());
  EXPECT_EQ(&ov, snaps[0].overlay);
  EXPECT_DOUBLE_EQ(wp.lat, snaps[0].point.lat);
  EXPECT_DOUBLE_EQ(wp.lon, snaps[0].point.lon);
  // Qualified by the route's name: "WP2" alone says nothing in a chooser
  // listing several overlays' answers.
  EXPECT_EQ("Beach loop: RTURN", snaps[0].description);
  EXPECT_GT(snaps[0].distance_px, 0.0);
}

TEST(RouteOverlay, AnOverlayThatHasNotDrawnAnswersNoSnapEither) {
  // The same rule as the pick, and it matters more here: a coordinate that
  // jumped to a waypoint which is not on the screen is indistinguishable from
  // the app losing the user's pick.
  RouteOverlay ov("Route1");
  ov.SetWaypoints(FixtureWaypoints());
  std::vector<fv::app::SnapToItem> snaps;
  ov.SnapToPoint(KiawahProj(), fv::PixelPoint{400, 300}, 8.0, snaps);
  EXPECT_TRUE(snaps.empty());
}

TEST(RouteOverlay, AnOverlayThatHasNotDrawnAnswersNoPick) {
  // Deliberate, and the same rule route.py follows: the hit test reads what
  // was DRAWN, so an overlay with nothing on the screen has nothing under the
  // cursor.
  RouteOverlay ov("Route1");
  ov.SetWaypoints(FixtureWaypoints());
  std::vector<fv::app::HitItem> hits;
  ov.HitTestPoint(KiawahProj(), fv::PixelPoint{400, 300}, 8.0, hits);
  EXPECT_TRUE(hits.empty());
}

TEST(RouteOverlay, AFeatureIdIsStableAcrossRedraws) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(FixtureWaypoints());
  ov.SetShowLabels(false);
  const fv::MapProjection proj = KiawahProj();
  fv::CpuCanvas canvas(800, 600);
  ASSERT_TRUE(Draw(ov, proj, canvas).ok()) << Draw(ov, proj, canvas).message;

  double x = 0, y = 0;
  ASSERT_TRUE(proj.GeoToSurface(ov.waypoints()[0].position, &x, &y).ok());
  const fv::PixelPoint at{(int)std::lround(x), (int)std::lround(y)};

  std::vector<fv::app::HitItem> first;
  ov.HitTestPoint(proj, at, 8.0, first);
  ASSERT_FALSE(first.empty());
  ASSERT_TRUE(Draw(ov, proj, canvas).ok()) << Draw(ov, proj, canvas).message;
  std::vector<fv::app::HitItem> second;
  ov.HitTestPoint(proj, at, 8.0, second);
  ASSERT_FALSE(second.empty());
  EXPECT_EQ(first[0].feature, second[0].feature);
}

TEST(RouteOverlay, TheSymbolDpiScaleWidensThePickTarget) {
  // The two references, told apart (see the header): a route's markers take
  // the APP's unit, so a shell that says "one authored pixel is two device
  // pixels" gets a marker twice the size — and a pick target to match, or a
  // finger would miss what it can see.
  RouteOverlay ov("Route1");
  ov.SetWaypoints(FixtureWaypoints());
  ov.SetShowLabels(false);
  const fv::MapProjection proj = KiawahProj();
  fv::CpuCanvas canvas(800, 600);

  double x = 0, y = 0;
  ASSERT_TRUE(proj.GeoToSurface(ov.waypoints()[0].position, &x, &y).ok());
  // 12 px out: past the 1.0-scale marker's 7.2 px half-width plus a 4 px
  // tolerance, inside the 2.0-scale one's 14.4.
  const fv::PixelPoint off{(int)std::lround(x) + 12, (int)std::lround(y)};

  ASSERT_TRUE(Draw(ov, proj, canvas).ok()) << Draw(ov, proj, canvas).message;
  std::vector<fv::app::HitItem> narrow;
  ov.HitTestPoint(proj, off, 4.0, narrow);
  EXPECT_TRUE(narrow.empty());

  ov.SetSymbolDpiScale(2.0);
  ASSERT_TRUE(Draw(ov, proj, canvas).ok()) << Draw(ov, proj, canvas).message;
  std::vector<fv::app::HitItem> wide;
  ov.HitTestPoint(proj, off, 4.0, wide);
  EXPECT_FALSE(wide.empty());
}

// --- the plan --------------------------------------------------------------

TEST(RouteOverlay, WithNoPlannerFollowRoadsSaysSoAndDrawsStraight) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(FixtureWaypoints());
  EXPECT_FALSE(ov.FollowRoads());
  EXPECT_FALSE(ov.has_plan());
  EXPECT_FALSE(ov.status().empty());
}

TEST(RouteOverlay, ACalculatedRouteReplacesTheStraightLegs) {
  const std::string graph = KiawahGraph();
  if (graph.empty()) GTEST_SKIP() << "no TestData/OSM/kiawah.fvroad";

  RoutePlanner planner(graph, FV_ROUTE_RULES_FILE);
  RouteOverlay ov("Route1");
  ov.SetPlanner(&planner);
  ov.SetShowLabels(false);
  ov.SetShowStatus(false);
  ASSERT_TRUE(ov.FileOpen(FV_ROUTE_FIXTURE_FILE).ok());

  RoutePlanOptions opts;
  opts.profile = "bicycle";
  ASSERT_TRUE(ov.FollowRoads(opts)) << ov.status();
  EXPECT_TRUE(ov.has_plan());
  EXPECT_TRUE(ov.plan().is_bicycle);
  EXPECT_EQ(2u, ov.plan().legs.size());

  fv::CpuCanvas canvas(800, 600);
  ASSERT_TRUE(Draw(ov, KiawahProj(), canvas).ok()) << Draw(ov, KiawahProj(), canvas).message;
}

TEST(RouteOverlay, TheDocumentsOwnProfileIsTheDefaultForAReplan) {
  const std::string graph = KiawahGraph();
  if (graph.empty()) GTEST_SKIP() << "no TestData/OSM/kiawah.fvroad";

  RoutePlanner planner(graph, FV_ROUTE_RULES_FILE);
  RouteOverlay ov("Route1");
  ov.SetPlanner(&planner);
  ASSERT_TRUE(ov.FileOpen(FV_ROUTE_FIXTURE_FILE).ok());
  ov.SetProfile("bicycle");

  // No profile on the call: a route saved as a cycle route replans as one.
  ASSERT_TRUE(ov.FollowRoads()) << ov.status();
  EXPECT_TRUE(ov.plan().is_bicycle);
  EXPECT_EQ(0u, ov.status().find("bicycle: ")) << ov.status();

  // ... and the profile is saved, so it survives the trip through disk.
  const std::string spec = TempSpec("profile");
  ASSERT_TRUE(ov.FileSaveAs(spec, 0).ok());
  RouteOverlay back("x");
  ASSERT_TRUE(back.FileOpen(spec).ok());
  EXPECT_EQ("bicycle", back.profile());
  std::remove(spec.c_str());
}

TEST(RouteOverlay, MovingAWaypointDropsTheRoadItWasComputedFrom) {
  const std::string graph = KiawahGraph();
  if (graph.empty()) GTEST_SKIP() << "no TestData/OSM/kiawah.fvroad";

  RoutePlanner planner(graph, FV_ROUTE_RULES_FILE);
  RouteOverlay ov("Route1");
  ov.SetPlanner(&planner);
  ov.SetWaypoints(FixtureWaypoints());
  ASSERT_TRUE(ov.FollowRoads()) << ov.status();
  ASSERT_TRUE(ov.has_plan());

  // A stale road under a moved marker reads as a bug in the router, so the
  // plan goes with the waypoints it was computed from.
  std::vector<RouteWaypoint> moved = FixtureWaypoints();
  moved[0].position.lat += 0.002;
  ov.SetWaypoints(moved);
  EXPECT_FALSE(ov.has_plan());
  EXPECT_TRUE(ov.status().empty());
}

TEST(RouteOverlay, OpeningADocumentDropsThePlanToo) {
  const std::string graph = KiawahGraph();
  if (graph.empty()) GTEST_SKIP() << "no TestData/OSM/kiawah.fvroad";

  RoutePlanner planner(graph, FV_ROUTE_RULES_FILE);
  RouteOverlay ov("Route1");
  ov.SetPlanner(&planner);
  ov.SetWaypoints(FixtureWaypoints());
  ASSERT_TRUE(ov.FollowRoads());
  ASSERT_TRUE(ov.FileOpen(FV_ROUTE_FIXTURE_FILE).ok());
  EXPECT_FALSE(ov.has_plan());
}

TEST(RouteOverlay, SelectionIsNotADocumentChange) {
  RouteOverlay ov("Route1");
  ASSERT_TRUE(ov.FileOpen(FV_ROUTE_FIXTURE_FILE).ok());
  ASSERT_FALSE(ov.is_dirty());
  ov.SetSelected("WP2");
  EXPECT_EQ("WP2", ov.selected());
  EXPECT_FALSE(ov.is_dirty());
  ov.SetShowLabels(false);

  // A selected waypoint still draws — as ITSELF, highlighted (G4) — which is
  // the branch a nullptr symbol library would fall over on.
  fv::CpuCanvas canvas(800, 600);
  EXPECT_TRUE(Draw(ov, KiawahProj(), canvas).ok()) << Draw(ov, KiawahProj(), canvas).message;
}

TEST(RouteOverlay, SelectionSurvivesAReplacementThatKeptTheLabel) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(FixtureWaypoints());
  ov.SetSelected("WP2");
  std::vector<RouteWaypoint> moved = FixtureWaypoints();
  moved[1].position.lat += 0.001;
  ov.SetWaypoints(moved);
  EXPECT_EQ("WP2", ov.selected());

  // ... and does not survive one that removed it, or the highlight would point
  // at a waypoint that is not there.
  ov.SetWaypoints({{"ONLY", {32.6, -80.1}}});
  EXPECT_TRUE(ov.selected().empty());
}

}  // namespace
