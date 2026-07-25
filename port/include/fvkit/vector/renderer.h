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

 private:
  VectorSourcePtr source_;
  StyleEnginePtr style_;
  double dpi_ = 96.0;
  double symbol_scale_ = 1.0;
  size_t max_features_ = 0;
  bool pick_enabled_ = true;
  size_t features_queried_ = 0;
  size_t draws_emitted_ = 0;
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

}  // namespace fv

#endif  // FVKIT_VECTOR_RENDERER_H_
