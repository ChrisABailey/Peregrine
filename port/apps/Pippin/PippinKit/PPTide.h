// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPTide.h — the pack's tide table, as `fv::nav::TideTable` answers it.
//
// Times are epoch seconds and heights are metres above the table's datum;
// the shell formats both. Nothing outside [validFrom, validUntil] is
// answered: heights there are NaN and lists are clipped to the valid span.
// The table is read once when the pack opens and never changes, so a PPTide
// may be read from any thread.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// One predicted high or low water.
NS_SWIFT_SENDABLE
@interface PPTideExtreme : NSObject
- (instancetype)init NS_UNAVAILABLE;
@property(nonatomic, readonly) NSTimeInterval time;
@property(nonatomic, readonly) double heightMeters;
@property(nonatomic, readonly) BOOL isHigh;
@end

/// A span of time, [begin, end], in epoch seconds.
NS_SWIFT_SENDABLE
@interface PPTideWindow : NSObject
- (instancetype)init NS_UNAVAILABLE;
@property(nonatomic, readonly) NSTimeInterval begin;
@property(nonatomic, readonly) NSTimeInterval end;
@end

/// One sunrise or sunset.
NS_SWIFT_SENDABLE
@interface PPSunEvent : NSObject
- (instancetype)init NS_UNAVAILABLE;
@property(nonatomic, readonly) NSTimeInterval time;
@property(nonatomic, readonly) BOOL isRise;
@end

/// A station's predicted tide, and the height below which the beach rides.
NS_SWIFT_SENDABLE
@interface PPTide : NSObject

/// Made by the map from the pack's `[tides]` section, never by the shell.
- (instancetype)init NS_UNAVAILABLE;

/// NOAA's station id and name, e.g. "8667062", "Kiawah River Bridge".
@property(nonatomic, readonly, copy) NSString *stationID;
@property(nonatomic, readonly, copy) NSString *stationName;
/// The vertical datum heights are measured from, e.g. "MLLW".
@property(nonatomic, readonly, copy) NSString *datum;

@property(nonatomic, readonly) NSTimeInterval validFrom;
@property(nonatomic, readonly) NSTimeInterval validUntil;

/// `beach.rideable_below_m` from `pippin.ini`, metres above the datum.
@property(nonatomic, readonly) double rideableBelowMeters;

/// `beach.walk_good_below_m`: the height below which the beach is easy
/// walking. Walking is not ruled out above it, only harder.
@property(nonatomic, readonly) double walkEasyBelowMeters;

/// The predicted height at `time`, or NaN outside the valid span.
- (double)heightAt:(NSTimeInterval)time NS_SWIFT_NAME(height(at:));

/// Whether the water is rising at `time`; NO outside the valid span. At an
/// extreme it is the trend that follows it.
- (BOOL)isRisingAt:(NSTimeInterval)time NS_SWIFT_NAME(isRising(at:));

/// The extremes with `from <= time <= to`, clipped to the valid span.
- (NSArray<PPTideExtreme *> *)extremesFrom:(NSTimeInterval)from
                                        to:(NSTimeInterval)to;

/// The spans within [from, to] where the water is at or below `threshold`
/// metres, solved exactly. [from, to] is first clipped to the valid span;
/// the result is empty when nothing of it is left.
- (NSArray<PPTideWindow *> *)windowsBelow:(double)threshold
                                     from:(NSTimeInterval)from
                                       to:(NSTimeInterval)to;

/// Sunrises and sunsets at the station with `from <= time <= to`, in time
/// order. Computed, not tabled, so not clipped to the valid span.
- (NSArray<PPSunEvent *> *)sunEventsFrom:(NSTimeInterval)from
                                      to:(NSTimeInterval)to;

@end

NS_ASSUME_NONNULL_END
