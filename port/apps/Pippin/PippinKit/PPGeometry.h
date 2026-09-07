// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPGeometry.h — the two geographic value types the whole bridge speaks.
//
// Their own header rather than `PPMap.h`'s, because the geometry happens in
// `PPViewport` and a viewport that imports the map to name a position has the
// dependency backwards.
//
// Deliberately not `CLLocationCoordinate2D`: nothing below `PPLocationSource`
// needs CoreLocation, and one class knowing iOS has a location API is better
// than the whole framework knowing it.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// A WGS-84 position, in degrees.
typedef struct PPGeoPoint {
  double latitude;
  double longitude;
} PPGeoPoint;

/// A WGS-84 box, south-west and north-east corners.
typedef struct PPGeoBounds {
  PPGeoPoint southWest;
  PPGeoPoint northEast;
} PPGeoBounds;

static inline PPGeoPoint PPGeoPointMake(double latitude, double longitude) {
  PPGeoPoint p;
  p.latitude = latitude;
  p.longitude = longitude;
  return p;
}

NS_ASSUME_NONNULL_END
