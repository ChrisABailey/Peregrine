// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPFix.h — one position report, as a value.
//
// `fv::PositionFix` with the `std::` taken off, and it exists for
// `PPViewport`'s reason: it crosses a queue. A fix is born on whatever thread
// CoreLocation chose, is handed to the main thread, and is handed on to the
// render queue where the moving map lives, so it is immutable,
// self-contained and `NS_SWIFT_SENDABLE`, and no part of that needs a lock.
//
// Every field carries its own validity, which is MM1's first rule: the
// Windows original's -1000.0 sentinels do not port. `hasSpeed` false is a
// receiver that did not say and `speed` 0 is a bike standing still, which are
// not the same fact. The sentinel translation is in `PPLocationFix.h`, under
// ctest on the mac.
//
// Deliberately not a `CLLocation`. Above PippinKit the shell need not know
// which platform API a fix came from, and below it only `PPLocationSource`
// knows CoreLocation exists, which is what makes a recorded GPX and the demo
// feed sources of the same type rather than special cases.

#import <Foundation/Foundation.h>

#import <PippinKit/PPGeometry.h>

NS_ASSUME_NONNULL_BEGIN

NS_SWIFT_SENDABLE
@interface PPFix : NSObject

/// Fixes are made by a source inside PippinKit, never by the shell.
- (instancetype)init NS_UNAVAILABLE;

/// Where, and whether that is known at all. A fix with an invalid position is
/// still a fix, since it can carry an altitude and a time, and the consumer
/// decides such a thing is not worth drawing.
@property(nonatomic, readonly) PPGeoPoint coordinate;
@property(nonatomic, readonly) BOOL hasPosition;

/// The receiver's own horizontal error, in metres. What the road snapper
/// sizes its search radius from, by way of the HDOP field `PositionFix`
/// carries.
@property(nonatomic, readonly) double horizontalAccuracyMeters;

/// Height above mean sea level, in metres. CoreLocation's `altitude` is
/// already MSL, so nothing shifts a datum on the way here.
@property(nonatomic, readonly) double altitudeMeters;
@property(nonatomic, readonly) BOOL hasAltitude;

/// Course over ground, degrees clockwise from true north. Absent more often
/// than expected, since a phone standing still reports none, and that is not
/// a gap: `HeadingResolver` derives one from successive positions in screen
/// space, which is the port's normal path.
@property(nonatomic, readonly) double courseDegrees;
@property(nonatomic, readonly) BOOL hasCourse;

/// Ground speed, metres per second.
@property(nonatomic, readonly) double speedMetersPerSecond;
@property(nonatomic, readonly) BOOL hasSpeed;

/// When the receiver stamped it, in epoch seconds UTC. Not when it arrived,
/// which is what makes a recorded ride replayable at the speed it was
/// ridden.
@property(nonatomic, readonly) NSTimeInterval timestamp;
@property(nonatomic, readonly) BOOL hasTimestamp;

@end

NS_ASSUME_NONNULL_END
