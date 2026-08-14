// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_road_graph.h — the routable road graph (O4).
//
// Built offline from a raw OSM extract (see BuildRoadGraph) and saved as a
// compact `.fvroad` file; loaded at runtime by the router. The graph is
// *noded*: a vertex exists only where two or more ways share a node, or at a
// way's ends. Everything between two vertices is edge geometry, kept so a
// route can be drawn as the road actually runs rather than as a chord.
//
// Each undirected edge appears as two arcs, one in each endpoint's adjacency
// list. Both arcs carry the same pair of permissions, mirrored:
//
//   arc stored at u, target v:
//     kArcForward  -> driving u -> v is allowed
//     kArcBackward -> driving v -> u is allowed
//
// so a backward (reverse) search standing at v reads v's own arc list and
// tests kArcBackward. One CSR serves both directions of a bidirectional
// search; there is no separate reverse index.
//
// Coordinates are stored as degrees x 1e7 in int32 — OSM's own precision
// (~1 cm), exact, and half the size of a double.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace routing {

// "no arc" / "no arrival" — used for a restriction that does not resolve and
// for the search state at a route's own start, which arrived along nothing.
constexpr uint32_t kNoArc = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// Road classes
// ---------------------------------------------------------------------------

// The `highway=*` values worth routing on. Ordering is by descending
// importance and is part of the file format — append, never renumber.
enum class RoadClass : uint8_t {
  kMotorway = 0,
  kMotorwayLink,
  kTrunk,
  kTrunkLink,
  kPrimary,
  kPrimaryLink,
  kSecondary,
  kSecondaryLink,
  kTertiary,
  kTertiaryLink,
  kUnclassified,
  kResidential,
  kLivingStreet,
  kService,
  kTrack,
  kPedestrian,
  kFootway,
  kPath,
  kCycleway,
  kSteps,
  // O5e. Not a `highway=*` value at all — a ferry way is `route=ferry`, and
  // it is a class rather than a flag on a road because everything a class
  // decides differs on a boat: what may board it, how fast it goes (the
  // crossing's own `duration`, never the profile's walking speed) and whether
  // a route may use it. Appended, so a .fvroad written before it exists holds
  // no arc of this class and reads exactly as it always did.
  kFerry,
  kCount,
  kNone = 255,  // not a routable highway value
};

// The OSM tag value this class came from ("motorway_link", ...); "" for kNone.
const char* RoadClassName(RoadClass klass);

// Maps a `highway=*` tag value to a class, or kNone when the value is not
// something anything can travel along (bus_stop, crossing, proposed, ...).
// NEVER returns kFerry: `highway=ferry` is not a tag OSM uses, and a caller
// naming a class by its own name wants RoadClassFromName below.
RoadClass RoadClassFromHighwayTag(const std::string& value);

// Maps a class's own name to the class — RoadClassFromHighwayTag plus the
// classes that do not come from a `highway=*` tag. This is what a rule file's
// `classes` object is resolved through, so a profile can weight "ferry".
RoadClass RoadClassFromName(const std::string& name);

// True for classes a car may use. Footways, paths, steps, cycleways and
// pedestrian streets are kept in the graph (a walking profile wants them)
// but excluded from a driving route. A ferry counts as driveable: whether a
// car may actually board is the way's own `motor_vehicle` access tag, which
// is on the arc.
bool IsDriveable(RoadClass klass);

bool IsCycleable(RoadClass klass);

// Fallback speed when the way carries no usable `maxspeed`, km/h.
int DefaultSpeedKph(RoadClass klass);

// ---------------------------------------------------------------------------
// Graph
// ---------------------------------------------------------------------------

enum ArcFlags : uint16_t {
  kArcForward = 1 << 0,   // source -> target permitted
  kArcBackward = 1 << 1,  // target -> source permitted
  kArcGeomReversed = 1 << 2,  // stored geometry runs target -> source
  // What OSM's access tags say about each mode, resolved at build time (see
  // AccessFor). Recorded per arc rather than only filtered at build time so
  // one general graph can answer a driving, walking AND bicycle query — and
  // so a .fvroad written before these bits existed reads them clear, i.e.
  // "nothing said", exactly the old behaviour.
  //
  // Two kinds of "you may not", because OSM means two different things by
  // them (O5b): `no` is a denial and the mode simply cannot use the arc,
  // while `private` is *permission the router does not hold* — the road is
  // there, it connects, and someone routing to a house behind the gate has to
  // be allowed down it. So `private` is a separate bit that prices the arc up
  // (RouteOptions::private_penalty) instead of removing it. Conflating the
  // two deleted 51 ways / 340 driveable arcs on Kiawah Island — most of the
  // island's streets — and left driving with no connected network at all.
  kArcNoBicycle = 1 << 3,
  kArcNoFoot = 1 << 4,
  kArcNoMotorVehicle = 1 << 5,
  kArcPrivateBicycle = 1 << 6,
  kArcPrivateFoot = 1 << 7,
  kArcPrivateMotorVehicle = 1 << 8,
  // `toll=yes` (O5e). A property of an ordinary road rather than a class of
  // its own, which is exactly why it is a bit: a tolled motorway is still a
  // motorway and keeps a motorway's weight. `toll:hgv` and the other
  // mode-qualified forms are lorry signage and are not read.
  kArcToll = 1 << 9,
};

struct RoadArc {
  uint32_t target = 0;
  uint32_t geom_begin = 0;  // index into RoadGraph::geometry
  uint32_t geom_count = 0;  // interior points only; endpoints come from nodes
  uint32_t name = 0;        // index into the name table; 0 = unnamed
  float length_m = 0.0f;
  RoadClass klass = RoadClass::kNone;
  // 16 bits since O5b. The on-disk word packs klass, flags and speed and had
  // its top byte spare, so the extra bits went there: a file written before
  // them reads them clear, i.e. "nothing said" — the same read-old-as-silent
  // rule the access bits themselves got.
  uint16_t flags = 0;
  uint8_t speed_kph = 0;

  bool forward() const { return (flags & kArcForward) != 0; }
  bool backward() const { return (flags & kArcBackward) != 0; }
  bool bicycle_allowed() const { return (flags & kArcNoBicycle) == 0; }
  bool foot_allowed() const { return (flags & kArcNoFoot) == 0; }
  bool motor_vehicle_allowed() const { return (flags & kArcNoMotorVehicle) == 0; }
  bool bicycle_private() const { return (flags & kArcPrivateBicycle) != 0; }
  bool foot_private() const { return (flags & kArcPrivateFoot) != 0; }
  bool motor_vehicle_private() const { return (flags & kArcPrivateMotorVehicle) != 0; }
  bool tolled() const { return (flags & kArcToll) != 0; }
  bool is_ferry() const { return klass == RoadClass::kFerry; }

  // Seconds to drive this arc at its posted/assumed speed.
  double travel_seconds() const {
    const double kph = speed_kph > 0 ? static_cast<double>(speed_kph) : 1.0;
    return length_m / (kph * (1000.0 / 3600.0));
  }
};

struct RoadNode {
  int32_t lat_e7 = 0;
  int32_t lon_e7 = 0;
  int64_t osm_id = 0;
};

// ---------------------------------------------------------------------------
// Turn restrictions (O5)
// ---------------------------------------------------------------------------

// A `type=restriction` relation, resolved onto the graph. OSM states one as
// (from way, via node, to way); by the time it is here both ways have become
// *arcs of the via node* — the arc leaving the via node back along the from
// way, and the arc leaving it along the to way. That is the shape the search
// needs: standing at the via node, "which way did I come in" and "which way
// am I about to leave" are both arcs in this node's own adjacency range.
//
// NOTE: `via way` restrictions (the ones whose via member is a way, not a
// node — a few per cent of the planet, mostly divided-highway U-turns) are
// counted by the builder and NOT applied. Applying one needs more history
// than "the arc I arrived on", which is all a search state carries here.
enum class TurnRestrictionKind : uint8_t {
  kNo = 0,    // no_left_turn, no_u_turn, ... — this turn is forbidden
  kOnly = 1,  // only_straight_on, ... — every OTHER turn is forbidden
};

struct TurnRestriction {
  uint32_t via_node = 0;
  uint32_t from_arc = 0;  // arc of via_node running back along the from way
  uint32_t to_arc = 0;    // arc of via_node the restricted turn would take
  TurnRestrictionKind kind = TurnRestrictionKind::kNo;
};

class RoadGraph {
 public:
  RoadGraph() = default;

  // --- topology ---------------------------------------------------------
  uint32_t node_count() const { return static_cast<uint32_t>(nodes_.size()); }
  uint32_t arc_count() const { return static_cast<uint32_t>(arcs_.size()); }
  uint32_t geometry_count() const { return static_cast<uint32_t>(geometry_.size()) / 2; }

  const RoadNode& node(uint32_t i) const { return nodes_[i]; }
  GeoPoint location(uint32_t i) const {
    return GeoPoint{nodes_[i].lat_e7 * 1e-7, nodes_[i].lon_e7 * 1e-7};
  }

  uint32_t arc_begin(uint32_t node) const { return arc_begin_[node]; }
  uint32_t arc_end(uint32_t node) const { return arc_begin_[node + 1]; }
  const RoadArc& arc(uint32_t i) const { return arcs_[i]; }

  // Interior geometry point `k` of arc `a`, in the arc's own direction of
  // travel (so walking a path's arcs concatenates without reversing).
  GeoPoint arc_point(const RoadArc& a, uint32_t k) const {
    const uint32_t idx = (a.flags & kArcGeomReversed) ? (a.geom_count - 1 - k) : k;
    const size_t base = 2 * (a.geom_begin + idx);
    return GeoPoint{geometry_[base] * 1e-7, geometry_[base + 1] * 1e-7};
  }

  const std::string& name(uint32_t idx) const { return names_[idx]; }

  GeoRect bounds() const { return bounds_; }

  // --- turn restrictions ------------------------------------------------
  uint32_t restriction_count() const { return static_cast<uint32_t>(restrictions_.size()); }
  const TurnRestriction& restriction(uint32_t i) const { return restrictions_[i]; }
  bool has_restrictions() const { return !restrictions_.empty(); }

  // True when this node is the via node of at least one restriction, i.e. the
  // one kind of node where a search has to remember how it arrived.
  bool node_restricted(uint32_t node) const {
    return restriction_at_.find(node) != restriction_at_.end();
  }

  // May a vehicle standing at `via`, having arrived along `from_arc`, leave
  // along `to_arc`? Both are arcs in via's own adjacency range. kNoArc for
  // either (the route starts or ends here) is always allowed — a restriction
  // constrains a turn, and neither end of a route is one.
  bool TurnAllowed(uint32_t via, uint32_t from_arc, uint32_t to_arc) const;

  // The arc of `from_node` whose target is `to_node`, or kNoArc. Linear in
  // the node's degree, so the router calls it only at restricted nodes.
  // NOTE: with parallel edges between the same pair this returns the first,
  // which is the same arbitrary choice OSM itself leaves open when a
  // restriction names a way that meets the via node twice.
  uint32_t ArcBetween(uint32_t from_node, uint32_t to_node) const;

  // --- search state space -----------------------------------------------
  // A plain Dijkstra label per node cannot express a turn restriction: the
  // cheapest way to reach the via node may be exactly the approach the sign
  // forbids, and the second-cheapest is the one that gets through. So a
  // restricted node is SPLIT — one state per arc it can be entered along,
  // plus one for "arrived along nothing" (a route's own start or end).
  // Every other node stays a single state whose id is the node id, which is
  // why a graph with no restrictions costs exactly what it did before O5.
  uint32_t state_count() const { return state_count_; }

  // First state id belonging to `node`'s split states, or kNoArc when the
  // node is not split (its only state is the node id itself).
  uint32_t state_base(uint32_t node) const {
    auto it = state_base_.find(node);
    return it == state_base_.end() ? kNoArc : it->second;
  }

  uint32_t node_of_state(uint32_t state) const {
    return state < nodes_.size() ? state : state_node_[state - nodes_.size()];
  }

  // --- lookup -----------------------------------------------------------
  // Nearest graph node to `p` within `max_meters`. Uses a uniform grid built
  // at load; returns false when nothing is in range.
  bool NearestNode(const GeoPoint& p, double max_meters, uint32_t* out_node,
                   double* out_meters = nullptr) const;

  // --- persistence ------------------------------------------------------
  Status Save(const std::string& path) const;
  static Status Load(const std::string& path, RoadGraph* out);

  // Rebuilds the derived bounds and grid index. Called by Load and by the
  // builder; public so a hand-assembled graph (tests) can finish itself.
  void Finalize();

  // --- assembly (builder / tests) ---------------------------------------
  std::vector<RoadNode>& mutable_nodes() { return nodes_; }
  std::vector<uint32_t>& mutable_arc_begin() { return arc_begin_; }
  std::vector<RoadArc>& mutable_arcs() { return arcs_; }
  std::vector<int32_t>& mutable_geometry() { return geometry_; }
  std::vector<std::string>& mutable_names() { return names_; }
  std::vector<TurnRestriction>& mutable_restrictions() { return restrictions_; }

 private:
  // Drops restrictions that name an arc this graph does not have, sorts what
  // is left and derives the lookup index and the split-state numbering.
  void BuildRestrictionIndex();

  std::vector<RoadNode> nodes_;
  std::vector<uint32_t> arc_begin_;  // node_count + 1 entries
  std::vector<RoadArc> arcs_;
  std::vector<int32_t> geometry_;    // lat,lon interleaved, degrees x 1e7
  std::vector<std::string> names_;   // names_[0] is always ""

  // Sorted by (via_node, from_arc) in Finalize; restriction_at_ maps a via
  // node to its half-open range. A hash map rather than a per-node array
  // because restricted nodes are a fraction of a percent of any extract and
  // a node_count-sized index would cost more than the restrictions do.
  std::vector<TurnRestriction> restrictions_;
  std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> restriction_at_;

  std::unordered_map<uint32_t, uint32_t> state_base_;  // split node -> first state
  std::vector<uint32_t> state_node_;  // state - node_count -> node
  uint32_t state_count_ = 0;

  GeoRect bounds_;

  // Uniform grid over `bounds_`, cell lists in CSR form.
  int grid_cols_ = 0;
  int grid_rows_ = 0;
  double grid_lat_step_ = 0.0;
  double grid_lon_step_ = 0.0;
  std::vector<uint32_t> grid_begin_;
  std::vector<uint32_t> grid_items_;
};

// Great-circle distance in metres on a sphere of the WGS-84 mean radius.
// The graph is built and queried with the same function, so edge lengths and
// the A* heuristic can never disagree about what a metre is.
double GreatCircleMeters(double lat1, double lon1, double lat2, double lon2);

// ---------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------

struct RoadGraphBuildOptions {
  // Keep foot/path/cycle classes in the graph. They cost little and a walking
  // profile is meaningless without them; a driving route ignores them.
  bool include_non_driveable = true;

  bool cycle_only = false;  // keep only cycleable classes (for a bike profile)

  // Admit `route=ferry` ways as arcs of RoadClass::kFerry (O5e). On, because
  // a ferry is often the only link between two halves of a coastal network
  // and a graph without it is disconnected where the real road system is not.
  // Off reproduces the pre-O5e graph exactly, which is what lets a test show
  // that a ferry is what changed an answer.
  bool include_ferries = true;

  // Honour `oneway`, `junction=roundabout` and the motorway implication.
  // Off makes every edge bidirectional — useful for diagnosing a graph, not
  // for producing a legal route.
  bool honor_oneway = true;

  // Read the access tags at all. On, a way barred to every mode is dropped
  // and one barred to some carries the bits; `private` is never a drop (see
  // ArcFlags). Off clears every access bit, which also throws away a real
  // `bicycle=no` — it is a diagnostic, not a way to soften `private`.
  bool honor_access = true;

  // Read `type=restriction` relations and resolve them onto the graph. Off
  // builds the O4 graph exactly, which is what the router's own tests use to
  // show that a restriction is what changed an answer.
  bool honor_turn_restrictions = true;
};

struct RoadGraphBuildStats {
  int64_t ways_seen = 0;
  int64_t ways_kept = 0;
  int64_t nodes_needed = 0;
  int64_t nodes_resolved = 0;  // < nodes_needed when the extract is clipped
  int64_t edges = 0;
  int64_t edges_dropped_unresolved = 0;

  // Access, reported because a graph that quietly lost its road network looks
  // exactly like a healthy one until a route fails. `ways_dropped_access` is
  // the ways barred to every mode at once; the arc counts are over the
  // finished graph, both directions, so they are twice the edge count.
  int64_t ways_dropped_access = 0;
  int64_t arcs_no_motor_vehicle = 0;
  int64_t arcs_no_bicycle = 0;
  int64_t arcs_no_foot = 0;
  int64_t arcs_private_motor_vehicle = 0;
  int64_t arcs_private_bicycle = 0;
  int64_t arcs_private_foot = 0;

  // O5e. `ways_ferry` counts the `route=ferry` ways admitted; `ferries_timed`
  // how many of them carried a `duration` the speed came from rather than the
  // class default. Arc counts are over the finished graph, both directions.
  int64_t ways_ferry = 0;
  int64_t ferries_timed = 0;
  int64_t arcs_ferry = 0;
  int64_t arcs_toll = 0;

  int64_t restrictions_seen = 0;      // type=restriction relations in the input
  int64_t restrictions_applied = 0;   // turns actually forbidden in the graph
  int64_t restrictions_via_way = 0;   // recognised, not applied (see the note)
  int64_t restrictions_unresolved = 0;  // via node or a named way not in the graph
  int64_t restrictions_excepted = 0;  // `except` lets a car through anyway
};

// Two passes over each input: ways first (to learn which node IDs matter),
// then nodes (to resolve only those). That is what keeps a continent-sized
// extract inside a laptop's memory — never hold every node in the file.
Status BuildRoadGraph(const std::vector<std::string>& inputs,
                      const RoadGraphBuildOptions& options, RoadGraph* out,
                      RoadGraphBuildStats* stats = nullptr);

}  // namespace routing
}  // namespace fv
