// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file scale_bar_test.cpp
 * The scale bar: the division ladder against hand-worked values from
 * CScaleBarIcon::get_scale_params, and what OnDraw puts on the chart checked
 * against ground distance through geo_tool.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "fvkit/app/type_registry.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/overlay/scale_bar.h"
#include "fvkit/proj.h"
#include "geo_tool.h"

namespace {

using fv::ScaleBarDivisions;
using fv::ScaleBarOverlay;
using fv::ScaleBarUnits;
using fv::app::PropertyValue;

/// A real font, so the label boxes are measured. Same list as grid_overlay_test.
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

fv::MapProjection Atlanta(double scale = 5000000.0, int w = 640, int h = 480) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter({33.7488, -84.3882}).ok());
  EXPECT_TRUE(p.SetScale(scale).ok());
  return p;
}

std::shared_ptr<ScaleBarOverlay> MakeBar(long long orientation) {
  auto bar = std::make_shared<ScaleBarOverlay>();
  EXPECT_TRUE(
      bar->SetProperty("orientation", PropertyValue::Choice(orientation)).ok());
  EXPECT_TRUE(
      bar->SetProperty("label_font", PropertyValue::String(SystemFont())).ok());
  return bar;
}

double GroundMeters(const fv::MapProjection& proj, double x1, double y1,
                    double x2, double y2) {
  fv::GeoPoint a, b;
  EXPECT_TRUE(proj.SurfaceToGeo(x1, y1, &a).ok());
  EXPECT_TRUE(proj.SurfaceToGeo(x2, y2, &b).ok());
  double d = 0.0, ang = 0.0;
  GEO_geo_to_distance(a.lat, a.lon, b.lat, b.lon, &d, &ang);
  return d;
}

// ---------------------------------------------------------------------------
// The division ladder
// ---------------------------------------------------------------------------

TEST(ScaleBarDivisions, PowersOfTenStopAtTheMinimum) {
  // 100 NM: 1e5, 1e4, 1e3 give 0; 100 gives 1; 10 gives 10 and stops.
  const ScaleBarDivisions d =
      fv::ChooseScaleBarDivisions(185200.0, ScaleBarUnits::kNmYards, 3, 10);
  EXPECT_EQ(d.count, 11);
  EXPECT_DOUBLE_EQ(d.increment, 10.0);
  EXPECT_DOUBLE_EQ(d.meters_per_unit, 1852.0);
  EXPECT_EQ(d.unit_name, " NM");
}

TEST(ScaleBarDivisions, DoublesDownToTheMaximum) {
  // 250 NM: 10 gives 25 (> 10), 20 gives 12, 40 gives 6.
  const ScaleBarDivisions d =
      fv::ChooseScaleBarDivisions(463000.0, ScaleBarUnits::kNmYards, 3, 10);
  EXPECT_EQ(d.count, 7);
  EXPECT_DOUBLE_EQ(d.increment, 40.0);
}

TEST(ScaleBarDivisions, TheVerticalLadderAllowsMoreDivisions) {
  const ScaleBarDivisions d =
      fv::ChooseScaleBarDivisions(185200.0, ScaleBarUnits::kNmYards, 5, 20);
  EXPECT_EQ(d.count, 11);
  EXPECT_DOUBLE_EQ(d.increment, 10.0);
}

TEST(ScaleBarDivisions, BelowOneNauticalMileFallsBackToYards) {
  // 0.55 NM needs 0.1 NM steps, so the search restarts in yards: 1114 yd
  // gives 11 at 100 (> 10) and 5 at 200.
  const ScaleBarDivisions d = fv::ChooseScaleBarDivisions(
      0.55 * 1852.0, ScaleBarUnits::kNmYards, 3, 10);
  EXPECT_EQ(d.unit_name, " Yd");
  EXPECT_EQ(d.count, 6);
  EXPECT_DOUBLE_EQ(d.increment, 200.0);
  EXPECT_DOUBLE_EQ(d.meters_per_unit, 1852.0 / (6077.28 / 3.0));
}

TEST(ScaleBarDivisions, FalconViewsFootIsKept) {
  // 6077.28 ft to the nautical mile, not the international 6076.115.
  const ScaleBarDivisions d = fv::ChooseScaleBarDivisions(
      0.55 * 1852.0, ScaleBarUnits::kNmFeet, 3, 10);
  EXPECT_EQ(d.unit_name, " Ft");
  EXPECT_EQ(d.count, 4);
  EXPECT_DOUBLE_EQ(d.increment, 1000.0);
  EXPECT_DOUBLE_EQ(d.meters_per_unit, 1852.0 / 6077.28);
  EXPECT_GT(std::fabs(d.meters_per_unit - 0.3048), 5e-5);
}

TEST(ScaleBarDivisions, KilometresAndMetres) {
  const ScaleBarDivisions km =
      fv::ChooseScaleBarDivisions(3500.0, ScaleBarUnits::kKmMeters, 3, 10);
  EXPECT_EQ(km.unit_name, " Km");
  EXPECT_EQ(km.count, 4);
  EXPECT_DOUBLE_EQ(km.increment, 1.0);
  EXPECT_DOUBLE_EQ(km.meters_per_unit, 1000.0);

  const ScaleBarDivisions m =
      fv::ChooseScaleBarDivisions(850.0, ScaleBarUnits::kKmMeters, 3, 10);
  EXPECT_EQ(m.unit_name, " m");
  EXPECT_EQ(m.count, 9);
  EXPECT_DOUBLE_EQ(m.increment, 100.0);
  EXPECT_DOUBLE_EQ(m.meters_per_unit, 1.0);
}

TEST(ScaleBarDivisions, AnUnmeasurableDistanceDrawsNothing) {
  const double bad[] = {0.0, -5.0, std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::infinity(), 1e-300};
  for (double d : bad)
    EXPECT_EQ(fv::ChooseScaleBarDivisions(d, ScaleBarUnits::kNmYards, 3, 10)
                  .count,
              0)
        << d;
}

// ---------------------------------------------------------------------------
// OnDraw
// ---------------------------------------------------------------------------

TEST(ScaleBarOverlay, TheHorizontalRulerMeasuresTheGround) {
  const fv::MapProjection proj = Atlanta();
  fv::CpuCanvas canvas(640, 480);
  auto bar = MakeBar(1);
  ASSERT_TRUE(bar->OnDraw(proj, canvas).ok());
  const auto& s = bar->last_draw();

  ASSERT_GE(s.horizontal.count, 4);
  ASSERT_EQ(static_cast<int>(s.horizontal_x.size()), s.horizontal.count);
  EXPECT_EQ(s.horizontal_x.front(), 10);
  EXPECT_TRUE(s.vertical_y.empty());
  const int xinc = s.horizontal_x[1] - s.horizontal_x[0];
  for (size_t k = 1; k < s.horizontal_x.size(); ++k)
    EXPECT_EQ(s.horizontal_x[k] - s.horizontal_x[k - 1], xinc);

  // One division is its labelled ground distance to within a pixel.
  const double want_m = s.horizontal.increment * s.horizontal.meters_per_unit;
  const double got_m = GroundMeters(proj, 10, 240, 10 + xinc, 240);
  const double m_per_px = got_m / xinc;
  EXPECT_NEAR(got_m, want_m, m_per_px);

  ASSERT_EQ(static_cast<int>(s.labels.size()), s.horizontal.count);
  EXPECT_EQ(s.labels.front(), "0");
  EXPECT_NE(s.labels.back().find(" NM"), std::string::npos);
}

TEST(ScaleBarOverlay, TheVerticalRulerIsTheDefault) {
  const fv::MapProjection proj = Atlanta();
  fv::CpuCanvas canvas(640, 480);
  auto bar = std::make_shared<ScaleBarOverlay>();
  ASSERT_TRUE(bar->OnDraw(proj, canvas).ok());
  const auto& s = bar->last_draw();

  EXPECT_TRUE(s.horizontal_x.empty());
  ASSERT_GE(s.vertical.count, 5);
  // Bottom to top, starting at the bottom offset.
  EXPECT_NEAR(s.vertical_y.front(), 480 - 15, 1);
  for (size_t k = 1; k < s.vertical_y.size(); ++k)
    EXPECT_LT(s.vertical_y[k], s.vertical_y[k - 1]);

  const double want_m = s.vertical.increment * s.vertical.meters_per_unit;
  const int dy = s.vertical_y[0] - s.vertical_y[1];
  const double got_m = GroundMeters(proj, 320, s.vertical_y[0], 320,
                                    s.vertical_y[1]);
  EXPECT_NEAR(got_m, want_m, got_m / dy);
}

TEST(ScaleBarOverlay, BothRulersShareTheHorizontalIncrement) {
  // A tall, narrow view: the horizontal ruler's increment makes the vertical
  // one run past the 20 ticks Windows' fixed array held.
  const fv::MapProjection proj = Atlanta(5000000.0, 300, 2000);
  fv::CpuCanvas canvas(300, 2000);
  auto bar = MakeBar(2);
  ASSERT_TRUE(bar->OnDraw(proj, canvas).ok());
  const auto& s = bar->last_draw();

  ASSERT_GT(s.horizontal.count, 0);
  ASSERT_GT(s.vertical.count, 0);
  EXPECT_DOUBLE_EQ(s.vertical.increment, s.horizontal.increment);
  EXPECT_EQ(s.vertical.unit_name, s.horizontal.unit_name);
  EXPECT_GT(s.vertical.count, 20);
  // The horizontal ruler moves right to clear the vertical one's labels.
  EXPECT_GT(s.horizontal_x.front(), 10);
}

TEST(ScaleBarOverlay, SurvivesViewsItCannotMeasure) {
  fv::MapProjection proj = Atlanta(500000000.0);
  ASSERT_TRUE(proj.SetProjectionType(fv::ProjectionType::kOrthographic).ok());
  fv::CpuCanvas canvas(640, 480);
  auto bar = MakeBar(2);
  EXPECT_TRUE(bar->OnDraw(proj, canvas).ok());

  fv::MapProjection turned = Atlanta();
  ASSERT_TRUE(turned.SetRotation(30.0).ok());
  EXPECT_TRUE(bar->OnDraw(turned, canvas).ok());
  EXPECT_GT(bar->last_draw().horizontal.count, 0);
}

TEST(ScaleBarOverlay, HitsItsLabelsAndNothingElse) {
  if (SystemFont().empty()) GTEST_SKIP() << "no font to measure labels with";
  const fv::MapProjection proj = Atlanta();
  fv::CpuCanvas canvas(640, 480);
  auto bar = MakeBar(1);
  ASSERT_TRUE(bar->OnDraw(proj, canvas).ok());
  const int x0 = bar->last_draw().horizontal_x.front();

  // The first label sits centred above its tick: bottom edge at 480-15-11-2.
  std::vector<fv::app::HitItem> hits;
  bar->HitTestPoint(proj, {x0, 480 - 15 - 11 - 2 - 4}, 0.0, hits);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].overlay, bar.get());
  EXPECT_EQ(hits[0].hint.tool_tip, "Map Scale Bar");

  hits.clear();
  bar->HitTestPoint(proj, {320, 100}, 3.0, hits);
  EXPECT_TRUE(hits.empty());
}

// ---------------------------------------------------------------------------
// Properties and registration
// ---------------------------------------------------------------------------

TEST(ScaleBarOverlay, RefusesAChoiceItDoesNotOffer) {
  ScaleBarOverlay bar;
  EXPECT_FALSE(bar.SetProperty("orientation", PropertyValue::Choice(3)).ok());
  EXPECT_FALSE(bar.SetProperty("units", PropertyValue::Int(1)).ok());
  EXPECT_FALSE(bar.SetProperty("no_such_key", PropertyValue::Bool(true)).ok());
  ASSERT_TRUE(bar.SetProperty("units", PropertyValue::Choice(2)).ok());
  EXPECT_EQ(bar.GetInt("units", -1), 2);
  ASSERT_TRUE(bar.ResetToDefaults().ok());
  EXPECT_EQ(bar.GetInt("units", -1), 0);
  EXPECT_EQ(bar.GetInt("orientation", -1), 0);
  EXPECT_EQ(bar.GetInt("font_size", -1), 1);
}

TEST(ScaleBarOverlay, IsARegisteredStaticType) {
  fv::app::OverlayTypeRegistry registry;
  ASSERT_TRUE(fv::app::RegisterBuiltinOverlayTypes(registry).ok());
  const fv::app::OverlayTypeDesc* desc = registry.Find(ScaleBarOverlay::kTypeId);
  ASSERT_NE(desc, nullptr);
  EXPECT_TRUE(registry.IsStatic(ScaleBarOverlay::kTypeId));
  EXPECT_FALSE(desc->restore_at_startup);
  std::shared_ptr<fv::Overlay> made = desc->factory();
  ASSERT_NE(made, nullptr);
  EXPECT_NE(made->AsProperties(), nullptr);
  EXPECT_NE(made->AsHitTest(), nullptr);
  EXPECT_EQ(fv::app::SettingsPrefixForTypeId(ScaleBarOverlay::kTypeId),
            "scalebar.");
}

}  // namespace
