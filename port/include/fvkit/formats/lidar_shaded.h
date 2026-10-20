// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * fvkit/formats/lidar_shaded.h — "lidar-shaded" raster adapter: a GeoTIFF DEM
 * (format "lidar") drawn as shaded relief with the DTED renderer's lighting,
 * colour table and elevation bands, so lidar continues the Elevation map
 * family below DTED. FalconView drew lidar only as an elevation query; this
 * product has no Windows counterpart.
 *
 * One source is one DEM file, enumerated by GeoTiffDemEnumerator. The image
 * is an equal-arc grid over the file's WGS-84 bounds at its native post
 * spacing (for a geographic DEM, one pixel per post); each block is shaded
 * on demand from posts sampled at pixel centres. Voids are transparent.
 */

#pragma once

#include <memory>
#include <string>

#include "fvkit/formats/source.h"

namespace fv {

class LidarShadedRasterSource : public IRasterSource {
 public:
  LidarShadedRasterSource();
  ~LidarShadedRasterSource() override;

  /// Opens one DEM file; the elevation bands are the DTED shaded ones
  /// (SetDtedShadedElevationBands) at the time of the call.
  Status Open(const std::string& path) override;
  GeoRect Bounds() const override;
  Status Info(ImageInfo* info) const override;
  Status ReadBlock(const PixelRect& px_rect, PixelBuffer* out) override;
  Status PixelToGeo(double px, double py, GeoPoint* p) const override;
  Status GeoToPixel(const GeoPoint& p, double* px, double* py) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fv
