// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// P18's hit test, on the mac. A cache that answers "yes" once too often draws
// the wrong map and a cache that answers "no" every time is not a cache, and
// neither shows up as anything but a feeling in the simulator — so the whole
// decision is a pure function over two projections and the cases are here.

#include "PPBaseCoverage.h"
#include "PPPerspective.h"

#include <cmath>

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
  int w = 0, h = 0;
  pippin::GrownSurfacePixels(kScreenW, kScreenH, margin, false, &w, &h);
  return Make(w, h, center, scale, rotation);
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
  int w = 0, h = 0;
  pippin::GrownSurfacePixels(100, 200, 0.25, false, &w, &h);
  EXPECT_EQ(w, 150);
  EXPECT_EQ(h, 300);

  pippin::GrownSurfacePixels(100, 200, 0.0, false, &w, &h);
  EXPECT_EQ(w, 100) << "no margin is the surface itself";
  EXPECT_EQ(h, 200);

  pippin::GrownSurfacePixels(100, 200, -1.0, false, &w, &h);
  EXPECT_EQ(w, 100) << "a negative margin is no margin, not a shrink";

  pippin::GrownSurfacePixels(101, 101, 0.25, false, &w, &h);
  EXPECT_EQ(w, 153) << "rounded outward, by the same whole pixels each side";
}

TEST(BaseCoverage, TheBandGrowsByAnEvenNumberOfPixels) {
  // 1179 x 1.5 is 1768.5; a band of 1769 would put the centre half a pixel
  // off the screen's.
  int w = 0, h = 0;
  pippin::GrownSurfacePixels(kScreenW, kScreenH, 0.25, false, &w, &h);
  EXPECT_EQ(w, kScreenW + 2 * 295);
  EXPECT_EQ(h, kScreenH + 2 * 639);
  EXPECT_EQ((w - kScreenW) % 2, 0);
  EXPECT_EQ((h - kScreenH) % 2, 0);
}

TEST(BaseCoverage, ARotationSafeBandCoversEveryTurnWithTheMarginToSpare) {
  int w = 0, h = 0;
  pippin::GrownSurfacePixels(kScreenW, kScreenH, 0.25, true, &w, &h);
  const double diagonal = std::hypot(kScreenW, kScreenH);
  EXPECT_GE(w, diagonal);
  EXPECT_GE(h, diagonal);
  EXPECT_EQ((w - kScreenW) % 2, 0);
  EXPECT_EQ((h - kScreenH) % 2, 0);
  const fv::MapProjection band = Make(w, h, kHome, kRidingScale, 0);
  for (double turn = 0; turn < 360; turn += 7.5) {
    const fv::MapProjection live =
        Make(kScreenW, kScreenH, kHome, kRidingScale, turn);
    EXPECT_GE(pippin::BandHeadroomPx(band, live), 147.0) << turn;
  }
}

TEST(PixelAligned, ABandDrawnAtTheLiveCameraLandsOnTheScreensPixels) {
  const fv::MapProjection live = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  EXPECT_TRUE(pippin::PixelAligned(Band(kHome, kRidingScale, 0, 0.25), live));
  EXPECT_TRUE(pippin::PixelAligned(Band(kHome, kRidingScale, 0, 0.0), live));

  // The old band, 1769 wide by rounding the whole width: half a pixel off.
  const fv::MapProjection odd = Make(1769 + 1, 3834, kHome, kRidingScale, 0);
  EXPECT_FALSE(pippin::PixelAligned(odd, live));
}

TEST(PixelAligned, APanByWholePixelsStaysAlignedAndAFractionDoesNot) {
  const fv::MapProjection band = Band(kHome, kRidingScale, 30, 0.25);
  EXPECT_TRUE(pippin::PixelAligned(
      band, Make(kScreenW, kScreenH, Shifted(band, 12, -40), kRidingScale, 30)));
  EXPECT_FALSE(pippin::PixelAligned(
      band, Make(kScreenW, kScreenH, Shifted(band, 12.5, 0), kRidingScale, 30)));
  // A north-south pan moves the reference latitude. Across most of the band
  // at riding scale the drift stays under the tolerance; the same pan in
  // pixels at the widest view is five times the ground and is refused, so
  // the settle redraws it.
  const fv::MapProjection riding = Band(kHome, kRidingScale, 0, 0.25);
  EXPECT_TRUE(pippin::PixelAligned(
      riding,
      Make(kScreenW, kScreenH, Shifted(riding, 0, 600), kRidingScale, 0)));
  const fv::MapProjection wide = Band(kHome, 96161, 0, 0.25);
  EXPECT_FALSE(pippin::PixelAligned(
      wide, Make(kScreenW, kScreenH, Shifted(wide, 0, 600), 96161, 0)));
}

TEST(PixelAligned, ScaleTurnAndAScreenPastTheBandAreRefused) {
  const fv::MapProjection band = Band(kHome, kRidingScale, 0, 0.25);
  EXPECT_FALSE(pippin::PixelAligned(
      band, Make(kScreenW, kScreenH, kHome, kRidingScale * 1.01, 0)));
  EXPECT_FALSE(pippin::PixelAligned(
      band, Make(kScreenW, kScreenH, kHome, kRidingScale, 0.5)));
  EXPECT_FALSE(pippin::PixelAligned(
      band, Make(kScreenW, kScreenH, Shifted(band, 300, 0), kRidingScale, 0)));
  fv::MapProjection empty;
  EXPECT_FALSE(pippin::PixelAligned(empty, band));
}

TEST(BandHeadroom, IsTheMarginAtRestAndShrinksWithAPan) {
  const fv::MapProjection band = Band(kHome, kRidingScale, 0, 0.25);
  const fv::MapProjection live = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  EXPECT_NEAR(pippin::BandHeadroomPx(band, live), 295.0, 1e-6);
  EXPECT_NEAR(pippin::BandHeadroomPx(
                  band, Make(kScreenW, kScreenH, Shifted(band, 200, 0),
                             kRidingScale, 0)),
              95.0, 1e-6);
  EXPECT_LT(pippin::BandHeadroomPx(
                band, Make(kScreenW, kScreenH, Shifted(band, 0, 700),
                           kRidingScale, 0)),
            0.0)
      << "past the edge is negative";
}

TEST(BandLead, IsVelocityTimesDrawTimeCappedInsideTheMargin) {
  EXPECT_DOUBLE_EQ(pippin::BandLead(1000, 0.03, 98), 30.0);
  EXPECT_DOUBLE_EQ(pippin::BandLead(-1000, 0.03, 98), -30.0);
  EXPECT_DOUBLE_EQ(pippin::BandLead(5000, 0.07, 98), 73.5);
  EXPECT_DOUBLE_EQ(pippin::BandLead(-5000, 0.07, 98), -73.5);
  EXPECT_DOUBLE_EQ(pippin::BandLead(1000, 0.0, 98), 0.0);
  EXPECT_DOUBLE_EQ(pippin::BandLead(1000, 0.03, 0.0), 0.0);
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

// The underlay `PPMap` would build for a screen at this camera.
fv::MapProjection Underlay(fv::GeoPoint center, double scale, double rotation) {
  return Make(kScreenW, kScreenH, center, scale * pippin::kUnderlayZoomOut,
              rotation);
}

TEST(ScreenCoverage, AScreenInsideTheBandIsSharp) {
  const fv::MapProjection base = Band(kHome, kRidingScale, 0, 0.25);
  const fv::MapProjection under = Underlay(kHome, kRidingScale, 0);
  const fv::MapProjection live = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  EXPECT_EQ(pippin::ScreenCoverage(base, &under, live),
            pippin::ScreenCover::kSharp);
  EXPECT_EQ(pippin::ScreenCoverage(base, nullptr, live),
            pippin::ScreenCover::kSharp);
}

TEST(ScreenCoverage, APanPastTheBandIsSoftWithAnUnderlayAndBlankWithout) {
  const fv::MapProjection base = Band(kHome, kRidingScale, 0, 0.25);
  const fv::MapProjection under = Underlay(kHome, kRidingScale, 0);
  const fv::MapProjection seed = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  // Half a screen right: past the band's quarter, well inside the underlay.
  const fv::MapProjection live = Make(
      kScreenW, kScreenH, Shifted(seed, kScreenW * 0.5, 0), kRidingScale, 0);
  EXPECT_EQ(pippin::ScreenCoverage(base, &under, live),
            pippin::ScreenCover::kUnderlay);
  EXPECT_EQ(pippin::ScreenCoverage(base, nullptr, live),
            pippin::ScreenCover::kBackground);
}

TEST(ScreenCoverage, TheUnderlayCoversAScreenAndAHalfOfPan) {
  const fv::MapProjection base = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  const fv::MapProjection under = Underlay(kHome, kRidingScale, 0);
  // Four screens of ground: the live screen's far edge reaches the
  // underlay's edge after a screen and a half of pan in either axis.
  const fv::MapProjection in_x = Make(
      kScreenW, kScreenH, Shifted(base, kScreenW * 1.45, 0), kRidingScale, 0);
  EXPECT_EQ(pippin::ScreenCoverage(base, &under, in_x),
            pippin::ScreenCover::kUnderlay);
  const fv::MapProjection out_x = Make(
      kScreenW, kScreenH, Shifted(base, kScreenW * 1.55, 0), kRidingScale, 0);
  EXPECT_EQ(pippin::ScreenCoverage(base, &under, out_x),
            pippin::ScreenCover::kBackground);
  const fv::MapProjection in_y = Make(
      kScreenW, kScreenH, Shifted(base, 0, -kScreenH * 1.45), kRidingScale, 0);
  EXPECT_EQ(pippin::ScreenCoverage(base, &under, in_y),
            pippin::ScreenCover::kUnderlay);
}

TEST(ScreenCoverage, TheUnderlayCoversEveryTurn) {
  // A screen-sized base loses its corners at any turn; the underlay does not.
  const fv::MapProjection base = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  const fv::MapProjection under = Underlay(kHome, kRidingScale, 0);
  for (double turn = 5; turn < 360; turn += 5) {
    const fv::MapProjection live =
        Make(kScreenW, kScreenH, kHome, kRidingScale, turn);
    // Half a turn puts a screen-sized base exactly back on itself.
    const pippin::ScreenCover want = turn == 180
                                         ? pippin::ScreenCover::kSharp
                                         : pippin::ScreenCover::kUnderlay;
    EXPECT_EQ(pippin::ScreenCoverage(base, &under, live), want)
        << turn << " degrees";
  }
}

TEST(ScreenCoverage, APinchOutInsideTheBaseStaysSharp) {
  // Zooming in shrinks the ground the screen shows, so a scaled base still
  // covers it; the scale test belongs to `BaseCovers`, not to this one.
  const fv::MapProjection base = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  const fv::MapProjection live =
      Make(kScreenW, kScreenH, kHome, kRidingScale / 1.5, 0);
  EXPECT_EQ(pippin::ScreenCoverage(base, nullptr, live),
            pippin::ScreenCover::kSharp);
  const fv::MapProjection wider =
      Make(kScreenW, kScreenH, kHome, kRidingScale * 1.5, 0);
  EXPECT_EQ(pippin::ScreenCoverage(base, nullptr, wider),
            pippin::ScreenCover::kBackground);
}

TEST(UnderlayServes, TheCameraItWasBuiltForIsServed) {
  const fv::MapProjection under = Underlay(kHome, kRidingScale, 0);
  EXPECT_TRUE(pippin::UnderlayServes(
      under, Make(kScreenW, kScreenH, kHome, kRidingScale, 0)));
  EXPECT_TRUE(pippin::UnderlayServes(
      under, Make(kScreenW, kScreenH, kHome, kRidingScale, 137)))
      << "a turn never stales it";
}

TEST(UnderlayServes, LeavingTheMiddleHalfStalesIt) {
  const fv::MapProjection live = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  const fv::MapProjection under = Underlay(kHome, kRidingScale, 0);
  // The middle half of four screens is a screen either side of the centre.
  EXPECT_TRUE(pippin::UnderlayServes(
      under, Make(kScreenW, kScreenH, Shifted(live, kScreenW * 0.95, 0),
                  kRidingScale, 0)));
  EXPECT_FALSE(pippin::UnderlayServes(
      under, Make(kScreenW, kScreenH, Shifted(live, kScreenW * 1.05, 0),
                  kRidingScale, 0)));
  EXPECT_FALSE(pippin::UnderlayServes(
      under, Make(kScreenW, kScreenH, Shifted(live, 0, kScreenH * 1.05),
                  kRidingScale, 0)));
}

TEST(UnderlayServes, AScaleTwiceAwayStalesIt) {
  const fv::MapProjection under = Underlay(kHome, kRidingScale, 0);
  EXPECT_TRUE(pippin::UnderlayServes(
      under, Make(kScreenW, kScreenH, kHome, kRidingScale * 2.0, 0)));
  EXPECT_TRUE(pippin::UnderlayServes(
      under, Make(kScreenW, kScreenH, kHome, kRidingScale / 2.0, 0)));
  EXPECT_FALSE(pippin::UnderlayServes(
      under, Make(kScreenW, kScreenH, kHome, kRidingScale * 2.1, 0)));
  EXPECT_FALSE(pippin::UnderlayServes(
      under, Make(kScreenW, kScreenH, kHome, kRidingScale / 2.1, 0)));
}

// The ground quad of the 3x screen tilted to the cap, anchored where follow
// puts the ship (5/6 down).
struct Quad {
  double xs[4], ys[4];
};
Quad TiltedQuad() {
  pippin::PerspectiveParams p;
  p.width = kScreenW;
  p.height = kScreenH;
  p.pitch_deg = pippin::kMaxPitchDeg;
  p.anchor_x = (kScreenW - 1) / 2.0;
  p.anchor_y = (kScreenH - 1) * (5.0 / 6.0);
  Quad q;
  EXPECT_TRUE(pippin::Perspective(p).GroundQuad(q.xs, q.ys));
  return q;
}

// The underlay `PPMap` would build for that screen tilted.
fv::MapProjection TiltedUnderlay(fv::GeoPoint center, double scale,
                                 double rotation) {
  const Quad q = TiltedQuad();
  int w = 0, h = 0;
  pippin::UnderlaySurfacePixels(kScreenW, kScreenH, q.xs, q.ys, &w, &h);
  return Make(w, h, center, scale * pippin::kUnderlayZoomOut, rotation);
}

TEST(UnderlaySurface, FlatIsTheScreenAndTiltedIsASquareOfTheLongSide) {
  int w = 0, h = 0;
  pippin::UnderlaySurfacePixels(kScreenW, kScreenH, nullptr, nullptr, &w, &h);
  EXPECT_EQ(w, kScreenW);
  EXPECT_EQ(h, kScreenH);
  const Quad q = TiltedQuad();
  pippin::UnderlaySurfacePixels(kScreenW, kScreenH, q.xs, q.ys, &w, &h);
  EXPECT_EQ(w, kScreenH);
  EXPECT_EQ(h, kScreenH);
  // A screen whose quad would not fit the long side gets a larger square.
  const double far_x[4] = {-4000, 4000, 4000, -4000};
  const double far_y[4] = {-4000, -4000, 4000, 4000};
  pippin::UnderlaySurfacePixels(kScreenW, kScreenH, far_x, far_y, &w, &h);
  EXPECT_GT(w, kScreenH);
  EXPECT_EQ(w, h);
}

TEST(UnderlaySurface, TheScreenSizedUnderlayMissesATiltedScreenTurnedSideways) {
  // Found on the simulator: the chart turned near 90 degrees under a
  // screen-sized underlay showed background past its short axis.
  const Quad q = TiltedQuad();
  const fv::MapProjection live = Make(kScreenW, kScreenH, kHome, kRidingScale, 90);
  const fv::MapProjection base = Make(kScreenW, kScreenH, kHome, kRidingScale, 90);
  const fv::MapProjection narrow = Underlay(kHome, kRidingScale, 0);
  const fv::MapProjection square = TiltedUnderlay(kHome, kRidingScale, 0);
  EXPECT_EQ(pippin::ScreenCoverage(base, &narrow, live, q.xs, q.ys),
            pippin::ScreenCover::kBackground);
  EXPECT_EQ(pippin::ScreenCoverage(base, &square, live, q.xs, q.ys),
            pippin::ScreenCover::kUnderlay);
  EXPECT_FALSE(pippin::UnderlayServes(narrow, live, q.xs, q.ys))
      << "so the narrow one is rebuilt";
}

TEST(UnderlayServes, ATiltedScreensSquareServesItAtEveryTurnAndFlattened) {
  const Quad q = TiltedQuad();
  for (int turn = 0; turn < 360; turn += 5) {
    const fv::MapProjection under = TiltedUnderlay(kHome, kRidingScale, 0);
    const fv::MapProjection live =
        Make(kScreenW, kScreenH, kHome, kRidingScale, turn);
    EXPECT_TRUE(pippin::UnderlayServes(under, live, q.xs, q.ys))
        << "a fresh underlay is not stale at turn " << turn;
    EXPECT_TRUE(pippin::UnderlayServes(under, live))
        << "and still serves the screen flattened by a drag";
  }
}

TEST(UnderlayServes, ATiltedScreenRunningAheadStalesItWhileStillCovered) {
  const Quad q = TiltedQuad();
  const fv::MapProjection under = TiltedUnderlay(kHome, kRidingScale, 0);
  const fv::MapProjection seed = Make(kScreenW, kScreenH, kHome, kRidingScale, 0);
  const fv::MapProjection base = seed;
  int stale_at = -1;
  for (int ahead = 0; ahead <= 2 * kScreenH; ahead += 20) {
    const fv::MapProjection live = Make(
        kScreenW, kScreenH, Shifted(seed, 0, -ahead), kRidingScale, 0);
    if (!pippin::UnderlayServes(under, live, q.xs, q.ys)) {
      stale_at = ahead;
      EXPECT_EQ(pippin::ScreenCoverage(base, &under, live, q.xs, q.ys),
                pippin::ScreenCover::kUnderlay)
          << "stale before the far edge runs off it";
      break;
    }
  }
  EXPECT_GT(stale_at, 200) << "not rebuilt on every fix";
}

TEST(UnderlayServes, ANewScreenSizeStalesIt) {
  const fv::MapProjection under = Underlay(kHome, kRidingScale, 0);
  EXPECT_FALSE(pippin::UnderlayServes(
      under, Make(kScreenH, kScreenW, kHome, kRidingScale, 0)));
  fv::MapProjection empty;
  EXPECT_FALSE(pippin::UnderlayServes(empty, under));
}

}  // namespace
