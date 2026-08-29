// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/geo.h — FvKit L0 geographic primitives and error type.
// Contracts: port/fvkit-contracts.md (D2 geo conventions, D3 error model).
//
// All coordinates are WGS-84 geodetic decimal degrees, lat before lon,
// canonical ranges lat in [-90, +90], lon in (-180, +180] (the antimeridian
// is +180). A GeoRect with ll.lon > ur.lon crosses the antimeridian
// (FalconView's own convention). Poles never wrap: ll.lat <= ur.lat always.

#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace fv {

// ---------------------------------------------------------------------------
// Status (D3): fallible FvKit operations return Status; data comes back
// through pointer out-params. code 0 = success, FvKit errors are negative;
// legacy codes (HRESULT, decoder ints) are preserved in `message`, not code.
// ---------------------------------------------------------------------------

enum StatusCode {
  kOk = 0,
  kInvalidArg = -1,
  kNotFound = -2,
  kIoError = -3,
  kUnsupported = -4,
  kOutOfCoverage = -5,
  kInterrupted = -6,
  kInternal = -100,
};

struct Status {
  int code = kOk;
  std::string message;

  bool ok() const { return code == kOk; }
  static Status Ok() { return Status{}; }
  static Status Error(int c, std::string msg) { return Status{c, std::move(msg)}; }
};

// ---------------------------------------------------------------------------
// Pixel primitives (D4): x = column rightward, y = row downward, origin at
// the image's top-left.
// ---------------------------------------------------------------------------

struct PixelPoint {
  int x = 0;
  int y = 0;
};

struct PixelSize {
  int width = 0;
  int height = 0;
};

struct PixelRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// A SUB-PIXEL surface coordinate, same axes and origin as PixelPoint. It is
// what everything between "project a geographic point" and "round it for the
// canvas" carries: a projected vertex, a placer's stamp position, a clipped
// polyline vertex.
//
// Lives here rather than in fvkit/vector/renderer.h (where it was declared
// through G1) because it is a D4 pixel primitive and nothing about it is
// vector-specific — fvkit/geo/contour.h needs it and has no business pulling
// in the whole vector seam to get it.
struct SurfacePoint {
  double x = 0.0;
  double y = 0.0;
};

// ---------------------------------------------------------------------------
// GeoPoint / GeoRect (D2)
// ---------------------------------------------------------------------------

// Maps an arbitrary longitude into the canonical (-180, +180] range;
// +/-180 normalizes to +180.
inline double NormalizeLon(double lon) {
  lon = std::fmod(lon + 180.0, 360.0);
  if (lon <= 0.0) lon += 360.0;
  return lon - 180.0;
}

struct GeoPoint {
  double lat = 0.0;
  double lon = 0.0;

  // Canonicalize: lon into (-180, +180], lat clamped to [-90, +90].
  void Normalize() {
    lon = NormalizeLon(lon);
    if (lat > 90.0) lat = 90.0;
    if (lat < -90.0) lat = -90.0;
  }
};

// Edges are stored as-is; ll.lon > ur.lon means the rect crosses the
// antimeridian. No rect spans >= 360 degrees of longitude except the
// explicit World() rect. All edge comparisons are inclusive (touching
// rects intersect).
struct GeoRect {
  GeoPoint ll;  // south-west corner
  GeoPoint ur;  // north-east corner

  static GeoRect World() { return GeoRect{{-90.0, -180.0}, {90.0, 180.0}}; }

  bool CrossesAntimeridian() const { return ll.lon > ur.lon; }

  bool Contains(const GeoPoint& p) const {
    if (p.lat < ll.lat || p.lat > ur.lat) return false;
    if (CrossesAntimeridian()) return p.lon >= ll.lon || p.lon <= ur.lon;
    return p.lon >= ll.lon && p.lon <= ur.lon;
  }

  // Splits a crossing rect into two non-crossing boxes at +/-180. This is
  // what feeds R-tree insertion/queries (contracts D2): the catalog stores
  // 1-2 boxes per coverage rect and de-duplicates query hits by row id.
  std::vector<GeoRect> SplitAtAntimeridian() const {
    if (!CrossesAntimeridian()) return {*this};
    return {GeoRect{ll, {ur.lat, 180.0}}, GeoRect{{ll.lat, -180.0}, ur}};
  }

  bool Intersects(const GeoRect& other) const {
    for (const GeoRect& a : SplitAtAntimeridian())
      for (const GeoRect& b : other.SplitAtAntimeridian())
        if (a.ll.lat <= b.ur.lat && b.ll.lat <= a.ur.lat &&
            a.ll.lon <= b.ur.lon && b.ll.lon <= a.ur.lon)
          return true;
    return false;
  }
};

}  // namespace fv
