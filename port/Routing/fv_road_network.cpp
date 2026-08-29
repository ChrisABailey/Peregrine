// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_road_network.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fv {
namespace routing {
namespace {

constexpr double kEarthRadiusMeters = 6371008.8;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kMetersPerDegLat = kEarthRadiusMeters * kDegToRad;

}  // namespace

RoadGraphNetwork::RoadGraphNetwork(std::shared_ptr<const RoadGraph> graph,
                                   const RoadNetworkOptions& options)
    : graph_(std::move(graph)), options_(options) {
  BuildIndex();
}

void RoadGraphNetwork::SetGraph(std::shared_ptr<const RoadGraph> graph) {
  graph_ = std::move(graph);
  BuildIndex();
}

void RoadGraphNetwork::SetOptions(const RoadNetworkOptions& options) {
  options_ = options;
  BuildIndex();
}

bool RoadGraphNetwork::Admits(const RoadArc& a) const {
  switch (options_.filter) {
    case RoadSnapFilter::kAll:
      return true;
    case RoadSnapFilter::kDriveable:
      // The access bit as well as the class: a road a car may not enter is not
      // a road this ship is on. `private` is NOT a bar — O5b's rule, and on a
      // gated community the private streets ARE the street network.
      return IsDriveable(a.klass) && a.motor_vehicle_allowed();
    case RoadSnapFilter::kCycleable:
      return IsCycleable(a.klass) && a.bicycle_allowed();
  }
  return true;
}

void RoadGraphNetwork::BuildIndex() {
  arc_ids_.clear();
  arc_source_.clear();
  cell_begin_.clear();
  cell_items_.clear();
  grid_cols_ = grid_rows_ = 0;
  grid_bounds_ = GeoRect{};
  if (!graph_ || graph_->node_count() == 0) return;

  // --- which arcs -----------------------------------------------------------
  for (uint32_t u = 0; u < graph_->node_count(); ++u) {
    for (uint32_t ai = graph_->arc_begin(u); ai < graph_->arc_end(u); ++ai) {
      const RoadArc& a = graph_->arc(ai);
      // One candidate per undirected road (see the header). The mirrored arc
      // stored at the other end is the same tarmac.
      if (a.target < u) continue;
      if (a.target == u) {
        // A closed way with no junction on it: both its arcs sit in this same
        // node's range, so keep the first and skip the twin.
        bool first = true;
        for (uint32_t bi = graph_->arc_begin(u); bi < ai; ++bi)
          if (graph_->arc(bi).target == u) first = false;
        if (!first) continue;
      }
      if (!Admits(a)) continue;
      arc_ids_.push_back(ai);
      arc_source_.push_back(u);
    }
  }
  if (arc_ids_.empty()) return;

  // --- the grid -------------------------------------------------------------
  // OVER THE GEOMETRY, NOT OVER THE NODES. `RoadGraph::bounds()` is the box of
  // the graph's VERTICES, which is the right box for a router (it starts and
  // ends at junctions) and the wrong one here: a road's shape points can lie
  // well outside its own endpoints' box — a dogleg, a bay, a hairpin — and in
  // the degenerate case of a single east-west road the node box has no height
  // at all. Sizing the index off it puts the shape in a cell the query for the
  // same point never sweeps.
  bool first = true;
  for (uint32_t k = 0; k < arc_ids_.size(); ++k) {
    const RoadArc& a = graph_->arc(arc_ids_[k]);
    for (uint32_t g = 0; g <= a.geom_count + 1; ++g) {
      GeoPoint p;
      if (g == 0) {
        p = graph_->location(arc_source_[k]);
      } else if (g <= a.geom_count) {
        p = graph_->arc_point(a, g - 1);
      } else {
        p = graph_->location(a.target);
      }
      if (first) {
        grid_bounds_.ll = grid_bounds_.ur = p;
        first = false;
        continue;
      }
      grid_bounds_.ll.lat = std::min(grid_bounds_.ll.lat, p.lat);
      grid_bounds_.ll.lon = std::min(grid_bounds_.ll.lon, p.lon);
      grid_bounds_.ur.lat = std::max(grid_bounds_.ur.lat, p.lat);
      grid_bounds_.ur.lon = std::max(grid_bounds_.ur.lon, p.lon);
    }
  }
  const double mid_lat = 0.5 * (grid_bounds_.ll.lat + grid_bounds_.ur.lat);
  const double cos_lat = std::max(std::cos(mid_lat * kDegToRad), 1e-6);
  const double height_m = (grid_bounds_.ur.lat - grid_bounds_.ll.lat) * kMetersPerDegLat;
  const double width_m =
      (grid_bounds_.ur.lon - grid_bounds_.ll.lon) * kMetersPerDegLat * cos_lat;
  const double cell = options_.cell_size_m > 1.0 ? options_.cell_size_m : 1.0;
  auto side = [cell](double extent_m) {
    int n = static_cast<int>(std::ceil(extent_m / cell));
    if (n < 1) n = 1;
    if (n > 4096) n = 4096;
    return n;
  };
  grid_rows_ = side(height_m);
  grid_cols_ = side(width_m);
  grid_lat_step_ = (grid_bounds_.ur.lat - grid_bounds_.ll.lat) / grid_rows_;
  grid_lon_step_ = (grid_bounds_.ur.lon - grid_bounds_.ll.lon) / grid_cols_;
  if (grid_lat_step_ <= 0.0) grid_lat_step_ = 1e-9;
  if (grid_lon_step_ <= 0.0) grid_lon_step_ = 1e-9;

  auto col_of = [this](double lon) {
    int c = static_cast<int>((lon - grid_bounds_.ll.lon) / grid_lon_step_);
    return std::min(std::max(c, 0), grid_cols_ - 1);
  };
  auto row_of = [this](double lat) {
    int r = static_cast<int>((lat - grid_bounds_.ll.lat) / grid_lat_step_);
    return std::min(std::max(r, 0), grid_rows_ - 1);
  };

  // Each SEGMENT is registered in every cell its bounding box touches. A long
  // diagonal therefore over-registers into cells it only passes near, which
  // costs a projection that comes out too far away and is dropped — cheaper
  // than a line rasteriser, and never wrong in the other direction.
  std::vector<std::pair<uint32_t, uint32_t>> pairs;  // (cell, index into arc_ids_)
  for (uint32_t k = 0; k < arc_ids_.size(); ++k) {
    const RoadArc& a = graph_->arc(arc_ids_[k]);
    GeoPoint prev = graph_->location(arc_source_[k]);
    for (uint32_t g = 0; g <= a.geom_count; ++g) {
      const GeoPoint next = (g < a.geom_count) ? graph_->arc_point(a, g)
                                               : graph_->location(a.target);
      const int r0 = row_of(std::min(prev.lat, next.lat));
      const int r1 = row_of(std::max(prev.lat, next.lat));
      const int c0 = col_of(std::min(prev.lon, next.lon));
      const int c1 = col_of(std::max(prev.lon, next.lon));
      for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c)
          pairs.emplace_back(static_cast<uint32_t>(r) * grid_cols_ + c, k);
      prev = next;
    }
  }
  std::sort(pairs.begin(), pairs.end());
  pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());

  const size_t cells = static_cast<size_t>(grid_cols_) * grid_rows_;
  cell_begin_.assign(cells + 1, 0);
  for (const std::pair<uint32_t, uint32_t>& p : pairs) ++cell_begin_[p.first + 1];
  for (size_t c = 0; c < cells; ++c) cell_begin_[c + 1] += cell_begin_[c];
  cell_items_.resize(pairs.size());
  for (size_t i = 0; i < pairs.size(); ++i) cell_items_[i] = pairs[i].second;
}

void RoadGraphNetwork::QueryNear(const GeoPoint& p, double radius_m,
                                 std::vector<fv::RoadCandidate>* out) const {
  if (out == nullptr || arc_ids_.empty() || grid_cols_ == 0) return;
  if (radius_m <= 0.0) return;

  const double cos_lat = std::max(std::cos(p.lat * kDegToRad), 1e-6);
  const double dlat = radius_m / kMetersPerDegLat;
  const double dlon = radius_m / (kMetersPerDegLat * cos_lat);

  auto clamp_col = [this](double lon) {
    int c = static_cast<int>(std::floor((lon - grid_bounds_.ll.lon) / grid_lon_step_));
    return std::min(std::max(c, 0), grid_cols_ - 1);
  };
  auto clamp_row = [this](double lat) {
    int r = static_cast<int>(std::floor((lat - grid_bounds_.ll.lat) / grid_lat_step_));
    return std::min(std::max(r, 0), grid_rows_ - 1);
  };
  // Wholly outside the graph's own bounds by more than the radius: the clamped
  // sweep would still read the edge cells and project onto roads far away, so
  // say so here instead.
  if (p.lat + dlat < grid_bounds_.ll.lat || p.lat - dlat > grid_bounds_.ur.lat ||
      p.lon + dlon < grid_bounds_.ll.lon || p.lon - dlon > grid_bounds_.ur.lon)
    return;

  const int r0 = clamp_row(p.lat - dlat), r1 = clamp_row(p.lat + dlat);
  const int c0 = clamp_col(p.lon - dlon), c1 = clamp_col(p.lon + dlon);

  std::vector<uint32_t> hits;
  for (int r = r0; r <= r1; ++r) {
    for (int c = c0; c <= c1; ++c) {
      const uint32_t cell = static_cast<uint32_t>(r) * grid_cols_ + c;
      for (uint32_t i = cell_begin_[cell]; i < cell_begin_[cell + 1]; ++i)
        hits.push_back(cell_items_[i]);
    }
  }
  std::sort(hits.begin(), hits.end());
  hits.erase(std::unique(hits.begin(), hits.end()), hits.end());

  for (uint32_t k : hits) {
    const uint32_t arc_index = arc_ids_[k];
    const RoadArc& a = graph_->arc(arc_index);

    // Walk the road's own shape and keep the closest approach.
    double best_d = std::numeric_limits<double>::infinity();
    double best_along = 0.0;
    double best_bearing = 0.0;
    GeoPoint best_point;
    double walked = 0.0;
    GeoPoint prev = graph_->location(arc_source_[k]);
    for (uint32_t g = 0; g <= a.geom_count; ++g) {
      const GeoPoint next = (g < a.geom_count) ? graph_->arc_point(a, g)
                                               : graph_->location(a.target);
      fv::SegmentProjection sp;
      if (fv::ProjectOntoSegment(p, prev, next, &sp)) {
        if (sp.distance_m < best_d) {
          best_d = sp.distance_m;
          best_along = walked + sp.along_m;
          best_bearing = sp.bearing_deg;
          best_point = sp.point;
        }
        walked += sp.length_m;
      }
      prev = next;
    }
    if (!(best_d <= radius_m)) continue;

    fv::RoadCandidate cand;
    cand.arc = arc_index;
    cand.from_node = arc_source_[k];
    cand.to_node = a.target;
    cand.point = best_point;
    cand.distance_m = best_d;
    cand.along_m = best_along;
    cand.length_m = walked;
    cand.bearing_deg = best_bearing;

    // A road travelled in one direction only tells the snapper which way the
    // ship must be going along it. When that direction is the REVERSE of the
    // stored geometry, the bearing is the reverse too — the arc's flags are
    // mirrored, not its shape.
    const bool fwd = a.forward();
    const bool bwd = a.backward();
    cand.one_way = fwd != bwd;
    if (bwd && !fwd) cand.bearing_deg = fv::NormalizeHeadingDeg(cand.bearing_deg + 180.0);

    if (a.name != 0) cand.name = graph_->name(a.name);
    out->push_back(std::move(cand));
  }
}

}  // namespace routing
}  // namespace fv
