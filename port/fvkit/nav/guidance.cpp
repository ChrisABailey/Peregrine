// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/guidance.h"

#include <algorithm>
#include <cmath>

#include "fvkit/nav/road_snap.h"  // ProjectOntoSegment — the port's metre

namespace fv {
namespace nav {
namespace {

constexpr double kGuidanceDegToRad = 3.14159265358979323846 / 180.0;
// The metre port/Routing, road_snap.h and the trip computer all measure in.
constexpr double kGuidanceEarthRadiusM = 6371008.8;

double GreatCircleMeters(const GeoPoint& a, const GeoPoint& b) {
  const double phi1 = a.lat * kGuidanceDegToRad;
  const double phi2 = b.lat * kGuidanceDegToRad;
  const double dphi = (b.lat - a.lat) * kGuidanceDegToRad;
  const double dlam = NormalizeLon(b.lon - a.lon) * kGuidanceDegToRad;
  const double s1 = std::sin(dphi * 0.5);
  const double s2 = std::sin(dlam * 0.5);
  double h = s1 * s1 + std::cos(phi1) * std::cos(phi2) * s2 * s2;
  if (h > 1.0) h = 1.0;
  return 2.0 * kGuidanceEarthRadiusM * std::asin(std::sqrt(h));
}

double Clamp(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

unsigned char RingBit(GuidanceRing ring) {
  switch (ring) {
    case GuidanceRing::kHeadsUp: return 1u;
    case GuidanceRing::kActNow: return 2u;
    case GuidanceRing::kAt: return 4u;
    default: return 0u;
  }
}

}  // namespace

const char* GuidanceEventTypeName(GuidanceEventType type) {
  switch (type) {
    case GuidanceEventType::kApproach: return "approach";
    case GuidanceEventType::kPassed: return "passed";
    case GuidanceEventType::kOffRoute: return "off route";
    case GuidanceEventType::kRejoined: return "rejoined";
    case GuidanceEventType::kArrived: return "arrived";
  }
  return "?";
}

const char* GuidanceRingName(GuidanceRing ring) {
  switch (ring) {
    case GuidanceRing::kNone: return "none";
    case GuidanceRing::kHeadsUp: return "heads up";
    case GuidanceRing::kActNow: return "act now";
    case GuidanceRing::kAt: return "at";
  }
  return "?";
}

void Guidance::SetRoute(std::vector<GeoPoint> geometry,
                        std::vector<Maneuver> maneuvers) {
  geometry_ = std::move(geometry);
  maneuvers_ = std::move(maneuvers);
  cumulative_.clear();
  if (geometry_.size() < 2 || maneuvers_.empty()) {
    geometry_.clear();
    maneuvers_.clear();
  } else {
    cumulative_.assign(geometry_.size(), 0.0);
    for (std::size_t i = 1; i < geometry_.size(); ++i) {
      cumulative_[i] =
          cumulative_[i - 1] + GreatCircleMeters(geometry_[i - 1], geometry_[i]);
    }
  }
  Reset();
}

void Guidance::Reset() {
  fired_.assign(maneuvers_.size(), 0u);
  state_ = GuidanceState();
  have_fix_ = false;
  has_last_time_ = false;
  last_along_m_ = 0.0;
  leaving_ = false;
  has_leaving_time_ = false;
  arrived_ = false;
}

double Guidance::HeadsUpMeters(double speed_mps) const {
  return Clamp(speed_mps * settings_.heads_up_s, settings_.heads_up_min_m,
               settings_.heads_up_max_m);
}

double Guidance::ActNowMeters(double speed_mps) const {
  return Clamp(speed_mps * settings_.act_now_s, settings_.act_now_min_m,
               settings_.act_now_max_m);
}

double Guidance::AtMeters(double speed_mps) const {
  const double travelled = speed_mps * settings_.at_s;
  return travelled > settings_.at_m ? travelled : settings_.at_m;
}

bool Guidance::Project(const GeoPoint& p, double near_along_m, double window_m,
                       double* along_m, double* cross_m) const {
  if (geometry_.size() < 2) return false;

  bool have_best = false;
  double best_cross = 0.0;
  double best_along = 0.0;

  for (std::size_t i = 0; i + 1 < geometry_.size(); ++i) {
    if (window_m >= 0.0) {
      // Skip a segment that lies wholly outside the window. Both ends are
      // tested so a segment straddling the edge is kept.
      if (cumulative_[i + 1] < near_along_m - window_m) continue;
      if (cumulative_[i] > near_along_m + window_m) break;
    }
    SegmentProjection projection;
    if (!ProjectOntoSegment(p, geometry_[i], geometry_[i + 1], &projection)) continue;
    if (have_best && projection.distance_m >= best_cross) continue;
    have_best = true;
    best_cross = projection.distance_m;
    best_along = cumulative_[i] + projection.along_m;
  }

  if (!have_best) return false;
  *along_m = best_along;
  *cross_m = best_cross;
  return true;
}

std::vector<GuidanceEvent> Guidance::OnFix(const PositionFix& fix) {
  std::vector<GuidanceEvent> events;
  if (!has_route() || !fix.has_position) return events;

  GeoPoint here;
  here.lat = fix.lat;
  here.lon = fix.lon;

  // Speed: the receiver's when it has one, otherwise the step since the last
  // fix. The trip computer derives it the same way and for the same reason.
  double speed = 0.0;
  bool has_speed = false;
  if (fix.has_speed) {
    speed = fix.speed_mps < 0.0 ? 0.0 : fix.speed_mps;
    has_speed = true;
  } else if (have_fix_ && has_last_time_ && fix.has_time) {
    const double dt = fix.time_s - last_time_s_;
    if (dt > 0.0) {
      speed = GreatCircleMeters(last_position_, here) / dt;
      has_speed = true;
    }
  }

  // The windowed projection, widened to the whole route only when the answer
  // it gives is off route anyway.
  double along = 0.0;
  double cross = 0.0;
  const bool first = !have_fix_;
  if (!Project(here, last_along_m_, first ? -1.0 : settings_.search_window_m,
               &along, &cross)) {
    return events;
  }
  if (!first && cross > settings_.off_route_m) {
    double wide_along = 0.0;
    double wide_cross = 0.0;
    if (Project(here, 0.0, -1.0, &wide_along, &wide_cross) && wide_cross < cross) {
      along = wide_along;
      cross = wide_cross;
    }
  }

  // --- on route / off route, before anything is announced -----------------
  const bool was_on_route = state_.on_route || first;
  bool on_route = was_on_route;
  if (was_on_route) {
    if (cross > settings_.off_route_m) {
      if (!leaving_) {
        leaving_ = true;
        leaving_since_s_ = fix.time_s;
        has_leaving_time_ = fix.has_time;
      }
      // Without a clock there is nothing to hold against, so the first fix
      // beyond the tolerance is the answer.
      const bool held = !has_leaving_time_ || !fix.has_time ||
                        (fix.time_s - leaving_since_s_) >= settings_.off_route_hold_s;
      if (held) {
        on_route = false;
        leaving_ = false;
        GuidanceEvent e;
        e.type = GuidanceEventType::kOffRoute;
        e.distance_m = cross;
        events.push_back(e);
      }
    } else {
      leaving_ = false;
    }
  } else if (cross <= settings_.rejoin_m) {
    on_route = true;
    leaving_ = false;
    GuidanceEvent e;
    e.type = GuidanceEventType::kRejoined;
    e.distance_m = cross;
    events.push_back(e);
  }

  // Which maneuver is next, recomputed from the projection rather than
  // ratcheted: a rider who left the route and rejoined further back is at the
  // corner they are actually at.
  std::size_t next = 0;
  while (next < maneuvers_.size() && maneuvers_[next].distance_m <= along) ++next;

  // Passing one is worth saying, but only from a baseline: the first fix
  // establishes where the rider is without announcing everything behind them.
  if (!first && on_route && was_on_route && next > state_.next_index &&
      state_.has_next) {
    for (std::size_t i = state_.next_index; i < next && i < maneuvers_.size(); ++i) {
      if (maneuvers_[i].type == ManeuverType::kArrive) continue;
      GuidanceEvent e;
      e.type = GuidanceEventType::kPassed;
      e.maneuver_index = i;
      e.maneuver_type = maneuvers_[i].type;
      events.push_back(e);
    }
  }

  // Rejoining re-arms every alert still ahead: the corner was never announced
  // while the guidance was silent.
  if (on_route && !was_on_route) {
    for (std::size_t i = next; i < fired_.size(); ++i) fired_[i] = 0u;
  }

  const double total = cumulative_.back();
  const double remaining = total - along;

  // --- arrival, before the rings ------------------------------------------
  // The destination is announced once, by this rather than by the arrive
  // maneuver's own at-the-junction ring: they are the same point, and saying
  // it twice is worse than saying it once.
  if (on_route && !arrived_ && remaining <= settings_.arrive_m) {
    arrived_ = true;
    GuidanceEvent e;
    e.type = GuidanceEventType::kArrived;
    e.maneuver_index = maneuvers_.size() - 1;
    e.maneuver_type = maneuvers_.back().type;
    e.distance_m = remaining;
    events.push_back(e);
  }

  // --- the rings ----------------------------------------------------------
  bool has_then = false;
  std::size_t then_index = 0;
  if (on_route && !arrived_ && next < maneuvers_.size()) {
    const double to_next = maneuvers_[next].distance_m - along;
    const double act_now_m = ActNowMeters(speed);

    // A staggered junction: the one after this falls inside the same act-now
    // ring, so "right" alone is the wrong instruction.
    if (next + 1 < maneuvers_.size() &&
        maneuvers_[next + 1].distance_m - maneuvers_[next].distance_m <= act_now_m &&
        maneuvers_[next + 1].type != ManeuverType::kArrive) {
      has_then = true;
      then_index = next + 1;
    }

    // Innermost first: crossing two rings between fixes is one alert, and the
    // outer ones are spent rather than queued.
    const struct {
      GuidanceRing ring;
      double radius;
    } rings[] = {
        {GuidanceRing::kAt, AtMeters(speed)},
        {GuidanceRing::kActNow, act_now_m},
        {GuidanceRing::kHeadsUp, HeadsUpMeters(speed)},
    };

    bool emitted = false;
    for (const auto& r : rings) {
      if (to_next > r.radius) continue;
      const unsigned char bit = RingBit(r.ring);
      if ((fired_[next] & bit) != 0u) continue;
      fired_[next] |= bit;
      if (emitted) continue;  // spent, not announced
      emitted = true;
      GuidanceEvent e;
      e.type = GuidanceEventType::kApproach;
      e.ring = r.ring;
      e.maneuver_index = next;
      e.maneuver_type = maneuvers_[next].type;
      e.distance_m = to_next;
      e.has_then = has_then;
      e.then_index = then_index;
      if (has_then) e.then_type = maneuvers_[then_index].type;
      events.push_back(e);
    }
  }

  // --- what the banner draws ----------------------------------------------
  state_.valid = true;
  state_.on_route = on_route;
  state_.has_next = on_route && !arrived_ && next < maneuvers_.size();
  state_.next_index = next;
  if (state_.has_next) {
    state_.next_type = maneuvers_[next].type;
    state_.distance_to_next_m = maneuvers_[next].distance_m - along;
  } else {
    state_.next_type = ManeuverType::kStraight;
    state_.distance_to_next_m = 0.0;
  }
  state_.has_then = state_.has_next && has_then;
  state_.then_index = then_index;
  state_.then_type =
      state_.has_then ? maneuvers_[then_index].type : ManeuverType::kStraight;
  state_.along_m = along;
  state_.cross_track_m = cross;
  state_.remaining_m = remaining;
  state_.speed_mps = speed;
  state_.has_speed = has_speed;

  have_fix_ = true;
  last_position_ = here;
  last_along_m_ = along;
  if (fix.has_time) {
    last_time_s_ = fix.time_s;
    has_last_time_ = true;
  }
  return events;
}

}  // namespace nav
}  // namespace fv
