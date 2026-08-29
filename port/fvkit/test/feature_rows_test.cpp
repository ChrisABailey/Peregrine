// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// MergeFeatureRows (search-plan-COMPLETE.md, S3) — "one named thing, one
// row", which S2 discovered inside VectorMapOverlay and S3 had to share,
// because the live tile scan and the staged index must not hold two ideas of
// what one road is.
//
// vector_map_overlay_test.cpp already pins the RULE through its caller (a
// bridging piece, the metre, the anchor on the biggest piece). What is pinned
// here is what only a direct caller can see: the properties an INDEX BUILDER
// depends on, and the ones the internal south-to-north sweep could have
// broken while making a whole-pyramid merge affordable.

#include "fvkit/vector/feature_rows.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using fv::FeatureRow;
using fv::GeoPoint;
using fv::GeoRect;

// A row named `title` whose box is a small square at (lat, lon).
FeatureRow At(const char* title, double lat, double lon, int feature,
              double half = 0.0005) {
  FeatureRow r;
  r.title = title;
  r.layer = "transportation_name";
  r.style_key = "minor";
  r.bounds = GeoRect{{lat - half, lon - half}, {lat + half, lon + half}};
  r.anchor = GeoPoint{lat, lon};
  r.ref.layer = 0;
  r.ref.tile = 1;
  r.ref.feature = feature;
  return r;
}

std::vector<std::string> Titles(const std::vector<FeatureRow>& rows) {
  std::vector<std::string> out;
  for (const FeatureRow& r : rows) out.push_back(r.title);
  return out;
}

}  // namespace

TEST(MergeFeatureRows, TouchingPiecesOfOneNameBecomeOneRow) {
  std::vector<FeatureRow> rows{At("Ruddy Turnstone", 32.60, -80.10, 1),
                               At("Ruddy Turnstone", 32.6009, -80.10, 2),
                               At("Bufflehead", 32.60, -80.12, 3)};
  fv::MergeFeatureRows(&rows, 100.0);
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ("Ruddy Turnstone", rows[0].title);
  EXPECT_EQ("Bufflehead", rows[1].title);
  // The union, not either piece.
  EXPECT_NEAR(32.5995, rows[0].bounds.ll.lat, 1e-9);
  EXPECT_NEAR(32.6014, rows[0].bounds.ur.lat, 1e-9);
}

TEST(MergeFeatureRows, OutputKeepsTheOrderTheRowsFirstAppearedIn) {
  std::vector<FeatureRow> rows{At("C road", 32.70, -80.10, 1),
                               At("A road", 32.60, -80.10, 2),
                               At("B road", 32.65, -80.10, 3),
                               At("C road", 32.7009, -80.10, 4)};
  fv::MergeFeatureRows(&rows, 100.0);
  // NOT alphabetical and NOT south-to-north: the sweep inside is an
  // implementation detail, and a caller that fed rows in a deliberate order
  // (a search walking a tile range) gets them back in it.
  EXPECT_EQ(std::vector<std::string>({"C road", "A road", "B road"}),
            Titles(rows));
}

TEST(MergeFeatureRows, TheSurvivingRefIsTheFirstInINPUTOrder) {
  // The second piece is SOUTH of the first, so the internal sweep reaches it
  // first — and the ref must still be the one the caller met first, because
  // that is what the pinned tier-1 results say and what a kept mint depends on.
  std::vector<FeatureRow> rows{At("Ruddy Turnstone", 32.6009, -80.10, 11),
                               At("Ruddy Turnstone", 32.60, -80.10, 22)};
  fv::MergeFeatureRows(&rows, 100.0);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(11, rows[0].ref.feature);
}

TEST(MergeFeatureRows, QualityAndRankKeepTheBestOfThePieces) {
  std::vector<FeatureRow> rows{At("Main", 32.60, -80.10, 1),
                               At("Main", 32.6009, -80.10, 2)};
  rows[0].quality = 2;
  rows[0].rank = 14;
  rows[1].quality = 1;
  rows[1].rank = 9;
  fv::MergeFeatureRows(&rows, 100.0);
  ASSERT_EQ(1u, rows.size());
  // Best = smallest, for both: a road matched exactly by one of its pieces was
  // matched exactly, and a name still drawn at z9 is a z9 name.
  EXPECT_EQ(1, rows[0].quality);
  EXPECT_EQ(9, rows[0].rank);
}

TEST(MergeFeatureRows, TheAnchorComesFromTheBiggestPiece) {
  std::vector<FeatureRow> rows{At("Long road", 32.60, -80.10, 1, 0.0002),
                               At("Long road", 32.6005, -80.10, 2, 0.004)};
  fv::MergeFeatureRows(&rows, 500.0);
  ASSERT_EQ(1u, rows.size());
  EXPECT_NEAR(32.6005, rows[0].anchor.lat, 1e-9);
}

TEST(MergeFeatureRows, ABridgingPieceJoinsTwoClustersWhicheverOrderItArrivesIn) {
  // Two ends 400 m apart with a 100 m gap: two rows until the middle arrives.
  std::vector<FeatureRow> ends{At("Split", 32.6000, -80.10, 1),
                               At("Split", 32.6040, -80.10, 2)};
  fv::MergeFeatureRows(&ends, 100.0);
  EXPECT_EQ(2u, ends.size());

  std::vector<FeatureRow> bridged{At("Split", 32.6000, -80.10, 1),
                                  At("Split", 32.6040, -80.10, 2),
                                  At("Split", 32.6020, -80.10, 3, 0.0016)};
  fv::MergeFeatureRows(&bridged, 100.0);
  EXPECT_EQ(1u, bridged.size());
  EXPECT_EQ(1, bridged[0].ref.feature);  // still the first piece met
}

TEST(MergeFeatureRows, ADifferentLayerOrClassIsADifferentRow) {
  std::vector<FeatureRow> rows{At("Main", 32.60, -80.10, 1),
                               At("Main", 32.6001, -80.10, 2),
                               At("Main", 32.6002, -80.10, 3)};
  rows[1].style_key = "track";
  rows[2].layer = "water_name";
  fv::MergeFeatureRows(&rows, 100.0);
  EXPECT_EQ(3u, rows.size());
}

TEST(MergeFeatureRows, ANegativeGapLeavesEveryPieceAlone) {
  std::vector<FeatureRow> rows{At("Main", 32.60, -80.10, 1),
                               At("Main", 32.60, -80.10, 2)};
  fv::MergeFeatureRows(&rows, -1.0);
  EXPECT_EQ(2u, rows.size());
}

TEST(MergeFeatureRows, AZeroGapMergesOnlyPiecesThatTouch) {
  std::vector<FeatureRow> touching{At("Main", 32.6000, -80.10, 1),
                                   At("Main", 32.6009, -80.10, 2)};
  fv::MergeFeatureRows(&touching, 0.0);
  EXPECT_EQ(1u, touching.size());  // the boxes overlap: the tile-seam case

  std::vector<FeatureRow> apart{At("Main", 32.6000, -80.10, 1),
                                At("Main", 32.6030, -80.10, 2)};
  fv::MergeFeatureRows(&apart, 0.0);
  EXPECT_EQ(2u, apart.size());
}

TEST(MergeFeatureRows, AChainOfPiecesFoldsIntoOneRowHoweverLongItIs) {
  // What a whole-pyramid build does to one road: 500 pieces, each within the
  // gap of the next, in an order no caller controls. The sweep exists for
  // this case, and the answer must be the same one the quadratic walk gave.
  std::vector<FeatureRow> rows;
  for (int i = 0; i < 500; ++i) {
    rows.push_back(At("Kiawah Island Parkway", 32.60 + 0.0009 * i, -80.10,
                      1000 - i));
  }
  fv::MergeFeatureRows(&rows, 100.0);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(1000, rows[0].ref.feature);
  EXPECT_NEAR(32.60 - 0.0005, rows[0].bounds.ll.lat, 1e-9);
  EXPECT_NEAR(32.60 + 0.0009 * 499 + 0.0005, rows[0].bounds.ur.lat, 1e-9);
}
