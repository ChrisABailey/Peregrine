// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/** @file
 * Azimuthal Equidistant and Orthographic (PJ4): an independent-formulation
 * oracle for each, the round trip, the two things that make these the first
 * projections whose image of the earth does not fill the plane — a rim and a
 * hidden hemisphere — and the bounds when a pole, or the whole sphere, is on
 * the surface.
 *
 * The plan's GEOTRANS oracle is not usable here either: every GEOTRANS
 * projection rejects an inverse flattening outside [250, 350], so it cannot be
 * configured as the sphere the FalconView projectors use (the PJ2 finding).
 * The oracles below go through an explicit 3-D rotation into the frame of the
 * projection centre and, for Azimuthal Equidistant, an explicit arc and
 * azimuth, where the port carries over Windows' closed form with its c/sin(c)
 * scale factor.
 */

#include "fvkit/proj.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

constexpr double kR = 6378137.0;  // WGS84_a_METERS, the projectors' sphere

double Rad(double deg) { return deg * M_PI / 180.0; }

/// The point (`lat`, centre + `dlon`) as a unit vector in the frame of the
/// projection centre: east, north, and up out of the tangent plane. This is
/// the rotation the two projections share, written as a rotation.
void CenterFrame(double center_lat, double lat, double dlon, double* east,
                 double* north, double* up) {
  const double v[3] = {std::cos(Rad(lat)) * std::cos(Rad(dlon)),
                       std::cos(Rad(lat)) * std::sin(Rad(dlon)),
                       std::sin(Rad(lat))};
  const double c0 = std::cos(Rad(center_lat)), s0 = std::sin(Rad(center_lat));
  *up = v[0] * c0 + v[2] * s0;
  *east = v[1];
  *north = -v[0] * s0 + v[2] * c0;
}

/// Azimuthal Equidistant plane metres about the centre, from the arc and the
/// azimuth rather than from Windows' c/sin(c) scale factor. Plane y is north.
void AzEqOracle(double center_lat, double lat, double dlon, double* x_m,
                double* y_m) {
  double e = 0, n = 0, u = 0;
  CenterFrame(center_lat, lat, dlon, &e, &n, &u);
  const double c = std::atan2(std::hypot(e, n), u);  // arc from the centre
  const double az = std::atan2(e, n);
  *x_m = kR * c * std::sin(az);
  *y_m = kR * c * std::cos(az);
}

/// Orthographic plane metres about the centre: the perpendicular projection of
/// the point onto the tangent plane at the centre.
void OrthoOracle(double center_lat, double lat, double dlon, double* x_m,
                 double* y_m) {
  double e = 0, n = 0, u = 0;
  CenterFrame(center_lat, lat, dlon, &e, &n, &u);
  *x_m = kR * e;
  *y_m = kR * n;
}

/// A projection ready to use, with dpp set directly so the tests can predict
/// the disc radius in pixels.
fv::MapProjection Make(fv::ProjectionType type, double lat, double lon,
                       double dpp = 0.01, int w = 800, int h = 600) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter({lat, lon}).ok());
  EXPECT_TRUE(p.SetResolution(dpp, dpp).ok());
  EXPECT_TRUE(p.SetProjectionType(type).ok());
  EXPECT_TRUE(p.Ready());
  return p;
}

/// Great-circle metres between two points on the projectors' sphere.
double SphereMeters(fv::GeoPoint a, fv::GeoPoint b) {
  const double dlat = Rad(b.lat - a.lat), dlon = Rad(b.lon - a.lon);
  const double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
                   std::cos(Rad(a.lat)) * std::cos(Rad(b.lat)) *
                       std::sin(dlon / 2) * std::sin(dlon / 2);
  return 2 * kR * std::asin(std::min(1.0, std::sqrt(h)));
}

/// Surface distance from the surface centre, in pixels.
double PixelsFromCentre(const fv::MapProjection& p, double sx, double sy) {
  return std::hypot(sx - (p.SurfaceSize().width - 1) / 2.0,
                    sy - (p.SurfaceSize().height - 1) / 2.0);
}

/// Asserts that the plane point (`x_m`, `y_m`, north up) and the surface
/// offset (`dx`, `dy`, south down) are the same ray from the centre, at the
/// one scale factor `ratio` — which the first sample sets. Points within a
/// metre of the centre pin nothing and are skipped.
void ExpectSameRay(double x_m, double y_m, double dx, double dy,
                   double* ratio) {
  const double plane = std::hypot(x_m, y_m);
  const double pixels = std::hypot(dx, dy);
  if (plane < 1.0) return;
  ASSERT_GT(pixels, 0);
  if (*ratio == 0) *ratio = plane / pixels;
  EXPECT_NEAR(plane / pixels, *ratio, *ratio * 1e-9);
  // Cross product of (x_m, -y_m) with (dx, dy): zero when they are parallel.
  EXPECT_NEAR((x_m * dy + y_m * dx) / (plane * pixels), 0.0, 1e-9);
}

TEST(ProjAzimuthal, BothTypesAreAcceptedAndNeitherIsAffine) {
  for (auto t : {fv::ProjectionType::kAzimuthalEquidistant,
                 fv::ProjectionType::kOrthographic}) {
    fv::MapProjection p = Make(t, 35.0, -80.0);
    EXPECT_EQ(p.Type(), t);
    EXPECT_FALSE(p.IsAffine());
  }
}

// Both ports report surface pixels, not plane metres, so each oracle is
// checked up to one scale factor: the ratio of plane metres to pixels must be
// the same constant on both axes at every sample.
TEST(ProjAzimuthal, AzEqOracleAgreesUpToOneScaleFactor) {
  for (double clat : {0.0, 33.75, -52.0, 90.0}) {
    fv::MapProjection p =
        Make(fv::ProjectionType::kAzimuthalEquidistant, clat, 25.0, 0.02);
    double ratio = 0;
    for (double lat : {-80.0, -20.0, 5.0, 44.0, 85.0}) {
      for (double dlon : {-150.0, -40.0, -0.5, 0.0, 12.0, 95.0}) {
        double sx = 0, sy = 0;
        ASSERT_TRUE(p.GeoToSurface({lat, fv::NormalizeLon(25.0 + dlon)}, &sx,
                                   &sy)
                        .ok());
        double ox = 0, oy = 0;
        AzEqOracle(clat, lat, dlon, &ox, &oy);
        ExpectSameRay(ox, oy, sx - (p.SurfaceSize().width - 1) / 2.0,
                      sy - (p.SurfaceSize().height - 1) / 2.0, &ratio);
      }
    }
    EXPECT_GT(ratio, 0);
  }
}

TEST(ProjAzimuthal, OrthoOracleAgreesUpToOneScaleFactor) {
  for (double clat : {0.0, 33.75, -52.0}) {
    fv::MapProjection p =
        Make(fv::ProjectionType::kOrthographic, clat, 25.0, 0.02);
    double ratio = 0;
    for (double lat : {-60.0, -20.0, 5.0, 44.0, 70.0}) {
      for (double dlon : {-70.0, -40.0, -0.5, 0.0, 12.0, 65.0}) {
        double sx = 0, sy = 0;
        const fv::Status st =
            p.GeoToSurface({lat, fv::NormalizeLon(25.0 + dlon)}, &sx, &sy);
        if (st.code == fv::kNotProjectable) continue;  // the far hemisphere
        ASSERT_TRUE(st.ok());
        double ox = 0, oy = 0;
        OrthoOracle(clat, lat, dlon, &ox, &oy);
        ExpectSameRay(ox, oy, sx - (p.SurfaceSize().width - 1) / 2.0,
                      sy - (p.SurfaceSize().height - 1) / 2.0, &ratio);
      }
    }
    EXPECT_GT(ratio, 0);
  }
}

TEST(ProjAzimuthal, RoundTripsAtEveryRotation) {
  for (auto t : {fv::ProjectionType::kAzimuthalEquidistant,
                 fv::ProjectionType::kOrthographic}) {
    for (double clat : {0.0, 45.0, -66.5, 90.0}) {
      fv::MapProjection p = Make(t, clat, -100.0, 0.02);
      for (double rot : {0.0, 30.0, 90.0, 217.5}) {
        ASSERT_TRUE(p.SetRotation(rot).ok());
        for (int sy = 0; sy < 600; sy += 97) {
          for (int sx = 0; sx < 800; sx += 89) {
            fv::GeoPoint g;
            if (!p.SurfaceToGeo(sx, sy, &g).ok()) continue;
            double bx = 0, by = 0;
            ASSERT_TRUE(p.GeoToSurface(g, &bx, &by).ok())
                << static_cast<int>(t) << " " << clat << " " << rot << " "
                << sx << "," << sy;
            EXPECT_NEAR(bx, sx, 1e-6);
            EXPECT_NEAR(by, sy, 1e-6);
          }
        }
      }
    }
  }
}

// The property the projection is named for: a pixel distance from the centre
// is proportional to the great-circle arc, in every direction and all the way
// out to the rim.
TEST(ProjAzimuthal, AzEqKeepsDistancesFromTheCentre) {
  const fv::GeoPoint centre{20.0, -5.0};
  fv::MapProjection p = Make(fv::ProjectionType::kAzimuthalEquidistant,
                             centre.lat, centre.lon, 0.45, 900, 900);
  double px_per_m = 0;
  for (double lat = -85.0; lat <= 85.0; lat += 17.0) {
    for (double dlon = -170.0; dlon <= 170.0; dlon += 37.0) {
      const fv::GeoPoint g{lat, fv::NormalizeLon(centre.lon + dlon)};
      double sx = 0, sy = 0;
      ASSERT_TRUE(p.GeoToSurface(g, &sx, &sy).ok());
      const double meters = SphereMeters(centre, g);
      if (meters < 1.0) continue;
      const double ratio = PixelsFromCentre(p, sx, sy) / meters;
      if (px_per_m == 0) px_per_m = ratio;
      EXPECT_NEAR(ratio, px_per_m, px_per_m * 1e-9);
    }
  }
  EXPECT_GT(px_per_m, 0);
}

// The whole sphere fits inside the disc of radius pi * R, and the rim is the
// antipode. Windows keeps projecting past it and draws the earth a second
// time; the port stops (D7).
TEST(ProjAzimuthal, AzEqStopsAtTheRim) {
  fv::MapProjection p = Make(fv::ProjectionType::kAzimuthalEquidistant, 20.0,
                             -5.0, 0.45, 900, 900);
  // The disc radius, in pixels, from the one scale the projection has.
  double sx = 0, sy = 0;
  ASSERT_TRUE(p.GeoToSurface({20.0 + 1.0, -5.0}, &sx, &sy).ok());
  const double px_per_deg = PixelsFromCentre(p, sx, sy);
  const double rim_px = px_per_deg * 180.0;
  ASSERT_LT(rim_px, 450.0) << "the whole disc must fit on the surface";

  fv::GeoPoint g;
  EXPECT_TRUE(p.SurfaceToGeo(449.5 + rim_px * 0.98, 449.5, &g).ok());
  EXPECT_EQ(p.SurfaceToGeo(449.5 + rim_px * 1.02, 449.5, &g).code,
            fv::kNotProjectable);
  // The antipode itself has no single image: the whole rim is that one point.
  EXPECT_EQ(p.GeoToSurface({-20.0, 175.0}, &sx, &sy).code,
            fv::kNotProjectable);
}

// Orthographic sees one hemisphere. A point behind the globe and a pixel off
// the disc are both kNotProjectable, and the disc edge is where the far side
// starts.
TEST(ProjAzimuthal, OrthoHidesTheFarHemisphere) {
  fv::MapProjection p =
      Make(fv::ProjectionType::kOrthographic, 0.0, 0.0, 0.45, 900, 900);
  double sx = 0, sy = 0;
  ASSERT_TRUE(p.GeoToSurface({0.0, 89.0}, &sx, &sy).ok());
  const double disc_px = PixelsFromCentre(p, sx, sy) / std::sin(Rad(89.0));
  EXPECT_EQ(p.GeoToSurface({0.0, 91.0}, &sx, &sy).code, fv::kNotProjectable);
  EXPECT_EQ(p.GeoToSurface({0.0, 180.0}, &sx, &sy).code, fv::kNotProjectable);

  fv::GeoPoint g;
  EXPECT_TRUE(p.SurfaceToGeo(449.5 + disc_px * 0.98, 449.5, &g).ok());
  EXPECT_EQ(p.SurfaceToGeo(449.5 + disc_px * 1.02, 449.5, &g).code,
            fv::kNotProjectable);
  // Every pixel outside the disc, on every diagonal.
  int outside = 0;
  for (int y = 0; y < 900; y += 7) {
    for (int x = 0; x < 900; x += 7) {
      const bool on_disc = PixelsFromCentre(p, x, y) < disc_px;
      const bool ok = p.SurfaceToGeo(x, y, &g).ok();
      if (!on_disc) ++outside;
      EXPECT_EQ(ok, on_disc) << x << "," << y;
    }
  }
  EXPECT_GT(outside, 0);
}

// A pole on the surface is reached from every meridian, so the bounds open to
// the full circle of longitude and the pole is the latitude bound on its side.
TEST(ProjAzimuthal, APoleOnTheSurfaceOpensTheWholeCircle) {
  fv::MapProjection p = Make(fv::ProjectionType::kAzimuthalEquidistant, 90.0,
                             0.0, 0.05, 800, 600);
  bool north = false, south = false;
  ASSERT_TRUE(p.PoleOnSurface(&north, &south).ok());
  EXPECT_TRUE(north);
  EXPECT_FALSE(south);
  const fv::GeoRect r = p.VmapBounds();
  EXPECT_DOUBLE_EQ(r.ll.lon, -180.0);
  EXPECT_DOUBLE_EQ(r.ur.lon, 180.0);
  EXPECT_DOUBLE_EQ(r.ur.lat, 90.0);
  EXPECT_LT(r.ll.lat, 90.0);
  double west = 0, east = 0;
  ASSERT_TRUE(p.VmapLonRange(&west, &east).ok());
  EXPECT_DOUBLE_EQ(east - west, 360.0);
}

// The same question for the projections that came before: only Equal Arc can
// run past a pole, and only by being wider than the world.
TEST(ProjAzimuthal, PoleOnSurfaceAnswersForEveryProjection) {
  bool north = false, south = false;
  fv::MapProjection wide = Make(fv::ProjectionType::kEqualArc, 80.0, 0.0, 0.1);
  ASSERT_TRUE(wide.PoleOnSurface(&north, &south).ok());
  EXPECT_TRUE(north);   // 300 pixels of 0.1 degrees reaches past 90
  EXPECT_FALSE(south);
  fv::MapProjection tight =
      Make(fv::ProjectionType::kEqualArc, 40.0, 0.0, 0.001);
  ASSERT_TRUE(tight.PoleOnSurface(&north, &south).ok());
  EXPECT_FALSE(north);
  EXPECT_FALSE(south);
  fv::MapProjection merc = Make(fv::ProjectionType::kMercator, 70.0, 0.0, 0.1);
  ASSERT_TRUE(merc.PoleOnSurface(&north, &south).ok());
  EXPECT_FALSE(north);  // Mercator stops at kMercatorMaxLat
  EXPECT_FALSE(south);
}

// A world Orthographic: the limb is inside the surface, both poles are on it,
// and the bounds are the hemisphere the projection shows rather than the box
// of the four corners, every one of which is off the earth.
TEST(ProjAzimuthal, OrthoBoundsFollowTheLimbNotTheCorners) {
  fv::MapProjection p =
      Make(fv::ProjectionType::kOrthographic, 0.0, 10.0, 0.45, 900, 900);
  fv::GeoPoint corner;
  EXPECT_EQ(p.SurfaceToGeo(0, 0, &corner).code, fv::kNotProjectable);
  bool north = false, south = false;
  ASSERT_TRUE(p.PoleOnSurface(&north, &south).ok());
  EXPECT_TRUE(north);
  EXPECT_TRUE(south);
  const fv::GeoRect r = p.VmapBounds();
  EXPECT_DOUBLE_EQ(r.ll.lat, -90.0);
  EXPECT_DOUBLE_EQ(r.ur.lat, 90.0);
  EXPECT_DOUBLE_EQ(r.ll.lon, -180.0);
  EXPECT_DOUBLE_EQ(r.ur.lon, 180.0);
}

// A view small enough to stay on the near side keeps ordinary bounds, and they
// contain every point the surface actually shows.
TEST(ProjAzimuthal, BoundsContainTheSurfaceWhenTheEarthDoesNotRunOut) {
  for (auto t : {fv::ProjectionType::kAzimuthalEquidistant,
                 fv::ProjectionType::kOrthographic}) {
    fv::MapProjection p = Make(t, 35.0, -80.0, 0.01);
    const fv::GeoRect r = p.VmapBounds();
    EXPECT_LT(r.ll.lon, r.ur.lon);
    EXPECT_LT(r.ll.lat, r.ur.lat);
    for (int sy = 0; sy < 600; sy += 23) {
      for (int sx = 0; sx < 800; sx += 31) {
        fv::GeoPoint g;
        ASSERT_TRUE(p.SurfaceToGeo(sx, sy, &g).ok());
        EXPECT_GE(g.lat, r.ll.lat - 1e-9);
        EXPECT_LE(g.lat, r.ur.lat + 1e-9);
        EXPECT_GE(g.lon, r.ll.lon - 1e-9);
        EXPECT_LE(g.lon, r.ur.lon + 1e-9);
      }
    }
  }
}

// Both azimuthal transforms are periodic in longitude — every term goes
// through sin or cos of the difference — so the unwrapped forward is the
// wrapped one. That is the right answer, not a missing case: the earth
// appears once in an azimuthal projection, so the engine's world path has
// nothing to draw a second copy of.
TEST(ProjAzimuthal, TheUnwrappedForwardIsTheSameMap) {
  for (auto t : {fv::ProjectionType::kAzimuthalEquidistant,
                 fv::ProjectionType::kOrthographic}) {
    fv::MapProjection p = Make(t, 10.0, 0.0, 0.45, 900, 900);
    for (double lon : {-160.0, -20.0, 60.0}) {
      double wx = 0, wy = 0, rx = 0, ry = 0;
      const fv::Status w = p.GeoToSurface({10.0, lon}, &wx, &wy);
      const fv::Status r =
          p.GeoToSurfaceUnwrapped({10.0, lon + 360.0}, &rx, &ry);
      EXPECT_EQ(w.code, r.code);
      if (!w.ok()) continue;
      EXPECT_NEAR(rx, wx, 1e-9);
      EXPECT_NEAR(ry, wy, 1e-9);
    }
  }
}

// Neither projection is conformal, so LocalScaleAt reports two different
// numbers; both are checked against the pixel the surface actually draws.
TEST(ProjAzimuthal, LocalScaleMatchesTheSurfaceItDescribes) {
  for (auto t : {fv::ProjectionType::kAzimuthalEquidistant,
                 fv::ProjectionType::kOrthographic}) {
    fv::MapProjection p = Make(t, 15.0, 30.0, 0.2, 900, 900);
    for (double dx : {0.0, 80.0, -120.0}) {
      for (double dy : {0.0, -60.0, 90.0}) {
        fv::GeoPoint g;
        ASSERT_TRUE(p.SurfaceToGeo(449.5 + dx, 449.5 + dy, &g).ok());
        fv::MapProjection::LocalScale s;
        ASSERT_TRUE(p.LocalScaleAt(g, &s).ok());
        fv::GeoPoint left, right, up, down;
        ASSERT_TRUE(p.SurfaceToGeo(449.0 + dx, 449.5 + dy, &left).ok());
        ASSERT_TRUE(p.SurfaceToGeo(450.0 + dx, 449.5 + dy, &right).ok());
        ASSERT_TRUE(p.SurfaceToGeo(449.5 + dx, 449.0 + dy, &up).ok());
        ASSERT_TRUE(p.SurfaceToGeo(449.5 + dx, 450.0 + dy, &down).ok());
        EXPECT_NEAR(s.m_per_px_x, SphereMeters(left, right),
                    s.m_per_px_x * 1e-3);
        EXPECT_NEAR(s.m_per_px_y, SphereMeters(up, down), s.m_per_px_y * 1e-3);
      }
    }
  }
}

// The scale is anisotropic in the way each projection's geometry says: an
// Azimuthal Equidistant pixel keeps its radial size and shrinks across, an
// Orthographic pixel keeps its size across and stretches radially.
TEST(ProjAzimuthal, TheTwoPrincipalScalesGoOppositeWays) {
  const fv::GeoPoint centre{0.0, 0.0};
  fv::MapProjection az = Make(fv::ProjectionType::kAzimuthalEquidistant,
                              centre.lat, centre.lon, 0.45, 900, 900);
  fv::MapProjection orth = Make(fv::ProjectionType::kOrthographic, centre.lat,
                                centre.lon, 0.45, 900, 900);
  const fv::GeoPoint east{0.0, 60.0};  // due east of the centre: x is radial
  fv::MapProjection::LocalScale a, o, ac, oc;
  ASSERT_TRUE(az.LocalScaleAt(east, &a).ok());
  ASSERT_TRUE(orth.LocalScaleAt(east, &o).ok());
  ASSERT_TRUE(az.LocalScaleAt(centre, &ac).ok());
  ASSERT_TRUE(orth.LocalScaleAt(centre, &oc).ok());
  const double c = Rad(60.0);
  EXPECT_NEAR(a.m_per_px_x, ac.m_per_px_x, ac.m_per_px_x * 1e-9);
  EXPECT_NEAR(a.m_per_px_y, ac.m_per_px_y * std::sin(c) / c,
              ac.m_per_px_y * 1e-9);
  EXPECT_NEAR(o.m_per_px_x, oc.m_per_px_x / std::cos(c), oc.m_per_px_x * 1e-9);
  EXPECT_NEAR(o.m_per_px_y, oc.m_per_px_y, oc.m_per_px_y * 1e-9);
}

// Convergence: the angle a symbol has to turn so its north is the map's north.
// Windows approximates it as delta-lon * sin(lat); the port measures it, which
// a pole-centred Azimuthal Equidistant shows the difference of — there the
// answer is exactly the longitude difference at every latitude.
TEST(ProjAzimuthal, ConvergenceIsTheTurnTrueNorthTakes) {
  fv::MapProjection p = Make(fv::ProjectionType::kAzimuthalEquidistant, 90.0,
                             0.0, 0.05, 800, 600);
  fv::MapProjection::LocalScale s;
  for (double dlon : {-40.0, -7.5, 0.0, 12.0, 65.0}) {
    for (double lat : {75.0, 85.0}) {
      ASSERT_TRUE(p.LocalScaleAt({lat, dlon}, &s).ok());
      EXPECT_NEAR(s.convergence_deg, dlon, 1e-9);
    }
  }
  // And it is the plane bearing of true north, measured on the surface.
  fv::MapProjection q =
      Make(fv::ProjectionType::kOrthographic, 25.0, -60.0, 0.2, 900, 900);
  for (double dlon : {-50.0, 0.0, 35.0}) {
    const fv::GeoPoint g{45.0, fv::NormalizeLon(-60.0 + dlon)};
    ASSERT_TRUE(q.LocalScaleAt(g, &s).ok());
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    ASSERT_TRUE(q.GeoToSurface(g, &x0, &y0).ok());
    ASSERT_TRUE(q.GeoToSurface({g.lat + 1e-4, g.lon}, &x1, &y1).ok());
    const double plane_az =
        std::atan2(x1 - x0, -(y1 - y0)) * 180.0 / M_PI;  // clockwise from up
    EXPECT_NEAR(plane_az, -s.convergence_deg, 1e-4);
  }
}

// The centre is where both projections are exact and where their scale is the
// one MosaicMetersPerPixel derived.
TEST(ProjAzimuthal, TheCentreLandsOnTheSurfaceCentre) {
  for (auto t : {fv::ProjectionType::kAzimuthalEquidistant,
                 fv::ProjectionType::kOrthographic}) {
    for (double clat : {0.0, 0.0000001, -44.0}) {
      fv::MapProjection p = Make(t, clat, 17.5, 0.02);
      double sx = 0, sy = 0;
      ASSERT_TRUE(p.GeoToSurface(p.Center(), &sx, &sy).ok());
      EXPECT_NEAR(sx, (800 - 1) / 2.0, 1e-9);
      EXPECT_NEAR(sy, (600 - 1) / 2.0, 1e-9);
      fv::GeoPoint back;
      ASSERT_TRUE(p.SurfaceToGeo(sx, sy, &back).ok());
      EXPECT_NEAR(back.lat, clat, 1e-9);
      EXPECT_NEAR(back.lon, 17.5, 1e-9);
    }
  }
}

}  // namespace
