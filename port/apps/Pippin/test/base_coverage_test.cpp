// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// P18's hit test, on the mac. A cache that answers "yes" once too often draws
// the wrong map and a cache that answers "no" every time is not a cache, and
// neither shows up as anything but a feeling in the simulator — so the whole
// decision is a pure function over two projections and the cases are here.

#include "PPBaseCoverage.h"

#include <gtest/gtest.h>

namespace {

// Kiawah, at a scale a rider actually rides at, on a 3x phone.
constexpr double kMmPerPixel = 25.4 / (163.0 * 3.0);
constexpr double kRidingScale = 15950.0;
const fv::GeoPoint kHome{32.606, -80.076};

// A screen: 393x852 points at 3x.
constexpr int kScreenW = 1179;
constexpr int kScreenH = 2556;

fv::MapProjection Make(int w, int h, fv::GeoPoint center, double scale,
                       double rotation) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter(center).ok());
  EXPECT_TRUE(p.SetPhysicalScale(scale, kMmPerPixel).ok());
  EXPECT_TRUE(p.SetRotation(rotation).ok());
  return p;
}

// The band `PPMap` would build for that screen at a given margin.
fv::MapProjection Band(fv::GeoPoint center, double scale, double rotation,
                       double margin) {
  double w = 0, h = 0;
  pippin::GrownSurfaceSize(kScreenW, kScreenH, margin, &w, &h);
  return Make((int)w, (int)h, center, scale, rotation);
}

// Move a centre by n pixels across the screen at this scale.
fv::GeoPoint Shifted(const fv::MapProjection& p, double dx_px, double dy_px) {
  fv::GeoPoint g;
  const fv::PixelSize s = p.SurfaceSize();
  EXPECT_TRUE(p.SurfaceToGeo((s.width - 1) / 2.0 + dx_px,
                             (s.height - 1) / 2.0 + dy_px, &g)
                  .ok());
  return g;
}

TEST(BaseCoverage, TheBandIsTheSurfaceGrownOnEachSide) {
  double w = 0, h = 0;
  pippin::GrownSurfaceSize(100, 200, 0.25, &w, &h);
  EXPECT_DOUBLE_EQ(w, 150);
  EXPECT_DOUBLE_EQ(h, 300);

  pippin::GrownSurfaceSize(100, 200, 0.0, &w, &h);
  EXPECT_DOUBLE_EQ(w, 100) << "no margin is the surface itself";
  EXPECT_DOUBLE_EQ(h, 200);

  pippin::GrownSurfaceSize(100, 200, -1.0, &w, &h);
  EXPECT_DOUBLE_EQ(w, 100) << "a negative margin is no margin, not a shrink";

  pippin::GrownSurfaceSize(101, 101, 0.25, &w, &h);
  EXPECT_DOUBLE_EQ(w, 152) << "rounded outward, so the band is never short";
}

TEST(BaseCoverage, AnUnbandedBaseServesItsOwnCameraAndNothingElse) {
  const fv::MapProjection base = Band(kHome, kRidingScale, 0, 0.0);
  const fv::MapProjection live = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  // The hit that matters most, and the one an edge inset would throw away:
  // the same camera with the ownship somewhere new. No margin is needed,
  // because nothing is resampled.
  EXPECT_TRUE(pippin::BaseCovers(base, live));

  // One pixel of pan and it is off the end, which is what a zero margin
  // asks for.
  const fv::MapProjection moved =
      Make(kScreenW, kScreenH, Shifted(live, 1, 0), kRidingScale, 0);
  EXPECT_FALSE(pippin::BaseCovers(base, moved));
}

TEST(BaseCoverage, ABandAbsorbsAPanUpToItsOwnEdge) {
  const double margin = 0.25;
  const fv::MapProjection base = Band(kHome, kRidingScale, 0, margin);

  // A quarter of the screen on each side, so a pan of just under an eighth of
  // the width in either direction is inside it.
  const fv::MapProjection live = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  const double inside = kScreenW * margin - 4;
  const double outside = kScreenW * margin + 4;

  EXPECT_TRUE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, Shifted(live, inside, 0), kRidingScale, 0)));
  EXPECT_TRUE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, Shifted(live, -inside, 0), kRidingScale, 0)));
  EXPECT_FALSE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, Shifted(live, outside, 0), kRidingScale, 0)));

  // And the tall axis has its own, larger, budget — the margin is a fraction
  // of each axis, so a portrait phone gets more of it vertically. This is the
  // ride case: the map slides up the screen under the ship.
  const double tall_inside = kScreenH * margin - 4;
  EXPECT_TRUE(pippin::BaseCovers(
      base,
      Make(kScreenW, kScreenH, Shifted(live, 0, tall_inside), kRidingScale, 0)));
  EXPECT_FALSE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, Shifted(live, 0, kScreenH * margin + 4),
                 kRidingScale, 0)));
}

TEST(BaseCoverage, AZoomAlwaysMissesHoweverBigTheBand) {
  const fv::MapProjection base = Band(kHome, kRidingScale, 0, 1.0);
  // Zoomed IN: the screen is a small part of the band geographically, so the
  // corner test would pass — the refusal is the scale rule, and it has to be,
  // because the style and the tile source both chose their level from it.
  EXPECT_FALSE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, kHome, kRidingScale / 2.0, 0)));
  EXPECT_FALSE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, kHome, kRidingScale * 1.001, 0)));
  EXPECT_TRUE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, kHome, kRidingScale, 0)));
}

TEST(BaseCoverage, ADifferentPitchIsADifferentPictureAtTheSameOneToN) {
  fv::MapProjection base = Band(kHome, kRidingScale, 0, 0.25);
  fv::MapProjection live = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  ASSERT_TRUE(live.SetPhysicalScale(kRidingScale, kMmPerPixel * 2.0).ok());
  EXPECT_FALSE(pippin::BaseCovers(base, live));
}

TEST(BaseCoverage, TheTurnIsCappedForQualityNotForCoverage) {
  const double margin = 0.5;  // big enough that coverage is not the binding rule
  const fv::MapProjection base = Band(kHome, kRidingScale, 0, margin);

  pippin::BaseCoverageLimits limits;
  limits.max_rotation_delta_deg = 2.5;

  EXPECT_TRUE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, kHome, kRidingScale, 2.0), limits));
  EXPECT_FALSE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, kHome, kRidingScale, 3.0), limits));

  // Raise the cap and the same camera is served: the band had the pixels all
  // along, which is the point being made.
  limits.max_rotation_delta_deg = 10.0;
  EXPECT_TRUE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, kHome, kRidingScale, 3.0), limits));
}

TEST(BaseCoverage, TheTurnIsMeasuredTheShortWayAroundNorth) {
  const fv::MapProjection base = Band(kHome, kRidingScale, 359.0, 0.5);
  pippin::BaseCoverageLimits limits;
  limits.max_rotation_delta_deg = 2.5;
  // 359 -> 1 is two degrees, not 358. A subtraction here would rebuild the
  // base every time a rider crossed north.
  EXPECT_TRUE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, kHome, kRidingScale, 1.0), limits));
}

TEST(BaseCoverage, ATurnedScreenNeedsItsCornersInTheBandAndNotJustItsEdges) {
  // A band big enough for a straight screen is NOT big enough for a turned
  // one: the corners sweep further than the edges do. With the quality cap
  // lifted, this is the coverage rule on its own.
  pippin::BaseCoverageLimits limits;
  limits.max_rotation_delta_deg = 360.0;

  const fv::MapProjection small = Band(kHome, kRidingScale, 0, 0.05);
  EXPECT_TRUE(pippin::BaseCovers(
      small, Make(kScreenW, kScreenH, kHome, kRidingScale, 0), limits));
  EXPECT_FALSE(pippin::BaseCovers(
      small, Make(kScreenW, kScreenH, kHome, kRidingScale, 20), limits));

  const fv::MapProjection big = Band(kHome, kRidingScale, 0, 0.60);
  EXPECT_TRUE(pippin::BaseCovers(
      big, Make(kScreenW, kScreenH, kHome, kRidingScale, 20), limits));
}

TEST(BaseCoverage, APanAndATurnTogetherSpendTheSameBand) {
  pippin::BaseCoverageLimits limits;
  limits.max_rotation_delta_deg = 360.0;
  const fv::MapProjection base = Band(kHome, kRidingScale, 0, 0.25);
  const fv::MapProjection live = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);

  const fv::GeoPoint half = Shifted(live, kScreenW * 0.20, 0);
  EXPECT_TRUE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, half, kRidingScale, 0), limits));
  // The same pan with the chart turned needs corners the band no longer has.
  EXPECT_FALSE(pippin::BaseCovers(
      base, Make(kScreenW, kScreenH, half, kRidingScale, 15), limits));
}

TEST(BaseCoverage, AnUnreadyOrEmptyProjectionNeverServes) {
  fv::MapProjection empty;
  const fv::MapProjection live = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  EXPECT_FALSE(pippin::BaseCovers(empty, live));
  EXPECT_FALSE(pippin::BaseCovers(live, empty));
}

TEST(BaseCoverage, AScreenThatGrewPastTheBandMisses) {
  // The phone rotated, or a split view opened. The band was built for the old
  // surface and the new one does not fit inside it.
  const fv::MapProjection base = Band(kHome, kRidingScale, 0, 0.25);
  EXPECT_FALSE(pippin::BaseCovers(
      base, Make(kScreenH, kScreenW, kHome, kRidingScale, 0)));
}

}  // namespace
