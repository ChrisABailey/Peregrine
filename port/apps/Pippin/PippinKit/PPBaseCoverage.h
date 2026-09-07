// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPBaseCoverage.h — may the cached base map be shown for this camera?
// Pure C++ and header-only, like `PPCameraFit.h` and `PPFollowCadence.h`, so
// the mac gtest owns it rather than the simulator.
//
// The base map is drawn into a surface larger than the screen, a guard band,
// and kept; the overlay (route, points, ownship) is drawn every frame into
// its own transparent surface at the live camera; and the display composites
// the two, applying the scale, turn and offset it already applies to the last
// frame as `MapScreen`'s preview transform. So the band needs no blitter of
// its own: the compositor is the blitter, on the GPU, and handles rotation
// and scale for free, which is what lets a band survive course-up.
//
// This file is the hit test, and it asks three questions in order.
//
//   1. Is the scale exactly the same? Both the tile source and the style
//      engine choose a zoom level from the scale, so a base reused across a
//      zoom is a picture the style never asked for; `VectorScene::CanServe`
//      refuses for the same reason. A pinch therefore misses every frame,
//      which is what it did before the cache existed. See `bandMargin` in
//      `PPMap.h` for why a pinch must not be charged for the band either.
//
//   2. Has the chart turned too far? A quality limit rather than a coverage
//      one: the corner test below would allow a much bigger angle out of a
//      big band, but every degree of turn is resampled by the compositor and
//      a map is the one thing here nobody wants softened. Compared the short
//      way, since 359.9 and 0.1 are a fifth of a degree apart and a
//      subtraction calls them 360.
//
//   3. Does the band still cover the screen? The four corners of the live
//      surface, put through the live projection into the ground and back
//      through the base's, must land inside the base surface. Both transforms
//      are affine, so four corners settle a rectangle exactly, with no
//      sampling and no margin fraction to tune. Eight multiply-adds, which is
//      why it can be asked every frame.
//
// Nothing here is about content. A route replanning, a point being dragged
// and the ownship moving are all overlay changes, and the overlay is redrawn
// every frame regardless. What does invalidate the base from outside is the
// style engine's own epoch — the reference latitude stepping, a mariner
// setting — which is `PPMap`'s business, because only `PPMap` can see it.

#ifndef PIPPIN_PPBASECOVERAGE_H_
#define PIPPIN_PPBASECOVERAGE_H_

#include <cmath>

#include "fvkit/nav/camera_slew.h"  // fv::ShortestRotationDelta
#include "fvkit/proj.h"

namespace pippin {

struct BaseCoverageLimits {
  // Degrees of chart turn the cached base may be shown through. A quality
  // number; the corner test would allow far more.
  double max_rotation_delta_deg = 2.5;

  // How far inside the band a live corner must land, in base-surface pixels.
  // The compositor filters when the transform is not the identity, so a
  // corner exactly on the last row would be sampled half from a row that does
  // not exist. One pixel is enough.
  //
  // Not charged when the transform is the identity, which is the most
  // valuable hit this cache has: a fix arriving under a camera that has not
  // moved is the same base map with a chevron in a new place, and it must be
  // served even at a zero margin, where the screen is the band and every
  // corner sits on an edge.
  double edge_inset_px = 1.0;
};

// The size of the guard band, in the same unit the surface is given in.
// `margin` is the fraction added on each side, so 0.25 is 2.25x the pixels.
// Rounded outward, and never smaller than the surface it grows.
inline void GrownSurfaceSize(double width, double height, double margin,
                             double* out_width, double* out_height) {
  const double m = (margin > 0.0 && std::isfinite(margin)) ? margin : 0.0;
  const double k = 1.0 + 2.0 * m;
  *out_width = std::ceil(width * k);
  *out_height = std::ceil(height * k);
  if (*out_width < width) *out_width = width;
  if (*out_height < height) *out_height = height;
}

// True when the base map drawn for `base` can be composited for `live`
// without being drawn again.
inline bool BaseCovers(const fv::MapProjection& base,
                       const fv::MapProjection& live,
                       const BaseCoverageLimits& limits = {}) {
  if (!base.Ready() || !live.Ready()) return false;

  // 1. The scale, exactly. Both are physical-scale projections, so the pitch
  //    must match too: the same 1:N on a different pitch is a different
  //    number of ground metres per pixel.
  if (!(base.Scale() == live.Scale())) return false;
  if (!(base.MmPerPixel() == live.MmPerPixel())) return false;

  // 2. The turn, the short way.
  const double turn =
      std::fabs(fv::ShortestRotationDelta(base.Rotation(), live.Rotation()));
  if (!(turn <= limits.max_rotation_delta_deg)) return false;

  // 3. The corners. A surface of w pixels covers [-0.5, w-0.5] about its
  //    pixel centres, FalconView's convention which `MapProjection` keeps, so
  //    the extremes are half a pixel outside the first and last centre.
  const fv::PixelSize lsz = live.SurfaceSize();
  const fv::PixelSize bsz = base.SurfaceSize();
  if (lsz.width <= 0 || lsz.height <= 0 || bsz.width <= 0 || bsz.height <= 0)
    return false;

  // Nothing is resampled when the base and the live camera are the same
  // picture, so nothing is inset either. See `edge_inset_px`.
  const bool identity = bsz.width == lsz.width && bsz.height == lsz.height &&
                        base.Center().lat == live.Center().lat &&
                        base.Center().lon == live.Center().lon &&
                        base.Rotation() == live.Rotation();
  const double inset = identity ? 0.0 : limits.edge_inset_px;

  const double lx[2] = {-0.5, (double)lsz.width - 0.5};
  const double ly[2] = {-0.5, (double)lsz.height - 0.5};
  const double lo = -0.5 + inset;
  const double hi_x = (double)bsz.width - 0.5 - inset;
  const double hi_y = (double)bsz.height - 0.5 - inset;
  if (hi_x < lo || hi_y < lo) return false;

  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      fv::GeoPoint g;
      if (!live.SurfaceToGeo(lx[i], ly[j], &g).ok()) return false;
      double bx = 0.0, by = 0.0;
      if (!base.GeoToSurface(g, &bx, &by).ok()) return false;
      if (!std::isfinite(bx) || !std::isfinite(by)) return false;
      if (bx < lo || bx > hi_x || by < lo || by > hi_y) return false;
    }
  }
  return true;
}

}  // namespace pippin

#endif  // PIPPIN_PPBASECOVERAGE_H_
