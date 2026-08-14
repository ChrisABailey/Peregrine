// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Extracted verbatim from fv_geosym_style.cpp (G2). See fv_cgm_to_symbol.h.

#include "fv_cgm_to_symbol.h"

// NOTE: GeoSymServer's StdAfx POSIX block defines the Win32 min/max MACROS
// (scoped to this tree, see the 2026-07-21 ledger entry), so every std::min /
// std::max call below is parenthesized to stop macro expansion.
#include <algorithm>
#include <cmath>

#include "stdafx.h"  // POSIX branch: fv_compat + CString + MFC containers

namespace fv {

FvColor FromColorRef(COLORREF c) {
  FvColor out;
  out.r = static_cast<unsigned char>(c & 0xFF);
  out.g = static_cast<unsigned char>((c >> 8) & 0xFF);
  out.b = static_cast<unsigned char>((c >> 16) & 0xFF);
  out.a = 255;
  return out;
}

namespace {

// --- CGM display list -> product-neutral VectorSymbol ----------------------

void AddPoints(const std::vector<CgmPoint>& in, SymbolPrimitive* out) {
  out->points.reserve(in.size());
  for (const CgmPoint& p : in)
    out->points.push_back(SymbolPoint{static_cast<double>(p.x),
                                      static_cast<double>(p.y)});
}

// Solves r = radius1*cos(t) + radius2*sin(t) for t, so an arc's delimiting
// rays become parameter angles in the conjugate-diameter basis.
double AngleInBasis(const CgmPoint& r1, const CgmPoint& r2, const CgmPoint& r) {
  const double a = static_cast<double>(r1.x), b = static_cast<double>(r2.x);
  const double c = static_cast<double>(r1.y), d = static_cast<double>(r2.y);
  const double det = a * d - b * c;
  if (std::fabs(det) < 1e-12) return 0.0;
  const double x = static_cast<double>(r.x), y = static_cast<double>(r.y);
  const double cos_t = (d * x - b * y) / det;
  const double sin_t = (-c * x + a * y) / det;
  return std::atan2(sin_t, cos_t);
}

// Flattens an elliptical arc into a polyline. CGM's conjugate-diameter form
// handles rotation for free: P(t) = center + r1*cos t + r2*sin t.
void FlattenArc(const CgmElement& e, SymbolPrimitive* out) {
  double t0 = AngleInBasis(e.radius1, e.radius2, e.ray[0]);
  double t1 = AngleInBasis(e.radius1, e.radius2, e.ray[1]);
  const double kTwoPi = 6.283185307179586476925286766559;
  double sweep = t1 - t0;
  if (e.clockwise) {
    while (sweep > 0.0) sweep -= kTwoPi;
    if (sweep <= -kTwoPi) sweep += kTwoPi;
  } else {
    while (sweep < 0.0) sweep += kTwoPi;
    if (sweep >= kTwoPi) sweep -= kTwoPi;
  }
  const int steps = 48;
  out->points.reserve(static_cast<size_t>(steps) + 2);
  for (int i = 0; i <= steps; ++i) {
    const double t = t0 + sweep * (static_cast<double>(i) / steps);
    const double ct = std::cos(t), st = std::sin(t);
    out->points.push_back(SymbolPoint{
        e.center.x + e.radius1.x * ct + e.radius2.x * st,
        e.center.y + e.radius1.y * ct + e.radius2.y * st});
  }
  if (e.arc_close == CgmArcClose::kPie) {
    out->points.push_back(SymbolPoint{static_cast<double>(e.center.x),
                                      static_cast<double>(e.center.y)});
  }
}

// Applies the picture's VDC direction multipliers, which is what Windows does
// in CCGMDrawingObject::RotateVDC ("Need to do VDC adjustments for reflection
// even if zero rotation angle") when it builds the m_disp_vertices the GDI
// path draws. The parser already applied them ONCE while reading coordinates
// (CCGMFile::ReadVDCScaledY), and CgmSymbol keeps those once-applied values,
// so without this second application the symbol is mirrored — for the
// standard lly<ury extent every GeoSym symbol uses, dir_y is -1 and the
// display list is y-DOWN while VectorSymbol's contract (and VectorRenderer's
// flip) is y-UP, which drew every DNC point symbol upside down.
//
// Rotation still matches: Windows rotates BEFORE the multipliers, we rotate
// after, and diag(1,-1)*R(a) == R(-a)*diag(1,-1) — the same net transform.
void ApplyVdcDir(const CgmSymbol& cgm, SymbolPrimitive* p) {
  const double dx = static_cast<double>(cgm.dir_x());
  const double dy = static_cast<double>(cgm.dir_y());
  for (SymbolPoint& pt : p->points) { pt.x *= dx; pt.y *= dy; }
  p->center.x *= dx;
  p->center.y *= dy;
  p->radius1.x *= dx;
  p->radius1.y *= dy;
  p->radius2.x *= dx;
  p->radius2.y *= dy;
}

}  // namespace

VectorSymbol ToVectorSymbol(const CgmSymbol& cgm,
                            const CSymColorAdjuster& adjuster) {
  VectorSymbol out;
  out.primitives.reserve(cgm.elements().size());
  for (const CgmElement& e : cgm.elements()) {
    SymbolPrimitive p;
    // CGM fill styles: 0 hollow, 1 solid, 4 empty. Anything else (pattern,
    // hatch, geometric, interpolated) has no canvas equivalent yet and is
    // drawn as its solid colour rather than dropped.
    const bool solid_fill = e.fill_style != 0 && e.fill_style != 4;
    switch (e.type) {
      case CgmElementType::kPolyline:
        p.type = SymbolPrimitiveType::kPolyline;
        AddPoints(e.vertices, &p);
        p.has_stroke = true;
        p.stroke_color = FromColorRef(adjuster.Adjust(e.line_color));
        p.stroke_width = static_cast<double>(e.line_width);
        break;
      case CgmElementType::kPolygon:
      case CgmElementType::kPolygonSet:
        p.type = SymbolPrimitiveType::kPolygon;
        AddPoints(e.vertices, &p);
        p.has_fill = solid_fill;
        p.fill_color = FromColorRef(adjuster.Adjust(e.fill_color));
        p.has_stroke = e.edge_visible;
        p.stroke_color = FromColorRef(adjuster.Adjust(e.edge_color));
        p.stroke_width = static_cast<double>(e.edge_width);
        break;
      case CgmElementType::kEllipse:
        p.type = SymbolPrimitiveType::kEllipse;
        p.center = SymbolPoint{static_cast<double>(e.center.x),
                               static_cast<double>(e.center.y)};
        p.radius1 = SymbolPoint{static_cast<double>(e.radius1.x),
                                static_cast<double>(e.radius1.y)};
        p.radius2 = SymbolPoint{static_cast<double>(e.radius2.x),
                                static_cast<double>(e.radius2.y)};
        p.has_fill = solid_fill;
        p.fill_color = FromColorRef(adjuster.Adjust(e.fill_color));
        p.has_stroke = e.edge_visible;
        p.stroke_color = FromColorRef(adjuster.Adjust(e.edge_color));
        p.stroke_width = static_cast<double>(e.edge_width);
        break;
      case CgmElementType::kEllipticalArc:
        // An arc becomes a polyline (or a closed polygon for pie/chord).
        p.type = e.arc_close == CgmArcClose::kOpen
                     ? SymbolPrimitiveType::kPolyline
                     : SymbolPrimitiveType::kPolygon;
        FlattenArc(e, &p);
        if (p.type == SymbolPrimitiveType::kPolygon) {
          p.has_fill = solid_fill;
          p.fill_color = FromColorRef(adjuster.Adjust(e.fill_color));
          p.has_stroke = e.edge_visible;
          p.stroke_color = FromColorRef(adjuster.Adjust(e.edge_color));
          p.stroke_width = static_cast<double>(e.edge_width);
        } else {
          p.has_stroke = true;
          p.stroke_color = FromColorRef(adjuster.Adjust(e.line_color));
          p.stroke_width = static_cast<double>(e.line_width);
        }
        break;
      case CgmElementType::kText:
        p.type = SymbolPrimitiveType::kText;
        p.text = e.text;
        p.center = SymbolPoint{static_cast<double>(e.position.x),
                               static_cast<double>(e.position.y)};
        p.text_height = static_cast<double>(e.char_height);
        p.has_fill = true;
        p.fill_color = FromColorRef(adjuster.Adjust(e.line_color));
        break;
      default:
        continue;
    }
    ApplyVdcDir(cgm, &p);
    out.primitives.push_back(std::move(p));
  }
  // bounds() is in the parser's once-applied space too, so the extent takes
  // the same multipliers; min/max stay order-agnostic rather than copying the
  // `top`/`bottom` names across a sign change.
  const CgmRect& b = cgm.bounds();
  const double dx = static_cast<double>(cgm.dir_x());
  const double dy = static_cast<double>(cgm.dir_y());
  const double x0 = b.left * dx, x1 = b.right * dx;
  const double y0 = b.top * dy, y1 = b.bottom * dy;
  out.min_x = (std::min)(x0, x1);
  out.max_x = (std::max)(x0, x1);
  out.min_y = (std::min)(y0, y1);
  out.max_y = (std::max)(y0, y1);
  return out;
}


}  // namespace fv
