// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Terrain contour tracing — see fvkit/geo/terrain_contour.h.

#include "fvkit/geo/terrain_contour.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

namespace fv {
namespace {

// An edge of the post lattice, as one integer. A horizontal edge runs east
// from post (row, col); a vertical edge runs north from it. Two cells sharing
// an edge compute the same id AND the same crossing point (the same two posts
// through the same expression), which is what lets the join pass match chains
// by identity rather than by distance.
enum EdgeKind { kHoriz = 0, kVert = 1 };

inline int64_t EdgeId(int row, int col, int width, EdgeKind kind) {
  return ((static_cast<int64_t>(row) * width) + col) * 2 + kind;
}

struct Segment {
  int64_t a = 0, b = 0;  // edge ids, in the order the case table names them
};

// One level's worth of work: the crossing found on each edge, and the
// segments joining them.
struct LevelWork {
  std::unordered_map<int64_t, GeoPoint> points;
  std::vector<Segment> segments;
};

// Guard against an interval so fine, relative to the relief, that a cell
// would emit thousands of levels: a mistyped setting should draw a bad map,
// not hang the frame.
constexpr int kMaxLevelsPerCell = 4096;

// Crossing on the edge between posts `lo` and `hi` (in that lattice order),
// as the fraction along it. Only called when the two straddle `v`.
inline double CrossFraction(double lo, double hi, double v) {
  const double d = hi - lo;
  if (d == 0.0) return 0.0;  // unreachable while the two straddle v
  double t = (v - lo) / d;
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;
  return t;
}

// Registers (idempotently) the crossing on one edge and returns its id.
int64_t AddCrossing(const ElevationGrid& g, LevelWork* w, double v, int row,
                    int col, EdgeKind kind) {
  const int64_t id = EdgeId(row, col, g.width, kind);
  auto it = w->points.find(id);
  if (it != w->points.end()) return id;

  const double lat0 = g.bounds.ll.lat, lon0 = g.bounds.ll.lon;
  const double dlat = g.LatStep(), dlon = g.LonStep();
  GeoPoint p;
  if (kind == kHoriz) {
    const double t = CrossFraction(g.At(row, col), g.At(row, col + 1), v);
    p.lat = lat0 + row * dlat;
    p.lon = lon0 + (col + t) * dlon;
  } else {
    const double t = CrossFraction(g.At(row, col), g.At(row + 1, col), v);
    p.lat = lat0 + (row + t) * dlat;
    p.lon = lon0 + col * dlon;
  }
  w->points.emplace(id, p);
  return id;
}

// The four edges of cell (row, col), by compass side.
inline int64_t SouthEdge(const ElevationGrid& g, LevelWork* w, double v, int r,
                         int c) {
  return AddCrossing(g, w, v, r, c, kHoriz);
}
inline int64_t NorthEdge(const ElevationGrid& g, LevelWork* w, double v, int r,
                         int c) {
  return AddCrossing(g, w, v, r + 1, c, kHoriz);
}
inline int64_t WestEdge(const ElevationGrid& g, LevelWork* w, double v, int r,
                        int c) {
  return AddCrossing(g, w, v, r, c, kVert);
}
inline int64_t EastEdge(const ElevationGrid& g, LevelWork* w, double v, int r,
                        int c) {
  return AddCrossing(g, w, v, r, c + 1, kVert);
}

// Marching squares over one cell at one level. Corner bits, SW-SE-NE-NW:
//   1 = SW above, 2 = SE, 4 = NE, 8 = NW. "Above" is strictly greater, so a
// post exactly on the level is below it (see the header).
void MarchCell(const ElevationGrid& g, LevelWork* w, double v, int r, int c) {
  const double sw = g.At(r, c), se = g.At(r, c + 1);
  const double ne = g.At(r + 1, c + 1), nw = g.At(r + 1, c);

  const int code = (sw > v ? 1 : 0) | (se > v ? 2 : 0) | (ne > v ? 4 : 0) |
                   (nw > v ? 8 : 0);
  if (code == 0 || code == 15) return;

  auto emit = [&](int64_t a, int64_t b) { w->segments.push_back(Segment{a, b}); };
  auto S = [&] { return SouthEdge(g, w, v, r, c); };
  auto N = [&] { return NorthEdge(g, w, v, r, c); };
  auto W = [&] { return WestEdge(g, w, v, r, c); };
  auto E = [&] { return EastEdge(g, w, v, r, c); };

  switch (code) {
    case 1: case 14: emit(S(), W()); break;
    case 2: case 13: emit(S(), E()); break;
    case 3: case 12: emit(W(), E()); break;
    case 4: case 11: emit(E(), N()); break;
    case 6: case 9:  emit(S(), N()); break;
    case 7: case 8:  emit(W(), N()); break;
    // The two saddles. The cell's mean says whether the CENTRE is above the
    // level, and that is what decides which pair of corners the contour
    // separates. FalconView had no rule here at all -- its multimap handed
    // back whichever neighbour it happened to hold first -- so the same tile
    // could split a saddle two ways on two draws.
    case 5:
      if ((sw + se + ne + nw) * 0.25 > v) {
        emit(S(), E());
        emit(W(), N());
      } else {
        emit(S(), W());
        emit(E(), N());
      }
      break;
    case 10:
      if ((sw + se + ne + nw) * 0.25 > v) {
        emit(S(), W());
        emit(E(), N());
      } else {
        emit(S(), E());
        emit(W(), N());
      }
      break;
    default: break;
  }
}

// Chains one level's segments into polylines. Open chains first (walked from
// a free end), then whatever is left, which can only be rings.
void JoinLevel(const LevelWork& w, double level_m, int level_index,
               std::vector<ContourLine>* out) {
  if (w.segments.empty()) return;

  std::unordered_map<int64_t, std::vector<size_t>> at_edge;
  at_edge.reserve(w.segments.size() * 2);
  for (size_t i = 0; i < w.segments.size(); ++i) {
    at_edge[w.segments[i].a].push_back(i);
    at_edge[w.segments[i].b].push_back(i);
  }

  std::vector<bool> used(w.segments.size(), false);

  // Walks from `edge` through `seg`, consuming segments, appending points.
  auto walk = [&](int64_t edge, size_t seg, std::vector<GeoPoint>* pts) {
    int64_t cur = edge;
    size_t si = seg;
    pts->push_back(w.points.at(cur));
    for (;;) {
      used[si] = true;
      const Segment& s = w.segments[si];
      const int64_t next_edge = (s.a == cur) ? s.b : s.a;
      const GeoPoint& p = w.points.at(next_edge);
      // Drop a repeated point: a segment can be zero length where the level
      // passes exactly through a post.
      if (pts->empty() || pts->back().lat != p.lat || pts->back().lon != p.lon)
        pts->push_back(p);
      cur = next_edge;
      auto it = at_edge.find(cur);
      if (it == at_edge.end()) break;
      size_t nxt = w.segments.size();
      for (size_t cand : it->second) {
        if (!used[cand]) {
          nxt = cand;
          break;
        }
      }
      if (nxt == w.segments.size()) break;
      si = nxt;
    }
    return cur;  // the edge the walk stopped on
  };

  auto degree = [&](int64_t e) { return at_edge[e].size(); };

  // Pass 1: open chains. A free end is an edge used by exactly one segment --
  // the grid boundary, or a cell dropped for a void corner.
  for (size_t i = 0; i < w.segments.size(); ++i) {
    if (used[i]) continue;
    const Segment& s = w.segments[i];
    int64_t start = 0;
    if (degree(s.a) == 1) {
      start = s.a;
    } else if (degree(s.b) == 1) {
      start = s.b;
    } else {
      continue;
    }
    ContourLine line;
    line.level_m = level_m;
    line.level_index = level_index;
    walk(start, i, &line.points);
    if (line.points.size() >= 2) out->push_back(std::move(line));
  }

  // Pass 2: rings.
  for (size_t i = 0; i < w.segments.size(); ++i) {
    if (used[i]) continue;
    ContourLine line;
    line.level_m = level_m;
    line.level_index = level_index;
    const int64_t start = w.segments[i].a;
    const int64_t end = walk(start, i, &line.points);
    if (end == start && line.points.size() >= 3) {
      const GeoPoint& first = line.points.front();
      const GeoPoint& last = line.points.back();
      if (last.lat != first.lat || last.lon != first.lon)
        line.points.push_back(first);
      line.closed = true;
    }
    if (line.points.size() >= 2) out->push_back(std::move(line));
  }
}

}  // namespace

Status SampleElevationGrid(IElevationSource& src, const GeoRect& bounds,
                           int width, int height, ElevationGrid* out,
                           std::vector<unsigned char>* covered) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  if (width < 2 || height < 2)
    return Status::Error(kInvalidArg, "grid must be at least 2x2 posts");
  if (bounds.CrossesAntimeridian())
    return Status::Error(kInvalidArg,
                         "elevation grid may not cross the antimeridian");
  if (bounds.ur.lat <= bounds.ll.lat || bounds.ur.lon <= bounds.ll.lon)
    return Status::Error(kInvalidArg, "empty bounds");

  out->bounds = bounds;
  out->width = width;
  out->height = height;
  out->meters.assign(static_cast<size_t>(width) * height,
                     std::numeric_limits<float>::quiet_NaN());
  if (covered != nullptr)
    covered->assign(static_cast<size_t>(width) * height, 0);

  const double dlat = out->LatStep(), dlon = out->LonStep();
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      GeoPoint p{bounds.ll.lat + r * dlat, bounds.ll.lon + c * dlon};
      p.Normalize();
      float e = 0.0f;
      // A hole in the coverage is a normal thing to draw over: out of
      // coverage, unreadable and void all land in the grid as NaN, and the
      // tracer drops any cell that touches one. `covered` keeps the one
      // distinction the grid itself throws away -- see the header.
      const size_t i = static_cast<size_t>(r) * width + c;
      if (src.GetElevation(p, &e).ok()) {
        out->meters[i] = e;
        if (covered != nullptr) (*covered)[i] = 1;
      }
    }
  }
  return Status::Ok();
}

std::vector<ContourLine> TraceElevationContours(const ElevationGrid& grid,
                                                double interval_m) {
  std::vector<ContourLine> out;
  if (!grid.Valid() || !(interval_m > 0.0)) return out;

  std::map<int, LevelWork> work;  // ordered: ascending level index

  for (int r = 0; r < grid.height - 1; ++r) {
    for (int c = 0; c < grid.width - 1; ++c) {
      const double sw = grid.At(r, c), se = grid.At(r, c + 1);
      const double ne = grid.At(r + 1, c + 1), nw = grid.At(r + 1, c);
      if (std::isnan(sw) || std::isnan(se) || std::isnan(ne) || std::isnan(nw))
        continue;

      const double lo = std::min(std::min(sw, se), std::min(ne, nw));
      const double hi = std::max(std::max(sw, se), std::max(ne, nw));
      const long long k0 = static_cast<long long>(std::floor(lo / interval_m));
      const long long k1 = static_cast<long long>(std::ceil(hi / interval_m));
      if (k1 - k0 > kMaxLevelsPerCell) continue;

      for (long long k = k0; k <= k1; ++k) {
        const double v = k * interval_m;
        if (v >= hi || v < lo) continue;  // the cell cannot cross this level
        MarchCell(grid, &work[static_cast<int>(k)], v, r, c);
      }
    }
  }

  for (const auto& kv : work)
    JoinLevel(kv.second, kv.first * interval_m, kv.first, &out);
  return out;
}

std::vector<ContourLine> TraceElevationContoursAtLevels(
    const ElevationGrid& grid, const std::vector<double>& levels_m) {
  std::vector<ContourLine> out;
  if (!grid.Valid() || levels_m.empty()) return out;

  // The levels arrive named rather than derived, so they are neither sorted
  // nor necessarily distinct. Sort a copy, remember where each one came from,
  // and hand the ORIGINAL index back on the line -- a caller that asked for
  // warning, caution and OK gets `level_index` 0, 1 and 2 whatever order the
  // three altitudes happen to fall in.
  std::vector<std::pair<double, int>> sorted;
  sorted.reserve(levels_m.size());
  for (size_t i = 0; i < levels_m.size(); ++i)
    sorted.emplace_back(levels_m[i], static_cast<int>(i));
  std::sort(sorted.begin(), sorted.end());

  std::map<int, LevelWork> work;  // keyed by index into `sorted`

  for (int r = 0; r < grid.height - 1; ++r) {
    for (int c = 0; c < grid.width - 1; ++c) {
      const double sw = grid.At(r, c), se = grid.At(r, c + 1);
      const double ne = grid.At(r + 1, c + 1), nw = grid.At(r + 1, c);
      if (std::isnan(sw) || std::isnan(se) || std::isnan(ne) || std::isnan(nw))
        continue;

      const double lo = std::min(std::min(sw, se), std::min(ne, nw));
      const double hi = std::max(std::max(sw, se), std::max(ne, nw));
      // The cell crosses the contiguous run of levels in [lo, hi) -- the same
      // half-open test the interval version makes, so a post exactly on a
      // level counts as below it and no cell is ambiguous.
      auto first = std::lower_bound(
          sorted.begin(), sorted.end(), lo,
          [](const std::pair<double, int>& a, double v) { return a.first < v; });
      for (auto it = first; it != sorted.end() && it->first < hi; ++it)
        MarchCell(grid, &work[static_cast<int>(it - sorted.begin())],
                  it->first, r, c);
    }
  }

  for (const auto& kv : work)
    JoinLevel(kv.second, sorted[static_cast<size_t>(kv.first)].first,
              sorted[static_cast<size_t>(kv.first)].second, &out);
  return out;
}

}  // namespace fv
