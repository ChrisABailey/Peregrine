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

@implementation PPGuidanceEvent {
  fv::nav::GuidanceEvent _event;
}

- (instancetype)initWithEvent:(const fv::nav::GuidanceEvent&)event {
  self = [super init];
  if (self != nil) _event = event;
  return self;
}

- (PPGuidanceEventKind)kind { return PPKindFrom(_event.type); }
- (PPGuidanceRing)ring { return PPRingFrom(_event.ring); }
- (PPManeuver)maneuver { return PPManeuverFrom(_event.maneuver_type); }
- (double)distanceMeters { return _event.distance_m; }

- (NSString*)description {
  return [NSString stringWithFormat:@"<PPGuidanceEvent %s %s %.0f m>",
                                    fv::nav::GuidanceEventTypeName(_event.type),
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
