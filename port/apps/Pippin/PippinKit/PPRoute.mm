// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPRoute.h"

#import "PPRoute+Internal.h"

#include <string>

namespace {

// `nil` and an empty string are the same thing to every consumer of these
// properties, so the boundary spells it one way: never nil.
NSString* Str(const std::string& s) {
  NSString* out = [NSString stringWithUTF8String:s.c_str()];
  return out != nil ? out : @"";
}

std::string Cxx(NSString* _Nullable s) {
  return s != nil ? std::string(s.UTF8String) : std::string();
}

}  // namespace

@implementation PPWaypoint

- (instancetype)initWithLabel:(NSString*)label coordinate:(PPGeoPoint)coordinate {
  self = [super init];
  if (self == nil) return nil;
  _label = [label copy] ?: @"";
  _coordinate = coordinate;
  return self;
}

- (NSString*)description {
  return [NSString stringWithFormat:@"<PPWaypoint %@ %.5f,%.5f>", _label,
                                    _coordinate.latitude, _coordinate.longitude];
}

@end

@implementation PPRoute

- (instancetype)initWithSnapshot:(const pippin::RouteSnapshot&)snapshot
                planMilliseconds:(double)planMilliseconds {
  self = [super init];
  if (self == nil) return nil;

  NSMutableArray<PPWaypoint*>* points =
      [NSMutableArray arrayWithCapacity:snapshot.waypoints.size()];
  for (const fv::RouteWaypoint& w : snapshot.waypoints) {
    [points addObject:[[PPWaypoint alloc]
                          initWithLabel:Str(w.label)
                             coordinate:PPGeoPointMake(w.position.lat,
                                                       w.position.lon)]];
  }

  _exists = snapshot.exists ? YES : NO;
  _waypoints = [points copy];
  _profile = Str(snapshot.profile);
  _isCalculated = snapshot.calculated ? YES : NO;
  _isComplete = snapshot.complete ? YES : NO;
  _straightLegs = (NSInteger)snapshot.straight_legs;
  _isBicycle = snapshot.is_bicycle ? YES : NO;
  _lengthMeters = snapshot.length_m;
  _seconds = snapshot.seconds;
  _statusText = Str(snapshot.status);
  _planMilliseconds = planMilliseconds;
  return self;
}

- (NSString*)description {
  return [NSString
      stringWithFormat:@"<PPRoute %lu wp %@ %@ %.0f m>",
                       (unsigned long)_waypoints.count, _profile,
                       _isCalculated ? @"calculated" : @"straight", _lengthMeters];
}

@end

@implementation PPPlace

- (instancetype)initWithDescription:(const pippin::PlaceDescription&)place {
  self = [super init];
  if (self == nil) return nil;
  _isUsable = place.usable ? YES : NO;
  _name = Str(place.name);
  _isNamed = place.named ? YES : NO;
  _distanceMeters = place.distance_m;
  return self;
}

- (NSString*)description {
  return _isUsable ? [NSString stringWithFormat:@"<PPPlace %@ %.0f m>", _name,
                                                _distanceMeters]
                   : @"<PPPlace nothing usable>";
}

@end

@implementation PPSnapTarget

- (instancetype)initWithItem:(const fv::app::SnapToItem&)item
              distancePoints:(double)distancePoints {
  self = [super init];
  if (self == nil) return nil;
  _coordinate = PPGeoPointMake(item.point.lat, item.point.lon);
  _name = Str(item.description);
  _distancePoints = distancePoints;
  return self;
}

- (NSString*)description {
  return [NSString stringWithFormat:@"<PPSnapTarget %@ %.1f pt>", _name,
                                    _distancePoints];
}

@end

std::vector<fv::RouteWaypoint> PPWaypointsToRoute(
    NSArray<PPWaypoint*>* waypoints) {
  std::vector<fv::RouteWaypoint> out;
  out.reserve(waypoints.count);
  for (PPWaypoint* w in waypoints) {
    fv::RouteWaypoint r;
    r.label = Cxx(w.label);
    r.position = fv::GeoPoint{w.coordinate.latitude, w.coordinate.longitude};
    out.push_back(std::move(r));
  }
  return out;
}
