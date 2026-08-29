// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/nav/trip.h — the trip computer (Pippin plan P8).
//
// Five numbers a rider wants while riding: how long, how fast, how far gone,
// how far to go, and when they will get there. Headless, no UI, no clock of
// its own — every entry point that needs the time is HANDED it, which is what
// makes a 28-minute ride testable in a millisecond.
//
// THE ANCHOR, which is the one idea in here worth reading before the code.
// A GPS at a standstill does not report the same point twice; it wanders a
// metre or two a second, and a naive odometer summing every fix-to-fix step
// climbs at walking pace while the bike is locked to a rack. The cure is a
// minimum-movement floor — but applied the obvious way it breaks the other
// end of the range, because a WALKER at 1.4 m/s moves less than the floor
// between two 1 Hz fixes and would be recorded as never moving at all.
//
// So the floor is applied against an ANCHOR that only moves when a step is
// counted. Distance is always measured from the last COUNTED position, not
// from the previous fix. A walker's steps accumulate against a stationary
// anchor until they cross the floor and are then counted whole; a parked
// bike's jitter stays bounded around the anchor and crosses it rarely. One
// mechanism, both ends of the range, and it is also where the derived speed
// comes from (below), so the two numbers can never disagree about whether the
// ship is moving.
//
// WHAT IS DELIBERATELY NOT HERE. No moving-time/stopped-time split (the
// requirement asks for elapsed, and a rider who wants moving time wants a
// pause button, which is a UI decision); no ascent/descent (the fixture's own
// barometric altitude goes below sea level, and smoothing that into a climb
// total is a second algorithm with its own tests); no breadcrumb trail (the
// nav plan says so, and HeadingResolver's short history remains the only
// position history in the port).

#ifndef FVKIT_NAV_TRIP_H_
#define FVKIT_NAV_TRIP_H_

#include <vector>

#include "fvkit/geo.h"
#include "fvkit/nav/position.h"

namespace fv {

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

// Every constant the computer has, in one place, because all three are the
// kind of number a different vehicle wants differently.
struct TripSettings {
  // The anchor floor described above, metres. 2 m is about twice the
  // second-to-second wander of a consumer receiver with a clear sky, which is
  // the number that stops a parked bike creeping without losing a walker.
  double min_movement_m = 2.0;

  // Time constant of the exponential average the ETA divides by, seconds.
  // The ETA must not lurch every time the rider coasts, and 30 s is long
  // enough to ride out a junction and short enough to notice a headwind.
  // Sampled at whatever rate the fixes arrive: the weight is
  // 1 - exp(-dt/tau), so a 1 Hz feed and a 5 s feed smooth the same amount
  // per SECOND rather than per fix.
  double speed_ema_tau_s = 30.0;

  // Below this smoothed speed the ETA is not published at all, m/s. 0.5 m/s
  // is under half walking pace: slower than this the arrival time is a number
  // like "next Tuesday", and a bar showing an en-dash is telling the truth
  // where that would be lying. It is also the divide-by-zero guard.
  double min_eta_speed_mps = 0.5;
};

// ---------------------------------------------------------------------------
// The numbers
// ---------------------------------------------------------------------------

// Each number carries its own validity, and an invalid one is 0 rather than a
// NaN so that a shell which ignores the flags shows zeros and not "nan".
// Pippin's bar draws an en-dash for every false.
struct TripStats {
  // Since Start(), seconds — WALL CLOCK, not fix time. A rider who presses
  // GPS and then waits twenty seconds for a lock has been on the trip for
  // twenty seconds, and a first fix that arrives late must not rewrite that.
  double elapsed_s = 0.0;
  bool has_elapsed = false;

  // Distance travelled, metres. See the anchor note above.
  double odometer_m = 0.0;
  bool has_odometer = false;

  // Current ground speed, m/s. The fix's own when it reports one, otherwise
  // derived — the same preference order heading.h applies to a course, and
  // for the same reason: a receiver's own number beats anything computed from
  // two of its positions.
  double speed_mps = 0.0;
  bool has_speed = false;

  // Along-route distance from the ship to the route's end, metres. Invalid
  // when there is no route or no fix has arrived.
  double remaining_m = 0.0;
  bool has_remaining = false;

  // Predicted arrival, epoch seconds: now + remaining / smoothed speed.
  double eta_epoch_s = 0.0;
  bool has_eta = false;

  // The smoothed speed the ETA was computed from, m/s. Exposed because it is
  // the honest answer to "why is the ETA blank" and costs nothing to carry.
  double average_speed_mps = 0.0;
  bool has_average_speed = false;
};

// ---------------------------------------------------------------------------
// The computer
// ---------------------------------------------------------------------------

class TripComputer {
 public:
  explicit TripComputer(const TripSettings& settings = TripSettings());

  void SetSettings(const TripSettings& settings) { settings_ = settings; }
  const TripSettings& settings() const { return settings_; }

  // The route the "distance to go" is measured along, in order. Fewer than
  // two points is no route at all and clears it. Setting a route does NOT
  // reset the trip: a rider may plan one mid-ride, and the odometer behind
  // them is still theirs.
  void SetRoute(std::vector<GeoPoint> route);
  void ClearRoute() { SetRoute({}); }
  bool has_route() const { return route_.size() >= 2; }

  // Begins a trip at `now_epoch_s`, discarding any previous one. Starting an
  // already-running trip restarts it — the button that calls this is a toggle
  // and cannot produce that, but a caller must not have to know.
  void Start(double now_epoch_s);

  // Freezes elapsed at `now_epoch_s`. The numbers stay readable; Stats() goes
  // on answering with the frozen ones, which is what lets a shell leave the
  // bar up for a moment after the ride ends.
  void Stop(double now_epoch_s);

  // Back to "never started": every number invalid, the route kept.
  void Reset();

  bool running() const { return running_; }
  bool started() const { return started_; }

  // Feeds one fix. A fix with no position is ignored entirely (it carries no
  // distance and no speed worth trusting), and so is EVERY fix while the trip
  // is not running: this computer measures a trip, and the time between two
  // trips is not part of one.
  void OnFix(const PositionFix& fix);

  // The numbers as of `now_epoch_s`. Only `elapsed` and `eta` depend on it —
  // the rest were settled by the last fix — so a shell may call this on its
  // own frame clock, far faster than fixes arrive, and get a ticking clock
  // without inventing distance.
  TripStats Stats(double now_epoch_s) const;

 private:
  // Along-route distance from `p` to the end, or false when there is no
  // route. The projection is onto the NEAREST segment; see the note in the
  // .cpp about what that costs on a route that doubles back.
  bool RemainingAlongRoute(const GeoPoint& p, double* out_m) const;

  TripSettings settings_;

  std::vector<GeoPoint> route_;
  // Distance from each route vertex to the end, so the tail sum is a lookup
  // rather than a walk. Built once per SetRoute; route_to_end_[i] is the
  // length of route_[i..end].
  std::vector<double> route_to_end_;

  bool started_ = false;
  bool running_ = false;
  double start_epoch_s_ = 0.0;
  double stop_epoch_s_ = 0.0;

  // The anchor: the last position the odometer actually counted, and the fix
  // time it was counted at. `has_anchor_time_` is separate because a feed can
  // carry positions with no timestamps (an NMEA GLL), which still measures
  // distance and simply cannot derive a speed.
  GeoPoint anchor_{};
  bool has_anchor_ = false;
  double anchor_time_s_ = 0.0;
  bool has_anchor_time_ = false;

  double odometer_m_ = 0.0;

  GeoPoint last_position_{};
  bool has_last_position_ = false;

  double speed_mps_ = 0.0;
  bool has_speed_ = false;

  double average_speed_mps_ = 0.0;
  bool has_average_speed_ = false;
  double average_time_s_ = 0.0;
  bool has_average_time_ = false;
};

}  // namespace fv

#endif  // FVKIT_NAV_TRIP_H_
