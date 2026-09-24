// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// AdaptiveWarp — see fvkit/raster_warp.h.

#include "fvkit/raster_warp.h"

#include <cmath>

namespace fv {

namespace {

/// Rounds to nearest, ties to even, as Windows' DOUBLE2INT under _RC_NEAR
/// does. A value outside int range is reported as unmapped rather than
/// wrapped.
bool RoundIndex(double v, int* i) {
  const double r = std::nearbyint(v);
  if (!(r > (double)INT_MIN && r <= (double)INT_MAX)) return false;
  *i = (int)r;
  return true;
}

/// Maps one pixel exactly and stores its rounded source index.
void MapOne(const WarpMap& map, int x, int y, WarpIndex* out) {
  const size_t k = (size_t)y * out->width + x;
  double fx = 0, fy = 0;
  int ix = 0, iy = 0;
  if (map(x, y, &fx, &fy) && RoundIndex(fx, &ix) && RoundIndex(fy, &iy)) {
    out->sx[k] = ix;
    out->sy[k] = iy;
  }
}

/// One rectangle of project_image_hlpr_32bit. Corners are numbered as in
/// Windows: 0 top-left, 1 bottom-left, 2 top-right, 3 bottom-right.
void Warp(const WarpMap& map, int x0s, int y0s, int w, int h, WarpIndex* out) {
  if (w < 8 || h < 8) {
    for (int y = y0s; y < y0s + h; ++y)
      for (int x = x0s; x < x0s + w; ++x) MapOne(map, x, y, out);
    return;
  }

  const int mid_x = x0s + w / 2;
  const int mid_y = y0s + h / 2;
  const int x_last = x0s + w - 1;
  const int y_last = y0s + h - 1;

  // Unlike Windows, the corners stay continuous: there is no integer
  // virtual surface between the display and the source.
  double x0, y0, x1, y1, x2, y2, x3, y3;
  if (map(x0s, y0s, &x0, &y0) && map(x0s, y_last, &x1, &y1) &&
      map(x_last, y0s, &x2, &y2) && map(x_last, y_last, &x3, &y3)) {
    const double denom = (double)(w - 1) * (h - 1);

    // True when the bilinear fit through the corners lands within 0.5 px of
    // the exact mapping at (px, py).
    auto fits = [&](int px, int py) {
      double ax = 0, ay = 0;
      if (!map(px, py, &ax, &ay)) return false;
      const double r = x_last - px, l = px - x0s;
      const double b = y_last - py, t = py - y0s;
      const double ix =
          (x0 * (r * b) + x1 * (r * t) + x2 * (l * b) + x3 * (l * t)) / denom;
      const double iy =
          (y0 * (r * b) + y1 * (r * t) + y2 * (l * b) + y3 * (l * t)) / denom;
      return std::fabs(ax - ix) <= 0.5 && std::fabs(ay - iy) <= 0.5;
    };
    // Windows tests the centre only, which a distortion odd about the centre
    // (Mercator latitude across the equator) passes exactly. The quarter
    // points break that symmetry. The four edge midpoints catch what neither
    // sees: the fit reduces to linear interpolation along an edge, so a map
    // that curves along one — the radial distortion of the azimuthal
    // projections does — deviates most there, where nothing interior looks.
    const int qx0 = x0s + (w - 1) / 4, qx1 = x0s + 3 * (w - 1) / 4;
    const int qy0 = y0s + (h - 1) / 4, qy1 = y0s + 3 * (h - 1) / 4;
    if (fits(mid_x, mid_y) && fits(qx0, qy0) && fits(qx1, qy0) &&
        fits(qx0, qy1) && fits(qx1, qy1) && fits(mid_x, y0s) &&
        fits(mid_x, y_last) && fits(x0s, mid_y) && fits(x_last, mid_y)) {
      // Same accumulation order as Windows: a per-row step down the left
      // edge, a per-pixel step across, and a second difference per row.
      double outer_xp = x0;
      const double delta_outer_xp = (w - 1) * (x1 - x0) / denom;
      double outer_yp = y0;
      const double delta_outer_yp = (w - 1) * (y1 - y0) / denom;
      double delta_x = (x2 - x0) * (h - 1) / denom;
      double delta_y = (y2 - y0) * (h - 1) / denom;
      const double delta_delta_x = (x0 - x1 - x2 + x3) / denom;
      const double delta_delta_y = (y0 - y1 - y2 + y3) / denom;

      for (int y = y0s; y < y0s + h; ++y) {
        double ix = outer_xp;
        outer_xp += delta_outer_xp;
        double iy = outer_yp;
        outer_yp += delta_outer_yp;
        size_t k = (size_t)y * out->width + x0s;
        for (int x = x0s; x < x0s + w; ++x, ++k) {
          int px = 0, py = 0;
          if (RoundIndex(ix, &px) && RoundIndex(iy, &py)) {
            out->sx[k] = px;
            out->sy[k] = py;
          }
          ix += delta_x;
          iy += delta_y;
        }
        delta_x += delta_delta_x;
        delta_y += delta_delta_y;
      }
      return;
    }
  }

  // Quadrants, split after the centre pixel as Windows does.
  const int wl = mid_x - x0s + 1, ht = mid_y - y0s + 1;
  Warp(map, x0s, y0s, wl, ht, out);
  Warp(map, mid_x + 1, y0s, w - wl, ht, out);
  Warp(map, mid_x + 1, mid_y + 1, w - wl, h - ht, out);
  Warp(map, x0s, mid_y + 1, wl, h - ht, out);
}

void Reset(int width, int height, WarpIndex* out) {
  out->width = width;
  out->height = height;
  const size_t n = (size_t)width * height;
  out->sx.assign(n, kWarpUnmapped);
  out->sy.assign(n, kWarpUnmapped);
}

}  // namespace

void AdaptiveWarp(int width, int height, const WarpMap& map, WarpIndex* out) {
  Reset(width, height, out);
  if (width > 0 && height > 0) Warp(map, 0, 0, width, height, out);
}

void ExactWarp(int width, int height, const WarpMap& map, WarpIndex* out) {
  Reset(width, height, out);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) MapOne(map, x, y, out);
}

}  // namespace fv
