// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

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

}  // namespace
