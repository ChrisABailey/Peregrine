// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/** @file
 * Pins MapProjection's Equal Arc transforms to a frozen copy of the
 * equal-arc-only implementation, bit for bit, over a grid of centres, scales,
 * rotations and points. Every later projection phase must keep this green.
 */

#include "fvkit/proj.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

namespace {

/// Frozen equal-arc transforms, reading the projection only through its
/// public getters. Do not edit to follow MapProjection; that is the point.
struct FrozenEqualArc {
  double w, h, clat, clon, dlat, dlon, rot, c, s;

  explicit FrozenEqualArc(const fv::MapProjection& p)
      : w(p.SurfaceSize().width),
        h(p.SurfaceSize().height),
        clat(p.Center().lat),
        clon(p.Center().lon),
        dlat(p.DegPerPixelLat()),
        dlon(p.DegPerPixelLon()),
        rot(p.Rotation()) {
    if (rot == 0.0) { c = 1; s = 0; }
    else if (rot == 90.0) { c = 0; s = 1; }
    else if (rot == 180.0) { c = -1; s = 0; }
    else if (rot == 270.0) { c = 0; s = -1; }
    else {
      const double rad = rot * M_PI / 180.0;
      c = std::cos(rad);
      s = std::sin(rad);
    }
  }

  void GeoToSurface(const fv::GeoPoint& p, double* sx, double* sy) const {
    double d = fv::UnwrapLonNear(p.lon, clon) - clon;
    double dx = d / dlon;
    double dy = (clat - p.lat) / dlat;
    if (rot != 0.0) {
      const double x = dx * c - dy * s;
      const double y = dx * s + dy * c;
      dx = x;
      dy = y;
    }
    *sx = (w - 1) / 2.0 + dx;
    *sy = (h - 1) / 2.0 + dy;
  }

  void SurfaceToGeo(double sx, double sy, fv::GeoPoint* p) const {
    double dx = sx - (w - 1) / 2.0;
    double dy = sy - (h - 1) / 2.0;
    if (rot != 0.0) {
      const double x = dx * c + dy * s;
      const double y = -dx * s + dy * c;
      dx = x;
      dy = y;
    }
    p->lat = clat - dy * dlat;
    p->lon = fv::NormalizeLon(clon + dx * dlon);
  }
};

bool SameBits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

TEST(MapProjectionIdentity, EqualArcIsBitIdenticalToFrozenCopy) {
  const fv::GeoPoint centres[] = {
      {32.78, -79.93}, {0.0, 0.0}, {-45.5, 179.9}, {71.2, -179.95}, {-89.0, 12.0}};
  const double scales[] = {5e6, 250000, 12500};
  const double rotations[] = {0, 17.25, 45, 90, 180, 270, 333.3};
  int compared = 0;
  for (const auto& c : centres) {
    for (double scale : scales) {
      for (double rot : rotations) {
        fv::MapProjection p;
        ASSERT_TRUE(p.SetSurfaceSize(801, 600).ok());
        ASSERT_TRUE(p.SetCenter(c).ok());
        ASSERT_TRUE(p.SetScale(scale).ok());
        ASSERT_TRUE(p.SetRotation(rot).ok());
        ASSERT_TRUE(p.SetProjectionType(fv::ProjectionType::kEqualArc).ok());
        const FrozenEqualArc f(p);
        for (int iy = -2; iy <= 12; ++iy) {
          for (int ix = -2; ix <= 12; ++ix) {
            const double sx = ix * 80.1 + 0.37, sy = iy * 60.3 - 0.21;
            fv::GeoPoint g, gf;
            ASSERT_TRUE(p.SurfaceToGeo(sx, sy, &g).ok());
            f.SurfaceToGeo(sx, sy, &gf);
            ASSERT_TRUE(SameBits(g.lat, gf.lat) && SameBits(g.lon, gf.lon))
                << "SurfaceToGeo at " << sx << "," << sy << " rot " << rot;
            double x, y, xf, yf;
            ASSERT_TRUE(p.GeoToSurface(g, &x, &y).ok());
            f.GeoToSurface(g, &xf, &yf);
            ASSERT_TRUE(SameBits(x, xf) && SameBits(y, yf))
                << "GeoToSurface at " << g.lat << "," << g.lon << " rot " << rot;
            ++compared;
          }
        }
      }
    }
  }
  EXPECT_EQ(compared, 5 * 3 * 7 * 15 * 15);
}

TEST(MapProjectionIdentity, DefaultTypeIsAffineEqualArc) {
  fv::MapProjection p;
  EXPECT_EQ(p.Type(), fv::ProjectionType::kEqualArc);
  EXPECT_TRUE(p.IsAffine());
}

// All five projections are ported, so the only thing left to refuse is a
// value that is not one of them; what matters here is that selecting one and
// coming back leaves Equal Arc bit-identical, since every golden is equal-arc.
TEST(MapProjectionIdentity, EqualArcSurvivesAVisitToEveryOtherProjection) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(400, 300).ok());
  ASSERT_TRUE(p.SetCenter({10, 20}).ok());
  ASSERT_TRUE(p.SetScale(1e6).ok());
  const fv::GeoRect before = p.VmapBounds();
  for (auto t : {fv::ProjectionType::kMercator, fv::ProjectionType::kLambert,
                 fv::ProjectionType::kAzimuthalEquidistant,
                 fv::ProjectionType::kOrthographic}) {
    ASSERT_TRUE(p.SetProjectionType(t).ok());
    EXPECT_FALSE(p.IsAffine());
    ASSERT_TRUE(p.SetProjectionType(fv::ProjectionType::kEqualArc).ok());
    EXPECT_TRUE(p.IsAffine());
    EXPECT_TRUE(p.Ready());
  }
  const fv::GeoRect after = p.VmapBounds();
  EXPECT_TRUE(SameBits(before.ll.lat, after.ll.lat) &&
              SameBits(before.ur.lon, after.ur.lon));
}

TEST(MapProjectionIdentity, NotProjectableIsItsOwnCode) {
  EXPECT_EQ(fv::kNotProjectable, -7);
  EXPECT_NE(fv::kNotProjectable, fv::kOutOfCoverage);
}

}  // namespace
