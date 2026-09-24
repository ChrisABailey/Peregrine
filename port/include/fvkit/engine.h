// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/engine.h — FvKit L3b map engine. Pure calls, no draw loop, no
// callback registry (the app owns the loop, per the design review):
// configure viewport -> RenderBaseMap(canvas) composites whatever the
// catalog says intersects it. This is the C++ retirement of the pan-viewer
// demo's compositing loop.
//
// Per-frame resampling: source pixel coords are computed at the overlap
// corners via the source's exact transforms and interpolated linearly
// across the region (exact for equal-arc CADRG/TIROS; a small-region affine
// approximation for projected GeoTIFF — same trade the demo made, now
// documented here). Nearest-neighbor sampling, non-AA, deterministic.
//
// ROTATION (PR3). A turned projection needs a turned blit, and the two cases
// are separate code paths on purpose:
//   * rotation 0 runs exactly the arithmetic it ran before rotation existed,
//     in the same order, so every pinned raster golden stays byte-identical
//     (PR1's rule);
//   * a turned chart maps the target's AXIS-ALIGNED BOX back through
//     SurfaceToGeo -> GeoToPixel and MASKS what falls outside the frame.
// The mask is the whole difference in kind. The unturned path can CLAMP a
// source coordinate to the block it read, because the target region it built
// is exactly the overlap; a turned frame's box has corners that are not in
// the frame at all, and clamping there would smear the edge pixels out to
// fill them. So the turned path leaves an unmapped pixel at alpha 0 and
// DrawPixmap's blend drops it, which is what gives a turned frame straight
// edges at the angle it was turned to.
//
// A NON-AFFINE projection (Mercator, Lambert, ...) takes a third path,
// CompositeRowProjected: the adaptive bilinear warp of Windows'
// projection_dc.cpp (fvkit/raster_warp.h), masked like the turned path. The
// gate is MapProjection::IsAffine(), so equal-arc never reaches it.

#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "fvkit/canvas/canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/source.h"
#include "fvkit/proj.h"

namespace fv {

class MapEngine {
 public:
  // The engine queries this catalog; scan it before rendering.
  explicit MapEngine(std::shared_ptr<Catalog> catalog,
                     size_t source_cache_capacity = 32);
  ~MapEngine();
  MapEngine(const MapEngine&) = delete;
  MapEngine& operator=(const MapEngine&) = delete;

  Status SetSurfaceDimensions(int width, int height);
  Status SetCenter(const GeoPoint& center);
  Status SetScale(double scale_denominator);
  Status SetResolution(double dpp_lat, double dpp_lon);  // tile-pyramid mode

  // Physical-display scale for a whole SERIES, given how that series reports
  // its scale in the catalog (SeriesRow.scale / SeriesRow.scale_units):
  //   * a cartographic denominator (MAP_SCALE_DENOMINATOR) is used directly,
  //     so a 1:N map draws at 1:N on an mm_per_pixel screen;
  //   * a ground resolution (MAP_SCALE_METERS / _KILOMETER — what imagery
  //     like a 1 m DOQ reports) is displayed at 100% (one source pixel per
  //     screen pixel) at the reference pitch kNativeDisplayMmPerPixel, and
  //     scaled from there by mm_per_pixel.
  // Either way mm_per_pixel is the single zoom knob (larger = zoomed out).
  // See MapProjection::SetPhysicalScale for the aspect/scale guarantees.
  Status SetPhysicalScale(double series_scale, int series_scale_units,
                          double mm_per_pixel);

  // Turns the chart clockwise on screen (MapProjection::SetRotation). Both
  // paths honour it as of PR3: the vector overlays because they draw through
  // the projection, the base map because CompositeRow resamples through it.
  Status SetRotation(double degrees);

  /// Selects the display projection (MapProjection::SetProjectionType). A
  /// non-affine one puts the base map on the projected raster path.
  Status SetProjectionType(ProjectionType type);

  const MapProjection& CurrentProj() const { return proj_; }

  // Composites all catalog coverage intersecting the viewport (restricted
  // to series_id when nonzero) into canvas, nearest frame data wins by
  // ascending coverage id (catalog order). `interrupted` is polled between
  // frames; returning true aborts with kInterrupted (canvas keeps whatever
  // was drawn). frames_drawn may be null.
  Status RenderBaseMap(ICanvas& canvas, int64_t series_id = 0,
                       const std::function<bool()>& interrupted = {},
                       int* frames_drawn = nullptr);

  // Optional elevation source (e.g. DtedElevationSource); meters/NaN per D4.
  void SetElevationSource(std::shared_ptr<IElevationSource> src);
  Status GetElevation(const GeoPoint& p, float* elevation_meters);

  /// Sends every frame through the non-affine path even when the projection
  /// is affine, so that path can be tested against the affine ones.
  void ForceProjectedPathForTest(bool on) { force_projected_ = on; }

 private:
  std::shared_ptr<IRasterSource> SourceFor(const CoverageRow& row, Status* s);
  Status CompositeRow(const CoverageRow& row, ICanvas& canvas);
  /// The unturned blit of one copy of a frame. The overlap is in the
  /// longitude frame unwrapped around the centre; `frame` is the row's bounds.
  Status CompositeRowStraight(IRasterSource& src, const ImageInfo& info,
                              const GeoRect& frame, double o_top, double o_bot,
                              double o_west, double o_east, ICanvas& canvas);
  /// The turned blit (PR3). Same inputs as CompositeRowStraight.
  Status CompositeRowTurned(IRasterSource& src, const ImageInfo& info,
                            const GeoRect& frame, double o_top, double o_bot,
                            double o_west, double o_east, ICanvas& canvas);
  /// The non-affine blit: the target box is the bounds of the overlap's
  /// projected edges, filled by AdaptiveWarp and masked to the frame.
  Status CompositeRowProjected(IRasterSource& src, const ImageInfo& info,
                               const GeoRect& frame, double o_top,
                               double o_bot, double o_west, double o_east,
                               ICanvas& canvas);
  /// GeoToSurface for a longitude in the centre-unwrapped frame, which may
  /// lie 180 degrees or more from the centre when the view wraps the world.
  Status OverlapToSurface(double lat, double lon, double* sx,
                          double* sy) const;

  std::shared_ptr<Catalog> catalog_;
  std::shared_ptr<IElevationSource> elevation_;
  MapProjection proj_;
  bool force_projected_ = false;

  struct CacheEntry {
    std::string path;
    std::shared_ptr<IRasterSource> source;
  };
  size_t cache_capacity_;
  std::list<CacheEntry> source_cache_;  // front = most recently used
};

}  // namespace fv
