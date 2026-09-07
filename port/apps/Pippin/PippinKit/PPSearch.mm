// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPSearch+Internal.h"

#include <string>

NSString* PPSearchKindWord(PPSearchKind kind) {
  switch (kind) {
    case PPSearchKindPoint:
      return @"Point";
    case PPSearchKindRoad:
      return @"Road";
    case PPSearchKindPoi:
      return @"POI";
  }
  return @"POI";
}

namespace {

NSString* Str(const std::string& s) {
  // `PPPoint.mm`'s rule, restated where it is used: a string that is not valid
  // UTF-8 comes back nil, and a nil in a `copy` property is a crash three
  // screens later. Empty is what a missing label already means.
  NSString* out = [NSString stringWithUTF8String:s.c_str()];
  return out != nil ? out : @"";
}

PPSearchKind KindOf(pippin::SearchKind k) {
  switch (k) {
    case pippin::SearchKind::kPoint:
      return PPSearchKindPoint;
    case pippin::SearchKind::kRoad:
      return PPSearchKindRoad;
    case pippin::SearchKind::kPoi:
      return PPSearchKindPoi;
  }
  return PPSearchKindPoi;
}

}  // namespace

@implementation PPSearchResult

- (instancetype)initWithRow:(const pippin::SearchRow&)row {
  self = [super init];
  if (self == nil) return nil;
  _title = Str(row.title);
  _kind = KindOf(row.kind);
  _kindWord = PPSearchKindWord(_kind);
  _coordinate = PPGeoPointMake(row.position.lat, row.position.lon);
  _bounds.southWest = PPGeoPointMake(row.bounds.ll.lat, row.bounds.ll.lon);
  _bounds.northEast = PPGeoPointMake(row.bounds.ur.lat, row.bounds.ur.lon);
  // Strictly greater on either axis, so a box degenerate in one direction
  // only — a road running exactly north can come back with equal longitudes —
  // still counts as having extent and is framed rather than only centred.
  _hasExtent = (row.bounds.ur.lat > row.bounds.ll.lat ||
                row.bounds.ur.lon > row.bounds.ll.lon)
                   ? YES
                   : NO;
  _distanceMeters = row.distance_m;
  return self;
}

@end
