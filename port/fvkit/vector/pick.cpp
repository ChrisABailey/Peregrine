// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/vector/pick.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fv {
namespace {

PixelRect BoundsOfPoints(const std::vector<PixelPoint>& pts, double grow) {
  PixelRect r;
  if (pts.empty()) return r;
  int minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
  for (const PixelPoint& p : pts) {
    minx = std::min(minx, p.x);
    maxx = std::max(maxx, p.x);
    miny = std::min(miny, p.y);
    maxy = std::max(maxy, p.y);
  }
  const int g = static_cast<int>(std::ceil(grow));
  r.x = minx - g;
  r.y = miny - g;
  r.width = (maxx - minx) + 2 * g + 1;
  r.height = (maxy - miny) + 2 * g + 1;
  return r;
}

bool InflatedRectContains(const PixelRect& r, double x, double y, double tol) {
  return x >= r.x - tol && y >= r.y - tol && x <= r.x + r.width + tol &&
         y <= r.y + r.height + tol;
}

// Distance from (px,py) to the rectangle; 0 when inside.
double DistanceToRect(const PixelRect& r, double px, double py) {
  const double x0 = r.x, y0 = r.y;
  const double x1 = r.x + std::max(r.width - 1, 0);
  const double y1 = r.y + std::max(r.height - 1, 0);
  const double dx = std::max({x0 - px, 0.0, px - x1});
  const double dy = std::max({y0 - py, 0.0, py - y1});
  return std::hypot(dx, dy);
}

double DistanceToSegment(double px, double py, const PixelPoint& a,
                         const PixelPoint& b) {
  const double ax = a.x, ay = a.y, bx = b.x, by = b.y;
  const double vx = bx - ax, vy = by - ay;
  const double len2 = vx * vx + vy * vy;
  if (len2 <= 0.0) return std::hypot(px - ax, py - ay);
  double t = ((px - ax) * vx + (py - ay) * vy) / len2;
  t = std::max(0.0, std::min(1.0, t));
  return std::hypot(px - (ax + t * vx), py - (ay + t * vy));
}

double DistanceToPolyline(const std::vector<PixelPoint>& pts, double px,
                          double py) {
  if (pts.empty()) return std::numeric_limits<double>::infinity();
  if (pts.size() == 1) return std::hypot(px - pts[0].x, py - pts[0].y);
  double best = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i + 1 < pts.size(); ++i)
    best = std::min(best, DistanceToSegment(px, py, pts[i], pts[i + 1]));
  return best;
}

// Even-odd point-in-polygon, the same rule CpuCanvas fills a ring with (GDI
// ALTERNATE), so "inside the fill" here means the pixel really is painted.
bool PointInRing(const std::vector<PixelPoint>& ring, double px, double py) {
  bool in = false;
  const size_t n = ring.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const double yi = ring[i].y, yj = ring[j].y;
    const double xi = ring[i].x, xj = ring[j].x;
    if ((yi > py) != (yj > py)) {
      const double x_at = (xj - xi) * (py - yi) / (yj - yi) + xi;
      if (px < x_at) in = !in;
    }
  }
  return in;
}

// Distance to a closed ring's boundary (the ring's last->first edge included).
double DistanceToRingEdge(const std::vector<PixelPoint>& ring, double px,
                          double py) {
  double best = std::numeric_limits<double>::infinity();
  const size_t n = ring.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++)
    best = std::min(best, DistanceToSegment(px, py, ring[j], ring[i]));
  return best;
}

}  // namespace

void PickIndex::Clear() { shapes_.clear(); }

void PickIndex::AddStroke(const FeatureRef& ref, int priority,
                          const std::vector<PixelPoint>& run,
                          double half_width) {
  if (run.empty()) return;
  Shape s;
  s.kind = Kind::kStroke;
  s.ref = ref;
  s.priority = priority;
  s.order = shapes_.size();
  s.half_width = std::max(0.0, half_width);
  s.pts = run;
  s.box = BoundsOfPoints(run, s.half_width);
  shapes_.push_back(std::move(s));
}

void PickIndex::AddFill(const FeatureRef& ref, int priority,
                        const std::vector<PixelPoint>& ring) {
  if (ring.size() < 3) return;
  Shape s;
  s.kind = Kind::kFill;
  s.ref = ref;
  s.priority = priority;
  s.order = shapes_.size();
  s.pts = ring;
  s.box = BoundsOfPoints(ring, 0.0);
  shapes_.push_back(std::move(s));
}

void PickIndex::AddBox(const FeatureRef& ref, int priority,
                       const PixelRect& box) {
  if (box.width <= 0 || box.height <= 0) return;
  Shape s;
  s.kind = Kind::kBox;
  s.ref = ref;
  s.priority = priority;
  s.order = shapes_.size();
  s.box = box;
  shapes_.push_back(std::move(s));
}

std::vector<PickHit> PickIndex::HitTest(int x, int y, double tolerance) const {
  const double px = x, py = y;
  const double tol = std::max(0.0, tolerance);

  // Accumulate one entry per feature: the topmost primitive that hit, carrying
  // the smallest distance any of that feature's primitives reported.
  struct Acc {
    PickHit hit;
    size_t order = 0;
  };
  std::vector<Acc> acc;

  for (const Shape& s : shapes_) {
    if (!InflatedRectContains(s.box, px, py, tol)) continue;

    double d = std::numeric_limits<double>::infinity();
    switch (s.kind) {
      case Kind::kStroke: {
        const double raw = DistanceToPolyline(s.pts, px, py);
        d = std::max(0.0, raw - s.half_width);  // distance to the INK
        break;
      }
      case Kind::kFill:
        d = PointInRing(s.pts, px, py) ? 0.0 : DistanceToRingEdge(s.pts, px, py);
        break;
      case Kind::kBox:
        d = DistanceToRect(s.box, px, py);
        break;
    }
    if (!(d <= tol)) continue;  // also rejects NaN

    auto it = std::find_if(acc.begin(), acc.end(), [&](const Acc& a) {
      return a.hit.ref == s.ref;
    });
    if (it == acc.end()) {
      Acc a;
      a.hit.ref = s.ref;
      a.hit.priority = s.priority;
      a.hit.distance = d;
      a.order = s.order;
      acc.push_back(a);
    } else {
      it->hit.distance = std::min(it->hit.distance, d);
      // "Topmost" for a feature drawn in several passes is its last pass.
      if (s.priority > it->hit.priority ||
          (s.priority == it->hit.priority && s.order > it->order)) {
        it->hit.priority = s.priority;
        it->order = s.order;
      }
    }
  }

  std::sort(acc.begin(), acc.end(), [](const Acc& a, const Acc& b) {
    if (a.hit.priority != b.hit.priority)
      return a.hit.priority > b.hit.priority;  // higher priority drawn later
    return a.order > b.order;                  // later draw is on top
  });

  std::vector<PickHit> out;
  out.reserve(acc.size());
  for (const Acc& a : acc) out.push_back(a.hit);
  return out;
}

}  // namespace fv
