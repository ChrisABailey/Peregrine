// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPTrip+Internal.h"

@implementation PPTrip {
  fv::TripStats _stats;
}

- (instancetype)initWithStats:(const fv::TripStats&)stats {
  self = [super init];
  if (self != nil) _stats = stats;
  return self;
}

- (NSTimeInterval)elapsedSeconds { return _stats.elapsed_s; }
- (BOOL)hasElapsed { return _stats.has_elapsed ? YES : NO; }

- (double)odometerMeters { return _stats.odometer_m; }
- (BOOL)hasOdometer { return _stats.has_odometer ? YES : NO; }

- (double)speedMetersPerSecond { return _stats.speed_mps; }
- (BOOL)hasSpeed { return _stats.has_speed ? YES : NO; }

- (double)remainingMeters { return _stats.remaining_m; }
- (BOOL)hasRemaining { return _stats.has_remaining ? YES : NO; }

- (NSTimeInterval)etaTimestamp { return _stats.eta_epoch_s; }
- (BOOL)hasEta { return _stats.has_eta ? YES : NO; }

- (double)averageSpeedMetersPerSecond { return _stats.average_speed_mps; }
- (BOOL)hasAverageSpeed { return _stats.has_average_speed ? YES : NO; }

- (NSString*)description {
  if (!_stats.has_elapsed) return @"<PPTrip not started>";
  return [NSString
      stringWithFormat:@"<PPTrip %.0fs %.0fm%@%@>", _stats.elapsed_s,
                       _stats.odometer_m,
                       _stats.has_speed ? [NSString stringWithFormat:@" %.1fm/s",
                                                                    _stats.speed_mps]
                                        : @"",
                       _stats.has_remaining
                           ? [NSString stringWithFormat:@" %.0fm to go",
                                                        _stats.remaining_m]
                           : @""];
}

@end
