// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPTide+Internal.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "fvkit/nav/sun.h"

@implementation PPTideExtreme {
  fv::nav::TideExtreme _e;
}

- (instancetype)initWithExtreme:(const fv::nav::TideExtreme&)e {
  self = [super init];
  if (self != nil) _e = e;
  return self;
}

- (NSTimeInterval)time { return _e.time_s; }
- (double)heightMeters { return _e.height_m; }
- (BOOL)isHigh { return _e.high ? YES : NO; }

- (NSString*)description {
  return [NSString stringWithFormat:@"<PPTideExtreme %@ %.0f %.3fm>",
                                    _e.high ? @"H" : @"L", _e.time_s,
                                    _e.height_m];
}

@end

@implementation PPTideWindow {
  fv::nav::TideWindow _w;
}

- (instancetype)initWithWindow:(const fv::nav::TideWindow&)w {
  self = [super init];
  if (self != nil) _w = w;
  return self;
}

- (NSTimeInterval)begin { return _w.begin_s; }
- (NSTimeInterval)end { return _w.end_s; }

@end

@implementation PPSunEvent {
  fv::nav::SunEvent _e;
}

- (instancetype)initWithEvent:(const fv::nav::SunEvent&)e {
  self = [super init];
  if (self != nil) _e = e;
  return self;
}

- (NSTimeInterval)time { return _e.time_s; }
- (BOOL)isRise { return _e.rise ? YES : NO; }

@end

namespace {

NSString* ToNSString(const std::string& s) {
  return [NSString stringWithUTF8String:s.c_str()] ?: @"";
}

}  // namespace

@implementation PPTide {
  std::shared_ptr<const fv::nav::TideTable> _table;
  double _rideableBelow;
  double _walkEasyBelow;
}

- (instancetype)initWithTable:(std::shared_ptr<const fv::nav::TideTable>)table
          rideableBelowMeters:(double)rideableBelow
          walkEasyBelowMeters:(double)walkEasyBelow {
  self = [super init];
  if (self != nil) {
    _table = std::move(table);
    _rideableBelow = rideableBelow;
    _walkEasyBelow = walkEasyBelow;
  }
  return self;
}

- (NSString*)stationID { return ToNSString(_table->station().id); }
- (NSString*)stationName { return ToNSString(_table->station().name); }
- (NSString*)datum { return ToNSString(_table->datum()); }
- (NSTimeInterval)validFrom { return _table->ValidFrom(); }
- (NSTimeInterval)validUntil { return _table->ValidUntil(); }
- (double)rideableBelowMeters { return _rideableBelow; }
- (double)walkEasyBelowMeters { return _walkEasyBelow; }

- (double)heightAt:(NSTimeInterval)time {
  double h = 0.0;
  return _table->HeightAt(time, &h).ok() ? h : NAN;
}

- (BOOL)isRisingAt:(NSTimeInterval)time {
  fv::nav::TideTrend trend;
  return _table->TrendAt(time, &trend).ok() && trend.rising ? YES : NO;
}

- (NSArray<PPTideExtreme*>*)extremesFrom:(NSTimeInterval)from
                                      to:(NSTimeInterval)to {
  const std::vector<fv::nav::TideExtreme> found = _table->Extremes(from, to);
  NSMutableArray<PPTideExtreme*>* out =
      [NSMutableArray arrayWithCapacity:found.size()];
  for (const auto& e : found)
    [out addObject:[[PPTideExtreme alloc] initWithExtreme:e]];
  return out;
}

- (NSArray<PPTideWindow*>*)windowsBelow:(double)threshold
                                   from:(NSTimeInterval)from
                                     to:(NSTimeInterval)to {
  // TideTable refuses a span that leaves the table; a card straddling the
  // table's end still wants the part that is covered.
  const double t0 = std::max(from, _table->ValidFrom());
  const double t1 = std::min(to, _table->ValidUntil());
  std::vector<fv::nav::TideWindow> found;
  if (t1 < t0 || !_table->WindowsBelow(threshold, t0, t1, &found).ok())
    return @[];
  NSMutableArray<PPTideWindow*>* out =
      [NSMutableArray arrayWithCapacity:found.size()];
  for (const auto& w : found)
    [out addObject:[[PPTideWindow alloc] initWithWindow:w]];
  return out;
}

- (NSArray<PPSunEvent*>*)sunEventsFrom:(NSTimeInterval)from
                                    to:(NSTimeInterval)to {
  const auto& station = _table->station();
  const std::vector<fv::nav::SunEvent> found =
      fv::nav::SunEvents(station.lat, station.lon, from, to);
  NSMutableArray<PPSunEvent*>* out =
      [NSMutableArray arrayWithCapacity:found.size()];
  for (const auto& e : found)
    [out addObject:[[PPSunEvent alloc] initWithEvent:e]];
  return out;
}

- (NSString*)description {
  return [NSString stringWithFormat:@"<PPTide %@ %@ until %.0f>",
                                    self.stationID, self.stationName,
                                    _table->ValidUntil()];
}

@end
