// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/trip.h"

#include <cmath>
#include <utility>

#include "fvkit/nav/road_snap.h"

namespace fv {
namespace {

constexpr double kTripDegToRad = 3.14159265358979323846 / 180.0;
// The SAME metre port/Routing and road_snap.h measure in (WGS-84 mean
// radius). An odometer that disagreed with the router's own arc lengths would
// count a route down to zero from a total it never had.
constexpr double kTripEarthRadiusM = 6371008.8;

// Great-circle distance, the haversine RoadGraph uses verbatim. Not the
// tangent-plane approximation ProjectOntoSegment works in: that one is exact
// enough over a road segment and is measuring a perpendicular offset, while
// this is summed 1705 times over a ride and its error would accumulate.
double GreatCircleMeters(const GeoPoint& a, const GeoPoint& b) {
  const double phi1 = a.lat * kTripDegToRad;
  const double phi2 = b.lat * kTripDegToRad;
  const double dphi = (b.lat - a.lat) * kTripDegToRad;
  const double dlam = NormalizeLon(b.lon - a.lon) * kTripDegToRad;
  const double s1 = std::sin(dphi * 0.5);
  const double s2 = std::sin(dlam * 0.5);
  double h = s1 * s1 + std::cos(phi1) * std::cos(phi2) * s2 * s2;
  if (h > 1.0) h = 1.0;
  return 2.0 * kTripEarthRadiusM * std::asin(std::sqrt(h));
}

}  // namespace

TripComputer::TripComputer(const TripSettings& settings) : settings_(settings) {}

// ---------------------------------------------------------------------------
// Route
// ---------------------------------------------------------------------------

void TripComputer::SetRoute(std::vector<GeoPoint> route) {
  route_ = std::move(route);
  route_to_end_.clear();
  if (route_.size() < 2) {
    route_.clear();
    return;
  }
  // Suffix lengths, walked once from the end. route_to_end_[i] is how far
  // route_[i] is from the finish along the line.
  route_to_end_.assign(route_.size(), 0.0);
  for (std::size_t i = route_.size() - 1; i > 0; --i) {
    route_to_end_[i - 1] =
        route_to_end_[i] + GreatCircleMeters(route_[i - 1], route_[i]);
  }
}

// NOTE (nearest segment, and what it costs). The ship is placed on the route
// by taking the segment it is CLOSEST to. On a route that doubles back — an
// out-and-back, or a loop passing within a few metres of itself — the outward
// and return limbs are both close and the nearest one may be the wrong one,
// which shows up as the distance-to-go jumping to the other limb's value.
// The alternative is to carry along-route progress as state and forbid it
// moving backwards, and that trades this glitch for a worse one: a rider who
// leaves the route and rejoins it further on is then stuck at the point they
// left. Kept stateless deliberately; if this ever needs fixing the fix is a
// SEARCH WINDOW around the last projection, not a ratchet.
bool TripComputer::RemainingAlongRoute(const GeoPoint& p, double* out_m) const {
  if (route_.size() < 2 || out_m == nullptr) return false;

  double best_distance = 0.0;
  double best_remaining = 0.0;
  bool have_best = false;

  for (std::size_t i = 0; i + 1 < route_.size(); ++i) {
    SegmentProjection projection;
    if (!ProjectOntoSegment(p, route_[i], route_[i + 1], &projection)) continue;
    // What is left of THIS segment past the projection, plus everything after
    // it. `along_m` is measured from route_[i], so the tail of the segment is
    // its length minus that.
    double segment_tail = projection.length_m - projection.along_m;
    if (segment_tail < 0.0) segment_tail = 0.0;
    const double remaining = segment_tail + route_to_end_[i + 1];
    if (!have_best || projection.distance_m < best_distance) {
      best_distance = projection.distance_m;
      best_remaining = remaining;
      have_best = true;
    }
  }
  if (!have_best) return false;
  *out_m = best_remaining;
  return true;
}

// ---------------------------------------------------------------------------
// The trip
// ---------------------------------------------------------------------------

void TripComputer::Start(double now_epoch_s) {
  Reset();
  started_ = true;
  running_ = true;
  start_epoch_s_ = now_epoch_s;
  stop_epoch_s_ = now_epoch_s;
}

void TripComputer::Stop(double now_epoch_s) {
  if (!running_) return;
  running_ = false;
  // Freeze at the later of the two: a caller passing a clock that has gone
  // backwards (a system time correction mid-ride is the real case) must not
  // shorten a ride that has already been ridden.
  stop_epoch_s_ = now_epoch_s > start_epoch_s_ ? now_epoch_s : start_epoch_s_;
}

void TripComputer::Reset() {
  started_ = false;
  running_ = false;
  start_epoch_s_ = 0.0;
  stop_epoch_s_ = 0.0;
  has_anchor_ = false;
  anchor_time_s_ = 0.0;
  has_anchor_time_ = false;
  odometer_m_ = 0.0;
  has_last_position_ = false;
  speed_mps_ = 0.0;
  has_speed_ = false;
  average_speed_mps_ = 0.0;
  has_average_speed_ = false;
  average_time_s_ = 0.0;
  has_average_time_ = false;
}

void TripComputer::OnFix(const PositionFix& fix) {
  if (!running_ || !fix.has_position) return;

  const GeoPoint p = fix.position();
  last_position_ = p;
  has_last_position_ = true;

  if (!has_anchor_) {
    anchor_ = p;
    has_anchor_ = true;
    anchor_time_s_ = fix.time_s;
    has_anchor_time_ = fix.has_time;
    // The first fix of a trip has nothing to measure against. A reported
    // speed is still a reported speed, though, so fall through to the speed
    // block rather than returning.
  } else {
    const double step_m = GreatCircleMeters(anchor_, p);
    if (step_m >= settings_.min_movement_m) {
      odometer_m_ += step_m;
      // The anchor moves ONLY here. See the header: this is what lets one
      // floor serve both a parked bike and a walker.
      const double previous_time = anchor_time_s_;
      const bool had_time = has_anchor_time_;
      anchor_ = p;
      anchor_time_s_ = fix.time_s;
      has_anchor_time_ = fix.has_time;

      // The derived speed rides on the same step, so "moving" means the same
      // thing to both numbers. dt is from the anchor rather than from the
      // previous fix, which is exactly right at low speed: at a standstill
      // the numerator stays small while dt keeps growing, and the speed falls
      // toward zero on its own.
      if (!fix.has_speed && had_time && fix.has_time) {
        const double dt = fix.time_s - previous_time;
        if (dt > 0.0) {
          speed_mps_ = step_m / dt;
          has_speed_ = true;
        }
      }
    } else if (!fix.has_speed && has_anchor_time_ && fix.has_time) {
      // Below the floor and not reported: the ship is as good as stopped, and
      // the honest reading is the same quotient over the growing dt rather
      // than the last speed held forever.
      const double dt = fix.time_s - anchor_time_s_;
      if (dt > 0.0) {
        speed_mps_ = step_m / dt;
        has_speed_ = true;
      }
    }
  }

  // A receiver's own speed always wins (heading.h's preference order).
  if (fix.has_speed) {
    speed_mps_ = fix.speed_mps;
    has_speed_ = true;
  }

  // The ETA's smoothed speed. Weighted by ELAPSED TIME, not by fix count, so
  // the constant means the same thing on a 1 Hz feed and on a 5 s one.
  if (has_speed_) {
    if (!has_average_speed_ || !has_average_time_ || !fix.has_time) {
      average_speed_mps_ = speed_mps_;
      has_average_speed_ = true;
    } else {
      const double dt = fix.time_s - average_time_s_;
      if (dt > 0.0) {
        const double tau = settings_.speed_ema_tau_s > 0.0
                               ? settings_.speed_ema_tau_s
                               : 1e-9;
        const double alpha = 1.0 - std::exp(-dt / tau);
        average_speed_mps_ += alpha * (speed_mps_ - average_speed_mps_);
      }
    }
    if (fix.has_time) {
      average_time_s_ = fix.time_s;
      has_average_time_ = true;
    }
  }
}

TripStats TripComputer::Stats(double now_epoch_s) const {
  TripStats out;
  if (!started_) return out;

  const double end = running_ ? now_epoch_s : stop_epoch_s_;
  out.elapsed_s = end > start_epoch_s_ ? end - start_epoch_s_ : 0.0;
  out.has_elapsed = true;

  out.odometer_m = odometer_m_;
  out.has_odometer = true;

  out.speed_mps = speed_mps_;
  out.has_speed = has_speed_;

  out.average_speed_mps = average_speed_mps_;
  out.has_average_speed = has_average_speed_;

  if (has_last_position_) {
    double remaining = 0.0;
    if (RemainingAlongRoute(last_position_, &remaining)) {
      out.remaining_m = remaining;
      out.has_remaining = true;
    }
  }

  if (out.has_remaining && has_average_speed_ &&
      average_speed_mps_ >= settings_.min_eta_speed_mps) {
    out.eta_epoch_s = now_epoch_s + out.remaining_m / average_speed_mps_;
    out.has_eta = true;
  }
  return out;
}

}  // namespace fv
