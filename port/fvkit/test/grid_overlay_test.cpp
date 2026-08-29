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

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/overlay/grid.h"
#include "fvkit/overlay/grid_spacing.h"
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
