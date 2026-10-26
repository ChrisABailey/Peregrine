// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/course_deviation.h"

#include <cmath>

#include "geo_tool.h"  // fv_geo_tool: rhumb range and bearing

namespace fv {
namespace nav {
namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/// Rhumb range and bearing from `a` to `b`. Coincident points (which the
/// geodesy refuses) are zero range, bearing 0.
void Rhumb(const GeoPoint& a, const GeoPoint& b, double* range_m, double* bearing_deg) {
  double r = 0.0;
  double brg = 0.0;
  if (GEO_calc_range_and_bearing(a.lat, a.lon, b.lat, b.lon, &r, &brg, FALSE) != SUCCESS) {
    r = 0.0;
    brg = 0.0;
  }
  *range_m = r;
  *bearing_deg = brg;
}

}  // namespace

void CourseDeviation::SetRoute(std::vector<GeoPoint> waypoints) {
  waypoints_ = std::move(waypoints);
  leg_course_deg_.clear();
  leg_length_m_.clear();
  for (std::size_t i = 0; i + 1 < waypoints_.size(); ++i) {
    double range = 0.0;
    double course = 0.0;
    Rhumb(waypoints_[i], waypoints_[i + 1], &range, &course);
    leg_course_deg_.push_back(course);
    leg_length_m_.push_back(range);
  }
  Reset();
}

void CourseDeviation::Reset() {
  state_ = CourseState();
  have_fix_ = false;
  has_last_along_ = false;
  last_along_m_ = 0.0;
  leaving_ = false;
  leaving_since_s_ = 0.0;
}

CourseDeviation::LegPosition CourseDeviation::Locate(const GeoPoint& p,
                                                     std::size_t leg) const {
  LegPosition at;
  double from_start_m = 0.0;
  double from_start_deg = 0.0;
  Rhumb(waypoints_[leg], p, &from_start_m, &from_start_deg);
  const double off = (from_start_deg - leg_course_deg_[leg]) * kDegToRad;
  at.along_m = from_start_m * std::cos(off);
  at.cross_m = from_start_m * std::sin(off);
  Rhumb(p, waypoints_[leg + 1], &at.to_waypoint_m, &at.bearing_deg);
  return at;
}

bool CourseDeviation::LegDone(const LegPosition& at, std::size_t leg) const {
  if (at.to_waypoint_m <= settings_.arrival_m) return true;
  if (at.along_m < leg_length_m_[leg]) return false;
  // The last perpendicular counts only when crossed: a boat already beyond it
  // when the leg became active has not arrived.
  if (leg + 2 == waypoints_.size()) return has_last_along_ && last_along_m_ < leg_length_m_[leg];
  return true;
}

std::vector<CourseEvent> CourseDeviation::OnFix(const PositionFix& fix) {
  std::vector<CourseEvent> events;
  if (!has_route() || state_.arrived || !fix.has_position) return events;

  GeoPoint p;
  p.lat = fix.lat;
  p.lon = fix.lon;

  const bool first = !have_fix_;
  have_fix_ = true;
  const std::size_t last_leg = waypoints_.size() - 2;

  LegPosition at = Locate(p, state_.leg_index);
  while (LegDone(at, state_.leg_index)) {
    const std::size_t reached = state_.leg_index + 1;
    if (state_.leg_index == last_leg) {
      state_.arrived = true;
      state_.valid = true;
      state_.waypoint_index = reached;
      state_.distance_to_waypoint_m = at.to_waypoint_m;
      state_.distance_to_end_m = at.to_waypoint_m;
      state_.has_time_to_go = false;
      state_.has_eta = false;
      CourseEvent e;
      e.type = CourseEventType::kArrived;
      e.waypoint_index = reached;
      e.cross_track_m = at.cross_m;
      events.push_back(e);
      return events;
    }
    if (!first) {
      CourseEvent e;
      e.type = CourseEventType::kWaypoint;
      e.waypoint_index = reached;
      e.cross_track_m = at.cross_m;
      events.push_back(e);
    }
    ++state_.leg_index;
    has_last_along_ = false;
    at = Locate(p, state_.leg_index);
  }
  if (state_.leg_index == last_leg) {
    last_along_m_ = at.along_m;
    has_last_along_ = true;
  }

  state_.valid = true;
  state_.waypoint_index = state_.leg_index + 1;
  state_.leg_course_deg = leg_course_deg_[state_.leg_index];
  state_.cross_track_m = at.cross_m;
  state_.bearing_to_waypoint_deg = at.bearing_deg;
  state_.distance_to_waypoint_m = at.to_waypoint_m;
  double to_end = at.to_waypoint_m;
  for (std::size_t i = state_.leg_index + 1; i < leg_length_m_.size(); ++i) {
    to_end += leg_length_m_[i];
  }
  state_.distance_to_end_m = to_end;

  state_.has_speed = fix.has_speed;
  state_.speed_mps = fix.has_speed ? fix.speed_mps : 0.0;
  state_.has_course_over_ground = fix.has_true_heading;
  state_.course_over_ground_deg = fix.has_true_heading ? fix.true_heading_deg : 0.0;

  state_.has_time_to_go = fix.has_speed && fix.speed_mps >= settings_.min_speed_mps;
  if (state_.has_time_to_go) {
    state_.time_to_waypoint_s = state_.distance_to_waypoint_m / fix.speed_mps;
    state_.time_to_end_s = state_.distance_to_end_m / fix.speed_mps;
  } else {
    state_.time_to_waypoint_s = 0.0;
    state_.time_to_end_s = 0.0;
  }
  state_.has_eta = state_.has_time_to_go && fix.has_time;
  state_.eta_waypoint_s = state_.has_eta ? fix.time_s + state_.time_to_waypoint_s : 0.0;
  state_.eta_end_s = state_.has_eta ? fix.time_s + state_.time_to_end_s : 0.0;

  const double xte = std::fabs(at.cross_m);
  if (!state_.off_course) {
    if (xte > settings_.off_course_m) {
      if (!leaving_) {
        leaving_ = true;
        leaving_since_s_ = fix.has_time ? fix.time_s : 0.0;
      }
      // An untimed stream cannot hold, so its first fix past the limit alarms.
      const bool held = !fix.has_time ||
                        fix.time_s - leaving_since_s_ >= settings_.off_course_hold_s;
      if (held) {
        state_.off_course = true;
        leaving_ = false;
        CourseEvent e;
        e.type = CourseEventType::kOffCourse;
        e.waypoint_index = state_.waypoint_index;
        e.cross_track_m = at.cross_m;
        events.push_back(e);
      }
    } else {
      leaving_ = false;
    }
  } else if (xte < settings_.off_course_m * settings_.back_on_course_fraction) {
    state_.off_course = false;
    CourseEvent e;
    e.type = CourseEventType::kBackOnCourse;
    e.waypoint_index = state_.waypoint_index;
    e.cross_track_m = at.cross_m;
    events.push_back(e);
  }
  return events;
}

const char* CourseEventTypeName(CourseEventType type) {
  switch (type) {
    case CourseEventType::kWaypoint: return "waypoint";
    case CourseEventType::kArrived: return "arrived";
    case CourseEventType::kOffCourse: return "off_course";
    case CourseEventType::kBackOnCourse: return "back_on_course";
  }
  return "?";
}

}  // namespace nav
}  // namespace fv
