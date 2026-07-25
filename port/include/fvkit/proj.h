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

// Physical pixel pitch of the REFERENCE display, in millimetres. Used only to
// define "100%" for imagery that carries a ground resolution instead of a
// cartographic scale: at this pitch one source pixel maps to one screen pixel.
// An Apple Cinema Display is ~4 px/mm; change this for a different panel.
constexpr double kNativeDisplayMmPerPixel = 0.25;

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

  // Physical-display mode: choose dpp so a feature 1:scale_denominator large
  // is drawn at THAT SAME scale on a device whose pixel pitch is
  // mm_per_pixel, with square ground cells at the center latitude. So at
  // 1:1,000,000 on a 0.25 mm/px screen, 1 cm of screen spans ~10 km of
  // ground. Unlike SetScale (which reproduces each map's baked native pixel
  // density, and only corrects aspect below 1:10M), this gives correct
  // physical scale AND correct aspect at every scale — it drives dpp from
  // real ground distances via the ported MapScaleUtil::ResolutionToDegrees.
  //
  // mm_per_pixel is the zoom knob: larger = more ground per pixel = zoomed
  // out. Mutually exclusive with SetScale/SetResolution; last call wins.
  // Scale() reports scale_denominator in this mode.
  Status SetPhysicalScale(double scale_denominator, double mm_per_pixel);
  double MmPerPixel() const { return mm_per_pixel_; }

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

  // How dpp is derived. kScale = MapScaleUtil native density; kResolution =
  // caller-supplied dpp; kPhysical = physical-display scale (dpp from ground
  // metres/pixel). Last setter wins.
  enum class Mode { kScale, kResolution, kPhysical };

  int width_ = 0, height_ = 0;
  GeoPoint center_;
  double scale_ = 0;
  double dpp_lat_ = 0, dpp_lon_ = 0;
  double mm_per_pixel_ = 0;  // kPhysical only
  bool have_center_ = false;
  Mode mode_ = Mode::kScale;
  bool ready_ = false;
};

// Unwraps lon into the +/-180-degree window around ref (helper shared with
// the engine's per-frame overlap math).
double UnwrapLonNear(double lon, double ref);

}  // namespace fv
