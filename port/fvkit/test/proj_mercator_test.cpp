// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/** @file
 * Mercator (PJ2): an independent-formulation oracle, the round trip, the
 * kMercatorMaxLat gate, the centre walk that keeps the view inside it, and
 * the separability that makes Mercator the easy first projection.
 *
 * The plan's GEOTRANS oracle is not usable here: every GEOTRANS projection
 * rejects an inverse flattening outside [250, 350], so it cannot be
 * configured as the sphere the FalconView projectors use. The oracle below is
 * the gudermannian form (asinh/sinh) instead of the ported half-angle
 * logarithm, which is a different expression of the same projection and so
 * still catches a transcription error.
 */

#include "fvkit/proj.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

constexpr double kR = 6378137.0;  // WGS84_a_METERS, the projectors' sphere

/// Spherical Mercator plane coordinates in metres, relative to the projection
/// centre, in the gudermannian form.
void Oracle(double std_parallel_deg, double lat_deg, double dlon_deg,
            double* x_m, double* y_m) {
  const double k = kR * std::cos(std_parallel_deg * M_PI / 180.0);
  *x_m = k * (dlon_deg * M_PI / 180.0);
  *y_m = k * std::asinh(std::tan(lat_deg * M_PI / 180.0));
}

/// A projection ready to use: 800x600, 1:2,000,000, Mercator at `center`.
fv::MapProjection Make(double lat, double lon, double scale = 2000000.0,
                       int w = 800, int h = 600) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter({lat, lon}).ok());
  EXPECT_TRUE(p.SetScale(scale).ok());
  EXPECT_TRUE(p.SetProjectionType(fv::ProjectionType::kMercator).ok());
  EXPECT_TRUE(p.Ready());
  return p;
}

TEST(ProjMercator, TypeIsAcceptedAndIsNotAffine) {
  fv::MapProjection p = Make(32.78, -79.93);
  EXPECT_EQ(p.Type(), fv::ProjectionType::kMercator);
  EXPECT_FALSE(p.IsAffine());
}

// The port reports surface pixels, not plane metres, so the oracle is checked
// up to one scale factor: the ratio of plane metres to pixels must be the same
// constant on both axes at every sample, which pins the projection formula
// without pinning the metres-per-pixel derivation.
TEST(ProjMercator, OracleAgreesUpToOneScaleFactor) {
  for (double clat : {0.0, 32.78, -41.3, 60.0}) {
    fv::MapProjection p = Make(clat, -79.93);
    double cx = 0, cy = 0;
    ASSERT_TRUE(p.GeoToSurface({clat, -79.93}, &cx, &cy).ok());

    double k = 0;  // metres per pixel, taken from the first sample
    for (double dlat : {-6.0, -2.5, -0.1, 0.0, 0.1, 2.5, 6.0}) {
      for (double dlon : {-9.0, -3.0, 0.0, 3.0, 9.0}) {
        const double lat = clat + dlat;
        if (std::fabs(lat) > fv::kMercatorMaxLat) continue;
        double sx = 0, sy = 0;
        ASSERT_TRUE(p.GeoToSurface({lat, -79.93 + dlon}, &sx, &sy).ok());
        double ox = 0, oy = 0, oy0 = 0, ignored = 0;
        Oracle(clat, lat, dlon, &ox, &oy);
        Oracle(clat, clat, 0.0, &ignored, &oy0);
        // Surface y is down, so it runs against the plane northing.
        const double dy_m = oy0 - oy;
        if (k == 0 && sx != cx) k = ox / (sx - cx);
        if (sx != cx) EXPECT_NEAR(ox / (sx - cx), k, std::fabs(k) * 1e-9);
        if (sy != cy) EXPECT_NEAR(dy_m / (sy - cy), k, std::fabs(k) * 1e-9);
      }
    }
    EXPECT_GT(k, 0);
  }
}

TEST(ProjMercator, RoundTrips) {
  for (double rot : {0.0, 37.0, 90.0, 215.0}) {
    fv::MapProjection p = Make(32.78, -79.93);
    ASSERT_TRUE(p.SetRotation(rot).ok());
    for (double dlat : {-5.0, 0.0, 3.25}) {
      for (double dlon : {-170.0, -7.5, 0.0, 7.5, 170.0}) {
        const fv::GeoPoint in{32.78 + dlat,
                              fv::NormalizeLon(-79.93 + dlon)};
        double sx = 0, sy = 0;
        ASSERT_TRUE(p.GeoToSurface(in, &sx, &sy).ok());
        fv::GeoPoint out;
        ASSERT_TRUE(p.SurfaceToGeo(sx, sy, &out).ok());
        EXPECT_NEAR(out.lat, in.lat, 1e-9);
        EXPECT_NEAR(out.lon, in.lon, 1e-9);
      }
    }
  }
}

// The unwrapped forward is the same map with the short-way unwrap removed:
// 200 degrees east lands 200 degrees east, not 160 west.
TEST(ProjMercator, UnwrappedForwardDoesNotTakeTheShortWay) {
  fv::MapProjection p = Make(0.0, 0.0, 200000000.0);
  double wrapped_x = 0, wrapped_y = 0, raw_x = 0, raw_y = 0;
  ASSERT_TRUE(p.GeoToSurface({10.0, -160.0}, &wrapped_x, &wrapped_y).ok());
  ASSERT_TRUE(p.GeoToSurfaceUnwrapped({10.0, 200.0}, &raw_x, &raw_y).ok());
  EXPECT_NEAR(raw_y, wrapped_y, 1e-9);
  EXPECT_GT(raw_x, wrapped_x);
  double edge_x = 0, edge_y = 0;
  ASSERT_TRUE(p.GeoToSurface({10.0, 180.0}, &edge_x, &edge_y).ok());
  EXPECT_NEAR(raw_x - wrapped_x, 2 * (edge_x - (p.SurfaceSize().width - 1) / 2.0),
              1e-6);
}

TEST(ProjMercator, BeyondTheLimitIsNotProjectable) {
  fv::MapProjection p = Make(0.0, 0.0, 100000000.0);
  double sx = 0, sy = 0;
  EXPECT_TRUE(p.GeoToSurface({fv::kMercatorMaxLat, 0.0}, &sx, &sy).ok());
  EXPECT_EQ(p.GeoToSurface({fv::kMercatorMaxLat + 1e-9, 0.0}, &sx, &sy).code,
            fv::kNotProjectable);
  EXPECT_EQ(p.GeoToSurface({-fv::kMercatorMaxLat - 1e-9, 0.0}, &sx, &sy).code,
            fv::kNotProjectable);
  EXPECT_EQ(
      p.GeoToSurfaceUnwrapped({85.0, 0.0}, &sx, &sy).code,
      fv::kNotProjectable);

  // And the inverse: a pixel row past the limit is empty, not smeared.
  double lim_x = 0, lim_y = 0;
  ASSERT_TRUE(p.GeoToSurface({fv::kMercatorMaxLat, 0.0}, &lim_x, &lim_y).ok());
  fv::GeoPoint out;
  EXPECT_TRUE(p.SurfaceToGeo(lim_x, lim_y + 1, &out).ok());
  EXPECT_EQ(p.SurfaceToGeo(lim_x, lim_y - 1, &out).code,
            fv::kNotProjectable);
}

// validate_center: a centre high enough to push the top edge past the limit is
// walked back towards the equator until the edge is inside it. The caller's
// requested centre is what Center() keeps reporting.
TEST(ProjMercator, CentreWalksBackToKeepTheViewInsideTheLimit) {
  fv::MapProjection p = Make(79.5, 10.0, 5000000.0, 400, 400);
  EXPECT_DOUBLE_EQ(p.Center().lat, 79.5);
  fv::GeoPoint top;
  ASSERT_TRUE(p.SurfaceToGeo((400 - 1) / 2.0, 0.0, &top).ok());
  EXPECT_LE(top.lat, fv::kMercatorMaxLat);
  // The walk stops as soon as the edge is inside, so it does not overshoot by
  // more than the one pixel of latitude it steps with.
  EXPECT_GT(top.lat, fv::kMercatorMaxLat - 2 * p.DegPerPixelLat());

  fv::MapProjection s = Make(-79.5, 10.0, 5000000.0, 400, 400);
  fv::GeoPoint bottom;
  ASSERT_TRUE(s.SurfaceToGeo((400 - 1) / 2.0, 399.0, &bottom).ok());
  EXPECT_GE(bottom.lat, -fv::kMercatorMaxLat);
}

// A centre far enough inside the limit is left exactly where the caller put
// it: the walk must not fire on an ordinary view.
TEST(ProjMercator, OrdinaryCentreIsNotMoved) {
  fv::MapProjection p = Make(32.78, -79.93);
  double sx = 0, sy = 0;
  ASSERT_TRUE(p.GeoToSurface({32.78, -79.93}, &sx, &sy).ok());
  EXPECT_DOUBLE_EQ(sx, (800 - 1) / 2.0);
  EXPECT_DOUBLE_EQ(sy, (600 - 1) / 2.0);
}

// Meridians and parallels stay axis-aligned, which is what makes a vector bug
// in Mercator show up as a scale error rather than as shear.
TEST(ProjMercator, MeridiansAndParallelsAreAxisAligned) {
  fv::MapProjection p = Make(32.78, -79.93);
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  ASSERT_TRUE(p.GeoToSurface({30.0, -79.93}, &x0, &y0).ok());
  ASSERT_TRUE(p.GeoToSurface({36.0, -79.93}, &x1, &y1).ok());
  EXPECT_DOUBLE_EQ(x0, x1);
  ASSERT_TRUE(p.GeoToSurface({32.78, -82.0}, &x0, &y0).ok());
  ASSERT_TRUE(p.GeoToSurface({32.78, -77.0}, &x1, &y1).ok());
  EXPECT_DOUBLE_EQ(y0, y1);
}

// At the standard parallel the projection is true to scale, so a pixel at the
// centre spans the equal-arc pixel the dpp came from — to within 1%, because
// the metres per pixel are measured on the WGS84 ellipsoid (Windows'
// GEO_geo_to_distance) and then spent on a sphere of radius WGS84_a. A degree
// of meridian is 110.6 km on the ellipsoid at the equator and 111.3 km on that
// sphere, so the pixel is 0.67% short there and less further north. This
// mismatch is FalconView's, carried over deliberately.
TEST(ProjMercator, ScaleAtTheCentreMatchesTheEqualArcPixel) {
  for (double clat : {0.0, 45.0, -60.0}) {
    fv::MapProjection p = Make(clat, 12.0);
    fv::GeoPoint a, b;
    const double cx = (800 - 1) / 2.0, cy = (600 - 1) / 2.0;
    // Centred differences: the Mercator latitude step grows away from the
    // equator, so a one-sided difference carries a first-order error that
    // swamps the isotropy being checked.
    ASSERT_TRUE(p.SurfaceToGeo(cx, cy - 0.5, &a).ok());
    ASSERT_TRUE(p.SurfaceToGeo(cx, cy + 0.5, &b).ok());
    const double step = a.lat - b.lat;
    EXPECT_NEAR(step, p.DegPerPixelLat(), p.DegPerPixelLat() * 0.01);
    // East-west a Mercator pixel covers the same GROUND as north-south, where
    // equal arc keeps a constant degree step on both axes instead.
    ASSERT_TRUE(p.SurfaceToGeo(cx - 0.5, cy, &a).ok());
    ASSERT_TRUE(p.SurfaceToGeo(cx + 0.5, cy, &b).ok());
    EXPECT_NEAR((b.lon - a.lon) * std::cos(clat * M_PI / 180.0), step,
                step * 1e-6);
  }
}

// The bounds are the box of the viewport's pixel EDGES (half-extents of
// width/height), so they sit half a pixel outside the outermost pixel centres
// — the same convention equal arc has always had.
TEST(ProjMercator, BoundsAndLonRangeCoverTheViewport) {
  fv::MapProjection p = Make(32.78, -79.93);
  const fv::GeoRect r = p.VmapBounds();
  fv::GeoPoint nw, se;
  ASSERT_TRUE(p.SurfaceToGeo(-0.5, -0.5, &nw).ok());
  ASSERT_TRUE(p.SurfaceToGeo(799.5, 599.5, &se).ok());
  EXPECT_NEAR(r.ur.lat, nw.lat, 1e-9);
  EXPECT_NEAR(r.ll.lat, se.lat, 1e-9);
  EXPECT_NEAR(r.ll.lon, nw.lon, 1e-9);
  EXPECT_NEAR(r.ur.lon, se.lon, 1e-9);
  // Which puts them half a pixel outside the outermost pixel CENTRES.
  fv::GeoPoint tl;
  ASSERT_TRUE(p.SurfaceToGeo(0, 0, &tl).ok());
  EXPECT_GT(r.ur.lat, tl.lat);
  EXPECT_LT(r.ll.lon, tl.lon);

  double west = 0, east = 0;
  ASSERT_TRUE(p.VmapLonRange(&west, &east).ok());
  EXPECT_LT(west, p.Center().lon);
  EXPECT_GT(east, p.Center().lon);
  EXPECT_NEAR(west, r.ll.lon, 1e-9);
  EXPECT_NEAR(east, r.ur.lon, 1e-9);

  // A view wider than the world reports more than 360 degrees, which the
  // GeoRect cannot say. Driven by resolution: MapScaleUtil has no scale
  // denominator this coarse.
  fv::MapProjection world;
  ASSERT_TRUE(world.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(world.SetCenter({0.0, 0.0}).ok());
  ASSERT_TRUE(world.SetResolution(0.5, 0.5).ok());
  ASSERT_TRUE(world.SetProjectionType(fv::ProjectionType::kMercator).ok());
  ASSERT_TRUE(world.VmapLonRange(&west, &east).ok());
  EXPECT_GT(east - west, 360.0);
  const fv::GeoRect wr = world.VmapBounds();
  EXPECT_DOUBLE_EQ(wr.ll.lon, -180.0);
  EXPECT_DOUBLE_EQ(wr.ur.lon, 180.0);
}

// Switching to Mercator and back leaves the equal-arc transforms bit-identical:
// the constants are rebuilt, not accumulated.
TEST(ProjMercator, SwitchingBackToEqualArcRestoresItExactly) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(p.SetCenter({32.78, -79.93}).ok());
  ASSERT_TRUE(p.SetScale(2000000.0).ok());
  double bx = 0, by = 0;
  ASSERT_TRUE(p.GeoToSurface({33.5, -78.0}, &bx, &by).ok());

  ASSERT_TRUE(p.SetProjectionType(fv::ProjectionType::kMercator).ok());
  ASSERT_TRUE(p.SetProjectionType(fv::ProjectionType::kEqualArc).ok());
  double ax = 0, ay = 0;
  ASSERT_TRUE(p.GeoToSurface({33.5, -78.0}, &ax, &ay).ok());
  EXPECT_EQ(bx, ax);
  EXPECT_EQ(by, ay);
}

}  // namespace
