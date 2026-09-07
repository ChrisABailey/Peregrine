// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPFix+Internal.h"

@implementation PPFix {
  PPLocationSample _sample;
}

- (instancetype)initWithSample:(const PPLocationSample&)sample {
  self = [super init];
  if (self == nil) return nil;
  _sample = sample;
  return self;
}

- (const PPLocationSample&)sample { return _sample; }

// Every property below READS the sample rather than storing a second copy of
// the answer, so the sentinel rules live in exactly one place.

- (PPGeoPoint)coordinate {
  return PPGeoPointMake(_sample.latitude, _sample.longitude);
}

- (BOOL)hasPosition { return _sample.horizontal_accuracy_m >= 0.0; }

- (double)horizontalAccuracyMeters { return _sample.horizontal_accuracy_m; }

- (double)altitudeMeters { return _sample.altitude_m; }
- (BOOL)hasAltitude { return _sample.vertical_accuracy_m >= 0.0; }

- (double)courseDegrees { return _sample.course_deg; }
- (BOOL)hasCourse { return _sample.course_deg >= 0.0; }

- (double)speedMetersPerSecond { return _sample.speed_mps; }
- (BOOL)hasSpeed { return _sample.speed_mps >= 0.0; }

- (NSTimeInterval)timestamp { return _sample.timestamp_s; }
- (BOOL)hasTimestamp { return _sample.has_timestamp ? YES : NO; }

- (NSString*)description {
  if (!self.hasPosition) return @"<PPFix no position>";
  return [NSString
      stringWithFormat:@"<PPFix %.6f,%.6f ±%.0fm%@%@>", _sample.latitude,
                       _sample.longitude, _sample.horizontal_accuracy_m,
                       self.hasCourse
                           ? [NSString stringWithFormat:@" %03.0f°",
                                                        _sample.course_deg]
                           : @"",
                       self.hasSpeed
                           ? [NSString stringWithFormat:@" %.1fm/s",
                                                        _sample.speed_mps]
                           : @""];
}

@end
