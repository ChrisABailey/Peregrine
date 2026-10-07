// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Viewport camera arithmetic and the preview transform.

#include "fv_view_map_view.h"
#include "fv_view_viewport.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

using fv::GeoPoint;
using fv::ProjectionType;
using fv::view::Affine;
using fv::view::PointF;
using fv::view::PreviewTransform;
using fv::view::ScaleLimits;
using fv::view::Viewport;

// Charleston harbour, a 1200x800-point window on a 2x screen.
Viewport Charleston(double rotation = 0.0,
                    ProjectionType type = ProjectionType::kEqualArc) {
  return Viewport::Make(GeoPoint{32.78, -79.93}, 250000.0, rotation)
      .WithProjectionType(type)
      .WithSurface(1200, 800, 2.0, 0.25);
}

// Positions compared to well under a pixel at 1:1,000 (about 1e-7 degrees).
void ExpectSameGeo(const GeoPoint& a, const GeoPoint& b) {
  EXPECT_NEAR(a.lat, b.lat, 1e-8);
  EXPECT_NEAR(a.lon, b.lon, 1e-8);
}

TEST(Viewport, SurfaceCentreShowsTheCentre) {
  const Viewport v = Charleston();
  ASSERT_TRUE(v.HasSurface());
  EXPECT_EQ(v.PixelWidth(), 2400);
  EXPECT_EQ(v.PixelHeight(), 1600);
  ExpectSameGeo(v.GeoAt(v.SurfaceCenter()), v.Center());
}

TEST(Viewport, NoSurfaceCannotPanOrZoom) {
  const Viewport v = Viewport::Make(GeoPoint{10, 20}, 1e6);
  EXPECT_FALSE(v.HasSurface());
  EXPECT_EQ(v.Panned(10, 10), v);
  EXPECT_EQ(v.ZoomedBy(2, PointF{0, 0}), v);
  EXPECT_EQ(v.WithSurface(0, 100, 2, 0.25), v);
}

TEST(Viewport, PanMovesContentWithThePointer) {
  const Viewport v = Charleston();
  const PointF c = v.SurfaceCenter();
  const GeoPoint g = v.GeoAt(PointF{c.x - 40, c.y + 25});
  const Viewport moved = v.Panned(40, -25);
  ExpectSameGeo(moved.Center(), g);
  // Dragging right moves the centre west, not east; up moves it south.
  EXPECT_LT(moved.Center().lon, v.Center().lon);
  EXPECT_LT(moved.Center().lat, v.Center().lat);
}

class GeoAtPin : public ::testing::TestWithParam<double> {};

TEST_P(GeoAtPin, GrabbedPositionLandsUnderThePointer) {
  // A long north-south drag: the x scale changes with the centre latitude,
  // and the pin still lands to a millionth of a point.
  const Viewport v = Charleston(GetParam());
  const PointF from{900, 650}, to{300, 40};
  const GeoPoint g = v.GeoAt(from);
  PointF q;
  ASSERT_TRUE(v.WithGeoAt(g, to).PointFor(g, &q));
  EXPECT_NEAR(q.x, to.x, 1e-6);
  EXPECT_NEAR(q.y, to.y, 1e-6);
}

INSTANTIATE_TEST_SUITE_P(Rotations, GeoAtPin, ::testing::Values(0.0, 30.0, 200.0));

class ZoomAnchor : public ::testing::TestWithParam<std::pair<double, ProjectionType>> {};

TEST_P(ZoomAnchor, PointUnderTheCursorStaysPut) {
  const Viewport v = Charleston(GetParam().first, GetParam().second);
  const PointF corner{40, 760};
  const GeoPoint under = v.GeoAt(corner);
  for (double f : {2.0, 0.5, 3.7}) {
    const Viewport z = v.ZoomedBy(f, corner);
    EXPECT_NEAR(z.ScaleDenom(), v.ScaleDenom() / f, 1e-6);
    ExpectSameGeo(z.GeoAt(corner), under);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Projections, ZoomAnchor,
    ::testing::Values(std::make_pair(0.0, ProjectionType::kEqualArc),
                      std::make_pair(37.0, ProjectionType::kEqualArc),
                      std::make_pair(0.0, ProjectionType::kMercator),
                      std::make_pair(0.0, ProjectionType::kLambert)));

TEST(Viewport, ZoomIntoALimitKeepsTheAnchor) {
  const Viewport v = Charleston().WithLimits(ScaleLimits{100000.0, 1e7});
  const PointF p{1000, 100};
  const GeoPoint under = v.GeoAt(p);
  const Viewport z = v.ZoomedBy(10.0, p);
  EXPECT_EQ(z.ScaleDenom(), 100000.0);
  ExpectSameGeo(z.GeoAt(p), under);
}

TEST(Viewport, RotateAboutAnAnchor) {
  const Viewport v = Charleston(350.0);
  const PointF p{200, 600};
  const GeoPoint under = v.GeoAt(p);
  const Viewport r = v.RotatedBy(25.0, p);
  EXPECT_NEAR(r.Rotation(), 15.0, 1e-12);
  ExpectSameGeo(r.GeoAt(p), under);
}

TEST(PreviewTransform, IdentityForTheSameView) {
  const Viewport v = Charleston();
  Affine m;
  ASSERT_TRUE(PreviewTransform(v, v, &m));
  EXPECT_NEAR(m.a, 1, 1e-9);
  EXPECT_NEAR(m.b, 0, 1e-9);
  EXPECT_NEAR(m.c, 0, 1e-9);
  EXPECT_NEAR(m.d, 1, 1e-9);
  EXPECT_NEAR(m.tx, 0, 1e-6);
  EXPECT_NEAR(m.ty, 0, 1e-6);
}

// The fitted transform agrees with the projections at points across the frame.
void ExpectTransformMatches(const Viewport& frame, const Viewport& live, double tol) {
  Affine m;
  ASSERT_TRUE(PreviewTransform(frame, live, &m));
  for (PointF fp : {PointF{0, 0}, PointF{1199, 0}, PointF{600, 400}, PointF{0, 799},
                    PointF{1199, 799}}) {
    PointF want;
    ASSERT_TRUE(live.PointFor(frame.GeoAt(fp), &want));
    const PointF got = m.Apply(fp);
    EXPECT_NEAR(got.x, want.x, tol) << fp.x << "," << fp.y;
    EXPECT_NEAR(got.y, want.y, tol) << fp.x << "," << fp.y;
  }
}

TEST(PreviewTransform, PanIsATranslation) {
  const Viewport v = Charleston();
  const Viewport live = v.Panned(12, -7);
  ExpectTransformMatches(v, live, 1e-6);
  Affine m;
  ASSERT_TRUE(PreviewTransform(v, live, &m));
  // Close to a pure shift; the x scale follows the centre latitude.
  EXPECT_NEAR(m.a, 1, 1e-3);
  EXPECT_NEAR(m.d, 1, 1e-3);
  EXPECT_NEAR(m.tx, 12, 0.05);
  EXPECT_NEAR(m.ty, -7, 0.05);
}

TEST(PreviewTransform, ZoomScalesAboutTheAnchor) {
  const Viewport v = Charleston();
  const PointF anchor{100, 700};
  const Viewport live = v.ZoomedBy(2.0, anchor);
  ExpectTransformMatches(v, live, 1e-6);
  Affine m;
  ASSERT_TRUE(PreviewTransform(v, live, &m));
  EXPECT_NEAR(m.a, 2, 5e-3);
  EXPECT_NEAR(m.d, 2, 5e-3);
  const PointF a = m.Apply(anchor);
  EXPECT_NEAR(a.x, anchor.x, 1e-6);
  EXPECT_NEAR(a.y, anchor.y, 1e-6);
}

TEST(PreviewTransform, MercatorIsAFirstOrderFit) {
  const Viewport v = Charleston(0.0, ProjectionType::kMercator);
  ExpectTransformMatches(v, v.ZoomedBy(1.5, PointF{900, 100}), 0.5);
}

TEST(PreviewTransform, TurnIsARotation) {
  const Viewport v = Charleston();
  Affine m;
  ASSERT_TRUE(PreviewTransform(v, v.RotatedBy(90.0, v.SurfaceCenter()), &m));
  // Clockwise on a y-down screen: +x maps to +y.
  EXPECT_NEAR(m.a, 0, 1e-6);
  EXPECT_NEAR(m.b, 1, 1e-6);
  EXPECT_NEAR(m.c, -1, 1e-6);
  EXPECT_NEAR(m.d, 0, 1e-6);
}

}  // namespace
