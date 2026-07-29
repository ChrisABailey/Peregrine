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

  // Wall-clock split of the last Render, in milliseconds. The three phases are
  // exactly the three costs R3 is about: pulling features out of the source,
  // asking the style engine what they look like, and putting pixels down.
  // Measured always (two clock reads per phase is nothing next to a render)
  // so a caller can report them without a special build.
  double query_ms() const { return query_ms_; }
  double style_ms() const { return style_ms_; }
  double draw_ms() const { return draw_ms_; }

 private:
  VectorSourcePtr source_;
  StyleEnginePtr style_;
  double dpi_ = 96.0;
  double symbol_scale_ = 1.0;
  size_t max_features_ = 0;
  double scene_margin_ = 0.0;
  double simplify_px_ = 0.0;
  VectorScene scene_;
  bool scene_reused_ = false;
  bool pick_enabled_ = true;
  size_t features_queried_ = 0;
  size_t draws_emitted_ = 0;
  double query_ms_ = 0.0;
  double style_ms_ = 0.0;
  double draw_ms_ = 0.0;
  PickIndex pick_;
};

// --- geometry helpers, exposed because they are worth testing directly -----

struct SurfacePoint {
  double x = 0.0;
  double y = 0.0;
};

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

// Grid of stamp positions covering `ring` (a clipped pixel ring). A position
// is emitted when it is inside the ring by the even-odd rule, which is the
// same fill rule CpuCanvas uses, so a pattern lands exactly where the solid
// fill would have. DEVIATION: a symbol whose ink overruns the boundary is not
// clipped — ICanvas has no clip region — so a pattern can bleed by up to half
// a symbol. Documented in the ledger; the fix is an ICanvas clip rect.
std::vector<SurfacePoint> PlaceOverArea(const std::vector<PixelPoint>& ring,
                                        double spacing_x, double spacing_y,
                                        bool staggered);

// Budget for one PlaceAlongPath call.
constexpr int kMaxPatternCycles = 4096;

}  // namespace fv

#endif  // FVKIT_VECTOR_RENDERER_H_
