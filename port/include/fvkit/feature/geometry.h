// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/geometry.h — OGC Simple Features geometry and its WKB form
/// (port/feature-overlay-plan.md §1–2).
///
/// Coordinates are WGS-84 degrees in lat, lon order (contracts D2); WKB and
/// GeoJSON store x = lon, y = lat, and the codecs swap at the boundary. Z and M
/// are carried when the source has them so a round-trip loses neither.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fvkit/geo.h"

namespace fv {

/// The ISO 19125 type codes, as WKB spells them. `kGeometry` (0) is "any
/// type" in a layer schema and "no geometry" on a feature.
enum class GeometryType : int {
  kGeometry = 0,
  kPoint = 1,
  kLineString = 2,
  kPolygon = 3,
  kMultiPoint = 4,
  kMultiLineString = 5,
  kMultiPolygon = 6,
  kGeometryCollection = 7,
};

/// "POINT", "LINESTRING", ... as GeoPackage's `geometry_type_name` spells them.
const char* GeometryTypeName(GeometryType type);
/// Case-insensitive inverse of `GeometryTypeName`; false for an unknown name.
bool GeometryTypeFromName(const char* name, GeometryType* out);

/// One vertex. `z` and `m` are meaningful only when the geometry's
/// `has_z`/`has_m` says so.
struct Position {
  double lat = 0.0;
  double lon = 0.0;
  double z = 0.0;
  double m = 0.0;

  GeoPoint point() const { return GeoPoint{lat, lon}; }
  static Position At(const GeoPoint& p) { return Position{p.lat, p.lon, 0.0, 0.0}; }
};

using Path = std::vector<Position>;

/// A Simple Features geometry. A default-constructed one is null (no geometry).
///
/// - Point: one path of one position; an empty point has no paths.
/// - LineString: one path.
/// - Polygon: rings, exterior first, each closed (first == last).
/// - Multi* and GeometryCollection: `parts`, each a geometry of its own.
struct Geometry {
  GeometryType type = GeometryType::kGeometry;
  bool has_z = false;
  bool has_m = false;
  std::vector<Path> paths;
  std::vector<Geometry> parts;

  static Geometry Point(const GeoPoint& p);
  static Geometry LineString(const std::vector<GeoPoint>& points);
  /// `rings[0]` is the exterior; each ring is closed here if it is not already.
  static Geometry Polygon(const std::vector<std::vector<GeoPoint>>& rings);
  /// A Multi* or collection of `parts`; `type` must be one of the four.
  static Geometry Collection(GeometryType type, std::vector<Geometry> parts);

  bool IsNull() const { return type == GeometryType::kGeometry; }
  /// True for null and for a geometry with no positions anywhere in it.
  bool IsEmpty() const;
  /// Every position in the geometry, depth first.
  size_t PositionCount() const;

  /// The lat/lon bounding box. False when the geometry is empty. Longitudes
  /// are taken as stored: a line crossing ±180 is not unwrapped, so its box
  /// spans the world.
  bool Envelope(GeoRect* out) const;

  bool operator==(const Geometry& other) const;
  bool operator!=(const Geometry& other) const { return !(*this == other); }
};

/// Decodes one WKB geometry (ISO or EWKB-style Z/M flags, either byte order).
/// `consumed`, when set, receives the bytes read. A NaN point is the empty
/// point, as GeoPackage writes it. kInvalidArg for a truncated, oversized or
/// unknown encoding.
Status ReadWkb(const uint8_t* data, size_t size, Geometry* out, size_t* consumed = nullptr);

/// Encodes ISO WKB, little-endian, with Z/M codes (1000/2000/3000) when the
/// geometry has them. A null geometry has no WKB: kInvalidArg.
Status WriteWkb(const Geometry& geometry, std::vector<uint8_t>* out);

}  // namespace fv
