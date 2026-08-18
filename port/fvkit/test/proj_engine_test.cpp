// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// L3a MapProjection + L3b MapEngine tests. Projection math is checked
// against MapScaleUtil directly (the plan's requirement); engine renders are
// pinned by hash after visual verification, like the canvas goldens.

#include "fvkit/engine.h"
#include "fvkit/proj.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "fv_map_enums.h"
#include "fv_map_scale_util.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/formats/dted.h"
#include "fvkit/formats/registry.h"
#include "fvkit/tools/png_write.h"

namespace fs = std::filesystem;

namespace {

std::string TestDataDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  return d ? d : "";
}

uint64_t Fnv1a(const fv::PixelBuffer& b) {
  uint64_t h = 1469598103934665603ull;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int i = 0; i < b.Width() * 4; ++i) {
      h ^= row[i];
      h *= 1099511628211ull;
    }
  }
  return h;
}

// ---------------------------------------------------------------------------
// MapProjection (always runs)
// ---------------------------------------------------------------------------

TEST(MapProjection, DppMatchesMapScaleUtil) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(p.SetCenter({33.75, -84.39}).ok());
  ASSERT_TRUE(p.SetScale(500000.0).ok());
  ASSERT_TRUE(p.Ready());

  double dlat = 0, dlon = 0;
  fv::MapScaleUtil util;
  ASSERT_EQ(util.GetDegreesPerPixel(33.75, 500000.0, MAP_SCALE_DENOMINATOR,
                                    &dlat, &dlon),
            0);
  EXPECT_DOUBLE_EQ(p.DegPerPixelLat(), dlat);
  EXPECT_DOUBLE_EQ(p.DegPerPixelLon(), dlon);
}

TEST(MapProjection, RoundTripAndBounds) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(801, 601).ok());
  ASSERT_TRUE(p.SetCenter({33.75, -84.39}).ok());
  ASSERT_TRUE(p.SetScale(500000.0).ok());

  // center maps to the surface midpoint
  double sx = 0, sy = 0;
  ASSERT_TRUE(p.GeoToSurface({33.75, -84.39}, &sx, &sy).ok());
  EXPECT_NEAR(sx, 400.0, 1e-9);
  EXPECT_NEAR(sy, 300.0, 1e-9);

  // round-trip an arbitrary surface point
  fv::GeoPoint g;
  ASSERT_TRUE(p.SurfaceToGeo(123.0, 456.0, &g).ok());
  ASSERT_TRUE(p.GeoToSurface(g, &sx, &sy).ok());
  EXPECT_NEAR(sx, 123.0, 1e-9);
  EXPECT_NEAR(sy, 456.0, 1e-9);

  // bounds contain the center and match dpp arithmetic
  fv::GeoRect b = p.VmapBounds();
  EXPECT_TRUE(b.Contains({33.75, -84.39}));
  EXPECT_NEAR(b.ur.lat - b.ll.lat, 601 * p.DegPerPixelLat(), 1e-9);
}

TEST(MapProjection, AntimeridianViewport) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(400, 300).ok());
  ASSERT_TRUE(p.SetCenter({-17.0, 179.9}).ok());  // Fiji-ish
  ASSERT_TRUE(p.SetScale(2000000.0).ok());

  fv::GeoRect b = p.VmapBounds();
  EXPECT_TRUE(b.CrossesAntimeridian());
  // a point just west of the dateline lands left of center; just east lands
  // right of center — through the wrap
  double sx_w, sy_w, sx_e, sy_e;
  ASSERT_TRUE(p.GeoToSurface({-17.0, 179.0}, &sx_w, &sy_w).ok());
  ASSERT_TRUE(p.GeoToSurface({-17.0, -179.5}, &sx_e, &sy_e).ok());
  EXPECT_LT(sx_w, 200.0);
  EXPECT_GT(sx_e, 200.0);
}

TEST(MapProjection, NotConfiguredErrors) {
  fv::MapProjection p;
  double sx, sy;
  EXPECT_EQ(p.GeoToSurface({0, 0}, &sx, &sy).code, fv::kInvalidArg);
  EXPECT_EQ(p.SetScale(-5).code, fv::kInvalidArg);
  EXPECT_EQ(p.SetSurfaceSize(0, 10).code, fv::kInvalidArg);
}

// ---------------------------------------------------------------------------
// Physical-display scale (the native-scale / correct-aspect path)
// ---------------------------------------------------------------------------

// The user's worked example: 1:1,000,000 on a 0.25 mm/px screen puts ~10 km
// of ground under 1 cm (40 px) of screen at the center.
TEST(MapProjection, PhysicalScalePutsNativeGroundUnderTheRuler) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(p.SetCenter({33.75, -84.39}).ok());
  ASSERT_TRUE(p.SetPhysicalScale(1000000.0, 0.25).ok());
  ASSERT_TRUE(p.Ready());
  EXPECT_DOUBLE_EQ(p.Scale(), 1000000.0);
  EXPECT_DOUBLE_EQ(p.MmPerPixel(), 0.25);

  // 40 px = 1 cm of screen. Measure the ground span north-south through the
  // center via surface_to_geo, then convert degrees of latitude to metres
  // (~111.32 km/deg). Expect ~10 km, generously toleranced for the ellipsoid.
  fv::GeoPoint a, b;
  ASSERT_TRUE(p.SurfaceToGeo(400, 300 - 20, &a).ok());
  ASSERT_TRUE(p.SurfaceToGeo(400, 300 + 20, &b).ok());
  const double ground_m = std::fabs(a.lat - b.lat) * 111320.0;
  EXPECT_NEAR(ground_m, 10000.0, 400.0);
}

// Correct aspect: one screen pixel must cover the SAME ground distance
// vertically and horizontally, at a latitude where dpp_lat != dpp_lon.
// This is the crux of the user's "aspect ratio is off" report: the ratio
// dpp_lon/dpp_lat must be ~1/cos(lat) (a degree of longitude is physically
// shorter away from the equator), NOT the ~2.1 that MapScaleUtil's Vincenty
// path produced.
TEST(MapProjection, PhysicalScaleHasSquareGroundPixels) {
  const double lat = 45.0;
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(512, 512).ok());
  ASSERT_TRUE(p.SetCenter({lat, 10.0}).ok());
  ASSERT_TRUE(p.SetPhysicalScale(2000000.0, 0.25).ok());

  // Convert each pixel's degrees to ground metres via the same WGS84 series
  // the projection uses; the two must be within a fraction of a percent.
  const double m_per_deg_lat = 111132.92 - 559.82 * std::cos(2 * lat * M_PI / 180);
  const double m_per_deg_lon = 111412.84 * std::cos(lat * M_PI / 180) -
                               93.5 * std::cos(3 * lat * M_PI / 180);
  const double ground_lat = p.DegPerPixelLat() * m_per_deg_lat;
  const double ground_lon = p.DegPerPixelLon() * m_per_deg_lon;
  // Square to ~1e-5 (the test drops one high-order term the projection keeps).
  EXPECT_NEAR(ground_lon, ground_lat, ground_lat * 1e-4);

  // Aspect ratio ~ 1/cos(45) = 1.414, and firmly NOT the ~1.99 the old
  // Vincenty path returned here.
  const double ratio = p.DegPerPixelLon() / p.DegPerPixelLat();
  EXPECT_NEAR(ratio, 1.0 / std::cos(lat * M_PI / 180), 0.01);
  EXPECT_LT(ratio, 1.6);
}

// mm_per_pixel is the zoom knob: doubling it doubles ground per pixel.
TEST(MapProjection, MmPerPixelZooms) {
  fv::MapProjection near_, far_;
  for (fv::MapProjection* p : {&near_, &far_}) {
    ASSERT_TRUE(p->SetSurfaceSize(256, 256).ok());
    ASSERT_TRUE(p->SetCenter({0.0, 0.0}).ok());
  }
  ASSERT_TRUE(near_.SetPhysicalScale(1000000.0, 0.25).ok());
  ASSERT_TRUE(far_.SetPhysicalScale(1000000.0, 0.50).ok());
  EXPECT_NEAR(far_.DegPerPixelLat(), 2.0 * near_.DegPerPixelLat(), 1e-12);
}

TEST(MapProjection, PhysicalScaleRejectsBadArgs) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(64, 64).ok());
  ASSERT_TRUE(p.SetCenter({0, 0}).ok());
  EXPECT_EQ(p.SetPhysicalScale(-1.0, 0.25).code, fv::kInvalidArg);
  EXPECT_EQ(p.SetPhysicalScale(1000000.0, 0.0).code, fv::kInvalidArg);
}

// ---------------------------------------------------------------------------
// Rotation (PR1) — a turned chart is the projection's job
// ---------------------------------------------------------------------------

namespace {

// A configured Charleston-ish projection with an ODD, non-square surface, so
// the two half-extents differ and a swapped axis cannot hide.
fv::MapProjection MakeRotationFixture() {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(801, 601).ok());
  EXPECT_TRUE(p.SetCenter({32.78, -79.93}).ok());
  EXPECT_TRUE(p.SetScale(500000.0).ok());
  EXPECT_TRUE(p.Ready());
  return p;
}

}  // namespace

// THE acceptance test for this slice. Every pinned golden in the tree was
// rendered at rotation 0, so the unrotated path must be byte-identical and
// not merely close: EXPECT_DOUBLE_EQ would let a 1-ulp drift through, so
// these are exact EXPECT_EQ on doubles.
TEST(MapProjectionRotation, ZeroIsTheExactIdentity) {
  fv::MapProjection ref = MakeRotationFixture();  // never told about rotation
  fv::MapProjection rot = MakeRotationFixture();
  ASSERT_TRUE(rot.SetRotation(0.0).ok());
  EXPECT_EQ(rot.Rotation(), 0.0);

  for (double lat = 30.0; lat <= 35.0; lat += 0.37) {
    for (double lon = -82.0; lon <= -78.0; lon += 0.29) {
      double ax = 0, ay = 0, bx = 0, by = 0;
      ASSERT_TRUE(ref.GeoToSurface({lat, lon}, &ax, &ay).ok());
      ASSERT_TRUE(rot.GeoToSurface({lat, lon}, &bx, &by).ok());
      EXPECT_EQ(ax, bx) << lat << "," << lon;
      EXPECT_EQ(ay, by) << lat << "," << lon;
    }
  }
  for (double sx = -50.0; sx <= 850.0; sx += 71.0) {
    for (double sy = -50.0; sy <= 650.0; sy += 53.0) {
      fv::GeoPoint a, b;
      ASSERT_TRUE(ref.SurfaceToGeo(sx, sy, &a).ok());
      ASSERT_TRUE(rot.SurfaceToGeo(sx, sy, &b).ok());
      EXPECT_EQ(a.lat, b.lat) << sx << "," << sy;
      EXPECT_EQ(a.lon, b.lon) << sx << "," << sy;
    }
  }
  const fv::GeoRect a = ref.VmapBounds(), b = rot.VmapBounds();
  EXPECT_EQ(a.ll.lat, b.ll.lat);
  EXPECT_EQ(a.ll.lon, b.ll.lon);
  EXPECT_EQ(a.ur.lat, b.ur.lat);
  EXPECT_EQ(a.ur.lon, b.ur.lon);
}

// ...and a chart that has been turned and turned BACK is the identity too,
// which is the case a shell actually produces (track-up on, then off).
TEST(MapProjectionRotation, ReturningToZeroIsTheExactIdentity) {
  fv::MapProjection ref = MakeRotationFixture();
  fv::MapProjection rot = MakeRotationFixture();
  ASSERT_TRUE(rot.SetRotation(137.5).ok());
  ASSERT_TRUE(rot.SetRotation(360.0).ok());  // wraps to 0
  EXPECT_EQ(rot.Rotation(), 0.0);

  double ax = 0, ay = 0, bx = 0, by = 0;
  ASSERT_TRUE(ref.GeoToSurface({33.1, -80.4}, &ax, &ay).ok());
  ASSERT_TRUE(rot.GeoToSurface({33.1, -80.4}, &bx, &by).ok());
  EXPECT_EQ(ax, bx);
  EXPECT_EQ(ay, by);
  EXPECT_EQ(ref.VmapBounds().ur.lat, rot.VmapBounds().ur.lat);
}

TEST(MapProjectionRotation, NormalizesAndRejectsNonFinite) {
  fv::MapProjection p = MakeRotationFixture();
  ASSERT_TRUE(p.SetRotation(-90.0).ok());
  EXPECT_EQ(p.Rotation(), 270.0);
  ASSERT_TRUE(p.SetRotation(450.0).ok());
  EXPECT_EQ(p.Rotation(), 90.0);
  ASSERT_TRUE(p.SetRotation(-720.0).ok());
  EXPECT_EQ(p.Rotation(), 0.0);

  EXPECT_EQ(p.SetRotation(std::nan("")).code, fv::kInvalidArg);
  EXPECT_EQ(p.SetRotation(HUGE_VAL).code, fv::kInvalidArg);
  EXPECT_EQ(p.Rotation(), 0.0);  // a rejected turn changes nothing
}

// The centre is the pivot: it lands on the surface midpoint at every angle.
TEST(MapProjectionRotation, CentreIsTheFixedPoint) {
  fv::MapProjection p = MakeRotationFixture();
  for (double deg : {0.0, 17.0, 90.0, 180.0, 270.0, 344.5}) {
    ASSERT_TRUE(p.SetRotation(deg).ok());
    double sx = 0, sy = 0;
    ASSERT_TRUE(p.GeoToSurface(p.Center(), &sx, &sy).ok());
    EXPECT_NEAR(sx, 400.0, 1e-9) << deg;
    EXPECT_NEAR(sy, 300.0, 1e-9) << deg;
  }
}

// Clockwise means clockwise ON THE SCREEN: at 90, what was north of the
// centre is now to its right.
TEST(MapProjectionRotation, TurnsTheChartClockwise) {
  fv::MapProjection p = MakeRotationFixture();
  const fv::GeoPoint north{p.Center().lat + 0.2, p.Center().lon};

  double sx0 = 0, sy0 = 0;
  ASSERT_TRUE(p.GeoToSurface(north, &sx0, &sy0).ok());
  ASSERT_NEAR(sx0, 400.0, 1e-9);
  ASSERT_LT(sy0, 300.0);  // north is UP on an unturned chart
  const double arm = 300.0 - sy0;

  ASSERT_TRUE(p.SetRotation(90.0).ok());
  double sx = 0, sy = 0;
  ASSERT_TRUE(p.GeoToSurface(north, &sx, &sy).ok());
  EXPECT_NEAR(sx, 400.0 + arm, 1e-9);  // swung to the right
  EXPECT_NEAR(sy, 300.0, 1e-12);       // and exactly level: the table, not cos
}

// The convention MM2's camera answers in: it reports rotation 270 for a
// course of 090, and 270 must put what is EAST of the ship at the top of the
// screen. This is the test that pins the SIGN of the whole feature.
TEST(MapProjectionRotation, TrackUpPutsTheCourseUpTheScreen) {
  fv::MapProjection p = MakeRotationFixture();
  const fv::GeoPoint ahead{p.Center().lat, p.Center().lon + 0.2};  // due east

  double sx0 = 0, sy0 = 0;
  ASSERT_TRUE(p.GeoToSurface(ahead, &sx0, &sy0).ok());
  const double arm = sx0 - 400.0;
  ASSERT_GT(arm, 0.0);

  ASSERT_TRUE(p.SetRotation(270.0).ok());  // camera's answer for course 090
  double sx = 0, sy = 0;
  ASSERT_TRUE(p.GeoToSurface(ahead, &sx, &sy).ok());
  EXPECT_NEAR(sx, 400.0, 1e-12);
  EXPECT_NEAR(sy, 300.0 - arm, 1e-9);  // dead ahead is up the screen
}

// Cardinal turns are exact, not 6.1e-17 off: 180 negates both offsets.
TEST(MapProjectionRotation, HalfTurnIsAnExactNegation) {
  fv::MapProjection p = MakeRotationFixture();
  const fv::GeoPoint g{33.4, -80.6};
  double sx0 = 0, sy0 = 0;
  ASSERT_TRUE(p.GeoToSurface(g, &sx0, &sy0).ok());

  ASSERT_TRUE(p.SetRotation(180.0).ok());
  double sx = 0, sy = 0;
  ASSERT_TRUE(p.GeoToSurface(g, &sx, &sy).ok());
  EXPECT_EQ(sx, 400.0 - (sx0 - 400.0));
  EXPECT_EQ(sy, 300.0 - (sy0 - 300.0));
}

TEST(MapProjectionRotation, RoundTripsAtAnAwkwardAngle) {
  fv::MapProjection p = MakeRotationFixture();
  for (double deg : {30.0, 137.25, 271.9}) {
    ASSERT_TRUE(p.SetRotation(deg).ok());
    for (double sx : {12.0, 400.0, 790.0}) {
      for (double sy : {7.0, 300.0, 590.0}) {
        fv::GeoPoint g;
        ASSERT_TRUE(p.SurfaceToGeo(sx, sy, &g).ok());
        double bx = 0, by = 0;
        ASSERT_TRUE(p.GeoToSurface(g, &bx, &by).ok());
        EXPECT_NEAR(bx, sx, 1e-9) << deg;
        EXPECT_NEAR(by, sy, 1e-9) << deg;
      }
    }
  }
}

// Distance from the centre is preserved — a rotation is not a scaling. Done
// in PIXELS, because the geographic frame is anisotropic (dpp_lat != dpp_lon)
// and only the pixel frame is a rotation-invariant one.
TEST(MapProjectionRotation, PreservesDistanceFromTheCentre) {
  fv::MapProjection p = MakeRotationFixture();
  const fv::GeoPoint g{33.4, -80.6};
  double sx0 = 0, sy0 = 0;
  ASSERT_TRUE(p.GeoToSurface(g, &sx0, &sy0).ok());
  const double r0 = std::hypot(sx0 - 400.0, sy0 - 300.0);

  for (double deg : {23.0, 90.0, 211.75}) {
    ASSERT_TRUE(p.SetRotation(deg).ok());
    double sx = 0, sy = 0;
    ASSERT_TRUE(p.GeoToSurface(g, &sx, &sy).ok());
    EXPECT_NEAR(std::hypot(sx - 400.0, sy - 300.0), r0, 1e-9) << deg;
  }
}

// The real cost of a turned chart: the box every source is queried with is
// the AABB of the TURNED viewport. A quarter turn swaps the two extents; 45
// degrees costs the diagonal.
TEST(MapProjectionRotation, BoundsAreTheBoxOfTheTurnedViewport) {
  fv::MapProjection p = MakeRotationFixture();
  const fv::GeoRect b0 = p.VmapBounds();
  const double h0 = b0.ur.lat - b0.ll.lat;   // 601 px of latitude
  const double w0 = b0.ur.lon - b0.ll.lon;   // 801 px of longitude
  const double px_h = h0 / 601.0, px_w = w0 / 801.0;

  ASSERT_TRUE(p.SetRotation(90.0).ok());
  const fv::GeoRect b90 = p.VmapBounds();
  // The 801-pixel axis is now the vertical one, and vice versa.
  EXPECT_NEAR(b90.ur.lat - b90.ll.lat, 801 * px_h, 1e-9);
  EXPECT_NEAR(b90.ur.lon - b90.ll.lon, 601 * px_w, 1e-9);

  ASSERT_TRUE(p.SetRotation(180.0).ok());
  const fv::GeoRect b180 = p.VmapBounds();
  EXPECT_NEAR(b180.ur.lat - b180.ll.lat, h0, 1e-9);
  EXPECT_NEAR(b180.ur.lon - b180.ll.lon, w0, 1e-9);

  ASSERT_TRUE(p.SetRotation(45.0).ok());
  const fv::GeoRect b45 = p.VmapBounds();
  const double diag_px = (801 + 601) / std::sqrt(2.0);
  EXPECT_NEAR(b45.ur.lat - b45.ll.lat, diag_px * px_h, 1e-9);
  EXPECT_NEAR(b45.ur.lon - b45.ll.lon, diag_px * px_w, 1e-9);
  EXPECT_GT(b45.ur.lat - b45.ll.lat, h0);  // strictly larger: this is the cost
  EXPECT_GT(b45.ur.lon - b45.ll.lon, w0);

  // At every angle the box still holds the centre and all four corners of
  // the surface, which is the property a source query depends on.
  for (double deg : {0.0, 17.0, 45.0, 90.0, 211.75, 300.0}) {
    ASSERT_TRUE(p.SetRotation(deg).ok());
    const fv::GeoRect b = p.VmapBounds();
    EXPECT_TRUE(b.Contains(p.Center())) << deg;
    for (double sx : {0.0, 800.0}) {
      for (double sy : {0.0, 600.0}) {
        fv::GeoPoint g;
        ASSERT_TRUE(p.SurfaceToGeo(sx, sy, &g).ok());
        EXPECT_TRUE(b.Contains(g)) << deg << " @ " << sx << "," << sy;
      }
    }
  }
}

// Rotation is orthogonal to how dpp was chosen — it moves where a degree
// lands, never how big it is.
TEST(MapProjectionRotation, DoesNotDisturbTheScaleModes) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(640, 480).ok());
  ASSERT_TRUE(p.SetCenter({45.0, 7.0}).ok());
  ASSERT_TRUE(p.SetPhysicalScale(1000000.0, 0.25).ok());
  const double dlat = p.DegPerPixelLat(), dlon = p.DegPerPixelLon();

  ASSERT_TRUE(p.SetRotation(63.0).ok());
  EXPECT_EQ(p.DegPerPixelLat(), dlat);
  EXPECT_EQ(p.DegPerPixelLon(), dlon);
  EXPECT_EQ(p.Scale(), 1000000.0);
  EXPECT_TRUE(p.Ready());

  // and the turn survives a re-centre / re-scale
  ASSERT_TRUE(p.SetCenter({46.0, 8.0}).ok());
  ASSERT_TRUE(p.SetScale(250000.0).ok());
  EXPECT_EQ(p.Rotation(), 63.0);
}

// A viewport over the dateline still reports a wrapped rect when turned —
// the wrap is decided after the corners are, not instead of them.
TEST(MapProjectionRotation, TurnedViewportStillCrossesTheAntimeridian) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(400, 300).ok());
  ASSERT_TRUE(p.SetCenter({-17.0, 179.9}).ok());
  ASSERT_TRUE(p.SetScale(2000000.0).ok());
  ASSERT_TRUE(p.SetRotation(35.0).ok());
  EXPECT_TRUE(p.VmapBounds().CrossesAntimeridian());
}

// Engine convenience: a cartographic series passes its denominator straight
// through; a metres-resolution series (imagery) lands at exactly 100% at the
// reference pitch — one source pixel per screen pixel.
TEST(MapEnginePhysical, ImageryRendersAtNativeResolution) {
  auto catalog = std::make_shared<fv::Catalog>();
  fv::MapEngine engine(catalog);
  ASSERT_TRUE(engine.SetSurfaceDimensions(512, 512).ok());
  ASSERT_TRUE(engine.SetCenter({40.0, -80.0}).ok());

  // 1 m/px imagery at the native pitch must render at 100%: one screen pixel
  // spans one ground metre north-south.
  ASSERT_TRUE(engine.SetPhysicalScale(1.0, MAP_SCALE_METERS,
                                      fv::kNativeDisplayMmPerPixel)
                  .ok());
  const double lat = 40.0;
  const double m_per_deg_lat = 111132.92 - 559.82 * std::cos(2 * lat * M_PI / 180);
  const double ground_m = engine.CurrentProj().DegPerPixelLat() * m_per_deg_lat;
  EXPECT_NEAR(ground_m, 1.0, 1e-3);

  // A cartographic series: denominator used directly.
  ASSERT_TRUE(engine.SetPhysicalScale(500000.0, MAP_SCALE_DENOMINATOR, 0.25).ok());
  EXPECT_DOUBLE_EQ(engine.CurrentProj().Scale(), 500000.0);
}

// ---------------------------------------------------------------------------
// MapEngine synthetic: a dateline-crossing frame composites on both sides
// ---------------------------------------------------------------------------

// Solid-color 100x100 "frame" spanning lon 175 -> -178 (width 7 deg),
// lat -20 -> -15, with linear equal-arc transforms.
class StubRaster : public fv::IRasterSource {
 public:
  fv::Status Open(const std::string&) override { return fv::Status::Ok(); }
  fv::GeoRect Bounds() const override {
    return fv::GeoRect{{-20.0, 175.0}, {-15.0, -178.0}};
  }
  fv::Status Info(fv::ImageInfo* i) const override {
    i->size = {100, 100};
    i->bounds = Bounds();
    return fv::Status::Ok();
  }
  fv::Status ReadBlock(const fv::PixelRect& r, fv::PixelBuffer* out) override {
    *out = fv::PixelBuffer(r.width, r.height);
    for (int y = 0; y < r.height; ++y)
      for (int x = 0; x < r.width; ++x) {
        unsigned char* p = out->Row(y) + 4 * x;
        p[0] = 200; p[1] = 40; p[2] = 40; p[3] = 255;
      }
    return fv::Status::Ok();
  }
  fv::Status PixelToGeo(double px, double py, fv::GeoPoint* p) const override {
    p->lon = fv::NormalizeLon(175.0 + px * (7.0 / 100.0));
    p->lat = -15.0 - py * (5.0 / 100.0);
    return fv::Status::Ok();
  }
  fv::Status GeoToPixel(const fv::GeoPoint& p, double* px,
                        double* py) const override {
    *px = (fv::UnwrapLonNear(p.lon, 178.5) - 175.0) / (7.0 / 100.0);
    *py = (-15.0 - p.lat) / (5.0 / 100.0);
    return fv::Status::Ok();
  }
};

class StubEnum : public fv::IFrameEnumerator {
 public:
  fv::Status Begin(const std::string& dir) override {
    done_ = false;
    dir_ = dir;
    return fv::Status::Ok();
  }
  bool Next(fv::FrameInfo* f) override {
    if (done_) return false;
    done_ = true;
    f->path = dir_ + "/stub.frame";
    f->bounds = fv::GeoRect{{-20.0, 175.0}, {-15.0, -178.0}};
    f->series_key = "STUB";
    f->scale = 1000000.0;
    f->scale_units = 0;
    f->size_bytes = 1;
    return true;
  }

 private:
  std::string dir_;
  bool done_ = true;
};

TEST(EngineSynthetic, DatelineFrameCompositesBothSides) {
  fv::ClearFormatRegistryForTest();
  fv::FormatFactories f;
  f.format_key = "stubr";
  f.make_enumerator = [] { return std::make_shared<StubEnum>(); };
  f.make_raster_source = [] { return std::make_shared<StubRaster>(); };
  ASSERT_TRUE(fv::RegisterFormat(f).ok());

  auto cat = std::make_shared<fv::Catalog>();
  ASSERT_TRUE(cat->Open(":memory:").ok());
  int64_t id = 0;
  int n = 0;
  ASSERT_TRUE(cat->AddDataSource("/stub", "stubr", 0, &id).ok());
  ASSERT_TRUE(cat->Scan(id, &n).ok());
  ASSERT_EQ(n, 1);

  fv::MapEngine engine(cat);
  ASSERT_TRUE(engine.SetSurfaceDimensions(200, 150).ok());
  ASSERT_TRUE(engine.SetCenter({-17.5, 178.5}).ok());  // frame center, on AM
  // 1:40M -> viewport ~11 deg wide, wider than the 7-deg frame, so both
  // frame edges are on-surface
  ASSERT_TRUE(engine.SetScale(40000000.0).ok());

  fv::CpuCanvas canvas(200, 150);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  int drawn = 0;
  fv::Status s = engine.RenderBaseMap(canvas, 0, {}, &drawn);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(drawn, 1);

  auto sample = [&](double lon, const char* what) -> int {
    double sx = 0, sy = 0;
    EXPECT_TRUE(
        engine.CurrentProj().GeoToSurface({-17.5, lon}, &sx, &sy).ok());
    EXPECT_GE((int)sx, 0) << what;
    EXPECT_LT((int)sx, 200) << what;  // probe must be on-surface
    EXPECT_GE((int)sy, 0) << what;
    EXPECT_LT((int)sy, 150) << what;
    return canvas.Buffer().Row((int)sy)[4 * (int)sx];
  };
  // the frame paints pixels BOTH west and east of the dateline column
  EXPECT_EQ(sample(176.0, "west of AM"), 200);
  EXPECT_EQ(sample(-179.0, "east of AM"), 200);
  // inside the viewport but beyond the frame's east edge (182 = -178):
  // untouched background
  EXPECT_EQ(sample(-177.0, "beyond east edge"), 0);
  fv::ClearFormatRegistryForTest();
}

// ---------------------------------------------------------------------------
// MapEngine, the TURNED raster path (PR3)
// ---------------------------------------------------------------------------

// A frame with a NORTH and a SOUTH, which is what makes it possible to say
// that the picture turned rather than merely that its footprint did. 150x100
// pixels over lat [-1, 1] x lon [-1.5, 1.5] — 0.02 deg per pixel on both
// axes, so at the engine's matching resolution one frame pixel is one surface
// pixel and every expectation below is an exact count rather than a fit.
constexpr double kQuadDpp = 0.02;
constexpr int kQuadW = 150, kQuadH = 100;

class QuadRaster : public fv::IRasterSource {
 public:
  fv::Status Open(const std::string&) override { return fv::Status::Ok(); }
  fv::GeoRect Bounds() const override {
    return fv::GeoRect{{-1.0, -1.5}, {1.0, 1.5}};
  }
  fv::Status Info(fv::ImageInfo* i) const override {
    i->size = {kQuadW, kQuadH};
    i->bounds = Bounds();
    return fv::Status::Ok();
  }
  fv::Status ReadBlock(const fv::PixelRect& r, fv::PixelBuffer* out) override {
    *out = fv::PixelBuffer(r.width, r.height);
    for (int y = 0; y < r.height; ++y)
      for (int x = 0; x < r.width; ++x) {
        unsigned char* p = out->Row(y) + 4 * x;
        const bool north = (r.y + y) < kQuadH / 2;
        p[0] = north ? 200 : 40;
        p[1] = 40;
        p[2] = north ? 40 : 200;
        p[3] = 255;
      }
    return fv::Status::Ok();
  }
  fv::Status PixelToGeo(double px, double py, fv::GeoPoint* p) const override {
    p->lon = -1.5 + px * kQuadDpp;
    p->lat = 1.0 - py * kQuadDpp;
    return fv::Status::Ok();
  }
  fv::Status GeoToPixel(const fv::GeoPoint& p, double* px,
                        double* py) const override {
    *px = (p.lon + 1.5) / kQuadDpp;
    *py = (1.0 - p.lat) / kQuadDpp;
    return fv::Status::Ok();
  }
};

class QuadEnum : public fv::IFrameEnumerator {
 public:
  fv::Status Begin(const std::string& dir) override {
    done_ = false;
    dir_ = dir;
    return fv::Status::Ok();
  }
  bool Next(fv::FrameInfo* f) override {
    if (done_) return false;
    done_ = true;
    f->path = dir_ + "/quad.frame";
    f->bounds = fv::GeoRect{{-1.0, -1.5}, {1.0, 1.5}};
    f->series_key = "QUAD";
    f->scale = 100000.0;
    f->scale_units = 0;
    f->size_bytes = 1;
    return true;
  }

 private:
  std::string dir_;
  bool done_ = true;
};

// 240x240 so the frame stays wholly on-surface at EVERY angle: its
// half-diagonal is hypot(75, 50) = 90.1 px about a centre at 119.5.
constexpr int kTurnSurf = 240;
constexpr double kTurnCx = (kTurnSurf - 1) / 2.0;
constexpr double kTurnCy = (kTurnSurf - 1) / 2.0;
constexpr unsigned char kTurnBg = 24;

class EngineTurned : public ::testing::Test {
 protected:
  void SetUp() override {
    fv::ClearFormatRegistryForTest();
    fv::FormatFactories f;
    f.format_key = "stubq";
    f.make_enumerator = [] { return std::make_shared<QuadEnum>(); };
    f.make_raster_source = [] { return std::make_shared<QuadRaster>(); };
    ASSERT_TRUE(fv::RegisterFormat(f).ok());
    cat_ = std::make_shared<fv::Catalog>();
    ASSERT_TRUE(cat_->Open(":memory:").ok());
    int64_t id = 0;
    int n = 0;
    ASSERT_TRUE(cat_->AddDataSource("/quad", "stubq", 0, &id).ok());
    ASSERT_TRUE(cat_->Scan(id, &n).ok());
    ASSERT_EQ(n, 1);
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  // Renders the frame with the chart turned `deg` clockwise. The engine is
  // rebuilt per call so nothing but the rotation can differ.
  fv::PixelBuffer Render(double deg, int* drawn_out = nullptr) {
    fv::MapEngine engine(cat_);
    EXPECT_TRUE(engine.SetSurfaceDimensions(kTurnSurf, kTurnSurf).ok());
    EXPECT_TRUE(engine.SetCenter({0.0, 0.0}).ok());
    EXPECT_TRUE(engine.SetResolution(kQuadDpp, kQuadDpp).ok());
    EXPECT_TRUE(engine.SetRotation(deg).ok());
    fv::CpuCanvas canvas(kTurnSurf, kTurnSurf);
    canvas.Clear(fv::FvColor{kTurnBg, kTurnBg, kTurnBg, 255});
    int drawn = 0;
    fv::Status s = engine.RenderBaseMap(canvas, 0, {}, &drawn);
    EXPECT_TRUE(s.ok()) << s.message;
    EXPECT_EQ(drawn, 1);
    if (drawn_out != nullptr) *drawn_out = drawn;
    return canvas.Buffer();
  }

  // What is at this offset from the surface centre: 'r' north half, 'b'
  // south half, '.' background.
  static char At(const fv::PixelBuffer& b, double dx, double dy) {
    int x = (int)std::lround(kTurnCx + dx), y = (int)std::lround(kTurnCy + dy);
    EXPECT_GE(x, 0);
    EXPECT_LT(x, b.Width());
    EXPECT_GE(y, 0);
    EXPECT_LT(y, b.Height());
    const unsigned char* p = b.Row(y) + 4 * x;
    if (p[0] == 200) return 'r';
    if (p[2] == 200) return 'b';
    if (p[0] == kTurnBg && p[1] == kTurnBg) return '.';
    return '?';
  }

  static long Inked(const fv::PixelBuffer& b) {
    long n = 0;
    for (int y = 0; y < b.Height(); ++y) {
      const unsigned char* row = b.Row(y);
      for (int x = 0; x < b.Width(); ++x)
        if (row[4 * x] != kTurnBg || row[4 * x + 2] != kTurnBg) ++n;
    }
    return n;
  }

  // Axis-aligned box of everything that is not background.
  static void InkBox(const fv::PixelBuffer& b, int* w, int* h) {
    int x0 = b.Width(), y0 = b.Height(), x1 = -1, y1 = -1;
    for (int y = 0; y < b.Height(); ++y) {
      const unsigned char* row = b.Row(y);
      for (int x = 0; x < b.Width(); ++x)
        if (row[4 * x] != kTurnBg || row[4 * x + 2] != kTurnBg) {
          x0 = std::min(x0, x);
          x1 = std::max(x1, x);
          y0 = std::min(y0, y);
          y1 = std::max(y1, y);
        }
    }
    *w = x1 - x0 + 1;
    *h = y1 - y0 + 1;
  }

  std::shared_ptr<fv::Catalog> cat_;
};

// THE ACCEPTANCE TEST THE WHOLE PR SERIES IS UNDER, now for the raster path:
// rotation 0 is not "close to" the unrotated blit, it IS it, and a projection
// that has been turned and brought back is indistinguishable from one that
// never turned. The gate in CompositeRow is what guarantees it — the turned
// path is not entered at all — and this is the test that would fail if some
// later edit "unified" the two paths.
TEST_F(EngineTurned, ZeroIsTheExactIdentityAndSurvivesARoundTrip) {
  fv::PixelBuffer straight = Render(0.0);
  EXPECT_EQ(Fnv1a(Render(137.5)), Fnv1a(Render(137.5)));  // deterministic
  EXPECT_NE(Fnv1a(Render(137.5)), Fnv1a(straight));       // and really turns

  // 0 -> 137.5 -> 0 on ONE engine, which is what a shell produces the moment
  // track-up is switched off.
  fv::MapEngine engine(cat_);
  ASSERT_TRUE(engine.SetSurfaceDimensions(kTurnSurf, kTurnSurf).ok());
  ASSERT_TRUE(engine.SetCenter({0.0, 0.0}).ok());
  ASSERT_TRUE(engine.SetResolution(kQuadDpp, kQuadDpp).ok());
  ASSERT_TRUE(engine.SetRotation(137.5).ok());
  fv::CpuCanvas scratch(kTurnSurf, kTurnSurf);
  scratch.Clear(fv::FvColor{kTurnBg, kTurnBg, kTurnBg, 255});
  ASSERT_TRUE(engine.RenderBaseMap(scratch).ok());
  ASSERT_TRUE(engine.SetRotation(0.0).ok());
  fv::CpuCanvas back(kTurnSurf, kTurnSurf);
  back.Clear(fv::FvColor{kTurnBg, kTurnBg, kTurnBg, 255});
  ASSERT_TRUE(engine.RenderBaseMap(back).ok());
  EXPECT_EQ(Fnv1a(back.Buffer()), Fnv1a(straight));
}

// THE PICTURE TURNS, NOT JUST ITS FOOTPRINT. A frame with a red north and a
// blue south says which way is up; a rotation that moved the box but blitted
// the image axis-aligned would keep red above the centre at every angle.
// The sense is PR1's: the chart turns CLOCKWISE, so north swings to the right.
TEST_F(EngineTurned, TheImageTurnsWithTheChart) {
  fv::PixelBuffer b0 = Render(0.0);
  EXPECT_EQ(At(b0, 0, -20), 'r') << "north is up on an unturned chart";
  EXPECT_EQ(At(b0, 0, 20), 'b');

  fv::PixelBuffer b90 = Render(90.0);
  EXPECT_EQ(At(b90, 20, 0), 'r') << "a quarter turn puts north to the RIGHT";
  EXPECT_EQ(At(b90, -20, 0), 'b');

  fv::PixelBuffer b180 = Render(180.0);
  EXPECT_EQ(At(b180, 0, -20), 'b') << "half a turn puts south up";
  EXPECT_EQ(At(b180, 0, 20), 'r');

  fv::PixelBuffer b270 = Render(270.0);
  EXPECT_EQ(At(b270, -20, 0), 'r');
  EXPECT_EQ(At(b270, 20, 0), 'b');
}

// A quarter turn is the cheap case — it merely swaps the two extents — and
// it is where the two paths can be compared side by side: the unturned blit
// of a 150x100 frame is EXACTLY 150x100, and turning it a quarter gives
// 100x150 to within one pixel on the long axis.
//
// THE ONE PIXEL IS A HALF-PIXEL TIE AND IT IS NOT WORTH REMOVING. This
// frame's edges land on x = 44.5 and 194.5 of a 240-wide surface, exactly
// between two pixel centres, so the edge column belongs to the frame by a
// tie. The unturned path breaks it on the SURFACE coordinate (lround(44.5)
// = 45, and the far edge loses its column to the `- 1`), the turned path
// breaks it on the SOURCE coordinate the affine walks to, and the affine
// arrives at -0.4999999999 where the closed form would say -0.5 — so the
// tie goes the other way and the 90-degree box keeps a row the 0-degree box
// drops. Any rule stated without an epsilon has this; the fix would be to
// pick an epsilon, which trades a visible half-pixel for an invisible one.
TEST_F(EngineTurned, AQuarterTurnSwapsTheInkBox) {
  int w = 0, h = 0;
  InkBox(Render(0.0), &w, &h);
  EXPECT_EQ(w, kQuadW) << "the unturned path is exact and stays exact";
  EXPECT_EQ(h, kQuadH);
  InkBox(Render(90.0), &w, &h);
  EXPECT_EQ(w, kQuadH);
  EXPECT_NEAR(h, kQuadW, 1);
  InkBox(Render(270.0), &w, &h);
  EXPECT_EQ(w, kQuadH);
  EXPECT_NEAR(h, kQuadW, 1);
}

// THE NEGATIVE CLAIM, AND IT IS THE ONE THE TURNED PATH EXISTS FOR. The
// target region is the axis-aligned BOX of the turned frame, so its corners
// are outside the frame entirely. The unturned path clamps a source
// coordinate into the block it read — do that here and the frame's edge
// pixels smear out to fill all four corners, turning a diamond into a
// square. Masking them (alpha 0, dropped by DrawPixmap) is what leaves the
// diamond with the edges it should have.
//
// At 45 degrees the box is +/-88.4 px about the centre while the frame's own
// reach along the axes is 90.1 at the tips only; (80, 80) is 113 px along the
// frame's own x once un-turned, well past its 75-px half-width.
TEST_F(EngineTurned, ATurnedFrameDoesNotSmearIntoItsBoundingBox) {
  fv::PixelBuffer b = Render(45.0);
  EXPECT_EQ(At(b, 80, 80), '.') << "box corner must stay background";
  EXPECT_EQ(At(b, -80, 80), '.');
  EXPECT_EQ(At(b, 80, -80), '.');
  EXPECT_EQ(At(b, -80, -80), '.');
  // ...while the diamond itself is drawn, and still has a north.
  EXPECT_EQ(At(b, 60, 0), 'r') << "60 px right of centre is north-east of it";
  EXPECT_EQ(At(b, -60, 0), 'b');
}

// A rotation is area-preserving, so a frame that fits on the surface at every
// angle must cover about the same number of pixels at every angle. This is
// what catches the opposite failure from the one above: a turned blit that
// drops rows or columns and leaves the chart with holes in it.
TEST_F(EngineTurned, ATurnedFrameCoversTheSameAreaOfChart) {
  const long straight = Inked(Render(0.0));
  EXPECT_EQ(straight, (long)kQuadW * kQuadH);
  for (double deg : {30.0, 45.0, 137.5, 200.0, 315.0}) {
    const long turned = Inked(Render(deg));
    EXPECT_NEAR((double)turned, (double)straight, straight * 0.005)
        << "at " << deg << " degrees";
  }
}

// ---------------------------------------------------------------------------
// MapEngine (real data)
// ---------------------------------------------------------------------------

// Pinned 2026-07-17 after visually verifying fvrender_atlanta.png (LFC
// chart over Atlanta: city label, Hartsfield-Jackson, seamless frames,
// correct resample). 0 = probe mode.
constexpr uint64_t kHashAtlanta = 0xafffcd30a1aee1aaull;

class EngineReal : public ::testing::Test {
 protected:
  void SetUp() override {
    td_ = TestDataDir();
    if (td_.empty() || !fs::exists(td_ + "/rpf")) GTEST_SKIP();
    fv::ClearFormatRegistryForTest();
    fv::RegisterBuiltinFormats();
    catalog_ = std::make_shared<fv::Catalog>();
    ASSERT_TRUE(catalog_->Open(":memory:").ok());
    int64_t id = 0;
    int n = 0;
    ASSERT_TRUE(catalog_->AddDataSource(td_ + "/rpf", "cadrg", 0, &id).ok());
    ASSERT_TRUE(catalog_->Scan(id, &n).ok());
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  std::string td_;
  std::shared_ptr<fv::Catalog> catalog_;
};

TEST_F(EngineReal, RendersAtlantaLfc) {
  fv::MapEngine engine(catalog_);
  ASSERT_TRUE(engine.SetSurfaceDimensions(400, 300).ok());
  ASSERT_TRUE(engine.SetCenter({33.7488, -84.3882}).ok());
  ASSERT_TRUE(engine.SetScale(500000.0).ok());

  // restrict to LFC so the render is deterministic across series
  int64_t lfc = 0;
  std::vector<fv::SeriesRow> series;
  ASSERT_TRUE(catalog_->Series(&series).ok());
  for (const auto& s : series)
    if (s.series_key == "LFC") lfc = s.id;
  ASSERT_NE(lfc, 0);

  fv::CpuCanvas canvas(400, 300);
  canvas.Clear(fv::FvColor{24, 24, 24, 255});
  int drawn = 0;
  fv::Status s = engine.RenderBaseMap(canvas, lfc, {}, &drawn);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_GE(drawn, 1);

  // content sanity: mostly covered, many colors
  long inked = 0;
  for (int y = 0; y < 300; ++y) {
    const unsigned char* row = canvas.Buffer().Row(y);
    for (int x = 0; x < 400; ++x)
      if (row[4 * x] != 24 || row[4 * x + 1] != 24) ++inked;
  }
  EXPECT_GT(inked, 300 * 400 * 0.9) << "viewport should be nearly covered";

  uint64_t h = Fnv1a(canvas.Buffer());
  if (kHashAtlanta == 0) {
    printf("PROBE atlanta hash: 0x%llxull\n", (unsigned long long)h);
    fv::WritePng(canvas.Buffer(), "fvrender_atlanta.png");
  } else {
    EXPECT_EQ(h, kHashAtlanta);
  }
}

TEST_F(EngineReal, InterruptStopsRender) {
  fv::MapEngine engine(catalog_);
  ASSERT_TRUE(engine.SetSurfaceDimensions(400, 300).ok());
  ASSERT_TRUE(engine.SetCenter({33.7488, -84.3882}).ok());
  ASSERT_TRUE(engine.SetScale(500000.0).ok());
  fv::CpuCanvas canvas(400, 300);
  fv::Status s = engine.RenderBaseMap(canvas, 0, [] { return true; }, nullptr);
  EXPECT_EQ(s.code, fv::kInterrupted);
}

TEST_F(EngineReal, ElevationThroughEngine) {
  if (!fs::exists(td_ + "/dted")) GTEST_SKIP();
  fv::MapEngine engine(catalog_);
  float elev = 0;
  EXPECT_EQ(engine.GetElevation({31.5, -81.5}, &elev).code, fv::kNotFound);
  engine.SetElevationSource(
      std::make_shared<fv::DtedElevationSource>(td_ + "/dted"));
  ASSERT_TRUE(engine.GetElevation({31.5, -81.5}, &elev).ok());
  EXPECT_FLOAT_EQ(elev, 9.0f);  // pinned since the DTED adapter session
}

TEST_F(EngineReal, UnconfiguredEngineRejectsRender) {
  fv::MapEngine engine(catalog_);
  fv::CpuCanvas canvas(64, 64);
  EXPECT_EQ(engine.RenderBaseMap(canvas).code, fv::kInvalidArg);
}

}  // namespace
