// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/nav/guidance.h — the approach state machine (guidance plan GD2).
//
// Fed GD1's maneuver list and a stream of fixes, it answers what a banner
// needs — which maneuver is next and how far along the route it is — and
// emits the alert events, edge-triggered, once each. Headless: no timer of
// its own, no UI, and every entry point that needs the clock reads it off the
// fix.
//
// FOUR THINGS ARE DECIDED HERE.
//
// 1. THE RINGS ARE TIMES, CLAMPED TO DISTANCES. A cyclist at 25 km/h and a
//    walker want different warnings from the same corner, so the ring is
//    `speed * seconds`; but time alone gives a stopped rider an infinite ring
//    and a fast one a 400 m heads-up on a residential street, so it is
//    clamped at both ends. A rider whose speed is unknown gets the floor.
//
// 2. ONLY THE INNERMOST RING FIRES. Crossing two rings between one fix and
//    the next — which happens whenever guidance starts mid-approach, and at
//    every staggered junction — emits the innermost one and marks the outer
//    ones spent. Two alerts in the same second are not two warnings.
//
// 3. OFF ROUTE IS SUSTAINED, REJOINING IS IMMEDIATE. A single wandering fix
//    must not silence the guidance, so leaving needs `off_route_m` held for
//    `off_route_hold_s`; being back within `rejoin_m` of the line is
//    unambiguous and takes effect at once. Off route the guidance falls
//    SILENT rather than lying, and says so once rather than repeating.
//
// 4. THE PROJECTION IS WINDOWED, AND THAT IS THE DIFFERENCE FROM THE TRIP
//    COMPUTER. `TripComputer::RemainingAlongRoute` takes the nearest segment
//    on the whole route and its own comment names the cost: on an
//    out-and-back the wrong limb can win. A jumpy distance-to-go is
//    tolerable; announcing a turn from the other limb is not. So the search
//    is confined to `search_window_m` either side of the last projection, and
//    only widens to the whole route when the windowed answer is off route
//    anyway — which is what lets a rider who left and rejoined further on be
//    found again.
//
// Distances are metres along the route from its start, matching Maneuver;
// cross-track is metres from the line (contract D2/D4).

#ifndef FVKIT_NAV_GUIDANCE_H_
#define FVKIT_NAV_GUIDANCE_H_

#include <cstddef>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/nav/maneuver.h"
#include "fvkit/nav/position.h"

namespace fv {
namespace nav {

// How near the maneuver an alert is. `kAt` is the junction itself, which is
// the last thing said about it.
enum class GuidanceRing {
  kNone,
  kHeadsUp,
  kActNow,
  kAt,
};

enum class GuidanceEventType {
  kApproach,   // a ring was crossed; `ring` says which
  kPassed,     // the maneuver is behind the rider
  kOffRoute,   // guidance is going silent
  kRejoined,   // ...and is speaking again
  kArrived,    // the destination is reached, and nothing is said after it
};

// One thing that happened on one fix.
struct GuidanceEvent {
  GuidanceEventType type = GuidanceEventType::kApproach;
  GuidanceRing ring = GuidanceRing::kNone;

  // Which maneuver it is about. Meaningless for kOffRoute and kRejoined,
  // which are about the route rather than a corner.
  std::size_t maneuver_index = 0;
  ManeuverType maneuver_type = ManeuverType::kStraight;

  // Along-route metres from the rider to that maneuver when it fired.
  double distance_m = 0.0;

  // The maneuver immediately after this one when it falls inside the same
  // act-now ring — a staggered junction, where "right" alone is the wrong
  // instruction. Set on any kApproach and on no other event: the pairing is a
  // property of the two corners rather than of the ring that is firing, so the
  // heads-up carries it too.
  bool has_then = false;
  std::size_t then_index = 0;
  ManeuverType then_type = ManeuverType::kStraight;
};

// Every number the machine has.
struct GuidanceSettings {
  // Ring 1: the heads-up. 30 s is about 200 m at riding pace.
  double heads_up_s = 30.0;
  double heads_up_min_m = 100.0;
  double heads_up_max_m = 400.0;

  // Ring 2: act now.
  double act_now_s = 8.0;
  double act_now_min_m = 20.0;
  double act_now_max_m = 80.0;

  // Ring 3: at the junction. A time like the other two, and for a reason
  // found on a real ride: a fixed 10 m window is smaller than the 8 m a rider
  // covers between two 1 Hz fixes at 29 km/h, so the ring was stepped over
  // and the alert fired only sometimes. `at_m` is now the floor a stopped
  // rider gets rather than the whole rule.
  double at_s = 1.5;
  double at_m = 10.0;

  // Off route beyond this, held for that long; back on within `rejoin_m`.
  // The gap between the two is the hysteresis and must not be closed.
  double off_route_m = 25.0;
  double off_route_hold_s = 5.0;
  double rejoin_m = 15.0;

  // Arrived within this of the route's end.
  double arrive_m = 15.0;

  // How far either side of the last projection the next one is looked for.
  double search_window_m = 150.0;
};

// What a banner draws. Valid only once a route, a maneuver list and one fix
// have all arrived.
struct GuidanceState {
  bool valid = false;
  bool on_route = true;

  // False once the destination is reached, or when every maneuver is behind
  // the rider. There is nothing more to say and `next_*` mean nothing.
  bool has_next = false;
  std::size_t next_index = 0;
  ManeuverType next_type = ManeuverType::kStraight;
  double distance_to_next_m = 0.0;

  // The staggered-junction pair described on GuidanceEvent.
  bool has_then = false;
  std::size_t then_index = 0;
  ManeuverType then_type = ManeuverType::kStraight;

  // Where the rider is on the line.
  double along_m = 0.0;
  double cross_track_m = 0.0;
  double remaining_m = 0.0;

  // The speed the rings were sized from, whether the fix carried one or it
  // was derived from the previous fix.
  double speed_mps = 0.0;
  bool has_speed = false;
};

// One route's worth of guidance.
//
// The maneuvers must be GD1's over the SAME geometry: their `distance_m` is
// read as along-route metres on the line handed to SetRoute, and a list built
// over a different line would count down to the wrong corner.
class Guidance {
 public:
  Guidance() = default;
  explicit Guidance(const GuidanceSettings& settings) : settings_(settings) {}

  void SetSettings(const GuidanceSettings& settings) { settings_ = settings; }
  const GuidanceSettings& settings() const { return settings_; }

  // Replaces the route and starts over. Fewer than two geometry points, or no
  // maneuvers, is no guidance at all.
  void SetRoute(std::vector<GeoPoint> geometry, std::vector<Maneuver> maneuvers);
  void ClearRoute() { SetRoute({}, {}); }
  bool has_route() const { return geometry_.size() >= 2 && !maneuvers_.empty(); }

  // Back to the start of the route, every alert unspent, the route kept.
  void Reset();

  // Feeds one fix and returns what it caused, in the order it happened. A fix
  // with no position is ignored and returns nothing.
  std::vector<GuidanceEvent> OnFix(const PositionFix& fix);

  const GuidanceState& state() const { return state_; }
  const std::vector<Maneuver>& maneuvers() const { return maneuvers_; }

 private:
  // Along-route position of `p`, searching `window_m` either side of
  // `near_along_m` (a negative window searches the whole route). False when
  // there is no route to project onto.
  bool Project(const GeoPoint& p, double near_along_m, double window_m,
               double* along_m, double* cross_m) const;

  // The ring radii at `speed_mps`, in metres.
  double HeadsUpMeters(double speed_mps) const;
  double ActNowMeters(double speed_mps) const;
  double AtMeters(double speed_mps) const;

  GuidanceSettings settings_;

  std::vector<GeoPoint> geometry_;
  std::vector<double> cumulative_;  // along-route metres at each vertex
  std::vector<Maneuver> maneuvers_;

  // Rings already spent, one bit per GuidanceRing, per maneuver.
  std::vector<unsigned char> fired_;

  GuidanceState state_;

  bool have_fix_ = false;
  GeoPoint last_position_;
  double last_time_s_ = 0.0;
  bool has_last_time_ = false;
  double last_along_m_ = 0.0;

  // When the rider first went beyond off_route_m, for the hold.
  bool leaving_ = false;
  double leaving_since_s_ = 0.0;
  bool has_leaving_time_ = false;

  bool arrived_ = false;
};

// The event's short name, for a test failure or a log line. Not for a user.
const char* GuidanceEventTypeName(GuidanceEventType type);
const char* GuidanceRingName(GuidanceRing ring);

}  // namespace nav
}  // namespace fv

#endif  // FVKIT_NAV_GUIDANCE_H_
