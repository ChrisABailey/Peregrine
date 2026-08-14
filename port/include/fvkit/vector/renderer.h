// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::VectorRenderer — the SHARED right-hand side of the vector seam
// (vpf-geosym plan phase V5b).
//
//   IVectorSource -> IStyleEngine -> [ VectorRenderer ] -> ICanvas
//
// It owns exactly the parts that are the same for every vector product:
// query the viewport, style each feature, sort by display priority, project
// WGS-84 degrees to surface pixels, clip to the canvas, and emit ICanvas
// calls. It knows nothing about VPF, GeoSym, S-57 or MVT.
//
// This replaces FalconView's CSymLayeredDisplay, whose N offscreen DCs (one
// per display priority, composited at the end) were a GDI workaround. The
// plan's call: render in priority order into ONE buffer, single pass. Per-
// layer alpha groups get added only when GeoSym area fills actually need
// them (no area features until V5c, so not yet).

#ifndef FVKIT_VECTOR_RENDERER_H_
#define FVKIT_VECTOR_RENDERER_H_

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

#include "fvkit/canvas/canvas.h"
#include "fvkit/proj.h"
#include "fvkit/vector/pick.h"
#include "fvkit/vector/scene.h"
#include "fvkit/vector/style.h"
#include "fvkit/vector/vector.h"

namespace fv {

// HIMETRIC (0.01 mm) per 1/100 inch. FalconView's CCGMSymbol carries this as
// s_dblConversionFactor = 25.4 and divides symbol VDC extents by it to get a
// device size, i.e. it treats 1/100 inch as one pixel (100 DPI) for point
// symbols regardless of the actual device. Preserved (bit-faithful rule); see
// SymbolPixelsPerHimetric below for where the device DPI *is* honoured.
//
// It is the DEFAULT of IStyleEngine::himetric_per_symbol_pixel(), not a
// constant of the renderer: a product states the grid its symbols were drawn
// on, and S-52 states 32 (0.32 mm). Everything that measures a distance in the
// same pixels a symbol is drawn at — an area pattern's pitch, a complex
// line's period — must use the engine's number, not this one.
constexpr double kHimetricPerHundredthInch = 25.4;

class VectorRenderer {
 public:
  VectorRenderer(VectorSourcePtr source, StyleEnginePtr style);

  // Device resolution, used for style values the original converted with
  // GDI's HIMETRICtoDP (line widths, text sizes). Default 96.
  void SetDeviceDpi(double dpi) { dpi_ = dpi > 0 ? dpi : 96.0; }
  double device_dpi() const { return dpi_; }

  // Multiplies point-symbol size. GeoSym's zoom factor is an integer percent;
  // this is that / 100. Symbols smaller than 20% are dropped by the engine,
  // matching CCGMSymbol::DrawSymbol's early-out.
  void SetSymbolScale(double s) { symbol_scale_ = s > 0 ? s : 1.0; }

  // Guard for interactive callers over dense coverages. 0 = unlimited.
  void SetMaxFeatures(size_t n) { max_features_ = n; }

  // Makes PIXEL-sized labels scale with the map (see LabelSizeUnit).
  //
  // Every product authors label size in pixels — GeoSym in points, a GL style
  // in px — which keeps text a constant size on screen while the map zooms
  // under it. Setting a reference scale denominator reinterprets that authored
  // size as the size AT THAT SCALE: text is then `size * ref / current`, so it
  // grows into a zoom and shrinks out of one along with the geometry it names.
  // A label that already carries LabelSizeUnit::kMeters ignores this — it
  // said what it meant.
  //
  // 0 = off, the default, because turning it on changes every rendered frame
  // that has a label in it and the pinned goldens are the record of what those
  // frames look like. Interactive callers opt in.
  void SetLabelReferenceScale(double scale_denominator) {
    label_ref_scale_ = scale_denominator > 0.0 ? scale_denominator : 0.0;
  }
  double label_reference_scale() const { return label_ref_scale_; }

  // --- the retained scene (R3a, plan §5.4) ---------------------------------
  //
  // Every Render goes through a VectorScene; these control how long one lives.
  // The scene is REUSED when the viewport still lies inside the area it was
  // built for and nothing it baked has changed (see VectorScene::CanServe), so
  // a pan skips the query and the styling — measured at ~40% of a DNC harbour
  // frame. A zoom, a symbol-scale change or a style-engine change rebuilds.
  //
  // MARGIN. The scene is built for the viewport grown by this fraction on each
  // side, which is what makes a small pan a cache hit. It DEFAULTS TO 0 — a
  // margin queries features outside the viewport, and symbology anchored just
  // off-canvas legitimately inks into it, so a non-zero margin draws slightly
  // MORE than a zero margin does. That is the more correct picture (an edge
  // symbol is currently half-missing), but it is a change to what is drawn, so
  // an interactive caller opts in and the pinned goldens stay put.
  void SetSceneMargin(double fraction) {
    scene_margin_ = fraction > 0.0 ? fraction : 0.0;
  }
  double scene_margin() const { return scene_margin_; }

  // Douglas-Peucker tolerance in pixels applied when a scene is built. 0 =
  // exact, the default. Simplification is RENDER-ONLY: identify goes back to
  // the source through the FeatureRef, so no precision is lost to a tap.
  void SetSimplifyPixels(double px) {
    simplify_px_ = px > 0.0 ? px : 0.0;
  }
  double simplify_pixels() const { return simplify_px_; }

  // Drops the retained scene. Needed only when something changed that the
  // style engine's epoch does not report — which for both real products is
  // nothing, since LookupTableStyleEngine reports its own.
  void InvalidateScene() { scene_.Clear(); }

  // Whether the LAST Render reused the retained scene instead of rebuilding.
  bool scene_reused() const { return scene_reused_; }
  const VectorScene& scene() const { return scene_; }

  // Record every emitted primitive for hit-testing (plan §5.3). ON by default:
  // identify is load-bearing for an interactive caller, and a silently empty
  // pick list is a worse failure than the cost — one small shape per drawn
  // run. A bulk/offline renderer (tile packing) should turn it off.
  void SetPickEnabled(bool on) { pick_enabled_ = on; }
  bool pick_enabled() const { return pick_enabled_; }

  // Hit-test structure for the LAST Render(), in canvas pixels. Cleared at the
  // start of every Render, and empty when picking is disabled.
  const PickIndex& pick_index() const { return pick_; }

  // Queries `proj`'s geographic bounds, styles, sorts and draws. Does NOT
  // clear the canvas — the caller decides the background (a chart renders
  // over a raster base in the usual case).
  Status Render(const MapProjection& proj, ICanvas* canvas);

  // Stats from the last Render, for tests and the demo's status line.
  size_t features_queried() const { return features_queried_; }
  size_t draws_emitted() const { return draws_emitted_; }

  // Extra text draws spent on label halos (T2). NOT counted in
  // draws_emitted(): a haloed label is one label, and folding its 4-8 stamps
  // into that number would move every existing draw-count assertion the moment
  // a style turned halos on. Reported separately because it is real work — it
  // is the one diagnostic that says what the halo cost.
  size_t halo_draws() const { return halo_draws_; }

  // Wall-clock split of the last Render, in milliseconds. The three phases are
  // exactly the three costs R3 is about: pulling features out of the source,
  // asking the style engine what they look like, and putting pixels down.
  // Measured always (two clock reads per phase is nothing next to a render)
  // so a caller can report them without a special build.
  double query_ms() const { return query_ms_; }
  double style_ms() const { return style_ms_; }
  double draw_ms() const { return draw_ms_; }

 private:
  // This frame's pixel anchor for one area-pattern spacing, carried forward as
  // a ground point so a pan cannot slide the lattice. See the long comment on
  // the definition — the short version is that anchoring on a point 600,000 px
  // away made the lattice sensitive to dpp, and dpp moves with the centre
  // latitude in every scale-driven projection mode.
  void PatternAnchor(const MapProjection& proj, double seed_x, double seed_y,
                     double spacing_x, double spacing_y, double* ax,
                     double* ay);

  VectorSourcePtr source_;
  StyleEnginePtr style_;
  double dpi_ = 96.0;
  double symbol_scale_ = 1.0;
  size_t max_features_ = 0;
  double scene_margin_ = 0.0;
  double simplify_px_ = 0.0;
  double label_ref_scale_ = 0.0;
  VectorScene scene_;
  bool scene_reused_ = false;
  bool pick_enabled_ = true;
  size_t features_queried_ = 0;
  size_t draws_emitted_ = 0;
  size_t halo_draws_ = 0;
  double query_ms_ = 0.0;
  double style_ms_ = 0.0;
  double draw_ms_ = 0.0;
  PickIndex pick_;
  // Retained stamp lattices, keyed by (spacing_x, spacing_y). Each value is the
  // ground point the lattice hangs on; a handful of entries at most.
  std::map<std::pair<double, double>, GeoPoint> pattern_anchors_;
};

// --- geometry helpers, exposed because they are worth testing directly -----
//
// `SurfacePoint` moved to fvkit/geo.h in G1 — it is a D4 pixel primitive and
// fvkit/geo/contour.h needs it without the vector seam. Same type, same
// namespace; this note exists only so nobody goes looking for it here.

// Cohen-Sutherland clip of a polyline against [0,w) x [0,h), emitting the
// visible runs. A run is only emitted when it has >= 2 points.
std::vector<std::vector<PixelPoint>> ClipPolyline(
    const std::vector<SurfacePoint>& pts, int width, int height);

// Sutherland-Hodgman clip of a ring against the same rect. Returns an empty
// ring when nothing survives. Convex clip region, so this is exact.
std::vector<PixelPoint> ClipPolygon(const std::vector<SurfacePoint>& ring,
                                    int width, int height);

// --- the shared along-path / area placer (E3b) -----------------------------
//
// One walk serves GeoSym's SAMI lines, S-52's LC and AP, and whatever OSM
// brings; see the LinePatternStyle comment in style.h for why it is one
// primitive. Both functions are pure geometry over PIXEL coordinates — no
// canvas, no style engine, no product — which is what makes them testable
// without any chart data at all.

// One symbol the placer decided to stamp.
struct PlacedSymbol {
  std::string symbol_id;
  double x = 0.0, y = 0.0;    // pixels, the symbol's own (0,0) anchor
  double rotation_deg = 0.0;  // the angle DrawSymbolAt applies (see below)
  double scale = 1.0;
};

struct PathPlacement {
  // Sub-paths to stroke with LinePatternStyle::pen, in path order.
  std::vector<std::vector<SurfacePoint>> dashes;
  std::vector<PlacedSymbol> symbols;
  // True when the walk hit its cycle budget and stopped early. The caller
  // still draws what came back — a partly patterned line beats none.
  bool truncated = false;
};

// Walks `path` (>= 2 pixel points) laying `runs` down end to end, repeating
// the sequence until the path is consumed.
//
// ROTATION CONVENTION: a symbol's own +x axis is laid along the direction of
// travel, so the emitted rotation_deg is atan2(-dy, dx) of the local tangent
// in screen pixels (screen y grows downward, symbol y grows up — the flip the
// renderer already applies when it draws a symbol). PathRun::rotation_deg adds
// to that. A run whose length is <= 0 advances nothing; on a kDash that means
// GeoSym's "to the end of the line" and the walk finishes the path in one run.
//
// Cycles are capped (kMaxPatternCycles) so a pathological path — a projection
// that puts a vertex a million pixels away — cannot hang a render.
PathPlacement PlaceAlongPath(const std::vector<SurfacePoint>& path,
                             const std::vector<PathRun>& runs, double phase);

// Grid of stamp positions covering `ring`, the EXACT projected outline in
// sub-pixel surface coordinates — deliberately not the clipped, whole-pixel
// ring the fill is drawn from, whose vertices move by up to half a pixel with
// the pan and make stamps near a boundary blink. A position is emitted when it
// is inside the ring by the even-odd rule, the same fill rule CpuCanvas uses,
// so a pattern lands where the solid fill would have.
//
// `clip_w`/`clip_h` bound the walk to a canvas of that size (0 = unbounded),
// which an exact ring needs because it may run far outside the viewport.
//
// DEVIATION: a symbol whose ink overruns the boundary is not clipped — ICanvas
// has no clip region — so a pattern can bleed by up to half a symbol.
// Documented in the ledger; the fix is an ICanvas clip rect.
//
// `anchor_x`/`anchor_y` pin the lattice: stamps land on anchor + k*spacing, so
// every ring in a frame shares one grid. The renderer passes the pixel position
// of a nearby FIXED GEOGRAPHIC POINT, which is what keeps a pattern still under
// its own area while the map pans — pass 0,0 to get the original canvas-origin
// lattice, which crawls. See VectorRenderer::PatternAnchor.
std::vector<SurfacePoint> PlaceOverArea(const std::vector<SurfacePoint>& ring,
                                        double spacing_x, double spacing_y,
                                        bool staggered, double anchor_x = 0.0,
                                        double anchor_y = 0.0,
                                        double clip_w = 0.0,
                                        double clip_h = 0.0);

// Budget for one PlaceAlongPath call.
constexpr int kMaxPatternCycles = 4096;

// --- text along a path (the label placer) -----------------------------------
//
// The third thing that repeats along a line, after SAMI/LC symbols and area
// patterns, and the one PlaceAlongPath cannot express: a glyph's step is its
// OWN advance, not a run length the style knows in advance, and a run either
// fits and is drawn or does not fit and is dropped whole — half a road name is
// worse than none.
//
// Pure geometry, like its two neighbours: pixels in, pixels out, no canvas and
// no font. The caller measures the text (that is the canvas's job) and passes
// the per-glyph advances in.

struct PlacedGlyph {
  size_t index = 0;      // which glyph, into the advances array
  double x = 0.0, y = 0.0;  // its baseline-left origin, pixels
  double angle_rad = 0.0;   // CCW on screen, the ICanvas text convention
};

// Glyphs are in reading order and always run left-to-right on screen: a run
// whose path direction would put the text upside down is walked backwards.
struct PlacedTextRun {
  std::vector<PlacedGlyph> glyphs;
};

// Lays `advances` (pixels, one per glyph) along `path` (>= 2 pixel points).
//
//   spacing_px    distance between the starts of consecutive runs; <= 0 puts
//                 ONE run at the centre of the path, which is what a road
//                 wants — a source hands out roads part by part already.
//   max_angle_deg the largest turn tolerated between two consecutive glyphs.
//                 A run that turns harder is dropped, not straightened.
//   offset_px     perpendicular shift, positive to the LEFT of travel.
//
// Returns nothing when the text is longer than the path: a name that does not
// fit is not drawn. Every emitted run is complete.
std::vector<PlacedTextRun> PlaceTextAlongPath(
    const std::vector<SurfacePoint>& path, const std::vector<double>& advances,
    double spacing_px, double max_angle_deg, double offset_px);

// Sizing bounds for a label whose size is a GROUND size (LabelSizeUnit::
// kMeters, or a renderer label reference scale). Ground sizing has no natural
// floor or ceiling — zoom far enough either way and the text is a smear of
// single pixels or a wall of letters taller than the canvas — so the converted
// size is clamped here and the label is dropped below the floor.
constexpr double kMinLabelPx = 5.0;
constexpr double kMaxLabelPx = 200.0;

// Degrees of latitude to metres, for label ground sizing ONLY. A spherical
// degree: the label is nominal typography, not a measurement, and carrying the
// ellipsoidal meridian arc here would imply a precision the size does not have.
constexpr double kMetersPerDegreeLat = 111319.49079327358;

}  // namespace fv

#endif  // FVKIT_VECTOR_RENDERER_H_
