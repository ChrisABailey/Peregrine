// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/proj.h — FvKit L3a map projection (equal-arc first, like the
// original's default). Pure math: center + scale + surface size -> degrees
// per pixel (via the ported MapScaleUtil, so dpp matches FalconView) and
// linear geo<->surface transforms. Rotation and the virtual-surface
// variants of ISettableMapProj are deliberately not here yet (plan: stubbed
// until a consumer needs them).
//
// Surface coords are doubles: x right, y down, origin at the surface's
// top-left pixel center, per contracts D4.

#pragma once

#include "fvkit/geo.h"

namespace fv {

class MapProjection {
 public:
  Status SetSurfaceSize(int width, int height);
  Status SetCenter(const GeoPoint& center);   // lon normalized to (-180,180]
  Status SetScale(double scale_denominator);  // 1:N equal-arc (dpp via MapScaleUtil)

  // Explicit-resolution mode (tile pyramids need latitude-independent,
  // exactly reproducible levels): sets dpp directly, bypassing MapScaleUtil.
  // Mutually exclusive with SetScale — whichever was called last wins;
  // Scale() reports 0 in resolution mode.
  Status SetResolution(double dpp_lat, double dpp_lon);

  bool Ready() const { return ready_; }
  PixelSize SurfaceSize() const { return {width_, height_}; }
  GeoPoint Center() const { return center_; }
  double Scale() const { return scale_; }
  double DegPerPixelLat() const { return dpp_lat_; }
  double DegPerPixelLon() const { return dpp_lon_; }

  // Geographic bounds of the surface (lat clamped to +/-90; the rect
  // crosses the antimeridian when the viewport does).
  GeoRect VmapBounds() const;

  // Linear equal-arc transforms. Longitude deltas are taken the short way
  // around relative to the center, so viewports spanning the antimeridian
  // work without special-casing by callers.
  Status GeoToSurface(const GeoPoint& p, double* sx, double* sy) const;
  Status SurfaceToGeo(double sx, double sy, GeoPoint* p) const;

 private:
  Status Update();  // recompute dpp when center+scale are known

  int width_ = 0, height_ = 0;
  GeoPoint center_;
  double scale_ = 0;
  double dpp_lat_ = 0, dpp_lon_ = 0;
  bool have_center_ = false;
  bool explicit_dpp_ = false;
  bool ready_ = false;
};

// Unwraps lon into the +/-180-degree window around ref (helper shared with
// the engine's per-frame overlap math).
double UnwrapLonNear(double lon, double ref);

}  // namespace fv
