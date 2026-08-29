// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// BuiltinSymbolLibrary (draw plan G2) — the symbols the port authors itself.
//
// These are VectorSymbol LITERALS, not data files: they scale and rotate, they
// need no directory, no sheet and no install step, and an overlay that draws a
// waypoint marker or a line decoration should not first have to find GeoSym on
// disk. Two sets:
//
//   the SHAPES  — the six PointOverlay draws by hand today (circle, square,
//                 triangle, diamond, cross, star), so a marker becomes an id
//                 rather than a switch statement;
//   the DECORATIONS — the stamps G3's line presets need (tick, arrowhead,
//                 crosstie, tee, notch), which is how LineSegmentRenderer.cpp's
//                 15 classes reduce to a table over PathRun{kSymbol}.
//
// Plus a north arrow and a crosshair, which every map application wants and
// neither chart product ships.
//
// UNITS: the default 1/100-inch grid (himetric_per_symbol_pixel() == 25.4), so
// a symbol authored as "9 nominal pixels wide" is 9*25.4 HIMETRIC wide and
// composes with GeoSym's without a second number. See CompositeSymbolLibrary
// on why that matters.
//
// ORIENTATION: symbol space is y UP (the seam's rule), so a north arrow points
// at +y and a triangle "point up" has its apex at +y. The drawer flips.

#ifndef FVKIT_SYMBOL_BUILTIN_H_
#define FVKIT_SYMBOL_BUILTIN_H_

#include <map>
#include <string>

#include "fvkit/symbol/library.h"

namespace fv {

// Every builtin is authored inside a box this many NOMINAL PIXELS across,
// centred on the symbol's own origin. Nine because that is PointOverlay's
// default size_px and the smallest box the pick index hands back, so a builtin
// at scale 1 is exactly a point-overlay marker — and a caller that wants a
// marker `n` pixels wide asks for scale n / this.
constexpr double kBuiltinSymbolNominalPx = 9.0;

// The ids this library answers to. Kept as an enum beside the strings so a
// caller can spell one without a typo, and so the test can enumerate them.
namespace builtin_symbol {

// Shapes (PointOverlay's six).
constexpr const char* kCircle = "fv.circle";
constexpr const char* kSquare = "fv.square";
constexpr const char* kTriangle = "fv.triangle";
constexpr const char* kDiamond = "fv.diamond";
constexpr const char* kCross = "fv.cross";
constexpr const char* kStar = "fv.star";

// Line decorations (G3's presets stamp these along a path).
constexpr const char* kTick = "fv.tick";        // one stroke across the line
constexpr const char* kArrowhead = "fv.arrow";  // open V, pointing +x
constexpr const char* kCrosstie = "fv.crosstie";  // railroad sleeper
constexpr const char* kTee = "fv.tee";          // FEBA/FLOT T-mark
constexpr const char* kNotch = "fv.notch";      // half-tick to one side

// Map furniture.
constexpr const char* kNorthArrow = "fv.north";
constexpr const char* kCrosshair = "fv.crosshair";

// The moving map's own ship (nav plan MM4). An aircraft in plan view, nose at
// +y, so a stamp rotated to the heading points where the platform is going.
// It is the only builtin authored to be drawn LARGER than the shape box (an
// ownship is furniture the user's eye returns to, not a marker in a set), and
// a caller whose platform is not an aircraft asks for `fv.north` instead — a
// plain chevron is the generic form and there is no reason to author it twice.
constexpr const char* kOwnship = "fv.ownship";

// Every id above, in this order. Terminated by nullptr.
extern const char* const kAll[];

}  // namespace builtin_symbol

// The builtins, built on first use and cached. Filled shapes carry a fill AND
// a stroke of the same colour, which is what makes them read at 9 px.
//
// COLOUR IS NOT A PROPERTY OF A BUILTIN. A display list carries its own
// colours and there is no per-draw tint at this seam, so the builtins are
// authored in ONE colour and the library is asked for a recolour instead:
// `SetColor` re-bakes every symbol. That is a library-wide setting rather than
// a per-id one because the caller that wants two colours wants two libraries,
// and two of these cost nothing (there are no files to re-read).
//
// A re-bake REWRITES the display lists in place and does not drop them, so a
// VectorSymbol* taken before a SetColor is still valid after it — the base
// class's lifetime promise holds. What it points AT changes, which is the
// point of asking.
class BuiltinSymbolLibrary : public ISymbolLibrary {
 public:
  BuiltinSymbolLibrary();

  // Ink colour for every symbol. Default: opaque black.
  void SetColor(FvColor c);
  FvColor color() const { return color_; }

  // Stroke width in NOMINAL PIXELS (the drawer multiplies by the same factor
  // it uses on coordinates). Default 1.0. Zero asks for the thinnest line the
  // device can draw, which is what the display list's 0 already means.
  void SetStrokeWidth(double nominal_px);
  double stroke_width() const { return stroke_px_; }

  const VectorSymbol* Symbol(const std::string& symbol_id) override;

 private:
  void Rebuild();

  std::map<std::string, VectorSymbol> symbols_;
  FvColor color_{0, 0, 0, 255};
  double stroke_px_ = 1.0;
};

}  // namespace fv

#endif  // FVKIT_SYMBOL_BUILTIN_H_
