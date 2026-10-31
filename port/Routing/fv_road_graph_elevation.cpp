// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Elevation for a built road graph: node heights from a DEM, and per-arc
/// climb and descent counted with hysteresis (AddElevation).

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "fv_road_graph.h"

namespace fv {
namespace routing {
namespace {

constexpr float kUnknown = std::numeric_limits<float>::quiet_NaN();

/// An arc whose DEM profile is not the road's: a bridge, a tunnel or a ferry.
bool Interpolated(const RoadArc& a) { return a.structure() || a.is_ferry(); }

uint16_t ToDm(double metres) {
  return static_cast<uint16_t>(std::min(65535.0, std::max(0.0, std::round(metres * 10.0))));
}

/// Climb and descent along `profile` (first and last entries are the end
/// nodes; NaN entries are skipped). A change is counted once it reaches
/// `hysteresis` from the last counted level; the remainder to the last entry
/// is then counted outright, so climb minus descent is the net change.
void CountClimb(const std::vector<float>& profile, double hysteresis, double* climb,
                double* descent) {
  *climb = 0.0;
  *descent = 0.0;
  double ref = profile.front();
  for (size_t i = 1; i + 1 < profile.size(); ++i) {
    const double e = profile[i];
    if (std::isnan(e)) continue;
    if (e - ref >= hysteresis) {
      *climb += e - ref;
      ref = e;
    } else if (ref - e >= hysteresis) {
      *descent += ref - e;
      ref = e;
    }
  }
  const double rest = profile.back() - ref;
  if (rest > 0.0) *climb += rest;
  else *descent -= rest;
}

}  // namespace

namespace {

/// One pass of a running min (or max) of width `window` along rows (`dx`=1)
/// or columns of a `cols` x `rows` grid; NaN posts are ignored.
void RunningExtreme(std::vector<float>* grid, int cols, int rows, int window, bool rows_pass,
                    bool take_max) {
  const int half = window / 2;
  const int lines = rows_pass ? rows : cols;
  const int length = rows_pass ? cols : rows;
  std::vector<float> line(static_cast<size_t>(length));
  for (int l = 0; l < lines; ++l) {
    auto at = [&](int k) -> float& {
      return (*grid)[rows_pass ? static_cast<size_t>(l) * cols + k
                               : static_cast<size_t>(k) * cols + l];
    };
    for (int k = 0; k < length; ++k) line[k] = at(k);
    for (int k = 0; k < length; ++k) {
      float best = kUnknown;
      for (int d = std::max(0, k - half); d <= std::min(length - 1, k + half); ++d) {
        const float e = line[d];
        if (std::isnan(e)) continue;
        if (std::isnan(best) || (take_max ? e > best : e < best)) best = e;
      }
      if (!std::isnan(line[k])) at(k) = best;
    }
  }
}

/// Erosion (min) or dilation (max) with a `window` x `window` square.
void Extreme(std::vector<float>* grid, int cols, int rows, int window, bool take_max) {
  RunningExtreme(grid, cols, rows, window, true, take_max);
  RunningExtreme(grid, cols, rows, window, false, take_max);
}

}  // namespace

ElevationSampler GroundSampler(const ElevationSampler& raw, const GeoRect& box, double lat_step,
                               double lon_step, int open_window, int close_window) {
  const int window = std::max(open_window, close_window);
  const int cols = static_cast<int>(std::ceil((box.ur.lon - box.ll.lon) / lon_step)) + 1;
  const int rows = static_cast<int>(std::ceil((box.ur.lat - box.ll.lat) / lat_step)) + 1;
  if (window < 2 || cols < 2 || rows < 2 || static_cast<double>(cols) * rows > 1e8) return raw;
  auto grid = std::make_shared<std::vector<float>>(static_cast<size_t>(cols) * rows, kUnknown);
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      float e = kUnknown;
      if (raw(box.ll.lat + r * lat_step, box.ll.lon + c * lon_step, &e) && std::isfinite(e))
        (*grid)[static_cast<size_t>(r) * cols + c] = e;
    }
  }
  // Opening (min then max) removes what stands up narrower than its window;
  // closing (max then min) then fills pits narrower than its own.
  if (open_window > 1) {
    Extreme(grid.get(), cols, rows, open_window, false);
    Extreme(grid.get(), cols, rows, open_window, true);
  }
  if (close_window > 1) {
    Extreme(grid.get(), cols, rows, close_window, true);
    Extreme(grid.get(), cols, rows, close_window, false);
  }

  const GeoPoint origin = box.ll;
  return [raw, grid, origin, lat_step, lon_step, cols, rows](double lat, double lon,
                                                              float* metres) {
    const double y = (lat - origin.lat) / lat_step;
    const double x = (lon - origin.lon) / lon_step;
    const int r = static_cast<int>(std::floor(y));
    const int c = static_cast<int>(std::floor(x));
    if (r < 0 || c < 0 || r + 1 >= rows || c + 1 >= cols) return raw(lat, lon, metres);
    const double fy = y - r, fx = x - c;
    const auto post = [&](int rr, int cc) { return (*grid)[static_cast<size_t>(rr) * cols + cc]; };
    const float e00 = post(r, c), e01 = post(r, c + 1), e10 = post(r + 1, c),
                e11 = post(r + 1, c + 1);
    if (std::isnan(e00) || std::isnan(e01) || std::isnan(e10) || std::isnan(e11))
      return raw(lat, lon, metres);
    *metres = static_cast<float>((e00 * (1 - fx) + e01 * fx) * (1 - fy) +
                                 (e10 * (1 - fx) + e11 * fx) * fy);
    return true;
  };
}

Status AddElevation(const ElevationSampler& sample, const ElevationOptions& options,
                    RoadGraph* graph, ElevationStats* stats) {
  if (graph == nullptr || !sample) return Status::Error(kInvalidArg, "AddElevation: null argument");
  ElevationStats st;
  const double step = std::min(30.0, std::max(10.0, options.sample_step_m));
  const uint32_t n = graph->node_count();

  // --- nodes: the DEM where a node touches ground, else its neighbours -----
  std::vector<float> elev(n, kUnknown);
  std::vector<uint32_t> floating;
  for (uint32_t u = 0; u < n; ++u) {
    bool grounded = false;
    for (uint32_t a = graph->arc_begin(u); a < graph->arc_end(u) && !grounded; ++a)
      grounded = !Interpolated(graph->arc(a));
    float e = kUnknown;
    const GeoPoint p = graph->location(u);
    if (grounded && sample(p.lat, p.lon, &e) && std::isfinite(e)) {
      elev[u] = e;
      ++st.nodes_sampled;
    } else {
      floating.push_back(u);
    }
  }
  // Gauss-Seidel on the 1/length-weighted mean of known neighbours: on a
  // chain of bridge arcs this converges to a linear profile between the ends.
  for (int pass = 0; pass < 1000 && !floating.empty(); ++pass) {
    double moved = 0.0;
    for (uint32_t u : floating) {
      double sum = 0.0, weight = 0.0;
      for (uint32_t a = graph->arc_begin(u); a < graph->arc_end(u); ++a) {
        const RoadArc& arc = graph->arc(a);
        const float e = elev[arc.target];
        if (std::isnan(e)) continue;
        const double w = 1.0 / std::max(1.0, static_cast<double>(arc.length_m));
        sum += w * e;
        weight += w;
      }
      if (weight == 0.0) continue;
      const float next = static_cast<float>(sum / weight);
      moved = std::max(moved, std::isnan(elev[u]) ? 1e9 : std::fabs(next - elev[u]));
      elev[u] = next;
    }
    if (moved < 0.005) break;
  }
  for (uint32_t u : floating) {
    if (!std::isnan(elev[u])) {
      ++st.nodes_interpolated;
      continue;
    }
    // A component with no ground at all: take the DEM if it answers.
    float e = kUnknown;
    const GeoPoint p = graph->location(u);
    if (sample(p.lat, p.lon, &e) && std::isfinite(e)) {
      elev[u] = e;
      ++st.nodes_sampled;
    } else {
      elev[u] = 0.0f;
      ++st.nodes_no_data;
    }
  }

  std::vector<int32_t>& node_dm = graph->mutable_node_elevation_dm();
  node_dm.resize(n);
  for (uint32_t u = 0; u < n; ++u) node_dm[u] = static_cast<int32_t>(std::lround(elev[u] * 10.0));

  // --- arcs: each undirected edge once, the twin gets the swapped pair -----
  std::vector<RoadArc>& arcs = graph->mutable_arcs();
  std::vector<bool> done(arcs.size(), false);
  std::vector<GeoPoint> shape;
  std::vector<float> profile;
  for (uint32_t u = 0; u < n; ++u) {
    for (uint32_t a = graph->arc_begin(u); a < graph->arc_end(u); ++a) {
      if (done[a]) continue;
      RoadArc& arc = arcs[a];
      const double from = elev[u];
      const double to = elev[arc.target];
      double climb = 0.0, descent = 0.0;
      if (Interpolated(arc)) {
        climb = std::max(0.0, to - from);
        descent = std::max(0.0, from - to);
        ++st.edges_structure;
      } else {
        shape.clear();
        graph->ArcShape(a, &shape);
        double total = 0.0;
        for (size_t i = 1; i < shape.size(); ++i)
          total += GreatCircleMeters(shape[i - 1].lat, shape[i - 1].lon, shape[i].lat, shape[i].lon);
        const int intervals = std::max(1, static_cast<int>(std::ceil(total / step)));
        const double spacing = total / intervals;
        profile.clear();
        profile.push_back(static_cast<float>(from));
        // Walk the polyline, emitting a sample at each multiple of `spacing`.
        double seg_start = 0.0;
        size_t seg = 1;
        for (int k = 1; k < intervals; ++k) {
          const double at = k * spacing;
          double seg_len = 0.0;
          while (seg < shape.size()) {
            seg_len = GreatCircleMeters(shape[seg - 1].lat, shape[seg - 1].lon, shape[seg].lat,
                                        shape[seg].lon);
            if (seg_start + seg_len >= at || seg + 1 == shape.size()) break;
            seg_start += seg_len;
            ++seg;
          }
          const double t = seg_len > 0.0 ? std::min(1.0, (at - seg_start) / seg_len) : 0.0;
          const GeoPoint& p0 = shape[seg - 1];
          const GeoPoint& p1 = shape[seg];
          float e = kUnknown;
          ++st.samples;
          if (!sample(p0.lat + t * (p1.lat - p0.lat), p0.lon + t * (p1.lon - p0.lon), &e) ||
              !std::isfinite(e)) {
            e = kUnknown;
            ++st.samples_missing;
          }
          profile.push_back(e);
        }
        profile.push_back(static_cast<float>(to));
        CountClimb(profile, options.hysteresis_m, &climb, &descent);
      }
      arc.climb_dm = ToDm(climb);
      arc.descent_dm = ToDm(descent);
      done[a] = true;
      st.climb_m += climb;
      const uint32_t twin = graph->TwinArc(u, a);
      if (twin != kNoArc && !done[twin]) {
        arcs[twin].climb_dm = arc.descent_dm;
        arcs[twin].descent_dm = arc.climb_dm;
        done[twin] = true;
      }
    }
  }

  if (stats != nullptr) *stats = st;
  return Status::Ok();
}

}  // namespace routing
}  // namespace fv
