// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPWind+Internal.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>

@implementation PPWindSample {
  fv::nav::WindSample _s;
}

- (instancetype)initWithSample:(const fv::nav::WindSample&)s {
  self = [super init];
  if (self != nil) _s = s;
  return self;
}

- (double)speedMetersPerSecond { return _s.speed_mps; }
- (double)gustMetersPerSecond { return _s.gust_mps; }
- (double)fromDegrees { return _s.from_deg; }

- (double)tailwindAlongHeading:(double)headingDegrees {
  return fv::nav::TailwindComponent(_s, headingDegrees);
}

- (double)onshoreForSeaward:(double)seawardDegrees {
  return fv::nav::OnshoreComponent(_s, seawardDegrees);
}

@end

@implementation PPWindForecast {
  std::shared_ptr<const fv::nav::WindForecast> _forecast;
}

- (nullable instancetype)initWithJSON:(NSData*)json
                                error:(NSError* _Nullable* _Nullable)error {
  self = [super init];
  if (self == nil) return nil;
  auto forecast = std::make_shared<fv::nav::WindForecast>();
  const std::string text(static_cast<const char*>(json.bytes), json.length);
  const fv::Status s = forecast->Parse(text);
  if (!s.ok()) {
    if (error != nullptr) {
      *error = [NSError errorWithDomain:@"org.peregrine.PippinKit.wind"
                                   code:s.code
                               userInfo:@{
                                 NSLocalizedDescriptionKey :
                                     [NSString stringWithUTF8String:s.message.c_str()] ?: @""
                               }];
    }
    return nil;
  }
  _forecast = std::move(forecast);
  return self;
}

- (NSTimeInterval)updateTime { return _forecast->UpdateTime(); }
- (NSTimeInterval)validUntil { return _forecast->ValidUntil(); }

- (nullable PPWindSample*)sampleAt:(NSTimeInterval)time {
  fv::nav::WindSample s;
  if (!_forecast->At(time, &s).ok()) return nil;
  return [[PPWindSample alloc] initWithSample:s];
}

@end

@implementation PPWindSettings

- (instancetype)initWithLatitude:(double)latitude
                       longitude:(double)longitude
               beachFacesDegrees:(double)faces
      onshoreWarnMetersPerSecond:(double)onshoreWarn
     headwindWarnMetersPerSecond:(double)headwindWarn {
  self = [super init];
  if (self != nil) {
    _latitude = latitude;
    _longitude = longitude;
    _beachFacesDegrees = faces;
    _onshoreWarnMetersPerSecond = onshoreWarn;
    _headwindWarnMetersPerSecond = headwindWarn;
  }
  return self;
}

- (double)seawardForHeading:(double)headingDegrees {
  if (std::isnan(_beachFacesDegrees) || std::isnan(headingDegrees)) return NAN;
  return fv::nav::SeawardOf(headingDegrees, _beachFacesDegrees);
}

@end
