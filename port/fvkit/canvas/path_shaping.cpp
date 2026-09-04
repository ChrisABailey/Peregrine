// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Path thinning and smoothing — see fvkit/canvas/path_shaping.h.

#include "fvkit/canvas/path_shaping.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fv {
namespace {

double MeanSegmentPx(const std::vector<SurfacePoint>& pts) {
  if (pts.size() < 2) return 0.0;
  double total = 0.0;
  for (size_t i = 1; i < pts.size(); ++i)
    total += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
  return total / (pts.size() - 1);
}

}  // namespace

// Douglas-Peucker, iterative (a contour can be thousands of points and this
// runs per frame; recursion here would be a stack depth nobody has measured).
std::vector<SurfacePoint> DecimatePath(const std::vector<SurfacePoint>& pts,
                                       double tol_px) {
  if (tol_px <= 0.0 || pts.size() < 3) return pts;
  std::vector<bool> keep(pts.size(), false);
  keep.front() = keep.back() = true;
  std::vector<std::pair<size_t, size_t>> stack{{0, pts.size() - 1}};
  const double tol2 = tol_px * tol_px;
  while (!stack.empty()) {
    const size_t lo = stack.back().first, hi = stack.back().second;
    stack.pop_back();
    if (hi <= lo + 1) continue;
    const double ax = pts[lo].x, ay = pts[lo].y;
    const double bx = pts[hi].x, by = pts[hi].y;
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double worst = -1.0;
    size_t worst_i = lo;
    for (size_t i = lo + 1; i < hi; ++i) {
      double d2;
      if (len2 <= 0.0) {
        const double ex = pts[i].x - ax, ey = pts[i].y - ay;
        d2 = ex * ex + ey * ey;
      } else {
        const double cross =
            (pts[i].x - ax) * dy - (pts[i].y - ay) * dx;
        d2 = cross * cross / len2;
      }
      if (d2 > worst) {
        worst = d2;
        worst_i = i;
      }
    }
    if (worst > tol2) {
      keep[worst_i] = true;
      stack.push_back({lo, worst_i});
      stack.push_back({worst_i, hi});
    }
  }
  std::vector<SurfacePoint> out;
  out.reserve(pts.size());
  for (size_t i = 0; i < pts.size(); ++i)
    if (keep[i]) out.push_back(pts[i]);
  return out;
}

// Chaikin corner cutting. APPROXIMATING and convex-hull bounded, which is the
// property that matters on a contour map: a smoothed line stays inside the
// polygon its own vertices make, so it cannot bulge across the contour above
// it. Two contours crossing is a WRONG map, not an ugly one, and an
// interpolating spline can do exactly that on a tight staircase.
std::vector<SurfacePoint> SmoothPathChaikin(std::vector<SurfacePoint> pts,
                                           bool closed, int max_iters,
                                           double target_px) {
  for (int it = 0; it < max_iters; ++it) {
    if (pts.size() < 3) break;
    if (MeanSegmentPx(pts) < target_px) break;
    std::vector<SurfacePoint> next;
    next.reserve(pts.size() * 2);
    const size_t n = closed ? pts.size() - 1 : pts.size();  // ring: drop the
                                                            // repeated vertex
    if (!closed) next.push_back(pts.front());
    for (size_t i = 0; i + 1 <= n - 1 + (closed ? 1 : 0); ++i) {
      const SurfacePoint& a = pts[i % n];
      const SurfacePoint& b = pts[(i + 1) % n];
      next.push_back(SurfacePoint{0.75 * a.x + 0.25 * b.x,
                                  0.75 * a.y + 0.25 * b.y});
      next.push_back(SurfacePoint{0.25 * a.x + 0.75 * b.x,
                                  0.25 * a.y + 0.75 * b.y});
    }
    if (closed) {
      next.push_back(next.front());  // close the ring again
    } else {
      next.push_back(pts.back());
    }
    pts.swap(next);
  }
  return pts;
}

// Centripetal Catmull-Rom -- INTERPOLATING, so the curve passes through every
// traced crossing. Centripetal (alpha = 0.5) and not uniform: the uniform
// parameterisation cusps and self-intersects on exactly the tight corners
// this exists to round off.
std::vector<SurfacePoint> SmoothPathCatmullRom(
    const std::vector<SurfacePoint>& pts, bool closed, double step_px) {
  if (pts.size() < 3) return pts;
  const size_t n = closed ? pts.size() - 1 : pts.size();
  auto at = [&](long i) -> const SurfacePoint& {
    if (closed) {
      long k = i % static_cast<long>(n);
      if (k < 0) k += static_cast<long>(n);
      return pts[static_cast<size_t>(k)];
    }
    if (i < 0) return pts.front();
    if (i >= static_cast<long>(n)) return pts[n - 1];
    return pts[static_cast<size_t>(i)];
  };

  std::vector<SurfacePoint> out;
  out.reserve(pts.size() * 3);
  const long last = static_cast<long>(closed ? n : n - 1);
  for (long i = 0; i < last; ++i) {
    const SurfacePoint p0 = at(i - 1), p1 = at(i), p2 = at(i + 1),
                       p3 = at(i + 2);
    const double d01 = std::sqrt(std::hypot(p1.x - p0.x, p1.y - p0.y));
    const double d12 = std::sqrt(std::hypot(p2.x - p1.x, p2.y - p1.y));
    const double d23 = std::sqrt(std::hypot(p3.x - p2.x, p3.y - p2.y));
    out.push_back(p1);
    const double seg = std::hypot(p2.x - p1.x, p2.y - p1.y);
    int steps = static_cast<int>(std::lround(seg / std::max(1.0, step_px)));
    if (steps < 1) steps = 1;
    if (steps > 16) steps = 16;
    if (d01 <= 0.0 || d12 <= 0.0 || d23 <= 0.0) continue;  // repeated vertex
    const double t0 = 0.0, t1 = d01, t2 = t1 + d12, t3 = t2 + d23;
    for (int k = 1; k < steps; ++k) {
      const double t = t1 + (t2 - t1) * (static_cast<double>(k) / steps);
      auto lerp = [&](const SurfacePoint& a, const SurfacePoint& b, double ta,
                      double tb) {
        const double w = (tb - ta) > 0.0 ? (tb - t) / (tb - ta) : 0.0;
        return SurfacePoint{a.x * w + b.x * (1.0 - w),
                            a.y * w + b.y * (1.0 - w)};
      };
      const SurfacePoint a1 = lerp(p0, p1, t0, t1);
      const SurfacePoint a2 = lerp(p1, p2, t1, t2);
      const SurfacePoint a3 = lerp(p2, p3, t2, t3);
      const SurfacePoint b1 = lerp(a1, a2, t0, t2);
      const SurfacePoint b2 = lerp(a2, a3, t1, t3);
      out.push_back(lerp(b1, b2, t1, t2));
    }
  }
  out.push_back(closed ? out.front() : pts.back());
  return out;
}

}  // namespace fv
