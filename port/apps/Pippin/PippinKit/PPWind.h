// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPWind.h — the NWS wind forecast, as `fv::nav::WindForecast` reads it, and
// the pack's `[wind]` settings.
//
// Speeds are metres per second; directions are degrees true the wind blows
// from; times are epoch seconds. The shell fetches the document; this parses
// it and answers questions of it. Immutable once made, so readable from any
// thread.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// The forecast wind at one time. `gustMetersPerSecond` is NaN when the
/// forecast has no gust for that hour.
NS_SWIFT_SENDABLE
@interface PPWindSample : NSObject
- (instancetype)init NS_UNAVAILABLE;
@property(nonatomic, readonly) double speedMetersPerSecond;
@property(nonatomic, readonly) double gustMetersPerSecond;
@property(nonatomic, readonly) double fromDegrees;

/// The component along `headingDegrees`: positive behind the rider,
/// negative in their face.
- (double)tailwindAlongHeading:(double)headingDegrees NS_SWIFT_NAME(tailwind(heading:));

/// The component blowing onto a shore whose seaward normal is
/// `seawardDegrees`: positive onshore.
- (double)onshoreForSeaward:(double)seawardDegrees NS_SWIFT_NAME(onshore(seaward:));
@end

/// An api.weather.gov gridpoint forecast.
NS_SWIFT_SENDABLE
@interface PPWindForecast : NSObject
- (instancetype)init NS_UNAVAILABLE;

/// Parses a gridpoint document; nil, with `error` set, when it cannot.
- (nullable instancetype)initWithJSON:(NSData *)json
                                error:(NSError *_Nullable *_Nullable)error;

/// When NWS last updated the forecast; 0 when the document omits it.
@property(nonatomic, readonly) NSTimeInterval updateTime;
/// The first instant the forecast no longer covers.
@property(nonatomic, readonly) NSTimeInterval validUntil;

/// The wind at `time`, or nil outside the forecast.
- (nullable PPWindSample *)sampleAt:(NSTimeInterval)time NS_SWIFT_NAME(sample(at:));
@end

/// The pack's `[wind]` section and the beach's facing from `[beach]`.
NS_SWIFT_SENDABLE
@interface PPWindSettings : NSObject
- (instancetype)init NS_UNAVAILABLE;

/// The point the forecast is asked for: a spot on the pack's beach, never the
/// rider's position.
@property(nonatomic, readonly) double latitude;
@property(nonatomic, readonly) double longitude;

/// `beach.faces_deg`: the direction the beach faces out to sea, degrees true;
/// NaN when the pack does not say.
@property(nonatomic, readonly) double beachFacesDegrees;

/// Sustained onshore component at or above which the water may run higher
/// than the prediction.
@property(nonatomic, readonly) double onshoreWarnMetersPerSecond;
/// Headwind component on a beach stretch at or above which the sheet warns.
@property(nonatomic, readonly) double headwindWarnMetersPerSecond;

/// The seaward normal of a stretch run along `headingDegrees`: whichever
/// perpendicular lies nearer `beachFacesDegrees`. NaN when the facing is
/// unknown.
- (double)seawardForHeading:(double)headingDegrees NS_SWIFT_NAME(seaward(heading:));
@end

NS_ASSUME_NONNULL_END
