// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Web-Mercator tile arithmetic for remote feature layers.

#include "fvkit/feature/web/tiles.h"

#include <algorithm>
#include <cmath>

namespace fv {
namespace web {
namespace {

constexpr double kPi = 3.14159265358979323846;

double TileLon(int x, int z) { return x / std::ldexp(1.0, z) * 360.0 - 180.0; }

double TileLat(int y, int z) {
  double n = kPi * (1.0 - 2.0 * y / std::ldexp(1.0, z));
  return std::atan(std::sinh(n)) * 180.0 / kPi;
}

int ColumnOf(double lon, int z) {
  int count = 1 << z;
  int x = static_cast<int>(std::floor((lon + 180.0) / 360.0 * count));
  return std::clamp(x, 0, count - 1);
}

int RowOf(double lat, int z) {
  int count = 1 << z;
  double r = std::clamp(lat, -kTileMaxLat, kTileMaxLat) * kPi / 180.0;
  double t = (1.0 - std::asinh(std::tan(r)) / kPi) / 2.0;
  int y = static_cast<int>(std::floor(t * count));
  return std::clamp(y, 0, count - 1);
}

void AppendCovering(double west, double south, double east, double north, int z,
                    std::vector<TileKey>* out) {
  int x0 = ColumnOf(west, z), x1 = ColumnOf(east, z);
  int y0 = RowOf(north, z), y1 = RowOf(south, z);
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) out->push_back(TileKey{z, x, y});
}

}  // namespace

GeoRect TileBounds(const TileKey& t) {
  GeoRect r;
  r.ll = GeoPoint{TileLat(t.y + 1, t.z), TileLon(t.x, t.z)};
  r.ur = GeoPoint{TileLat(t.y, t.z), TileLon(t.x + 1, t.z)};
  return r;
}

TileKey TileParent(const TileKey& t) {
  if (t.z == 0) return t;
  return TileKey{t.z - 1, t.x / 2, t.y / 2};
}

std::vector<TileKey> TileChildren(const TileKey& t) {
  int z = t.z + 1, x = t.x * 2, y = t.y * 2;
  return {TileKey{z, x, y}, TileKey{z, x + 1, y}, TileKey{z, x, y + 1}, TileKey{z, x + 1, y + 1}};
}

int TileZoomForBox(const GeoRect& box, int min_zoom, int max_zoom) {
  double span = box.ur.lon - box.ll.lon;
  if (box.CrossesAntimeridian()) span += 360.0;
  if (!(span > 0.0)) return max_zoom;
  int z = static_cast<int>(std::lround(std::log2(720.0 / span)));
  return std::clamp(z, min_zoom, max_zoom);
}

std::vector<TileKey> TilesCovering(const GeoRect& box, int z) {
  std::vector<TileKey> out;
  if (box.CrossesAntimeridian()) {
    AppendCovering(box.ll.lon, box.ll.lat, 180.0, box.ur.lat, z, &out);
    AppendCovering(-180.0, box.ll.lat, box.ur.lon, box.ur.lat, z, &out);
  } else {
    AppendCovering(box.ll.lon, box.ll.lat, box.ur.lon, box.ur.lat, z, &out);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace web
}  // namespace fv
