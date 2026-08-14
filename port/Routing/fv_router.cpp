// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fv_router.h"

#include "fv_route_rules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace fv {
namespace routing {
namespace {

constexpr uint32_t kNoIndex = 0xFFFFFFFFu;
const double kInf = std::numeric_limits<double>::infinity();

struct QueueEntry {
  double key;
  uint32_t state;
};

struct GreaterKey {
  bool operator()(const QueueEntry& a, const QueueEntry& b) const { return a.key > b.key; }
};

using MinQueue = std::priority_queue<QueueEntry, std::vector<QueueEntry>, GreaterKey>;

// A bike and a pair of legs hold one speed whatever the road's posted limit
// is, so the driving clock (length / maxspeed) means nothing to either: these
// flat speeds are what both the cost and the reported duration are built on.
// The arc's own speed_kph stays the driving speed and is left alone — a
// residential street is 35 km/h to a car whoever else is on it.
constexpr double kBikeSpeedMps = 15.0 / 3.6;
constexpr double kWalkSpeedMps = 5.0 / 3.6;

double BikeSeconds(const RoadArc& arc) {
  return static_cast<double>(arc.length_m) / kBikeSpeedMps;
}

double WalkSeconds(const RoadArc& arc) {
  return static_cast<double>(arc.length_m) / kWalkSpeedMps;
}

// How long this arc takes under the profile the query asked for. With a rule
// file loaded (O5c) the speed comes from the profile; without one these are
// the three speeds O5b compiled in.
double ProfileSeconds(const RoadArc& arc, const RouteOptions& options) {
  // You do not walk a ferry (O5e). Every other class travels at whatever
  // speed the profile says its mode goes, because the traveller is what moves;
  // on a crossing the BOAT is, so the arc's own speed — which the builder
  // derived from the way's `duration` — is the clock for every profile alike.
  if (arc.is_ferry()) return arc.travel_seconds();
  if (options.profile != nullptr)
    return options.profile->Seconds(static_cast<double>(arc.length_m), arc.speed_kph);
  if (options.cycle_only) return BikeSeconds(arc);
  return options.driving ? arc.travel_seconds() : WalkSeconds(arc);
}

// --- the search state space (O5) -------------------------------------------
//
// Both searches label STATES, not nodes. Away from a turn restriction a state
// IS a node and this whole class is the identity function — a graph with no
// restrictions, or a walking query on one that has them, searches exactly the
// space O4 searched. At a restricted via node the state also carries one arc:
//
//   forward  — the arc the path ARRIVED along (so the next turn can be tested)
//   reverse  — the arc the path will LEAVE along (the same turn, seen from the
//              other end: the reverse search meets a junction before it knows
//              what came into it, and after it knows what leaves)
//
// In both cases the arc is one of the via node's own, which is why the same
// numbering serves both frontiers.
//
// O5d adds two things, and both exist only for a leg of a multi-stop route:
//
//   * an EXTRA split node — the leg's start. A leg inherits the arc the
//     previous one arrived on, and inheriting it means nothing unless the
//     start node can hold it, which on an unrestricted graph it otherwise
//     cannot. The extra states are numbered above every other state, so
//     nothing else's numbering moves.
//   * a BANNED turn, which is how the U-turn out of a stop is expressed. It
//     goes through Allowed() like signage does, so the forward frontier, the
//     reverse frontier and the meeting test all obey it without one of them
//     having to be told about stops.
//
// Neither is constructed for a plain two-point route, so that search is
// exactly the one O5c shipped, state for state.
class StateSpace {
 public:
  struct BannedTurn {
    uint32_t via = kNoArc;
    uint32_t from_arc = kNoArc;
    uint32_t to_arc = kNoArc;
  };

  StateSpace(const RoadGraph& graph, bool enabled, uint32_t extra_split_node,
             const BannedTurn& banned)
      : g_(graph),
        on_(enabled && graph.has_restrictions()),
        count_(on_ ? graph.state_count() : graph.node_count()),
        banned_(banned) {
    // Only worth a block of its own if the node is not already split — if it
    // is, it already remembers exactly the arc the leg needs it to.
    if (extra_split_node != kNoArc && !(on_ && g_.state_base(extra_split_node) != kNoArc)) {
      extra_node_ = extra_split_node;
      extra_base_ = count_;
      count_ += Degree(extra_node_) + 1;
    }
  }

  uint32_t count() const { return count_; }

  bool split(uint32_t node) const {
    return node == extra_node_ || (on_ && g_.state_base(node) != kNoArc);
  }

  uint32_t NodeOf(uint32_t state) const {
    if (state >= extra_base_) return extra_node_;
    return on_ ? g_.node_of_state(state) : state;
  }

  // The state of `node` for a path that arrives (or leaves) along `arc`.
  // kNoArc means "along nothing" — a route's own start or end.
  uint32_t StateOf(uint32_t node, uint32_t arc) const {
    const uint32_t base = BaseOf(node);
    if (base == kNoArc) return node;
    return arc == kNoArc ? base + Degree(node) : base + (arc - g_.arc_begin(node));
  }

  // The arc `state` remembers, or kNoArc when it remembers none.
  uint32_t ArcOf(uint32_t state, uint32_t node) const {
    const uint32_t base = BaseOf(node);
    if (base == kNoArc || state < base) return kNoArc;
    const uint32_t local = state - base;
    return local >= Degree(node) ? kNoArc : g_.arc_begin(node) + local;
  }

  bool Allowed(uint32_t via, uint32_t from_arc, uint32_t to_arc) const {
    if (via == banned_.via && from_arc == banned_.from_arc && to_arc == banned_.to_arc) {
      return false;
    }
    return !on_ || g_.TurnAllowed(via, from_arc, to_arc);
  }

  // Every state a path can be in at `node` — one, unless the node is split.
  template <typename Fn>
  void ForEachState(uint32_t node, Fn fn) const {
    const uint32_t base = BaseOf(node);
    if (base == kNoArc) { fn(node); return; }
    const uint32_t degree = Degree(node);
    for (uint32_t k = 0; k <= degree; ++k) fn(base + k);
  }

 private:
  uint32_t Degree(uint32_t node) const { return g_.arc_end(node) - g_.arc_begin(node); }

  // Where `node`'s block of states starts, or kNoArc when it has none and a
  // state is just the node.
  uint32_t BaseOf(uint32_t node) const {
    if (node == extra_node_) return extra_base_;
    return on_ ? g_.state_base(node) : kNoArc;
  }

  const RoadGraph& g_;
  bool on_;
  uint32_t count_;
  BannedTurn banned_;
  uint32_t extra_node_ = kNoArc;
  // Above every ordinary state, so `state >= extra_base_` identifies the block
  // in one comparison. kNoIndex when there is no extra block, which makes that
  // comparison false for every real state.
  uint32_t extra_base_ = kNoIndex;
};

}  // namespace

bool Router::UseTurnRestrictions(const RouteOptions& options) const {
  // A no-left-turn sign is addressed to vehicles; a pedestrian crossing the
  // same junction is not turning left in any sense the sign means. The graph
  // carries no per-mode exceptions beyond the `except` the builder already
  // applied, so the profile is where the line gets drawn.
  if (!options.honor_turn_restrictions || !graph_.has_restrictions()) return false;
  // The query's own --ignore-turns still wins over the rule file: the flag is
  // a diagnostic ("show me the route the signs are steering"), and a profile
  // that turned it back on would take that tool away.
  if (options.profile != nullptr) {
    return options.profile->turn_restrictions &&
           options.profile->mode == TravelMode::kMotorVehicle;
  }
  return options.driving && !options.cycle_only;
}

namespace {

// The multiplier this arc earns for being private to the mode being routed.
// Which mode is asking matters: Kiawah's trails are private to drive and
// explicitly open to ride, so a bicycle must not pay for the car's gate.
double PrivateFactor(const RoadArc& arc, const RouteOptions& options) {
  if (options.profile != nullptr) {
    const RouteProfile& p = *options.profile;
    const bool is_private = p.mode == TravelMode::kBicycle ? arc.bicycle_private()
                            : p.mode == TravelMode::kFoot  ? arc.foot_private()
                                                           : arc.motor_vehicle_private();
    return is_private ? (p.private_penalty > 0.0 ? p.private_penalty : 1.0) : 1.0;
  }
  const bool is_private = options.cycle_only ? arc.bicycle_private()
                          : options.driving  ? arc.motor_vehicle_private()
                                             : arc.foot_private();
  return is_private ? (options.private_penalty > 0.0 ? options.private_penalty : 1.0) : 1.0;
}

// The toll and ferry preferences (O5e). Unlike every other profile-backed
// setting these are read from the OPTIONS and never from the profile, because
// they are the two a caller overrides per query: SelectProfile seeds them from
// the chosen profile and whatever the user then asks for has the last word.
// An excluded one never reaches here — ArcUsable has already refused the arc —
// so a non-positive value can only be a caller's own nonsense and means "no
// preference", the same reading private_penalty gives it.
double AvoidFactor(const RoadArc& arc, const RouteOptions& options) {
  double factor = 1.0;
  if (arc.tolled() && options.toll_penalty > 0.0) factor *= options.toll_penalty;
  if (arc.is_ferry() && options.ferry_penalty > 0.0) factor *= options.ferry_penalty;
  return factor;
}

}  // namespace

double Router::ArcCost(const RoadArc& arc, const RouteOptions& options) const {
  const double gate = PrivateFactor(arc, options) * AvoidFactor(arc, options);
  // O5c: one formula, base cost times the class weight the rules give it. The
  // bicycle branch below is the same arithmetic with the multipliers written
  // in C++ instead — which is why the builtin rules reproduce it exactly.
  if (options.profile != nullptr) {
    const RouteProfile& p = *options.profile;
    // Through ProfileSeconds and not p.Seconds directly: that is the one
    // place the ferry rule lives, and calling past it costs a crossing at the
    // profile's own flat speed while Materialize (which does go through it)
    // reports the boat's — the cost and the reported clock would disagree.
    const double base = p.metric == RouteMetric::kDistance
                            ? static_cast<double>(arc.length_m)
                            : ProfileSeconds(arc, options);
    return base * p.weight(arc.klass) * gate;
  }
  if (options.cycle_only) {
    // Bike time, scaled by how pleasant the class is to ride. The scaling is
    // a PREFERENCE, not a clock — Materialize reports the unscaled seconds.
    // `metric` has no say here: distance and time differ only by a constant
    // once the speed is flat, so a bike query is always this weighted time.
    double bike_cost = ProfileSeconds(arc, options);
    if (arc.klass == RoadClass::kCycleway) {
      bike_cost *= 0.50;  // the whole point of a bike is to use these
    } else if (arc.klass == RoadClass::kPath) {
      bike_cost *= 0.65;  // what a bike is for
    } else if (arc.klass == RoadClass::kResidential ||
               arc.klass == RoadClass::kLivingStreet) {
      bike_cost *= 0.85;  // quiet enough to be nearly as good
    } else if (arc.klass == RoadClass::kFootway ||
               arc.klass == RoadClass::kPedestrian) {
      bike_cost *= 1.5;  // walking the bike
    }
    return bike_cost * gate;
  }
  const double base = options.metric == RouteMetric::kDistance
                          ? static_cast<double>(arc.length_m)
                          : ProfileSeconds(arc, options);
  return base * gate;
}

bool Router::ArcUsable(const RoadArc& arc, const RouteOptions& options) const {
  // The hard half of the toll/ferry preference (O5e), before anything else:
  // an excluded arc is not in the search space at all, so both frontiers and
  // the meeting test agree about it without being told separately. This is
  // an ARC filter, not a turn one, so it is safe as a filter on relaxation —
  // the rule that a constraint cannot be one applies to constraints over
  // PAIRS of arcs (a turn), which no frontier can decide alone.
  if (arc.tolled() && options.toll_penalty == kAvoidExcluded) return false;
  if (arc.is_ferry() && options.ferry_penalty == kAvoidExcluded) return false;
  if (options.profile != nullptr) {
    const RouteProfile& p = *options.profile;
    if (!p.allows(arc.klass)) return false;
    switch (p.mode) {
      case TravelMode::kBicycle: return arc.bicycle_allowed();
      case TravelMode::kFoot: return arc.foot_allowed();
      case TravelMode::kMotorVehicle: return arc.motor_vehicle_allowed();
    }
    return false;
  }
  // cycle_only wins over driving: a bike profile that then dropped every
  // non-driveable arc would throw away the cycleways and paths it exists to
  // find. Works on a graph built WITHOUT --cycle-only too, which is the
  // point — the filter belongs to the query, not only to the build.
  if (options.cycle_only) return IsCycleable(arc.klass) && arc.bicycle_allowed();
  if (options.driving) return IsDriveable(arc.klass) && arc.motor_vehicle_allowed();
  return arc.foot_allowed();
}

// ---------------------------------------------------------------------------
// Unidirectional Dijkstra — the reference implementation
// ---------------------------------------------------------------------------

namespace {

// The state space a leg searches in: ordinary for a plain route, and with the
// start node split (and possibly one turn out of it barred) for a leg that
// carries something over from the leg before it.
StateSpace SpaceFor(const RoadGraph& graph, bool restrictions, uint32_t s, uint32_t entry_arc,
                    uint32_t banned_arc) {
  const bool seeded = entry_arc != kNoArc || banned_arc != kNoArc;
  StateSpace::BannedTurn ban;
  if (banned_arc != kNoArc) {
    // A U-turn is one turn: in along the arc, out along the same arc. Barring
    // it needs the entry arc, so a ban without one is not expressible — and
    // never asked for, since both come from the same previous leg.
    ban.via = s;
    ban.from_arc = entry_arc;
    ban.to_arc = banned_arc;
  }
  return StateSpace(graph, restrictions, seeded ? s : kNoArc, ban);
}

}  // namespace

bool Router::SearchUnidirectional(uint32_t s, uint32_t t, const RouteOptions& o,
                                  const LegSeed& seed, std::vector<Step>* steps,
                                  std::vector<uint32_t>* nodes, int64_t* expanded) const {
  const StateSpace space =
      SpaceFor(graph_, UseTurnRestrictions(o), s, seed.entry_arc, seed.banned_arc);
  const uint32_t n = space.count();
  // Scratch is per-query rather than a member: Router is const and shared,
  // and a full reset of two arrays costs a couple of milliseconds even on a
  // state-sized graph — well under the search itself.
  std::vector<double> dist(n, kInf);
  std::vector<uint32_t> parent_arc(n, kNoIndex);
  std::vector<uint32_t> parent_state(n, kNoIndex);
  std::vector<uint8_t> settled(n, 0);

  // The leg starts having arrived along `seed.entry_arc` — kNoArc on a plain
  // route, which is the "arrived along nothing" state and turns every test at
  // s into a no-op.
  const uint32_t s_state = space.StateOf(s, seed.entry_arc);
  MinQueue queue;
  dist[s_state] = 0.0;
  queue.push(QueueEntry{0.0, s_state});

  uint32_t goal = kNoIndex;
  while (!queue.empty()) {
    const QueueEntry top = queue.top();
    queue.pop();
    if (settled[top.state]) continue;  // stale entry: this queue is lazy-deleted
    settled[top.state] = 1;
    ++*expanded;
    const uint32_t u = space.NodeOf(top.state);
    // The target may have several states; the first one settled is the
    // cheapest, because the queue pops in nondecreasing key order.
    if (u == t) { goal = top.state; break; }

    const uint32_t arrival = space.ArcOf(top.state, u);
    for (uint32_t a = graph_.arc_begin(u); a < graph_.arc_end(u); ++a) {
      const RoadArc& arc = graph_.arc(a);
      if (!arc.forward() || !ArcUsable(arc, o)) continue;
      if (!space.Allowed(u, arrival, a)) continue;
      const uint32_t v = arc.target;
      // Arriving at v along this arc is, from v's side, the arc that points
      // back at u — which is the arc a restriction at v names as its `from`.
      const uint32_t next =
          space.split(v) ? space.StateOf(v, graph_.ArcBetween(v, u)) : v;
      const double nd = top.key + ArcCost(arc, o);
      if (nd < dist[next]) {
        dist[next] = nd;
        parent_arc[next] = a;
        parent_state[next] = top.state;
        queue.push(QueueEntry{nd, next});
      }
    }
  }

  if (goal == kNoIndex) return false;

  std::vector<uint32_t> rev_nodes;
  std::vector<Step> rev_steps;
  for (uint32_t x = goal; x != s_state; x = parent_state[x]) {
    if (parent_state[x] == kNoIndex) return false;
    rev_nodes.push_back(space.NodeOf(x));
    rev_steps.push_back(Step{parent_arc[x], true});
  }
  rev_nodes.push_back(s);
  std::reverse(rev_nodes.begin(), rev_nodes.end());
  std::reverse(rev_steps.begin(), rev_steps.end());
  *nodes = std::move(rev_nodes);
  *steps = std::move(rev_steps);
  return true;
}

// ---------------------------------------------------------------------------
// Bidirectional Dijkstra
// ---------------------------------------------------------------------------

bool Router::SearchBidirectional(uint32_t s, uint32_t t, const RouteOptions& o,
                                 const LegSeed& seed, std::vector<Step>* steps,
                                 std::vector<uint32_t>* nodes, int64_t* expanded) const {
  const StateSpace space =
      SpaceFor(graph_, UseTurnRestrictions(o), s, seed.entry_arc, seed.banned_arc);
  const uint32_t n = space.count();
  std::vector<double> df(n, kInf), dr(n, kInf);
  std::vector<uint32_t> fparent_arc(n, kNoIndex), fparent_state(n, kNoIndex);
  std::vector<uint32_t> rparent_arc(n, kNoIndex), rparent_state(n, kNoIndex);
  std::vector<uint8_t> fsettled(n, 0), rsettled(n, 0);

  // Forward starts where the previous leg left the path (kNoArc on a plain
  // route); the reverse frontier's own seed is unconditionally "leaves along
  // nothing", because the end of a leg is where the route stops.
  const uint32_t s_state = space.StateOf(s, seed.entry_arc);
  const uint32_t t_state = space.StateOf(t, kNoArc);

  MinQueue fq, rq;
  df[s_state] = 0.0;
  dr[t_state] = 0.0;
  fq.push(QueueEntry{0.0, s_state});
  rq.push(QueueEntry{0.0, t_state});

  double best = kInf;
  uint32_t meet_f = kNoIndex, meet_r = kNoIndex;

  // The two frontiers label the same junction differently — forward by the
  // arc it came in on, reverse by the arc it will go out on — so a meeting is
  // a PAIR of states, and the pair only joins into a path if the turn between
  // those two arcs is legal. Away from a restriction there is one state each
  // and this reduces to the O4 test, `df[v] + dr[v]`.
  auto ConsiderMeeting = [&](bool from_forward, uint32_t node, uint32_t state, double cost) {
    const std::vector<double>& other = from_forward ? dr : df;
    space.ForEachState(node, [&](uint32_t ostate) {
      if (!std::isfinite(other[ostate])) return;
      const uint32_t fstate = from_forward ? state : ostate;
      const uint32_t rstate = from_forward ? ostate : state;
      if (!space.Allowed(node, space.ArcOf(fstate, node), space.ArcOf(rstate, node))) return;
      const double total = cost + other[ostate];
      if (total < best) {
        best = total;
        meet_f = fstate;
        meet_r = rstate;
      }
    });
  };

  while (!fq.empty() && !rq.empty()) {
    // Both frontiers are monotone, so once their two minimum keys together
    // cover the best meeting found, nothing cheaper can still appear.
    if (fq.top().key + rq.top().key >= best) break;

    // Advance whichever side is currently cheaper — the usual balancing rule.
    const bool go_forward = fq.top().key <= rq.top().key;
    MinQueue& queue = go_forward ? fq : rq;
    std::vector<double>& dist = go_forward ? df : dr;
    std::vector<uint8_t>& settled = go_forward ? fsettled : rsettled;
    std::vector<uint32_t>& parent_arc = go_forward ? fparent_arc : rparent_arc;
    std::vector<uint32_t>& parent_state = go_forward ? fparent_state : rparent_state;

    const QueueEntry top = queue.top();
    queue.pop();
    if (settled[top.state]) continue;
    settled[top.state] = 1;
    ++*expanded;

    const uint32_t u = space.NodeOf(top.state);
    // Forward: the arc this path came in on. Reverse: the arc it goes out on.
    const uint32_t marker = space.ArcOf(top.state, u);

    for (uint32_t a = graph_.arc_begin(u); a < graph_.arc_end(u); ++a) {
      const RoadArc& arc = graph_.arc(a);
      if (!ArcUsable(arc, o)) continue;
      // Forward stands at u and drives u -> target: needs kArcForward.
      // Backward stands at v and asks who can drive INTO v: that is exactly
      // kArcBackward on v's own arc, which is why one CSR serves both.
      if (go_forward ? !arc.forward() : !arc.backward()) continue;
      // The same turn from both ends. Forward knows what it arrived on and is
      // choosing where to go; reverse knows where it is going and is choosing
      // what arrives — and what arrives at u from arc.target is arc `a`.
      if (!(go_forward ? space.Allowed(u, marker, a) : space.Allowed(u, a, marker))) continue;

      const double nd = top.key + ArcCost(arc, o);
      const uint32_t w = arc.target;
      // Either way round, the state at w records the arc of w that faces u:
      // forward that is what it arrived along, reverse what it will leave
      // along, and for this one step they are the same piece of road.
      const uint32_t next = space.split(w) ? space.StateOf(w, graph_.ArcBetween(w, u)) : w;
      if (nd < dist[next]) {
        dist[next] = nd;
        parent_arc[next] = a;
        parent_state[next] = top.state;
        queue.push(QueueEntry{nd, next});
      }
      ConsiderMeeting(go_forward, w, next, nd);
    }

    // The start node itself can be the meeting point when the other search
    // has already reached it and no arc relaxation will ever revisit it.
    ConsiderMeeting(go_forward, u, top.state, top.key);
  }

  if (meet_f == kNoIndex) return false;

  std::vector<uint32_t> path_nodes;
  std::vector<Step> path_steps;

  // s -> meeting, walked backwards through the forward parents.
  std::vector<uint32_t> head_nodes;
  std::vector<Step> head_steps;
  for (uint32_t x = meet_f; x != s_state; x = fparent_state[x]) {
    if (fparent_state[x] == kNoIndex) return false;  // defensive: no forward tree
    head_nodes.push_back(space.NodeOf(x));
    head_steps.push_back(Step{fparent_arc[x], true});
  }
  head_nodes.push_back(s);
  std::reverse(head_nodes.begin(), head_nodes.end());
  std::reverse(head_steps.begin(), head_steps.end());
  path_nodes = std::move(head_nodes);
  path_steps = std::move(head_steps);

  // meeting -> t. The reverse tree found u from v across an arc that lives in
  // v's adjacency, so travelling u -> v walks that arc against stored order.
  for (uint32_t x = meet_r; x != t_state;) {
    const uint32_t px = rparent_state[x];
    if (px == kNoIndex) return false;
    path_steps.push_back(Step{rparent_arc[x], false});
    path_nodes.push_back(space.NodeOf(px));
    x = px;
  }

  *nodes = std::move(path_nodes);
  *steps = std::move(path_steps);
  return true;
}

// ---------------------------------------------------------------------------
// Materialization
// ---------------------------------------------------------------------------

void Router::Materialize(const std::vector<Step>& steps, const std::vector<uint32_t>& nodes,
                         const RouteOptions& options, routing::Route* out) const {
  out->found = true;
  out->nodes = nodes;
  out->geometry.clear();
  out->legs.clear();
  out->length_m = 0.0;
  out->seconds = 0.0;
  if (nodes.empty()) return;

  out->geometry.push_back(graph_.location(nodes.front()));
  uint32_t leg_name = kNoIndex;
  RoadClass leg_class = RoadClass::kNone;

  for (size_t i = 0; i < steps.size(); ++i) {
    const RoadArc& arc = graph_.arc(steps[i].arc);
    if (steps[i].stored_order) {
      for (uint32_t k = 0; k < arc.geom_count; ++k) out->geometry.push_back(graph_.arc_point(arc, k));
    } else {
      for (uint32_t k = arc.geom_count; k > 0; --k) {
        out->geometry.push_back(graph_.arc_point(arc, k - 1));
      }
    }
    out->geometry.push_back(graph_.location(nodes[i + 1]));

    const double len = static_cast<double>(arc.length_m);
    const double secs = ProfileSeconds(arc, options);
    out->length_m += len;
    out->seconds += secs;

    if (out->legs.empty() || arc.name != leg_name || arc.klass != leg_class) {
      RouteLeg leg;
      leg.name = graph_.name(arc.name);
      leg.klass = RoadClassName(arc.klass);
      out->legs.push_back(leg);
      leg_name = arc.name;
      leg_class = arc.klass;
    }
    out->legs.back().length_m += len;
    out->legs.back().seconds += secs;
  }
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

bool Router::Search(uint32_t s, uint32_t t, const RouteOptions& o, const LegSeed& seed,
                    std::vector<Step>* steps, std::vector<uint32_t>* nodes,
                    int64_t* expanded) const {
  return o.bidirectional ? SearchBidirectional(s, t, o, seed, steps, nodes, expanded)
                         : SearchUnidirectional(s, t, o, seed, steps, nodes, expanded);
}

// The arc of the route's final node that faces back along the route. That is
// what the next leg inherits: a restriction at the stop names its `from` arc
// from the stop's own side, and so does a U-turn.
//
// It is a lookup by node pair and NOT the last step's own arc, even though a
// step walked against stored order already IS an arc of the node being entered
// and would save the search. Where two nodes are joined by more than one arc
// the two differ, and `ArcBetween` — first arc to that node — is the rule both
// searches already label a state by. Taking the exact arc here instead made
// the seed depend on which search ran, and the two disagreed on Kiawah by 50
// seconds over the same node sequence: same road, the other carriageway.
//
// NOTE so a parallel-arc pair is approximated, and deliberately: one rule for
// what "the arc I arrived along" means, used everywhere, is worth more than a
// sharper answer in one of the three places that ask.
uint32_t Router::ArrivalArc(const std::vector<Step>& steps,
                            const std::vector<uint32_t>& nodes) const {
  if (steps.empty() || nodes.size() < 2) return kNoArc;
  return graph_.ArcBetween(nodes.back(), nodes[nodes.size() - 2]);
}

Status Router::RouteNodes(uint32_t from_node, uint32_t to_node, const RouteOptions& options,
                          routing::Route* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "Router: null out");
  *out = routing::Route{};
  const uint32_t n = graph_.node_count();
  if (n == 0) return Status::Error(kInvalidArg, "Router: empty graph");
  if (from_node >= n || to_node >= n) {
    return Status::Error(kInvalidArg, "Router: node index out of range");
  }
  out->start_node = from_node;
  out->end_node = to_node;

  if (from_node == to_node) {
    out->found = true;
    out->nodes.push_back(from_node);
    out->geometry.push_back(graph_.location(from_node));
    return Status::Ok();
  }

  std::vector<Step> steps;
  std::vector<uint32_t> nodes;
  if (!Search(from_node, to_node, options, LegSeed{}, &steps, &nodes, &out->nodes_expanded)) {
    return Status::Ok();  // disconnected is an answer, not an error
  }
  Materialize(steps, nodes, options, out);
  out->start_node = from_node;
  out->end_node = to_node;
  return Status::Ok();
}

Status Router::RouteNodesVia(const std::vector<uint32_t>& stops, const RouteOptions& options,
                             routing::Route* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "Router: null out");
  *out = routing::Route{};
  if (stops.size() < 2) return Status::Error(kInvalidArg, "Router: a route needs two stops");
  const uint32_t n = graph_.node_count();
  if (n == 0) return Status::Error(kInvalidArg, "Router: empty graph");
  for (uint32_t stop : stops) {
    if (stop >= n) return Status::Error(kInvalidArg, "Router: node index out of range");
  }

  out->stop_nodes = stops;
  out->start_node = stops.front();
  out->end_node = stops.back();

  // One steps/nodes pair for the WHOLE route, so Materialize sees it as the
  // single line it is — which is also why a leg boundary in the middle of a
  // road does not split the road's leg summary in two.
  std::vector<Step> steps;
  std::vector<uint32_t> nodes{stops.front()};
  std::vector<size_t> stop_at{0};  // index into `nodes` for each stop

  LegSeed seed;
  for (size_t leg = 0; leg + 1 < stops.size(); ++leg) {
    const uint32_t a = stops[leg], b = stops[leg + 1];
    if (a == b) {  // two stops on the same node: a legal zero-length leg
      stop_at.push_back(nodes.size() - 1);
      continue;
    }

    std::vector<Step> leg_steps;
    std::vector<uint32_t> leg_nodes;
    bool found = Search(a, b, options, seed, &leg_steps, &leg_nodes, &out->nodes_expanded);
    if (!found && seed.banned_arc != kNoArc) {
      // The stop had no other way out — a cul-de-sac, or a one-way pair whose
      // only exit is the way in. The U-turn is a preference, so it yields to
      // there being no route at all, and the stop is reported.
      LegSeed relaxed = seed;
      relaxed.banned_arc = kNoArc;
      found = Search(a, b, options, relaxed, &leg_steps, &leg_nodes, &out->nodes_expanded);
      if (found) out->u_turn_stops.push_back(static_cast<uint32_t>(leg));
    }
    if (!found) {
      const int64_t expanded = out->nodes_expanded;  // what the attempt cost
      *out = routing::Route{};
      out->nodes_expanded = expanded;
      out->stop_nodes = stops;
      out->start_node = stops.front();
      out->end_node = stops.back();
      out->unreachable_leg = static_cast<uint32_t>(leg);
      return Status::Ok();  // still an answer, and it says which pair
    }

    // leg_nodes[0] is `a`, which is already the tail of `nodes`.
    steps.insert(steps.end(), leg_steps.begin(), leg_steps.end());
    nodes.insert(nodes.end(), leg_nodes.begin() + 1, leg_nodes.end());
    stop_at.push_back(nodes.size() - 1);

    const uint32_t arrival = ArrivalArc(leg_steps, leg_nodes);
    seed.entry_arc = arrival;
    seed.banned_arc = options.allow_u_turn_at_stops ? kNoArc : arrival;
  }

  Materialize(steps, nodes, options, out);

  // Materialize emits one geometry point per node plus that arc's shape
  // points, so a stop's place in `geometry` has to be counted as the line is
  // walked, not derived from its index in `nodes`.
  out->stop_geometry_index.assign(stop_at.size(), 0);
  size_t g = 0, next = 0;
  while (next < stop_at.size() && stop_at[next] == 0) out->stop_geometry_index[next++] = 0;
  for (size_t i = 0; i < steps.size(); ++i) {
    g += graph_.arc(steps[i].arc).geom_count + 1;
    while (next < stop_at.size() && stop_at[next] == i + 1) {
      out->stop_geometry_index[next++] = static_cast<uint32_t>(g);
    }
  }
  return Status::Ok();
}

Status Router::Route(const GeoPoint& from, const GeoPoint& to, const RouteOptions& options,
                     routing::Route* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "Router: null out");
  *out = routing::Route{};

  uint32_t s = 0, t = 0;
  double s_off = 0.0, t_off = 0.0;
  if (!graph_.NearestNode(from, options.snap_meters, &s, &s_off)) {
    return Status::Error(kOutOfCoverage, "no road within snap_meters of the start point");
  }
  if (!graph_.NearestNode(to, options.snap_meters, &t, &t_off)) {
    return Status::Error(kOutOfCoverage, "no road within snap_meters of the end point");
  }

  Status status = RouteNodes(s, t, options, out);
  out->start_offset_m = s_off;
  out->end_offset_m = t_off;
  return status;
}

Status Router::RouteVia(const std::vector<GeoPoint>& stops, const RouteOptions& options,
                        routing::Route* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "Router: null out");
  *out = routing::Route{};
  if (stops.size() < 2) return Status::Error(kInvalidArg, "Router: a route needs two stops");

  // Every stop is snapped before anything is routed: a stop nowhere near a
  // road makes the request wrong, and finding that out after three legs have
  // been searched wastes the work and reports it as the wrong stop's problem.
  std::vector<uint32_t> nodes(stops.size());
  std::vector<double> offsets(stops.size());
  for (size_t i = 0; i < stops.size(); ++i) {
    if (!graph_.NearestNode(stops[i], options.snap_meters, &nodes[i], &offsets[i])) {
      return Status::Error(kOutOfCoverage, "no road within snap_meters of stop " +
                                               std::to_string(i) + " of " +
                                               std::to_string(stops.size()));
    }
  }

  Status status = RouteNodesVia(nodes, options, out);
  out->stop_offsets_m = offsets;
  out->start_offset_m = offsets.front();
  out->end_offset_m = offsets.back();
  return status;
}

}  // namespace routing
}  // namespace fv
