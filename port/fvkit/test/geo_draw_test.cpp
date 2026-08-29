// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// GeoDraw tests (draw plan G3).
//
// No goldens here on purpose. G3's output is new symbology, not ported
// symbology, so there is nothing to be bit-faithful TO — and a hash proves
// nothing about the two properties that actually matter at this seam (is the
// casing OUTSIDE the line, does the great circle bow the right way). Every
// assertion below is therefore directional or a count, in the F1/F2 spirit.

#include "fvkit/canvas/geo_draw.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/symbol/builtin.h"

namespace {

using fv::FvColor;
using fv::GeoDraw;
using fv::GeoLineStyle;
using fv::GeoPoint;
using fv::LineKind;
using fv::MapProjection;

const FvColor kBlue{40, 90, 210, 255};
const FvColor kWhite{255, 255, 255, 255};
const FvColor kRed{220, 30, 30, 255};

MapProjection HarbourView() {
  MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(400, 300).ok());
  EXPECT_TRUE(p.SetCenter({32.75, -79.90}).ok());
  EXPECT_TRUE(p.SetResolution(0.0002, 0.0002).ok());
  return p;
}

MapProjection WorldView() {
  MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(400, 300).ok());
  EXPECT_TRUE(p.SetCenter({40.0, -60.0}).ok());
  EXPECT_TRUE(p.SetResolution(0.25, 0.4).ok());
  return p;
}

// Pixels that are not the background.
int InkCount(const fv::PixelBuffer& b) {
  int n = 0;
  for (int y = 0; y < b.Height(); ++y)
    for (int x = 0; x < b.Width(); ++x) {
      const unsigned char* px = b.Row(y) + 4 * x;
      if (px[0] != 255 || px[1] != 255 || px[2] != 255) ++n;
    }
  return n;
}

bool IsColor(const fv::PixelBuffer& b, int x, int y, const FvColor& c) {
  if (x < 0 || y < 0 || x >= b.Width() || y >= b.Height()) return false;
  const unsigned char* px = b.Row(y) + 4 * x;
  return px[0] == c.r && px[1] == c.g && px[2] == c.b;
}

int CountColor(const fv::PixelBuffer& b, const FvColor& c) {
  int n = 0;
  for (int y = 0; y < b.Height(); ++y)
    for (int x = 0; x < b.Width(); ++x)
      if (IsColor(b, x, y, c)) ++n;
  return n;
}

// The topmost inked row in a column, or -1.
int TopInkRow(const fv::PixelBuffer& b, int x) {
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* px = b.Row(y) + 4 * x;
    if (px[0] != 255 || px[1] != 255 || px[2] != 255) return y;
  }
  return -1;
}

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

// ---------------------------------------------------------------------------
// The preset table
// ---------------------------------------------------------------------------

TEST(LinePresets, SolidIsThePlainPenAndNotAOneRunCycle) {
  fv::Pen pen;
  pen.width = 2;
  // Expressing solid as a cycle would cost a placer walk to draw what
  // DrawLines draws, so it deliberately comes back invalid.
  EXPECT_FALSE(fv::MakeLinePreset(fv::line_preset::kSolid, pen).valid);
}

TEST(LinePresets, AnUnknownNameFallsBackToSolidRatherThanToNothing) {
  fv::Pen pen;
  EXPECT_FALSE(fv::MakeLinePreset("no-such-preset", pen).valid);
  GeoLineStyle s = fv::PresetGeoLine("no-such-preset", kRed, 2);
  EXPECT_TRUE(s.stroke.valid) << "a style file naming an unknown preset must "
                                 "still draw a line";
}

TEST(LinePresets, EveryNamedPresetBuildsRunsAndTheDecoratedOnesNameASymbol) {
  fv::Pen pen;
  for (const char* const* n = fv::line_preset::kAll; *n != nullptr; ++n) {
    const std::string name = *n;
    fv::LinePatternStyle p = fv::MakeLinePreset(name, pen);
    if (name == fv::line_preset::kSolid) continue;
    EXPECT_TRUE(p.valid) << name;
    EXPECT_FALSE(p.runs.empty()) << name;
    if (name == fv::line_preset::kRailroad || name == fv::line_preset::kArrow ||
        name == fv::line_preset::kTick || name == fv::line_preset::kNotch ||
        name == fv::line_preset::kFeba) {
      bool has_symbol = false;
      for (const fv::PathRun& r : p.runs)
        if (r.type == fv::PathRunType::kSymbol && !r.symbol_id.empty())
          has_symbol = true;
      EXPECT_TRUE(has_symbol)
          << name << " is a decoration: it must stamp a builtin";
    }
  }
}

TEST(LinePresets, ScaleMultipliesEveryLengthAndEveryStamp) {
  fv::Pen pen;
  fv::LinePatternStyle one = fv::MakeLinePreset(fv::line_preset::kRailroad, pen);
  fv::LinePatternStyle two =
      fv::MakeLinePreset(fv::line_preset::kRailroad, pen, 2.0);
  ASSERT_EQ(one.runs.size(), two.runs.size());
  for (size_t i = 0; i < one.runs.size(); ++i) {
    EXPECT_DOUBLE_EQ(two.runs[i].length, one.runs[i].length * 2.0);
    EXPECT_EQ(two.runs[i].type, one.runs[i].type);
    // symbol_scale rides on the stamps; a dash run carries the default and is
    // not asked to scale something it does not draw.
    if (one.runs[i].type == fv::PathRunType::kSymbol)
      EXPECT_DOUBLE_EQ(two.runs[i].symbol_scale,
                       one.runs[i].symbol_scale * 2.0);
  }
}

TEST(LinePresets, APatternReplacesThePlainStrokeRatherThanSittingOverIt) {
  GeoLineStyle s = fv::PresetGeoLine(fv::line_preset::kDash, kBlue, 3);
  EXPECT_TRUE(s.pattern.valid);
  EXPECT_FALSE(s.stroke.valid)
      << "a solid stroke under a dash pattern would draw the solid line the "
         "pattern exists to replace";
  EXPECT_EQ(s.pattern.pen.width, 3);
}

TEST(LinePresets, ACasingIsWiderOnEachSideAndTakesItsOwnColour) {
  GeoLineStyle s = fv::SolidGeoLine(kBlue, 3);
  fv::AddCasing(&s, kWhite, 2);
  EXPECT_TRUE(s.casing.valid);
  EXPECT_EQ(s.casing.pen.width, 3 + 2 * 2);
  EXPECT_EQ(s.casing.pen.color.r, 255);

  // On a patterned line the casing measures from the PATTERN's pen, which is
  // where the width lives once the plain stroke has been turned off.
  GeoLineStyle d = fv::PresetGeoLine(fv::line_preset::kDash, kBlue, 3);
  fv::AddCasing(&d, kWhite, 2);
  EXPECT_EQ(d.casing.pen.width, 7);
}

// ---------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------

TEST(GeoDraw, DrawsAGeographicLineAndDashingInksLessThanSolid) {
  MapProjection proj = HarbourView();
  const GeoPoint a{32.73, -79.93};
  const GeoPoint b{32.77, -79.87};

  fv::CpuCanvas solid_c(400, 300);
  solid_c.Clear(kWhite);
  GeoDraw solid(proj, &solid_c);
  ASSERT_TRUE(
      solid.DrawGeoLine(a, b, LineKind::kSimple, fv::SolidGeoLine(kBlue, 1))
          .ok());
  const int solid_ink = InkCount(solid_c.Buffer());
  EXPECT_GT(solid_ink, 100);

  fv::CpuCanvas dash_c(400, 300);
  dash_c.Clear(kWhite);
  GeoDraw dash(proj, &dash_c);
  ASSERT_TRUE(dash.DrawGeoLine(a, b, LineKind::kSimple,
                               fv::PresetGeoLine(fv::line_preset::kDash, kBlue,
                                                 1))
                  .ok());
  const int dash_ink = InkCount(dash_c.Buffer());
  EXPECT_GT(dash_ink, 0);
  EXPECT_LT(dash_ink, solid_ink)
      << "a dashed line covers less of the same path than a solid one";
}

TEST(GeoDraw, TheCasingIsUnderTheLineAndShowsOutsideIt) {
  MapProjection proj = HarbourView();
  // A horizontal line across the middle, so "outside" is straight up and down.
  const GeoPoint a{32.75, -79.93};
  const GeoPoint b{32.75, -79.87};

  fv::CpuCanvas c(400, 300);
  c.Clear(kWhite);
  GeoDraw d(proj, &c);
  GeoLineStyle style = fv::SolidGeoLine(kBlue, 3);
  fv::AddCasing(&style, kWhite, 2);
  // A white casing on a white background is invisible, so this one is red.
  style.casing.pen.color = kRed;
  ASSERT_TRUE(d.DrawGeoLine(a, b, LineKind::kSimple, style).ok());

  const fv::PixelBuffer& b8 = c.Buffer();
  const int x = 200;
  int top = TopInkRow(b8, x);
  ASSERT_GE(top, 0);
  // The topmost ink in the column is the CASING, and the blue is inside it.
  EXPECT_TRUE(IsColor(b8, x, top, kRed)) << "the casing must be outermost";
  bool blue_below = false;
  for (int y = top; y < top + 10 && y < b8.Height(); ++y)
    if (IsColor(b8, x, y, kBlue)) blue_below = true;
  EXPECT_TRUE(blue_below) << "the line must be drawn over its own casing";
  EXPECT_GT(CountColor(b8, kRed), 0);
  EXPECT_GT(CountColor(b8, kBlue), 0);
}

TEST(GeoDraw, ADashedLinesCasingIsDashedToo) {
  // A SOLID casing under a dashed line reads as a solid white line with blue
  // dashes painted on it, which is not what "halo" means — so the casing pass
  // walks the same pattern. Counted rather than measured in pixels: at any
  // usable casing width the square nib at each dash end bridges most of the
  // gap, so the ink is a bad witness and the DRAW COUNT is the honest one.
  MapProjection proj = HarbourView();
  const GeoPoint a{32.75, -79.93};
  const GeoPoint b{32.75, -79.87};

  fv::CpuCanvas c(400, 300);
  c.Clear(kWhite);
  GeoDraw bare(proj, &c);
  GeoLineStyle s0 = fv::PresetGeoLine(fv::line_preset::kDash, kBlue, 2);
  ASSERT_TRUE(bare.DrawGeoLine(a, b, LineKind::kSimple, s0).ok());
  const size_t dashes = bare.draws_emitted();
  EXPECT_GT(dashes, 4u);

  c.Clear(kWhite);
  GeoDraw cased(proj, &c);
  GeoLineStyle s1 = s0;
  fv::AddCasing(&s1, kRed, 2);
  ASSERT_TRUE(cased.DrawGeoLine(a, b, LineKind::kSimple, s1).ok());
  EXPECT_EQ(cased.draws_emitted(), 2 * dashes)
      << "one casing dash per line dash — a solid casing would be ONE draw";

  // And the casing really is outside: red above the blue at the line's row.
  const int x = 200;
  const int top = TopInkRow(c.Buffer(), x);
  ASSERT_GE(top, 0);
  EXPECT_TRUE(IsColor(c.Buffer(), x, top, kRed));
}

TEST(GeoDraw, AGreatCircleIsDrawnNorthOfTheStraightLineBetweenTheSamePoints) {
  // The G1 property, asserted through the PIXELS this time — which is the
  // thing G3 adds and the thing risk 6.1 warns is silent when it breaks.
  MapProjection proj = WorldView();
  const GeoPoint a{40.0, -74.0};   // New York
  const GeoPoint b{40.0, -10.0};   // due east, same latitude

  auto top_at_centre = [&](LineKind kind) {
    fv::CpuCanvas c(400, 300);
    c.Clear(kWhite);
    GeoDraw d(proj, &c);
    EXPECT_TRUE(d.DrawGeoLine(a, b, kind, fv::SolidGeoLine(kBlue, 1)).ok());
    return TopInkRow(c.Buffer(), 200);
  };
  const int straight = top_at_centre(LineKind::kSimple);
  const int great = top_at_centre(LineKind::kGreatCircle);
  ASSERT_GE(straight, 0);
  ASSERT_GE(great, 0);
  EXPECT_LT(great, straight - 2)
      << "the great circle between two points at the same latitude bows "
         "poleward, which on screen is UP";
}

TEST(GeoDraw, ARailroadStampsCrosstiesThatReachBesideTheLine) {
  MapProjection proj = HarbourView();
  const GeoPoint a{32.75, -79.93};
  const GeoPoint b{32.75, -79.87};

  fv::BuiltinSymbolLibrary lib;
  lib.SetColor(kBlue);

  fv::CpuCanvas plain(400, 300);
  plain.Clear(kWhite);
  GeoDraw d0(proj, &plain, &lib);
  ASSERT_TRUE(
      d0.DrawGeoLine(a, b, LineKind::kSimple, fv::SolidGeoLine(kBlue, 1)).ok());

  fv::CpuCanvas rail(400, 300);
  rail.Clear(kWhite);
  GeoDraw d1(proj, &rail, &lib);
  ASSERT_TRUE(d1.DrawGeoLine(a, b, LineKind::kSimple,
                             fv::PresetGeoLine(fv::line_preset::kRailroad,
                                               kBlue, 1))
                  .ok());
  // The ties are perpendicular strokes, so the railroad's ink spreads over
  // more ROWS than a bare line does even though it covers less of the path.
  auto rows_with_ink = [](const fv::PixelBuffer& b) {
    int n = 0;
    for (int y = 0; y < b.Height(); ++y)
      for (int x = 0; x < b.Width(); ++x) {
        const unsigned char* px = b.Row(y) + 4 * x;
        if (px[0] != 255 || px[1] != 255 || px[2] != 255) {
          ++n;
          break;
        }
      }
    return n;
  };
  EXPECT_GT(rows_with_ink(rail.Buffer()), rows_with_ink(plain.Buffer()));
}

// ---------------------------------------------------------------------------
// Symbols
// ---------------------------------------------------------------------------

TEST(GeoDraw, StampsABuiltinSymbolAtItsGeographicPosition) {
  MapProjection proj = HarbourView();
  fv::BuiltinSymbolLibrary lib;
  lib.SetColor(kRed);

  fv::CpuCanvas c(400, 300);
  c.Clear(kWhite);
  GeoDraw d(proj, &c, &lib);
  ASSERT_TRUE(d.DrawSymbol(proj.Center(), fv::builtin_symbol::kCircle).ok());
  EXPECT_EQ(d.draws_emitted(), 1u);
  // The centre of the surface is the centre of the projection.
  EXPECT_TRUE(IsColor(c.Buffer(), 200, 150, kRed));
  // ... and a corner is not.
  EXPECT_TRUE(IsColor(c.Buffer(), 5, 5, kWhite));
}

TEST(GeoDraw, AMistypedSymbolIdIsAnErrorAndNotASilentNothing) {
  MapProjection proj = HarbourView();
  fv::BuiltinSymbolLibrary lib;
  fv::CpuCanvas c(400, 300);
  c.Clear(kWhite);
  GeoDraw d(proj, &c, &lib);
  fv::Status s = d.DrawSymbol(proj.Center(), "fv.no-such-symbol");
  EXPECT_EQ(s.code, fv::kNotFound);
  EXPECT_EQ(InkCount(c.Buffer()), 0);

  // No library at all is a different failure and is also loud.
  GeoDraw bare(proj, &c);
  EXPECT_FALSE(bare.DrawSymbol(proj.Center(), fv::builtin_symbol::kCircle).ok());
}

TEST(GeoDraw, SymbolScaleAndDpiScaleBothGrowTheStamp) {
  MapProjection proj = HarbourView();
  fv::BuiltinSymbolLibrary lib;
  lib.SetColor(kRed);

  auto ink_for = [&](double scale, double dpi) {
    fv::CpuCanvas c(400, 300);
    c.Clear(kWhite);
    GeoDraw d(proj, &c, &lib);
    d.SetSymbolScale(scale);
    d.SetSymbolDpiScale(dpi);
    EXPECT_TRUE(d.DrawSymbol(proj.Center(), fv::builtin_symbol::kSquare).ok());
    return InkCount(c.Buffer());
  };
  const int base = ink_for(1.0, 1.0);
  EXPECT_GT(ink_for(2.0, 1.0), base);
  // The DPI factor is the ledger's way out of the symbol-DPI defect, and it
  // has to be a real multiplier rather than a carried-and-ignored number.
  EXPECT_GT(ink_for(1.0, 2.0), base);
  EXPECT_EQ(ink_for(2.0, 1.0), ink_for(1.0, 2.0));
}

// ---------------------------------------------------------------------------
// Labels
// ---------------------------------------------------------------------------

TEST(GeoDraw, DrawsALabelWithAHaloAroundIt) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";
  MapProjection proj = HarbourView();

  fv::CpuCanvas plain(400, 300);
  plain.Clear(kWhite);
  fv::LabelStyle ls;
  ls.valid = true;
  ls.style.font_path = font;
  ls.style.size = 14.0;
  ls.style.color = FvColor{0, 0, 0, 255};

  GeoDraw d0(proj, &plain);
  ASSERT_TRUE(d0.DrawLabel(proj.Center(), "Fort Sumter", ls).ok());
  EXPECT_EQ(d0.halo_draws(), 0u);
  const int plain_ink = InkCount(plain.Buffer());
  EXPECT_GT(plain_ink, 0);

  fv::CpuCanvas haloed(400, 300);
  haloed.Clear(kWhite);
  ls.halo_width = 2.0;
  ls.halo_color = kRed;
  GeoDraw d1(proj, &haloed);
  ASSERT_TRUE(d1.DrawLabel(proj.Center(), "Fort Sumter", ls).ok());
  EXPECT_EQ(d1.halo_draws(), 8u) << "past one pixel the diagonals are added";
  EXPECT_EQ(d1.draws_emitted(), 1u) << "a haloed label is still ONE label";
  EXPECT_GT(CountColor(haloed.Buffer(), kRed), 0);
  EXPECT_GT(InkCount(haloed.Buffer()), plain_ink);
}

// The pixel proof for the along-path anchor. The placer tests in
// vector_renderer_test pin the arithmetic; this pins that the INK lands where
// the arithmetic says, through a real font and the real rasterizer — which is
// the half that was wrong on screen while every number was right.
TEST(GeoDraw, ACentredNameStraddlesItsLineInsteadOfStandingOnIt) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";
  MapProjection proj = HarbourView();

  // A horizontal road straight across the middle of the surface.
  const double road_y = 150.0;
  std::vector<std::vector<fv::SurfacePoint>> path{
      {{40.0, road_y}, {360.0, road_y}}};

  // Ink above the line, ink below it, and the vertical centre of the ink.
  struct Ink { int above = 0, below = 0; double mean_y = 0.0; };
  auto measure = [&](fv::LabelAlongAnchor anchor) {
    fv::CpuCanvas c(400, 300);
    c.Clear(kWhite);
    fv::LabelStyle ls;
    ls.valid = true;
    ls.style.font_path = font;
    ls.style.size = 20.0;
    ls.style.color = FvColor{0, 0, 0, 255};
    ls.placement = fv::LabelPlacement::kAlongPath;
    ls.along_anchor = anchor;
    GeoDraw d(proj, &c);
    EXPECT_TRUE(d.DrawLabelAlongPath(path, "EAST BAY STREET", ls).ok());
    Ink ink;
    double sum = 0.0;
    for (int y = 0; y < c.Buffer().Height(); ++y)
      for (int x = 0; x < c.Buffer().Width(); ++x) {
        const unsigned char* px = c.Buffer().Row(y) + 4 * x;
        if (px[0] == 255 && px[1] == 255 && px[2] == 255) continue;
        if (y < road_y) ++ink.above; else ++ink.below;
        sum += y;
      }
    const int n = ink.above + ink.below;
    EXPECT_GT(n, 0) << "the name left no ink at all";
    ink.mean_y = n > 0 ? sum / n : 0.0;
    return ink;
  };

  const Ink base = measure(fv::LabelAlongAnchor::kBaseline);
  const Ink mid = measure(fv::LabelAlongAnchor::kCenter);

  // THE BUG: baseline-anchored, essentially every pixel of the name is above
  // the road — it rides along the top edge. (Not exactly all: a descender and
  // the antialiasing of the baseline row itself fall below.)
  EXPECT_GT(base.above, 20 * base.below)
      << "baseline anchoring should put the name above its road";

  // THE FIX: centred, the road runs through the name. Neither side is starved,
  // and an all-caps string (no descenders) is close to an even split.
  EXPECT_GT(mid.above, mid.below / 2);
  EXPECT_GT(mid.below, mid.above / 2);

  // The ink's own centre lands within a pixel of the road, where before it sat
  // a third of a cap height clear of it.
  EXPECT_NEAR(mid.mean_y, road_y, 1.5);
  EXPECT_LT(base.mean_y, road_y - 3.0);
}

TEST(GeoDraw, LabelAlignmentMovesTheBoxAndTheDefaultCostsNoMeasurement) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";
  MapProjection proj = HarbourView();

  auto ink_left_edge = [&](fv::LabelHAlign h) {
    fv::CpuCanvas c(400, 300);
    c.Clear(kWhite);
    fv::LabelStyle ls;
    ls.valid = true;
    ls.style.font_path = font;
    ls.style.size = 14.0;
    ls.style.color = FvColor{0, 0, 0, 255};
    ls.halign = h;
    GeoDraw d(proj, &c);
    EXPECT_TRUE(d.DrawLabel(proj.Center(), "Sumter", ls).ok());
    for (int x = 0; x < c.Buffer().Width(); ++x)
      for (int y = 0; y < c.Buffer().Height(); ++y) {
        const unsigned char* px = c.Buffer().Row(y) + 4 * x;
        if (px[0] != 255 || px[1] != 255 || px[2] != 255) return x;
      }
    return -1;
  };
  const int left = ink_left_edge(fv::LabelHAlign::kLeft);
  const int centre = ink_left_edge(fv::LabelHAlign::kCenter);
  const int right = ink_left_edge(fv::LabelHAlign::kRight);
  ASSERT_GE(left, 0);
  EXPECT_LT(centre, left);
  EXPECT_LT(right, centre);
}

// ---------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------

TEST(GeoDraw, PickingIsOffByDefaultAndIndexesTheInkWhenTurnedOn) {
  MapProjection proj = HarbourView();
  const GeoPoint a{32.75, -79.93};
  const GeoPoint b{32.75, -79.87};

  fv::CpuCanvas c(400, 300);
  c.Clear(kWhite);
  GeoDraw off(proj, &c);
  ASSERT_TRUE(
      off.DrawGeoLine(a, b, LineKind::kSimple, fv::SolidGeoLine(kBlue, 3)).ok());
  EXPECT_TRUE(off.pick_index().empty())
      << "an overlay with its own analytic hit test should not pay for an "
         "index nobody reads";

  GeoDraw on(proj, &c);
  on.SetPickEnabled(true);
  on.SetFeature(42);
  ASSERT_TRUE(
      on.DrawGeoLine(a, b, LineKind::kSimple, fv::SolidGeoLine(kBlue, 3)).ok());
  ASSERT_FALSE(on.pick_index().empty());
  std::vector<fv::PickHit> hits = on.pick_index().HitTest(200, 150, 4.0);
  ASSERT_FALSE(hits.empty());
  EXPECT_EQ(hits.front().ref.feature, 42);
  // ... and well away from the line, nothing.
  EXPECT_TRUE(on.pick_index().HitTest(200, 40, 4.0).empty());
}

TEST(GeoDraw, ASymbolIsPickableOverItsGlyphAndNotJustItsAnchorPixel) {
  MapProjection proj = HarbourView();
  fv::BuiltinSymbolLibrary lib;
  fv::CpuCanvas c(400, 300);
  c.Clear(kWhite);
  GeoDraw d(proj, &c, &lib);
  d.SetPickEnabled(true);
  d.SetFeature(7);
  ASSERT_TRUE(d.DrawSymbol(proj.Center(), fv::builtin_symbol::kSquare).ok());
  // Three pixels off the anchor is still on the 9-px square.
  std::vector<fv::PickHit> hits = d.pick_index().HitTest(203, 152, 0.0);
  ASSERT_FALSE(hits.empty());
  EXPECT_EQ(hits.front().ref.feature, 7);
}

// ---------------------------------------------------------------------------
// Render state (G4)
// ---------------------------------------------------------------------------
//
// The property under test throughout is the one the hand-swapped colour got
// wrong: a highlighted thing must still be DRAWN AS ITSELF. So every
// assertion below compares the two states over the same call and asks what
// changed — nothing about the feature's own ink, something around it.

using fv::RenderState;

// The bounding box of everything that is not the background.
struct InkBounds {
  int minx = 0, miny = 0, maxx = 0, maxy = 0;
  bool any = false;
  int width() const { return any ? maxx - minx + 1 : 0; }
  int height() const { return any ? maxy - miny + 1 : 0; }
};

InkBounds BoundsOfInk(const fv::PixelBuffer& b) {
  InkBounds r;
  for (int y = 0; y < b.Height(); ++y)
    for (int x = 0; x < b.Width(); ++x) {
      const unsigned char* px = b.Row(y) + 4 * x;
      if (px[0] == 255 && px[1] == 255 && px[2] == 255) continue;
      if (!r.any) {
        r.minx = r.maxx = x;
        r.miny = r.maxy = y;
        r.any = true;
      } else {
        r.minx = std::min(r.minx, x);
        r.maxx = std::max(r.maxx, x);
        r.miny = std::min(r.miny, y);
        r.maxy = std::max(r.maxy, y);
      }
    }
  return r;
}

const FvColor kYellow{255, 220, 0, 255};

TEST(GeoDrawState, AHighlightedSymbolKeepsEveryPixelOfItsOwnColour) {
  MapProjection proj = HarbourView();
  fv::BuiltinSymbolLibrary lib;
  lib.SetColor(kRed);

  auto render = [&](RenderState st, fv::CpuCanvas* c) {
    c->Clear(kWhite);
    GeoDraw d(proj, c, &lib);
    d.SetState(st);
    EXPECT_TRUE(d.DrawSymbol(proj.Center(), fv::builtin_symbol::kSquare).ok());
    return d.draws_emitted();
  };

  fv::CpuCanvas plain(400, 300), lit(400, 300);
  const size_t n0 = render(RenderState::kNormal, &plain);
  const size_t n1 = render(RenderState::kHighlighted, &lit);

  // THE POINT OF G4. Before it, selection swapped the fill colour and the
  // marker stopped saying which route or which category it belonged to.
  EXPECT_EQ(CountColor(plain.Buffer(), kRed), CountColor(lit.Buffer(), kRed))
      << "the highlight goes UNDER the symbol; not one pixel of the symbol's "
         "own colour may be traded for it";
  EXPECT_EQ(CountColor(plain.Buffer(), kYellow), 0);
  EXPECT_GT(CountColor(lit.Buffer(), kYellow), 0);
  EXPECT_EQ(n0, n1) << "a highlight is not a draw the caller asked for";

  // ... and it is a ring AROUND the symbol, not a wash over it.
  const InkBounds b0 = BoundsOfInk(plain.Buffer());
  const InkBounds b1 = BoundsOfInk(lit.Buffer());
  EXPECT_GT(b1.width(), b0.width());
  EXPECT_GT(b1.height(), b0.height());
  EXPECT_TRUE(IsColor(lit.Buffer(), b1.minx + 1, (b1.miny + b1.maxy) / 2,
                      kYellow))
      << "the outermost ink of a highlighted symbol is the highlight";
}

TEST(GeoDrawState, AHighlightIsCountedApartAndIsNotPickable) {
  MapProjection proj = HarbourView();
  fv::BuiltinSymbolLibrary lib;
  fv::CpuCanvas c(400, 300);
  c.Clear(kWhite);

  GeoDraw d(proj, &c, &lib);
  d.SetPickEnabled(true);
  d.SetFeature(7);
  ASSERT_TRUE(d.DrawSymbol(proj.Center(), fv::builtin_symbol::kSquare).ok());
  const size_t normal_shapes = d.pick_index().shape_count();
  EXPECT_EQ(d.highlight_draws(), 0u);

  d.ClearPick();
  d.ResetCounts();
  d.SetState(RenderState::kHighlighted);
  ASSERT_TRUE(d.DrawSymbol(proj.Center(), fv::builtin_symbol::kSquare).ok());
  EXPECT_GT(d.highlight_draws(), 0u);
  EXPECT_EQ(d.draws_emitted(), 1u);
  // The user aims at the waypoint, not at its glow: a selected feature must
  // not become a bigger target than an unselected one, or a click near two
  // markers would prefer whichever is already selected.
  EXPECT_EQ(d.pick_index().shape_count(), normal_shapes);
}

TEST(GeoDrawState, TheStateIsPerDrawAndSettingItBackRestoresNormal) {
  MapProjection proj = HarbourView();
  fv::BuiltinSymbolLibrary lib;
  fv::CpuCanvas c(400, 300);
  c.Clear(kWhite);
  GeoDraw d(proj, &c, &lib);
  EXPECT_EQ(d.state(), RenderState::kNormal);

  d.SetState(RenderState::kHighlighted);
  ASSERT_TRUE(d.DrawSymbolAtPixel(100, 100, fv::builtin_symbol::kSquare).ok());
  const size_t lit = d.highlight_draws();
  EXPECT_GT(lit, 0u);

  d.SetState(RenderState::kNormal);
  ASSERT_TRUE(d.DrawSymbolAtPixel(300, 100, fv::builtin_symbol::kSquare).ok());
  EXPECT_EQ(d.highlight_draws(), lit) << "the state applies until it is set "
                                         "back, and then it stops applying";
}

TEST(GeoDrawState, AHighlightedLineIsWiderAndTheHighlightIsOutsideTheLine) {
  MapProjection proj = HarbourView();
  const GeoPoint a{32.75, -79.93};
  const GeoPoint b{32.75, -79.87};

  auto render = [&](RenderState st, fv::CpuCanvas* c) {
    c->Clear(kWhite);
    GeoDraw d(proj, c);
    d.SetState(st);
    EXPECT_TRUE(d.DrawGeoLine(a, b, LineKind::kSimple,
                              fv::SolidGeoLine(kBlue, 3)).ok());
  };

  fv::CpuCanvas plain(400, 300), lit(400, 300);
  render(RenderState::kNormal, &plain);
  render(RenderState::kHighlighted, &lit);

  const int x = 200;
  const int top0 = TopInkRow(plain.Buffer(), x);
  const int top1 = TopInkRow(lit.Buffer(), x);
  ASSERT_GE(top0, 0);
  ASSERT_GE(top1, 0);
  EXPECT_LT(top1, top0) << "the highlight shows past the line";
  EXPECT_TRUE(IsColor(lit.Buffer(), x, top1, kYellow));
  // ... and the line itself is still blue, at the same place.
  EXPECT_TRUE(IsColor(lit.Buffer(), x, (top0 + top0 + 3) / 2, kBlue));
}

TEST(GeoDrawState, AHighlightedPatternedLineGlowsAroundItsStampsToo) {
  MapProjection proj = HarbourView();
  const GeoPoint a{32.75, -79.93};
  const GeoPoint b{32.75, -79.87};
  fv::BuiltinSymbolLibrary lib;
  lib.SetColor(kBlue);

  auto render = [&](RenderState st, fv::CpuCanvas* c) {
    c->Clear(kWhite);
    GeoDraw d(proj, c, &lib);
    d.SetState(st);
    EXPECT_TRUE(d.DrawGeoLine(a, b, LineKind::kSimple,
                              fv::PresetGeoLine(fv::line_preset::kRailroad,
                                                kBlue, 2))
                    .ok());
  };

  fv::CpuCanvas plain(400, 300), lit(400, 300);
  render(RenderState::kNormal, &plain);
  render(RenderState::kHighlighted, &lit);

  // A railroad's CROSSTIES are what reach furthest from the rail, so the ink
  // can only get taller if the stamps grew and took the highlight colour too.
  // Widening the rail alone would leave this number exactly where it was.
  EXPECT_GT(BoundsOfInk(lit.Buffer()).height(),
            BoundsOfInk(plain.Buffer()).height());
  EXPECT_GT(CountColor(lit.Buffer(), kYellow), 0);
}

// A one-tile library, because the interesting half of the tint is the RASTER
// half: a display list has colours to replace, a tile has only pixels, and an
// icon set is black-on-transparent so an untinted halo would be a black blob.
class OneTileLibrary : public fv::ISymbolLibrary {
 public:
  OneTileLibrary() {
    pix_.tile = fv::PixelBuffer(8, 8);
    for (int y = 0; y < 8; ++y) {
      unsigned char* row = pix_.tile.Row(y);
      for (int x = 0; x < 8; ++x) {
        // A solid black disc-ish blob, transparent outside it.
        const bool inside = (x >= 2 && x <= 5 && y >= 2 && y <= 5);
        row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = 0;
        row[x * 4 + 3] = inside ? 255 : 0;
      }
    }
    pix_.pivot_x = pix_.pivot_y = 4.0;
  }
  const fv::VectorSymbol* Symbol(const std::string&) override {
    return nullptr;
  }
  const fv::SymbolPixmap* Pixmap(const std::string& id) override {
    return id == "tile" ? &pix_ : nullptr;
  }

 private:
  fv::SymbolPixmap pix_;
};

TEST(GeoDrawState, AHighlightedPixmapIsTintedByAlphaAndKeepsItsOwnPixels) {
  MapProjection proj = HarbourView();
  OneTileLibrary lib;
  const FvColor kBlack{0, 0, 0, 255};

  auto render = [&](RenderState st, fv::CpuCanvas* c) {
    c->Clear(kWhite);
    GeoDraw d(proj, c, &lib);
    d.SetState(st);
    EXPECT_TRUE(d.DrawSymbol(proj.Center(), "tile").ok());
  };

  fv::CpuCanvas plain(400, 300), lit(400, 300);
  render(RenderState::kNormal, &plain);
  render(RenderState::kHighlighted, &lit);

  EXPECT_EQ(CountColor(plain.Buffer(), kBlack),
            CountColor(lit.Buffer(), kBlack))
      << "the tile itself is drawn last and untouched";
  EXPECT_GT(CountColor(lit.Buffer(), kYellow), 0)
      << "a tint recolours RGB and keeps alpha, so the glow is the tile's "
         "SHAPE and not its bounding box";
  // The tile's transparent border must stay transparent in the tint too: a
  // tint that ignored alpha would paint an 8x8 yellow square, which at these
  // offsets would be a solid block rather than a ring.
  const InkBounds b = BoundsOfInk(lit.Buffer());
  EXPECT_TRUE(IsColor(lit.Buffer(), (b.minx + b.maxx) / 2,
                      (b.miny + b.maxy) / 2, kBlack))
      << "the centre is still the symbol";
}

TEST(GeoDrawState, AHighlightedLabelKeepsItsOwnHaloAndGainsOneOutsideIt) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";
  MapProjection proj = HarbourView();

  fv::LabelStyle ls;
  ls.valid = true;
  ls.style.font_path = font;
  ls.style.size = 18.0;
  ls.style.color = FvColor{0, 0, 0, 255};
  ls.halo_width = 2.0;
  ls.halo_color = kWhite;

  fv::CpuCanvas c(400, 300);
  c.Clear(kBlue);  // so a WHITE halo is visible as ink of its own
  GeoDraw d(proj, &c);
  d.SetState(RenderState::kHighlighted);
  ASSERT_TRUE(d.DrawLabel(proj.Center(), "Fort Sumter", ls).ok());

  EXPECT_EQ(d.draws_emitted(), 1u) << "a highlighted label is still ONE label";
  EXPECT_EQ(d.halo_draws(), 8u) << "the label's own halo is untouched";
  EXPECT_EQ(d.highlight_draws(), 8u);
  EXPECT_GT(CountColor(c.Buffer(), kWhite), 0) << "legibility is not traded "
                                                  "for selection";
  EXPECT_GT(CountColor(c.Buffer(), kYellow), 0);
}

// ---------------------------------------------------------------------------
// PR2 — GeoDraw on a turned chart
// ---------------------------------------------------------------------------
//
// Three rules, one test each, plus the pick index that follows for free.

// A library with one deliberately LOPSIDED symbol: a bar from the origin out
// along +y, which is UP on screen. The builtins are all symmetric about at
// least one axis, and a bounding box cannot see which end of a symmetric
// symbol turned.
class BarLibrary : public fv::ISymbolLibrary {
 public:
  BarLibrary() {
    fv::SymbolPrimitive bar;
    bar.type = fv::SymbolPrimitiveType::kPolygon;
    bar.points = {{-20, 0}, {20, 0}, {20, 600}, {-20, 600}};
    bar.has_fill = true;
    bar.fill_color = kBlue;
    bar_.primitives.push_back(bar);
  }
  const fv::VectorSymbol* Symbol(const std::string& id) override {
    return id == "bar" ? &bar_ : nullptr;
  }

 private:
  fv::VectorSymbol bar_;
};

MapProjection TurnedHarbour(double rotation_deg) {
  MapProjection p = HarbourView();
  EXPECT_TRUE(p.SetRotation(rotation_deg).ok());
  return p;
}

TEST(GeoDrawRotation, AGeoAnchoredSymbolTurnsAndAPixelAnchoredOneDoesNot) {
  // THE WHOLE OF PR2'S SYMBOL RULE, in one comparison. Both calls stamp the
  // same bar at the same pixel — the surface centre, which is the pivot, so
  // the anchor cannot move and only the ANGLE is under test.
  BarLibrary lib;

  auto stamp = [&](double rotation_deg, bool geo) {
    MapProjection proj = TurnedHarbour(rotation_deg);
    fv::CpuCanvas c(400, 300);
    c.Clear(kWhite);
    GeoDraw d(proj, &c, &lib);
    if (geo)
      EXPECT_TRUE(d.DrawSymbol(proj.Center(), "bar").ok());
    else
      EXPECT_TRUE(d.DrawSymbolAtPixel(199.5, 149.5, "bar").ok());
    return BoundsOfInk(c.Buffer());
  };

  // Unturned, both forms lay the bar upward: tall, narrow, and above centre.
  for (const bool geo : {true, false}) {
    const InkBounds up = stamp(0.0, geo);
    ASSERT_TRUE(up.any);
    EXPECT_GT(up.height(), up.width()) << "geo=" << geo;
    EXPECT_LT(up.miny, 140) << "geo=" << geo << ": the bar points up";
  }

  // Turn the chart a quarter clockwise. The GEOGRAPHIC anchor's angle is
  // measured against the chart's north, so the bar goes with it.
  const InkBounds turned_geo = stamp(90.0, true);
  ASSERT_TRUE(turned_geo.any);
  EXPECT_GT(turned_geo.width(), turned_geo.height());
  EXPECT_GT(turned_geo.maxx, 210) << "north is now to the right of the screen";

  // The PIXEL anchor's angle is a screen angle and the chart's turn is none of
  // its business — this is what keeps a scale bar level and what lets MM4's
  // ownship keep drawing at its own `screen_angle_deg`.
  const InkBounds turned_px = stamp(90.0, false);
  ASSERT_TRUE(turned_px.any);
  EXPECT_GT(turned_px.height(), turned_px.width())
      << "map furniture does not turn with the map";
  EXPECT_LT(turned_px.miny, 140);
}

TEST(GeoDrawRotation, APatternStampIsNotTurnedTWICE) {
  // A pattern symbol's angle comes from the PROJECTED tangent, and the
  // projection is already turned — so applying the chart's rotation here as
  // well would turn the stamp twice. The setup makes the double count visible
  // rather than subtle: an EAST-WEST line on an unturned chart and a
  // NORTH-SOUTH line on one turned 90 clockwise project to the SAME horizontal
  // screen line (HarbourView's dpp is equal on both axes), so the two pictures
  // must agree. A tick lying along its own line instead of across it would
  // collapse the ink's height, and that is exactly what the second rotation
  // would do.
  fv::BuiltinSymbolLibrary lib;
  const GeoLineStyle style = fv::PresetGeoLine(fv::line_preset::kTick, kBlue, 2);
  const GeoPoint c{32.75, -79.90};

  auto draw = [&](double rotation_deg, const GeoPoint& from,
                  const GeoPoint& to) {
    MapProjection proj = TurnedHarbour(rotation_deg);
    fv::CpuCanvas canvas(400, 300);
    canvas.Clear(kWhite);
    GeoDraw d(proj, &canvas, &lib);
    EXPECT_TRUE(d.DrawGeoLine(from, to, LineKind::kSimple, style).ok());
    return BoundsOfInk(canvas.Buffer());
  };

  const InkBounds east = draw(0.0, GeoPoint{c.lat, c.lon - 0.02},
                              GeoPoint{c.lat, c.lon + 0.02});
  ASSERT_TRUE(east.any);
  ASSERT_GT(east.width(), east.height()) << "the line itself runs across";
  ASSERT_GT(east.height(), 4) << "the ticks stand out from the line";

  // Same line in the other frame. Within a pixel or two: the two paths are
  // the same length on screen but the rasteriser is filling a different set of
  // pixels, and the point of the test is the SHAPE, not a hash.
  const InkBounds turned = draw(90.0, GeoPoint{c.lat - 0.02, c.lon},
                                GeoPoint{c.lat + 0.02, c.lon});
  ASSERT_TRUE(turned.any);
  EXPECT_NEAR(turned.width(), east.width(), 2);
  EXPECT_NEAR(turned.height(), east.height(), 2)
      << "the ticks were turned a second time and lay down along the line";
}

TEST(GeoDrawRotation, APointLabelStaysUprightAndAnAlongPathOneFollowsTheTurn) {
  // FALCONVIEW'S OWN BEHAVIOUR, and the reason the declined "rotate the
  // rendered image" route was declined: a place name is for READING, so it
  // stays level however the chart is turned, while a name that belongs to a
  // road goes round the bend with the road.
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  fv::LabelStyle ls;
  ls.valid = true;
  ls.style.font_path = font;
  ls.style.size = 18.0;
  ls.style.color = FvColor{0, 0, 0, 255};

  // A point label, at 0 and at 45 degrees. A string is much wider than it is
  // tall, and it stays that way.
  for (const double deg : {0.0, 45.0, 90.0}) {
    MapProjection proj = TurnedHarbour(deg);
    fv::CpuCanvas c(400, 300);
    c.Clear(kWhite);
    GeoDraw d(proj, &c);
    ASSERT_TRUE(d.DrawLabel(proj.Center(), "Fort Sumter", ls).ok());
    const InkBounds t = BoundsOfInk(c.Buffer());
    ASSERT_TRUE(t.any) << deg;
    EXPECT_GT(t.width(), 3 * t.height())
        << "a point label at " << deg << " degrees is not standing on its end";
  }

  // The same name along a NORTH-SOUTH road, which on an unturned chart reads
  // vertically. Turn the chart 90 clockwise and the road — and the name on it
  // — lie across the screen. The path is projected through the turned
  // projection, which is the whole mechanism.
  ls.placement = fv::LabelPlacement::kAlongPath;
  auto along = [&](double rotation_deg) {
    MapProjection proj = TurnedHarbour(rotation_deg);
    const GeoPoint a{32.75 - 0.02, -79.90};
    const GeoPoint b{32.75 + 0.02, -79.90};
    fv::SurfacePoint p0, p1;
    EXPECT_TRUE(proj.GeoToSurface(a, &p0.x, &p0.y).ok());
    EXPECT_TRUE(proj.GeoToSurface(b, &p1.x, &p1.y).ok());
    fv::CpuCanvas c(400, 300);
    c.Clear(kWhite);
    GeoDraw d(proj, &c);
    EXPECT_TRUE(d.DrawLabelAlongPath({{p0, p1}}, "BAY ST", ls).ok());
    return BoundsOfInk(c.Buffer());
  };

  const InkBounds up_the_screen = along(0.0);
  ASSERT_TRUE(up_the_screen.any);
  EXPECT_GT(up_the_screen.height(), up_the_screen.width())
      << "a north-south road's name reads up the screen";

  const InkBounds across = along(90.0);
  ASSERT_TRUE(across.any);
  EXPECT_GT(across.width(), across.height())
      << "turn the chart and the road's name turns with the road";
}

TEST(GeoDrawRotation, ThePickBoxFollowsTheTurnedInk) {
  // Rotation lives in the projection, so the index built from emitted ink
  // agrees with the ink without being told anything. Free, and therefore
  // worth pinning before something makes it not free.
  BarLibrary lib;
  MapProjection proj = TurnedHarbour(90.0);
  fv::CpuCanvas c(400, 300);
  c.Clear(kWhite);
  GeoDraw d(proj, &c, &lib);
  d.SetPickEnabled(true);
  d.SetFeature(42);
  // 0.01 degrees NORTH of centre. A quarter turn clockwise puts north to the
  // RIGHT, so the symbol is drawn right of centre and nowhere above it.
  const GeoPoint north{32.76, -79.90};
  ASSERT_TRUE(d.DrawSymbol(north, "bar").ok());

  double sx = 0.0, sy = 0.0;
  ASSERT_TRUE(proj.GeoToSurface(north, &sx, &sy).ok());
  EXPECT_GT(sx, 210.0) << "the anchor itself swung to the right";
  EXPECT_NEAR(sy, 149.5, 1.0);

  auto hits = d.pick_index().HitTest(static_cast<int>(sx),
                                     static_cast<int>(sy), 2.0);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].ref.feature, 42);
  // And nothing is indexed where an UNTURNED chart would have drawn it.
  EXPECT_TRUE(d.pick_index().HitTest(200, 100, 2.0).empty());
}

}  // namespace
