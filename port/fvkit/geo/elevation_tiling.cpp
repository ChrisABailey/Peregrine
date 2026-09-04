// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The shared terrain lattice — see fvkit/geo/elevation_tiling.h. Extracted
// verbatim from contour_overlay.cpp (plan TA1); the comments explaining WHY
// each rule is what it is came with it.

#include "fvkit/geo/elevation_tiling.h"

#include <algorithm>
#include <cmath>

namespace fv {

// The tile lattice, in whole fractions of a degree so that no tile straddles
// the antimeridian and so a tile's edge posts land on round numbers.
//
// THE SIZE IS CHOSEN FROM THE VIEWPORT FIRST, and this is where the port
// stops copying. FalconView keyed it on the DTED level alone -- 0.2 degrees
// for level 1, 0.1 for level 2, 0.05 for level 3 -- which is fine at the
// display threshold and absurd when zoomed in: at 1:24 K a 0.2-degree tile is
// SIXTY TIMES the area on screen, so the first frame after a pan pays to
// sample and trace 58,000 posts to draw about a thousand of them (measured,
// 2026-08-29). Here the step is the smallest one that still covers the
// viewport, so a screenful is one to four tiles at every zoom, and the cache
// still holds whole tiles that survive a pan.
//
// The post cap is the second half: a tile must not be so finely sampled that
// one of them is a whole frame's work, and not so coarsely that the lattice
// costs more in bookkeeping than it saves.
double ElevationTiling::TileSizeFor(double sample_deg, double view_span_deg) {
  static const double kLadder[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0};
  const int kN = sizeof(kLadder) / sizeof(kLadder[0]);
  const int kMaxPostsPerAxis = 512;
  const int kMinPostsPerAxis = 32;

  int i = kN - 1;
  for (int k = 0; k < kN; ++k) {
    if (kLadder[k] >= view_span_deg) {
      i = k;
      break;
    }
  }
  if (sample_deg > 0.0) {
    while (i > 0 && kLadder[i] / sample_deg > kMaxPostsPerAxis) --i;
    while (i < kN - 1 && kLadder[i] / sample_deg < kMinPostsPerAxis) ++i;
  }
  return kLadder[i];
}

void ElevationTiling::Reset() {
  sample_lat_ = sample_lon_ = 0.0;
  tile_deg_ = 0.0;
}

bool ElevationTiling::Adopt(const MapProjection& proj, IElevationSource* src,
                            bool* invalidated) {
  if (invalidated != nullptr) *invalidated = false;
  if (!proj.Ready()) return false;

  // Four screen pixels between posts, or the data's own spacing, whichever is
  // coarser. Anything finer than the posts is interpolation dressed up as
  // terrain.
  double want_lat = 4.0 * proj.DegPerPixelLat();
  double want_lon = 4.0 * proj.DegPerPixelLon();
  double post_lat = 0.0, post_lon = 0.0;
  if (src != nullptr && src->PostSpacing(proj.Center(), &post_lat, &post_lon)) {
    want_lat = std::max(want_lat, post_lat);
    want_lon = std::max(want_lon, post_lon);
  }
  if (!(want_lat > 0.0) || !(want_lon > 0.0)) return false;

  // FalconView's hysteresis: nothing moves until the demand has moved by more
  // than a third, so a zoom nudge does not throw away every traced tile.
  const bool moved = sample_lat_ <= 0.0 || sample_lon_ <= 0.0 ||
                     std::fabs(sample_lat_ - want_lat) > sample_lat_ / 3.0 ||
                     std::fabs(sample_lon_ - want_lon) > sample_lon_ / 3.0;
  if (moved) {
    sample_lat_ = want_lat;
    sample_lon_ = want_lon;
    if (invalidated != nullptr) *invalidated = true;
  }

  const GeoRect view = proj.VmapBounds();
  double lon_span = view.ur.lon - view.ll.lon;
  if (lon_span < 0.0) lon_span += 360.0;
  const double tile =
      TileSizeFor(sample_lat_, std::max(view.ur.lat - view.ll.lat, lon_span));
  if (tile != tile_deg_) {
    tile_deg_ = tile;
    if (invalidated != nullptr) *invalidated = true;
  }
  return true;
}

int ElevationTiling::PostsX() const {
  if (sample_lon_ <= 0.0) return 2;
  return std::max(2, std::min(2048, static_cast<int>(std::lround(
                                        tile_deg_ / sample_lon_)) + 1));
}

int ElevationTiling::PostsY() const {
  if (sample_lat_ <= 0.0) return 2;
  return std::max(2, std::min(2048, static_cast<int>(std::lround(
                                        tile_deg_ / sample_lat_)) + 1));
}

bool ElevationTiling::Valid(TileIndex t) const {
  if (tile_deg_ <= 0.0) return false;
  const double south = t.lat * tile_deg_;
  return south >= -90.0 && south + tile_deg_ <= 90.0;
}

GeoRect ElevationTiling::BoundsOf(TileIndex t) const {
  const double south = t.lat * tile_deg_;
  const double west = t.lon * tile_deg_;
  return GeoRect{{south, west}, {south + tile_deg_, west + tile_deg_}};
}

std::vector<TileIndex> ElevationTiling::VisibleCells(
    const MapProjection& proj) const {
  std::vector<TileIndex> out;
  if (tile_deg_ <= 0.0 || !proj.Ready()) return out;

  const GeoRect view = proj.VmapBounds();
  const int lat0 = static_cast<int>(std::floor(view.ll.lat / tile_deg_));
  const int lat1 = static_cast<int>(std::floor(view.ur.lat / tile_deg_));
  double west = view.ll.lon, east = view.ur.lon;
  if (east < west) east += 360.0;  // an antimeridian viewport, unwrapped
  const int lon0 = static_cast<int>(std::floor(west / tile_deg_));
  const int lon1 = static_cast<int>(std::floor(east / tile_deg_));
  const int lon_cells = static_cast<int>(std::lround(360.0 / tile_deg_));

  for (int li = lat0; li <= lat1; ++li) {
    for (int oi = lon0; oi <= lon1; ++oi) {
      // Fold the longitude index into [0, 360) worth of cells, so a viewport
      // crossing the antimeridian asks for the tiles that are really there
      // rather than for a tile at 181 degrees east.
      int folded = oi % lon_cells;
      if (folded >= lon_cells / 2) folded -= lon_cells;
      if (folded < -lon_cells / 2) folded += lon_cells;
      out.push_back(TileIndex{li, folded});
    }
  }
  return out;
}

GeoRect ElevationTiling::KeepRect(const MapProjection& proj) const {
  const GeoRect view = proj.VmapBounds();
  return GeoRect{{view.ll.lat - tile_deg_, view.ll.lon - tile_deg_},
                 {view.ur.lat + tile_deg_, view.ur.lon + tile_deg_}};
}

}  // namespace fv
