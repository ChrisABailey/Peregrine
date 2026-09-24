// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/** @file
 * The adaptive inverse warp behind the non-affine raster path: for every
 * target pixel, the source pixel it samples. Port of
 * gra_projection_dc::project_image_hlpr_32bit (graph/projection_dc.cpp),
 * with the target->source mapping supplied by the caller instead of a
 * Projector and a virtual surface. Not pixel-compatible with Windows: the
 * linearity test adds four quarter points to the Windows centre sample.
 */

#pragma once

#include <climits>
#include <functional>
#include <vector>

namespace fv {

/// Maps target pixel (x, y) to a continuous source pixel coordinate.
/// Returns false when the pixel has no source (off the globe, say).
using WarpMap = std::function<bool(int x, int y, double* sx, double* sy)>;

/// Source index stored for a target pixel the map could not place.
constexpr int kWarpUnmapped = INT_MIN;

/// Source indices for a width x height target, row-major. A source
/// coordinate is rounded to nearest, ties to even; an unmapped pixel holds
/// kWarpUnmapped in both vectors.
struct WarpIndex {
  int width = 0;
  int height = 0;
  std::vector<int> sx;
  std::vector<int> sy;
};

/// Fills `out` by adaptive bilinear subdivision. A rectangle is filled by
/// interpolating its four exactly mapped corners when that interpolation
/// lands within 0.5 px of the exact mapping, on both axes, at the centre and
/// at the four quarter points; otherwise it is split into quadrants.
/// Rectangles under 8x8, and any rectangle with an unmappable corner or test
/// point, are mapped per pixel at the bottom of the recursion.
void AdaptiveWarp(int width, int height, const WarpMap& map, WarpIndex* out);

/// Fills `out` by calling `map` for every pixel. The reference AdaptiveWarp
/// is measured against.
void ExactWarp(int width, int height, const WarpMap& map, WarpIndex* out);

}  // namespace fv
