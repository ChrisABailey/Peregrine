// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PJ5: the consumers that assumed geo->surface was linear.
//
// Each test states the assumption the site used to make and pins the answer the
// projection gives instead. The oracles are independent of the code under test:
// the direction of true north is taken by projecting two points, and the ground
// scale by projecting a known ground distance.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/overlay/moving_map_overlay.h"
#include "fvkit/overlay/ta_mask_overlay.h"
#include "fvkit/proj.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr fv::FvColor kWhite{255, 255, 255, 255};

std::string SystemFont() {
  const char* candidates[] = {
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/Supplemental/Courier New.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  };
  for (const char* f : candidates)
    if (FILE* fp = fopen(f, "rb")) {
      fclose(fp);
      return f;
    }
  return std::string();
}

// A Lambert view wide enough that the cone's convergence is degrees rather than
// arc-seconds, centred where the cone is tight.
fv::MapProjection WideLambert(int w = 800, int h = 600) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter({60.0, 0.0}).ok());
  EXPECT_TRUE(p.SetScale(30000000.0).ok());
  EXPECT_TRUE(p.SetProjectionType(fv::ProjectionType::kLambert).ok());
  return p;
}

// The clockwise screen angle of true north at `at`, from two projected points
// and nothing else. This is the oracle every convergence claim below is
// measured against.
double TrueNorthOnScreenDeg(const fv::MapProjection& proj,
                            const fv::GeoPoint& at) {
  double ax = 0, ay = 0, bx = 0, by = 0;
  EXPECT_TRUE(proj.GeoToSurface(at, &ax, &ay).ok());
  EXPECT_TRUE(proj.GeoToSurface({at.lat + 0.01, at.lon}, &bx, &by).ok());
  // Screen y grows downward, so north is -y; atan2(dx, -dy) is clockwise from
  // screen up.
  return std::atan2(bx - ax, -(by - ay)) * 180.0 / kPi;
}

// A bar from the origin out along +y (up on screen), so the ink says which way
// the symbol was turned. Same shape geo_draw_test uses for the rotation rule.
class BarLibrary : public fv::ISymbolLibrary {
 public:
  BarLibrary() {
    fv::SymbolPrimitive bar;
    bar.type = fv::SymbolPrimitiveType::kPolygon;
    bar.points = {{-20, 0}, {20, 0}, {20, 600}, {-20, 600}};
    bar.has_fill = true;
    bar.fill_color = fv::FvColor{0, 0, 255, 255};
    bar_.primitives.push_back(bar);
  }
  const fv::VectorSymbol* Symbol(const std::string& id) override {
    return id == "bar" ? &bar_ : nullptr;
  }

 private:
  fv::VectorSymbol bar_;
};

// The centroid of everything inked, weighted equally.
bool InkCentroid(const fv::PixelBuffer& b, double* cx, double* cy) {
  double sx = 0, sy = 0;
  long n = 0;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width(); ++x)
      if (row[4 * x + 2] > 150 && row[4 * x + 0] < 100) {
        sx += x;
        sy += y;
        ++n;
      }
  }
  if (n == 0) return false;
  *cx = sx / n;
  *cy = sy / n;
  return true;
}

// Columns of inked pixels, for the width a ground-sized label came out at.
int InkWidth(const fv::PixelBuffer& b) {
  int minx = b.Width(), maxx = -1;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width(); ++x)
      if (row[4 * x + 3] > 40 && row[4 * x + 0] < 200) {
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
      }
  }
  return maxx < minx ? 0 : maxx - minx + 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// SymbolAngleOnChart: the chart's turn was the only thing taken out of a
// north-up symbol, so off a cylindrical projection a symbol authored against a
// real-world bearing pointed wherever the grid happened to point.
// ---------------------------------------------------------------------------

TEST(ProjLinearity, AGeoAnchoredSymbolFollowsTrueNorthNotTheGrid) {
  fv::MapProjection proj = WideLambert();
  BarLibrary lib;

  // A point far enough east that the meridian there leans by several degrees.
  const fv::GeoPoint at{60.0, 20.0};
  double px = 0, py = 0;
  ASSERT_TRUE(proj.GeoToSurface(at, &px, &py).ok());
  ASSERT_GT(px, 20.0);
  ASSERT_LT(px, 780.0);
  const double north_deg = TrueNorthOnScreenDeg(proj, at);
  ASSERT_GT(std::fabs(north_deg), 5.0)
      << "this view does not lean the meridian enough to tell the two apart";

  // Drawn large: the angle is read off the ink, and a bar a few pixels long
  // cannot report one to better than a degree.
  fv::PointSymbolStyle big;
  big.scale = 10.0;

  fv::CpuCanvas c(800, 600);
  c.Clear(kWhite);
  fv::GeoDraw d(proj, &c, &lib);
  ASSERT_TRUE(d.DrawSymbol(at, "bar", big).ok());

  double cx = 0, cy = 0;
  ASSERT_TRUE(InkCentroid(c.Buffer(), &cx, &cy));
  // The bar runs from the anchor toward its centroid; that direction is the
  // direction the symbol's authored "up" came out at.
  const double drawn_deg = std::atan2(cx - px, -(cy - py)) * 180.0 / kPi;
  EXPECT_NEAR(drawn_deg, north_deg, 1.5)
      << "the bar was laid along the grid (0 degrees) rather than along the "
         "meridian (" << north_deg << ")";
}

TEST(ProjLinearity, APixelAnchoredSymbolIsStillTheCallersOwnAngle) {
  // The convergence belongs to the geographic anchor. A caller stamping at a
  // pixel said what angle it wanted and must get it, on any projection.
  fv::MapProjection proj = WideLambert();
  BarLibrary lib;
  fv::CpuCanvas c(800, 600);
  c.Clear(kWhite);
  fv::GeoDraw d(proj, &c, &lib);
  fv::PointSymbolStyle big;
  big.scale = 10.0;
  ASSERT_TRUE(d.DrawSymbolAtPixel(600.0, 300.0, "bar", big).ok());
  double cx = 0, cy = 0;
  ASSERT_TRUE(InkCentroid(c.Buffer(), &cx, &cy));
  EXPECT_NEAR(cx, 600.0, 1.5);
  EXPECT_LT(cy, 300.0) << "the bar must still point straight up";
}

// ---------------------------------------------------------------------------
// Ground-sized labels were measured from the CENTRE's metres per pixel, which
// on a conic is the wrong number by percent at the edge of the frame.
// ---------------------------------------------------------------------------

TEST(ProjLinearity, AGroundSizedLabelIsMeasuredAtItsOwnAnchor) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no system font";

  // Orthographic, because its radial scale is 1/cos(c) and runs away toward the
  // limb: a whole-globe view is where a centre-scale conversion is worst.
  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(800, 800).ok());
  ASSERT_TRUE(proj.SetCenter({0.0, 0.0}).ok());
  // 0.3 degrees per pixel over 800 px: the whole disc, with the limb on screen.
  ASSERT_TRUE(proj.SetResolution(0.3, 0.3).ok());
  ASSERT_TRUE(proj.SetProjectionType(fv::ProjectionType::kOrthographic).ok());

  fv::MapProjection::LocalScale centre, edge;
  ASSERT_TRUE(proj.LocalScaleAt(proj.Center(), &centre).ok());

  // A pixel well out toward the limb, on the vertical through the centre so the
  // y scale is the radial one. Where the limb falls depends on the scale, so
  // find it rather than assuming it.
  fv::GeoPoint g;
  double limb_y = 0.0;
  for (double y = 0.0; y < 400.0; y += 1.0)
    if (proj.SurfaceToGeo(400.0, y, &g).ok()) {
      limb_y = y;
      break;
    }
  ASSERT_GT(limb_y, 0.0) << "the whole surface is on the globe";
  // Inside the limb by enough room for the text, which is drawn above its
  // anchor.
  const double edge_y = limb_y + 30.0;
  fv::GeoPoint high;
  ASSERT_TRUE(proj.SurfaceToGeo(400.0, edge_y, &high).ok());
  ASSERT_TRUE(proj.LocalScaleAt(high, &edge).ok());
  const double ratio = centre.m_per_px_y / edge.m_per_px_y;
  ASSERT_LT(ratio, 0.7) << "the scale barely varies over this frame";

  fv::LabelStyle ls;
  ls.valid = true;
  ls.style.font_path = font;
  ls.style.color = fv::FvColor{0, 0, 0, 255};
  ls.size_unit = fv::LabelSizeUnit::kMeters;
  ls.ground_size_m = 1500000.0;

  auto width_at = [&](double x, double y) {
    fv::CpuCanvas c(800, 800);
    c.Clear(kWhite);
    fv::GeoDraw d(proj, &c);
    EXPECT_TRUE(d.DrawLabelAtPixel(x, y, "MMM", ls).ok());
    return InkWidth(c.Buffer());
  };

  const int w_centre = width_at(400.0, 400.0);
  const int w_edge = width_at(400.0, edge_y);
  ASSERT_GT(w_centre, 8);
  ASSERT_GT(w_edge, 4);
  // The label is a fixed number of ground metres, so where a pixel covers MORE
  // ground the text is narrower. The measured ratio follows the scale ratio
  // rather than sitting at 1, which is where it sat while every label was
  // converted at the centre.
  const double measured = static_cast<double>(w_edge) / w_centre;
  EXPECT_NEAR(measured, ratio, 0.15)
      << "centre " << w_centre << " px, limb " << w_edge
      << " px: expected the ground size to be converted at the anchor";
}

// ---------------------------------------------------------------------------
// The moving map's third term. MM2 ported `heading + map_rotation +
// convergence` and the convergence was identically zero because Equal Arc was
// the only projection there was.
// ---------------------------------------------------------------------------

TEST(ProjLinearity, TheMovingMapTakesItsConvergenceFromTheProjection) {
  fv::MapProjection proj = WideLambert();
  const fv::GeoPoint ship{60.0, 20.0};
  fv::MapProjection::LocalScale ls;
  ASSERT_TRUE(proj.LocalScaleAt(ship, &ls).ok());
  ASSERT_GT(std::fabs(ls.convergence_deg), 5.0);

  fv::MovingMapOverlay ovl;
  fv::PositionFix fix;
  fix.has_position = true;
  fix.lat = ship.lat;
  fix.lon = ship.lon;
  fix.has_true_heading = true;
  fix.true_heading_deg = 90.0;
  ovl.PushFix(fix);
  ovl.Tick(proj, 0.1);
  // The fix has to be in hand before the convergence can be asked for at the
  // ship, so the term lands on the tick after the first.
  ovl.Tick(proj, 0.1);

  // The overlay's term is the NEGATED projection convergence, because its two
  // consumers add it (moving_map_overlay.h). The claim that settles the sign is
  // geometric and is asserted here rather than restated: a true bearing of 90
  // at the ship must come out at the screen angle the projection draws that
  // bearing at.
  EXPECT_NEAR(ovl.convergence_deg(), -ls.convergence_deg, 1e-9);

  double ax = 0, ay = 0, bx = 0, by = 0;
  ASSERT_TRUE(proj.GeoToSurface(ship, &ax, &ay).ok());
  // A point due east of the ship: bearing 090 on the ground.
  ASSERT_TRUE(proj.GeoToSurface({ship.lat, ship.lon + 0.02}, &bx, &by).ok());
  const double east_on_screen =
      fv::NormalizeHeadingDeg(std::atan2(bx - ax, -(by - ay)) * 180.0 / kPi);
  EXPECT_NEAR(ovl.screen_angle_deg(), east_on_screen, 0.2);
}

TEST(ProjLinearity, AnEqualArcMovingMapHasNoConvergenceTerm) {
  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(proj.SetCenter({60.0, 0.0}).ok());
  ASSERT_TRUE(proj.SetScale(30000000.0).ok());

  fv::MovingMapOverlay ovl;
  fv::PositionFix fix;
  fix.has_position = true;
  fix.lat = 60.0;
  fix.lon = 20.0;
  fix.has_true_heading = true;
  fix.true_heading_deg = 90.0;
  ovl.PushFix(fix);
  ovl.Tick(proj, 0.1);
  ovl.Tick(proj, 0.1);
  EXPECT_DOUBLE_EQ(ovl.convergence_deg(), 0.0);
  EXPECT_NEAR(ovl.screen_angle_deg(), 90.0, 1e-9);
}

// ---------------------------------------------------------------------------
// The terrain-avoidance mask walked the surface with two constant geographic
// steps, taken from three SurfaceToGeo calls. That is exact while the
// projection is affine and a shear everywhere else.
// ---------------------------------------------------------------------------

namespace {

// Ground at 1000 m in a narrow band of latitude and no coverage outside it. The
// edge of the coverage is a PARALLEL, which is a straight line on an affine
// chart and an arc on a conic -- so the shape of the painted region is the
// whole assertion.
class BandElevation : public fv::IElevationSource {
 public:
  BandElevation(double south, double north) : south_(south), north_(north) {}
  fv::GeoRect Bounds() const override {
    return fv::GeoRect{{south_ - 5.0, -40.0}, {north_ + 5.0, 40.0}};
  }
  fv::Status GetElevation(const fv::GeoPoint& p, float* out) override {
    if (p.lat < south_ || p.lat > north_)
      return fv::Status::Error(fv::kOutOfCoverage, "outside the band");
    *out = 1000.0f;
    return fv::Status::Ok();
  }
  bool PostSpacing(const fv::GeoPoint&, double* lat, double* lon) override {
    if (lat) *lat = 1.0 / 1200.0;
    if (lon) *lon = 1.0 / 1200.0;
    return true;
  }

 private:
  double south_, north_;
};

}  // namespace

TEST(ProjLinearity, TheTerrainMaskFollowsTheProjectedParallel) {
  constexpr double kSouth = 59.9, kNorth = 60.1;

  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(1200, 900).ok());
  ASSERT_TRUE(proj.SetCenter({60.0, 0.0}).ok());
  ASSERT_TRUE(proj.SetScale(2000000.0).ok());
  ASSERT_TRUE(proj.SetProjectionType(fv::ProjectionType::kLambert).ok());

  // The band's edge has to bend by more than the test's tolerance, or an affine
  // walk would satisfy it.
  double lx = 0, ly = 0, mx = 0, my = 0, rx = 0, ry = 0;
  ASSERT_TRUE(proj.GeoToSurface({kNorth, -2.0}, &lx, &ly).ok());
  ASSERT_TRUE(proj.GeoToSurface({kNorth, 0.0}, &mx, &my).ok());
  ASSERT_TRUE(proj.GeoToSurface({kNorth, 2.0}, &rx, &ry).ok());
  ASSERT_GT(std::fabs(my - 0.5 * (ly + ry)), 3.0)
      << "this view does not bend the parallel enough to tell an arc from a "
         "chord";

  fv::TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<BandElevation>(kSouth, kNorth));
  ASSERT_TRUE(
      ovl.SetProperty("show_peak", fv::app::PropertyValue::Bool(false)).ok());
  // Flying below the band's 1000 m, so every post in it is a warning: the
  // BAND'S SHAPE is what this test is about, not the classifier.
  ASSERT_TRUE(ovl.SetProperty("altitude",
                              fv::app::PropertyValue::Double(2000.0)).ok());
  fv::CpuCanvas canvas(1200, 900);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  ASSERT_GT(ovl.last_draw().mask_pixels, 0);

  // Every sampled pixel: painted exactly where the projection says the pixel
  // sits inside the band. A margin of a few posts either side of the band edge
  // is excluded, because the mask reads the NEAREST POST and the tile lattice
  // it reads from is not aligned to the band: within that margin the two
  // answers legitimately differ, and outside it any disagreement is the walk.
  const double post = 1.0 / 1200.0;
  int inside_painted = 0, outside_clear = 0, wrong = 0;
  for (int y = 0; y < 900; y += 3)
    for (int x = 0; x < 1200; x += 7) {
      fv::GeoPoint g;
      if (!proj.SurfaceToGeo(x + 0.5, y + 0.5, &g).ok()) continue;
      const double from_edge =
          std::min(std::fabs(g.lat - kSouth), std::fabs(g.lat - kNorth));
      if (from_edge < 4.0 * post) continue;
      const bool in_band = g.lat > kSouth && g.lat < kNorth;
      const bool painted = canvas.Buffer().Row(y)[4 * x + 3] > 0 &&
                           !(canvas.Buffer().Row(y)[4 * x + 0] == 0 &&
                             canvas.Buffer().Row(y)[4 * x + 1] == 0 &&
                             canvas.Buffer().Row(y)[4 * x + 2] == 0);
      if (in_band == painted) {
        if (in_band)
          ++inside_painted;
        else
          ++outside_clear;
      } else {
        ++wrong;
      }
    }
  EXPECT_GT(inside_painted, 200) << "the band was barely drawn";
  EXPECT_GT(outside_clear, 200) << "nothing was left unpainted";
  EXPECT_EQ(wrong, 0) << wrong << " sampled pixels disagree with the "
                                 "projection about which side of the band "
                                 "edge they are on";
}

// The resolver divides RAW DEGREE deltas by the degrees-per-pixel it is handed,
// so the local metres per pixel have to be converted per axis before they get
// there. Handing the metres over unconverted drops the cos(lat) that separates
// a degree of longitude from a degree of latitude, which at 60 north is a
// factor of two and twelve degrees of bearing.
//
// Only a DERIVED heading reaches that arithmetic: a reported one wins outright,
// which is why the convergence tests above cannot see it. The end-to-end claim
// is asserted, since it is the one a user sees: a ship whose track runs up and
// to the right on the screen is drawn pointing up and to the right.
TEST(ProjLinearity, ADerivedHeadingUsesTheLocalScaleInTheRightUnits) {
  fv::MapProjection proj = WideLambert();
  const fv::GeoPoint a{60.0, 20.0};
  // The second fix is placed by SCREEN offset, so the answer is read off the
  // surface rather than off the globe.
  double ax = 0, ay = 0;
  ASSERT_TRUE(proj.GeoToSurface(a, &ax, &ay).ok());
  fv::GeoPoint b;
  ASSERT_TRUE(proj.SurfaceToGeo(ax + 40.0, ay - 40.0, &b).ok());

  fv::MovingMapOverlay ovl;
  for (const fv::GeoPoint& p : {a, b}) {
    fv::PositionFix fix;
    fix.has_position = true;
    fix.lat = p.lat;
    fix.lon = p.lon;
    ovl.PushFix(fix);
    ovl.Tick(proj, 0.1);
  }
  const fv::MovingMapTick t = ovl.Tick(proj, 0.1);
  ASSERT_TRUE(t.heading.known);
  ASSERT_FALSE(t.heading.reported);

  // 45 degrees: the step was 40 px right and 40 px up. The budget covers the
  // resolver's own tangent-plane approximation over a step this long (it scales
  // longitude at the first fix, not at the midpoint), and nothing else -- the
  // unconverted metres put this at 64 rather than 45.
  EXPECT_NEAR(ovl.screen_angle_deg(), 45.0, 3.0);
}
