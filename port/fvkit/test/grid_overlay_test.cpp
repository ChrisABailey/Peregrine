// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The graticule: the spacing table, the label ladder, and what OnDraw puts on
// the chart. A pixel hash says a picture changed; these say WHAT it drew, and
// that is the assertion a graticule needs -- its failure mode is the wrong
// NUMBER of lines, or lines that are not parallels at all.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/overlay/grid.h"
#include "fvkit/overlay/grid_spacing.h"
#include "fvkit/geo.h"
#include "fvkit/proj.h"
#include "fvkit/settings.h"

namespace {

// A real font, so the label paths are exercised rather than skipped. Same
// candidate list as geo_draw_test's SystemFont.
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

// 1:5M over 640x480 shows about 3.2 degrees of latitude and 5.2 of longitude
// over Atlanta, which at that row of the table (5-degree majors, 1-degree
// minors, 15/5-minute ticks) is three parallels, five meridians and two major
// lines. A SMALL viewport at a FINE scale draws nothing at all and is right to
// -- at 1:2M the minor spacing is a whole degree and a 240x180 window spans
// half of one, which is what the sample's pixel-derived interval hid.
fv::MapProjection Atlanta(double scale = 5000000.0, int w = 640, int h = 480) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter({33.7488, -84.3882}).ok());
  EXPECT_TRUE(p.SetScale(scale).ok());
  return p;
}

// How many multiples of `step` fall inside [lo, hi]. The test's own
// arithmetic, deliberately not the overlay's: the claim being checked is
// "exactly the lines this spacing puts inside this viewport", and computing it
// here from the projection's published bounds keeps that claim independent of
// how OnDraw walks them.
int CountMultiplesIn(double lo, double hi, double step) {
  if (!(step > 0.0)) return 0;
  int n = 0;
  for (long long k = static_cast<long long>(std::ceil(lo / step - 1e-9));
       static_cast<double>(k) * step <= hi + 1e-12; ++k)
    ++n;
  return n;
}

// ---------------------------------------------------------------------------
// The spacing table
// ---------------------------------------------------------------------------

TEST(GraticuleSpacing, TranscribedFaithfully) {
  const fv::GraticuleSpacing lat =
      fv::GraticuleSpacingFor(1000000.0, fv::GridAxis::kLatitude);
  EXPECT_DOUBLE_EQ(lat.major_line_deg, 1.0);
  EXPECT_DOUBLE_EQ(lat.minor_line_deg, 1.0);
  EXPECT_DOUBLE_EQ(lat.major_tick_deg, 5.0 / 60.0);
  EXPECT_DOUBLE_EQ(lat.minor_tick_deg, 1.0 / 60.0);
}

// The one row where the two axes disagree, and the reason the table is a table.
TEST(GraticuleSpacing, LongitudeIsWiderThanLatitudeAtOneToTwoMillion) {
  const fv::GraticuleSpacing lat =
      fv::GraticuleSpacingFor(2000000.0, fv::GridAxis::kLatitude);
  const fv::GraticuleSpacing lon =
      fv::GraticuleSpacingFor(2000000.0, fv::GridAxis::kLongitude);
  EXPECT_DOUBLE_EQ(lat.major_line_deg, 1.0);
  EXPECT_DOUBLE_EQ(lon.major_line_deg, 3.0);
}

// From 1:100K in, every line is a major line -- the original spelled that as a
// separate `scale >= ONE_TO_100K` test in its draw loop; here it is the table.
TEST(GraticuleSpacing, MajorEqualsMinorAtLargeScale) {
  for (double s : {100000.0, 50000.0, 10000.0, 1000.0}) {
    const fv::GraticuleSpacing g =
        fv::GraticuleSpacingFor(s, fv::GridAxis::kLatitude);
    EXPECT_DOUBLE_EQ(g.major_line_deg, g.minor_line_deg) << "at 1:" << s;
    EXPECT_FALSE(g.has_ticks()) << "at 1:" << s;
  }
}

TEST(GraticuleSpacing, ClampsOffBothEndsOfTheTable) {
  // Coarser than 1:100M, and finer than 1:1K.
  EXPECT_DOUBLE_EQ(
      fv::GraticuleSpacingFor(1e12, fv::GridAxis::kLatitude).major_line_deg,
      60.0);
  EXPECT_DOUBLE_EQ(
      fv::GraticuleSpacingFor(1.0, fv::GridAxis::kLatitude).major_line_deg,
      5.0 / 3600.0);
}

TEST(GraticuleSpacing, EveryRowDrawsSomething) {
  for (const auto& row : fv::GraticuleSpacingTable().rows()) {
    EXPECT_TRUE(row.value.lat.has_lines()) << row.scale_denominator;
    EXPECT_TRUE(row.value.lon.has_lines()) << row.scale_denominator;
  }
}

// ---------------------------------------------------------------------------
// The label ladder
// ---------------------------------------------------------------------------

TEST(GraticuleLabel, FormatFollowsTheMINORSpacing) {
  // Degrees, at 1 degree spacing and coarser.
  EXPECT_EQ(fv::GraticuleLabelText(34.0, fv::GridAxis::kLatitude, 1.0),
            "N 34\xc2\xb0");
  // Degrees and minutes from 1 minute.
  EXPECT_EQ(fv::GraticuleLabelText(34.5, fv::GridAxis::kLatitude, 1.0 / 60.0),
            "N 34\xc2\xb0 30'");
  // Degrees, minutes and seconds from 1 second.
  EXPECT_EQ(
      fv::GraticuleLabelText(34.5125, fv::GridAxis::kLatitude, 1.0 / 3600.0),
      "N 34\xc2\xb0 30' 45\"");
  // Tenths of a second below that.
  EXPECT_EQ(
      fv::GraticuleLabelText(34.5125, fv::GridAxis::kLatitude, 0.1 / 3600.0),
      "N 34\xc2\xb0 30' 45.0\"");
}

TEST(GraticuleLabel, HemisphereLetterAndFieldWidth) {
  EXPECT_EQ(fv::GraticuleLabelText(-34.0, fv::GridAxis::kLatitude, 1.0),
            "S 34\xc2\xb0");
  // Longitude is three digits so a column of labels lines up.
  EXPECT_EQ(fv::GraticuleLabelText(-84.0, fv::GridAxis::kLongitude, 1.0),
            "W 084\xc2\xb0");
  EXPECT_EQ(fv::GraticuleLabelText(7.0, fv::GridAxis::kLongitude, 1.0),
            "E 007\xc2\xb0");
  EXPECT_EQ(fv::GraticuleLabelText(0.0, fv::GridAxis::kLatitude, 1.0),
            "N 00\xc2\xb0");
}

// The original's carry chain, which is the only fiddly part of the formatter.
TEST(GraticuleLabel, RoundingCarriesThroughEveryField) {
  // 34 deg 59 min 59.96 sec rounds to 35 deg 00 min 00.0 sec.
  const double v = 34.0 + 59.0 / 60.0 + 59.96 / 3600.0;
  EXPECT_EQ(fv::GraticuleLabelText(v, fv::GridAxis::kLatitude, 0.1 / 3600.0),
            "N 35\xc2\xb0 00' 00.0\"");
  // 34 deg 59.7 min rounds to 35 deg 00 min.
  EXPECT_EQ(fv::GraticuleLabelText(34.0 + 59.7 / 60.0, fv::GridAxis::kLatitude,
                                   1.0 / 60.0),
            "N 35\xc2\xb0 00'");
}

// ---------------------------------------------------------------------------
// OnDraw
// ---------------------------------------------------------------------------

TEST(GridOverlayDraw, DrawsTheLinesTheTableAsksFor) {
  fv::MapProjection proj = Atlanta();
  fv::CpuCanvas canvas(640, 480);
  canvas.SetDefaultFont(SystemFont());
  canvas.Clear(fv::FvColor{0, 0, 32, 255});
  fv::GridOverlay grid;
  ASSERT_TRUE(grid.OnDraw(proj, canvas).ok());

  const fv::GridOverlay::DrawStats& st = grid.last_draw();
  EXPECT_DOUBLE_EQ(st.scale_denominator, 5000000.0);
  EXPECT_DOUBLE_EQ(st.lat_spacing.major_line_deg, 5.0);
  EXPECT_DOUBLE_EQ(st.lat_spacing.minor_line_deg, 1.0);

  // Exactly the whole degrees inside the viewport, and no others.
  const fv::GeoRect b = proj.VmapBounds();
  EXPECT_EQ(st.parallels, CountMultiplesIn(b.ll.lat, b.ur.lat, 1.0));
  EXPECT_EQ(st.meridians, CountMultiplesIn(b.ll.lon, b.ur.lon, 1.0));
  EXPECT_GT(st.parallels, 1) << "viewport too small to be interesting";
  EXPECT_GT(st.meridians, 1) << "viewport too small to be interesting";
  // Of those, the multiples of five.
  EXPECT_EQ(st.major_lines, CountMultiplesIn(b.ll.lat, b.ur.lat, 5.0) +
                                CountMultiplesIn(b.ll.lon, b.ur.lon, 5.0));
  EXPECT_GT(st.labels_placed, 0);
}

TEST(GridOverlayDraw, NotReadyProjectionIsAnError) {
  fv::MapProjection proj;
  fv::CpuCanvas canvas(64, 64);
  fv::GridOverlay grid;
  EXPECT_FALSE(grid.OnDraw(proj, canvas).ok());
}

TEST(GridOverlayDraw, MinorLinesCanBeSwitchedOff) {
  fv::MapProjection proj = Atlanta();
  fv::CpuCanvas canvas(640, 480);
  canvas.SetDefaultFont(SystemFont());
  fv::GridOverlay grid;

  const fv::GeoRect b = proj.VmapBounds();
  ASSERT_TRUE(grid.OnDraw(proj, canvas).ok());
  EXPECT_EQ(grid.last_draw().parallels, CountMultiplesIn(b.ll.lat, b.ur.lat, 1.0));

  // With minors off only the 5-degree majors survive.
  ASSERT_TRUE(grid.SetProperty("show_minor_lines",
                               fv::app::PropertyValue::Bool(false))
                  .ok());
  ASSERT_TRUE(grid.OnDraw(proj, canvas).ok());
  EXPECT_EQ(grid.last_draw().parallels, CountMultiplesIn(b.ll.lat, b.ur.lat, 5.0));
  EXPECT_EQ(grid.last_draw().meridians, CountMultiplesIn(b.ll.lon, b.ur.lon, 5.0));
  EXPECT_LT(grid.last_draw().parallels,
            CountMultiplesIn(b.ll.lat, b.ur.lat, 1.0))
      << "turning minor lines off changed nothing";
}

TEST(GridOverlayDraw, TicksOnlyWhereTheTableHasThem) {
  fv::CpuCanvas canvas(640, 480);
  canvas.SetDefaultFont(SystemFont());
  fv::GridOverlay grid;

  // 1:5M has tick spacings (15 and 5 minutes).
  fv::MapProjection fine = Atlanta();
  ASSERT_TRUE(grid.OnDraw(fine, canvas).ok());
  EXPECT_GT(grid.last_draw().ticks, 0);

  // 1:50K does not: from 1:100K in the table zeroes them.
  fv::MapProjection large = Atlanta(50000.0);
  ASSERT_TRUE(grid.OnDraw(large, canvas).ok());
  EXPECT_EQ(grid.last_draw().ticks, 0);

  // And they can be turned off where they exist.
  ASSERT_TRUE(
      grid.SetProperty("show_ticks", fv::app::PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(grid.OnDraw(fine, canvas).ok());
  EXPECT_EQ(grid.last_draw().ticks, 0);
}

// THE BUG THIS PORT CLOSED (ledger: "grid overlay draws wrong under map
// rotation"). The sample drew screen-axis-aligned lines, so a turned chart got
// a grid that was still square to the screen. A real parallel projected under
// a 30-degree turn must come out at 30 degrees on screen -- and the way to
// assert that without a golden is to project two points of the same parallel
// and check the angle of the line between them.
TEST(GridOverlayDraw, ParallelsFollowTheChartUnderRotation) {
  fv::MapProjection proj = Atlanta();
  ASSERT_TRUE(proj.SetRotation(30.0).ok());

  double ax = 0, ay = 0, bx = 0, by = 0;
  ASSERT_TRUE(proj.GeoToSurface({34.0, -85.0}, &ax, &ay).ok());
  ASSERT_TRUE(proj.GeoToSurface({34.0, -84.0}, &bx, &by).ok());
  const double angle_deg = std::atan2(by - ay, bx - ax) * 180.0 / 3.14159265358979323846;
  // A clockwise chart turn tilts an eastward parallel by that much on screen.
  EXPECT_NEAR(std::fabs(angle_deg), 30.0, 1.0);

  // And the overlay draws that line rather than a horizontal one: the ink
  // reaches rows the sample's horizontals could never have touched.
  fv::CpuCanvas canvas(640, 480);
  canvas.SetDefaultFont(SystemFont());
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::GridOverlay grid;
  ASSERT_TRUE(grid.OnDraw(proj, canvas).ok());
  EXPECT_GT(grid.last_draw().parallels, 0);
  // The labels have to survive the turn too: their anchors come from scanning
  // each line for where it enters the surface, and under rotation the entry
  // edge is no longer the one the unrotated case used.
  // Every major line still gets a label. That is not free under rotation:
  // a line's entry point can land in a corner where a left-aligned label runs
  // off the screen, which is exactly what EdgeAnchoredStyle and the second
  // anchor are for. Measured before them: 1 placed, 3 refused.
  EXPECT_EQ(grid.last_draw().labels_placed, grid.last_draw().major_lines);
  EXPECT_EQ(grid.last_draw().labels_rejected, 0);

  // A column of pixels crossed by a tilted grid has ink at several DIFFERENT
  // rows; under the old screen-aligned drawing every column had ink at the
  // same rows as every other. Count distinct inked rows in two far-apart
  // columns and require them to differ.
  auto inked_rows = [&canvas](int x) {
    std::vector<int> rows;
    for (int y = 0; y < 180; ++y)
      if (canvas.Buffer().Row(y)[4 * x + 0] > 40) rows.push_back(y);
    return rows;
  };
  const std::vector<int> left = inked_rows(20);
  const std::vector<int> right = inked_rows(220);
  ASSERT_FALSE(left.empty());
  ASSERT_FALSE(right.empty());
  EXPECT_NE(left, right) << "grid ink is identical in two far-apart columns: "
                            "the lines are still screen-aligned";
}

// ---------------------------------------------------------------------------
// PJ5: the two defects the projections exposed
// ---------------------------------------------------------------------------

namespace {

// A wide view at a scale where a Lambert parallel visibly bends. The centre is
// far enough north that the cone is tight.
fv::MapProjection WideLambert(double center_lon = 0.0, int w = 800,
                              int h = 600) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter({55.0, center_lon}).ok());
  EXPECT_TRUE(p.SetScale(40000000.0).ok());
  EXPECT_TRUE(p.SetProjectionType(fv::ProjectionType::kLambert).ok());
  return p;
}

// Rows of the canvas that carry grid ink in column x, red channel over a
// threshold the background cannot reach.
std::vector<int> InkedRows(const fv::CpuCanvas& canvas, int x) {
  std::vector<int> rows;
  for (int y = 0; y < canvas.Size().height; ++y)
    if (canvas.Buffer().Row(y)[4 * x + 0] > 40) rows.push_back(y);
  return rows;
}

}  // namespace

// DEFECT 1: a parallel was sampled every 30 degrees and the samples joined by
// straight legs, which is exact in Equal Arc and wrong everywhere else. In
// Lambert a parallel is a circular arc, so the chord across a leg cut inside
// the true line by several pixels. The walk now subdivides by surface
// deflection, so the ink follows the projected curve to within a pixel.
TEST(GridOverlayProjected, ALambertParallelIsDrawnAsTheArcItIs) {
  fv::MapProjection proj = WideLambert();
  fv::CpuCanvas canvas(800, 600);
  canvas.SetDefaultFont(SystemFont());
  canvas.Clear(fv::FvColor{0, 0, 0, 255});

  fv::GridOverlay grid;
  ASSERT_TRUE(
      grid.SetProperty("show_ticks", fv::app::PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(
      grid.SetProperty("show_labels", fv::app::PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(grid.OnDraw(proj, canvas).ok());
  ASSERT_GT(grid.last_draw().parallels, 0);

  // The parallel nearest the centre row, sampled at the surface centre.
  const fv::GraticuleSpacing sp = grid.last_draw().lat_spacing;
  ASSERT_GT(sp.minor_line_deg, 0.0);
  const double lat =
      std::round(proj.Center().lat / sp.minor_line_deg) * sp.minor_line_deg;

  // The curve is worth testing only if it actually bends: the sagitta of one
  // of the old 30-degree chords has to be several pixels.
  double cx = 0, cy = 0, ex = 0, ey = 0, mx = 0, my = 0;
  ASSERT_TRUE(proj.GeoToSurface({lat, -15.0}, &cx, &cy).ok());
  ASSERT_TRUE(proj.GeoToSurface({lat, 15.0}, &ex, &ey).ok());
  ASSERT_TRUE(proj.GeoToSurface({lat, 0.0}, &mx, &my).ok());
  const double sagitta = std::fabs(my - 0.5 * (cy + ey));
  ASSERT_GT(sagitta, 3.0) << "this view does not bend the parallel enough to "
                             "tell a chord from an arc";

  // Walk the parallel itself and require ink where the projection puts it.
  // Stepping in longitude rather than in screen columns is what makes this a
  // statement about the CURVE: a chord would satisfy a column test at its two
  // ends and fail it in between, which is where these samples fall.
  const fv::GeoRect b = proj.VmapBounds();
  int checked = 0;
  for (int i = 0; i <= 60; ++i) {
    const double lon = b.ll.lon + (b.ur.lon - b.ll.lon) * (i / 60.0);
    double px = 0, py = 0;
    if (!proj.GeoToSurface({lat, lon}, &px, &py).ok()) continue;
    if (px < 20 || px > 780 || py < 2 || py > 598) continue;
    const std::vector<int> rows = InkedRows(canvas, static_cast<int>(px));
    ASSERT_FALSE(rows.empty()) << "no grid ink in column " << px;
    double best = 1e9;
    for (int r : rows) best = std::min(best, std::fabs(r - py));
    EXPECT_LT(best, 1.5) << "at " << lon << " east the nearest ink is " << best
                         << " px from the projected parallel";
    ++checked;
  }
  EXPECT_GT(checked, 20);
}

// DEFECT 2: every sample was normalized into [-180, 180) and only then
// projected, through a short-way unwrap taken per point against the viewport
// centre. Inside one 360-degree window that is harmless, because the unwrap
// puts the point back where it belongs. It stops being harmless once the view
// is wider than the world: the second and third copies of a meridian fold back
// onto the first, so a world view drew one set of lines where it should draw
// three, and the run between two folded samples was stroked across the map
// until the contour builder's seam guard cut it -- leaving a gap instead.
//
// The walk now stays in the unwrapped frame VmapLonRange reports, so every
// copy the view shows is drawn in its own place.
TEST(GridOverlayProjected, AViewWiderThanTheWorldDrawsEveryCopyOfAMeridian) {
  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(900, 300).ok());
  ASSERT_TRUE(proj.SetCenter({0.0, 0.0}).ok());
  // 0.6 degrees per pixel over 900 px is 540 degrees of longitude: one and a
  // half worlds.
  ASSERT_TRUE(proj.SetResolution(0.6, 0.6).ok());
  double west = 0, east = 0;
  ASSERT_TRUE(proj.VmapLonRange(&west, &east).ok());
  ASSERT_GT(east - west, 360.0) << "this view does not wrap the world";

  fv::CpuCanvas canvas(900, 300);
  canvas.SetDefaultFont(SystemFont());
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::GridOverlay grid;
  ASSERT_TRUE(
      grid.SetProperty("show_ticks", fv::app::PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(
      grid.SetProperty("show_labels", fv::app::PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(grid.OnDraw(proj, canvas).ok());
  const int meridians = grid.last_draw().meridians;
  ASSERT_GT(meridians, 3);

  // A meridian is a column inked from top to bottom. Count them, allowing
  // neighbouring columns to belong to the same stroke.
  int columns = 0;
  bool in_run = false;
  for (int x = 0; x < 900; ++x) {
    bool full = true;
    for (int y = 0; y < 300 && full; ++y)
      full = canvas.Buffer().Row(y)[4 * x + 0] > 40;
    if (full && !in_run) ++columns;
    in_run = full;
  }
  EXPECT_EQ(columns, meridians)
      << "the graticule reports " << meridians << " meridians but drew "
      << columns << " -- the copies past the seam folded onto each other";
}

// The meridian at 190 degrees east in the unwrapped frame is the line the
// reader calls 170 west, and the label has to say so.
TEST(GridOverlayProjected, AMeridianPastTheSeamIsLabelledByItsWrappedValue) {
  EXPECT_EQ(fv::GraticuleLabelText(fv::NormalizeLon(190.0),
                                   fv::GridAxis::kLongitude, 1.0),
            "W 170\xc2\xb0");

  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(proj.SetCenter({20.0, 180.0}).ok());
  ASSERT_TRUE(proj.SetScale(60000000.0).ok());
  fv::CpuCanvas canvas(800, 600);
  canvas.SetDefaultFont(SystemFont());
  fv::GridOverlay grid;
  ASSERT_TRUE(grid.OnDraw(proj, canvas).ok());
  // Meridians on both sides of the seam are drawn, and they get labels.
  EXPECT_GT(grid.last_draw().meridians, 2);
  if (!SystemFont().empty()) EXPECT_GT(grid.last_draw().labels_placed, 0);
}

// Orthographic hides half the earth, so a parallel runs off the limb. The walk
// breaks the run there instead of stroking a chord across the hidden side.
TEST(GridOverlayProjected, AnOrthographicParallelStopsAtTheLimb) {
  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(600, 600).ok());
  ASSERT_TRUE(proj.SetCenter({0.0, 0.0}).ok());
  ASSERT_TRUE(proj.SetScale(60000000.0).ok());
  ASSERT_TRUE(proj.SetProjectionType(fv::ProjectionType::kOrthographic).ok());

  fv::CpuCanvas canvas(600, 600);
  canvas.SetDefaultFont(SystemFont());
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::GridOverlay grid;
  ASSERT_TRUE(
      grid.SetProperty("show_labels", fv::app::PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(grid.OnDraw(proj, canvas).ok());
  ASSERT_GT(grid.last_draw().parallels, 0);

  // No ink outside the disc: every inked pixel projects back to a point that
  // projects forward to itself.
  int outside = 0, inside = 0;
  for (int y = 0; y < 600; y += 3)
    for (int x = 0; x < 600; x += 3) {
      if (canvas.Buffer().Row(y)[4 * x + 0] <= 40) continue;
      fv::GeoPoint g;
      if (proj.SurfaceToGeo(x, y, &g).ok())
        ++inside;
      else
        ++outside;
    }
  EXPECT_GT(inside, 100) << "nothing was drawn at all";
  EXPECT_EQ(outside, 0) << outside << " inked samples fall off the globe";
}

// ---------------------------------------------------------------------------
// The property page
// ---------------------------------------------------------------------------

TEST(GridOverlayProperties, IsDiscoverableThroughTheCapability) {
  fv::GridOverlay grid;
  fv::Overlay& as_overlay = grid;
  fv::app::Properties* p = as_overlay.AsProperties();
  ASSERT_NE(p, nullptr);
  EXPECT_FALSE(p->Describe().empty());
  // Every declared property must be readable and every default must be
  // settable -- the round trip a generic dialog performs on open and cancel.
  for (const fv::app::PropertySpec& s : p->Describe()) {
    fv::app::PropertyValue v;
    ASSERT_TRUE(p->GetProperty(s.key, &v).ok()) << s.key;
    EXPECT_EQ(v.type, s.type) << s.key;
    EXPECT_TRUE(p->SetProperty(s.key, s.default_value).ok()) << s.key;
  }
}

TEST(GridOverlayProperties, SetColorAgreesWithTheSchema) {
  fv::GridOverlay grid;
  grid.SetColor(fv::FvColor{1, 2, 3, 4});
  EXPECT_EQ(grid.GetColor("line_color", fv::FvColor{}).b, 3);
}

TEST(GridOverlayProperties, SurviveASettingsRoundTrip) {
  fv::GridOverlay a;
  ASSERT_TRUE(
      a.SetProperty("show_ticks", fv::app::PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(a.SetProperty("line_width", fv::app::PropertyValue::Int(3)).ok());
  fv::Settings s;
  ASSERT_TRUE(a.SaveTo(&s, "grid.").ok());

  fv::GridOverlay b;
  ASSERT_TRUE(b.LoadFrom(s, "grid.").ok());
  EXPECT_FALSE(b.GetBool("show_ticks", true));
  EXPECT_EQ(b.GetInt("line_width", -1), 3);
}

}  // namespace
