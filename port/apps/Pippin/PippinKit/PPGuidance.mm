// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPGuidance+Internal.h"

static PPManeuver PPManeuverFrom(fv::nav::ManeuverType type) {
  switch (type) {
    case fv::nav::ManeuverType::kDepart: return PPManeuverDepart;
    case fv::nav::ManeuverType::kStraight: return PPManeuverStraight;
    case fv::nav::ManeuverType::kSlightLeft: return PPManeuverSlightLeft;
    case fv::nav::ManeuverType::kLeft: return PPManeuverLeft;
    case fv::nav::ManeuverType::kSharpLeft: return PPManeuverSharpLeft;
    case fv::nav::ManeuverType::kSlightRight: return PPManeuverSlightRight;
    case fv::nav::ManeuverType::kRight: return PPManeuverRight;
    case fv::nav::ManeuverType::kSharpRight: return PPManeuverSharpRight;
    case fv::nav::ManeuverType::kUTurn: return PPManeuverUTurn;
    case fv::nav::ManeuverType::kArrive: return PPManeuverArrive;
  }
  return PPManeuverStraight;
}

static PPGuidanceRing PPRingFrom(fv::nav::GuidanceRing ring) {
  switch (ring) {
    case fv::nav::GuidanceRing::kNone: return PPGuidanceRingNone;
    case fv::nav::GuidanceRing::kHeadsUp: return PPGuidanceRingHeadsUp;
    case fv::nav::GuidanceRing::kActNow: return PPGuidanceRingActNow;
    case fv::nav::GuidanceRing::kAt: return PPGuidanceRingAt;
  }
  return PPGuidanceRingNone;
}

static PPGuidanceEventKind PPKindFrom(fv::nav::GuidanceEventType type) {
  switch (type) {
    case fv::nav::GuidanceEventType::kApproach: return PPGuidanceEventApproach;
    case fv::nav::GuidanceEventType::kPassed: return PPGuidanceEventPassed;
    case fv::nav::GuidanceEventType::kOffRoute: return PPGuidanceEventOffRoute;
    case fv::nav::GuidanceEventType::kRejoined: return PPGuidanceEventRejoined;
    case fv::nav::GuidanceEventType::kArrived: return PPGuidanceEventArrived;
  }
  return PPGuidanceEventPassed;
}

static PPGuidanceEventKind PPKindFrom(fv::nav::CourseEventType type) {
  switch (type) {
    case fv::nav::CourseEventType::kWaypoint: return PPGuidanceEventWaypoint;
    case fv::nav::CourseEventType::kArrived: return PPGuidanceEventArrived;
    case fv::nav::CourseEventType::kOffCourse: return PPGuidanceEventOffCourse;
    case fv::nav::CourseEventType::kBackOnCourse: return PPGuidanceEventBackOnCourse;
  }
  return PPGuidanceEventWaypoint;
}

@implementation PPGuidanceEvent {
  fv::nav::GuidanceEvent _event;
  PPGuidanceEventKind _kind;
  NSString* _road;
  NSString* _nextName;
  double _crossTrack;
}

- (instancetype)initWithEvent:(const fv::nav::GuidanceEvent&)event
                         road:(NSString*)road {
  self = [super init];
  if (self != nil) {
    _event = event;
    _kind = PPKindFrom(event.type);
    _road = [road copy];
    _nextName = @"";
  }
  return self;
}

- (instancetype)initWithCourseEvent:(const fv::nav::CourseEvent&)event
                               name:(NSString*)name
                           nextName:(NSString*)nextName {
  self = [super init];
  if (self != nil) {
    _event.maneuver_index = event.waypoint_index;
    _kind = PPKindFrom(event.type);
    _road = [name copy];
    _nextName = [nextName copy];
    _crossTrack = event.cross_track_m;
  }
  return self;
}

- (PPGuidanceEventKind)kind { return _kind; }
- (double)crossTrackMeters { return _crossTrack; }
- (NSString*)nextName { return _nextName; }
- (PPGuidanceRing)ring { return PPRingFrom(_event.ring); }
- (PPManeuver)maneuver { return PPManeuverFrom(_event.maneuver_type); }
- (double)distanceMeters { return _event.distance_m; }
- (NSInteger)maneuverIndex { return static_cast<NSInteger>(_event.maneuver_index); }
- (NSString*)road { return _road; }

- (NSString*)description {
  return [NSString stringWithFormat:@"<PPGuidanceEvent %ld %s %.0f m>",
                                    static_cast<long>(_kind),
                                    fv::nav::GuidanceRingName(_event.ring),
                                    _event.distance_m];
}

@end

@implementation PPGuidance {
  fv::nav::GuidanceState _state;
  NSString* _road;
}

- (instancetype)initWithState:(const fv::nav::GuidanceState&)state
                         road:(NSString*)road {
  self = [super init];
  if (self != nil) {
    _state = state;
    _road = [road copy];
  }
  return self;
}

- (BOOL)onRoute { return _state.on_route ? YES : NO; }
- (PPManeuver)maneuver { return PPManeuverFrom(_state.next_type); }
- (double)distanceMeters { return _state.distance_to_next_m; }
- (NSString*)road { return _road; }
- (BOOL)hasThen { return _state.has_then ? YES : NO; }
- (PPManeuver)thenManeuver { return PPManeuverFrom(_state.then_type); }
- (double)remainingMeters { return _state.remaining_m; }

- (NSString*)description {
  if (!_state.on_route) return @"<PPGuidance off route>";
  return [NSString stringWithFormat:@"<PPGuidance %s in %.0f m%@%@>",
                                    fv::nav::ManeuverTypeName(_state.next_type),
                                    _state.distance_to_next_m,
                                    _road.length > 0
                                        ? [@" onto " stringByAppendingString:_road]
                                        : @"",
                                    _state.has_then ? @" (+then)" : @""];
}

@end

@implementation PPCourseDeviation {
  fv::nav::CourseState _state;
  double _limit;
  NSInteger _count;
  NSString* _name;
}

- (instancetype)initWithState:(const fv::nav::CourseState&)state
                        limit:(double)offCourseLimitMeters
                waypointCount:(NSInteger)count
                         name:(NSString*)name {
  self = [super init];
  if (self != nil) {
    _state = state;
    _limit = offCourseLimitMeters;
    _count = count;
    _name = [name copy];
  }
  return self;
}

- (double)legCourseDegrees { return _state.leg_course_deg; }
- (double)crossTrackMeters { return _state.cross_track_m; }
- (double)offCourseLimitMeters { return _limit; }
- (BOOL)offCourse { return _state.off_course ? YES : NO; }
- (NSInteger)waypointIndex { return static_cast<NSInteger>(_state.waypoint_index); }
- (NSInteger)waypointCount { return _count; }
- (NSString*)waypointName { return _name; }
- (double)bearingToWaypointDegrees { return _state.bearing_to_waypoint_deg; }
- (double)distanceToWaypointMeters { return _state.distance_to_waypoint_m; }
- (double)distanceToEndMeters { return _state.distance_to_end_m; }
- (BOOL)hasCourseOverGround { return _state.has_course_over_ground ? YES : NO; }
- (double)courseOverGroundDegrees { return _state.course_over_ground_deg; }
- (BOOL)hasTimeToGo { return _state.has_time_to_go ? YES : NO; }
- (double)secondsToWaypoint { return _state.time_to_waypoint_s; }
- (double)secondsToEnd { return _state.time_to_end_s; }
- (BOOL)hasETA { return _state.has_eta ? YES : NO; }
- (double)etaWaypointEpochSeconds { return _state.eta_waypoint_s; }
- (double)etaEndEpochSeconds { return _state.eta_end_s; }

- (NSString*)description {
  return [NSString stringWithFormat:@"<PPCourseDeviation wp %ld %.0f°T %.0f m xte %.0f m>",
                                    static_cast<long>(_state.waypoint_index),
                                    _state.bearing_to_waypoint_deg,
                                    _state.distance_to_waypoint_m, _state.cross_track_m];
}

@end
