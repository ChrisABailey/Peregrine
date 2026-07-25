// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// FvKit vector style seam (vpf-geosym plan phase V5b) — the MIDDLE of the
// three-part split declared in fvkit/vector/vector.h:
//
//   IVectorSource   product-specific features        (V5a, done)
//   IStyleEngine    product-specific (feature, scale) -> draw ops   <- HERE
//   VectorRenderer  SHARED projection/clipping -> ICanvas  (renderer.h)
//
// Nothing here mentions VPF, GeoSym, CGM, S-52 or MapLibre. GeoSym is the
// first implementation (port/GeoSymServer/fv_geosym_style.h); ENC's S-52 and
// an OSM style subset are meant to slot in beside it, not fork the renderer.
//
// WHY A SYMBOL DISPLAY LIST LIVES HERE: point symbology is a little vector
// drawing, and every product has one (GeoSym ships CGM, S-52 ships its own
// symbol library, OSM ships sprite sheets). The renderer has to draw them, so
// the shape has to be expressed in product-neutral terms. VectorSymbol is
// deliberately the small common subset — polyline / polygon / ellipse / text —
// which is exactly what fv::CgmSymbol's display list reduces to.
//
// SYMBOL UNITS ARE HIMETRIC (0.01 mm), y UP. That is the CGM VDC convention
// GeoSym symbols are authored in and what GDI's HIMETRICtoDP consumed on
// Windows; the renderer converts to pixels and flips y, because only it knows
// the device. A product whose symbols are authored in pixels just multiplies
// by 25.4 on the way in (see kHimetricPerHundredthInch in renderer.h).

#ifndef FVKIT_VECTOR_STYLE_H_
#define FVKIT_VECTOR_STYLE_H_

#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"  // Pen, Brush, TextStyle, FvColor
#include "fvkit/vector/vector.h"

namespace fv {

// ---------------------------------------------------------------------------
// Symbol display list
// ---------------------------------------------------------------------------

enum class SymbolPrimitiveType {
  kPolyline = 0,
  kPolygon,   // even-odd filled, optionally edged
  kEllipse,   // center + two conjugate radius vectors (handles rotation)
  kText,
};

struct SymbolPoint {
  double x = 0.0;
  double y = 0.0;
};

struct SymbolPrimitive {
  SymbolPrimitiveType type = SymbolPrimitiveType::kPolyline;

  std::vector<SymbolPoint> points;  // polyline / polygon vertices

  // kEllipse: CGM's conjugate-diameter encoding, kept rather than reduced to
  // an axis-aligned box so rotated ellipses survive the seam.
  SymbolPoint center;
  SymbolPoint radius1;
  SymbolPoint radius2;

  // kText
  std::string text;
  double text_height = 0.0;  // HIMETRIC

  bool has_fill = false;
  bool has_stroke = false;
  FvColor fill_color;
  FvColor stroke_color;
  double stroke_width = 0.0;  // HIMETRIC; 0 = thinnest the device can draw
};

// A resolution-independent symbol. Extent is in the same HIMETRIC units and
// is informational — the renderer anchors at the symbol's own (0,0), which is
// where the authoring convention puts the hot spot.
struct VectorSymbol {
  std::vector<SymbolPrimitive> primitives;
  double min_x = 0.0, min_y = 0.0, max_x = 0.0, max_y = 0.0;
};

// ---------------------------------------------------------------------------
// Draw ops
// ---------------------------------------------------------------------------

// Pen/Brush/TextStyle here are already in PIXELS: converting a product's
// authoring units (HIMETRIC for GeoSym, points for S-52) is the style
// engine's job, because only it knows what the numbers in its tables mean.
// The renderer is told the device DPI and passes it down via StyleContext.

struct StrokeStyle {
  bool valid = false;
  Pen pen;
};

struct FillStyle {
  bool valid = false;
  Brush brush;
};

struct PointSymbolStyle {
  bool valid = false;
  std::string symbol_id;      // key for IStyleEngine::Symbol()
  double rotation_deg = 0.0;  // clockwise from north-up, GeoSym's convention
  double scale = 1.0;         // multiplies the renderer's symbol scale
};

struct LabelStyle {
  bool valid = false;
  std::string text;
  TextStyle style;
  int dx = 0, dy = 0;  // pixel offset from the anchor point
};

// One draw pass for one feature. A feature can produce several (GeoSym area
// rows carry a centroid point symbol AND a boundary line symbol, at different
// display priorities), which is why Style() appends to a vector.
struct StyleResult {
  bool visible = true;

  // Draw order, LOWER FIRST. GeoSym's dispri column lands here unchanged;
  // the renderer stable-sorts, so same-priority features keep source order.
  int priority = 0;

  StrokeStyle stroke;
  FillStyle fill;
  PointSymbolStyle symbol;
  LabelStyle label;
};

// What the renderer tells the engine about the device, so pixel-valued styles
// can be computed once per render instead of per feature.
struct StyleContext {
  double scale_denominator = 0.0;  // 1:N of the viewport
  double device_dpi = 96.0;
  double symbol_scale = 1.0;  // user zoom on symbology (GeoSym's zoom/100)
};

class IStyleEngine {
 public:
  virtual ~IStyleEngine() = default;

  // Appends zero or more draw passes for `f`. Returning Ok with nothing
  // appended means "this feature is not symbolized at this scale" — a normal
  // outcome, not an error.
  virtual Status Style(const VectorFeature& f, const StyleContext& ctx,
                       std::vector<StyleResult>* out) = 0;

  // Display list for a symbol_id handed out by Style(). Owned and cached by
  // the engine; valid until the engine is destroyed. nullptr = unknown id.
  virtual const VectorSymbol* Symbol(const std::string& symbol_id) = 0;
};

using StyleEnginePtr = std::shared_ptr<IStyleEngine>;

}  // namespace fv

#endif  // FVKIT_VECTOR_STYLE_H_
