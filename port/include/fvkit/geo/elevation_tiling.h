// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/geo/elevation_tiling.h — the geographic lattice an overlay reads
// terrain over: where the tiles are, how finely to sample them, and when the
// answer has moved enough to be worth re-deriving.
//
// THE FIFTH PIECE OF THE SHARED OVERLAY TOOLKIT (port/PORTING.md §1a-bis),
// and an EXTRACTION rather than new work. Every line here was written for the
// contour overlay (port/contour-plan.md C2, 2026-08-29) and lived inside
// contour_overlay.cpp until the terrain-avoidance mask needed the same
// lattice, the same sampling rule and the same hysteresis to sit on
// (port/tamask-plan.md TA1). Both overlays now go through this file, which is
// what makes it an extraction and not a guess.
//
// THE PAYLOAD IS NOT HERE, deliberately. Contours cache traced polylines and
// the mask caches elevation posts; the two caches have nothing in common
// except their KEYS. So this class owns the lattice and the sampling policy
// and hands out cell indices, bounds and post counts — and each overlay keeps
// its own `std::map<int64_t, whatever>` beside it.
//
// The four things it knows, none of which is obvious:
//
//   * THE SCREEN IS NOT THE UNIT OF WORK. A lattice of whole fractions of a
//     degree is, so a pan reuses what the last frame read and the lines do
//     not writhe as the map moves. FalconView keyed the size on the DTED
//     level alone (0.2 / 0.1 / 0.05 degrees for level 1 / 2 / 3), which is
//     absurd zoomed in — at 1:24 K a 0.2-degree tile is sixty times the area
//     on screen. Here the step comes off a ladder, chosen from the viewport
//     first and then clamped by a post budget per axis.
//   * SAMPLE AT max(4 screen pixels, the native post spacing). Finer than the
//     posts invents terrain; finer than 4 px pays for detail nobody can see.
//   * THE ONE-THIRD HYSTERESIS. Re-sampling on every zoom nudge would throw
//     the cache away continuously, so the demanded spacing must move by more
//     than a third of the spacing in force before anything changes.
//   * A TILE'S EDGE POSTS BELONG TO BOTH ITS NEIGHBOURS. `PostsX`/`PostsY`
//     count both edges, so two tiles that share a boundary sample the same
//     posts along it and their contours meet there. (FalconView asked its
//     DTED server for one extra post north and east to arrange the same
//     thing.)

#ifndef FVKIT_GEO_ELEVATION_TILING_H_
#define FVKIT_GEO_ELEVATION_TILING_H_

#include <cstdint>
#include <vector>

#include "fvkit/formats/source.h"
#include "fvkit/geo.h"
#include "fvkit/proj.h"

namespace fv {

// One cell of the lattice. The cell's south-west corner is at
// (lat * tile_deg, lon * tile_deg), so the indices are signed and the cell at
// the equator/prime meridian is (0, 0).
struct TileIndex {
  int lat = 0;
  int lon = 0;
};

class ElevationTiling {
 public:
  // Chooses the post spacing and the tile size for this frame.
  //
  // Returns false when neither can be chosen (a projection that is not ready,
  // or a degenerate degrees-per-pixel), in which case nothing else here may
  // be called. `*invalidated`, which may be null, is set true when the
  // sampling or the tile size MOVED — the caller must then drop every cached
  // tile, because what it holds was read at a spacing that is no longer in
  // force.
  //
  // `src` may be null; it is asked only for its native post spacing, and a
  // source that does not know is a legal answer (source.h).
  bool Adopt(const MapProjection& proj, IElevationSource* src,
             bool* invalidated);

  // Forgets the adopted sampling, so the next Adopt starts from nothing and
  // reports itself as an invalidation. A caller clearing its cache for its
  // own reasons (a new elevation source, a changed interval) calls this too.
  void Reset();

  bool Ready() const { return tile_deg_ > 0.0; }
  double sample_lat() const { return sample_lat_; }
  double sample_lon() const { return sample_lon_; }
  double tile_deg() const { return tile_deg_; }

  // Posts per axis in one tile, INCLUDING both edges — see the header note.
  // Clamped to [2, 2048]: a tile must have at least one cell in it, and no
  // tile is worth four million posts.
  int PostsX() const;
  int PostsY() const;

  // A cell whose latitude band is off the earth is not a cell. Longitude
  // needs no such test: VisibleCells folds it.
  bool Valid(TileIndex t) const;
  GeoRect BoundsOf(TileIndex t) const;

  // The cells the viewport touches, in row-major order, longitudes folded
  // into the real ones so a viewport crossing the antimeridian asks for the
  // tiles that are there rather than for a tile at 181 degrees east.
  std::vector<TileIndex> VisibleCells(const MapProjection& proj) const;

  // What a cache should keep after a draw: the viewport grown by one tile in
  // every direction, so a small pan does not evict the tile it is about to
  // ask for again.
  GeoRect KeepRect(const MapProjection& proj) const;

  static int64_t Key(TileIndex t) {
    return (static_cast<int64_t>(t.lat) << 32) ^ static_cast<uint32_t>(t.lon);
  }

  // Exposed for the tests only: the ladder step chosen for a given sampling
  // and viewport span.
  static double TileSizeFor(double sample_deg, double view_span_deg);

 private:
  double sample_lat_ = 0.0;  // degrees between posts
  double sample_lon_ = 0.0;
  double tile_deg_ = 0.0;
};

}  // namespace fv

#endif  // FVKIT_GEO_ELEVATION_TILING_H_
