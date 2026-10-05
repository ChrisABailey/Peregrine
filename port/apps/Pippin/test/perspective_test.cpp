// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the 2.5D camera in `PPPerspective.h` and the tilted-quad form of
/// the base-coverage test.

#include "PPPerspective.h"

#include "PPBaseCoverage.h"

#include <cmath>

#include <gtest/gtest.h>

namespace {

using pippin::Perspective;
using pippin::PerspectiveParams;

// An iPhone 14 Pro screen, with the ship where follow puts it: five sixths down.
constexpr int kW = 1179;
constexpr int kH = 2556;
constexpr double kAnchorX = (kW - 1) / 2.0;
constexpr double kAnchorY = (kH - 1) * 5.0 / 6.0;

Perspective Camera(double pitch, double anchor_y = kAnchorY) {
  PerspectiveParams p;
  p.width = kW;
  p.height = kH;
  p.pitch_deg = pitch;
  p.anchor_x = kAnchorX;
  p.anchor_y = anchor_y;
  return Perspective(p);
}

double BoundsRatio(const Perspective& cam) {
  const pippin::PixelRect r = cam.RenderBounds(0);
  return (double)r.width * r.height / ((double)kW * kH);
}

double QuadRatio(const Perspective& cam) {
  double x[4], y[4];
  EXPECT_TRUE(cam.GroundQuad(x, y));
  double a = 0.0;
  for (int i = 0; i < 4; ++i) a += x[i] * y[(i + 1) % 4] - x[(i + 1) % 4] * y[i];
  return std::fabs(a) / 2.0 / ((double)kW * kH);
}

TEST(Perspective, PitchZeroIsExactlyTheIdentity) {
  const Perspective cam = Camera(0.0);
  EXPECT_TRUE(cam.IsFlat());
  const double pts[][2] = {{0, 0}, {kW - 1, kH - 1}, {123.25, 2001.5}, {-40, 5000}};
  for (const auto& q : pts) {
    double x = 0, y = 0;
    ASSERT_TRUE(cam.FlatToScreen(q[0], q[1], &x, &y));
    EXPECT_EQ(x, q[0]);
    EXPECT_EQ(y, q[1]);
    ASSERT_TRUE(cam.ScreenToFlat(q[0], q[1], &x, &y));
    EXPECT_EQ(x, q[0]);
    EXPECT_EQ(y, q[1]);
  }
  const pippin::PixelRect r = cam.RenderBounds(0);
  EXPECT_EQ(r.x0, 0);
  EXPECT_EQ(r.y0, 0);
  EXPECT_EQ(r.width, kW);
  EXPECT_EQ(r.height, kH);
}

TEST(Perspective, PitchIsClampedToTheCap) {
  EXPECT_EQ(Camera(70.0).pitch_deg(), pippin::kMaxPitchDeg);
  EXPECT_EQ(Camera(-5.0).pitch_deg(), 0.0);
  EXPECT_EQ(Camera(NAN).pitch_deg(), 0.0);
}

TEST(Perspective, TheAnchorKeepsItsPlaceAndItsScale) {
  for (double pitch : {15.0, 30.0, 45.0}) {
    const Perspective cam = Camera(pitch);
    double x = 0, y = 0;
    ASSERT_TRUE(cam.FlatToScreen(kAnchorX, kAnchorY, &x, &y));
    EXPECT_NEAR(x, kAnchorX, 1e-9);
    EXPECT_NEAR(y, kAnchorY, 1e-9);
    // One flat pixel across at the anchor is one screen pixel across.
    double x1 = 0, y1 = 0;
    ASSERT_TRUE(cam.FlatToScreen(kAnchorX + 1.0, kAnchorY, &x1, &y1));
    EXPECT_NEAR(x1 - x, 1.0, 1e-6);
    EXPECT_NEAR(cam.DepthScale(kAnchorY), 1.0, 1e-12);
  }
}

TEST(Perspective, InverseRoundTrips) {
  const Perspective cam = Camera(45.0);
  for (double sx : {0.0, 300.0, 1178.0}) {
    for (double sy : {0.0, 900.0, 2555.0}) {
      double fx = 0, fy = 0, bx = 0, by = 0;
      ASSERT_TRUE(cam.ScreenToFlat(sx, sy, &fx, &fy));
      ASSERT_TRUE(cam.FlatToScreen(fx, fy, &bx, &by));
      EXPECT_NEAR(bx, sx, 1e-6);
      EXPECT_NEAR(by, sy, 1e-6);
    }
  }
}

TEST(Perspective, FarGroundIsSmallerAndWider) {
  const Perspective cam = Camera(45.0);
  EXPECT_LT(cam.DepthScale(0.0), cam.DepthScale(kAnchorY));
  EXPECT_GT(cam.DepthScale(kH - 1.0), 1.0);
  double x[4], y[4];
  ASSERT_TRUE(cam.GroundQuad(x, y));
  // Top edge (far) spans more ground than the bottom edge (near).
  EXPECT_GT(x[1] - x[0], x[2] - x[3]);
  EXPECT_LT(y[0], 0.0);  // the view reaches past the flat screen's top
  double horizon = 0;
  ASSERT_TRUE(cam.HorizonY(&horizon));
  EXPECT_LT(horizon, 0.0);  // 45 + 20 degrees never reaches the horizon
}

// The footprint the base must be drawn at. Holding the ship's detail at five
// sixths down costs more than the centre-anchored figures in the plan.
TEST(Perspective, FootprintRatios) {
  EXPECT_NEAR(BoundsRatio(Camera(30.0, (kH - 1) / 2.0)), 1.53, 0.03);
  EXPECT_NEAR(BoundsRatio(Camera(45.0, (kH - 1) / 2.0)), 2.56, 0.03);
  EXPECT_NEAR(BoundsRatio(Camera(30.0)), 1.99, 0.03);
  EXPECT_NEAR(BoundsRatio(Camera(45.0)), 3.96, 0.04);
  EXPECT_NEAR(QuadRatio(Camera(45.0)), 2.90, 0.03);
}

TEST(Perspective, RenderBoundsCoverTheQuad) {
  const Perspective cam = Camera(45.0);
  const pippin::PixelRect r = cam.RenderBounds(1);
  double x[4], y[4];
  ASSERT_TRUE(cam.GroundQuad(x, y));
  for (int i = 0; i < 4; ++i) {
    EXPECT_GE(x[i], r.x0 - 0.5);
    EXPECT_LE(x[i], r.x0 + r.width - 0.5);
    EXPECT_GE(y[i], r.y0 - 0.5);
    EXPECT_LE(y[i], r.y0 + r.height - 0.5);
  }
}

// A base drawn over the tilted footprint covers it; the flat screen-sized
// base that serves pitch 0 does not.
TEST(Perspective, CoverageTakesTheGroundQuad) {
  constexpr double kMmPerPixel = 25.4 / (163.0 * 3.0);
  const fv::GeoPoint home{32.606, -80.076};
  auto make = [&](int w, int h, fv::GeoPoint c) {
    fv::MapProjection p;
    EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
    EXPECT_TRUE(p.SetCenter(c).ok());
    EXPECT_TRUE(p.SetPhysicalScale(15950.0, kMmPerPixel).ok());
    return p;
  };
  const fv::MapProjection live = make(kW, kH, home);
  const Perspective cam = Camera(45.0);
  double x[4], y[4];
  ASSERT_TRUE(cam.GroundQuad(x, y));

  EXPECT_TRUE(pippin::BaseCovers(live, live));
  EXPECT_FALSE(pippin::BaseCovers(live, live, {}, x, y));

  const pippin::PixelRect r = cam.RenderBounds(2);
  fv::GeoPoint c;
  ASSERT_TRUE(live.SurfaceToGeo(r.x0 + (r.width - 1) / 2.0,
                                r.y0 + (r.height - 1) / 2.0, &c)
                  .ok());
  const fv::MapProjection base = make(r.width, r.height, c);
  EXPECT_TRUE(pippin::BaseCovers(base, live, {}, x, y));
  EXPECT_EQ(pippin::ScreenCoverage(base, nullptr, live, x, y),
            pippin::ScreenCover::kSharp);
  EXPECT_EQ(pippin::ScreenCoverage(live, nullptr, live, x, y),
            pippin::ScreenCover::kBackground);

  // The headroom is measured from the quad, so the tight base has about its
  // two pixels of padding and the screen-sized one is far past its edge.
  EXPECT_NEAR(pippin::BandHeadroomPx(base, live, x, y), 2.0, 1.0);
  EXPECT_LT(pippin::BandHeadroomPx(live, live, x, y), -1000.0);
  EXPECT_NEAR(pippin::BandHeadroomPx(live, live), 0.0, 1e-6);
}

// The band holds the quad's bounds and the whole screen, so a band drawn at
// the live camera is still pixel-aligned with it.
TEST(Perspective, BandRectHoldsTheQuadAndTheScreen) {
  EXPECT_EQ(Camera(0.0).BandRect(4).width, 0);
  for (double pitch : {15.0, 30.0, 45.0}) {
    const Perspective cam = Camera(pitch);
    const pippin::PixelRect b = cam.BandRect(4);
    const pippin::PixelRect r = cam.RenderBounds(4);
    EXPECT_LE(b.x0, std::min(r.x0, 0));
    EXPECT_LE(b.y0, std::min(r.y0, 0));
    EXPECT_GE(b.x0 + b.width, std::max(r.x0 + r.width, kW));
    EXPECT_GE(b.y0 + b.height, std::max(r.y0 + r.height, kH));
  }
  // At 45 degrees nearly all of the extra ground is ahead, above the screen.
  const pippin::PixelRect b = Camera(45.0).BandRect(0);
  EXPECT_LT(b.y0, -2 * kH / 3);
  EXPECT_LT(b.y0 + b.height - kH, 40);
}

TEST(Perspective, TheFarFadeCoversTheLastScreenOfDepth) {
  EXPECT_EQ(Camera(0.0).FarFade().end_y, 0.0);
  EXPECT_EQ(Camera(0.0).FarFade().top_alpha, 0.0);
  const Perspective::Fade f45 = Camera(45.0).FarFade();
  EXPECT_NEAR(f45.end_y, 798.0, 2.0);  // a third of the screen less a bit
  EXPECT_NEAR(f45.top_alpha, 0.98, 0.02);
  // At 30 degrees the view is under two screens deep, so the cap holds.
  const Perspective::Fade f30 = Camera(30.0).FarFade();
  EXPECT_DOUBLE_EQ(f30.end_y, kH / 3.0);
  EXPECT_LT(f30.top_alpha, f45.top_alpha);
}

}  // namespace
