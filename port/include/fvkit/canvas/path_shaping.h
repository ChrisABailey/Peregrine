// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/canvas/path_shaping.h — thinning and smoothing for an already
// projected path. The SHARED OVERLAY TOOLKIT's fourth piece (with
// scale_table.h, label_placer.h and app/properties.h): if you are drawing a
// dense line that came out of sampled data -- a contour, a terrain-avoidance
// boundary, a decoded track, an isochrone -- this is where the pixel-space
// tidying lives, and no overlay should grow its own.
//
// IT IS IN PIXELS AND THAT IS THE DESIGN. Both operations answer questions
// about the picture, not about the world: "this vertex does not move the line
// by half a pixel" and "this corner is sharper than the data can justify at
// this zoom". Doing either in geographic space means re-deciding it at every
// zoom level, and caching the result of a decision that the next frame
// invalidates.
//
// The two are meant to be used in this order -- thin, then smooth. Thinning
// first keeps the smoother from rounding the corners between a thousand
// sub-pixel vertices; smoothing second fits the curve to the vertices that
// actually carry the shape.
//
// A CLOSED path is one whose last point equals its first. Say so with the
// `closed` flag and the seam is treated as an ordinary vertex; leave it false
// and the ring keeps a visible corner where it joins itself.

#ifndef FVKIT_CANVAS_PATH_SHAPING_H_
#define FVKIT_CANVAS_PATH_SHAPING_H_

#include <vector>

#include "fvkit/geo.h"

namespace fv {

// Douglas-Peucker: drops every vertex that is within `tol_px` of the chord
// standing in for it. Endpoints are always kept. tol_px <= 0 returns the
// input unchanged, which is the "keep every vertex" setting.
//
// Iterative rather than recursive on purpose: a traced contour can be
// thousands of points and this runs inside a draw.
std::vector<SurfacePoint> DecimatePath(const std::vector<SurfacePoint>& pts,
                                       double tol_px);

// Chaikin corner cutting, iterated until the mean segment is shorter than
// `target_px` or `max_iters` is reached (each iteration roughly doubles the
// vertex count, so the cap is what bounds the cost).
//
// APPROXIMATING and convex-hull bounded, and on a contour map that is the
// property that decides it: the smoothed line stays inside the polygon its
// own vertices make, so it cannot bulge across the contour above it. Two
// contours crossing is a WRONG map, not an ugly one.
std::vector<SurfacePoint> SmoothPathChaikin(std::vector<SurfacePoint> pts,
                                            bool closed, int max_iters = 3,
                                            double target_px = 4.0);

// Centripetal Catmull-Rom: INTERPOLATING, so the curve passes through every
// input vertex, with about one sample every `step_px` along each segment
// (capped at 16 per segment).
//
// Centripetal (alpha = 0.5) rather than uniform, because the uniform
// parameterisation cusps and self-intersects on exactly the tight corners
// this exists to round off.
std::vector<SurfacePoint> SmoothPathCatmullRom(
    const std::vector<SurfacePoint>& pts, bool closed, double step_px = 4.0);

}  // namespace fv

#endif  // FVKIT_CANVAS_PATH_SHAPING_H_
