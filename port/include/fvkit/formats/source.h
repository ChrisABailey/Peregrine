// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/formats/source.h — FvKit L1 data-source interfaces.
// Contracts: port/fvkit-contracts.md (D1 ownership, D4 data contracts).
//
// Interfaces are held as std::shared_ptr, have virtual dtors, no data.
// This header must stay C++14-parseable (see fvkit/raster.h).

#pragma once

#include <string>

#include "fvkit/geo.h"
#include "fvkit/raster.h"

namespace fv {

// A georeferenced raster (one frame/file). Usage: Open once, then query.
// ReadBlock is the only pixel path — block reads, never per-pixel (CADRG
// perf). The block rect must lie entirely inside the image; callers clip
// first (kInvalidArg otherwise, no partial fills).
// PixelToGeo/GeoToPixel are the source's EXACT transforms (see raster.h on
// why there is no stored affine); pixel coords are doubles for sub-pixel
// composition, (0,0) = center of the top-left pixel as in the legacy
// readers.
struct IRasterSource {
  virtual ~IRasterSource() = default;
  IRasterSource(const IRasterSource&) = delete;
  IRasterSource& operator=(const IRasterSource&) = delete;

  virtual Status Open(const std::string& path) = 0;
  virtual GeoRect Bounds() const = 0;
  virtual Status Info(ImageInfo* info) const = 0;
  virtual Status ReadBlock(const PixelRect& px_rect, PixelBuffer* out) = 0;
  virtual Status PixelToGeo(double px, double py, GeoPoint* p) const = 0;
  virtual Status GeoToPixel(const GeoPoint& p, double* px, double* py) const = 0;

 protected:
  IRasterSource() = default;
};

// A queryable elevation coverage (one or many cells/files behind it).
// Elevations are METERS as float (D4); void/partial posts come back as NaN
// with Status ok; a point outside Bounds()/coverage is kOutOfCoverage.
// The legacy feet/meters enum and raw sentinels (-32767) stay below the
// adapter, in the bit-faithful layer.
struct IElevationSource {
  virtual ~IElevationSource() = default;
  IElevationSource(const IElevationSource&) = delete;
  IElevationSource& operator=(const IElevationSource&) = delete;

  // Bounding box of the coverage (may include interior gaps; a Contains()
  // hit does not guarantee data — GetElevation is the ground truth).
  virtual GeoRect Bounds() const = 0;

  virtual Status GetElevation(const GeoPoint& p, float* elevation_meters) = 0;

  // The NATIVE post spacing at `p`, in degrees, when the source has one.
  // Returns false for "I don't know", which is the default and is a legal
  // answer -- a caller that asks is choosing a sampling rate and must have a
  // fallback anyway.
  //
  // Why it exists (contour overlay, plan C2): sampling elevation finer than
  // the posts invents terrain, so a consumer building a grid wants
  // max(what the screen needs, what the data actually has). Latitude and
  // longitude are separate because a DTED cell thins its east-west posts
  // toward the poles -- one value would be wrong above 50 degrees.
  virtual bool PostSpacing(const GeoPoint& p, double* lat_deg,
                           double* lon_deg) {
    (void)p;
    (void)lat_deg;
    (void)lon_deg;
    return false;
  }

 protected:
  IElevationSource() = default;
};

}  // namespace fv
