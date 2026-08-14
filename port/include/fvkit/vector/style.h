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
// WHERE THE SYMBOL TYPES WENT (G2): VectorSymbol, SymbolPixmap and the display
// list they are made of moved to fvkit/symbol/library.h, which also names the
// interface that hands them out (ISymbolLibrary). This header includes it, so
// every existing include of style.h still sees all of them.

#ifndef FVKIT_VECTOR_STYLE_H_
#define FVKIT_VECTOR_STYLE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"  // Pen, Brush, TextStyle, FvColor
#include "fvkit/symbol/library.h"  // VectorSymbol, SymbolPixmap, ISymbolLibrary
#include "fvkit/vector/vector.h"

namespace fv {

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

// How a label meets its geometry.
enum class LabelPlacement {
  // One horizontal string at the first vertex of each part, plus dx/dy. The
  // original behaviour and still the right answer for a point feature.
  kPoint = 0,
  // Glyph by glyph along a line, each rotated to the local tangent. The
  // renderer's PlaceTextAlongPath does the walk; a run that does not fit, or
  // that turns harder than max_angle_deg, is not drawn at all.
  kAlongPath,
};

// Where the label's own box sits relative to the anchor (kPoint only). The
// canvas draws from the BASELINE-LEFT, so kLeft/kBaseline are what the canvas
// already does and are therefore the defaults: a product that says nothing
// draws exactly where it drew before. Anything else costs the renderer one
// GetTextExtent per label, which is why it is not measured unconditionally.
//
// S-52's TX/TE carry HJUST/VJUST for every string in the library and this is
// where they land; GeoSym and OSM do not use them (both pre-compute a dx/dy).
enum class LabelHAlign {
  kLeft = 0,  // the anchor is the left edge of the text
  kCenter,
  kRight,     // the anchor is the right edge — the text runs back from it
};

// The vertical box is modelled as running from `baseline - height` to
// `baseline`, the same approximation the pick box has always made: a descender
// hangs slightly below the box. Splitting it properly needs ascent/descent,
// which ICanvas does not expose and which every ICanvas implementor (including
// pyfvw's Python subclasses) would have to grow.
enum class LabelVAlign {
  kBaseline = 0,  // the anchor IS the baseline; no measurement, no adjustment
  kBottom,        // the anchor is the bottom of the box (== baseline, named)
  kCenter,
  kTop,           // the anchor is the top of the box; the text hangs below it
};

// What LabelStyle::style.size means.
enum class LabelSizeUnit {
  kPixels = 0,  // a constant on-screen size at any scale
  // A constant GROUND size: `ground_size_m` metres of cap height, converted to
  // pixels per frame, so the text zooms with the map exactly like the road it
  // names. The renderer clamps the result (see kMinLabelPx/kMaxLabelPx) so a
  // zoom-out cannot turn every label into a smear of single pixels.
  kMeters,
};

struct LabelStyle {
  bool valid = false;
  std::string text;
  TextStyle style;
  int dx = 0, dy = 0;  // pixel offset from the anchor point (kPoint only)

  // Applied AFTER dx/dy, so the offset moves the anchor and the alignment then
  // hangs the box off it — which is the order S-52 states XOFFS/YOFFS and
  // HJUST/VJUST in, and the only order in which a right-justified string at
  // XOFFS=-1 ends up wholly to the left of its buoy.
  LabelHAlign halign = LabelHAlign::kLeft;
  LabelVAlign valign = LabelVAlign::kBaseline;

  LabelPlacement placement = LabelPlacement::kPoint;

  // --- halo (outlined text) -------------------------------------------------
  // A name lying on the road it belongs to, or on a dense fill, is unreadable
  // in one colour. The halo is drawn the way the Windows FalconView drew it:
  // the SAME string, `halo_width` pixels off in each direction, in
  // `halo_color`, before the fill pass. Not a distance field and not a stroked
  // outline — those need a rasterizer that can dilate glyph coverage, and this
  // needs nothing from ICanvas at all, which is why every canvas backend
  // (including pyfvw's Python subclasses) gets it for free.
  //
  // 0 = no halo, and that is the default, so a product that says nothing draws
  // exactly what it drew before.
  //
  // Pixels, like every other pixel-valued field here: the style engine has
  // already converted its own units and applied device DPI.
  double halo_width = 0.0;
  FvColor halo_color{255, 255, 255, 255};

  // Ground sizing. `style.size` still carries the pixel size and is what a
  // kPixels label uses; a kMeters label uses this instead, and the renderer
  // leaves style.size alone so a caller can fall back to it.
  LabelSizeUnit size_unit = LabelSizeUnit::kPixels;
  double ground_size_m = 0.0;

  // --- kAlongPath only ------------------------------------------------------
  // Pixels between the START of one run and the start of the next. 0 = one run
  // per part, centred on it — which is the right default for a road, whose
  // parts already come out of the source one carriageway at a time.
  double spacing_px = 0.0;
  // Rejects a run whose direction turns by more than this between any two
  // consecutive glyphs. MapLibre's text-max-angle, same meaning and default.
  double max_angle_deg = 45.0;
  // Perpendicular displacement in pixels, positive to the LEFT of the
  // direction of travel — the same sense as PathRun::offset. Lifts a name off
  // the centreline it would otherwise sit on.
  double offset_px = 0.0;
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

// A style engine IS A SYMBOL LIBRARY (G2). The three symbol accessors it used
// to declare — Symbol / Pixmap / himetric_per_symbol_pixel — are exactly
// ISymbolLibrary's, so GeoSym, S-52, OSM and LookupTableStyleEngine all became
// symbol libraries the moment this base class was named, with no other edit.
// Their contract is unchanged and is documented there.
class IStyleEngine : public ISymbolLibrary {
 public:
  ~IStyleEngine() override = default;

  // Appends zero or more draw passes for `f`. Returning Ok with nothing
  // appended means "this feature is not symbolized at this scale" — a normal
  // outcome, not an error.
  virtual Status Style(const VectorFeature& f, const StyleContext& ctx,
                       std::vector<StyleResult>* out) = 0;

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
