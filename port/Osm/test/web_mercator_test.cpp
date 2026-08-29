// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Hermetic tests for the slippy-map tile grid (OSM phase O1). No file, no
// tile, no source — if any of these needs test data, the header has grown
// something it should not have.
//
// The expected values are computed from the spherical Web Mercator formulas
// independently (a throwaway Python oracle), not read back out of this code.

#include "fv_web_mercator.h"

#include <gtest/gtest.h>

#include <cmath>

using fv::webmerc::kMaxLatitude;
using fv::webmerc::LatToTileY;
using fv::webmerc::LonToTileX;
using fv::webmerc::MetersPerPixel;
using fv::webmerc::TileBounds;
using fv::webmerc::TileId;
using fv::webmerc::TilesPerAxis;
using fv::webmerc::TileToLat;
using fv::webmerc::TileToLon;
using fv::webmerc::TmsRow;
using fv::webmerc::ZoomForScale;

TEST(WebMercator, ZoomZeroIsOneTileOverTheWholeGrid) {
  EXPECT_EQ(TilesPerAxis(0), 1);
  EXPECT_EQ(TilesPerAxis(14), 16384);

  const fv::GeoRect r = TileBounds(TileId{0, 0, 0});
  EXPECT_DOUBLE_EQ(r.ll.lon, -180.0);
  EXPECT_DOUBLE_EQ(r.ur.lon, 180.0);
  // The square grid stops at atan(sinh(pi)); the poles are not on the map.
  EXPECT_NEAR(r.ur.lat, kMaxLatitude, 1e-12);
  EXPECT_NEAR(r.ll.lat, -kMaxLatitude, 1e-12);
}

TEST(WebMercator, EdgesOfTheAtlantaTileMatchTheFormula) {
  // z14/4351/6558 is downtown Atlanta, and is the tile the real-data tests
  // read. These four numbers come from the oracle, to 10 decimal places.
  EXPECT_NEAR(TileToLon(4351, 14), -84.39697265625, 1e-10);
  EXPECT_NEAR(TileToLon(4352, 14), -84.375, 1e-10);
  EXPECT_NEAR(TileToLat(6558, 14), 33.760882000869174, 1e-10);
  EXPECT_NEAR(TileToLat(6559, 14), 33.742612777346880, 1e-10);

  const fv::GeoRect r = TileBounds(TileId{14, 4351, 6558});
  // y grows SOUTHWARD: the tile's own y is its NORTH edge. Getting this
  // backwards flips a chart, so assert the ordering, not just the values.
  EXPECT_GT(r.ur.lat, r.ll.lat);
  EXPECT_NEAR(r.ur.lat, 33.760882000869174, 1e-10);
  EXPECT_NEAR(r.ll.lat, 33.742612777346880, 1e-10);
}

TEST(WebMercator, ForwardAndInverseAreInverses) {
  const double lats[] = {-84.0, -33.75, -0.0001, 0.0, 12.5, 33.7490, 71.2};
  const double lons[] = {-179.9, -106.65, -84.3880, 0.0, 45.5, 179.9};
  for (int z : {0, 5, 14, 22}) {
    for (double lat : lats) {
      const double y = LatToTileY(lat, z);
      EXPECT_NEAR(TileToLat(y, z), lat, 1e-9) << "z=" << z << " lat=" << lat;
    }
    for (double lon : lons) {
      const double x = LonToTileX(lon, z);
      EXPECT_NEAR(TileToLon(x, z), lon, 1e-9) << "z=" << z << " lon=" << lon;
    }
  }
}

TEST(WebMercator, LatitudeIsClampedToWhatTheGridCanHold) {
  // A pole has no row. LatToTileY must clamp rather than return an infinity
  // that then becomes a tile index of INT_MIN.
  EXPECT_NEAR(LatToTileY(90.0, 3), LatToTileY(kMaxLatitude, 3), 1e-9);
  EXPECT_NEAR(LatToTileY(-90.0, 3), LatToTileY(-kMaxLatitude, 3), 1e-9);
  EXPECT_TRUE(std::isfinite(LatToTileY(90.0, 14)));
  EXPECT_NEAR(LatToTileY(-kMaxLatitude, 14), 16384.0, 1e-6);
}

TEST(WebMercator, TmsFlipIsItsOwnInverse) {
  for (int z : {0, 1, 8, 14}) {
    const int n = static_cast<int>(TilesPerAxis(z));
    for (int y : {0, 1, n / 2, n - 1}) {
      EXPECT_EQ(TmsRow(z, TmsRow(z, y)), y) << "z=" << z << " y=" << y;
    }
  }
  // The concrete case the MBTiles reader depends on: at z14 the file's
  // northernmost row is its LARGEST tile_row.
  EXPECT_EQ(TmsRow(14, 10219), 6164);
  EXPECT_EQ(TmsRow(14, 9319), 7064);
  EXPECT_EQ(TmsRow(0, 0), 0);
}

TEST(WebMercator, GroundResolutionHalvesPerZoomAndShrinksWithLatitude) {
  EXPECT_NEAR(MetersPerPixel(0, 0.0), 156543.03392804097, 1e-6);
  EXPECT_NEAR(MetersPerPixel(1, 0.0), 156543.03392804097 / 2.0, 1e-6);
  EXPECT_NEAR(MetersPerPixel(14, 0.0), 156543.03392804097 / 16384.0, 1e-9);
  EXPECT_NEAR(MetersPerPixel(14, 60.0), MetersPerPixel(14, 0.0) * 0.5, 1e-9);
}

TEST(WebMercator, ScaleChoosesTheZoomTheCutterGeneralizedFor) {
  // Oracle values at Atlanta's latitude on the port's reference 0.25 mm pitch;
  // the unclamped log2 is in the comment, the answer is the rounded clamp.
  const double lat = 33.75;
  EXPECT_EQ(ZoomForScale(12000, lat, 0.25, 0, 14), 14);      // 15.40 -> clamp
  EXPECT_EQ(ZoomForScale(100000, lat, 0.25, 0, 14), 12);     // 12.35
  EXPECT_EQ(ZoomForScale(1000000, lat, 0.25, 0, 14), 9);     // 9.02
  EXPECT_EQ(ZoomForScale(10000000, lat, 0.25, 0, 14), 6);    // 5.70 -> 6
  // Nearest, not floor: 5.70 became 6 above. Floor would have picked 5 and
  // drawn a level generalized for half this scale.
  EXPECT_EQ(ZoomForScale(10000000, lat, 0.25, 0, 4), 4);     // clamped high
  EXPECT_EQ(ZoomForScale(12000, lat, 0.25, 10, 14), 14);
  EXPECT_EQ(ZoomForScale(1000000, lat, 0.25, 11, 14), 11);   // clamped low
}

TEST(WebMercator, ScaleWithNoUsableInputsFallsBackToTheDeepestLevel) {
  // 0 is the seam's "no scale filter". A source cannot answer "no zoom", so
  // the deepest level is the documented fallback and the tile-count guard is
  // what keeps that affordable.
  EXPECT_EQ(ZoomForScale(0.0, 33.75, 0.25, 0, 14), 14);
  EXPECT_EQ(ZoomForScale(-1.0, 33.75, 0.25, 0, 14), 14);
  EXPECT_EQ(ZoomForScale(12000, 33.75, 0.0, 0, 14), 14);
}

TEST(WebMercator, LatitudeIsClampedBeforeItReachesTheZoomFormula) {
  // FOUND BY THIS TEST: at exactly 90 degrees cos(lat) is 6e-17 rather than
  // 0, so the guard for a non-finite ratio never fired and the answer walked
  // off the BOTTOM of the pyramid — 1:12,000 at the pole asked for zoom 0.
  // The clamp to the grid's last row is the same one LatToTileY applies, and
  // it is the only answer consistent with "there is no tile up there".
  EXPECT_EQ(ZoomForScale(12000, 90.0, 0.25, 0, 14),
            ZoomForScale(12000, kMaxLatitude, 0.25, 0, 14));
  EXPECT_EQ(ZoomForScale(12000, -90.0, 0.25, 0, 14),
            ZoomForScale(12000, -kMaxLatitude, 0.25, 0, 14));
  // And that answer is a real level, not an endpoint of the clamp range.
  const int z = ZoomForScale(12000, 90.0, 0.25, 0, 14);
  EXPECT_GT(z, 0);
  EXPECT_LT(z, 14);
}

TEST(WebMercator, PitchIsTheZoomKnob) {
  // Halving the pixel pitch (a retina display) at the same map scale asks for
  // one zoom level deeper, because each pixel now covers half the ground.
  const double lat = 33.75;
  EXPECT_EQ(ZoomForScale(1000000, lat, 0.25, 0, 20), 9);
  EXPECT_EQ(ZoomForScale(1000000, lat, 0.125, 0, 20), 10);
}
