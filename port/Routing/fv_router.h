// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_router.h — shortest-path routing over a RoadGraph (O4).
//
// Two searches, same relaxation and the same cost function:
//   * unidirectional Dijkstra — the reference. Simple enough to be obviously
//     right, which is what the bidirectional one is tested against.
//   * bidirectional Dijkstra — the default. Two frontiers, stopping when
//     min_f + min_r >= the best meeting found so far.
//
// No contraction hierarchies: at region-extract scale (a US state, a few
// million edges) plain bidirectional Dijkstra answers in tens of
// milliseconds, and a CH would add a preprocessing stage and an ordering
// heuristic to maintain for no user-visible gain.
//
// O5 added turn restrictions, and they are the reason both searches label
// STATES rather than nodes: the cheapest way to a junction may be exactly the
// approach a sign forbids, so a single label per node cannot represent the
// route that gets through. Only the junctions a restriction names are split;
// everywhere else — and on any graph with no restrictions at all — a state is
// a node and the search is the one O4 shipped.
//
// O5d added ordered stops (RouteVia). A stop is not a new search problem —
// the stops are fixed and in order, so routing each consecutive pair really is
// optimal — but the pairs are not INDEPENDENT: what the route arrives at a
// stop along constrains what it may leave along. Chaining is therefore the
// whole of it, and it is done by seeding the next leg's start state with the
// arc the last one arrived on, so the existing turn machinery enforces signage
// at a stop for free. The second constraint, the U-turn, is expressed the same
// way: as a turn the search is not allowed to make.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fv_road_graph.h"
#include "fvkit/geo.h"

namespace fv {
namespace routing {

enum class RouteMetric : uint8_t {
  kTime = 0,      // seconds, using each arc's posted or assumed speed
  kDistance = 1,  // metres
};

// A cost profile loaded from the JSON rule file (O5c, fv_route_rules.h). Left
// incomplete here on purpose: the router only ever holds and consults one, and
// the rules loader pulls in a JSON parser that nothing else in this header's
// dependents needs.
struct RouteProfile;

// An avoidance penalty of this value means "may not be used at all", as
// opposed to a very large penalty, which means "only if there is no other
// way". The distinction matters: an excluded arc leaves an island served only
// by a ferry unreachable, a heavily penalised one still gets you there.
// Spelled the same way RouteProfile spells an excluded class (kClassExcluded).
constexpr double kAvoidExcluded = -1.0;

struct RouteOptions {
  // The rules this query costs arcs by (O5c). Null keeps the profile the
  // boolean fields below describe, computed exactly as O5b computed it — a
  // caller that never loads a rule file is unaffected by their existence.
  //
  // When set, the profile decides EVERYTHING the fields below would have:
  // which classes and access bits are usable, the speed, the metric, the
  // private/toll/ferry penalties, and whether turn signage binds.
  // SelectProfile() mirrors
  // its choice onto those fields so a UI reading them still sees the truth,
  // but the router does not consult them. Ownership is shared with the whole
  // rule set, so a mid-route file reload cannot pull it away.
  std::shared_ptr<const RouteProfile> profile;

  // Restrict to arcs a car may use. Off lets a route take footways and steps.
  bool driving = true;

  // Bicycle profile: only classes a bike may ride, at a flat speed, with
  // cycleways and quiet streets preferred. Overrides `driving` and `metric`.
  bool cycle_only = false;

  // What "shortest" means.
  RouteMetric metric = RouteMetric::kTime;

  // Run the two-frontier search. Off falls back to plain Dijkstra — same
  // answer, more expansions; the tests use it as the reference.
  bool bidirectional = true;

  // Obey the graph's turn restrictions. They are motor-vehicle signage, so
  // they apply to the driving profile only: a walking or cycling route is
  // unaffected whatever this says. Off is for diagnosing a graph — a route
  // that changes when this is cleared is a route a sign is steering.
  bool honor_turn_restrictions = true;

  // What an arc the active mode may use only `private`ly costs, as a
  // multiplier on its normal cost (O5b). A private road is passable — it is
  // the only way to reach anything behind the gate, and on a gated community
  // it is the whole street network — but a route should not cut through one
  // to save a minute. 1.0 makes private free; a very large value approaches
  // "avoid unless there is no other way", which is the shape wanted, and is
  // not the same as deleting the arc: the search still finds a route made
  // entirely of private roads.
  //
  // This is a PREFERENCE, not a clock: Route::seconds and Route::length_m are
  // reported unweighted, the same way the bicycle profile's class weights are.
  double private_penalty = 5.0;

  // What a tolled road and a ferry crossing cost, as a multiplier on their
  // normal cost (O5e); 1.0 is no preference either way, and kAvoidExcluded
  // bars them outright. Same shape as private_penalty and, like it, a
  // PREFERENCE — Route::seconds and Route::length_m are reported unweighted,
  // so a route over a ferry still reports the crossing's real duration.
  //
  // Two knobs rather than one "avoid" flag because the two are different
  // kinds of thing to a driver: a toll is money and a large multiplier says
  // "worth a detour, not worth an hour", while a ferry is a timetable nobody
  // here models and is often the only link there is. Excluding either is
  // available and is the honest expression of "I cannot pay" / "I will not
  // board"; it can make a destination unreachable, which is the answer.
  double toll_penalty = 1.0;
  double ferry_penalty = 1.0;

  // How far an endpoint may be from the road network before the request is
  // refused. Applies to Router::Route (geographic endpoints) only.
  double snap_meters = 500.0;

  // Snap a geographic endpoint onto the road SHAPE rather than onto the
  // nearest junction (§1d). On.
  //
  // WHY IT IS ON. A graph vertex exists only where ways meet or a way ends, so
  // the nearest NODE to somebody standing halfway down a block is a junction
  // that can be hundreds of metres away — and 2026-08-27's profile-aware
  // filter only made it the nearest junction this traveller may use, not the
  // nearest road. A rider standing on a cycleway was still attached to a
  // junction a hundred metres off it, and the route then began by walking
  // there. Projecting onto the segments puts the route where the request was.
  //
  // Off is the O4/O5 behaviour exactly and is a DIAGNOSTIC: a route that
  // changes when this is cleared is a route the snap was moving.
  bool snap_to_arcs = true;

  // Whether a multi-stop route (RouteVia) may leave an intermediate stop back
  // along the road it arrived on. Off — the default — is what a driver means
  // by "go via here": a stop dropped just past a junction should not make the
  // route go out and come straight back.
  //
  // "Off" cannot be absolute, because a stop at the end of a cul-de-sac has
  // no other way out. So a leg that cannot be routed at all with the U-turn
  // barred is retried with it allowed, and the stops where that happened are
  // reported in Route::u_turn_stops. This is a PREFERENCE and not a rule: a
  // U-turn that is merely expensive to avoid (round the block) is avoided,
  // and one that is impossible to avoid is taken.
  //
  // Signage is a separate matter and is never optional here: a no-U-turn
  // restriction at the stop binds through the ordinary turn machinery,
  // whatever this says.
  bool allow_u_turn_at_stops = false;
};

// One run of consecutive arcs sharing a road name — the human-readable spine
// of a route ("0.8 km on Folly Rd").
struct RouteLeg {
  std::string name;   // "" for an unnamed road
  std::string klass;  // the OSM highway value
  double length_m = 0.0;
  double seconds = 0.0;
};

struct Route {
  bool found = false;

  double length_m = 0.0;
  double seconds = 0.0;

  // The full drawn line, road geometry included, from the snapped start node
  // to the snapped end node. This is what goes onto the route overlay.
  std::vector<GeoPoint> geometry;

  // Graph node indices along the route, start and end inclusive. A node can
  // appear TWICE when a turn restriction forces the route back through a
  // junction it has already passed — that is a legal route, not a loop.
  std::vector<uint32_t> nodes;

  std::vector<RouteLeg> legs;

  // Where the request was snapped onto the network. `*_offset_m` is how far
  // the caller's point was from the point it snapped to — a large value means
  // the click was nowhere near a road, not that the route is bad.
  //
  // SINCE §1d the snap can land mid-road, and then these nodes are the first
  // and last JUNCTION the route reaches rather than where it begins: the line
  // in `geometry` starts at `start_point`, part-way along `start_arc`, and
  // runs to `start_node` from there. A route that never leaves one road
  // touches no junction at all, and both nodes are then that road's own source
  // node — which is the only honest answer a node index can give.
  uint32_t start_node = 0;
  uint32_t end_node = 0;
  double start_offset_m = 0.0;
  double end_offset_m = 0.0;

  // The road each end snapped onto, or kNoArc when it snapped to a junction.
  // Reported because "which road does this route start on" is a question a UI
  // asks and the node list can no longer answer.
  uint32_t start_arc = kNoArc;
  uint32_t end_arc = kNoArc;

  // Where each end actually is on the ground — `geometry.front()` and
  // `geometry.back()` when the route was found, and still filled when it was
  // not, which is what makes them worth reporting separately.
  GeoPoint start_point;
  GeoPoint end_point;

  // --- ordered stops (O5d) -------------------------------------------------
  // Empty on a two-point route. On a RouteVia these have one entry per stop,
  // first and last included, so a caller never has to special-case the ends.

  // The snapped node and snap distance for each stop, in request order.
  // `stop_nodes[i]` is kNoArc for a stop that snapped MID-ROAD (§1d) — there
  // is no junction there, and naming the nearest one would be a different
  // place from where the route actually passes. `stop_points` is always the
  // truth and is what a UI should draw.
  std::vector<uint32_t> stop_nodes;
  std::vector<double> stop_offsets_m;
  std::vector<GeoPoint> stop_points;

  // Where each stop falls in `geometry`, so a UI can mark the stops or cut
  // the line into per-leg pieces. Strictly increasing except where two stops
  // snap to the same node, which is a legal (zero-length) leg.
  std::vector<uint32_t> stop_geometry_index;

  // The stops the route had to U-turn at because there was no other way out
  // (see RouteOptions::allow_u_turn_at_stops). Always empty when U-turns are
  // allowed outright — then no stop "had to".
  std::vector<uint32_t> u_turn_stops;

  // On `found == false`, which pair of consecutive stops has no route between
  // them: leg i runs from stop i to stop i+1. kNoLeg when the route was found
  // or the failure was not a leg's.
  static constexpr uint32_t kNoLeg = 0xFFFFFFFFu;
  uint32_t unreachable_leg = kNoLeg;

  // Search states settled. Reported so a caller (and the tests) can see what
  // the bidirectional search actually saved. Away from a turn restriction a
  // state is a node, so on most graphs this is a node count exactly. On a
  // multi-stop route it is the total over every leg, retries included.
  int64_t nodes_expanded = 0;
};

// Is `arc` in the search space this query defines? THE ROUTER'S OWN ANSWER,
// pulled out of `Router` so something that is not routing can ask it (Pippin
// P11: a pick button that names the nearest road has to name one the very next
// replan can actually use, or it promises a road and then refuses it). The
// profile decides — its class weights and the access bits for its travel mode
// — and `private` is deliberately NOT a refusal: O5b prices a private arc
// rather than deleting it, because on a gated island the private roads ARE the
// street network.
//
// `Router::ArcUsable` is this function; the member remains because the search
// calls it on every relaxation and reads better as one.
bool ArcUsable(const RoadArc& arc, const RouteOptions& options);

// Where a route touches the network (§1d). Either a graph NODE — a junction,
// which is all a node-indexed snap can find and all O4 could express — or a
// POINT ON A ROAD, which is where somebody standing halfway down a block
// actually is. Everything the router does with stops is expressed in these, so
// the node API and the geographic one are the same code with a different snap.
struct RouteAnchor {
  uint32_t arc = kNoArc;  // kNoArc for a node anchor
  uint32_t node = 0;      // node anchor: the node. Arc anchor: the arc's source.
  uint32_t to = 0;        // arc anchor: the arc's target
  double t = 0.0;         // arc anchor: fraction of the arc, `node` -> `to`
  GeoPoint point;         // where this is on the ground

  bool mid_arc() const { return arc != kNoArc; }

  static RouteAnchor OnNode(uint32_t node, const GeoPoint& p) {
    RouteAnchor a;
    a.node = node;
    a.point = p;
    return a;
  }

  // A snap that landed on an endpoint becomes a NODE anchor, not an arc one at
  // t = 0 or 1: the two describe the same place, and the node form is the one
  // every other part of the router already knows how to chain, seed and report.
  static RouteAnchor OnArc(const RoadGraph::ArcSnap& s) {
    RouteAnchor a;
    if (s.at_node()) {
      a.node = s.nearest_end();
      a.point = s.point;
      return a;
    }
    a.arc = s.arc;
    a.node = s.from;
    a.to = s.to;
    a.t = s.t();
    a.point = s.point;
    return a;
  }
};

class Router {
 public:
  explicit Router(const RoadGraph& graph) : graph_(graph) {}

  // Snaps both endpoints to the nearest graph node within
  // `options.snap_meters` and routes between them.
  Status Route(const GeoPoint& from, const GeoPoint& to, const RouteOptions& options,
               routing::Route* out) const;

  // Routes between graph nodes directly. Returns Ok with `found == false`
  // when the two are not connected under these options — an unreachable
  // destination is an answer, not an error.
  Status RouteNodes(uint32_t from_node, uint32_t to_node, const RouteOptions& options,
                    routing::Route* out) const;

  // One route THROUGH `stops`, in the order given: not a concatenation of
  // independent pairs but a single continuous route that arrives at each stop
  // and leaves it consistently. Two stops is exactly Route(); fewer is an
  // error. Every stop is snapped first, and a stop that snaps nowhere fails
  // the whole request (kOutOfCoverage, naming which one) rather than quietly
  // dropping it.
  //
  // All or nothing: one unreachable pair makes the whole route `found ==
  // false` with `unreachable_leg` naming it. A caller that would rather draw
  // what it can — the application does — routes the pairs itself on that
  // answer; this call will not half-deliver a through route.
  Status RouteVia(const std::vector<GeoPoint>& stops, const RouteOptions& options,
                  routing::Route* out) const;

  // RouteVia on graph nodes, skipping the snap.
  Status RouteNodesVia(const std::vector<uint32_t>& stops, const RouteOptions& options,
                       routing::Route* out) const;

  // The one that does the work: RouteVia over anchors already snapped. Public
  // because a caller that has done its own snapping — the moving map, which
  // already holds a RoadGraphNetwork, or an audit tool asking what a specific
  // point on a specific road routes to — should not have to make the router
  // snap a second time and get a different answer.
  Status RouteAnchorsVia(const std::vector<RouteAnchor>& stops, const RouteOptions& options,
                         routing::Route* out) const;

  // Where `p` lands on the network under `options` — the snap Route() and
  // RouteVia() do, exposed so a caller can see it, or refuse it, before
  // committing to a route.
  bool Snap(const GeoPoint& p, const RouteOptions& options, RouteAnchor* out) const;

  const RoadGraph& graph() const { return graph_; }

 private:
  // A traversal of one arc, possibly only part of it (§1d).
  //
  // `t0` and `t1` are fractions of the arc's shape length measured in the
  // ARC'S OWN direction — source to target, the direction `arc_point` walks —
  // so a whole arc taken with the grain runs 0 -> 1 and against it 1 -> 0.
  // That is where the old `stored_order` flag went: it is the sign of
  // `t1 - t0`, and keeping the two as one pair means a partial traversal and a
  // reversed one are the same kind of thing rather than two special cases.
  struct Step {
    uint32_t arc = 0;
    double t0 = 0.0;
    double t1 = 1.0;

    bool stored_order() const { return t1 >= t0; }
    double fraction() const { return t1 >= t0 ? t1 - t0 : t0 - t1; }

    static Step Whole(uint32_t arc, bool stored_order) {
      Step s;
      s.arc = arc;
      s.t0 = stored_order ? 0.0 : 1.0;
      s.t1 = stored_order ? 1.0 : 0.0;
      return s;
    }
  };

  // One end of a search: a state to seed, and what standing there has already
  // cost. A node anchor gives ONE of these; a mid-arc anchor gives up to two,
  // one for each end of the road the traveller may set off toward, each
  // carrying the price of the part of that road it would use.
  //
  // This is what makes mid-arc routing a seeding change rather than a search
  // change: both frontiers are still plain Dijkstra, started from a set.
  struct Terminal {
    uint32_t node = 0;
    uint32_t arc = kNoArc;  // the arc the seeded state remembers
    double cost = 0.0;      // the partial arc already paid for
    // The partial traversal this terminal stands for, prepended (source) or
    // appended (target) to the path the search finds. Absent for a node
    // anchor, which begins and ends at the junction itself.
    bool partial = false;
    Step step;
  };

  // What a leg inherits from the one before it (O5d). All three are kNoArc /
  // kNoNode on the first leg, which is what makes a one-leg RouteVia identical
  // to a plain Route.
  struct LegSeed {
    // A stop ON A JUNCTION: arcs of the leg's start node. The U-turn is barred
    // as a TURN, through the same machinery signage uses.
    uint32_t entry_arc = kNoArc;   // the arc the previous leg arrived along
    uint32_t banned_arc = kNoArc;  // an arc this leg may not leave along

    // A stop MID-ROAD (§1d): there is no junction to turn at, so the U-turn is
    // barred by DROPPING a source instead — the endpoint the previous leg came
    // from is the one this leg may not set off back toward. Simpler than the
    // junction case, and it needs no state space of its own.
    uint32_t banned_toward = kNoArc;
  };

  double ArcCost(const RoadArc& arc, const RouteOptions& options) const;
  bool ArcUsable(const RoadArc& arc, const RouteOptions& options) const;

  // Turn restrictions bind a car only, and only when the graph has any.
  bool UseTurnRestrictions(const RouteOptions& options) const;

  // The terminals an anchor offers a search. `banned_toward` drops the source
  // that would U-turn on a mid-arc stop; `entry_arc` is the junction-stop
  // equivalent and is carried on the single terminal a node anchor gives.
  void SourcesFor(const RouteAnchor& a, const RouteOptions& o, const LegSeed& seed,
                  std::vector<Terminal>* out) const;
  void TargetsFor(const RouteAnchor& a, const RouteOptions& o,
                  std::vector<Terminal>* out) const;

  bool SearchUnidirectional(const std::vector<Terminal>& sources,
                            const std::vector<Terminal>& targets, const RouteOptions& o,
                            const LegSeed& seed, std::vector<Step>* steps, uint32_t* source_used,
                            uint32_t* target_used, int64_t* expanded) const;
  bool SearchBidirectional(const std::vector<Terminal>& sources,
                           const std::vector<Terminal>& targets, const RouteOptions& o,
                           const LegSeed& seed, std::vector<Step>* steps, uint32_t* source_used,
                           uint32_t* target_used, int64_t* expanded) const;

  // Either search, chosen by `o.bidirectional`. `steps` comes back WITHOUT the
  // terminals' own partial traversals; the caller prepends and appends them,
  // because only it knows whether a leg's ends are shared with its neighbours.
  bool Search(const std::vector<Terminal>& sources, const std::vector<Terminal>& targets,
              const RouteOptions& o, const LegSeed& seed, std::vector<Step>* steps,
              uint32_t* source_used, uint32_t* target_used, int64_t* expanded) const;

  // One leg, terminals and all: the search plus the partial traversals its two
  // ends contribute, plus the one case no search can find — both ends on the
  // SAME road, where the answer never touches a junction.
  bool RouteLegBetween(const RouteAnchor& from, const RouteAnchor& to, const RouteOptions& o,
                       const LegSeed& seed, std::vector<Step>* steps, int64_t* expanded) const;

  // The node a step ends at, or kNoArc when it ends part-way along a road.
  uint32_t StepEndNode(const Step& step) const;
  uint32_t StepStartNode(const Step& step) const;

  // The arc of the route's last node that faces back down the route — what the
  // next leg inherits as its entry arc. kNoArc when the leg ended mid-road,
  // where the U-turn is barred by dropping a source instead.
  uint32_t ArrivalArc(const std::vector<Step>& steps) const;

  // `geom_at_step[i]` is the index in `out->geometry` of the point step i
  // ends at, so a caller can mark a stop without re-walking the line.
  void Materialize(const std::vector<Step>& steps, const RouteOptions& options,
                   routing::Route* out, std::vector<uint32_t>* geom_at_step) const;

  // The arc's shape and the cumulative length to each of its points, in the
  // arc's own direction. `cum.back()` is the shape length the fractions are
  // taken against.
  void ArcShapeAndLengths(uint32_t arc, std::vector<GeoPoint>* shape,
                          std::vector<double>* cum) const;

  const RoadGraph& graph_;
};

}  // namespace routing
}  // namespace fv
