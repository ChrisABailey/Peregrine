// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPGuidance.h — the next turn, as a value.
//
// `fv::nav::GuidanceState` crossing the render queue on `PPFrame`, beside
// `PPTrip` and for the same reason: the state machine is fed in
// `consumeRawFix:`, where the live receiver and the scripted replay have
// already become one stream.
//
// Nil rather than empty, in three cases that mean different things to the
// rider and the same thing to the banner: outside GPS mode there is no ride,
// with no planned route there is nothing to be guided along, and past the
// destination there is nothing left to say. The banner is hidden for all
// three.
//
// Off route is NOT one of those cases. The guidance is still running and
// still knows where the rider is; it has stopped naming a corner, and the
// banner says so rather than vanishing.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// What the rider is being told to do. `fv::nav::ManeuverType`, and the
/// banner picks a glyph off it.
typedef NS_ENUM(NSInteger, PPManeuver) {
  PPManeuverDepart = 0,
  PPManeuverStraight,
  PPManeuverSlightLeft,
  PPManeuverLeft,
  PPManeuverSharpLeft,
  PPManeuverSlightRight,
  PPManeuverRight,
  PPManeuverSharpRight,
  PPManeuverUTurn,
  PPManeuverArrive,
};

/// Which approach ring an alert came from. The rider hears a different thing
/// for each, so the distinction has to survive the crossing.
typedef NS_ENUM(NSInteger, PPGuidanceRing) {
  PPGuidanceRingNone = 0,
  PPGuidanceRingHeadsUp,
  PPGuidanceRingActNow,
  PPGuidanceRingAt,
};

typedef NS_ENUM(NSInteger, PPGuidanceEventKind) {
  PPGuidanceEventApproach = 0,
  PPGuidanceEventPassed,
  PPGuidanceEventOffRoute,
  PPGuidanceEventRejoined,
  PPGuidanceEventArrived,
};

/// One thing the guidance said, edge-triggered and exactly once.
///
/// Events ride on `PPFrame` beside `PPGuidance` rather than on a channel of
/// their own, and they are BUFFERED between frames: a fix is consumed
/// whenever one arrives and a frame is drawn when the display link runs, so a
/// frame that swallowed two fixes must carry both fixes' events or an alert
/// is silently lost.
NS_SWIFT_SENDABLE
@interface PPGuidanceEvent : NSObject

- (instancetype)init NS_UNAVAILABLE;

@property(nonatomic, readonly) PPGuidanceEventKind kind;
/// Meaningful on `PPGuidanceEventApproach` and `PPGuidanceRingNone` on the
/// rest.
@property(nonatomic, readonly) PPGuidanceRing ring;
@property(nonatomic, readonly) PPManeuver maneuver;
/// Along-route metres to the maneuver when the event fired.
@property(nonatomic, readonly) double distanceMeters;

@end

NS_SWIFT_SENDABLE
@interface PPGuidance : NSObject

/// Guidance is made by the map, never by the shell.
- (instancetype)init NS_UNAVAILABLE;

/// NO when the rider has left the planned route. The guidance falls silent
/// rather than naming a corner it cannot see the rider approaching, and
/// `maneuver` and `distanceMeters` mean nothing until it is YES again.
@property(nonatomic, readonly) BOOL onRoute;

/// The turn being counted down to.
@property(nonatomic, readonly) PPManeuver maneuver;

/// Along-route metres to it. The shell formats; this is metres, like every
/// other distance crossing this boundary.
@property(nonatomic, readonly) double distanceMeters;

/// The road being joined, or empty for an unnamed way. Empty is common on
/// Kiawah, where a good deal of what a bicycle rides has no name in OSM, and
/// the banner drops the line rather than drawing a blank one.
@property(nonatomic, readonly) NSString *road;

/// A staggered junction: the corner immediately after this one, close enough
/// that "right" alone would be the wrong instruction. The banner draws both
/// glyphs.
@property(nonatomic, readonly) BOOL hasThen;
@property(nonatomic, readonly) PPManeuver thenManeuver;

/// Along-route metres left to the destination, for a banner that wants to
/// say so at the end. Distinct from `PPTrip.remainingMeters`, which is the
/// odometer's own projection and is absent on a ride with no route.
@property(nonatomic, readonly) double remainingMeters;

@end

NS_ASSUME_NONNULL_END
