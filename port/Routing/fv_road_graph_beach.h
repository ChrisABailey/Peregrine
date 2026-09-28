// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file fv_road_graph_beach.h
 * Beach synthesis for the road graph builder: turns the stretch of
 * `natural=coastline` that borders a beach polygon into ridable arcs, and
 * joins nearby path ends to them across the dry sand. Internal to
 * fv_routing; BuildRoadGraph is the only caller.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "fv_road_graph.h"

namespace fv {
namespace routing {
namespace beach {

/// A beach polygon: a closed `natural=beach` (or non-golf `natural=sand`) way,
/// resolved. `pts` repeats the first point last, as OSM closes a ring.
struct Ring {
  std::vector<int64_t> ids;
  std::vector<GeoPoint> pts;
  uint32_t name = 0;  // index into the graph's name table; 0 = unnamed
  GeoRect box;
};

/// A resolved stretch of `natural=coastline`, split wherever the extract
/// dropped a node.
struct Line {
  std::vector<int64_t> ids;
  std::vector<GeoPoint> pts;
};

/// A road-graph node that may lead onto the sand, with the access bits of the
/// way that brought it there.
struct AccessCandidate {
  uint32_t node = 0;
  GeoPoint p;
  uint16_t access_flags = 0;
};

/// The distances synthesis works to, in metres. Copied from
/// RoadGraphBuildOptions so this file does not depend on the option names.
struct Params {
  double line_snap_m = 15.0;
  double gap_m = 60.0;
  double access_snap_m = 25.0;
  double access_max_m = 200.0;
};

/// A node the synthesis adds to the graph.
struct NewNode {
  GeoPoint p;
  int64_t osm_id = 0;  // the coastline node's own id, or negative for a split point
};

/// An undirected edge the synthesis adds. `u`/`v` are graph node indices; new
/// nodes are numbered from the `first_new_node` passed to Synthesize.
struct NewEdge {
  uint32_t u = 0;
  uint32_t v = 0;
  std::vector<GeoPoint> interior;
  double length_m = 0.0;
  RoadClass klass = RoadClass::kBeach;
  uint32_t name = 0;
  uint16_t access_flags = 0;
};

struct Result {
  std::vector<NewNode> nodes;
  std::vector<NewEdge> edges;
  int64_t runs = 0;
  double run_m = 0.0;
  int64_t access_arcs = 0;
  int64_t access_unreached = 0;  // candidates with no run within access_max_m
};

/// True when `p` lies inside `ring` (even-odd rule in lat/lon).
bool Inside(const Ring& ring, const GeoPoint& p);

/// Distance in metres from `p` to the nearest point of `ring`'s boundary.
double BoundaryDistance(const Ring& ring, const GeoPoint& p);

/// True when `p` is inside any ring or within `snap_m` of one's boundary.
bool NearAnyRing(const std::vector<Ring>& rings, const GeoPoint& p, double snap_m);

/// Builds the beach runs and access arcs.
/// @param graph_node_of  maps a coastline OSM node id to its existing graph
///   node, or kNoArc when it is not one; a coastline node that is already a
///   road vertex is used rather than duplicated.
/// @param beach_name     name index for a run whose polygon has no name.
void Synthesize(const std::vector<Ring>& rings, const std::vector<Line>& lines,
                const std::vector<AccessCandidate>& candidates,
                const std::function<uint32_t(int64_t)>& graph_node_of,
                uint32_t first_new_node, uint32_t beach_name, const Params& params,
                Result* out);

}  // namespace beach
}  // namespace routing
}  // namespace fv
