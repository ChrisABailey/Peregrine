// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// VectorRenderer tests (vpf-geosym plan phase V5b).
//
// The renderer is the SHARED side of the vector seam, so everything here is
// synthetic on purpose: a stub IVectorSource and a stub IStyleEngine. If this
// file ever needs VPF or GeoSym to pass, the seam has leaked.
//
// The GeoSym/DNC end-to-end render (real assets, golden hash) lives in
// port/GeoSymServer/test/geosym_style_test.cpp.

#include "fvkit/vector/renderer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"

namespace {

using fv::ClipPolygon;
using fv::ClipPolyline;
using fv::PixelPoint;
using fv::SurfacePoint;

// --- clipping --------------------------------------------------------------

TEST(VectorClip, PolylineWhollyInsideIsUnchanged) {
  std::vector<SurfacePoint> pts{{10, 10}, {20, 30}, {40, 15}};
  auto runs = ClipPolyline(pts, 64, 64);
  ASSERT_EQ(runs.size(), 1u);
  ASSERT_EQ(runs[0].size(), 3u);
  EXPECT_EQ(runs[0][0].x, 10);
  EXPECT_EQ(runs[0][2].y, 15);
}

TEST(VectorClip, PolylineWhollyOutsideDisappears) {
  std::vector<SurfacePoint> pts{{-50, -50}, {-20, -30}};
  EXPECT_TRUE(ClipPolyline(pts, 64, 64).empty());
}

TEST(VectorClip, PolylineCrossingTheEdgeIsCutAtTheBoundary) {
  // Horizontal line from off-left to the middle: enters at x = 0.
  std::vector<SurfacePoint> pts{{-30, 20}, {30, 20}};
  auto runs = ClipPolyline(pts, 64, 64);
  ASSERT_EQ(runs.size(), 1u);
  ASSERT_EQ(runs[0].size(), 2u);
  EXPECT_EQ(runs[0][0].x, 0);
  EXPECT_EQ(runs[0][0].y, 20);
  EXPECT_EQ(runs[0][1].x, 30);
}

TEST(VectorClip, PolylineLeavingAndReenteringYieldsTwoRuns) {
  // in -> out the top -> back in: two separate visible runs.
  std::vector<SurfacePoint> pts{{10, 10}, {20, -40}, {30, -40}, {40, 10}};
  auto runs = ClipPolyline(pts, 64, 64);
  ASSERT_EQ(runs.size(), 2u);
  EXPECT_EQ(runs[0].front().x, 10);
  EXPECT_EQ(runs[1].back().x, 40);
}

TEST(VectorClip, PolygonIsClippedToTheViewport) {
  // A square hanging off the left edge; the survivor is the right half.
  std::vector<SurfacePoint> ring{{-20, 10}, {30, 10}, {30, 50}, {-20, 50}};
  auto out = ClipPolygon(ring, 64, 64);
  ASSERT_GE(out.size(), 3u);
  for (const PixelPoint& p : out) {
    EXPECT_GE(p.x, 0);
    EXPECT_LE(p.x, 63);
  }
}

TEST(VectorClip, PolygonEntirelyOutsideIsEmpty) {
  std::vector<SurfacePoint> ring{{100, 100}, {140, 100}, {120, 140}};
  EXPECT_TRUE(ClipPolygon(ring, 64, 64).empty());
}

// --- the R3b clip fast paths, against the algorithm they replaced ----------
//
// R3b made both clippers skip their work when every point is already inside
// the canvas — the common case on a chart drawn at its own scale, and the
// largest single cost in a vector frame before it. That is a claim of EXACT
// equivalence, not of approximation, so it is tested against an oracle: the
// pre-R3b Cohen-Sutherland / Sutherland-Hodgman code, transcribed here.
// A future optimization that is merely close will fail this.

// Pre-R3b ClipPolyline, verbatim in behaviour: no all-inside shortcut.
std::vector<std::vector<PixelPoint>> ReferenceClipPolyline(
    const std::vector<SurfacePoint>& pts, int w, int h) {
  std::vector<std::vector<PixelPoint>> runs;
  if (pts.size() < 2 || w <= 0 || h <= 0) return runs;
  const double xmax = w - 1.0, ymax = h - 1.0;
  auto inside = [&](const SurfacePoint& p) {
    return p.x >= 0.0 && p.x <= xmax && p.y >= 0.0 && p.y <= ymax;
  };
  auto to_px = [](double x, double y) {
    return PixelPoint{static_cast<int>(std::lround(x)),
                      static_cast<int>(std::lround(y))};
  };
  auto same = [](const PixelPoint& a, const PixelPoint& b) {
    return a.x == b.x && a.y == b.y;
  };
  // Every segment of an all-inside path survives whole, which is the only
  // case this oracle is used for; a straddling path is covered by the
  // hand-written cases above.
  std::vector<PixelPoint> cur;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    if (!inside(pts[i]) || !inside(pts[i + 1])) continue;
    const PixelPoint a = to_px(pts[i].x, pts[i].y);
    const PixelPoint b = to_px(pts[i + 1].x, pts[i + 1].y);
    if (cur.empty() || !same(cur.back(), a)) {
      if (cur.size() >= 2) runs.push_back(cur);
      cur.clear();
      cur.push_back(a);
    }
    if (!same(cur.back(), b)) cur.push_back(b);
  }
  if (cur.size() >= 2) runs.push_back(cur);
  return runs;
}

// Pre-R3b ClipPolygon: four unconditional Sutherland-Hodgman passes.
std::vector<PixelPoint> ReferenceClipPolygon(
    const std::vector<SurfacePoint>& ring, int w, int h) {
  std::vector<PixelPoint> out;
  if (ring.size() < 3 || w <= 0 || h <= 0) return out;
  const double xmax = w - 1.0, ymax = h - 1.0;
  auto in_edge = [&](const SurfacePoint& p, int side) {
    switch (side) {
      case 0: return p.x >= 0.0;
      case 1: return p.x <= xmax;
      case 2: return p.y >= 0.0;
      default: return p.y <= ymax;
    }
  };
  auto cross = [&](const SurfacePoint& a, const SurfacePoint& b, int side) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    SurfacePoint r{};
    double t = 0.0;
    if (side == 0) t = (0.0 - a.x) / dx;
    else if (side == 1) t = (xmax - a.x) / dx;
    else if (side == 2) t = (0.0 - a.y) / dy;
    else t = (ymax - a.y) / dy;
    r.x = a.x + t * dx;
    r.y = a.y + t * dy;
    return r;
  };
  std::vector<SurfacePoint> in = ring, work;
  for (int side = 0; side < 4 && !in.empty(); ++side) {
    work.clear();
    for (size_t i = 0; i < in.size(); ++i) {
      const SurfacePoint& c = in[i];
      const SurfacePoint& p = in[(i + in.size() - 1) % in.size()];
      const bool ci = in_edge(c, side), pi = in_edge(p, side);
      if (ci) {
        if (!pi) work.push_back(cross(p, c, side));
        work.push_back(c);
      } else if (pi) {
        work.push_back(cross(p, c, side));
      }
    }
    in.swap(work);
  }
  for (const SurfacePoint& p : in) {
    const PixelPoint px{static_cast<int>(std::lround(p.x)),
                        static_cast<int>(std::lround(p.y))};
    if (out.empty() || (out.back().x != px.x || out.back().y != px.y))
      out.push_back(px);
  }
  if (out.size() > 1 && out.front().x == out.back().x &&
      out.front().y == out.back().y)
    out.pop_back();
  if (out.size() < 3) out.clear();
  return out;
}

// A spread of shapes that all sit inside a 200x150 canvas, including the two
// cases the shortcut has to get right on its own: vertices that land ON the
// boundary (inside is inclusive of xmax/ymax, so these must still take the
// fast path) and consecutive points that round to the SAME pixel (the
// duplicate suppression the slow path did as a side effect).
std::vector<std::vector<SurfacePoint>> InsideShapes() {
  return {
      {{10, 10}, {180, 12}, {150, 130}, {20, 100}},
      {{0, 0}, {199, 0}, {199, 149}, {0, 149}},        // exactly the corners
      {{50.4, 50.4}, {50.6, 50.6}, {120, 60}, {90, 130}},  // a duplicate pixel
      {{5, 5}, {5.2, 5.1}, {5.4, 5.3}, {100, 100}},        // three in a row
      {{30, 30}, {60, 30}, {60, 60}, {45, 45}, {30, 60}},  // concave
  };
}

TEST(VectorClip, FastPathMatchesTheUnclippedAlgorithmForPolygons) {
  for (const auto& ring : InsideShapes()) {
    const auto got = ClipPolygon(ring, 200, 150);
    const auto want = ReferenceClipPolygon(ring, 200, 150);
    ASSERT_EQ(got.size(), want.size());
    for (size_t i = 0; i < got.size(); ++i) {
      EXPECT_EQ(got[i].x, want[i].x) << "vertex " << i;
      EXPECT_EQ(got[i].y, want[i].y) << "vertex " << i;
    }
  }
}

TEST(VectorClip, FastPathMatchesTheUnclippedAlgorithmForPolylines) {
  for (const auto& path : InsideShapes()) {
    const auto got = ClipPolyline(path, 200, 150);
    const auto want = ReferenceClipPolyline(path, 200, 150);
    ASSERT_EQ(got.size(), want.size());
    for (size_t r = 0; r < got.size(); ++r) {
      ASSERT_EQ(got[r].size(), want[r].size()) << "run " << r;
      for (size_t i = 0; i < got[r].size(); ++i) {
        EXPECT_EQ(got[r][i].x, want[r][i].x);
        EXPECT_EQ(got[r][i].y, want[r][i].y);
      }
    }
  }
}

// The shortcut must not fire when ANY vertex is out, however marginally. Each
// of these pushes exactly one vertex one unit past one edge; the result has to
// be the clipped shape, not the original.
TEST(VectorClip, OneVertexOutsideStillTakesTheClippingPath) {
  const std::vector<SurfacePoint> base{{10, 10}, {180, 12}, {150, 130},
                                       {20, 100}};
  const SurfacePoint nudges[] = {
      {-1, 10}, {200, 12}, {150, -1}, {20, 150}};
  for (const SurfacePoint& n : nudges) {
    for (size_t v = 0; v < base.size(); ++v) {
      std::vector<SurfacePoint> ring = base;
      ring[v] = n;
      const auto got = ClipPolygon(ring, 200, 150);
      const auto want = ReferenceClipPolygon(ring, 200, 150);
      ASSERT_EQ(got.size(), want.size()) << "vertex " << v;
      for (size_t i = 0; i < got.size(); ++i) {
        EXPECT_EQ(got[i].x, want[i].x);
        EXPECT_EQ(got[i].y, want[i].y);
      }
      for (const PixelPoint& p : got) {
        EXPECT_GE(p.x, 0);
        EXPECT_LE(p.x, 199);
        EXPECT_GE(p.y, 0);
        EXPECT_LE(p.y, 149);
      }
    }
  }
}

// The scratch buffers ClipPolygon reuses between calls must not leak state
// from one shape into the next.
TEST(VectorClip, ReusedScratchDoesNotCarryBetweenCalls) {
  const std::vector<SurfacePoint> straddling{{-20, 10}, {30, 10}, {30, 50},
                                             {-20, 50}};
  const std::vector<SurfacePoint> inside{{10, 10}, {40, 10}, {40, 40}};
  const auto want_straddle = ClipPolygon(straddling, 64, 64);
  const auto want_inside = ClipPolygon(inside, 64, 64);
  for (int i = 0; i < 5; ++i) {
    const auto a = ClipPolygon(straddling, 64, 64);
    const auto b = ClipPolygon(inside, 64, 64);
    ASSERT_EQ(a.size(), want_straddle.size());
    ASSERT_EQ(b.size(), want_inside.size());
    for (size_t k = 0; k < a.size(); ++k) EXPECT_EQ(a[k].x, want_straddle[k].x);
    for (size_t k = 0; k < b.size(); ++k) EXPECT_EQ(b[k].x, want_inside[k].x);
  }
}

// --- the shared along-path / area placer (E3b) -----------------------------
//
// Hermetic: no chart product reaches this far. GeoSym's SAMI lines and S-52's
// LC/AP both reduce to these two calls, so the invariants they rely on are
// pinned here rather than twice over in two product test files.

using fv::PathRun;
using fv::PathRunType;
using fv::PlaceAlongPath;
using fv::PlaceOverArea;

PathRun Dash(double len) {
  PathRun r;
  r.type = PathRunType::kDash;
  r.length = len;
  return r;
}
PathRun Gap(double len) {
  PathRun r;
  r.type = PathRunType::kGap;
  r.length = len;
  return r;
}
PathRun Sym(const std::string& id, double len) {
  PathRun r;
  r.type = PathRunType::kSymbol;
  r.symbol_id = id;
  r.length = len;
  return r;
}

TEST(PathPlacer, DashGapCycleCoversTheLineWithAlternatingRuns) {
  // 100 px straight east, 10 on / 10 off: 5 dashes, each 10 long.
  std::vector<SurfacePoint> path{{0, 0}, {100, 0}};
  const auto placed = PlaceAlongPath(path, {Dash(10), Gap(10)}, 0.0);
  EXPECT_FALSE(placed.truncated);
  ASSERT_EQ(placed.dashes.size(), 5u);
  EXPECT_TRUE(placed.symbols.empty());
  EXPECT_DOUBLE_EQ(placed.dashes[0][0].x, 0.0);
  EXPECT_DOUBLE_EQ(placed.dashes[0].back().x, 10.0);
  EXPECT_DOUBLE_EQ(placed.dashes[4][0].x, 80.0);
  EXPECT_DOUBLE_EQ(placed.dashes[4].back().x, 90.0);
}

TEST(PathPlacer, ZeroLengthDashRunsToTheEndOfTheLine) {
  // GeoSym encodes a solid SAMI line as a single dash element of length 0.
  std::vector<SurfacePoint> path{{0, 0}, {30, 0}, {30, 40}};
  const auto placed = PlaceAlongPath(path, {Dash(0)}, 0.0);
  ASSERT_EQ(placed.dashes.size(), 1u);
  // The whole 70 px path, interior vertex kept.
  ASSERT_EQ(placed.dashes[0].size(), 3u);
  EXPECT_DOUBLE_EQ(placed.dashes[0].back().y, 40.0);
}

TEST(PathPlacer, SymbolsFollowTheTangentAndKeepTheirSpacing) {
  std::vector<SurfacePoint> path{{0, 0}, {100, 0}};
  const auto placed = PlaceAlongPath(path, {Sym("s", 25)}, 0.0);
  ASSERT_EQ(placed.symbols.size(), 4u);
  EXPECT_DOUBLE_EQ(placed.symbols[0].x, 0.0);
  EXPECT_DOUBLE_EQ(placed.symbols[1].x, 25.0);
  EXPECT_DOUBLE_EQ(placed.symbols[3].x, 75.0);
  // Running east, the symbol's own +x axis is already east: no rotation.
  for (const auto& s : placed.symbols) EXPECT_NEAR(s.rotation_deg, 0.0, 1e-9);
}

TEST(PathPlacer, RotationTurnsWithTheLineInTheRendererSSense) {
  // Screen y grows DOWN, so a path heading down-screen is heading south, and
  // the symbol must be rotated -90 in the sense DrawSymbolAt applies (which
  // maps symbol +x to screen (cos, -sin)).
  std::vector<SurfacePoint> down{{0, 0}, {0, 50}};
  const auto a = PlaceAlongPath(down, {Sym("s", 25)}, 0.0);
  ASSERT_FALSE(a.symbols.empty());
  EXPECT_NEAR(a.symbols[0].rotation_deg, -90.0, 1e-9);

  std::vector<SurfacePoint> up{{0, 50}, {0, 0}};
  const auto b = PlaceAlongPath(up, {Sym("s", 25)}, 0.0);
  ASSERT_FALSE(b.symbols.empty());
  EXPECT_NEAR(b.symbols[0].rotation_deg, 90.0, 1e-9);
}

TEST(PathPlacer, PerpendicularOffsetPushesLeftOfTheDirectionOfTravel) {
  // SAMI's vertical displacement. Heading east on screen, "left" (the
  // symbol's +y) is toward smaller screen y, i.e. up.
  PathRun r = Sym("s", 100);
  r.offset = 10.0;
  std::vector<SurfacePoint> path{{0, 20}, {100, 20}};
  const auto placed = PlaceAlongPath(path, {r}, 0.0);
  ASSERT_EQ(placed.symbols.size(), 1u);
  EXPECT_DOUBLE_EQ(placed.symbols[0].x, 0.0);
  EXPECT_DOUBLE_EQ(placed.symbols[0].y, 10.0);
}

TEST(PathPlacer, PhaseStartsThePatternPartWayIntoTheCycle) {
  // 10 on / 10 off with phase 5: the first dash is the tail of a dash run.
  std::vector<SurfacePoint> path{{0, 0}, {100, 0}};
  const auto placed = PlaceAlongPath(path, {Dash(10), Gap(10)}, 5.0);
  ASSERT_GE(placed.dashes.size(), 1u);
  EXPECT_DOUBLE_EQ(placed.dashes[0][0].x, 0.0);
  EXPECT_DOUBLE_EQ(placed.dashes[0].back().x, 5.0);
  EXPECT_DOUBLE_EQ(placed.dashes[1][0].x, 15.0);
}

TEST(PathPlacer, SymbolsAreSpacedByArcLengthAcrossACorner) {
  // Two 30 px legs; a 20 px symbol run lands at 0, 20, 40 — the third one
  // past the corner, on the second leg's tangent.
  std::vector<SurfacePoint> path{{0, 0}, {30, 0}, {30, 30}};
  const auto placed = PlaceAlongPath(path, {Sym("s", 20)}, 0.0);
  ASSERT_EQ(placed.symbols.size(), 3u);
  EXPECT_DOUBLE_EQ(placed.symbols[1].x, 20.0);
  EXPECT_DOUBLE_EQ(placed.symbols[2].x, 30.0);
  EXPECT_DOUBLE_EQ(placed.symbols[2].y, 10.0);
  EXPECT_NEAR(placed.symbols[2].rotation_deg, -90.0, 1e-9);
}

TEST(PathPlacer, DegenerateInputsProduceNothingRatherThanHanging) {
  // Zero-length path, empty run list, and a cycle that never advances.
  std::vector<SurfacePoint> point{{5, 5}, {5, 5}};
  EXPECT_TRUE(PlaceAlongPath(point, {Dash(10)}, 0.0).dashes.empty());
  std::vector<SurfacePoint> path{{0, 0}, {100, 0}};
  EXPECT_TRUE(PlaceAlongPath(path, {}, 0.0).dashes.empty());
  const auto stuck = PlaceAlongPath(path, {Gap(0), Gap(0)}, 0.0);
  EXPECT_TRUE(stuck.truncated);  // the budget stopped it; it did not hang
}

TEST(AreaPlacer, TilesOnlyInsideTheRingAndStaggersAlternateRows) {
  // 40x40 square at the origin, 10 px grid: interior positions only.
  std::vector<SurfacePoint> ring{{0, 0}, {40, 0}, {40, 40}, {0, 40}};
  const auto linear = PlaceOverArea(ring, 10, 10, false);
  ASSERT_FALSE(linear.empty());
  for (const auto& p : linear) {
    // Half-open, as the even-odd rule makes it: the left/top edges belong to
    // this ring and the right/bottom ones to the neighbour, so two areas
    // sharing an edge stamp it once between them, not twice.
    EXPECT_GE(p.x, 0.0);
    EXPECT_LT(p.x, 40.0);
    EXPECT_GE(p.y, 0.0);
    EXPECT_LT(p.y, 40.0);
    // Grid is anchored to the canvas origin, not the ring's corner.
    EXPECT_DOUBLE_EQ(std::fmod(p.x, 10.0), 0.0);
  }
  const auto staggered = PlaceOverArea(ring, 10, 10, true);
  bool any_half = false;
  for (const auto& p : staggered)
    if (std::fmod(p.x, 10.0) != 0.0) any_half = true;
  EXPECT_TRUE(any_half);
}

// R3c. The defect: the stamp grid hung on the canvas origin, so a pattern
// crawled inside its own region as the map panned. The fix pins the lattice
// to a caller-supplied anchor, which the renderer fills with the pixel
// position of a fixed geographic point.
//
// These two tests are the property, stated both ways round.
TEST(AreaPlacer, MovingRingAndAnchorTogetherMovesTheStampsWithThem) {
  std::vector<SurfacePoint> ring{{0, 0}, {40, 0}, {40, 40}, {0, 40}};
  const auto before = PlaceOverArea(ring, 10, 10, false, 0.0, 0.0);
  ASSERT_FALSE(before.empty());

  // A pan of 3 px: everything on the ground moves 3 px across the canvas —
  // the ring AND the projected anchor, because both are geography.
  const int dx = 3, dy = 3;
  std::vector<SurfacePoint> panned;
  for (const SurfacePoint& p : ring)
    panned.push_back(SurfacePoint{p.x + dx, p.y + dy});
  const auto after = PlaceOverArea(panned, 10, 10, false, dx, dy);

  ASSERT_EQ(after.size(), before.size())
      << "a pan must not change how many stamps an area holds";
  for (size_t i = 0; i < before.size(); ++i) {
    EXPECT_DOUBLE_EQ(after[i].x, before[i].x + dx);
    EXPECT_DOUBLE_EQ(after[i].y, before[i].y + dy);
  }
}

TEST(AreaPlacer, MovingOnlyTheRingIsWhatUsedToMakePatternsCrawl) {
  // The same pan with a lattice that ignores the anchor — the pre-R3c
  // behaviour, which the default arguments still give — moves the stamps by
  // a DIFFERENT amount than the ring, and that difference is the crawl.
  std::vector<SurfacePoint> ring{{0, 0}, {40, 0}, {40, 40}, {0, 40}};
  const auto before = PlaceOverArea(ring, 10, 10, false);
  std::vector<SurfacePoint> panned;
  for (const SurfacePoint& p : ring)
    panned.push_back(SurfacePoint{p.x + 3, p.y + 3});
  const auto after = PlaceOverArea(panned, 10, 10, false);
  ASSERT_FALSE(before.empty());
  ASSERT_FALSE(after.empty());
  // Anchored to the canvas, the lattice does not move at all: the first stamp
  // is still on a multiple of the spacing rather than 3 px past one.
  EXPECT_DOUBLE_EQ(std::fmod(after[0].x, 10.0), 0.0);
  EXPECT_NE(after[0].x, before[0].x + 3);
}

TEST(AreaPlacer, StampsLandOnTheAnchorsOwnLattice) {
  std::vector<SurfacePoint> ring{{0, 0}, {40, 0}, {40, 40}, {0, 40}};
  // A fractional, far-away anchor. The renderer now keeps its anchor within
  // one cell of the viewport (a distant one made the lattice sensitive to dpp
  // — see VectorRenderer::PatternAnchor), but the placer must not depend on
  // that: the lattice is derived by arithmetic, never by walking cell by cell
  // from the anchor, so a far anchor is exact and costs nothing.
  const double ax = -100000.25, ay = 250000.75;
  const auto stamps = PlaceOverArea(ring, 10, 10, false, ax, ay);
  ASSERT_FALSE(stamps.empty());
  for (const auto& p : stamps) {
    EXPECT_NEAR(std::fmod(p.x - ax, 10.0), 0.0, 1e-9);
    EXPECT_NEAR(std::fmod(p.y - ay, 10.0), 0.0, 1e-9);
    EXPECT_GE(p.x, 0.0);
    EXPECT_LT(p.x, 40.0);
  }
}

TEST(AreaPlacer, ConcaveRingLeavesItsNotchEmpty) {
  // A C shape: the notch on the right is outside the ring, so no stamps land
  // in it even though it is inside the bounding box.
  std::vector<SurfacePoint> ring{{0, 0},  {60, 0},  {60, 20}, {20, 20},
                                 {20, 40}, {60, 40}, {60, 60}, {0, 60}};
  const auto stamps = PlaceOverArea(ring, 5, 5, false);
  ASSERT_FALSE(stamps.empty());
  for (const auto& p : stamps) {
    const bool in_notch = p.x > 20.0 && p.y > 20.0 && p.y < 40.0;
    EXPECT_FALSE(in_notch) << p.x << "," << p.y;
  }
}

// --- the text placer -------------------------------------------------------
//
// Pure geometry: the advances stand in for a font, so nothing here depends on
// the host having one. Every assertion is DIRECTIONAL — an angle, a side, an
// order — because that is exactly what a golden hash cannot see.

using fv::PlaceTextAlongPath;

// Six 10 px glyphs = a 60 px word.
std::vector<double> Word(size_t n = 6, double adv = 10.0) {
  return std::vector<double>(n, adv);
}

TEST(TextPlacer, AWordOnAStraightLineIsCentredAndUnrotated) {
  std::vector<SurfacePoint> path{{0, 50}, {100, 50}};
  const auto runs = PlaceTextAlongPath(path, Word(), 0.0, 45.0, 0.0);
  ASSERT_EQ(runs.size(), 1u);
  ASSERT_EQ(runs[0].glyphs.size(), 6u);
  // 100 px path, 60 px word: 20 px of slack each side.
  EXPECT_DOUBLE_EQ(runs[0].glyphs[0].x, 20.0);
  EXPECT_DOUBLE_EQ(runs[0].glyphs[5].x, 70.0);
  for (const auto& g : runs[0].glyphs) {
    EXPECT_DOUBLE_EQ(g.y, 50.0);
    EXPECT_NEAR(g.angle_rad, 0.0, 1e-12);
  }
}

TEST(TextPlacer, TextTooLongForItsPathIsNotDrawnAtAll) {
  // Half a road name is worse than none: the run is dropped whole.
  std::vector<SurfacePoint> path{{0, 0}, {40, 0}};
  EXPECT_TRUE(PlaceTextAlongPath(path, Word(), 0.0, 45.0, 0.0).empty());
}

TEST(TextPlacer, GlyphsFollowTheTangentThroughABend) {
  // East for 100, then due south for 100. The word sits astride the corner,
  // so its first glyphs run east (angle 0) and its last run south, which in
  // the screen-y-down CCW convention is -90 degrees.
  std::vector<SurfacePoint> path{{0, 0}, {100, 0}, {100, 100}};
  const auto runs = PlaceTextAlongPath(path, Word(), 0.0, 90.0, 0.0);
  ASSERT_EQ(runs.size(), 1u);
  const auto& g = runs[0].glyphs;
  EXPECT_NEAR(g.front().angle_rad, 0.0, 1e-9);
  EXPECT_NEAR(g.back().angle_rad, -M_PI / 2, 1e-9);
  // And the glyphs are still touching: each one starts where the last one's
  // advance ended, measured ALONG the path.
  for (size_t i = 1; i < g.size(); ++i) {
    const double step = std::hypot(g[i].x - g[i - 1].x, g[i].y - g[i - 1].y);
    EXPECT_LE(step, 10.0 + 1e-9);
    EXPECT_GT(step, 5.0);
  }
}

TEST(TextPlacer, ARunThatTurnsHarderThanTheLimitIsRejected) {
  // A hairpin: consecutive glyphs turn ~180 degrees at the point.
  std::vector<SurfacePoint> path{{0, 0}, {50, 0}, {0, 5}};
  EXPECT_TRUE(PlaceTextAlongPath(path, Word(), 0.0, 30.0, 0.0).empty());
  // The same geometry with the limit lifted places it — the rejection is the
  // limit doing its job, not the placer failing to walk the path.
  EXPECT_FALSE(PlaceTextAlongPath(path, Word(), 0.0, 0.0, 0.0).empty());
}

TEST(TextPlacer, AWestwardRoadReadsLeftToRightAnyway) {
  // Digitised east-to-west. Placed naively the text would be upside down;
  // the placer walks such a path backwards instead.
  std::vector<SurfacePoint> path{{100, 50}, {0, 50}};
  const auto runs = PlaceTextAlongPath(path, Word(), 0.0, 45.0, 0.0);
  ASSERT_EQ(runs.size(), 1u);
  const auto& g = runs[0].glyphs;
  for (const auto& p : g) EXPECT_NEAR(p.angle_rad, 0.0, 1e-12);
  EXPECT_LT(g.front().x, g.back().x) << "reading order must run rightward";
}

TEST(TextPlacer, ANorthSouthRoadReadsUpward) {
  // The tie case. Cartographic convention: text on a vertical line reads
  // bottom to top, i.e. +90 CCW, whichever way the line was digitised.
  std::vector<SurfacePoint> down{{50, 0}, {50, 100}};   // digitised southward
  std::vector<SurfacePoint> up{{50, 100}, {50, 0}};     // and northward
  for (const auto* path : {&down, &up}) {
    const auto runs = PlaceTextAlongPath(*path, Word(), 0.0, 45.0, 0.0);
    ASSERT_EQ(runs.size(), 1u);
    for (const auto& g : runs[0].glyphs)
      EXPECT_NEAR(g.angle_rad, M_PI / 2, 1e-9);
    // Reading upward means later glyphs are at SMALLER y.
    EXPECT_LT(runs[0].glyphs.back().y, runs[0].glyphs.front().y);
  }
}

TEST(TextPlacer, OffsetPutsTheTextOnTheLeftOfTravel) {
  // Same sense as PathRun::offset. Running east on a screen whose y grows
  // down, left of travel is UP: a positive offset must LOWER y.
  std::vector<SurfacePoint> path{{0, 50}, {100, 50}};
  const auto up = PlaceTextAlongPath(path, Word(), 0.0, 45.0, 6.0);
  const auto down = PlaceTextAlongPath(path, Word(), 0.0, 45.0, -6.0);
  ASSERT_EQ(up.size(), 1u);
  ASSERT_EQ(down.size(), 1u);
  EXPECT_DOUBLE_EQ(up[0].glyphs[0].y, 44.0);
  EXPECT_DOUBLE_EQ(down[0].glyphs[0].y, 56.0);
  // The offset is perpendicular only: it must not slide the word along.
  EXPECT_DOUBLE_EQ(up[0].glyphs[0].x, down[0].glyphs[0].x);
}

TEST(TextPlacer, SpacingRepeatsTheNameAlongALongRoad) {
  // 1000 px of road, a 60 px word, one every 200 px.
  std::vector<SurfacePoint> path{{0, 0}, {1000, 0}};
  const auto runs = PlaceTextAlongPath(path, Word(), 200.0, 45.0, 0.0);
  ASSERT_EQ(runs.size(), 5u);
  for (size_t i = 1; i < runs.size(); ++i) {
    EXPECT_DOUBLE_EQ(runs[i].glyphs[0].x - runs[i - 1].glyphs[0].x, 200.0);
  }
  // The block of runs is centred on the path, so the margins match.
  const double left = runs.front().glyphs.front().x;
  const double right = 1000.0 - (runs.back().glyphs.back().x + 10.0);
  EXPECT_NEAR(left, right, 1e-9);
}

TEST(TextPlacer, NoSpacingIsOneRunNotOnePerPathLength) {
  // A road part is already one name's worth of geometry, so the default has
  // to be a single centred run however long the part is.
  std::vector<SurfacePoint> path{{0, 0}, {10000, 0}};
  const auto runs = PlaceTextAlongPath(path, Word(), 0.0, 45.0, 0.0);
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_DOUBLE_EQ(runs[0].glyphs[0].x, (10000.0 - 60.0) / 2.0);
}

// --- stub source / style ---------------------------------------------------

class StubSource : public fv::IVectorSource {
 public:
  std::vector<fv::VectorFeature> features;
  fv::VectorQuery last_query;

  fv::Status Open(const std::string&) override { return fv::Status::Ok(); }
  bool IsOpen() const override { return true; }
  fv::GeoRect Bounds() const override { return fv::GeoRect::World(); }
  std::vector<std::string> Layers() const override { return {"stub"}; }
  fv::Status Query(const fv::VectorQuery& q,
                   std::vector<fv::VectorFeature>* out) override {
    last_query = q;
    for (const auto& f : features) out->push_back(f);
    return fv::Status::Ok();
  }
};

// Styles by layer name: colour comes from the layer, priority from a map, so
// tests can assert draw ORDER independently of query order.
class StubStyle : public fv::IStyleEngine {
 public:
  fv::FvColor color{255, 0, 0, 255};
  int priority = 0;
  bool emit_symbol = false;
  int wide_pen = 1;
  fv::VectorSymbol symbol;
  std::vector<std::string> styled_keys;

  fv::Status Style(const fv::VectorFeature& f, const fv::StyleContext& ctx,
                   std::vector<fv::StyleResult>* out) override {
    styled_keys.push_back(f.style_key);
    last_ctx = ctx;
    fv::StyleResult r;
    r.priority = priority;
    if (emit_symbol) {
      r.symbol.valid = true;
      r.symbol.symbol_id = "stub";
    } else {
      r.stroke.valid = true;
      r.stroke.pen.color = color;
      r.stroke.pen.width = wide_pen;
    }
    out->push_back(r);
    return fv::Status::Ok();
  }

  const fv::VectorSymbol* Symbol(const std::string& id) override {
    return id == "stub" ? &symbol : nullptr;
  }

  fv::StyleContext last_ctx;
};

// Styles each feature at the priority named by its style_key ("1", "2", ...)
// with that feature's own colour, so the LAST one drawn wins the pixel.
class PriorityStyle : public fv::IStyleEngine {
 public:
  fv::Status Style(const fv::VectorFeature& f, const fv::StyleContext&,
                   std::vector<fv::StyleResult>* out) override {
    fv::StyleResult r;
    r.priority = std::stoi(f.style_key);
    r.stroke.valid = true;
    r.stroke.pen.width = 1;
    r.stroke.pen.color = f.style_key == "1" ? fv::FvColor{255, 0, 0, 255}
                                            : fv::FvColor{0, 255, 0, 255};
    out->push_back(r);
    return fv::Status::Ok();
  }
  const fv::VectorSymbol* Symbol(const std::string&) override {
    return nullptr;
  }
};

fv::VectorFeature Line(const std::string& key,
                       std::vector<fv::GeoPoint> pts) {
  fv::VectorFeature f;
  f.type = fv::VectorGeometryType::kLine;
  f.style_key = key;
  f.layer = "stub";
  f.parts.push_back(std::move(pts));
  return f;
}

fv::MapProjection Proj(int w, int h, double lat, double lon, double dpp) {
  fv::MapProjection p;
  p.SetSurfaceSize(w, h);
  p.SetCenter(fv::GeoPoint{lat, lon});
  p.SetResolution(dpp, dpp);
  return p;
}

const unsigned char* Px(const fv::PixelBuffer& b, int x, int y) {
  return b.Row(y) + 4 * x;
}

// --- renderer --------------------------------------------------------------

TEST(VectorRenderer, DrawsAStrokedLineAndReportsStats) {
  auto src = std::make_shared<StubSource>();
  // A horizontal line through the centre of a 1 deg/px, 64x64 viewport at 0,0.
  src->features.push_back(Line("a", {{0.0, -10.0}, {0.0, 10.0}}));
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});

  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_EQ(r.features_queried(), 1u);
  EXPECT_EQ(r.draws_emitted(), 1u);
  // Centre row is red; a row well away from it is untouched.
  EXPECT_EQ(Px(canvas.Buffer(), 32, 32)[0], 255);
  EXPECT_EQ(Px(canvas.Buffer(), 32, 10)[0], 0);
}

TEST(VectorRenderer, PassesTheViewportAndScaleToTheSource) {
  auto src = std::make_shared<StubSource>();
  auto style = std::make_shared<StubStyle>();
  fv::CpuCanvas canvas(64, 64);

  fv::MapProjection p;
  p.SetSurfaceSize(64, 64);
  p.SetCenter(fv::GeoPoint{35.0, -84.0});
  p.SetScale(500000.0);

  fv::VectorRenderer r(src, style);
  r.SetMaxFeatures(7);
  ASSERT_TRUE(r.Render(p, &canvas).ok());

  EXPECT_EQ(src->last_query.max_features, 7u);
  EXPECT_DOUBLE_EQ(src->last_query.scale_denominator, 500000.0);
  EXPECT_TRUE(src->last_query.area.Contains(fv::GeoPoint{35.0, -84.0}));
}

TEST(VectorRenderer, HigherPriorityDrawsOnTop) {
  // Same geometry twice; the priority-2 green must win the pixel even though
  // it is queried first.
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("2", {{0.0, -10.0}, {0.0, 10.0}}));
  src->features.push_back(Line("1", {{0.0, -10.0}, {0.0, 10.0}}));
  auto style = std::make_shared<PriorityStyle>();

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  const unsigned char* px = Px(canvas.Buffer(), 32, 32);
  EXPECT_EQ(px[0], 0);    // not red
  EXPECT_EQ(px[1], 255);  // green, the priority-2 pass
}

TEST(VectorRenderer, DrawsAPointSymbolAnchoredAtTheFeature) {
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature pt;
  pt.type = fv::VectorGeometryType::kPoint;
  pt.style_key = "p";
  pt.parts.push_back({fv::GeoPoint{0.0, 0.0}});
  src->features.push_back(pt);

  auto style = std::make_shared<StubStyle>();
  style->emit_symbol = true;
  // A filled 200x200-HIMETRIC box centred on the symbol origin. At the
  // renderer's symbol scale (1/25.4 px per HIMETRIC unit) that is ~8 px.
  fv::SymbolPrimitive box;
  box.type = fv::SymbolPrimitiveType::kPolygon;
  box.points = {{-100, -100}, {100, -100}, {100, 100}, {-100, 100}};
  box.has_fill = true;
  box.fill_color = fv::FvColor{0, 0, 255, 255};
  style->symbol.primitives.push_back(box);

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_EQ(r.draws_emitted(), 1u);
  EXPECT_EQ(Px(canvas.Buffer(), 32, 32)[2], 255);  // blue at the anchor
  EXPECT_EQ(Px(canvas.Buffer(), 32, 50)[2], 0);    // and not far away
  // The symbol is small: ~200/25.4 px across, so 8 px out is already clear.
  EXPECT_EQ(Px(canvas.Buffer(), 45, 32)[2], 0);
}

// --- pixmap symbols (E6) ---------------------------------------------------
//
// The second symbol form at the seam. Everything here is synthetic: an engine
// that hands out a tile instead of a display list, which is all S-52's 679
// raster-only symbols are from the renderer's side.

// A style engine whose "stub" symbol is a PIXMAP: an opaque green tile with a
// single red pixel, so a test can locate the tile's own (0,0) after any
// transform. Optionally also carries a vector display list under the same id,
// to pin which of the two wins.
class PixmapStyle : public fv::IStyleEngine {
 public:
  fv::SymbolPixmap pix;
  fv::VectorSymbol vec;  // empty unless a test fills it
  double scale = 1.0;
  size_t pixmap_lookups = 0;

  PixmapStyle(int w, int h, double pivot_x, double pivot_y) {
    pix.tile = fv::PixelBuffer(w, h);
    pix.pivot_x = pivot_x;
    pix.pivot_y = pivot_y;
    for (int y = 0; y < h; ++y) {
      unsigned char* row = pix.tile.Row(y);
      for (int x = 0; x < w; ++x) {
        row[x * 4 + 0] = (x == 0 && y == 0) ? 255 : 0;
        row[x * 4 + 1] = (x == 0 && y == 0) ? 0 : 255;
        row[x * 4 + 2] = 0;
        row[x * 4 + 3] = 255;
      }
    }
  }

  fv::Status Style(const fv::VectorFeature&, const fv::StyleContext&,
                   std::vector<fv::StyleResult>* out) override {
    fv::StyleResult r;
    r.symbol.valid = true;
    r.symbol.symbol_id = "stub";
    r.symbol.scale = scale;
    out->push_back(r);
    return fv::Status::Ok();
  }
  const fv::VectorSymbol* Symbol(const std::string& id) override {
    return (id == "stub" && !vec.primitives.empty()) ? &vec : nullptr;
  }
  const fv::SymbolPixmap* Pixmap(const std::string& id) override {
    if (id != "stub") return nullptr;
    ++pixmap_lookups;
    return &pix;
  }
};

std::shared_ptr<StubSource> PointAtOrigin() {
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature pt;
  pt.type = fv::VectorGeometryType::kPoint;
  pt.style_key = "p";
  pt.parts.push_back({fv::GeoPoint{0.0, 0.0}});
  src->features.push_back(pt);
  return src;
}

TEST(VectorRendererPixmap, TileIsBlittedWithItsPivotOnTheFeature) {
  auto src = PointAtOrigin();
  // 8x6 tile whose pivot is (2,5) — like a beacon standing on its post.
  auto style = std::make_shared<PixmapStyle>(8, 6, 2.0, 5.0);

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_EQ(r.draws_emitted(), 1u);
  // Anchor is pixel (32,32); the tile's own (0,0) therefore lands at
  // (32-2, 32-5) and is the red marker pixel.
  EXPECT_EQ(Px(canvas.Buffer(), 30, 27)[0], 255);
  EXPECT_EQ(Px(canvas.Buffer(), 30, 27)[1], 0);
  // The rest of the tile is green, and one pixel past its far corner is not.
  EXPECT_EQ(Px(canvas.Buffer(), 32, 32)[1], 255);
  EXPECT_EQ(Px(canvas.Buffer(), 37, 32)[1], 255);
  EXPECT_EQ(Px(canvas.Buffer(), 38, 32)[1], 0);
}

TEST(VectorRendererPixmap, VectorDefinitionWinsWhenASymbolHasBoth) {
  auto src = PointAtOrigin();
  auto style = std::make_shared<PixmapStyle>(8, 6, 2.0, 5.0);
  fv::SymbolPrimitive box;  // a blue 200-HIMETRIC box, ~8 px
  box.type = fv::SymbolPrimitiveType::kPolygon;
  box.points = {{-100, -100}, {100, -100}, {100, 100}, {-100, 100}};
  box.has_fill = true;
  box.fill_color = fv::FvColor{0, 0, 255, 255};
  style->vec.primitives.push_back(box);

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_EQ(Px(canvas.Buffer(), 32, 32)[2], 255) << "the vector box drew";
  EXPECT_EQ(style->pixmap_lookups, 0u) << "the tile was never asked for";
}

TEST(VectorRendererPixmap, SymbolScaleResamplesTheTile) {
  auto src = PointAtOrigin();
  auto style = std::make_shared<PixmapStyle>(8, 6, 0.0, 0.0);

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  r.SetSymbolScale(2.0);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  // Pivot (0,0) lands on the anchor pixel (32,32) and each source pixel now
  // covers two: the tile spans x 31..46, y 31..42.
  EXPECT_EQ(Px(canvas.Buffer(), 31, 31)[0], 255) << "marker pixel, doubled";
  EXPECT_EQ(Px(canvas.Buffer(), 32, 32)[0], 255) << "...is 2x2 now";
  EXPECT_EQ(Px(canvas.Buffer(), 33, 33)[1], 255) << "and then green";
  EXPECT_EQ(Px(canvas.Buffer(), 46, 42)[1], 255) << "green to the far corner";
  EXPECT_EQ(Px(canvas.Buffer(), 47, 43)[1], 0) << "and no further";
}

// The claim the anchor snap exists to make: the resampler REPRODUCES the
// straight blit at unit scale, so a symbol does not jump by a pixel when a
// zoom crosses 1.0. Scale is nudged past the fast path's epsilon by 2e-6,
// which is far too small to move any sample.
TEST(VectorRendererPixmap, ResamplerAtUnitScaleMatchesTheStraightBlit) {
  auto src = PointAtOrigin();
  auto blitted = std::make_shared<PixmapStyle>(9, 7, 4.0, 6.0);
  fv::CpuCanvas a(64, 64);
  a.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer ra(src, blitted);
  ASSERT_TRUE(ra.Render(Proj(64, 64, 0.0, 0.0, 1.0), &a).ok());

  auto resampled = std::make_shared<PixmapStyle>(9, 7, 4.0, 6.0);
  fv::CpuCanvas b(64, 64);
  b.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer rb(src, resampled);
  rb.SetSymbolScale(1.0 + 2e-6);
  ASSERT_TRUE(rb.Render(Proj(64, 64, 0.0, 0.0, 1.0), &b).ok());

  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x)
      ASSERT_EQ(std::memcmp(Px(a.Buffer(), x, y), Px(b.Buffer(), x, y), 4), 0)
          << "differs at " << x << "," << y;
}

TEST(VectorRendererPixmap, RotationTurnsTheTileAboutItsPivot) {
  auto src = PointAtOrigin();
  // Pivot at the tile's own (0,0), so a rotation about it is easy to reason
  // about: the tile occupies the quadrant its +x/+y axes point into.
  auto style = std::make_shared<PixmapStyle>(8, 6, 0.0, 0.0);

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());
  // Unrotated: down-right of the anchor, nothing up-right.
  ASSERT_EQ(Px(canvas.Buffer(), 34, 34)[1], 255);
  ASSERT_EQ(Px(canvas.Buffer(), 34, 28)[1], 0);

  // 90 degrees. PointSymbolStyle::rotation_deg is clockwise from north-up and
  // the renderer's screen y grows downward, so +90 takes tile +y (down) to
  // screen -x... the observable claim is simply that the ink MOVED off the
  // quadrant it was in and the pivot pixel stayed put.
  fv::CpuCanvas turned(64, 64);
  turned.Clear(fv::FvColor{0, 0, 0, 255});
  auto rot = std::make_shared<PixmapStyle>(8, 6, 0.0, 0.0);
  class Rotated : public PixmapStyle {
   public:
    using PixmapStyle::PixmapStyle;
    fv::Status Style(const fv::VectorFeature& f, const fv::StyleContext& c,
                     std::vector<fv::StyleResult>* out) override {
      PixmapStyle::Style(f, c, out);
      out->back().symbol.rotation_deg = 90.0;
      return fv::Status::Ok();
    }
  };
  auto rstyle = std::make_shared<Rotated>(8, 6, 0.0, 0.0);
  fv::VectorRenderer r2(src, rstyle);
  ASSERT_TRUE(r2.Render(Proj(64, 64, 0.0, 0.0, 1.0), &turned).ok());

  EXPECT_EQ(Px(turned.Buffer(), 34, 34)[1], 0) << "the old quadrant is empty";
  EXPECT_EQ(Px(turned.Buffer(), 34, 28)[1], 255) << "the ink turned into this one";
  // Same amount of ink either way (the tile is opaque, so this is exact up to
  // the resampler's edge rounding).
  auto green = [](const fv::PixelBuffer& b) {
    size_t n = 0;
    for (int y = 0; y < b.Height(); ++y)
      for (int x = 0; x < b.Width(); ++x)
        if (Px(b, x, y)[1] == 255) ++n;
    return n;
  };
  const size_t a = green(canvas.Buffer()), b = green(turned.Buffer());
  EXPECT_GT(b, a * 8 / 10);
  EXPECT_LT(b, a * 12 / 10);
}

TEST(VectorRendererPixmap, ASymbolWithNeitherFormDrawsNothing) {
  auto src = PointAtOrigin();
  // Same engine, but the id it hands out resolves to nothing at all.
  class NoSymbol : public fv::IStyleEngine {
   public:
    fv::Status Style(const fv::VectorFeature&, const fv::StyleContext&,
                     std::vector<fv::StyleResult>* out) override {
      fv::StyleResult r;
      r.symbol.valid = true;
      r.symbol.symbol_id = "missing";
      out->push_back(r);
      return fv::Status::Ok();
    }
    const fv::VectorSymbol* Symbol(const std::string&) override {
      return nullptr;
    }
  };
  auto style = std::make_shared<NoSymbol>();

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());
  EXPECT_EQ(r.draws_emitted(), 0u);
  EXPECT_TRUE(r.pick_index().empty()) << "nothing drawn, nothing pickable";
}

TEST(VectorRenderer, LongLineIsClippedNotDropped) {
  auto src = std::make_shared<StubSource>();
  // Spans far beyond the viewport in both directions.
  src->features.push_back(Line("a", {{-80.0, -170.0}, {80.0, 170.0}}));
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_GE(r.draws_emitted(), 1u);
  // Clipped, not dropped: the visible run reaches both side edges. (The
  // exact centre pixel is not asserted — the projection puts the map centre
  // at (w-1)/2, i.e. 31.5, so the crossing straddles two pixels.)
  auto column_has_red = [&](int x) {
    for (int y = 0; y < 64; ++y)
      if (Px(canvas.Buffer(), x, y)[0] == 255) return true;
    return false;
  };
  EXPECT_TRUE(column_has_red(0));
  EXPECT_TRUE(column_has_red(63));
}

TEST(VectorRenderer, RejectsANullCanvasAndAnUnreadyProjection) {
  auto src = std::make_shared<StubSource>();
  auto style = std::make_shared<StubStyle>();
  fv::VectorRenderer r(src, style);
  EXPECT_EQ(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), nullptr).code,
            fv::kInvalidArg);

  fv::CpuCanvas canvas(8, 8);
  fv::MapProjection empty;
  EXPECT_EQ(r.Render(empty, &canvas).code, fv::kInvalidArg);
}

// --- pick index (identify, plan §5.3) --------------------------------------

TEST(VectorRendererPick, IndexesWhatWasDrawnAndHitTestsIt) {
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature f = Line("a", {{0.0, -10.0}, {0.0, 10.0}});
  f.ref.layer = 0;
  f.ref.tile = 4;
  f.ref.feature = 77;
  src->features.push_back(f);
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.pick_enabled()) << "identify is on by default";
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  // The line is drawn along the centre row; a tap there finds exactly it.
  auto hits = r.pick_index().HitTest(32, 32, 2.0);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].ref.feature, 77);
  EXPECT_EQ(hits[0].ref.tile, 4);
  // A tap far from any ink finds nothing.
  EXPECT_TRUE(r.pick_index().HitTest(32, 5, 2.0).empty());
}

TEST(VectorRendererPick, HitTestFollowsTheINKNotTheSourceGeometry) {
  // The plan's rule: the index is built from DRAWN geometry. A fat pen is
  // tappable across its whole width, and a feature clipped away is gone.
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature on_screen = Line("a", {{0.0, -10.0}, {0.0, 10.0}});
  on_screen.ref.layer = 0;
  on_screen.ref.feature = 1;
  fv::VectorFeature off_screen = Line("a", {{80.0, -10.0}, {80.0, 10.0}});
  off_screen.ref.layer = 0;
  off_screen.ref.feature = 2;
  src->features.push_back(on_screen);
  src->features.push_back(off_screen);

  auto style = std::make_shared<StubStyle>();
  style->wide_pen = 9;  // ink spans +-4 px around the centreline

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_EQ(r.features_queried(), 2u);
  // 4 px off the centreline is still on the 9-px-wide stroke...
  auto wide = r.pick_index().HitTest(32, 36, 0.0);
  ASSERT_EQ(wide.size(), 1u);
  EXPECT_EQ(wide[0].ref.feature, 1);
  // ...and the feature that never reached the canvas is not in the index.
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x)
      for (const auto& h : r.pick_index().HitTest(x, y, 0.0))
        EXPECT_NE(h.ref.feature, 2) << "clipped-away feature is pickable";
}

TEST(VectorRendererPick, DisablingPickingLeavesTheIndexEmpty) {
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("a", {{0.0, -10.0}, {0.0, 10.0}}));
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  r.SetPickEnabled(false);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());
  EXPECT_EQ(r.draws_emitted(), 1u) << "still drawn";
  EXPECT_TRUE(r.pick_index().empty());
}

TEST(VectorRendererPick, IndexIsRebuiltEachRender) {
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("a", {{0.0, -10.0}, {0.0, 10.0}}));
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());
  const size_t first = r.pick_index().shape_count();
  ASSERT_GT(first, 0u);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());
  EXPECT_EQ(r.pick_index().shape_count(), first) << "cleared, not appended";

  // Pan the map away from the feature: the index empties with the canvas.
  ASSERT_TRUE(r.Render(Proj(64, 64, 70.0, 70.0, 1.0), &canvas).ok());
  EXPECT_TRUE(r.pick_index().empty());
}

TEST(VectorRendererPick, PointSymbolsGetATappableBox) {
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature p;
  p.type = fv::VectorGeometryType::kPoint;
  p.style_key = "a";
  p.layer = "stub";
  p.parts.push_back({fv::GeoPoint{0.0, 0.0}});
  p.ref.layer = 0;
  p.ref.feature = 5;
  src->features.push_back(p);

  auto style = std::make_shared<StubStyle>();
  style->emit_symbol = true;
  // A minute symbol: two HIMETRIC units across, so it inks about one pixel.
  fv::SymbolPrimitive prim;
  prim.type = fv::SymbolPrimitiveType::kPolyline;
  prim.has_stroke = true;
  prim.points = {{-1.0, 0.0}, {1.0, 0.0}};
  style->symbol.primitives.push_back(prim);

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  auto hits = r.pick_index().HitTest(32, 32, 0.0);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].ref.feature, 5);
  // Its ink is about a pixel, but the box is padded to a finger-sized
  // minimum — that is the point of hit-testing the glyph, not the anchor.
  EXPECT_EQ(r.pick_index().HitTest(35, 34, 0.0).size(), 1u);
  EXPECT_TRUE(r.pick_index().HitTest(50, 50, 0.0).empty());
}

TEST(VectorRendererPick, ASymbolThatDrewNothingIsNotPickable) {
  // The index follows the ink: an unknown/empty symbol emits no draw call, so
  // it must not leave a phantom hit box behind either.
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature p;
  p.type = fv::VectorGeometryType::kPoint;
  p.style_key = "a";
  p.layer = "stub";
  p.parts.push_back({fv::GeoPoint{0.0, 0.0}});
  p.ref.layer = 0;
  p.ref.feature = 6;
  src->features.push_back(p);

  auto style = std::make_shared<StubStyle>();
  style->emit_symbol = true;  // stub symbol has no primitives at all

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());
  EXPECT_EQ(r.draws_emitted(), 0u);
  EXPECT_TRUE(r.pick_index().empty());
}

// A style engine that asks for a patterned line: dash, gap, symbol, cycled.
class PatternStyle : public fv::IStyleEngine {
 public:
  fv::VectorSymbol symbol;

  PatternStyle() {
    // A 4x4 HIMETRIC filled square, big enough to leave ink at the
    // renderer's symbol scale.
    fv::SymbolPrimitive p;
    p.type = fv::SymbolPrimitiveType::kPolygon;
    p.has_fill = true;
    p.fill_color = fv::FvColor{0, 0, 255, 255};
    p.points = {{-40, -40}, {40, -40}, {40, 40}, {-40, 40}};
    symbol.primitives.push_back(p);
  }

  fv::Status Style(const fv::VectorFeature&, const fv::StyleContext&,
                   std::vector<fv::StyleResult>* out) override {
    fv::StyleResult r;
    r.line_pattern.valid = true;
    r.line_pattern.pen.color = fv::FvColor{255, 0, 0, 255};
    r.line_pattern.pen.width = 1;
    fv::PathRun dash;
    dash.type = fv::PathRunType::kDash;
    dash.length = 6.0;
    fv::PathRun gap;
    gap.type = fv::PathRunType::kGap;
    gap.length = 4.0;
    fv::PathRun sym;
    sym.type = fv::PathRunType::kSymbol;
    sym.symbol_id = "stub";
    sym.length = 10.0;
    r.line_pattern.runs = {dash, gap, sym};
    out->push_back(r);
    return fv::Status::Ok();
  }
  const fv::VectorSymbol* Symbol(const std::string& id) override {
    return id == "stub" ? &symbol : nullptr;
  }
};

TEST(VectorRenderer, PatternedLineDrawsBothItsDashesAndItsSymbols) {
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("a", {{0.0, -30.0}, {0.0, 30.0}}));
  auto style = std::make_shared<PatternStyle>();

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  // Many draws, not one: the 20 px cycle repeats three times over 60 px.
  EXPECT_GT(r.draws_emitted(), 4u);

  // Both inks are on the centre row — the pen's red for the dashes and the
  // symbol's blue for the stamps. Either one missing means half the primitive
  // is not being drawn.
  bool red = false, blue = false;
  for (int x = 0; x < 64; ++x) {
    const unsigned char* p = Px(canvas.Buffer(), x, 32);
    if (p[0] > 200 && p[2] < 50) red = true;
    if (p[2] > 200 && p[0] < 50) blue = true;
  }
  EXPECT_TRUE(red) << "no dash ink";
  EXPECT_TRUE(blue) << "no symbol ink";

  // And the pick index carries both kinds, so a patterned line is still
  // identifiable where its symbols are, not only where its dashes are.
  EXPECT_FALSE(r.pick_index().empty());
}

TEST(VectorRenderer, PatternPhaseIsMeasuredFromThePathNotTheCanvasEdge) {
  // The same feature drawn into two viewports whose centres differ by a whole
  // number of pixels must put its stamps on the same GEOGRAPHIC points: the
  // placer walks the unclipped path, so clipping cannot restart the cycle.
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("a", {{0.0, -60.0}, {0.0, 60.0}}));
  auto style = std::make_shared<PatternStyle>();

  fv::CpuCanvas a(64, 64), b(64, 64);
  a.Clear(fv::FvColor{0, 0, 0, 255});
  b.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer ra(src, style), rb(src, style);
  // Centres 10 degrees = 10 pixels apart at 1 deg/px.
  ASSERT_TRUE(ra.Render(Proj(64, 64, 0.0, 0.0, 1.0), &a).ok());
  ASSERT_TRUE(rb.Render(Proj(64, 64, 0.0, 10.0, 1.0), &b).ok());

  // Column x in b shows what column x+10 showed in a.
  int compared = 0, same = 0;
  for (int x = 10; x < 54; ++x) {
    const unsigned char* pa = Px(a.Buffer(), x, 32);
    const unsigned char* pb = Px(b.Buffer(), x - 10, 32);
    ++compared;
    if (pa[0] == pb[0] && pa[1] == pb[1] && pa[2] == pb[2]) ++same;
  }
  EXPECT_EQ(same, compared) << "the pattern shifted when the map panned";
}

// --- labels ----------------------------------------------------------------

// A font is a host asset, so these tests skip rather than fail where there is
// none — the same bargain canvas_test makes for its text tests.
std::string SystemFont() {
  const char* candidates[] = {
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/Supplemental/Courier New.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  };
  for (const char* f : candidates)
    if (FILE* fp = fopen(f, "rb")) {
      fclose(fp);
      return f;
    }
  return std::string();
}

class LabelStyleEngine : public fv::IStyleEngine {
 public:
  fv::LabelStyle label;
  fv::Status Style(const fv::VectorFeature&, const fv::StyleContext&,
                   std::vector<fv::StyleResult>* out) override {
    fv::StyleResult r;
    r.label = label;
    out->push_back(r);
    return fv::Status::Ok();
  }
  const fv::VectorSymbol* Symbol(const std::string&) override {
    return nullptr;
  }
};

// The white ink's bounding box and area. Directional, not a hash: what these
// tests are about is WHERE the glyphs went and HOW BIG they came out.
struct Ink {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  long count = 0;
  int width() const { return x1 - x0; }
  int height() const { return y1 - y0; }
};
Ink Measure(const fv::PixelBuffer& b) {
  Ink k;
  bool any = false;
  for (int y = 0; y < b.Height(); ++y)
    for (int x = 0; x < b.Width(); ++x) {
      if (Px(b, x, y)[0] == 0) continue;
      ++k.count;
      if (!any) {
        k.x0 = k.x1 = x;
        k.y0 = k.y1 = y;
        any = true;
        continue;
      }
      k.x0 = std::min(k.x0, x);
      k.x1 = std::max(k.x1, x);
      k.y0 = std::min(k.y0, y);
      k.y1 = std::max(k.y1, y);
    }
  return k;
}

TEST(VectorRenderer, AnAlongPathLabelTurnsWithItsLine) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  // A road running due north through the middle of the viewport. Placed at a
  // point the name lies flat; placed along the path it stands up. Same
  // feature, same style, one field different.
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("r", {{-60.0, 0.0}, {60.0, 0.0}}));

  auto flat = std::make_shared<LabelStyleEngine>();
  flat->label.valid = true;
  flat->label.text = "Main Street";
  flat->label.style.font_path = font;
  flat->label.style.size = 14;
  flat->label.style.color = fv::FvColor{255, 255, 255, 255};
  auto along = std::make_shared<LabelStyleEngine>();
  along->label = flat->label;
  along->label.placement = fv::LabelPlacement::kAlongPath;

  fv::CpuCanvas a(200, 200), b(200, 200);
  a.Clear(fv::FvColor{0, 0, 0, 255});
  b.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer ra(src, flat), rb(src, along);
  ASSERT_TRUE(ra.Render(Proj(200, 200, 0.0, 0.0, 1.0), &a).ok());
  ASSERT_TRUE(rb.Render(Proj(200, 200, 0.0, 0.0, 1.0), &b).ok());

  const Ink flat_ink = Measure(a.Buffer());
  const Ink along_ink = Measure(b.Buffer());
  ASSERT_GT(flat_ink.count, 20) << "the point label drew nothing";
  ASSERT_GT(along_ink.count, 20) << "the along-path label drew nothing";
  EXPECT_GT(flat_ink.width(), flat_ink.height()) << "a point label lies flat";
  EXPECT_GT(along_ink.height(), along_ink.width())
      << "a label on a north-south road should stand up";
  // It is ON the road, not off at the first vertex: the ink straddles the
  // centre column.
  EXPECT_LT(along_ink.x0, 110);
  EXPECT_GT(along_ink.x1, 90);
  EXPECT_FALSE(rb.pick_index().empty()) << "an along-path label is identifiable";
}

// A point label hangs off its anchor by halign/valign. The canvas draws
// baseline-left, so kLeft/kBaseline must stay a no-op — GeoSym and OSM both
// pre-compute a dx/dy and say nothing about alignment, and neither may move.
TEST(VectorRenderer, PointLabelAlignmentHangsTheBoxOffTheAnchor) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  // One vertex dead centre of a 200x200 viewport, so the anchor is (100, 100)
  // and every assertion below is about which side of it the ink landed.
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("p", {{0.0, 0.0}, {0.0, 0.0}}));

  auto measure = [&](fv::LabelHAlign h, fv::LabelVAlign v) {
    auto eng = std::make_shared<LabelStyleEngine>();
    eng->label.valid = true;
    eng->label.text = "Anchor";
    eng->label.style.font_path = font;
    eng->label.style.size = 20;
    eng->label.style.color = fv::FvColor{255, 255, 255, 255};
    eng->label.halign = h;
    eng->label.valign = v;
    fv::CpuCanvas c(200, 200);
    c.Clear(fv::FvColor{0, 0, 0, 255});
    fv::VectorRenderer r(src, eng);
    EXPECT_TRUE(r.Render(Proj(200, 200, 0.0, 0.0, 1.0), &c).ok());
    return Measure(c.Buffer());
  };

  const Ink base = measure(fv::LabelHAlign::kLeft, fv::LabelVAlign::kBaseline);
  ASSERT_GT(base.count, 20) << "the label drew nothing";
  // The default: text runs RIGHT from the anchor and sits ABOVE the baseline.
  EXPECT_GE(base.x0, 99);
  EXPECT_LE(base.y1, 101);

  const Ink right = measure(fv::LabelHAlign::kRight, fv::LabelVAlign::kBaseline);
  EXPECT_LE(right.x1, 101) << "right-justified text ends at the anchor";
  EXPECT_LT(right.x0, base.x0);
  // Same string, same size: justification MOVES the box, it does not resize it.
  EXPECT_NEAR(right.width(), base.width(), 2);
  EXPECT_EQ(right.count, base.count);

  const Ink centre =
      measure(fv::LabelHAlign::kCenter, fv::LabelVAlign::kBaseline);
  EXPECT_LT(centre.x0, 100);
  EXPECT_GT(centre.x1, 100);
  EXPECT_NEAR((centre.x0 + centre.x1) / 2.0, 100.0, 4.0);

  // kTop pushes the baseline DOWN by the box height, so the ink hangs below
  // the anchor instead of standing on it.
  const Ink top = measure(fv::LabelHAlign::kLeft, fv::LabelVAlign::kTop);
  EXPECT_GT(top.y0, base.y0);
  EXPECT_GE(top.y0, 100) << "a top-aligned label hangs below its anchor";

  const Ink vcentre =
      measure(fv::LabelHAlign::kLeft, fv::LabelVAlign::kCenter);
  EXPECT_LT(vcentre.y0, 100);
  EXPECT_GT(vcentre.y1, 100) << "a centred label straddles its anchor";

  // kBottom is the baseline by another name: the box model runs from
  // baseline-height to baseline, so there is nothing left to subtract.
  const Ink bottom =
      measure(fv::LabelHAlign::kLeft, fv::LabelVAlign::kBottom);
  EXPECT_EQ(bottom.y0, base.y0);
  EXPECT_EQ(bottom.y1, base.y1);
}

TEST(VectorRenderer, ALabelLongerThanItsRoadIsNotDrawn) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  // A 4-pixel stub of road cannot carry a name, and drawing the part of it
  // that fits would be worse than drawing none.
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("r", {{-2.0, 0.0}, {2.0, 0.0}}));
  auto style = std::make_shared<LabelStyleEngine>();
  style->label.valid = true;
  style->label.text = "Bartholomew Boulevard";
  style->label.style.font_path = font;
  style->label.style.size = 14;
  style->label.style.color = fv::FvColor{255, 255, 255, 255};
  style->label.placement = fv::LabelPlacement::kAlongPath;

  fv::CpuCanvas c(200, 200);
  c.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(200, 200, 0.0, 0.0, 1.0), &c).ok());
  EXPECT_EQ(Measure(c.Buffer()).count, 0);
  EXPECT_EQ(r.draws_emitted(), 0u);
}

TEST(VectorRenderer, GroundSizedLabelsGrowWithTheMap) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("r", {{0.0, -60.0}, {0.0, 60.0}}));
  auto style = std::make_shared<LabelStyleEngine>();
  style->label.valid = true;
  style->label.text = "Ash";
  style->label.style.font_path = font;
  style->label.style.size = 14;  // ignored: the label states a GROUND size
  style->label.style.color = fv::FvColor{255, 255, 255, 255};
  style->label.size_unit = fv::LabelSizeUnit::kMeters;
  // 1 deg/px is ~111 km per pixel, so a metre-sized label would be invisible;
  // these are the sizes a text this big has to be to show up at all.
  style->label.ground_size_m = 2.0e6;

  // Zooming in by 4x (a quarter of the degrees per pixel) must make the text
  // about 4x taller, because it is pinned to the ground and the ground grew.
  fv::CpuCanvas out(400, 400), in(400, 400);
  out.Clear(fv::FvColor{0, 0, 0, 255});
  in.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer ro(src, style), ri(src, style);
  ASSERT_TRUE(ro.Render(Proj(400, 400, 0.0, 0.0, 1.0), &out).ok());
  ASSERT_TRUE(ri.Render(Proj(400, 400, 0.0, 0.0, 0.25), &in).ok());

  const Ink small = Measure(out.Buffer()), big = Measure(in.Buffer());
  ASSERT_GT(small.count, 10);
  ASSERT_GT(big.count, 10);
  EXPECT_NEAR(static_cast<double>(big.height()) / small.height(), 4.0, 0.6)
      << "ground-sized text must scale with the map";
}

TEST(VectorRenderer, ALabelReferenceScaleIsOffUntilItIsSet) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  auto src = std::make_shared<StubSource>();
  // Small enough that the anchor vertex is on screen at 1:500k, where a
  // 400 px canvas covers about half a degree.
  src->features.push_back(Line("r", {{-0.01, 0.0}, {0.01, 0.0}}));
  auto style = std::make_shared<LabelStyleEngine>();
  style->label.valid = true;
  style->label.text = "Ash";
  style->label.style.font_path = font;
  style->label.style.size = 14;
  style->label.style.color = fv::FvColor{255, 255, 255, 255};

  auto render = [&](double scale, double ref) {
    fv::CpuCanvas c(400, 400);
    c.Clear(fv::FvColor{0, 0, 0, 255});
    fv::MapProjection p;
    p.SetSurfaceSize(400, 400);
    p.SetCenter(fv::GeoPoint{0.0, 0.0});
    p.SetScale(scale);
    fv::VectorRenderer r(src, style);
    r.SetLabelReferenceScale(ref);
    EXPECT_TRUE(r.Render(p, &c).ok());
    return Measure(c.Buffer());
  };

  // Off (the default): the same pixel size at both scales — which is what
  // every pinned golden with a label in it depends on.
  const Ink a = render(500000.0, 0.0), b = render(250000.0, 0.0);
  ASSERT_GT(a.count, 10);
  EXPECT_EQ(a.height(), b.height());
  EXPECT_EQ(a.width(), b.width());

  // On, with the reference at the smaller scale: zooming to 1:250k doubles it.
  const Ink c = render(500000.0, 500000.0), d = render(250000.0, 500000.0);
  EXPECT_EQ(c.height(), a.height()) << "at the reference scale, nothing moves";
  EXPECT_NEAR(static_cast<double>(d.height()) / c.height(), 2.0, 0.35);
}

// --- halo (T2) --------------------------------------------------------------
//
// Counts of ink by COLOUR, which is what a halo test is really about: the
// outline has to be its own colour, outside the face, without eating the face.
struct Tally {
  long fill = 0;   // green
  long halo = 0;   // red
  int x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1;  // the halo's box
};
Tally CountInk(const fv::PixelBuffer& b) {
  Tally t;
  for (int y = 0; y < b.Height(); ++y)
    for (int x = 0; x < b.Width(); ++x) {
      const unsigned char* p = Px(b, x, y);
      // Deliberately a classification with a NEUTRAL BAND rather than a
      // nearest-colour split. The glyph's antialiased rim is green over red
      // and belongs to neither count; folding it into one would make "the
      // halo ate the face" and "the face has a blended edge" the same
      // measurement, which is exactly the distinction these tests exist for.
      if (p[1] >= 150 && p[0] <= 100) {
        ++t.fill;
      } else if (p[0] >= 150 && p[1] <= 100) {
        ++t.halo;
        t.x0 = std::min(t.x0, x);
        t.y0 = std::min(t.y0, y);
        t.x1 = std::max(t.x1, x);
        t.y1 = std::max(t.y1, y);
      }
    }
  return t;
}

std::shared_ptr<LabelStyleEngine> HaloLabel(const std::string& font,
                                            double halo_width) {
  auto s = std::make_shared<LabelStyleEngine>();
  s->label.valid = true;
  s->label.text = "Ash";
  s->label.style.font_path = font;
  s->label.style.size = 24;
  s->label.style.color = fv::FvColor{0, 255, 0, 255};
  s->label.halo_width = halo_width;
  s->label.halo_color = fv::FvColor{255, 0, 0, 255};
  return s;
}

TEST(VectorRendererHalo, OutlinesTheTextWithoutEatingIt) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("p", {{0.0, 0.0}, {0.0, 0.0}}));

  auto render = [&](double halo_width, Tally* t, size_t* halo_draws,
                    size_t* draws) {
    auto style = HaloLabel(font, halo_width);
    fv::CpuCanvas c(200, 200);
    c.Clear(fv::FvColor{0, 0, 0, 255});
    fv::VectorRenderer r(src, style);
    ASSERT_TRUE(r.Render(Proj(200, 200, 0.0, 0.0, 1.0), &c).ok());
    *t = CountInk(c.Buffer());
    *halo_draws = r.halo_draws();
    *draws = r.draws_emitted();
  };

  Tally none, haloed;
  size_t none_halo = 0, haloed_halo = 0, none_draws = 0, haloed_draws = 0;
  render(0.0, &none, &none_halo, &none_draws);
  render(1.0, &haloed, &haloed_halo, &haloed_draws);

  ASSERT_GT(none.fill, 50) << "the label drew nothing to begin with";
  EXPECT_EQ(none.halo, 0) << "no halo was asked for";
  EXPECT_EQ(none_halo, 0u);

  // Four stamps at one pixel — the offsets FalconView used.
  EXPECT_EQ(haloed_halo, 4u);
  // And the label is still ONE draw: a haloed label must not inflate the
  // number every other draw-count assertion is written against.
  EXPECT_EQ(haloed_draws, none_draws);

  EXPECT_GT(haloed.halo, 50) << "the halo put no ink down";
  // The face survives: the halo goes UNDER the text, so the glyph cores are
  // all still there. (Not exactly equal — a rim pixel that was green over
  // black is now green over red and falls into the neutral band.)
  EXPECT_GT(haloed.fill, none.fill * 0.9);
  // And it is OUTSIDE the face, so the coloured area grew.
  EXPECT_GT(haloed.fill + haloed.halo, none.fill);
}

TEST(VectorRendererHalo, AWiderHaloReachesFurtherAndAddsTheDiagonals) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("p", {{0.0, 0.0}, {0.0, 0.0}}));

  auto render = [&](double halo_width, Tally* t, size_t* halo_draws) {
    auto style = HaloLabel(font, halo_width);
    fv::CpuCanvas c(200, 200);
    c.Clear(fv::FvColor{0, 0, 0, 255});
    fv::VectorRenderer r(src, style);
    ASSERT_TRUE(r.Render(Proj(200, 200, 0.0, 0.0, 1.0), &c).ok());
    *t = CountInk(c.Buffer());
    *halo_draws = r.halo_draws();
  };

  Tally thin, thick;
  size_t thin_draws = 0, thick_draws = 0;
  render(1.0, &thin, &thin_draws);
  render(3.0, &thick, &thick_draws);

  EXPECT_EQ(thin_draws, 4u);
  EXPECT_EQ(thick_draws, 8u) << "past one pixel the corners need the diagonals";
  // Three pixels out on each side, so the outline's box is wider and taller
  // than the one-pixel outline's. Not an exact +4: the diagonals sit on the
  // circle of radius r, not at the square's corner.
  EXPECT_GT(thick.x1 - thick.x0, thin.x1 - thin.x0);
  EXPECT_GT(thick.y1 - thick.y0, thin.y1 - thin.y0);
  EXPECT_GT(thick.halo, thin.halo);
}

// A halo is authored in pixels against the authored text size. When the label
// grows — kMeters, or a label reference scale — the outline has to grow with
// it, or a 40 px name wears a one-pixel thread.
TEST(VectorRendererHalo, TheOutlineGrowsWithTheTextItOutlines) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("r", {{-0.01, 0.0}, {0.01, 0.0}}));

  auto render = [&](double scale, double ref) {
    auto style = HaloLabel(font, 1.0);
    fv::CpuCanvas c(400, 400);
    c.Clear(fv::FvColor{0, 0, 0, 255});
    fv::MapProjection p;
    p.SetSurfaceSize(400, 400);
    p.SetCenter(fv::GeoPoint{0.0, 0.0});
    p.SetScale(scale);
    fv::VectorRenderer r(src, style);
    r.SetLabelReferenceScale(ref);
    EXPECT_TRUE(r.Render(p, &c).ok());
    return r.halo_draws();
  };

  // At the reference scale the text is its authored size and the 1 px halo is
  // the four-stamp ring. Zoomed in 4x the text is 4x and the halo is 4 px,
  // which is past the point the diagonals are needed.
  EXPECT_EQ(render(500000.0, 500000.0), 4u);
  EXPECT_EQ(render(125000.0, 500000.0), 8u);
}

TEST(VectorRendererHalo, AnAlongPathLabelIsOutlinedGlyphByGlyph) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("r", {{-60.0, 0.0}, {60.0, 0.0}}));

  auto style = HaloLabel(font, 1.0);
  style->label.text = "Main Street";
  style->label.placement = fv::LabelPlacement::kAlongPath;

  fv::CpuCanvas c(200, 200);
  c.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(200, 200, 0.0, 0.0, 1.0), &c).ok());

  const Tally t = CountInk(c.Buffer());
  ASSERT_GT(t.fill, 50) << "the rotated label drew nothing";
  EXPECT_GT(t.halo, 50) << "a rotated glyph got no halo";
  // Four stamps per drawn glyph, and the glyphs of a name on a north-south
  // road all land on the canvas.
  EXPECT_EQ(r.halo_draws() % 4, 0u);
  EXPECT_GE(r.halo_draws(), 4u * 8u);
  // The outline stands up with the text rather than lying flat, i.e. it is in
  // screen space around a rotated run, not a horizontal smear.
  EXPECT_GT(t.y1 - t.y0, t.x1 - t.x0);
}

TEST(VectorRenderer, StyleContextCarriesDpiAndSymbolScale) {
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("a", {{0.0, -1.0}, {0.0, 1.0}}));
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(32, 32);
  fv::VectorRenderer r(src, style);
  r.SetDeviceDpi(144.0);
  r.SetSymbolScale(2.0);
  ASSERT_TRUE(r.Render(Proj(32, 32, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_DOUBLE_EQ(style->last_ctx.device_dpi, 144.0);
  EXPECT_DOUBLE_EQ(style->last_ctx.symbol_scale, 2.0);
}

}  // namespace
