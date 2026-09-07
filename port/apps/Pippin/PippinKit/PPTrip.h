// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPTrip.h — the ride's five numbers, as a value.
//
// `fv::TripStats` with the `std::` taken off, crossing a queue for
// `PPOwnship`'s reason: the trip computer is fed on the render queue, where
// both feeds converge. A live receiver's fixes arrive through `pushFix:` and
// a scripted replay's are polled inside the tick, and `MovingMapTick` is the
// one place that sees both, so the computer lives beside the moving map and
// its answer rides in on `PPFrame`.
//
// Every number carries its own validity, and the bar draws an en-dash rather
// than a zero for each false one. `0 km/h` is a bike at a red light and `—`
// is a receiver that has not said, and a rider glancing down deserves to know
// which.
//
// A ticking number on a frame is not a problem here: in GPS mode a frame is
// drawn for every fix, via `MapModel`'s content-dirty flag, so the bar
// refreshes at the receiver's own rate, which on this hardware is once a
// second.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

NS_SWIFT_SENDABLE
@interface PPTrip : NSObject

/// Trips are made by the map, never by the shell.
- (instancetype)init NS_UNAVAILABLE;

/// Seconds since the rider pressed GPS, on the wall clock rather than fix
/// time: a receiver that takes twenty seconds to get a lock has still been on
/// the trip for twenty seconds.
@property(nonatomic, readonly) NSTimeInterval elapsedSeconds;
@property(nonatomic, readonly) BOOL hasElapsed;

/// Distance ridden, in metres. Gated by the anchor floor in
/// `fvkit/nav/trip.h`, which stops a phone on a café table riding 200 m while
/// its owner drinks a coffee.
@property(nonatomic, readonly) double odometerMeters;
@property(nonatomic, readonly) BOOL hasOdometer;

/// Ground speed now, m/s. The receiver's own when it reports one.
@property(nonatomic, readonly) double speedMetersPerSecond;
@property(nonatomic, readonly) BOOL hasSpeed;

/// Distance to the end of the planned route, along the route, metres. Absent
/// when there is no route — which is most rides.
@property(nonatomic, readonly) double remainingMeters;
@property(nonatomic, readonly) BOOL hasRemaining;

/// Predicted arrival, in epoch seconds. Withdrawn below half walking pace,
/// because an arrival time computed from a stationary rider reads as next
/// Tuesday and an en-dash is more honest.
@property(nonatomic, readonly) NSTimeInterval etaTimestamp;
@property(nonatomic, readonly) BOOL hasEta;

/// The smoothed speed the ETA was divided by, in m/s. Carried because it
/// answers why the arrival time is blank.
@property(nonatomic, readonly) double averageSpeedMetersPerSecond;
@property(nonatomic, readonly) BOOL hasAverageSpeed;

@end

NS_ASSUME_NONNULL_END
