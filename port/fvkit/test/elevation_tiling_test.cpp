// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The shared terrain lattice (fvkit/geo/elevation_tiling.h, plan TA1). These
// rules used to live inside contour_overlay.cpp and were tested only through
// a drawn frame; now that a second overlay stands on them they are worth
// pinning directly, because every one of them is a rule about a CACHE and a
// cache thrashing is invisible in a picture.

#include <gtest/gtest.h>

#include <cmath>

#include "fvkit/geo/elevation_tiling.h"
#include "fvkit/proj.h"

namespace {

using fv::ElevationTiling;
using fv::GeoPoint;
using fv::GeoRect;
using fv::Status;
using fv::TileIndex;

// DTED-1 posts everywhere, and nothing else.
class Posts : public fv::IElevationSource {
 public:
  explicit Posts(double deg) : deg_(deg) {}
  GeoRect Bounds() const override { return GeoRect{{-90, -180}, {90, 180}}; }
  Status GetElevation(const GeoPoint&, float* out) override {
    *out = 0.0f;
    return Status::Ok();
  }
  bool PostSpacing(const GeoPoint&, double* lat, double* lon) override {
    if (lat) *lat = deg_;
    if (lon) *lon = deg_;
    return true;
  }

 private:
  double deg_;
};

fv::MapProjection View(double scale, double lat = 34.0, double lon = -84.0,
                       int w = 640, int h = 480) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter({lat, lon}).ok());
  EXPECT_TRUE(p.SetScale(scale).ok());
  return p;
}

}  // namespace

TEST(ElevationTiling, SamplesAtTheCoarserOfFourPixelsAndThePosts) {
  fv::MapProjection proj = View(50000.0);
  Posts dted1(1.0 / 1200.0);  // 3 arcseconds

  // Zoomed in, four screen pixels are finer than the posts, so the posts win:
  // anything finer would be interpolation dressed up as terrain.
  ElevationTiling t;
  ASSERT_TRUE(t.Adopt(proj, &dted1, nullptr));
  EXPECT_NEAR(t.sample_lat(), 1.0 / 1200.0, 1e-12);

  // Zoomed out, four pixels are coarser and they win instead.
  fv::MapProjection wide = View(2000000.0);
  ElevationTiling t2;
  ASSERT_TRUE(t2.Adopt(wide, &dted1, nullptr));
  EXPECT_NEAR(t2.sample_lat(), 4.0 * wide.DegPerPixelLat(), 1e-12);
  EXPECT_GT(t2.sample_lat(), 1.0 / 1200.0);

  // A source that does not know its own spacing is a legal answer, and then
  // four pixels is the whole rule.
  ElevationTiling t3;
  ASSERT_TRUE(t3.Adopt(proj, nullptr, nullptr));
  EXPECT_NEAR(t3.sample_lat(), 4.0 * proj.DegPerPixelLat(), 1e-12);
}

TEST(ElevationTiling, NothingMovesUntilTheDemandHasMovedByAThird) {
  Posts none(0.0);  // never answers, so the sampling is 4 px and follows zoom
  ElevationTiling t;
  bool invalidated = false;
  ASSERT_TRUE(t.Adopt(View(50000.0), nullptr, &invalidated));
  EXPECT_TRUE(invalidated);  // the first adoption always is
  const double first = t.sample_lat();

  // A 10% zoom nudge: inside the hysteresis, so a cache full of tiles read at
  // the old spacing is kept.
  ASSERT_TRUE(t.Adopt(View(55000.0), nullptr, &invalidated));
  EXPECT_FALSE(invalidated);
  EXPECT_EQ(t.sample_lat(), first);

  // A doubling is not.
  ASSERT_TRUE(t.Adopt(View(100000.0), nullptr, &invalidated));
  EXPECT_TRUE(invalidated);
  EXPECT_GT(t.sample_lat(), first);
}

TEST(ElevationTiling, TheTileSizeCoversTheViewportWithoutBurstingThePostCap) {
  // The ladder's job: a screenful is one to four tiles at every zoom.
  EXPECT_EQ(ElevationTiling::TileSizeFor(1.0 / 1200.0, 0.004), 0.05);
  EXPECT_EQ(ElevationTiling::TileSizeFor(0.01, 0.3), 0.5);
  // ... but the POST CAP outranks the viewport, and this is the case worth
  // pinning: at DTED-1 spacing the 0.5-degree step the viewport asks for
  // would be 601 posts a side, over the 512 cap, so the ladder steps back
  // down to 0.2 and a screenful becomes four tiles instead of one.
  EXPECT_EQ(ElevationTiling::TileSizeFor(1.0 / 1200.0, 0.3), 0.2);
  EXPECT_LT(ElevationTiling::TileSizeFor(1.0 / 1200.0, 4.0), 1.0);
  // ... and never so few that the bookkeeping costs more than it saves.
  EXPECT_GE(ElevationTiling::TileSizeFor(0.01, 0.001) / 0.01, 32.0);
}

TEST(ElevationTiling, TilesShareTheirEdgePosts) {
  ElevationTiling t;
  Posts dted1(1.0 / 1200.0);
  ASSERT_TRUE(t.Adopt(View(50000.0), &dted1, nullptr));

  // Posts INCLUDING both edges, so a tile's north edge is its neighbour's
  // south edge and a contour crossing the boundary meets itself.
  EXPECT_EQ(t.PostsX(),
            static_cast<int>(std::lround(t.tile_deg() / t.sample_lon())) + 1);
  const GeoRect a = t.BoundsOf(TileIndex{0, 0});
  const GeoRect b = t.BoundsOf(TileIndex{1, 0});
  EXPECT_DOUBLE_EQ(a.ur.lat, b.ll.lat);
}

TEST(ElevationTiling, LatitudeOffTheEarthIsNotACell) {
  ElevationTiling t;
  ASSERT_TRUE(t.Adopt(View(50000.0, 89.9, 0.0), nullptr, nullptr));
  const int top = static_cast<int>(std::floor(90.0 / t.tile_deg()));
  EXPECT_FALSE(t.Valid(TileIndex{top, 0}));
  EXPECT_TRUE(t.Valid(TileIndex{top - 1, 0}));
}

TEST(ElevationTiling, AViewportOverTheAntimeridianAsksForRealTiles) {
  ElevationTiling t;
  fv::MapProjection proj = View(2000000.0, 0.0, 180.0);
  ASSERT_TRUE(t.Adopt(proj, nullptr, nullptr));

  const std::vector<TileIndex> cells = t.VisibleCells(proj);
  ASSERT_FALSE(cells.empty());
  const int half = static_cast<int>(std::lround(360.0 / t.tile_deg())) / 2;
  for (TileIndex c : cells) {
    // Folded: no cell at 181 degrees east, and the two sides of the seam are
    // both present.
    EXPECT_GE(c.lon, -half);
    EXPECT_LE(c.lon, half);
  }
  bool east = false, west = false;
  for (TileIndex c : cells) {
    if (c.lon >= 0) east = true;
    if (c.lon < 0) west = true;
  }
  EXPECT_TRUE(east);
  EXPECT_TRUE(west);
}

TEST(ElevationTiling, KeysAreDistinctForNeighbouringCellsAndForNegativeOnes) {
  EXPECT_NE(ElevationTiling::Key(TileIndex{0, 0}),
            ElevationTiling::Key(TileIndex{0, 1}));
  EXPECT_NE(ElevationTiling::Key(TileIndex{0, 0}),
            ElevationTiling::Key(TileIndex{1, 0}));
  EXPECT_NE(ElevationTiling::Key(TileIndex{-1, -1}),
            ElevationTiling::Key(TileIndex{1, 1}));
  EXPECT_EQ(ElevationTiling::Key(TileIndex{-7, 3}),
            ElevationTiling::Key(TileIndex{-7, 3}));
}
