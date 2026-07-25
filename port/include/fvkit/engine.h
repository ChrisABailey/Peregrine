// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

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

 private:
  std::shared_ptr<IRasterSource> SourceFor(const CoverageRow& row, Status* s);
  Status CompositeRow(const CoverageRow& row, ICanvas& canvas);

  std::shared_ptr<Catalog> catalog_;
  std::shared_ptr<IElevationSource> elevation_;
  MapProjection proj_;

  struct CacheEntry {
    std::string path;
    std::shared_ptr<IRasterSource> source;
  };
  size_t cache_capacity_;
  std::list<CacheEntry> source_cache_;  // front = most recently used
};

}  // namespace fv
