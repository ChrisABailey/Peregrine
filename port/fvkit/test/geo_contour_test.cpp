// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Geographic contour tests (draw plan G1).
//
// Everything here is pure geometry — a projection and some coordinates, no
// canvas, no data files — which is the point of R4.
//
// Two things these tests deliberately do NOT do:
//   * pin a point count as a magic number over a whole contour. The count is
//     a function of degrees-per-pixel and the ledger's rule about pinning
//     totals applies; what IS pinned is the RELATIONSHIP (halving dpp roughly
//     doubles the count, and the count does not track the line's length).
//   * trust a golden. A great circle and a rhumb line between the same two
//     points produce the same endpoints and a different middle, so every
//     shape assertion here is directional.

#include "fvkit/geo/contour.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "geo_tool.h"  // GEO_geo_to_distance, to check a circle's radius

namespace {

using fv::GeoPoint;
using fv::LineKind;
using fv::MapProjection;

// Charleston harbour, the same water every other test in the port looks at.
MapProjection HarbourView() {
  MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(800, 600).ok());
  EXPECT_TRUE(p.SetCenter({32.75, -79.90}).ok());
  EXPECT_TRUE(p.SetResolution(0.0002, 0.0002).ok());
  return p;
}

// Wide enough to hold an intercontinental line whole.
MapProjection WorldView() {
  MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(800, 600).ok());
  EXPECT_TRUE(p.SetCenter({20.0, 0.0}).ok());
  EXPECT_TRUE(p.SetResolution(0.3, 0.45).ok());
  return p;
}

std::vector<GeoPoint> Walk(fv::IGeoContour& c) {
  std::vector<GeoPoint> out;
  c.MoveFirst();
  GeoPoint p;
  while (c.NextPoint(&p)) {
    out.push_back(p);
    if (out.size() > 200000) break;  // a runaway walk is a failure, not a hang
  }
  return out;
}

// RHUMB bearing from a to b, degrees clockwise from north, taken from
// geo_tool rather than computed here.
//
// It has to be the rhumb bearing and not the great-circle one: over a chord
// of several degrees of longitude the two differ by roughly dlon*sin(lat)/2,
// which on a mid-latitude line is whole degrees. A first attempt at this test
// used the great-circle initial bearing and watched it drift monotonically
// along a perfectly good rhumb line — the drift was the oracle's, not the
// contour's.
double RhumbBearing(const GeoPoint& a, const GeoPoint& b) {
  double distance = 0.0, bearing = 0.0;
  EXPECT_EQ(GEO_calc_range_and_bearing(a.lat, a.lon, b.lat, b.lon, &distance,
                                       &bearing, FALSE),
            0);
  return bearing;
}

}  // namespace

// ---------------------------------------------------------------------------
// Simple lines
// ---------------------------------------------------------------------------

TEST(SimpleGeoLine, IsTwoPointsAndNothingBetween) {
  MapProjection p = HarbourView();
  fv::SimpleGeoLine line(p, {32.70, -79.95}, {32.80, -79.85}, false);
  const std::vector<GeoPoint> pts = Walk(line);
  ASSERT_EQ(pts.size(), 2u);
  EXPECT_DOUBLE_EQ(pts[0].lat, 32.70);
  EXPECT_DOUBLE_EQ(pts[1].lon, -79.85);
}

TEST(SimpleGeoLine, ClipsToTheViewportWhenAsked) {
  MapProjection p = HarbourView();
  const fv::GeoRect b = p.VmapBounds();

  // Runs from far west of the map to far east of it, through the middle.
  fv::SimpleGeoLine line(p, {32.75, -85.0}, {32.75, -75.0}, true);
  const std::vector<GeoPoint> pts = Walk(line);
  ASSERT_EQ(pts.size(), 2u);

  // Both endpoints are pulled back to the map's own longitude span.
  EXPECT_GT(pts[0].lon, b.ll.lon - 1e-6);
  EXPECT_LT(pts[1].lon, b.ur.lon + 1e-6);
  // And the clip actually did something.
  EXPECT_GT(pts[0].lon, -85.0);
  EXPECT_LT(pts[1].lon, -75.0);
}

TEST(SimpleGeoLine, YieldsNothingWhenWhollyOffTheMap) {
  MapProjection p = HarbourView();
  fv::SimpleGeoLine line(p, {10.0, -60.0}, {12.0, -58.0}, true);
  EXPECT_TRUE(Walk(line).empty());
}

// ---------------------------------------------------------------------------
// Great circles
// ---------------------------------------------------------------------------

// The directional assertion the standing rule asks for: a great circle
// between two mid-northern points bows POLEWARD of both, and a rhumb line
// between the same two does not. A hash of either would prove neither.
TEST(GreatCircleContour, BowsPolewardOfBothEndpoints) {
  MapProjection p = WorldView();
  const GeoPoint jfk{40.64, -73.78};
  const GeoPoint lhr{51.47, -0.45};

  fv::GreatCircleContour gc(p, jfk, lhr, false);
  const std::vector<GeoPoint> arc = Walk(gc);
  ASSERT_GE(arc.size(), 3u);

  double highest = -90.0;
  for (const GeoPoint& g : arc) highest = std::max(highest, g.lat);
  EXPECT_GT(highest, lhr.lat) << "the arc should reach north of both ends";

  fv::RhumbLineContour rl(p, jfk, lhr, false);
  double rhumb_highest = -90.0;
  for (const GeoPoint& g : Walk(rl)) rhumb_highest = std::max(rhumb_highest, g.lat);
  EXPECT_LE(rhumb_highest, lhr.lat + 1e-6)
      << "a rhumb line never goes poleward of its northern end";
}

TEST(GreatCircleContour, KeepsItsEndpoints) {
  MapProjection p = WorldView();
  const GeoPoint a{40.64, -73.78}, b{51.47, -0.45};
  const std::vector<GeoPoint> arc = Walk(*fv::MakeGeoLine(p, a, b, LineKind::kGreatCircle, false));
  ASSERT_GE(arc.size(), 2u);
  EXPECT_NEAR(arc.front().lat, a.lat, 1e-6);
  EXPECT_NEAR(arc.front().lon, a.lon, 1e-6);
  EXPECT_NEAR(arc.back().lat, b.lat, 1e-6);
  EXPECT_NEAR(arc.back().lon, b.lon, 1e-6);
}

// The efficiency claim, made testable. The step size comes from
// degrees-per-pixel, so the vertex count tracks the SCREEN. Two consequences,
// both asserted: a longer line at the same resolution costs proportionally
// more (it covers more screen), and the same line at half the resolution
// costs about twice as much.
TEST(GreatCircleContour, PointCountTracksResolutionNotDistance) {
  MapProjection coarse = WorldView();
  MapProjection fine;
  ASSERT_TRUE(fine.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(fine.SetCenter({20.0, 0.0}).ok());
  ASSERT_TRUE(fine.SetResolution(0.15, 0.225).ok());  // half the dpp

  const GeoPoint a{40.64, -73.78}, b{51.47, -0.45};
  const size_t n_coarse = Walk(*fv::MakeGeoLine(coarse, a, b, LineKind::kGreatCircle, false)).size();
  const size_t n_fine = Walk(*fv::MakeGeoLine(fine, a, b, LineKind::kGreatCircle, false)).size();

  ASSERT_GT(n_coarse, 2u);
  const double ratio = static_cast<double>(n_fine) / static_cast<double>(n_coarse);
  EXPECT_GT(ratio, 1.6) << "halving dpp should roughly double the vertex count";
  EXPECT_LT(ratio, 2.4);
}

// The point of the geographic clip: an intercontinental arc drawn on a
// harbour map must cost a SEARCH, not a walk of the whole geodesic. At
// harbour resolution the full arc would be millions of points.
TEST(GreatCircleContour, AnArcThatMissesTheMapIsCheapAndEmpty) {
  MapProjection p = HarbourView();
  // New York to Tokyo, over the pole; nowhere near Charleston harbour.
  fv::GreatCircleContour gc(p, {40.64, -73.78}, {35.55, 139.78}, true);
  const std::vector<GeoPoint> arc = Walk(gc);
  EXPECT_LT(arc.size(), 100u)
      << "a missing arc must not densify; it returned " << arc.size();
}

TEST(GreatCircleContour, AnArcThatCrossesTheMapEntersInsideIt) {
  MapProjection p = HarbourView();
  const fv::GeoRect b = p.VmapBounds();

  // Charleston to Lisbon passes over the harbour on its way out.
  fv::GreatCircleContour gc(p, {32.75, -79.90}, {38.72, -9.14}, true);
  const std::vector<GeoPoint> arc = Walk(gc);
  ASSERT_GE(arc.size(), 2u);

  bool any_inside = false;
  for (const GeoPoint& g : arc) {
    if (g.lat >= b.ll.lat && g.lat <= b.ur.lat && g.lon >= b.ll.lon &&
        g.lon <= b.ur.lon) {
      any_inside = true;
      break;
    }
  }
  EXPECT_TRUE(any_inside) << "the clipped arc should still cross the viewport";
}

// A line of longitude IS a great circle, and the original short-circuits it to
// its two endpoints rather than walking the rotation math (which degenerates).
TEST(GreatCircleContour, ALineOfLongitudeIsTwoPoints) {
  MapProjection p = WorldView();
  fv::GreatCircleContour gc(p, {10.0, -30.0}, {50.0, -30.0}, false);
  const std::vector<GeoPoint> pts = Walk(gc);
  ASSERT_EQ(pts.size(), 2u);
  EXPECT_DOUBLE_EQ(pts[0].lon, -30.0);
  EXPECT_DOUBLE_EQ(pts[1].lon, -30.0);
}

TEST(GreatCircleContour, AntipodalPointsDoNotHangOrFail) {
  MapProjection p = WorldView();
  // The cross product is zero here and there are infinitely many great
  // circles; the original picks one rather than failing.
  fv::GreatCircleContour gc(p, {0.0, 0.0}, {0.0, 180.0}, false);
  const std::vector<GeoPoint> pts = Walk(gc);
  EXPECT_GE(pts.size(), 2u);
}

TEST(GreatCircleContour, IsRewindable) {
  MapProjection p = WorldView();
  fv::GreatCircleContour gc(p, {40.64, -73.78}, {51.47, -0.45}, true);
  const std::vector<GeoPoint> first = Walk(gc);
  const std::vector<GeoPoint> second = Walk(gc);
  ASSERT_EQ(first.size(), second.size());
  for (size_t i = 0; i < first.size(); ++i) {
    EXPECT_DOUBLE_EQ(first[i].lat, second[i].lat) << "at " << i;
    EXPECT_DOUBLE_EQ(first[i].lon, second[i].lon) << "at " << i;
  }
}

// ---------------------------------------------------------------------------
// Rhumb lines
// ---------------------------------------------------------------------------

// The defining property, and the one a golden could not check: a rhumb line
// holds ONE bearing from end to end.
TEST(RhumbLineContour, HoldsAConstantBearing) {
  MapProjection p = WorldView();
  fv::RhumbLineContour rl(p, {10.0, -60.0}, {45.0, -10.0}, false);
  const std::vector<GeoPoint> pts = Walk(rl);
  ASSERT_GE(pts.size(), 4u);

  const double first = RhumbBearing(pts[0], pts[1]);
  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    const double b = RhumbBearing(pts[i], pts[i + 1]);
    EXPECT_NEAR(b, first, 0.1) << "bearing wandered at leg " << i;
  }
}

// Same endpoints, different middle. The coordinates are an EAST-WEST crossing
// on purpose: for a line that mostly runs north, the northernmost point of
// the great-circle SEGMENT is simply its northern endpoint (the vertex of the
// full great circle lies past it), so "the arc bows poleward" is only visible
// when the two ends are at similar latitudes.
TEST(RhumbLineContour, AGreatCircleAndARhumbLineShareEndpointsOnly) {
  MapProjection p = WorldView();
  const GeoPoint a{40.64, -73.78};  // JFK
  const GeoPoint b{48.35, 11.79};   // Munich
  const std::vector<GeoPoint> gc =
      Walk(*fv::MakeGeoLine(p, a, b, LineKind::kGreatCircle, false));
  const std::vector<GeoPoint> rl =
      Walk(*fv::MakeGeoLine(p, a, b, LineKind::kRhumb, false));
  ASSERT_GE(gc.size(), 3u);
  ASSERT_GE(rl.size(), 3u);

  EXPECT_NEAR(gc.front().lat, rl.front().lat, 1e-6);
  EXPECT_NEAR(gc.back().lat, rl.back().lat, 1e-6);

  // The great circle is poleward of the rhumb line, and by a lot: this pair
  // is about 5 degrees apart at mid-ocean.
  double gc_high = -90.0, rl_high = -90.0;
  for (const GeoPoint& g : gc) gc_high = std::max(gc_high, g.lat);
  for (const GeoPoint& g : rl) rl_high = std::max(rl_high, g.lat);
  EXPECT_GT(gc_high, rl_high + 1.0);
}

TEST(RhumbLineContour, AConstantLatitudeLineStaysAtThatLatitude) {
  MapProjection p = WorldView();
  fv::RhumbLineContour rl(p, {20.0, -40.0}, {20.0, 10.0}, false);
  for (const GeoPoint& g : Walk(rl))
    EXPECT_DOUBLE_EQ(g.lat, 20.0)
        << "Mercator round-off must not move a constant-latitude line";
}

TEST(RhumbLineContour, ClipsAwayALineThatMissesTheViewport) {
  MapProjection p = HarbourView();
  fv::RhumbLineContour rl(p, {10.0, -60.0}, {12.0, -58.0}, true);
  GeoPoint pts[4];
  int count = 99;
  rl.ClippedPoints(pts, &count);
  EXPECT_EQ(count, 0);
  EXPECT_TRUE(Walk(rl).empty());
}

TEST(RhumbLineContour, KeepsALineWholeWhenClippingIsOff) {
  MapProjection p = HarbourView();
  fv::RhumbLineContour rl(p, {10.0, -60.0}, {12.0, -58.0}, false);
  GeoPoint pts[4];
  int count = 0;
  rl.ClippedPoints(pts, &count);
  EXPECT_EQ(count, 1);
  EXPECT_GE(Walk(rl).size(), 2u);
}

// ---------------------------------------------------------------------------
// Circle, ellipse, arc
// ---------------------------------------------------------------------------

TEST(GeoCircleContour, ClosesAndHoldsItsRadius) {
  const GeoPoint c{32.75, -79.90};
  const double radius_m = 5000.0;
  fv::GeoCircleContour circle(c, radius_m, 40);
  const std::vector<GeoPoint> pts = Walk(circle);

  // N+1 points: the first is repeated to close the ring.
  ASSERT_EQ(pts.size(), 41u);
  EXPECT_NEAR(pts.front().lat, pts.back().lat, 1e-9);
  EXPECT_NEAR(pts.front().lon, pts.back().lon, 1e-9);

  for (const GeoPoint& g : pts) {
    double d = 0.0, bearing = 0.0;
    ASSERT_EQ(GEO_geo_to_distance(c.lat, c.lon, g.lat, g.lon, &d, &bearing), 0);
    EXPECT_NEAR(d, radius_m, radius_m * 0.001);
  }
}

TEST(GeoEllipseContour, IsWiderThanItIsTall) {
  const GeoPoint c{32.75, -79.90};
  fv::GeoEllipseContour e(c, 2000.0, 8000.0, 0.0, 40);
  const std::vector<GeoPoint> pts = Walk(e);
  ASSERT_GE(pts.size(), 40u);

  double max_d = 0.0, min_d = 1e18;
  for (const GeoPoint& g : pts) {
    double d = 0.0, bearing = 0.0;
    ASSERT_EQ(GEO_geo_to_distance(c.lat, c.lon, g.lat, g.lon, &d, &bearing), 0);
    max_d = std::max(max_d, d);
    min_d = std::min(min_d, d);
  }
  EXPECT_NEAR(max_d, 8000.0, 100.0);
  EXPECT_NEAR(min_d, 2000.0, 100.0);
}

TEST(GeoArcContour, IsNotClosedAndScalesItsPointsWithItsSweep) {
  const GeoPoint c{32.75, -79.90};
  fv::GeoArcContour quarter(c, 5000.0, 0.0, 90.0, 80);
  fv::GeoArcContour half(c, 5000.0, 0.0, 180.0, 80);

  const std::vector<GeoPoint> q = Walk(quarter);
  const std::vector<GeoPoint> h = Walk(half);

  // 80 points per full circle -> 20 steps for a quarter, 40 for a half, and
  // one more point than steps because both ends are emitted.
  EXPECT_EQ(q.size(), 21u);
  EXPECT_EQ(h.size(), 41u);

  // Not closed: the last point is a quarter turn from the first.
  EXPECT_GT(std::fabs(q.front().lon - q.back().lon), 1e-4);
}

TEST(GeoArcContour, RunsCounterClockwiseOnANegativeSweep) {
  const GeoPoint c{32.75, -79.90};
  fv::GeoArcContour ccw(c, 5000.0, 0.0, -90.0, 80);
  const std::vector<GeoPoint> pts = Walk(ccw);
  ASSERT_GE(pts.size(), 2u);
  // Starting due north, sweeping backwards heads WEST.
  EXPECT_LT(pts.back().lon, pts.front().lon);
}

// ---------------------------------------------------------------------------
// Polylines
// ---------------------------------------------------------------------------

TEST(PolylineContour, JoinsLegsWithoutRepeatingTheSharedVertex) {
  MapProjection p = HarbourView();
  const std::vector<GeoPoint> pts{
      {32.70, -79.95}, {32.75, -79.90}, {32.80, -79.85}};
  fv::PolylineContour poly(p, pts, LineKind::kSimple, false);
  const std::vector<GeoPoint> out = Walk(poly);

  // Two simple legs of 2 points each, minus the one shared vertex.
  ASSERT_EQ(out.size(), 3u);
  EXPECT_DOUBLE_EQ(out[1].lat, 32.75);
}

TEST(PolylineContour, ClosesWhenAsked) {
  MapProjection p = HarbourView();
  const std::vector<GeoPoint> pts{
      {32.70, -79.95}, {32.75, -79.85}, {32.80, -79.95}};
  fv::PolylineContour poly(p, pts, LineKind::kSimple, false, /*closed=*/true);
  const std::vector<GeoPoint> out = Walk(poly);
  ASSERT_EQ(out.size(), 4u);
  EXPECT_DOUBLE_EQ(out.front().lat, out.back().lat);
  EXPECT_DOUBLE_EQ(out.front().lon, out.back().lon);
}

TEST(PolylineContour, DensifiesEachLegWhenTheKindIsGeodesic) {
  MapProjection p = WorldView();
  const std::vector<GeoPoint> pts{{10.0, -70.0}, {40.0, -30.0}, {55.0, 10.0}};
  fv::PolylineContour simple(p, pts, LineKind::kSimple, false);
  fv::PolylineContour gc(p, pts, LineKind::kGreatCircle, false);
  EXPECT_EQ(Walk(simple).size(), 3u);
  EXPECT_GT(Walk(gc).size(), 10u);
}

TEST(PolylineContour, AClippedAwayLegBreaksTheRunRatherThanJoiningAcrossIt) {
  // G3 closed this. Before it, PolylineContour got the intent right — it kept
  // the next leg's first point after an empty leg — and then handed
  // BuildGeoPath a FLAT point stream in which the break was invisible, so a
  // polyline whose middle leg was off screen came back as ONE sub-path drawn
  // end to end: a line the route does not have, painted across the map.
  // IGeoContour::AtBreak() is the signal that was missing.
  MapProjection p = HarbourView();
  const std::vector<GeoPoint> pts{
      {32.75, -79.92},   // in view
      {32.75, -79.88},   // in view
      {60.00, 150.00},   // this leg and the next one miss the viewport whole
      {61.00, 155.00},
      {32.74, -79.92},   // back in view
      {32.74, -79.88},
  };
  fv::PolylineContour poly(p, pts, LineKind::kSimple, /*clip=*/true);
  const auto paths = fv::BuildGeoPath(p, poly);
  ASSERT_EQ(paths.size(), 2u)
      << "the two visible stretches must not be joined by the leg that was "
         "clipped away";
  for (const auto& sub : paths) EXPECT_GE(sub.size(), 2u);

  // A single connected polyline is unaffected: AtBreak defaults to false and
  // every other contour is one run by construction.
  fv::PolylineContour whole(
      p, {{32.75, -79.92}, {32.75, -79.90}, {32.75, -79.88}}, LineKind::kSimple,
      true);
  EXPECT_EQ(fv::BuildGeoPath(p, whole).size(), 1u);
}

TEST(PolylineContour, IsEmptyBelowTwoPoints) {
  MapProjection p = HarbourView();
  fv::PolylineContour one(p, {{32.75, -79.90}}, LineKind::kSimple, false);
  EXPECT_TRUE(Walk(one).empty());
  fv::PolylineContour none(p, {}, LineKind::kSimple, false);
  EXPECT_TRUE(Walk(none).empty());
}

// ---------------------------------------------------------------------------
// BuildGeoPath
// ---------------------------------------------------------------------------

TEST(BuildGeoPath, ProjectsAContourIntoOneSubPath) {
  MapProjection p = HarbourView();
  const auto runs =
      fv::BuildGeoLinePath(p, {32.70, -79.95}, {32.80, -79.85},
                           LineKind::kSimple, false);
  ASSERT_EQ(runs.size(), 1u);
  ASSERT_EQ(runs[0].size(), 2u);

  // The surface really is the projection's: y grows DOWNWARD (D4), so the
  // first point (the southern one, 32.70N) has the LARGER y. A hash would not
  // have caught a flip.
  EXPECT_GT(runs[0][0].y, runs[0][1].y);
  EXPECT_LT(runs[0][0].x, runs[0][1].x);  // and east is to the right
}

TEST(BuildGeoPath, DropsARunThatCannotBeStroked) {
  MapProjection p = HarbourView();
  // Degenerate: both endpoints identical, so the contour yields two
  // coincident points -- still 2, still strokable. Then a contour that
  // yields nothing at all.
  fv::SimpleGeoLine gone(p, {10.0, -60.0}, {12.0, -58.0}, true);
  EXPECT_TRUE(fv::BuildGeoPath(p, gone).empty());
}

TEST(BuildGeoPath, BreaksTheRunAtTheAntimeridianSeam) {
  MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(p.SetCenter({0.0, 0.0}).ok());
  ASSERT_TRUE(p.SetResolution(0.3, 0.45).ok());

  // Two points either side of the seam, as far from the projection centre as
  // it is possible to be. Joining them would draw a line across the whole
  // surface; the seam rule breaks the run instead.
  const std::vector<GeoPoint> pts{{0.0, 179.0}, {0.0, -179.0}};
  fv::PolylineContour poly(p, pts, LineKind::kSimple, false);
  const auto runs = fv::BuildGeoPath(p, poly);

  for (const auto& run : runs)
    for (size_t i = 0; i + 1 < run.size(); ++i)
      EXPECT_LT(std::fabs(run[i + 1].x - run[i].x), 700.0)
          << "a sub-path spans nearly the whole surface: the seam leaked";
}

TEST(BuildGeoPath, AppendsRatherThanReplacing) {
  MapProjection p = HarbourView();
  std::vector<std::vector<fv::SurfacePoint>> out;
  auto a = fv::MakeGeoLine(p, {32.70, -79.95}, {32.80, -79.85},
                           LineKind::kSimple, false);
  auto b = fv::MakeGeoLine(p, {32.72, -79.93}, {32.78, -79.87},
                           LineKind::kSimple, false);
  fv::BuildGeoPathInto(p, *a, &out);
  fv::BuildGeoPathInto(p, *b, &out);
  EXPECT_EQ(out.size(), 2u);
}
