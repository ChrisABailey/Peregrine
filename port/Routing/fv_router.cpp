// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

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

  // `extra_split_nodes` are nodes a LEG needs to remember an arc at even
  // though no restriction names them: the stop it starts from (O5d), and
  // since §1d each end of a road a mid-arc endpoint sets off along or arrives
  // from. There are at most a handful, which is why they are a small vector
  // scanned linearly rather than another map.
  StateSpace(const RoadGraph& graph, bool enabled,
             const std::vector<uint32_t>& extra_split_nodes, const BannedTurn& banned)
      : g_(graph),
        on_(enabled && graph.has_restrictions()),
        count_(on_ ? graph.state_count() : graph.node_count()),
        banned_(banned) {
    for (uint32_t node : extra_split_nodes) {
      if (node == kNoArc || node >= g_.node_count()) continue;
      // Only worth a block of its own if the node is not already split — if it
      // is, it already remembers exactly the arc the leg needs it to.
      if (on_ && g_.state_base(node) != kNoArc) continue;
      bool already = false;
      for (const Extra& e : extra_) already = already || e.node == node;
      if (already) continue;
      Extra e;
      e.node = node;
      e.base = count_;
      e.degree = Degree(node);
      extra_.push_back(e);
      if (extra_base_ == kNoIndex || e.base < extra_base_) extra_base_ = e.base;
      count_ += e.degree + 1;
    }
  }

  uint32_t count() const { return count_; }

  bool split(uint32_t node) const {
    for (const Extra& e : extra_) if (e.node == node) return true;
    return on_ && g_.state_base(node) != kNoArc;
  }

  uint32_t NodeOf(uint32_t state) const {
    if (state >= extra_base_) {
      for (const Extra& e : extra_)
        if (state >= e.base && state <= e.base + e.degree) return e.node;
    }
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
    for (const Extra& e : extra_) if (e.node == node) return e.base;
    return on_ ? g_.state_base(node) : kNoArc;
  }

  struct Extra {
    uint32_t node = kNoArc;
    uint32_t base = kNoIndex;
    uint32_t degree = 0;
  };

  const RoadGraph& g_;
  bool on_;
  uint32_t count_;
  BannedTurn banned_;
  std::vector<Extra> extra_;
  // The lowest extra base, above every ordinary state, so `state >=
  // extra_base_` rules the whole set out in one comparison. kNoIndex when
  // there is no extra block, which makes that comparison false for every
  // real state.
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
  return routing::ArcUsable(arc, options);
}

bool ArcUsable(const RoadArc& arc, const RouteOptions& options) {
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

// The state space a leg searches in: ordinary for a plain route, and with a
// node split wherever a terminal needs it to remember an arc — the stop a leg
// starts from (O5d) and, since §1d, each end of a road a mid-arc terminal sets
// off along or arrives from.
StateSpace SpaceFor(const RoadGraph& graph, bool restrictions, std::vector<uint32_t> extra,
                    uint32_t banned_via, uint32_t banned_from, uint32_t banned_to) {
  StateSpace::BannedTurn ban;
  if (banned_to != kNoArc) {
    // A U-turn at a JUNCTION stop is one turn: in along the arc, out along the
    // same arc. Barring it needs the entry arc, so a ban without one is not
    // expressible — and never asked for, since both come from the same
    // previous leg. (A U-turn at a MID-ROAD stop is not a turn at all and is
    // barred by dropping a source; see LegSeed::banned_toward.)
    ban.via = banned_via;
    ban.from_arc = banned_from;
    ban.to_arc = banned_to;
    extra.push_back(banned_via);
  }
  return StateSpace(graph, restrictions, extra, ban);
}

// The nodes a leg's terminals need to remember an arc at. Templated only
// because Router::Terminal is private and this file's helpers are not friends
// of it; the body is the same scan either way.
template <typename Terminals>
std::vector<uint32_t> ExtraSplits(const Terminals& sources, const Terminals& targets) {
  std::vector<uint32_t> extra;
  for (const auto& t : sources)
    if (t.arc != kNoArc) extra.push_back(t.node);
  for (const auto& t : targets)
    if (t.arc != kNoArc) extra.push_back(t.node);
  return extra;
}

// Whether `state` is one of the seeded terminals, and which. The sets have at
// most two entries each, so this is a scan.
uint32_t TerminalAt(const std::vector<uint32_t>& states, uint32_t state) {
  for (uint32_t i = 0; i < states.size(); ++i)
    if (states[i] == state) return i;
  return kNoIndex;
}

}  // namespace

// The partial traversal a mid-arc terminal contributes, and the state it seeds.
// A node anchor gives one terminal that pays nothing and stands at the node
// itself; a mid-arc anchor gives one per end of the road it may leave along.
void Router::SourcesFor(const RouteAnchor& a, const RouteOptions& o, const LegSeed& seed,
                        std::vector<Terminal>* out) const {
  out->clear();
  if (!a.mid_arc()) {
    Terminal t;
    t.node = a.node;
    t.arc = seed.entry_arc;
    out->push_back(t);
    return;
  }
  const RoadArc& arc = graph_.arc(a.arc);
  if (!ArcUsable(arc, o)) return;
  const double cost = ArcCost(arc, o);
  const uint32_t u = a.node, v = a.to;

  // Setting off toward the target end travels the arc with the grain, which is
  // what kArcForward permits; toward the source end is kArcBackward. The flags
  // are mirrored onto the twin arc, so which end the graph happens to store
  // this road at never enters into it.
  if (arc.forward() && seed.banned_toward != v) {
    Terminal t;
    t.node = v;
    // Arriving at v along this road is, from v's side, the arc pointing back
    // at u — the arc a restriction at v names as its `from`, and the same one
    // the search itself would have labelled the state with.
    t.arc = graph_.ArcBetween(v, u);
    t.cost = (1.0 - a.t) * cost;
    t.partial = true;
    t.step.arc = a.arc;
    t.step.t0 = a.t;
    t.step.t1 = 1.0;
    out->push_back(t);
  }
  if (arc.backward() && seed.banned_toward != u) {
    Terminal t;
    t.node = u;
    t.arc = graph_.ArcBetween(u, v);
    t.cost = a.t * cost;
    t.partial = true;
    t.step.arc = a.arc;
    t.step.t0 = a.t;
    t.step.t1 = 0.0;
    out->push_back(t);
  }
}

void Router::TargetsFor(const RouteAnchor& a, const RouteOptions& o,
                        std::vector<Terminal>* out) const {
  out->clear();
  if (!a.mid_arc()) {
    Terminal t;
    t.node = a.node;
    out->push_back(t);
    return;
  }
  const RoadArc& arc = graph_.arc(a.arc);
  if (!ArcUsable(arc, o)) return;
  const double cost = ArcCost(arc, o);
  const uint32_t u = a.node, v = a.to;

  // Mirror of SourcesFor: reaching the point FROM u travels with the grain, so
  // it is kArcForward that permits it. The state remembers the arc it LEAVES
  // along, which is how the reverse frontier labels a junction.
  if (arc.forward()) {
    Terminal t;
    t.node = u;
    t.arc = graph_.ArcBetween(u, v);
    t.cost = a.t * cost;
    t.partial = true;
    t.step.arc = a.arc;
    t.step.t0 = 0.0;
    t.step.t1 = a.t;
    out->push_back(t);
  }
  if (arc.backward()) {
    Terminal t;
    t.node = v;
    t.arc = graph_.ArcBetween(v, u);
    t.cost = (1.0 - a.t) * cost;
    t.partial = true;
    t.step.arc = a.arc;
    t.step.t0 = 1.0;
    t.step.t1 = a.t;
    out->push_back(t);
  }
}

bool Router::SearchUnidirectional(const std::vector<Terminal>& sources,
                                  const std::vector<Terminal>& targets, const RouteOptions& o,
                                  const LegSeed& seed, std::vector<Step>* steps,
                                  uint32_t* source_used, uint32_t* target_used,
                                  int64_t* expanded) const {
  if (sources.empty() || targets.empty()) return false;
  const StateSpace space =
      SpaceFor(graph_, UseTurnRestrictions(o), ExtraSplits(sources, targets),
               sources.front().node, seed.entry_arc, seed.banned_arc);
  const uint32_t n = space.count();
  // Scratch is per-query rather than a member: Router is const and shared,
  // and a full reset of two arrays costs a couple of milliseconds even on a
  // state-sized graph — well under the search itself.
  std::vector<double> dist(n, kInf);
  std::vector<uint32_t> parent_arc(n, kNoIndex);
  std::vector<uint32_t> parent_state(n, kNoIndex);
  std::vector<uint8_t> settled(n, 0);

  MinQueue queue;
  std::vector<uint32_t> source_states(sources.size(), kNoIndex);
  for (uint32_t i = 0; i < sources.size(); ++i) {
    const uint32_t st = space.StateOf(sources[i].node, sources[i].arc);
    source_states[i] = st;
    if (sources[i].cost < dist[st]) {
      dist[st] = sources[i].cost;
      queue.push(QueueEntry{sources[i].cost, st});
    }
  }

  // The cheapest state settled at a target is NOT the answer when the targets
  // carry different arrival costs — a near end of the road settles first and
  // then charges for the whole of it. So the best total is tracked and the
  // loop runs until no unsettled state could still beat it, which is the same
  // rule a super-sink joined by cost-weighted edges would give.
  uint32_t goal = kNoIndex;
  uint32_t goal_target = kNoIndex;
  double best = kInf;
  while (!queue.empty()) {
    const QueueEntry top = queue.top();
    queue.pop();
    if (settled[top.state]) continue;  // stale entry: this queue is lazy-deleted
    if (top.key >= best) break;
    settled[top.state] = 1;
    ++*expanded;
    const uint32_t u = space.NodeOf(top.state);
    const uint32_t arrival = space.ArcOf(top.state, u);

    for (uint32_t i = 0; i < targets.size(); ++i) {
      if (targets[i].node != u) continue;
      // Leaving u along the target's own arc is a turn like any other, and at
      // a restricted junction the sign has the last word over it too.
      if (!space.Allowed(u, arrival, targets[i].arc)) continue;
      const double total = top.key + targets[i].cost;
      if (total < best) {
        best = total;
        goal = top.state;
        goal_target = i;
      }
    }

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

  std::vector<Step> rev_steps;
  uint32_t source = kNoIndex;
  for (uint32_t x = goal;;) {
    source = TerminalAt(source_states, x);
    if (source != kNoIndex) break;
    if (parent_state[x] == kNoIndex) return false;
    rev_steps.push_back(Step::Whole(parent_arc[x], true));
    x = parent_state[x];
  }
  std::reverse(rev_steps.begin(), rev_steps.end());
  *steps = std::move(rev_steps);
  *source_used = source;
  *target_used = goal_target;
  return true;
}

// ---------------------------------------------------------------------------
// Bidirectional Dijkstra
// ---------------------------------------------------------------------------

bool Router::SearchBidirectional(const std::vector<Terminal>& sources,
                                 const std::vector<Terminal>& targets, const RouteOptions& o,
                                 const LegSeed& seed, std::vector<Step>* steps,
                                 uint32_t* source_used, uint32_t* target_used,
                                 int64_t* expanded) const {
  if (sources.empty() || targets.empty()) return false;
  const StateSpace space =
      SpaceFor(graph_, UseTurnRestrictions(o), ExtraSplits(sources, targets),
               sources.front().node, seed.entry_arc, seed.banned_arc);
  const uint32_t n = space.count();
  std::vector<double> df(n, kInf), dr(n, kInf);
  std::vector<uint32_t> fparent_arc(n, kNoIndex), fparent_state(n, kNoIndex);
  std::vector<uint32_t> rparent_arc(n, kNoIndex), rparent_state(n, kNoIndex);
  std::vector<uint8_t> fsettled(n, 0), rsettled(n, 0);

  // Both frontiers start from a SET, each member already holding whatever
  // reaching it cost — nothing else about either search changes. A terminal's
  // cost is a constant added to one side's distances, which is exactly what a
  // super-source joined by weighted edges would contribute, so the stopping
  // rule below is the one it always was.
  MinQueue fq, rq;
  std::vector<uint32_t> source_states(sources.size(), kNoIndex);
  std::vector<uint32_t> target_states(targets.size(), kNoIndex);
  for (uint32_t i = 0; i < sources.size(); ++i) {
    const uint32_t st = space.StateOf(sources[i].node, sources[i].arc);
    source_states[i] = st;
    if (sources[i].cost < df[st]) {
      df[st] = sources[i].cost;
      fq.push(QueueEntry{sources[i].cost, st});
    }
  }
  for (uint32_t i = 0; i < targets.size(); ++i) {
    const uint32_t st = space.StateOf(targets[i].node, targets[i].arc);
    target_states[i] = st;
    if (targets[i].cost < dr[st]) {
      dr[st] = targets[i].cost;
      rq.push(QueueEntry{targets[i].cost, st});
    }
  }

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

  // A seeded state can be the whole meeting: a mid-arc start whose road ends
  // at the destination junction is a route with no search in it at all, and
  // neither frontier would ever relax an arc to notice.
  for (uint32_t st : source_states) ConsiderMeeting(true, space.NodeOf(st), st, df[st]);

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

  std::vector<Step> path_steps;

  // source -> meeting, walked backwards through the forward parents.
  std::vector<Step> head_steps;
  uint32_t source = kNoIndex;
  for (uint32_t x = meet_f;;) {
    source = TerminalAt(source_states, x);
    if (source != kNoIndex) break;
    if (fparent_state[x] == kNoIndex) return false;  // defensive: no forward tree
    head_steps.push_back(Step::Whole(fparent_arc[x], true));
    x = fparent_state[x];
  }
  std::reverse(head_steps.begin(), head_steps.end());
  path_steps = std::move(head_steps);

  // meeting -> target. The reverse tree found u from v across an arc that
  // lives in v's adjacency, so travelling u -> v walks that arc against
  // stored order.
  uint32_t target = kNoIndex;
  for (uint32_t x = meet_r;;) {
    target = TerminalAt(target_states, x);
    if (target != kNoIndex) break;
    const uint32_t px = rparent_state[x];
    if (px == kNoIndex) return false;
    path_steps.push_back(Step::Whole(rparent_arc[x], false));
    x = px;
  }

  *steps = std::move(path_steps);
  *source_used = source;
  *target_used = target;
  return true;
}

// ---------------------------------------------------------------------------
// Materialization
// ---------------------------------------------------------------------------

void Router::ArcShapeAndLengths(uint32_t arc, std::vector<GeoPoint>* shape,
                                std::vector<double>* cum) const {
  shape->clear();
  cum->clear();
  graph_.ArcShape(arc, shape);
  cum->reserve(shape->size());
  double walked = 0.0;
  for (size_t i = 0; i < shape->size(); ++i) {
    if (i > 0) {
      walked += GreatCircleMeters((*shape)[i - 1].lat, (*shape)[i - 1].lon, (*shape)[i].lat,
                                  (*shape)[i].lon);
    }
    cum->push_back(walked);
  }
}

namespace {

// The point `d` metres along a shape whose cumulative lengths are `cum`.
GeoPoint PointAlong(const std::vector<GeoPoint>& shape, const std::vector<double>& cum,
                    double d) {
  if (shape.empty()) return GeoPoint{};
  if (d <= 0.0 || cum.back() <= 0.0) return shape.front();
  if (d >= cum.back()) return shape.back();
  size_t i = 1;
  while (i + 1 < cum.size() && cum[i] < d) ++i;
  const double span = cum[i] - cum[i - 1];
  const double f = span > 0.0 ? (d - cum[i - 1]) / span : 0.0;
  // Straight in lat/lon, which is what the shape point pair itself is: the
  // road between two consecutive points is drawn as that chord everywhere
  // else, so interpolating along a great circle here would put the snapped
  // point off the line the map draws.
  return GeoPoint{shape[i - 1].lat + f * (shape[i].lat - shape[i - 1].lat),
                  shape[i - 1].lon + f * (shape[i].lon - shape[i - 1].lon)};
}

}  // namespace

uint32_t Router::StepEndNode(const Step& step) const {
  if (step.t1 >= 1.0) return graph_.arc(step.arc).target;
  if (step.t1 <= 0.0) return graph_.ArcSource(step.arc);
  return kNoArc;
}

uint32_t Router::StepStartNode(const Step& step) const {
  if (step.t0 >= 1.0) return graph_.arc(step.arc).target;
  if (step.t0 <= 0.0) return graph_.ArcSource(step.arc);
  return kNoArc;
}

void Router::Materialize(const std::vector<Step>& steps, const RouteOptions& options,
                         routing::Route* out, std::vector<uint32_t>* geom_at_step) const {
  out->found = true;
  out->nodes.clear();
  out->geometry.clear();
  out->legs.clear();
  out->length_m = 0.0;
  out->seconds = 0.0;
  if (geom_at_step != nullptr) geom_at_step->clear();
  if (steps.empty()) return;

  uint32_t leg_name = kNoIndex;
  RoadClass leg_class = RoadClass::kNone;
  std::vector<GeoPoint> shape;
  std::vector<double> cum;

  {
    const uint32_t first = StepStartNode(steps.front());
    if (first != kNoArc) out->nodes.push_back(first);
  }

  for (size_t i = 0; i < steps.size(); ++i) {
    const Step& step = steps[i];
    const RoadArc& arc = graph_.arc(step.arc);
    ArcShapeAndLengths(step.arc, &shape, &cum);
    const double total = cum.empty() ? 0.0 : cum.back();
    const double d0 = step.t0 * total, d1 = step.t1 * total;

    if (i == 0) out->geometry.push_back(PointAlong(shape, cum, d0));
    // The shape points strictly between the two cuts, in travel order. A whole
    // arc walked with the grain emits every interior point, which is what
    // Materialize always did; a partial one emits the part it covers, and the
    // interpolated ends are what close it.
    if (step.stored_order()) {
      for (size_t k = 1; k + 1 < shape.size(); ++k)
        if (cum[k] > d0 && cum[k] < d1) out->geometry.push_back(shape[k]);
    } else {
      for (size_t k = shape.size() - 1; k > 0; --k)
        if (cum[k - 1] < d0 && cum[k - 1] > d1) out->geometry.push_back(shape[k - 1]);
    }
    out->geometry.push_back(PointAlong(shape, cum, d1));
    if (geom_at_step != nullptr)
      geom_at_step->push_back(static_cast<uint32_t>(out->geometry.size()) - 1);

    const uint32_t end_node = StepEndNode(step);
    if (end_node != kNoArc) out->nodes.push_back(end_node);

    // A partial traversal costs its fraction of the arc, in both the metre and
    // the clock. `RoadArc::length_m` and not the shape length walked here: the
    // two agree to a rounding, and the arc's own number is what every other
    // total in this file is built from.
    const double f = step.fraction();
    const double len = f * static_cast<double>(arc.length_m);
    const double secs = f * ProfileSeconds(arc, options);
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

bool Router::Search(const std::vector<Terminal>& sources, const std::vector<Terminal>& targets,
                    const RouteOptions& o, const LegSeed& seed, std::vector<Step>* steps,
                    uint32_t* source_used, uint32_t* target_used, int64_t* expanded) const {
  return o.bidirectional ? SearchBidirectional(sources, targets, o, seed, steps, source_used,
                                               target_used, expanded)
                         : SearchUnidirectional(sources, targets, o, seed, steps, source_used,
                                                target_used, expanded);
}

// One leg end to end: the search between the two anchors' terminals, with each
// terminal's own partial traversal stitched on.
bool Router::RouteLegBetween(const RouteAnchor& from, const RouteAnchor& to,
                             const RouteOptions& o, const LegSeed& seed,
                             std::vector<Step>* steps, int64_t* expanded) const {
  steps->clear();

  // BOTH ENDS ON THE SAME ROAD, and no search can find it: the path never
  // reaches a junction, so there is no node for a frontier to settle. It is
  // also unconditionally optimal when it is legal — every alternative has to
  // leave the road at one end and come back in, which pays for more of this
  // same road plus whatever lies between, and every cost here is non-negative.
  if (from.mid_arc() && to.mid_arc() && from.arc == to.arc) {
    const RoadArc& arc = graph_.arc(from.arc);
    const bool with_grain = to.t >= from.t;
    const bool legal = ArcUsable(arc, o) && (with_grain ? arc.forward() : arc.backward());
    // A U-turn ban at a mid-road stop names the end the last leg came FROM,
    // and staying on the road heads away from it — so this is never the
    // U-turn the ban is about, whichever way along the road it runs.
    if (legal && from.t != to.t) {
      Step step;
      step.arc = from.arc;
      step.t0 = from.t;
      step.t1 = to.t;
      steps->push_back(step);
      return true;
    }
    if (legal && from.t == to.t) return true;  // a zero-length leg
  }

  std::vector<Terminal> sources, targets;
  SourcesFor(from, o, seed, &sources);
  TargetsFor(to, o, &targets);
  if (sources.empty() || targets.empty()) return false;

  std::vector<Step> found;
  uint32_t source_used = kNoIndex, target_used = kNoIndex;
  if (!Search(sources, targets, o, seed, &found, &source_used, &target_used, expanded)) {
    return false;
  }

  // A partial traversal of nothing is not a step. ArcSnap's end tolerance
  // means one should never arise; a zero-length leg in the middle of a route
  // would be a phantom road name in the leg list if one did.
  if (sources[source_used].partial && sources[source_used].step.fraction() > 0.0)
    steps->push_back(sources[source_used].step);
  steps->insert(steps->end(), found.begin(), found.end());
  if (targets[target_used].partial && targets[target_used].step.fraction() > 0.0)
    steps->push_back(targets[target_used].step);
  return true;
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
//
// kNoArc when the leg ended MID-ROAD, where there is no junction and so no
// turn to inherit; the U-turn there is barred by LegSeed::banned_toward.
uint32_t Router::ArrivalArc(const std::vector<Step>& steps) const {
  if (steps.empty()) return kNoArc;
  const Step& last = steps.back();
  const uint32_t arrived_at = StepEndNode(last);
  if (arrived_at == kNoArc) return kNoArc;
  const uint32_t came_from = StepStartNode(last);
  if (came_from == kNoArc || came_from == arrived_at) return kNoArc;
  return graph_.ArcBetween(arrived_at, came_from);
}

Status Router::RouteAnchorsVia(const std::vector<RouteAnchor>& stops,
                               const RouteOptions& options, routing::Route* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "Router: null out");
  *out = routing::Route{};
  if (stops.size() < 2) return Status::Error(kInvalidArg, "Router: a route needs two stops");
  const uint32_t n = graph_.node_count();
  if (n == 0) return Status::Error(kInvalidArg, "Router: empty graph");
  for (const RouteAnchor& a : stops) {
    if (a.node >= n) return Status::Error(kInvalidArg, "Router: node index out of range");
    if (a.mid_arc() && (a.arc >= graph_.arc_count() || a.to >= n))
      return Status::Error(kInvalidArg, "Router: arc index out of range");
  }

  auto report_stops = [&]() {
    out->stop_nodes.clear();
    out->stop_offsets_m.clear();
    out->stop_points.clear();
    for (const RouteAnchor& a : stops) {
      out->stop_nodes.push_back(a.mid_arc() ? kNoArc : a.node);
      out->stop_points.push_back(a.point);
    }
    out->start_arc = stops.front().arc;
    out->end_arc = stops.back().arc;
    out->start_point = stops.front().point;
    out->end_point = stops.back().point;
    // A placeholder until the route is materialized and the real first and
    // last junctions are known — and the final answer for a route that never
    // reaches one, where the anchor's own source node is all there is.
    out->start_node = stops.front().node;
    out->end_node = stops.back().node;
  };
  report_stops();

  // One steps list for the WHOLE route, so Materialize sees it as the single
  // line it is — which is also why a leg boundary in the middle of a road does
  // not split the road's leg summary in two.
  std::vector<Step> steps;
  std::vector<size_t> stop_at_step{0};  // steps index each stop follows, 1-based

  LegSeed seed;
  for (size_t leg = 0; leg + 1 < stops.size(); ++leg) {
    std::vector<Step> leg_steps;
    bool found =
        RouteLegBetween(stops[leg], stops[leg + 1], options, seed, &leg_steps, &out->nodes_expanded);
    if (!found && (seed.banned_arc != kNoArc || seed.banned_toward != kNoArc)) {
      // The stop had no other way out — a cul-de-sac, or a one-way pair whose
      // only exit is the way in. The U-turn is a preference, so it yields to
      // there being no route at all, and the stop is reported.
      LegSeed relaxed = seed;
      relaxed.banned_arc = kNoArc;
      relaxed.banned_toward = kNoArc;
      found = RouteLegBetween(stops[leg], stops[leg + 1], options, relaxed, &leg_steps,
                              &out->nodes_expanded);
      if (found) out->u_turn_stops.push_back(static_cast<uint32_t>(leg));
    }
    if (!found) {
      const int64_t expanded = out->nodes_expanded;  // what the attempt cost
      *out = routing::Route{};
      out->nodes_expanded = expanded;
      report_stops();
      out->unreachable_leg = static_cast<uint32_t>(leg);
      return Status::Ok();  // still an answer, and it says which pair
    }

    steps.insert(steps.end(), leg_steps.begin(), leg_steps.end());
    stop_at_step.push_back(steps.size());

    // What the NEXT leg inherits. A junction stop hands over a turn; a
    // mid-road stop hands over the end of the road the route came from, and
    // the two are exclusive because a stop is one or the other.
    //
    // A ZERO-LENGTH LEG HANDS ON WHAT IT WAS GIVEN. Two stops in the same
    // place (a user double-clicked) is a legal request that travels nothing,
    // and it must not wipe the arc the leg BEFORE it arrived along — the stop
    // after it is still approached the same way.
    if (leg_steps.empty()) continue;
    seed = LegSeed{};
    if (stops[leg + 1].mid_arc()) {
      if (!options.allow_u_turn_at_stops) {
        // The end this leg approached the stop from: the far end of the last
        // step, read in that step's own direction of travel.
        const Step& last = leg_steps.back();
        seed.banned_toward = StepStartNode(last);
        if (seed.banned_toward == kNoArc) {
          // The last step began mid-road too, which happens only when the
          // whole leg stayed on one road; then the end behind us is the one
          // the traversal ran away from.
          seed.banned_toward =
              last.stored_order() ? graph_.ArcSource(last.arc) : graph_.arc(last.arc).target;
        }
      }
    } else {
      const uint32_t arrival = ArrivalArc(leg_steps);
      seed.entry_arc = arrival;
      seed.banned_arc = options.allow_u_turn_at_stops ? kNoArc : arrival;
    }
  }

  std::vector<uint32_t> geom_at_step;
  Materialize(steps, options, out, &geom_at_step);
  if (out->geometry.empty()) {
    // A route that travels nothing — every stop in the same place. One point,
    // so `geometry.front()` is still where the route is and a caller drawing
    // the line has something to draw.
    out->geometry.push_back(stops.front().point);
  }

  // Where each stop falls on the drawn line. Counted from what Materialize
  // actually emitted rather than derived from the node list, because a partial
  // arc contributes a different number of points than a whole one.
  out->stop_geometry_index.assign(stop_at_step.size(), 0);
  for (size_t i = 0; i < stop_at_step.size(); ++i) {
    const size_t after = stop_at_step[i];
    out->stop_geometry_index[i] =
        after == 0 ? 0u : geom_at_step[after - 1];
  }

  // The first and last JUNCTION the route reaches. A route that never leaves
  // one road touches none, and both stay the road's own source node — see the
  // note on Route::start_node.
  if (!out->nodes.empty()) {
    out->start_node = out->nodes.front();
    out->end_node = out->nodes.back();
  }
  return Status::Ok();
}

Status Router::RouteNodesVia(const std::vector<uint32_t>& stops, const RouteOptions& options,
                             routing::Route* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "Router: null out");
  if (stops.size() < 2) {
    *out = routing::Route{};
    return Status::Error(kInvalidArg, "Router: a route needs two stops");
  }
  const uint32_t n = graph_.node_count();
  std::vector<RouteAnchor> anchors;
  anchors.reserve(stops.size());
  for (uint32_t stop : stops) {
    if (stop >= n) {
      *out = routing::Route{};
      if (n == 0) return Status::Error(kInvalidArg, "Router: empty graph");
      return Status::Error(kInvalidArg, "Router: node index out of range");
    }
    anchors.push_back(RouteAnchor::OnNode(stop, graph_.location(stop)));
  }
  return RouteAnchorsVia(anchors, options, out);
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

  if (from_node == to_node) {
    out->found = true;
    out->start_node = out->end_node = from_node;
    out->start_point = out->end_point = graph_.location(from_node);
    out->nodes.push_back(from_node);
    out->geometry.push_back(graph_.location(from_node));
    return Status::Ok();
  }

  const Status status = RouteNodesVia({from_node, to_node}, options, out);
  // A two-point route has no stops to report — Route says so — and no leg to
  // name when it fails, because there is only the one.
  out->stop_nodes.clear();
  out->stop_offsets_m.clear();
  out->stop_points.clear();
  out->stop_geometry_index.clear();
  out->unreachable_leg = routing::Route::kNoLeg;
  out->start_node = from_node;
  out->end_node = to_node;
  return status;
}

namespace {

// Snapping that knows what the traveller is. See `RoadGraph::NearestNode`'s
// header note for the defect the NODE filter exists to close: the nearest node
// is chosen on distance alone, and the nearest node to a bicycle on Kiawah was
// repeatedly a golf cart path tagged `bicycle=no`, which made a perfectly
// routable trip report "not connected".
//
// A node is acceptable when at least one arc incident to it is usable by this
// profile. That is the weakest test that means anything — a node whose every
// arc is barred is one the search cannot leave, so snapping to it guarantees
// the failure — and it is deliberately NOT a reachability test: whether the
// far end is reachable is the search's answer to give, not the snap's.
//
// AND IT FALLS BACK. If nothing within `snap_meters` has a usable arc the
// unfiltered nearest node is taken after all, so the caller still gets the
// "snapped N m away" diagnostic and a routing failure that says the two
// points are not connected, rather than a different error about coverage
// that would send whoever reads it looking at the wrong thing.
bool SnapNodeFor(const RoadGraph& graph, const GeoPoint& p, const RouteOptions& options,
                 uint32_t* out_node, double* out_meters) {
  const RoadGraph::NodeFilter usable = [&graph, &options](uint32_t n) {
    for (uint32_t k = graph.arc_begin(n); k < graph.arc_end(n); ++k) {
      if (ArcUsable(graph.arc(k), options)) return true;
    }
    return false;
  };
  if (graph.NearestNode(p, options.snap_meters, out_node, out_meters, usable)) return true;
  return graph.NearestNode(p, options.snap_meters, out_node, out_meters);
}

}  // namespace

bool Router::Snap(const GeoPoint& p, const RouteOptions& options, RouteAnchor* out) const {
  // The road first (§1d), and the arc filter is the router's own ArcUsable —
  // the same question the search will ask of the very first arc it relaxes, so
  // a snap can never hand the search a road it is about to refuse.
  if (options.snap_to_arcs) {
    RoadGraph::ArcSnap snap;
    const RoadGraph::ArcFilter usable = [&options](const RoadArc& a) {
      return routing::ArcUsable(a, options);
    };
    if (graph_.NearestArcPoint(p, options.snap_meters, &snap, usable)) {
      if (out != nullptr) {
        *out = RouteAnchor::OnArc(snap);
        // The offset the caller sees is the distance to the ROAD, which is the
        // number that answers "was this click anywhere near one".
        out->point = snap.point;
      }
      return true;
    }
    // Nothing usable within range. Fall through to the node snap, which has
    // its own unfiltered fallback: the caller still gets a snap offset and a
    // "not connected" answer rather than a coverage error pointing at the
    // wrong thing.
  }
  uint32_t node = 0;
  double meters = 0.0;
  if (!SnapNodeFor(graph_, p, options, &node, &meters)) return false;
  if (out != nullptr) *out = RouteAnchor::OnNode(node, graph_.location(node));
  return true;
}

namespace {

double OffsetOf(const GeoPoint& asked, const RouteAnchor& a) {
  return GreatCircleMeters(asked.lat, asked.lon, a.point.lat, a.point.lon);
}

}  // namespace

Status Router::Route(const GeoPoint& from, const GeoPoint& to, const RouteOptions& options,
                     routing::Route* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "Router: null out");
  *out = routing::Route{};

  RouteAnchor a, b;
  if (!Snap(from, options, &a)) {
    return Status::Error(kOutOfCoverage, "no road within snap_meters of the start point");
  }
  if (!Snap(to, options, &b)) {
    return Status::Error(kOutOfCoverage, "no road within snap_meters of the end point");
  }

  Status status;
  if (!a.mid_arc() && !b.mid_arc()) {
    // Two junctions: exactly the O4 route, including the same-node shortcut.
    status = RouteNodes(a.node, b.node, options, out);
  } else {
    status = RouteAnchorsVia({a, b}, options, out);
    out->stop_nodes.clear();
    out->stop_offsets_m.clear();
    out->stop_points.clear();
    out->stop_geometry_index.clear();
    out->unreachable_leg = routing::Route::kNoLeg;
  }
  out->start_arc = a.arc;
  out->end_arc = b.arc;
  out->start_point = a.point;
  out->end_point = b.point;
  out->start_offset_m = OffsetOf(from, a);
  out->end_offset_m = OffsetOf(to, b);
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
  std::vector<RouteAnchor> anchors(stops.size());
  std::vector<double> offsets(stops.size());
  for (size_t i = 0; i < stops.size(); ++i) {
    if (!Snap(stops[i], options, &anchors[i])) {
      return Status::Error(kOutOfCoverage, "no road within snap_meters of stop " +
                                               std::to_string(i) + " of " +
                                               std::to_string(stops.size()));
    }
    offsets[i] = OffsetOf(stops[i], anchors[i]);
  }

  Status status = RouteAnchorsVia(anchors, options, out);
  out->stop_offsets_m = offsets;
  out->start_offset_m = offsets.front();
  out->end_offset_m = offsets.back();
  return status;
}

}  // namespace routing
}  // namespace fv
