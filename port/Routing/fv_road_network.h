// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_road_network.h — the O4 road graph, offered to the moving map (MM5).
//
// `fv::RoadSnapper` (fvkit/nav/road_snap.h) asks one question — "which roads
// are near this point?" — and this is the answer over a `RoadGraph`. It lives
// HERE, in port/Routing, rather than in fvkit, because that is the direction
// the dependency has to run: fvkit does not link the router (MM1's rule, kept
// since the scripted track builder took a polyline rather than a Route), so
// the adapter belongs on the side that already knows both. An application that
// has no graph simply never constructs one and snapping is off.
//
// WHAT IT ADDS TO THE GRAPH, AND WHY THE GRAPH DOES NOT ALREADY HAVE IT.
// `RoadGraph::NearestNode` indexes NODES, which is the right index for a
// router: a route starts and ends at a junction. A snap is the other question
// — the ship is between junctions, on a road whose two ends may both be a
// kilometre away — so this builds a second index over the arc GEOMETRY, one
// uniform grid of cell -> arcs, and projects onto the segments themselves.
//
// THREE THINGS WORTH KNOWING.
//
// 1. ONE CANDIDATE PER ROAD, NOT PER ARC. The graph stores an undirected edge
//    as two mirrored arcs, one in each endpoint's adjacency list. They are the
//    same tarmac, so only the arc whose source id is the lower of the two is
//    indexed; offering both would make every two-way street ambiguous with
//    itself and halve the confidence of every snap on one.
//
// 2. A FILTER IS A REAL NEED, NOT A KNOB. The graph deliberately keeps
//    footways, paths and cycleways (a walking profile is meaningless without
//    them), and on Kiawah a cycleway runs beside almost every road. A car
//    snapping to the nearest arc of ANY class would spend the drive on the
//    cycle path. The default is therefore what a car may drive, which is
//    `IsDriveable` plus the arc's own motor-vehicle access bit.
//
// 3. THE INDEX IS BUILT OVER THE WHOLE GRAPH AT CONSTRUCTION. That is right
//    for an island, a county or a state; a continent-sized `.fvroad` would
//    want a windowed index built around the ship and rebuilt as it moves. The
//    cost is proportional to the geometry (a few pointers per segment), so the
//    limit is memory rather than time, and it is the same limit the router's
//    per-query scratch already has (ledger 2b).

#ifndef FV_ROAD_NETWORK_H_
#define FV_ROAD_NETWORK_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/nav/road_snap.h"

#include "fv_road_graph.h"

namespace fv {
namespace routing {

// Which arcs may be snapped to.
enum class RoadSnapFilter : uint8_t {
  kAll = 0,        // every arc in the graph, footpaths included
  kDriveable = 1,  // IsDriveable + the motor-vehicle access bit
  kCycleable = 2,  // IsCycleable + the bicycle access bit
};

struct RoadNetworkOptions {
  RoadSnapFilter filter = RoadSnapFilter::kDriveable;

  // Target index cell, metres. 200 m puts a handful of arcs in a cell on an
  // ordinary street network and keeps a snap radius inside a 3x3 sweep.
  double cell_size_m = 200.0;
};

class RoadGraphNetwork : public fv::IRoadNetwork {
 public:
  RoadGraphNetwork() = default;
  explicit RoadGraphNetwork(std::shared_ptr<const RoadGraph> graph,
                            const RoadNetworkOptions& options = RoadNetworkOptions());

  // Replacing the graph rebuilds the index. Null empties it, and every query
  // then returns nothing — which is a snapper with the feature off.
  void SetGraph(std::shared_ptr<const RoadGraph> graph);
  const std::shared_ptr<const RoadGraph>& graph() const { return graph_; }

  void SetOptions(const RoadNetworkOptions& options);
  const RoadNetworkOptions& options() const { return options_; }

  // fv::IRoadNetwork.
  void QueryNear(const GeoPoint& p, double radius_m,
                 std::vector<fv::RoadCandidate>* out) const override;
  // The box of the indexed ROAD SHAPE, which is not `RoadGraph::bounds()` (the
  // box of its vertices) — see BuildIndex. Empty when nothing is indexed.
  GeoRect bounds() const override { return grid_bounds_; }

  // How many roads the filter admitted — the number to look at when a snapper
  // that should be finding roads finds none.
  uint32_t indexed_arcs() const { return static_cast<uint32_t>(arc_ids_.size()); }
  int grid_cols() const { return grid_cols_; }
  int grid_rows() const { return grid_rows_; }

 private:
  void BuildIndex();
  bool Admits(const RoadArc& a) const;

  std::shared_ptr<const RoadGraph> graph_;
  RoadNetworkOptions options_;

  // The indexed arcs (graph arc indices, one per undirected road), and a
  // uniform grid over the graph's bounds holding positions INTO that list.
  std::vector<uint32_t> arc_ids_;
  std::vector<uint32_t> arc_source_;  // the node each indexed arc is stored at

  GeoRect grid_bounds_;
  int grid_cols_ = 0;
  int grid_rows_ = 0;
  double grid_lat_step_ = 0.0;
  double grid_lon_step_ = 0.0;
  std::vector<uint32_t> cell_begin_;  // cells + 1
  std::vector<uint32_t> cell_items_;  // indices into arc_ids_
};

}  // namespace routing
}  // namespace fv

#endif  // FV_ROAD_NETWORK_H_
