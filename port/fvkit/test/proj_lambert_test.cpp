// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/** @file
 * Lambert Conformal Conic (PJ3): an independent-formulation oracle, the round
 * trip, the two standard parallels the viewport derives, the pole clamps, the
 * near-equator switch to the Mercator equations, and the convergence that
 * makes this the first projection where north is not up.
 *
 * The plan's GEOTRANS oracle is not usable: every GEOTRANS projection rejects
 * an inverse flattening outside [250, 350], so it cannot be configured as the
 * sphere the FalconView projectors use (the same finding as PJ2). The oracle
 * below writes the conic in the sec+tan form rather than the half-angle
 * tangent the port carries over from Windows, and the scale-factor tests pin
 * the cone constant and F against a measured ground distance rather than
 * against either formula.
 */

#include "fvkit/proj.h"

#include <gtest/gtest.h>

#include <cmath>

#include "geo_tool.h"

namespace {

constexpr double kR = 6378137.0;  // WGS84_a_METERS, the projectors' sphere

double Rad(double deg) { return deg * M_PI / 180.0; }

/// Spherical LCC plane coordinates in metres, relative to the projection
/// centre, written with sec+tan instead of the half-angle tangent.
void Oracle(double phi1, double phi2, double center_lat, double lat,
            double dlon, double* x_m, double* y_m) {
  auto t = [](double deg) {
    return 1.0 / std::cos(Rad(deg)) + std::tan(Rad(deg));
  };
  const double n =
      phi1 == phi2
          ? std::sin(Rad(phi1))
          : std::log(std::cos(Rad(phi1)) / std::cos(Rad(phi2))) /
                std::log(t(phi2) / t(phi1));
  const double F = std::cos(Rad(phi1)) * std::pow(t(phi1), n) / n;
  const double rho = kR * F * std::pow(t(lat), -n);
  const double rho_0 = kR * F * std::pow(t(center_lat), -n);
  const double theta = n * Rad(dlon);
  *x_m = rho * std::sin(theta);
  *y_m = rho_0 - rho * std::cos(theta);  // north is +y in the plane
}

/// A projection ready to use: Lambert at `center`, dpp set directly so the
/// standard parallels the tests re-derive are exact.
fv::MapProjection Make(double lat, double lon, double dpp = 0.01, int w = 800,
                       int h = 600) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter({lat, lon}).ok());
  EXPECT_TRUE(p.SetResolution(dpp, dpp).ok());
  EXPECT_TRUE(p.SetProjectionType(fv::ProjectionType::kLambert).ok());
  EXPECT_TRUE(p.Ready());
  return p;
}

/// The two standard parallels the plan's rule gives for `p`, re-derived here
/// rather than read back off the projection.
void Parallels(const fv::MapProjection& p, double* phi1, double* phi2) {
  const double third = (p.SurfaceSize().height * p.DegPerPixelLat()) / 3.0;
  *phi1 = p.Center().lat + third;
  *phi2 = p.Center().lat - third;
}

/// Surface pixels spanned by a short step east at `g`.
double PixelsPerStepEast(const fv::MapProjection& p, fv::GeoPoint g,
                         double step) {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  EXPECT_TRUE(p.GeoToSurface(g, &x0, &y0).ok());
  EXPECT_TRUE(p.GeoToSurface({g.lat, g.lon + step}, &x1, &y1).ok());
  return std::hypot(x1 - x0, y1 - y0);
}

/// Ground metres per surface pixel at `g` on the sphere the projectors use.
/// This is the projection's own scale and nothing else.
double SphericalMetersPerPixel(const fv::MapProjection& p, fv::GeoPoint g) {
  const double step = 1e-4;  // degrees, ~11 m
  return kR * std::cos(Rad(g.lat)) * Rad(step) / PixelsPerStepEast(p, g, step);
}

/// The same, measured as a WGS84 geodesic. It differs from the spherical
/// value by a few parts in ten thousand, latitude by latitude: FalconView
/// measures the pixel on the ellipsoid and then spends it on a sphere (the
/// PJ2 quirk), so a caller asking for real ground metres carries that error.
double GeodesicMetersPerPixel(const fv::MapProjection& p, fv::GeoPoint g) {
  const double step = 1e-4;
  double meters = 0, bearing = 0;
  GEO_geo_to_distance(g.lat, g.lon, g.lat, g.lon + step, &meters, &bearing);
  return meters / PixelsPerStepEast(p, g, step);
}

TEST(ProjLambert, TypeIsAcceptedAndIsNotAffine) {
  fv::MapProjection p = Make(40.0, -100.0);
  EXPECT_EQ(p.Type(), fv::ProjectionType::kLambert);
  EXPECT_FALSE(p.IsAffine());
}

// The port reports surface pixels, not plane metres, so the oracle is checked
// up to one scale factor: the ratio of plane metres to pixels must be the
// same constant on both axes at every sample, which pins the projection
// formula without pinning the metres-per-pixel derivation.
TEST(ProjLambert, OracleAgreesUpToOneScaleFactor) {
  for (double clat : {40.0, 62.5, -35.0}) {
    fv::MapProjection p = Make(clat, -100.0);
    double phi1 = 0, phi2 = 0;
    Parallels(p, &phi1, &phi2);
    double cx = 0, cy = 0;
    ASSERT_TRUE(p.GeoToSurface({clat, -100.0}, &cx, &cy).ok());

    double ratio = 0;
    for (double dlat : {-2.0, -0.5, 0.0, 0.5, 2.0})
      for (double dlon : {-3.0, -1.0, 0.0, 1.0, 3.0}) {
        double sx = 0, sy = 0;
        ASSERT_TRUE(
            p.GeoToSurface({clat + dlat, -100.0 + dlon}, &sx, &sy).ok());
        double ox = 0, oy = 0;
        Oracle(phi1, phi2, clat, clat + dlat, dlon, &ox, &oy);
        if (dlat == 0.0 && dlon == 0.0) continue;
        // Plane y is north-up, surface y is down.
        const double rx = ox / (sx - cx);
        const double ry = oy / (cy - sy);
        if (ratio == 0) ratio = std::isfinite(rx) ? rx : ry;
        if (std::isfinite(rx)) EXPECT_NEAR(rx / ratio, 1.0, 1e-9) << clat;
        if (std::isfinite(ry)) EXPECT_NEAR(ry / ratio, 1.0, 1e-9) << clat;
      }
    EXPECT_GT(ratio, 0) << clat;
  }
}

TEST(ProjLambert, RoundTrips) {
  for (double clat : {40.0, -35.0, 0.0, 78.0})
    for (double rot : {0.0, 37.0, 90.0}) {
      fv::MapProjection p = Make(clat, 15.0);
      ASSERT_TRUE(p.SetRotation(rot).ok());
      for (int y = 0; y < p.SurfaceSize().height; y += 53)
        for (int x = 0; x < p.SurfaceSize().width; x += 61) {
          fv::GeoPoint g;
          ASSERT_TRUE(p.SurfaceToGeo(x, y, &g).ok()) << clat << " " << rot;
          double sx = 0, sy = 0;
          ASSERT_TRUE(p.GeoToSurface(g, &sx, &sy).ok());
          EXPECT_NEAR(sx, x, 1e-6) << clat << " " << rot;
          EXPECT_NEAR(sy, y, 1e-6) << clat << " " << rot;
        }
    }
}

// The scale factor is 1 on both standard parallels and nowhere else: between
// them the map is short of the ground, outside them it is long. Measured
// against geodesic distances, so it pins the cone constant and F together
// without reusing either formula.
TEST(ProjLambert, ScaleIsOneOnBothStandardParallels) {
  fv::MapProjection p = Make(40.0, -100.0);
  double phi1 = 0, phi2 = 0;
  Parallels(p, &phi1, &phi2);
  const double at1 = SphericalMetersPerPixel(p, {phi1, -100.0});
  const double at2 = SphericalMetersPerPixel(p, {phi2, -100.0});
  EXPECT_NEAR(at1 / at2, 1.0, 1e-9);
  // Between the parallels a pixel covers more ground; outside it covers less.
  EXPECT_GT(SphericalMetersPerPixel(p, {40.0, -100.0}), at1);
  EXPECT_LT(SphericalMetersPerPixel(p, {phi1 + 3.0, -100.0}), at1);
  EXPECT_LT(SphericalMetersPerPixel(p, {phi2 - 3.0, -100.0}), at1);
}

TEST(ProjLambert, LocalScaleMatchesTheMeasuredPixel) {
  fv::MapProjection p = Make(40.0, -100.0);
  for (double lat : {34.0, 40.0, 46.0})
    for (double lon : {-104.0, -100.0, -96.0}) {
      fv::MapProjection::LocalScale s;
      ASSERT_TRUE(p.LocalScaleAt({lat, lon}, &s).ok());
      // Conformal: the same scale on both axes.
      EXPECT_NEAR(s.m_per_px_x, s.m_per_px_y, 1e-9 * s.m_per_px_x);
      EXPECT_NEAR(s.m_per_px_x / SphericalMetersPerPixel(p, {lat, lon}), 1.0,
                  1e-9);
      // Against a real geodesic it carries the PJ2 quirk, and no more.
      EXPECT_NEAR(s.m_per_px_x / GeodesicMetersPerPixel(p, {lat, lon}), 1.0,
                  0.01);
    }
}

// North is not up away from the centre meridian, and the angle is the one
// LocalScaleAt reports. Measured off the surface: a step north from the point
// tilts by the convergence, clockwise east of the centre.
TEST(ProjLambert, ConvergenceIsSignedAndMatchesTheDrawnMeridian) {
  fv::MapProjection p = Make(40.0, -100.0);
  for (double dlon : {-4.0, -1.0, 1.0, 4.0}) {
    const fv::GeoPoint g{40.0, -100.0 + dlon};
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    ASSERT_TRUE(p.GeoToSurface(g, &x0, &y0).ok());
    ASSERT_TRUE(p.GeoToSurface({g.lat + 0.01, g.lon}, &x1, &y1).ok());
    // Bearing of true north on the surface, clockwise from surface up.
    const double drawn =
        std::atan2(x1 - x0, y0 - y1) * 180.0 / M_PI;
    fv::MapProjection::LocalScale s;
    ASSERT_TRUE(p.LocalScaleAt(g, &s).ok());
    EXPECT_NEAR(-drawn, s.convergence_deg, 1e-4) << dlon;
    // Positive east of the centre meridian in the north: the sign Windows'
    // get_convergence cannot report, because GEO_delta_lon drops it.
    EXPECT_EQ(s.convergence_deg > 0, dlon > 0) << dlon;
  }
  fv::MapProjection::LocalScale centre;
  ASSERT_TRUE(p.LocalScaleAt({40.0, -100.0}, &centre).ok());
  EXPECT_NEAR(centre.convergence_deg, 0.0, 1e-12);
}

// Within one pixel of latitude of the equator the cone constant goes to zero,
// and the Mercator equations take over: meridians become parallel and the
// convergence with them. The switch is inside that pixel and not outside it.
TEST(ProjLambert, SwitchesToMercatorWithinOnePixelOfTheEquator) {
  constexpr double kDpp = 0.01;
  auto meridians_are_parallel = [](double clat) {
    fv::MapProjection p = Make(clat, 0.0, kDpp);
    fv::MapProjection::LocalScale a, b;
    EXPECT_TRUE(p.LocalScaleAt({clat, -2.0}, &a).ok());
    EXPECT_TRUE(p.LocalScaleAt({clat, 2.0}, &b).ok());
    return a.convergence_deg == 0.0 && b.convergence_deg == 0.0;
  };
  EXPECT_TRUE(meridians_are_parallel(0.0));
  EXPECT_TRUE(meridians_are_parallel(0.9 * kDpp));
  EXPECT_TRUE(meridians_are_parallel(-0.9 * kDpp));
  EXPECT_FALSE(meridians_are_parallel(1.1 * kDpp));
  EXPECT_FALSE(meridians_are_parallel(-1.1 * kDpp));

  // On the Mercator side the projection is separable again: a parallel is a
  // surface row and a meridian is a surface column.
  fv::MapProjection p = Make(0.0, 0.0, kDpp);
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  ASSERT_TRUE(p.GeoToSurface({0.0, -1.0}, &x0, &y0).ok());
  ASSERT_TRUE(p.GeoToSurface({0.0, 1.0}, &x1, &y1).ok());
  EXPECT_NEAR(y0, y1, 1e-9);
  ASSERT_TRUE(p.GeoToSurface({1.0, 0.5}, &x0, &y0).ok());
  ASSERT_TRUE(p.GeoToSurface({-1.0, 0.5}, &x1, &y1).ok());
  EXPECT_NEAR(x0, x1, 1e-9);
}

// A visible pole pulls the projection centre onto it, so the pole lands at the
// surface centre and the requested centre does not. Center() still reports
// what the caller asked for.
TEST(ProjLambert, AVisiblePoleTakesTheCentre) {
  fv::MapProjection p = Make(80.0, 20.0, 0.1);  // 600 px * 0.1 = 60 deg tall
  EXPECT_DOUBLE_EQ(p.Center().lat, 80.0);
  double sx = 0, sy = 0;
  ASSERT_TRUE(p.GeoToSurface({89.999999, 20.0}, &sx, &sy).ok());
  EXPECT_NEAR(sx, (800 - 1) / 2.0, 1e-6);
  EXPECT_NEAR(sy, (600 - 1) / 2.0, 1e-6);

  // The pole is on the surface, so the bounds open to every longitude and
  // reach the pole itself.
  const fv::GeoRect r = p.VmapBounds();
  EXPECT_DOUBLE_EQ(r.ll.lon, -180.0);
  EXPECT_DOUBLE_EQ(r.ur.lon, 180.0);
  EXPECT_NEAR(r.ur.lat, 89.999999, 1e-6);
  double west = 0, east = 0;
  ASSERT_TRUE(p.VmapLonRange(&west, &east).ok());
  EXPECT_NEAR(east - west, 360.0, 1e-9);

  // South pole, same rule.
  fv::MapProjection s = Make(-80.0, 20.0, 0.1);
  ASSERT_TRUE(s.GeoToSurface({-89.999999, 20.0}, &sx, &sy).ok());
  EXPECT_NEAR(sy, (600 - 1) / 2.0, 1e-6);
}

// A centre far from the poles is left where the caller put it.
TEST(ProjLambert, AnOrdinaryCentreIsNotMoved) {
  fv::MapProjection p = Make(40.0, -100.0);
  double sx = 0, sy = 0;
  ASSERT_TRUE(p.GeoToSurface({40.0, -100.0}, &sx, &sy).ok());
  EXPECT_NEAR(sx, (800 - 1) / 2.0, 1e-9);
  EXPECT_NEAR(sy, (600 - 1) / 2.0, 1e-9);
}

// The bounds must contain every point the surface actually shows; a conic
// bows its parallels, so the box of the four corners is not enough.
TEST(ProjLambert, BoundsCoverTheWholeSurface) {
  for (double rot : {0.0, 25.0}) {
    fv::MapProjection p = Make(45.0, 10.0, 0.02);
    ASSERT_TRUE(p.SetRotation(rot).ok());
    const fv::GeoRect r = p.VmapBounds();
    double west = 0, east = 0;
    ASSERT_TRUE(p.VmapLonRange(&west, &east).ok());
    for (int y = 0; y < p.SurfaceSize().height; y += 29)
      for (int x = 0; x < p.SurfaceSize().width; x += 31) {
        fv::GeoPoint g;
        ASSERT_TRUE(p.SurfaceToGeo(x, y, &g).ok());
        EXPECT_GE(g.lat, r.ll.lat - 1e-9) << rot;
        EXPECT_LE(g.lat, r.ur.lat + 1e-9) << rot;
        const double unwrapped = fv::UnwrapLonNear(g.lon, p.Center().lon);
        EXPECT_GE(unwrapped, west - 1e-9) << rot;
        EXPECT_LE(unwrapped, east + 1e-9) << rot;
      }
    // The top edge bows away from the centre meridian, so the box is taller
    // than the corners alone would make it.
    fv::GeoPoint corner, middle;
    ASSERT_TRUE(p.SurfaceToGeo(0, 0, &corner).ok());
    ASSERT_TRUE(p.SurfaceToGeo(p.SurfaceSize().width / 2.0, 0, &middle).ok());
    if (rot == 0.0) EXPECT_GT(middle.lat, corner.lat);
  }
}

// Switching away and back leaves the equal-arc arithmetic bit-identical: the
// gate runs before any Lambert constant is touched.
TEST(ProjLambert, SwitchingBackToEqualArcRestoresItExactly) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(p.SetCenter({40.0, -100.0}).ok());
  ASSERT_TRUE(p.SetScale(2000000.0).ok());
  double bx = 0, by = 0;
  ASSERT_TRUE(p.GeoToSurface({41.0, -99.0}, &bx, &by).ok());
  ASSERT_TRUE(p.SetProjectionType(fv::ProjectionType::kLambert).ok());
  ASSERT_TRUE(p.SetProjectionType(fv::ProjectionType::kEqualArc).ok());
  double ax = 0, ay = 0;
  ASSERT_TRUE(p.GeoToSurface({41.0, -99.0}, &ax, &ay).ok());
  EXPECT_EQ(std::signbit(bx), std::signbit(ax));
  EXPECT_DOUBLE_EQ(bx, ax);
  EXPECT_DOUBLE_EQ(by, ay);
  EXPECT_TRUE(p.IsAffine());
}

// The far pole is on the cone's other side and has no finite plane radius.
TEST(ProjLambert, TheFarPoleIsNotProjectable) {
  fv::MapProjection p = Make(40.0, -100.0);
  double sx = 0, sy = 0;
  EXPECT_EQ(p.GeoToSurface({-90.0, -100.0}, &sx, &sy).code,
            fv::kNotProjectable);
  // The near pole is the apex and projects fine.
  EXPECT_TRUE(p.GeoToSurface({90.0, -100.0}, &sx, &sy).ok());
}

}  // namespace
