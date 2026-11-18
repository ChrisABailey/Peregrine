// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/vector/scene.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <utility>

#include "fvkit/draw_interrupt.h"

namespace fv {
namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// One styled pass, before flattening. Same tie-break the renderer used before
// the scene existed: display priority, then query order.
struct PendingItem {
  size_t feature = 0;
  size_t result = 0;
  int priority = 0;
  size_t order = 0;
};

// Perpendicular distance from p to the segment a-b, with each axis scaled so
// that "one unit" is one pixel on both. Squared, to keep the walk free of
// square roots.
double SegmentDistanceSq(const GeoPoint& p, const GeoPoint& a,
                         const GeoPoint& b, double sx, double sy) {
  const double px = (p.lon - a.lon) * sx, py = (p.lat - a.lat) * sy;
  const double bx = (b.lon - a.lon) * sx, by = (b.lat - a.lat) * sy;
  const double len2 = bx * bx + by * by;
  if (len2 <= 0.0) return px * px + py * py;
  double t = (px * bx + py * by) / len2;
  t = (std::max)(0.0, (std::min)(1.0, t));
  const double dx = px - bx * t, dy = py - by * t;
  return dx * dx + dy * dy;
}

// Iterative Douglas-Peucker. Explicit stack rather than recursion: a DNC
// coastline edge is tens of thousands of vertices and the recursion depth is
// data-controlled.
void DouglasPeucker(const std::vector<GeoPoint>& pts, double sx, double sy,
                    double tol2, std::vector<char>* keep) {
  std::vector<std::pair<size_t, size_t>> stack;
  stack.emplace_back(0, pts.size() - 1);
  while (!stack.empty()) {
    const size_t lo = stack.back().first, hi = stack.back().second;
    stack.pop_back();
    if (hi <= lo + 1) continue;

    double worst = -1.0;
    size_t at = lo;
    for (size_t i = lo + 1; i < hi; ++i) {
      const double d = SegmentDistanceSq(pts[i], pts[lo], pts[hi], sx, sy);
      if (d > worst) {
        worst = d;
        at = i;
      }
    }
    if (worst <= tol2) continue;
    (*keep)[at] = 1;
    stack.emplace_back(lo, at);
    stack.emplace_back(at, hi);
  }
}

// Simplifies `pts` onto the END of `out`, which is what the scene builder
// wants: it is appending into one flat array and a per-part temporary vector
// would be an allocation per part per feature — the exact cost the columnar
// layout exists to avoid. `keep` is the caller's scratch buffer, reused across
// parts. Returns the number of vertices appended.
size_t AppendSimplified(const std::vector<GeoPoint>& pts, double tol_lat,
                        double tol_lon, bool closed, std::vector<char>* keep,
                        std::vector<GeoPoint>* out) {
  if (pts.size() < 3 || !(tol_lat > 0.0) || !(tol_lon > 0.0)) {
    out->insert(out->end(), pts.begin(), pts.end());
    return pts.size();
  }

  // Work in "pixels": one unit per tolerance, so a single squared tolerance of
  // 1 covers an anisotropic projection without two separate tests.
  const double sx = 1.0 / tol_lon, sy = 1.0 / tol_lat;

  keep->assign(pts.size(), 0);
  keep->front() = 1;
  keep->back() = 1;
  DouglasPeucker(pts, sx, sy, 1.0, keep);

  size_t kept = 0;
  for (char c : *keep) kept += c ? 1 : 0;

  // A ring must still be a ring. DP keeps the first and last vertex, so a
  // closed ring can collapse to two distinct points and a degenerate sliver;
  // keep the original rather than draw that.
  if ((closed && kept < 4) || kept < 2) {
    out->insert(out->end(), pts.begin(), pts.end());
    return pts.size();
  }
  for (size_t i = 0; i < pts.size(); ++i)
    if ((*keep)[i]) out->push_back(pts[i]);
  return kept;
}

}  // namespace

std::vector<GeoPoint> SimplifyPath(const std::vector<GeoPoint>& pts,
                                   double tol_lat, double tol_lon,
                                   bool closed) {
  std::vector<GeoPoint> out;
  std::vector<char> keep;
  out.reserve(pts.size());
  AppendSimplified(pts, tol_lat, tol_lon, closed, &keep, &out);
  return out;
}

namespace {

/// Longitudes where the parallel at `lat` crosses any ring, sorted, using the
/// same half-open edge rule as the renderer's point-in-ring test. Coordinates
/// are relative to `origin`.
std::vector<double> ParallelCuts(const std::vector<std::vector<GeoPoint>>& rings,
                                 const GeoPoint& origin, double lat) {
  std::vector<double> cuts;
  for (const std::vector<GeoPoint>& r : rings) {
    if (r.size() < 3) continue;
    for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
      const double yi = r[i].lat - origin.lat, yj = r[j].lat - origin.lat;
      if ((yi > lat) == (yj > lat)) continue;
      const double xi = r[i].lon - origin.lon, xj = r[j].lon - origin.lon;
      cuts.push_back(xi + (lat - yi) * (xj - xi) / (yj - yi));
    }
  }
  std::sort(cuts.begin(), cuts.end());
  return cuts;
}

/// Midpoint of the widest interior span among sorted even-odd cuts.
bool WidestSpan(const std::vector<double>& cuts, double* mid) {
  double best = -1.0;
  for (size_t i = 0; i + 1 < cuts.size(); i += 2) {
    const double w = cuts[i + 1] - cuts[i];
    if (w > best) {
      best = w;
      *mid = (cuts[i] + cuts[i + 1]) / 2.0;
    }
  }
  return best >= 0.0;
}

}  // namespace

bool AreaInteriorPoint(const std::vector<std::vector<GeoPoint>>& rings,
                       GeoPoint* out) {
  if (rings.empty() || rings[0].size() < 3 || out == nullptr) return false;
  const std::vector<GeoPoint>& outer = rings[0];
  const GeoPoint o = outer[0];  // origin, for precision in small cells

  double a2 = 0.0, cx = 0.0, cy = 0.0;
  double minx = 0.0, maxx = 0.0, miny = 0.0, maxy = 0.0, sx = 0.0, sy = 0.0;
  for (size_t i = 0, j = outer.size() - 1; i < outer.size(); j = i++) {
    const double xi = outer[i].lon - o.lon, yi = outer[i].lat - o.lat;
    const double xj = outer[j].lon - o.lon, yj = outer[j].lat - o.lat;
    const double c = xj * yi - xi * yj;
    a2 += c;
    cx += (xj + xi) * c;
    cy += (yj + yi) * c;
    sx += xi;
    sy += yi;
    minx = std::min(minx, xi);
    maxx = std::max(maxx, xi);
    miny = std::min(miny, yi);
    maxy = std::max(maxy, yi);
  }
  const double n = static_cast<double>(outer.size());
  // A sliver with no area has no centroid; its vertex mean is the middle.
  const double span = std::max(maxx - minx, maxy - miny);
  if (std::fabs(a2) > 1e-12 * span * span) {
    cx /= 3.0 * a2;
    cy /= 3.0 * a2;
  } else {
    cx = sx / n;
    cy = sy / n;
  }

  std::vector<double> cuts = ParallelCuts(rings, o, cy);
  const size_t right = static_cast<size_t>(
      cuts.end() - std::upper_bound(cuts.begin(), cuts.end(), cx));
  if ((right & 1) == 0) {
    // Centroid outside the area (a crescent, or inside a hole).
    double mid = 0.0;
    if (!WidestSpan(cuts, &mid)) {
      cy = (miny + maxy) / 2.0;
      cuts = ParallelCuts(rings, o, cy);
      if (!WidestSpan(cuts, &mid)) mid = cx;
    }
    cx = mid;
  }
  *out = GeoPoint{o.lat + cy, o.lon + cx};
  return true;
}

namespace {

/// Bounding box of `n` >= 1 points, longitudes taken as stored.
GeoRect BoundsOf(const GeoPoint* pts, size_t n) {
  GeoRect b{pts[0], pts[0]};
  for (size_t i = 1; i < n; ++i) {
    b.ll.lat = std::min(b.ll.lat, pts[i].lat);
    b.ll.lon = std::min(b.ll.lon, pts[i].lon);
    b.ur.lat = std::max(b.ur.lat, pts[i].lat);
    b.ur.lon = std::max(b.ur.lon, pts[i].lon);
  }
  return b;
}

}  // namespace

void VectorScene::Clear() {
  items_.clear();
  styles_.clear();
  points_.clear();
  part_first_.clear();
  unstroked_.clear();
  part_bounds_.clear();
  built_ = false;
  features_ = 0;
  vertices_in_ = 0;
  query_ms_ = 0.0;
  style_ms_ = 0.0;
}

bool VectorScene::CanServe(const GeoRect& view, const StyleContext& ctx,
                           uint64_t style_epoch) const {
  if (!built_) return false;
  if (style_epoch != style_epoch_) return false;
  if (ctx.scale_denominator != ctx_.scale_denominator) return false;
  if (ctx.device_dpi != ctx_.device_dpi) return false;
  if (ctx.symbol_scale != ctx_.symbol_scale) return false;
  // See the header: wrapped-longitude containment is not worth the trap.
  if (view.CrossesAntimeridian() || area_.CrossesAntimeridian()) return false;
  return view.ll.lat >= area_.ll.lat && view.ur.lat <= area_.ur.lat &&
         view.ll.lon >= area_.ll.lon && view.ur.lon <= area_.ur.lon;
}

Status VectorScene::Build(IVectorSource* source, IStyleEngine* style,
                          const SceneBuildParams& params) {
  Clear();
  if (source == nullptr || style == nullptr)
    return Status::Error(kInvalidArg, "scene needs a source and a style");

  VectorQuery q;
  q.area = params.area;
  q.scale_denominator = params.ctx.scale_denominator;
  q.max_features = params.max_features;

  const Clock::time_point t_query = Clock::now();
  std::vector<VectorFeature> features;
  Status s = source->Query(q, &features);
  if (!s.ok()) return s;
  query_ms_ = MsSince(t_query);
  features_ = features.size();

  const Clock::time_point t_style = Clock::now();
  std::vector<std::vector<StyleResult>> styled(features.size());
  std::vector<PendingItem> pending;
  for (size_t i = 0; i < features.size(); ++i) {
    if (DrawInterruptedAt(i)) {
      Clear();
      return Status::Error(kInterrupted, "scene build interrupted");
    }
    Status ss = style->Style(features[i], params.ctx, &styled[i]);
    if (!ss.ok()) {
      Clear();
      return ss;
    }
    for (size_t r = 0; r < styled[i].size(); ++r) {
      if (!styled[i][r].visible) continue;
      PendingItem it;
      it.feature = i;
      it.result = r;
      it.priority = styled[i][r].priority;
      it.order = pending.size();
      pending.push_back(it);
    }
  }
  std::stable_sort(pending.begin(), pending.end(),
                   [](const PendingItem& a, const PendingItem& b) {
                     if (a.priority != b.priority) return a.priority < b.priority;
                     return a.order < b.order;
                   });
  style_ms_ = MsSince(t_style);

  // Flatten in draw order. A feature drawn by several passes has its geometry
  // stored once per pass: the passes are not adjacent after the sort (that is
  // the whole point of sorting across features), and one shared vertex range
  // would need an indirection on the hot path to save a copy of geometry that
  // is a few percent of the scene. Revisit with the columnar FeatureBatch.
  const bool simplify = params.simplify_px > 0.0 && params.dpp_x > 0.0 &&
                        params.dpp_y > 0.0;
  const double tol_lon = params.simplify_px * params.dpp_x;
  const double tol_lat = params.simplify_px * params.dpp_y;

  items_.reserve(pending.size());
  styles_.reserve(pending.size());
  part_first_.reserve(pending.size() + 1);
  part_first_.push_back(0);
  part_bounds_.reserve(pending.size());
  std::vector<char> keep_scratch;
  bool any_unstroked = false;
  for (const PendingItem& p : pending) {
    const VectorFeature& f = features[p.feature];

    SceneItem item;
    item.ref = f.ref;
    item.type = f.type;
    item.priority = p.priority;
    item.style = static_cast<uint32_t>(styles_.size());
    item.first_part = static_cast<uint32_t>(part_first_.size() - 1);

    uint32_t parts = 0;
    for (size_t k = 0; k < f.parts.size(); ++k) {
      const std::vector<GeoPoint>& part = f.parts[k];
      if (part.empty()) continue;
      vertices_in_ += part.size();

      const std::vector<uint8_t>* hidden =
          k < f.unstroked_edges.size() && !f.unstroked_edges[k].empty()
              ? &f.unstroked_edges[k]
              : nullptr;
      // The flags are allocated on the first part that has any, so a scene
      // without hidden edges pays nothing.
      if (hidden != nullptr && !any_unstroked) {
        unstroked_.assign(points_.size(), 0);
        any_unstroked = true;
      }

      // A point is never simplified, and neither is anything below the
      // three vertices DP needs. A part with hidden edges keeps every vertex
      // so its flags stay aligned.
      if (simplify && f.type != VectorGeometryType::kPoint &&
          hidden == nullptr) {
        AppendSimplified(part, tol_lat, tol_lon,
                         f.type == VectorGeometryType::kArea, &keep_scratch,
                         &points_);
      } else {
        points_.insert(points_.end(), part.begin(), part.end());
      }
      if (any_unstroked) {
        const size_t n = points_.size() - unstroked_.size();
        if (hidden != nullptr && hidden->size() == n)
          unstroked_.insert(unstroked_.end(), hidden->begin(), hidden->end());
        else
          unstroked_.resize(points_.size(), 0);
      }
      part_bounds_.push_back(
          BoundsOf(&points_[part_first_.back()], points_.size() - part_first_.back()));
      part_first_.push_back(static_cast<uint32_t>(points_.size()));
      ++parts;
    }
    if (parts == 0) continue;  // nothing drawable; drop the pass entirely

    item.part_count = parts;
    const StyleResult& sr = styled[p.feature][p.result];
    const bool centred =
        (sr.symbol.valid && sr.symbol.anchor == LabelAnchor::kCentre) ||
        (sr.label.valid && sr.label.placement == LabelPlacement::kPoint &&
         sr.label.anchor == LabelAnchor::kCentre);
    // From the source rings, not the simplified copy, so the anchor does not
    // shift with the simplification tolerance.
    if (centred && f.type == VectorGeometryType::kArea)
      item.has_centre = AreaInteriorPoint(f.parts, &item.centre);
    // The centre comes from the unsimplified rings, so it can lie just outside
    // the stored outer ring's box.
    if (item.has_centre) {
      GeoRect& b = part_bounds_[item.first_part];
      b.ll.lat = std::min(b.ll.lat, item.centre.lat);
      b.ll.lon = std::min(b.ll.lon, item.centre.lon);
      b.ur.lat = std::max(b.ur.lat, item.centre.lat);
      b.ur.lon = std::max(b.ur.lon, item.centre.lon);
    }
    items_.push_back(item);
    // Each (feature, result) pair produces exactly one PendingItem, so this
    // StyleResult — strings, pens, a run list — has no other owner to steal it
    // from.
    styles_.push_back(std::move(styled[p.feature][p.result]));
  }

  area_ = params.area;
  ctx_ = params.ctx;
  style_epoch_ = style->style_epoch();
  built_ = true;
  return Status::Ok();
}

}  // namespace fv
