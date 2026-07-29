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

#include <cstdint>
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
// Pixmap symbols
// ---------------------------------------------------------------------------
//
// Not every symbol a product ships is a display list. S-52's delivered library
// defines 679 of its 1018 symbols as RASTER ONLY — a tile in a symbol sheet
// with no HPGL at all — and they are not a fringe: the lateral buoys, beacons
// and daymarks of a harbour are in there. OSM sprite sheets are the same idea
// again, and GeoSym's stipple fills were bitmaps before they were CGM.
//
// So a symbol_id resolves through TWO accessors, vector first. This is a
// deliberate second slot rather than a rasterize-on-demand VectorSymbol: a
// pixmap has no geometry to hand back, and pretending otherwise would make
// every consumer of Symbol() (the pick index, the placer) reason about a
// display list that is really a rectangle.
//
// UNITS ARE PIXELS, unlike everything else at this seam — the tile is
// authored at a nominal display resolution and the renderer blits it. The
// pivot is where the feature's own position lands, in tile pixels with y DOWN
// (image convention, matching PixelBuffer), so it is subtracted from the
// anchor to get the tile's top-left.
struct SymbolPixmap {
  PixelBuffer tile;  // RGBA8, straight (non-premultiplied) alpha
  double pivot_x = 0.0;
  double pivot_y = 0.0;
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

// --- along-path and area pattern placement (E3b) ----------------------------
//
// Three products describe "repeat something along a line" and none of them can
// say it with a pen: GeoSym's SAMI line style is a cycle of dash / gap / point
// -symbol runs, S-52's LC is a symbol stamped end to end, and an OSM line
// pattern is the same idea again. They reduce to ONE primitive — a cyclic
// sequence of runs measured in pixels along the path — so the placement walk
// lives once in the renderer (PlaceAlongPath) rather than three times in three
// style engines. §5.1 of the plan calls this out; it is why LC and SAMI are
// not two features.
//
// A style engine converts its own units to PIXELS here, the same contract
// Pen/Brush already carry.

enum class PathRunType {
  kGap = 0,   // advance, draw nothing
  kDash,      // advance, stroking with LinePatternStyle::pen
  kSymbol,    // stamp `symbol_id`, then advance by `length`
};

struct PathRun {
  PathRunType type = PathRunType::kGap;

  // Pixels along the path. On a kDash this may be 0, which is GeoSym's
  // "run to the end of the line" (SAMI encodes a solid line that way).
  double length = 0.0;

  // kSymbol only.
  std::string symbol_id;
  double symbol_scale = 1.0;
  // Added to the path tangent, in the same sense PointSymbolStyle::rotation_deg
  // is applied. 0 = the symbol's own +x axis runs along the path.
  double rotation_deg = 0.0;
  // Perpendicular displacement in pixels, positive toward the symbol's +y
  // (left of the direction of travel). GeoSym's SAMI vertical displacement.
  double offset = 0.0;
};

struct LinePatternStyle {
  bool valid = false;
  std::vector<PathRun> runs;  // cycled until the path is consumed
  Pen pen;                    // strokes the kDash runs
  double phase = 0.0;         // pixels into the cycle at the path's start
};

// A repeating symbol fill. ICanvas has only a solid brush, so this is how a
// product's pattern brush (S-52 AP, GeoSym's bitmap stipples) says what it
// really is; the renderer stamps the symbol on a grid clipped to the ring.
struct AreaPatternStyle {
  bool valid = false;
  std::string symbol_id;
  double spacing_x = 0.0;  // pixels, centre to centre
  double spacing_y = 0.0;
  bool staggered = false;  // S-52 fill type 'S': every other row offset by x/2
  double symbol_scale = 1.0;
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

  // Set INSTEAD of stroke/fill when the product's linework or fill is a
  // repeated symbol rather than a pen or a solid brush. Both may coexist with
  // the plain slots — S-52's LC over an LS casing is legal — and the renderer
  // draws stroke first, then the pattern on top.
  LinePatternStyle line_pattern;
  AreaPatternStyle area_pattern;
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

  // The pixmap form of the same id, consulted ONLY when Symbol() has no
  // geometry — a product that authors a symbol both ways keeps its vector
  // definition, which scales and rotates without resampling. Owned and cached
  // by the engine on the same terms as Symbol(). Default: this product has no
  // raster symbology at all, which is true of GeoSym and of every synthetic
  // engine in the tests.
  virtual const SymbolPixmap* Pixmap(const std::string& /*symbol_id*/) {
    return nullptr;
  }

  // Monotone counter that MUST change whenever anything that could change what
  // Style() returns changes — a rule, a viewing group, a mariner setting, a
  // reload. It is what lets a retained VectorScene (scene.h) know its baked
  // styles are still current, so an engine that mutates and does not report it
  // will be drawn stale.
  //
  // Default 0 = "nothing about me ever changes", which is true of a fixed
  // table and of the synthetic engines in the tests. Both real products get a
  // correct one from LookupTableStyleEngine, which is where a product engine
  // should inherit it from rather than reimplementing.
  virtual uint64_t style_epoch() const { return 0; }
};

using StyleEnginePtr = std::shared_ptr<IStyleEngine>;

}  // namespace fv

#endif  // FVKIT_VECTOR_STYLE_H_
