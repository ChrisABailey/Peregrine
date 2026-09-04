// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/symbol/builtin.h"

#include <cmath>
#include <vector>

namespace fv {

namespace builtin_symbol {

const char* const kAll[] = {
    kCircle, kSquare,   kTriangle, kDiamond,  kCross,     kStar,
    kTick,   kArrowhead, kCrosstie, kTee,     kNotch,
    kNorthArrow, kCrosshair, kOwnship, nullptr};

}  // namespace builtin_symbol

namespace {

// HIMETRIC per nominal pixel on the default grid — the same 25.4 the base
// class reports, spelled once so the two cannot drift apart.
constexpr double kUnit = 25.4;

// The authoring box, from the header so a caller can size against it.
constexpr double kNominal = kBuiltinSymbolNominalPx;
constexpr double kR = kNominal * kUnit / 2.0;  // half-width, HIMETRIC

const double kPi = 3.14159265358979323846;

SymbolPoint P(double x, double y) { return SymbolPoint{x, y}; }

// A closed filled+edged ring.
SymbolPrimitive Ring(std::vector<SymbolPoint> pts, FvColor c, double w) {
  SymbolPrimitive p;
  p.type = SymbolPrimitiveType::kPolygon;
  p.points = std::move(pts);
  p.has_fill = true;
  p.fill_color = c;
  p.has_stroke = true;
  p.stroke_color = c;
  p.stroke_width = w;
  return p;
}

// An open stroked run.
SymbolPrimitive Line(std::vector<SymbolPoint> pts, FvColor c, double w) {
  SymbolPrimitive p;
  p.type = SymbolPrimitiveType::kPolyline;
  p.points = std::move(pts);
  p.has_stroke = true;
  p.stroke_color = c;
  p.stroke_width = w;
  return p;
}

// Recomputes min/max over whatever the primitives actually reach. Bounds are
// informational at this seam, but a caller that asks (a future symbol atlas,
// a placer sizing a run) must not be told zero.
void SetBounds(VectorSymbol* s) {
  bool any = false;
  auto add = [&](double x, double y) {
    if (!any) {
      s->min_x = s->max_x = x;
      s->min_y = s->max_y = y;
      any = true;
      return;
    }
    s->min_x = std::min(s->min_x, x);
    s->max_x = std::max(s->max_x, x);
    s->min_y = std::min(s->min_y, y);
    s->max_y = std::max(s->max_y, y);
  };
  for (const SymbolPrimitive& p : s->primitives) {
    for (const SymbolPoint& pt : p.points) add(pt.x, pt.y);
    if (p.type == SymbolPrimitiveType::kEllipse) {
      const double rx = std::hypot(p.radius1.x, p.radius1.y);
      const double ry = std::hypot(p.radius2.x, p.radius2.y);
      add(p.center.x - rx, p.center.y - ry);
      add(p.center.x + rx, p.center.y + ry);
    }
  }
}

VectorSymbol One(std::vector<SymbolPrimitive> prims) {
  VectorSymbol s;
  s.primitives = std::move(prims);
  SetBounds(&s);
  return s;
}

}  // namespace

BuiltinSymbolLibrary::BuiltinSymbolLibrary() { Rebuild(); }

void BuiltinSymbolLibrary::SetColor(FvColor c) {
  color_ = c;
  Rebuild();
}

void BuiltinSymbolLibrary::SetStrokeWidth(double nominal_px) {
  stroke_px_ = nominal_px > 0.0 ? nominal_px : 0.0;
  Rebuild();
}

const VectorSymbol* BuiltinSymbolLibrary::Symbol(const std::string& id) {
  auto it = symbols_.find(id);
  return it == symbols_.end() ? nullptr : &it->second;
}

void BuiltinSymbolLibrary::Rebuild() {
  // NOT symbols_.clear(). The id set is fixed, so every assignment below
  // overwrites an existing node in place and a std::map node does not move —
  // which is what keeps ISymbolLibrary's promise that a pointer handed out by
  // Symbol() stays valid until the library is destroyed. Clearing here would
  // dangle exactly the pointer the resolve-once-stamp-many pattern holds.
  const FvColor c = color_;
  const double w = stroke_px_ * kUnit;
  namespace bs = builtin_symbol;

  // --- shapes (PointOverlay's six) ------------------------------------------
  // Each is inscribed in the same circle of radius kR, so one `size` reads as
  // one size across all of them — the property point_overlay.cpp's ShapeRing
  // comment calls out for the triangle and which the star breaks on purpose
  // (its inner vertices are at 0.42, the ratio that makes a five-point star
  // look like one).
  {
    SymbolPrimitive e;
    e.type = SymbolPrimitiveType::kEllipse;
    e.center = P(0, 0);
    e.radius1 = P(kR, 0);
    e.radius2 = P(0, kR);
    e.has_fill = true;
    e.fill_color = c;
    e.has_stroke = true;
    e.stroke_color = c;
    e.stroke_width = w;
    symbols_[bs::kCircle] = One({e});
  }
  symbols_[bs::kSquare] =
      One({Ring({P(-kR, -kR), P(kR, -kR), P(kR, kR), P(-kR, kR)}, c, w)});
  symbols_[bs::kTriangle] = One({Ring(
      {P(0, kR), P(kR * 0.866, -kR * 0.5), P(-kR * 0.866, -kR * 0.5)}, c, w)});
  symbols_[bs::kDiamond] =
      One({Ring({P(0, kR), P(kR, 0), P(0, -kR), P(-kR, 0)}, c, w)});
  // The cross is two strokes, not a ring: it has no interior to fill.
  symbols_[bs::kCross] = One({Line({P(-kR, 0), P(kR, 0)}, c, w),
                              Line({P(0, -kR), P(0, kR)}, c, w)});
  {
    std::vector<SymbolPoint> star;
    star.reserve(10);
    for (int i = 0; i < 10; ++i) {
      // Start at the top (+y in symbol space) and alternate outer/inner.
      const double a = kPi / 2.0 - i * kPi / 5.0;
      const double rr = (i % 2 == 0) ? kR : kR * 0.42;
      star.push_back(P(rr * std::cos(a), rr * std::sin(a)));
    }
    symbols_[bs::kStar] = One({Ring(std::move(star), c, w)});
  }

  // --- line decorations -----------------------------------------------------
  // All authored to be stamped by PlaceAlongPath with rotation = the path
  // tangent, so +x is ALONG the line and +y is to its LEFT. That convention is
  // the whole reason LineSegmentRenderer's 15 classes reduce to a table: a
  // railroad is crossties every N px, a FEBA is tees, an arrowed route is
  // arrowheads.
  symbols_[bs::kTick] = One({Line({P(0, -kR), P(0, kR)}, c, w)});
  symbols_[bs::kNotch] = One({Line({P(0, 0), P(0, kR)}, c, w)});
  symbols_[bs::kCrosstie] =
      One({Line({P(0, -kR * 0.7), P(0, kR * 0.7)}, c, w)});
  // A tee's stem sits on the line and its bar stands to the left, which is the
  // side a FEBA/FLOT symbol is drawn on.
  symbols_[bs::kTee] = One({Line({P(0, 0), P(0, kR)}, c, w),
                            Line({P(-kR * 0.5, kR), P(kR * 0.5, kR)}, c, w)});
  // An open V, apex forward. Open rather than filled so it reads at 9 px with
  // a one-pixel pen, and so it does not need the fill colour to differ.
  symbols_[bs::kArrowhead] =
      One({Line({P(-kR, kR * 0.6), P(kR * 0.6, 0), P(-kR, -kR * 0.6)}, c, w)});

  // --- map furniture --------------------------------------------------------
  // North points at +y (symbol space is y up), and the arrow is drawn LARGER
  // than the shape box because it is furniture rather than a marker.
  //
  // IT IS AS WIDE AS 0.73 OF ITS LENGTH, and the first authoring was 0.28
  // (Chris, 2026-09-02: "it appears too narrow for its length"). The needle
  // was defensible for a compass rose in a corner and is wrong for the thing
  // that actually draws it: Pippin stamps this at the ownship, where a rider
  // glances down at a red mark on a moving chart and has to read WHICH WAY IT
  // POINTS in that glance. A dart four times longer than it is wide reads as a
  // line segment, and a line segment has two ends.
  //
  // The proportions are the ones already on the screen beside it — Apple's
  // `location.fill` on the GPS button and `location.north.line.fill` on the
  // compass, both close to square in their own boxes — so the map's arrow and
  // the buttons' arrows now agree about what an arrow is shaped like.
  //
  // The LENGTH also came down, from 4.00 of the shape box to 2.60, because a
  // rebalance that only widened would have made an ownship 28 points long and
  // 20 wide: a ratio is fixable by moving either number and only one of them
  // keeps the mark the size a phone wants. `movingmap.size_px` in the pack is
  // what turns that back into points, and it moved with this.
  {
    const double h = kR * 1.30;        // half the length, tip to tail
    const double half_w = kR * 0.95;   // half the width, tail to tail
    symbols_[bs::kNorthArrow] =
        One({Ring({P(0, h), P(half_w, -h), P(0, -h * 0.40), P(-half_w, -h)},
                  c, w)});
  }
  // A crosshair leaves the centre OPEN — the pixel the user is aiming at is
  // the one thing it must not cover.
  {
    const double g = kR * 0.35, e = kR * 1.6;
    symbols_[bs::kCrosshair] = One({Line({P(-e, 0), P(-g, 0)}, c, w),
                                    Line({P(g, 0), P(e, 0)}, c, w),
                                    Line({P(0, -e), P(0, -g)}, c, w),
                                    Line({P(0, g), P(0, e)}, c, w)});
  }
  // The ownship (MM4): an aircraft in plan view, nose at +y. ONE closed ring
  // and not a fuselage stroke plus two wing strokes, because a filled
  // silhouette is what stays readable when the map under it is a chart — and
  // because a stroked cross is what an ownship is most often mistaken for.
  //
  // It is deliberately ASYMMETRIC front-to-back — the wings sit forward of
  // centre, the tailplane is a third of their span, and THE NOSE REACHES
  // FURTHER FORWARD (1.00) THAN THE TAIL DOES AFT (0.85) — so that the symbol
  // says which way it is pointing even at 12 px, where the nose alone is two
  // pixels. That last one is what a test can state about the INK at any
  // heading, and per the ledger's rule this behaviour gets its own directional
  // assertion rather than only a golden.
  {
    const double u = kR;  // half the shape box; the ring reaches +/-1.0 of it
    symbols_[bs::kOwnship] = One({Ring(
        {P(0.00 * u, 1.00 * u),    // nose
         P(0.09 * u, 0.45 * u),
         P(0.80 * u, -0.10 * u),   // starboard wing, leading tip
         P(0.80 * u, -0.32 * u),   // starboard wing, trailing tip
         P(0.11 * u, -0.32 * u),
         P(0.26 * u, -0.72 * u),   // starboard tailplane
         P(0.26 * u, -0.85 * u),
         P(0.00 * u, -0.75 * u),   // tail cone
         P(-0.26 * u, -0.85 * u),
         P(-0.26 * u, -0.72 * u),
         P(-0.11 * u, -0.32 * u),
         P(-0.80 * u, -0.32 * u),
         P(-0.80 * u, -0.10 * u),
         P(-0.09 * u, 0.45 * u)},
        c, w)});
  }
}

}  // namespace fv
