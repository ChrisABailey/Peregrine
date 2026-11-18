// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/web/tiles.h — the Web-Mercator tile grid a remote layer
/// fetches by (port/web-features-plan.md WF-D7).
#pragma once

#include <cstdint>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace web {

/// Latitude limit of the Web-Mercator grid, degrees.
constexpr double kTileMaxLat = 85.0511287798066;

/// One tile: zoom, column from the antimeridian eastward, row from the north.
struct TileKey {
  int z = 0;
  int x = 0;
  int y = 0;

  bool operator==(const TileKey& o) const { return z == o.z && x == o.x && y == o.y; }
  bool operator!=(const TileKey& o) const { return !(*this == o); }
  bool operator<(const TileKey& o) const {
    if (z != o.z) return z < o.z;
    if (x != o.x) return x < o.x;
    return y < o.y;
  }
};

/// The tile's box in WGS 84 degrees.
GeoRect TileBounds(const TileKey& t);
/// The tile one zoom out holding `t`; `t` itself at zoom 0.
TileKey TileParent(const TileKey& t);
/// The four tiles one zoom in, north-west first, row by row.
std::vector<TileKey> TileChildren(const TileKey& t);

/// The zoom at which a tile is about half as wide as `box`, clamped to
/// [min_zoom, max_zoom]. A box with no width gets `max_zoom`.
int TileZoomForBox(const GeoRect& box, int min_zoom, int max_zoom);
/// The tiles at zoom `z` that `box` touches; a box crossing ±180 covers both
/// sides. Latitudes beyond `kTileMaxLat` are clamped to it.
std::vector<TileKey> TilesCovering(const GeoRect& box, int z);

}  // namespace web
}  // namespace fv
