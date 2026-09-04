// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The Contour Lines overlay (plan C2/C3): the thresholds, the tile cache, the
// interval arithmetic and the labels. The elevation source here is an
// analytic cone, so every count below is a fact about the overlay's own rules
// rather than about a DTED file that might not be on this machine.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "fvkit/app/type_registry.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/overlay/contour_overlay.h"
#include "fvkit/proj.h"

namespace {

using fv::ContourOverlay;
using fv::GeoPoint;
using fv::GeoRect;
using fv::Status;

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

// A cone 3000 m high with its summit at the test centre, falling 1 metre for
// every 3 metres out, and DTED-1 posts.
class ConeElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override {
    return GeoRect{{33.0, -85.0}, {35.0, -83.0}};
  }
  Status GetElevation(const GeoPoint& p, float* out) override {
    ++queries;
    const double dy = (p.lat - 34.0) * 111000.0;
    const double dx = (p.lon + 84.0) * 92000.0;
    const double d = std::hypot(dx, dy);
    *out = static_cast<float>(std::max(0.0, 3000.0 - d / 3.0));
    return Status::Ok();
  }
  bool PostSpacing(const GeoPoint&, double* lat, double* lon) override {
    if (lat) *lat = 1.0 / 1200.0;  // 3 arcseconds
    if (lon) *lon = 1.0 / 1200.0;
    return true;
  }
  int queries = 0;
};

fv::MapProjection View(double scale, double lat = 34.0, double lon = -84.0,
                       int w = 640, int h = 480) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter({lat, lon}).ok());
  EXPECT_TRUE(p.SetScale(scale).ok());
  return p;
}

std::shared_ptr<ConeElevation> Cone() {
  return std::make_shared<ConeElevation>();
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

TEST(ContourOverlay, NoSourceDrawsNothing) {
  ContourOverlay ovl;
  fv::CpuCanvas canvas(640, 480);
  fv::MapProjection proj = View(100000.0);
  EXPECT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_TRUE(ovl.last_draw().no_source);
  EXPECT_EQ(ovl.last_draw().lines_drawn, 0);
  EXPECT_EQ(ovl.cached_tiles(), 0u);
}

TEST(ContourOverlay, RefusesSmallScales) {
  ContourOverlay ovl;
  ovl.SetElevationSource(Cone());
  fv::CpuCanvas canvas(640, 480);

  // 1:1M is smaller-scale than the 1:250 K threshold: nothing is drawn and,
  // just as importantly, nothing is SAMPLED.
  fv::MapProjection wide = View(1000000.0);
  EXPECT_TRUE(ovl.OnDraw(wide, canvas).ok());
  EXPECT_TRUE(ovl.last_draw().below_threshold);
  EXPECT_EQ(ovl.last_draw().samples, 0);
  EXPECT_EQ(ovl.cached_tiles(), 0u);

  fv::MapProjection close = View(100000.0);
  EXPECT_TRUE(ovl.OnDraw(close, canvas).ok());
  EXPECT_FALSE(ovl.last_draw().below_threshold);
  EXPECT_GT(ovl.last_draw().lines_drawn, 0);
}

// ---------------------------------------------------------------------------
// The interval, which is the one piece of arithmetic a user gets wrong
// ---------------------------------------------------------------------------

TEST(ContourOverlay, IntervalIsMajorOverDivisionsInTheChosenUnit) {
  ContourOverlay ovl;
  // FalconView's defaults: 1000 feet in 5 divisions.
  EXPECT_NEAR(ovl.MajorIntervalMeters(), 304.8, 1e-9);
  EXPECT_NEAR(ovl.IntervalMeters(), 60.96, 1e-9);

  ASSERT_TRUE(ovl.SetProperty("interval_unit", fv::app::PropertyValue::Choice(1))
                  .ok());  // meters
  ASSERT_TRUE(ovl.SetProperty("major_interval",
                              fv::app::PropertyValue::Double(300.0))
                  .ok());
  ASSERT_TRUE(
      ovl.SetProperty("divisions", fv::app::PropertyValue::Int(6)).ok());
  EXPECT_DOUBLE_EQ(ovl.MajorIntervalMeters(), 300.0);
  EXPECT_DOUBLE_EQ(ovl.IntervalMeters(), 50.0);
}

TEST(ContourOverlay, PropertiesAreCheckedNotTrusted) {
  ContourOverlay ovl;
  fv::app::PropertyValue v;
  EXPECT_EQ(ovl.GetProperty("no_such_key", &v).code, fv::kNotFound);
  EXPECT_EQ(ovl.SetProperty("divisions", fv::app::PropertyValue::Int(0)).code,
            fv::kInvalidArg);
  EXPECT_EQ(ovl.SetProperty("divisions", fv::app::PropertyValue::Int(11)).code,
            fv::kInvalidArg);
  EXPECT_EQ(
      ovl.SetProperty("divisions", fv::app::PropertyValue::Double(5.0)).code,
      fv::kInvalidArg);  // wrong type
  EXPECT_EQ(
      ovl.SetProperty("interval_unit", fv::app::PropertyValue::Choice(7)).code,
      fv::kInvalidArg);  // no such choice
  // Unchanged by every refusal above.
  EXPECT_NEAR(ovl.IntervalMeters(), 60.96, 1e-9);
}

// ---------------------------------------------------------------------------
// What lands on the chart
// ---------------------------------------------------------------------------

TEST(ContourOverlay, MajorLinesAreEveryNthAndCanBeDrawnAlone) {
  ContourOverlay ovl;
  ovl.SetElevationSource(Cone());
  fv::CpuCanvas canvas(640, 480);
  fv::MapProjection proj = View(100000.0);

  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  const ContourOverlay::DrawStats both = ovl.last_draw();
  EXPECT_GT(both.lines_drawn, 0);
  EXPECT_GT(both.major_lines, 0);
  EXPECT_LT(both.major_lines, both.lines_drawn);
  EXPECT_GT(both.vertices, both.lines_drawn);

  ASSERT_TRUE(ovl.SetProperty("show_minor_lines",
                              fv::app::PropertyValue::Bool(false))
                  .ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_EQ(ovl.last_draw().lines_drawn, both.major_lines);
  EXPECT_EQ(ovl.last_draw().major_lines, both.major_lines);
  // Turning minor lines off is a DRAWING change, not a tracing one: nothing
  // was re-sampled.
  EXPECT_EQ(ovl.last_draw().samples, 0);
}

TEST(ContourOverlay, ConeGivesConcentricClosedLines) {
  ContourOverlay ovl;
  ovl.SetElevationSource(Cone());
  fv::CpuCanvas canvas(640, 480);
  fv::MapProjection proj = View(100000.0);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  // Ink actually reached the canvas: the contour colour is nowhere in a
  // freshly cleared surface.
  const fv::PixelBuffer& px = canvas.Buffer();
  int inked = 0;
  for (int y = 0; y < px.Height(); ++y) {
    const unsigned char* row = px.Row(y);
    for (int x = 0; x < px.Width(); ++x)
      if (row[x * 4] == 192 && row[x * 4 + 1] == 0 && row[x * 4 + 2] == 64)
        ++inked;
  }
  EXPECT_GT(inked, 100);
}

// ---------------------------------------------------------------------------
// The cache, which is the reason for the tile lattice
// ---------------------------------------------------------------------------

TEST(ContourOverlay, APanReusesTracedTiles) {
  auto src = Cone();
  ContourOverlay ovl;
  ovl.SetElevationSource(src);
  fv::CpuCanvas canvas(640, 480);

  ASSERT_TRUE(ovl.OnDraw(View(100000.0), canvas).ok());
  const int traced = ovl.last_draw().tiles_traced;
  EXPECT_GT(traced, 0);
  const int after_first = src->queries;

  // The same view again: no sampling at all.
  ASSERT_TRUE(ovl.OnDraw(View(100000.0), canvas).ok());
  EXPECT_EQ(ovl.last_draw().tiles_traced, 0);
  EXPECT_EQ(src->queries, after_first);

  // A pan of a few pixels: still no sampling, because the lattice did not
  // move under it.
  ASSERT_TRUE(ovl.OnDraw(View(100000.0, 34.0005, -84.0005), canvas).ok());
  EXPECT_EQ(ovl.last_draw().tiles_traced, 0);
  EXPECT_EQ(src->queries, after_first);
}

TEST(ContourOverlay, ChangingTheIntervalRetraces) {
  ContourOverlay ovl;
  ovl.SetElevationSource(Cone());
  fv::CpuCanvas canvas(640, 480);
  fv::MapProjection proj = View(100000.0);

  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  const int lines = ovl.last_draw().lines_drawn;
  ASSERT_GT(ovl.cached_tiles(), 0u);

  ASSERT_TRUE(
      ovl.SetProperty("divisions", fv::app::PropertyValue::Int(1)).ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_GT(ovl.last_draw().tiles_traced, 0);
  // Five times the interval, so far fewer lines -- and every one of them
  // major, since divisions is 1.
  EXPECT_LT(ovl.last_draw().lines_drawn, lines);
  EXPECT_EQ(ovl.last_draw().lines_drawn, ovl.last_draw().major_lines);
}

TEST(ContourOverlay, ZoomKeepsTheCacheUntilItReallyMoves) {
  auto src = Cone();
  ContourOverlay ovl;
  ovl.SetElevationSource(src);
  fv::CpuCanvas canvas(640, 480);

  ASSERT_TRUE(ovl.OnDraw(View(100000.0), canvas).ok());
  const double sample = ovl.last_draw().sample_lat_deg;
  EXPECT_GT(sample, 0.0);
  const int after_first = src->queries;

  // A 10% zoom: inside the one-third hysteresis, so nothing is re-sampled.
  ASSERT_TRUE(ovl.OnDraw(View(110000.0), canvas).ok());
  EXPECT_DOUBLE_EQ(ovl.last_draw().sample_lat_deg, sample);
  EXPECT_EQ(src->queries, after_first);

  // Zoomed right out to the threshold: the demand has more than doubled, so
  // the sampling moves and the cache goes with it.
  ASSERT_TRUE(ovl.OnDraw(View(250000.0), canvas).ok());
  EXPECT_GT(ovl.last_draw().sample_lat_deg, sample);
  EXPECT_GT(ovl.last_draw().tiles_traced, 0);
}

TEST(ContourOverlay, SampleBudgetDefersRatherThanHangs) {
  ContourOverlay ovl;
  ovl.SetElevationSource(Cone());
  fv::CpuCanvas canvas(640, 480);
  fv::MapProjection proj = View(100000.0);

  // Enough for one tile and not two.
  ASSERT_TRUE(ovl.SetProperty("max_samples_per_draw",
                              fv::app::PropertyValue::Int(80000))
                  .ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_GE(ovl.last_draw().tiles_considered, 2);
  EXPECT_EQ(ovl.last_draw().tiles_traced, 1);
  EXPECT_GT(ovl.last_draw().tiles_over_budget, 0);

  // The next draw picks up where it left off rather than starting again.
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_EQ(ovl.last_draw().tiles_traced, 1);
  EXPECT_EQ(ovl.cached_tiles(), 2u);
}

// ---------------------------------------------------------------------------
// Labels
// ---------------------------------------------------------------------------

TEST(ContourOverlay, LabelsGoOnMajorLinesOnly) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no system font";

  ContourOverlay ovl;
  ovl.SetElevationSource(Cone());
  fv::CpuCanvas canvas(640, 480);
  fv::MapProjection proj = View(100000.0);

  ASSERT_TRUE(ovl.SetProperty("label_font",
                              fv::app::PropertyValue::String(font))
                  .ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_EQ(ovl.last_draw().labels_placed, 0);  // off by default

  ASSERT_TRUE(
      ovl.SetProperty("show_labels", fv::app::PropertyValue::Bool(true)).ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  const ContourOverlay::DrawStats s = ovl.last_draw();
  EXPECT_GT(s.labels_placed, 0);
  EXPECT_LE(s.labels_placed, s.major_lines);

  // The label threshold is independent of the display threshold: lines still
  // draw, labels stop.
  ASSERT_TRUE(ovl.SetProperty("label_threshold",
                              fv::app::PropertyValue::Double(50000.0))
                  .ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_GT(ovl.last_draw().lines_drawn, 0);
  EXPECT_EQ(ovl.last_draw().labels_placed, 0);
}

// ---------------------------------------------------------------------------
// Smoothing (plan C5)
// ---------------------------------------------------------------------------

TEST(ContourOverlay, SmoothingIsShapingNotRetracing) {
  ContourOverlay ovl;
  ovl.SetElevationSource(Cone());
  fv::CpuCanvas canvas(640, 480);
  fv::MapProjection proj = View(50000.0);

  // On by default: more vertices are stroked than were traced.
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  const ContourOverlay::DrawStats smooth = ovl.last_draw();
  EXPECT_GT(smooth.shaped_vertices, smooth.vertices);

  // Off: the stroked geometry IS the traced geometry, vertex for vertex --
  // FalconView's own line.
  ASSERT_TRUE(
      ovl.SetProperty("smoothing", fv::app::PropertyValue::Choice(0)).ok());
  ASSERT_TRUE(
      ovl.SetProperty("thinning_px", fv::app::PropertyValue::Double(0.0)).ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_EQ(ovl.last_draw().shaped_vertices, ovl.last_draw().vertices);
  EXPECT_EQ(ovl.last_draw().vertices, smooth.vertices);

  // And none of it re-sampled a single elevation post: shaping is a property
  // of the picture, so the traced tiles were untouched by either change.
  EXPECT_EQ(ovl.last_draw().samples, 0);
  EXPECT_EQ(ovl.last_draw().tiles_traced, 0);

  // The spline is interpolating, so it adds vertices too.
  ASSERT_TRUE(
      ovl.SetProperty("smoothing", fv::app::PropertyValue::Choice(2)).ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_GT(ovl.last_draw().shaped_vertices, ovl.last_draw().vertices);
}

TEST(ContourOverlay, ThinningPaysForTheSmoothingWhenZoomedOut) {
  ContourOverlay ovl;
  ovl.SetElevationSource(Cone());
  fv::CpuCanvas canvas(640, 480);
  // At the display threshold the posts are about four pixels apart, so most
  // traced vertices are within half a pixel of the line through their
  // neighbours.
  fv::MapProjection proj = View(250000.0);

  ASSERT_TRUE(
      ovl.SetProperty("smoothing", fv::app::PropertyValue::Choice(0)).ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  const ContourOverlay::DrawStats thinned = ovl.last_draw();
  EXPECT_LT(thinned.shaped_vertices, thinned.vertices);
}

// A benchmark rather than an assertion: run it with FV_CONTOUR_BENCH=1 in a
// RELEASE build (the default CMake build type is not optimised, which is the
// ledger's own warning) to price the three smoothing settings.
TEST(ContourOverlay, BenchSmoothing) {
  if (getenv("FV_CONTOUR_BENCH") == nullptr)
    GTEST_SKIP() << "set FV_CONTOUR_BENCH=1 to price the smoothing";

  ContourOverlay ovl;
  ovl.SetElevationSource(Cone());
  fv::CpuCanvas canvas(1024, 768);
  fv::MapProjection proj = View(50000.0, 34.0, -84.0, 1024, 768);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());  // warm the tile cache

  const char* names[] = {"none", "chaikin", "spline"};
  for (int mode = 0; mode < 3; ++mode) {
    ASSERT_TRUE(
        ovl.SetProperty("smoothing", fv::app::PropertyValue::Choice(mode)).ok());
    const int kFrames = 20;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i) ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
    const auto t1 = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count() / kFrames;
    std::printf("  smoothing=%-8s %7.2f ms/frame  %6d traced -> %7d stroked\n",
                names[mode], ms, ovl.last_draw().vertices,
                ovl.last_draw().shaped_vertices);
  }
  // And what a COLD frame costs, which is the sampling and the tracing.
  ovl.ClearCache();
  const auto c0 = std::chrono::steady_clock::now();
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  const auto c1 = std::chrono::steady_clock::now();
  std::printf("  cold (%lld posts sampled, %d tiles traced) %7.2f ms\n",
              ovl.last_draw().samples, ovl.last_draw().tiles_traced,
              std::chrono::duration<double, std::milli>(c1 - c0).count());
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

TEST(ContourOverlay, IsARegisteredStaticType) {
  fv::app::OverlayTypeRegistry registry;
  ASSERT_TRUE(fv::app::RegisterBuiltinOverlayTypes(registry).ok());
  const fv::app::OverlayTypeDesc* d = registry.Find(ContourOverlay::kTypeId);
  ASSERT_NE(d, nullptr);
  EXPECT_TRUE(registry.IsStatic(ContourOverlay::kTypeId));
  EXPECT_FALSE(d->restore_at_startup);
  // Under the graticule (900), over the base map.
  EXPECT_LT(d->default_display_order, 900);
  std::shared_ptr<fv::Overlay> made = d->factory();
  ASSERT_NE(made, nullptr);
  EXPECT_NE(made->AsProperties(), nullptr);
  // Its settings section is [contour].
  EXPECT_EQ(fv::app::SettingsPrefixForTypeId(ContourOverlay::kTypeId),
            "contour.");
}

}  // namespace
