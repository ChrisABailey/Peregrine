// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Path thinning and smoothing (fvkit/canvas/path_shaping.h, plan C5). The
// assertions here are the PROPERTIES each algorithm was chosen for -- Chaikin
// stays inside the hull, Catmull-Rom passes through the input -- because those
// are what make one right for a contour map and the other right for a track,
// and a vertex count would not tell them apart.

#include "fvkit/canvas/path_shaping.h"

#include <cmath>

#include "gtest/gtest.h"

namespace {

using fv::SurfacePoint;

// A staircase: exactly what a contour traced off a coarse post lattice looks
// like, and the shape the smoothing exists for.
std::vector<SurfacePoint> Staircase(int steps, double size) {
  std::vector<SurfacePoint> pts{{0.0, 0.0}};
  for (int i = 0; i < steps; ++i) {
    pts.push_back(SurfacePoint{pts.back().x + size, pts.back().y});
    pts.push_back(SurfacePoint{pts.back().x, pts.back().y + size});
  }
  return pts;
}

bool Contains(const std::vector<SurfacePoint>& pts, const SurfacePoint& p) {
  for (const SurfacePoint& q : pts)
    if (std::fabs(q.x - p.x) < 1e-9 && std::fabs(q.y - p.y) < 1e-9) return true;
  return false;
}

// ---------------------------------------------------------------------------
// Thinning
// ---------------------------------------------------------------------------

TEST(PathShaping, DecimateDropsWhatDoesNotMoveTheLine) {
  // Two straight legs meeting at a right angle, densely sampled, with a
  // quarter-pixel wobble on every vertex -- which is what a traced contour
  // looks like when the map is zoomed out past its post spacing.
  std::vector<SurfacePoint> pts;
  for (int i = 0; i <= 20; ++i)
    pts.push_back(SurfacePoint{static_cast<double>(i), (i % 2) * 0.25});
  for (int i = 1; i <= 20; ++i)
    pts.push_back(SurfacePoint{20.0 + (i % 2) * 0.25, static_cast<double>(i)});

  // Half a pixel keeps the corner and throws the wobble away.
  const std::vector<SurfacePoint> half = fv::DecimatePath(pts, 0.5);
  ASSERT_EQ(half.size(), 3u);
  EXPECT_DOUBLE_EQ(half[1].x, 20.0);
  EXPECT_DOUBLE_EQ(half[1].y, 0.0);

  // Endpoints always survive, whatever the tolerance.
  const std::vector<SurfacePoint> coarse = fv::DecimatePath(pts, 100.0);
  ASSERT_EQ(coarse.size(), 2u);
  EXPECT_DOUBLE_EQ(coarse.front().x, 0.0);
  EXPECT_DOUBLE_EQ(coarse.back().y, 20.0);

  // Zero is "keep everything", which is what the property's 0 means.
  EXPECT_EQ(fv::DecimatePath(pts, 0.0).size(), pts.size());
}

TEST(PathShaping, DecimateLeavesShortPathsAlone) {
  std::vector<SurfacePoint> two{{0, 0}, {10, 10}};
  EXPECT_EQ(fv::DecimatePath(two, 100.0).size(), 2u);
  EXPECT_TRUE(fv::DecimatePath({}, 1.0).empty());
}

// ---------------------------------------------------------------------------
// Chaikin: approximating, hull-bounded, and that is why it is the default
// ---------------------------------------------------------------------------

TEST(PathShaping, ChaikinRoundsCornersAndStaysInsideTheHull) {
  const std::vector<SurfacePoint> stair = Staircase(6, 20.0);
  const std::vector<SurfacePoint> smooth =
      fv::SmoothPathChaikin(stair, false, 3, 4.0);

  EXPECT_GT(smooth.size(), stair.size());
  // Endpoints are kept: an open line still starts and ends where it did.
  EXPECT_DOUBLE_EQ(smooth.front().x, stair.front().x);
  EXPECT_DOUBLE_EQ(smooth.back().y, stair.back().y);

  // THE PROPERTY: every smoothed point is inside the input's bounding box, so
  // a smoothed contour cannot bulge across the one above it.
  double minx = stair[0].x, maxx = stair[0].x, miny = stair[0].y, maxy = stair[0].y;
  for (const SurfacePoint& p : stair) {
    minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
    miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
  }
  for (const SurfacePoint& p : smooth) {
    EXPECT_GE(p.x, minx - 1e-9);
    EXPECT_LE(p.x, maxx + 1e-9);
    EXPECT_GE(p.y, miny - 1e-9);
    EXPECT_LE(p.y, maxy + 1e-9);
  }

  // The right angles are gone: no interior turn as sharp as the original's.
  double sharpest = 0.0;
  for (size_t i = 1; i + 1 < smooth.size(); ++i) {
    const double a = std::atan2(smooth[i].y - smooth[i - 1].y,
                                smooth[i].x - smooth[i - 1].x);
    const double b = std::atan2(smooth[i + 1].y - smooth[i].y,
                                smooth[i + 1].x - smooth[i].x);
    double d = std::fabs(a - b) * 180.0 / M_PI;
    if (d > 180.0) d = 360.0 - d;
    sharpest = std::max(sharpest, d);
  }
  EXPECT_LT(sharpest, 89.0);
}

TEST(PathShaping, ChaikinClosesARingWithoutACornerAtTheSeam) {
  // A square, given as a closed ring (last point == first).
  std::vector<SurfacePoint> ring{{0, 0}, {40, 0}, {40, 40}, {0, 40}, {0, 0}};
  const std::vector<SurfacePoint> smooth =
      fv::SmoothPathChaikin(ring, true, 2, 4.0);
  ASSERT_GE(smooth.size(), 8u);
  EXPECT_DOUBLE_EQ(smooth.front().x, smooth.back().x);
  EXPECT_DOUBLE_EQ(smooth.front().y, smooth.back().y);
  // The seam vertex is no longer a corner of the square: a closed ring is
  // smoothed all the way round, so (0,0) itself is cut off like every other
  // corner.
  EXPECT_FALSE(Contains(smooth, SurfacePoint{0.0, 0.0}));
}

TEST(PathShaping, ChaikinStopsWhenTheSegmentsAreAlreadyShort) {
  // Segments of 1 px: below the 4 px target, so not one iteration runs and the
  // path comes back untouched. This is what keeps a zoomed-out draw free.
  std::vector<SurfacePoint> fine;
  for (int i = 0; i <= 50; ++i)
    fine.push_back(SurfacePoint{static_cast<double>(i), i % 2 ? 0.5 : 0.0});
  EXPECT_EQ(fv::SmoothPathChaikin(fine, false, 3, 4.0).size(), fine.size());
  // And the iteration cap bounds the cost whatever the input.
  const std::vector<SurfacePoint> stair = Staircase(6, 100.0);
  EXPECT_LE(fv::SmoothPathChaikin(stair, false, 3, 4.0).size(),
            stair.size() * 8 + 8);
}

// ---------------------------------------------------------------------------
// Catmull-Rom: interpolating, which is the whole reason it is offered
// ---------------------------------------------------------------------------

TEST(PathShaping, CatmullRomPassesThroughEveryInputVertex) {
  const std::vector<SurfacePoint> stair = Staircase(5, 20.0);
  const std::vector<SurfacePoint> smooth =
      fv::SmoothPathCatmullRom(stair, false, 4.0);
  EXPECT_GT(smooth.size(), stair.size());
  for (const SurfacePoint& p : stair)
    EXPECT_TRUE(Contains(smooth, p)) << "lost vertex " << p.x << "," << p.y;
}

TEST(PathShaping, CatmullRomHandlesRingsAndDegenerateInput) {
  std::vector<SurfacePoint> ring{{0, 0}, {40, 0}, {40, 40}, {0, 40}, {0, 0}};
  const std::vector<SurfacePoint> smooth =
      fv::SmoothPathCatmullRom(ring, true, 4.0);
  EXPECT_GT(smooth.size(), ring.size());
  EXPECT_DOUBLE_EQ(smooth.front().x, smooth.back().x);
  EXPECT_DOUBLE_EQ(smooth.front().y, smooth.back().y);

  // A repeated vertex makes the centripetal knot spacing zero; that segment
  // is passed through rather than dividing by it.
  std::vector<SurfacePoint> dup{{0, 0}, {10, 0}, {10, 0}, {20, 5}};
  const std::vector<SurfacePoint> ok = fv::SmoothPathCatmullRom(dup, false, 4.0);
  for (const SurfacePoint& p : ok) {
    EXPECT_FALSE(std::isnan(p.x));
    EXPECT_FALSE(std::isnan(p.y));
  }
  std::vector<SurfacePoint> two{{0, 0}, {10, 10}};
  EXPECT_EQ(fv::SmoothPathCatmullRom(two, false, 4.0).size(), 2u);
}

}  // namespace
