// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_web_mercator.h — the slippy-map tile grid (OSM phase O1).
//
// This is the ONLY place the port speaks Web Mercator, and it is deliberately
// small: MVT vertices are tile-local integers, so the exact inverse formula is
// applied per vertex ONCE, on the way in, and every feature then travels the
// same WGS-84 equal-arc pipeline as DNC, ENC and every raster product (plan
// section 6). There is no second projection in the engine and no raster
// warping — that is the whole reason OSM is cheap to add to this renderer.
//
// CONVENTIONS, because two of them are routinely confused:
//
//   * TileId is XYZ (a.k.a. "Google"/slippy): y = 0 is the NORTH edge.
//     MBTiles stores TMS, where row 0 is the SOUTH edge. `TmsRow` converts,
//     and is its own inverse. Everything above the MBTiles reader is XYZ.
//   * The grid is spherical Web Mercator (EPSG:3857): latitudes come from a
//     SPHERE of radius 6378137 m, not the WGS-84 ellipsoid. That is what the
//     tile cutter used, so it is what the inverse must use — treating the
//     result as geodetic latitude is the standard (and universal) practice,
//     and re-projecting it as if it were ellipsoidal would move every vertex
//     by up to ~20 km.
//
// Header-only and free of every other port header except geo.h, so the
// hermetic tests can pin the formulas without a tile, a file or a source.

#ifndef FV_WEB_MERCATOR_H_
#define FV_WEB_MERCATOR_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "fvkit/geo.h"

namespace fv {
namespace webmerc {

// Radius of the sphere EPSG:3857 pretends the Earth is.
constexpr double kEarthRadiusM = 6378137.0;

// Ground resolution at zoom 0 with the customary 256-pixel tile, in metres
// per pixel at the equator: 2*pi*R / 256. Every zoom halves it, and it is
// multiplied by cos(latitude) away from the equator.
constexpr double kMetersPerPixelZ0 = 156543.03392804097;

// The pole the square grid can represent: atan(sinh(pi)) in degrees. A tile
// grid has no rows beyond it, so it is a real clamp, not a rounding guard.
constexpr double kMaxLatitude = 85.0511287798066;

// One tile in the pyramid, XYZ convention.
struct TileId {
  int z = 0;
  int x = 0;
  int y = 0;

  bool operator==(const TileId& o) const {
    return z == o.z && x == o.x && y == o.y;
  }
  bool operator!=(const TileId& o) const { return !(*this == o); }
  // Ordering so tiles can key a std::map / be sorted for a stable scan order.
  bool operator<(const TileId& o) const {
    if (z != o.z) return z < o.z;
    if (x != o.x) return x < o.x;
    return y < o.y;
  }
};

// Tiles per axis at this zoom. int64 because z=31 overflows an int.
inline int64_t TilesPerAxis(int z) { return int64_t{1} << z; }

// MBTiles rows are TMS (row 0 = south); tiles above are XYZ (y 0 = north).
// Self-inverse: TmsRow(z, TmsRow(z, y)) == y.
inline int TmsRow(int z, int y) {
  return static_cast<int>(TilesPerAxis(z) - 1 - y);
}

// --- the exact inverse/forward pair, in fractional tile units --------------
// x_tile and y_tile are allowed to be fractional (a vertex inside a tile) and
// to fall outside [0, 2^z] (MVT tiles carry a buffer of neighbouring
// geometry, and that geometry is legitimately outside the tile's own box).

inline double TileToLon(double x_tile, int z) {
  return x_tile / static_cast<double>(TilesPerAxis(z)) * 360.0 - 180.0;
}

inline double TileToLat(double y_tile, int z) {
  const double n =
      M_PI * (1.0 - 2.0 * y_tile / static_cast<double>(TilesPerAxis(z)));
  return std::atan(std::sinh(n)) * 180.0 / M_PI;
}

inline double LonToTileX(double lon, int z) {
  return (lon + 180.0) / 360.0 * static_cast<double>(TilesPerAxis(z));
}

inline double LatToTileY(double lat, int z) {
  lat = std::max(-kMaxLatitude, std::min(kMaxLatitude, lat));
  const double rad = lat * M_PI / 180.0;
  return (1.0 - std::asinh(std::tan(rad)) / M_PI) / 2.0 *
         static_cast<double>(TilesPerAxis(z));
}

// The tile's own geographic box (its buffer is NOT included).
inline GeoRect TileBounds(const TileId& t) {
  GeoRect r;
  r.ll.lon = TileToLon(t.x, t.z);
  r.ur.lon = TileToLon(t.x + 1, t.z);
  r.ur.lat = TileToLat(t.y, t.z);      // y grows southward
  r.ll.lat = TileToLat(t.y + 1, t.z);
  return r;
}

// Ground resolution (metres per screen pixel) of zoom `z` at `lat`.
inline double MetersPerPixel(int z, double lat) {
  return kMetersPerPixelZ0 * std::cos(lat * M_PI / 180.0) /
         static_cast<double>(TilesPerAxis(z));
}

// --- THE zoom<->scale relation ---------------------------------------------
//
// ONE formula, both directions, used by everything OSM in this port: the
// source picks a tile level with it (O1) and the style loader turns a style
// layer's minzoom/maxzoom into a fvkit ScaleBand with it (O2). Two derivations
// would mean a style layer switching on at a different scale than the tiles it
// styles, which is exactly the kind of half-degree-off symbology nobody finds
// by reading code.
//
//   viewport m/px = scale_denominator * mm_per_pixel / 1000
//   zoom          = log2( 156543.034 * cos(lat) / viewport m/px )
//
// `mm_per_pixel` is the SAME display property MapProjection's SetPhysicalScale
// takes — pass the one the application is using, not a second opinion (E5's
// lesson: one physical property, set in one place).
//
// LATITUDE IS PART OF THE RELATION and there is no way around it: a Web
// Mercator pyramid has a constant scale per PIXEL, not per ground metre, so
// z=12 is 1:270k at the equator and 1:190k off Charleston. A caller that needs
// a single number for a viewport uses that viewport's centre latitude; a style
// engine, which is handed a scale and no geography, needs a reference latitude
// set on it (OsmStyleEngine::SetReferenceLatitude).

// Fractional, unclamped — the raw relation. Returns NaN for a nonsensical
// input rather than a plausible zoom, so a caller has to say what it wants.
inline double ZoomForScaleExact(double scale_denominator, double lat,
                                double mm_per_pixel) {
  if (!(scale_denominator > 0.0) || !(mm_per_pixel > 0.0))
    return std::numeric_limits<double>::quiet_NaN();
  const double viewport_m_per_px = scale_denominator * mm_per_pixel / 1000.0;
  // Same clamp LatToTileY applies, and for the same reason: past the grid's
  // last row there is no tile, and cos() there is close enough to zero that
  // an unclamped ratio walks the answer off the bottom of the pyramid.
  lat = std::max(-kMaxLatitude, std::min(kMaxLatitude, lat));
  const double z0 = kMetersPerPixelZ0 * std::cos(lat * M_PI / 180.0);
  if (!(z0 > 0.0) || !(viewport_m_per_px > 0.0))
    return std::numeric_limits<double>::quiet_NaN();
  const double z = std::log2(z0 / viewport_m_per_px);
  return std::isfinite(z) ? z : std::numeric_limits<double>::quiet_NaN();
}

// The inverse: the cartographic 1:N at which `zoom` renders 1 tile pixel to 1
// screen pixel. Exact round-trip with ZoomForScaleExact at the same latitude
// and pitch (pinned by a test), which is what makes a ScaleBand built from a
// minzoom mean the same thing as the zoom the source read.
inline double ScaleForZoomExact(double zoom, double lat, double mm_per_pixel) {
  if (!(mm_per_pixel > 0.0))
    return std::numeric_limits<double>::quiet_NaN();
  lat = std::max(-kMaxLatitude, std::min(kMaxLatitude, lat));
  const double z0 = kMetersPerPixelZ0 * std::cos(lat * M_PI / 180.0);
  const double viewport_m_per_px = z0 / std::exp2(zoom);
  const double denom = viewport_m_per_px * 1000.0 / mm_per_pixel;
  return std::isfinite(denom) ? denom : std::numeric_limits<double>::quiet_NaN();
}

// Which zoom level a map scale wants (plan section 6), clamped to what the
// file actually holds.
//
// Rounds to the NEAREST zoom rather than flooring: a tile pyramid is
// pre-generalized for a scale RANGE centred on its own, so the nearer level is
// the one whose generalization was authored for this scale.
inline int ZoomForScale(double scale_denominator, double lat,
                        double mm_per_pixel, int min_zoom, int max_zoom) {
  const double z = ZoomForScaleExact(scale_denominator, lat, mm_per_pixel);
  if (!std::isfinite(z)) return max_zoom;
  const int rounded = static_cast<int>(std::lround(z));
  return std::max(min_zoom, std::min(max_zoom, rounded));
}

}  // namespace webmerc
}  // namespace fv

#endif  // FV_WEB_MERCATOR_H_
