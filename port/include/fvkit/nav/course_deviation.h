// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/nav/course_deviation.h — course deviation indicator over a route of
// rhumb-line legs.
//
// Fed the route's waypoints and a stream of fixes, it keeps an active leg and
// reports cross-track error against it, bearing and distance to the active
// waypoint, and time to go from SOG. It advances the leg on reaching the
// arrival circle or crossing the perpendicular through the waypoint, and
// raises an off-course alarm once |XTE| has stayed past the limit for
// `off_course_hold_s`. Events are edge-triggered, once each. Headless: the
// clock is read off the fix.
//
// Geometry: each leg is a rhumb line. Cross-track and along-track come from
// the rhumb range and bearing from the leg start to the fix, resolved against
// the leg course; Mercator is conformal, so the angle is exact and the
// distances are a local-plane approximation that is well inside a metre for
// a boat a few miles off a leg.

#ifndef FVKIT_NAV_COURSE_DEVIATION_H_
#define FVKIT_NAV_COURSE_DEVIATION_H_

#include <cstddef>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/nav/position.h"

namespace fv {
namespace nav {

enum class CourseEventType {
  kWaypoint,     // `waypoint_index` reached; the next leg is active
  kArrived,      // the last waypoint is reached; nothing is said after it
  kOffCourse,    // |XTE| has stayed past the limit
  kBackOnCourse, // ...and is back inside it
};

/// One thing that happened on one fix.
struct CourseEvent {
  CourseEventType type = CourseEventType::kWaypoint;
  /// The waypoint reached (kWaypoint, kArrived) or steered for (the others).
  std::size_t waypoint_index = 0;
  /// Signed cross-track error when the event fired; positive is right of track.
  double cross_track_m = 0.0;
};

struct CourseSettings {
  /// A waypoint is reached inside this radius. 0.05 nm.
  double arrival_m = 92.6;
  /// The off-course alarm limit. 0.1 nm.
  double off_course_m = 185.2;
  /// How long |XTE| must stay past the limit before the alarm.
  double off_course_hold_s = 5.0;
  /// Back on course below this fraction of the limit, so a boat on the line
  /// does not chatter between the two.
  double back_on_course_fraction = 0.8;
  /// Below this SOG time to go is not estimated. 0.5 kn.
  double min_speed_mps = 0.2572;
};

/// What a CDI draws. Valid once a route and one fix have arrived.
struct CourseState {
  bool valid = false;
  bool arrived = false;
  bool off_course = false;

  /// The active leg runs from waypoint `leg_index` to `leg_index + 1`.
  std::size_t leg_index = 0;
  std::size_t waypoint_index = 1;

  /// Desired track: the active leg's rhumb course, degrees true.
  double leg_course_deg = 0.0;
  /// Signed metres from the leg; positive is right of track (steer left).
  double cross_track_m = 0.0;
  /// Rhumb bearing (degrees true) and distance from the fix to the active waypoint.
  double bearing_to_waypoint_deg = 0.0;
  double distance_to_waypoint_m = 0.0;
  /// Distance to the active waypoint plus every leg after it.
  double distance_to_end_m = 0.0;

  double speed_mps = 0.0;
  bool has_speed = false;
  /// Receiver COG, degrees true.
  double course_over_ground_deg = 0.0;
  bool has_course_over_ground = false;

  /// Seconds to the active waypoint and to the end at SOG, and the matching
  /// epoch-second arrival times. `has_time_to_go` is false when stopped or
  /// slower than `min_speed_mps`; the ETAs also need a timed fix.
  bool has_time_to_go = false;
  double time_to_waypoint_s = 0.0;
  double time_to_end_s = 0.0;
  bool has_eta = false;
  double eta_waypoint_s = 0.0;
  double eta_end_s = 0.0;
};

/// One route's worth of course deviation.
class CourseDeviation {
 public:
  CourseDeviation() = default;
  explicit CourseDeviation(const CourseSettings& settings) : settings_(settings) {}

  void SetSettings(const CourseSettings& settings) { settings_ = settings; }
  const CourseSettings& settings() const { return settings_; }

  /// Replaces the route and starts over. Fewer than two waypoints is no route.
  void SetRoute(std::vector<GeoPoint> waypoints);
  void ClearRoute() { SetRoute({}); }
  bool has_route() const { return waypoints_.size() >= 2; }

  /// Back to the first leg with the route kept.
  void Reset();

  /// Feeds one fix and returns what it caused, in order. A fix with no
  /// position is ignored. On the first fix, legs whose waypoint is already
  /// behind the boat are skipped without events.
  std::vector<CourseEvent> OnFix(const PositionFix& fix);

  const CourseState& state() const { return state_; }
  const std::vector<GeoPoint>& waypoints() const { return waypoints_; }

 private:
  struct LegPosition {
    double along_m = 0.0;
    double cross_m = 0.0;
    double to_waypoint_m = 0.0;
    double bearing_deg = 0.0;
  };

  /// Where `p` is relative to leg `leg`.
  LegPosition Locate(const GeoPoint& p, std::size_t leg) const;

  /// True when leg `leg` is done: inside the arrival circle or past the
  /// perpendicular through its end (crossed, on the last leg).
  bool LegDone(const LegPosition& at, std::size_t leg) const;

  CourseSettings settings_;
  std::vector<GeoPoint> waypoints_;
  std::vector<double> leg_course_deg_;
  std::vector<double> leg_length_m_;
  CourseState state_;

  bool have_fix_ = false;
  /// Along-track metres on the last leg at the previous fix.
  bool has_last_along_ = false;
  double last_along_m_ = 0.0;
  bool leaving_ = false;
  double leaving_since_s_ = 0.0;
};

/// The event's short name, for a test failure or a log line. Not for a user.
const char* CourseEventTypeName(CourseEventType type);

}  // namespace nav
}  // namespace fv

#endif  // FVKIT_NAV_COURSE_DEVIATION_H_
