// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// PickIndex tests (identify, plan §5.3).
//
// Synthetic on purpose, like the renderer tests: the index is pure pixel
// geometry, so nothing here needs a source, a style engine or a canvas. If it
// ever does, the seam has leaked.

#include "fvkit/vector/pick.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

fv::FeatureRef Ref(int layer, int feature, int tile = 0) {
  fv::FeatureRef r;
  r.layer = layer;
  r.feature = feature;
  r.tile = tile;
  return r;
}

std::vector<fv::PixelPoint> HLine(int y, int x0, int x1) {
  return {{x0, y}, {x1, y}};
}

TEST(PickIndex, EmptyIndexHitsNothing) {
  fv::PickIndex idx;
  EXPECT_TRUE(idx.empty());
  EXPECT_TRUE(idx.HitTest(10, 10).empty());
}

TEST(PickIndex, StrokeIsHitWithinItsInkAndMissedBeyondTolerance) {
  fv::PickIndex idx;
  // A 6-px-wide line at y = 50 => ink spans y in [47, 53].
  idx.AddStroke(Ref(0, 7), 10, HLine(50, 10, 90), 3.0);

  auto on = idx.HitTest(50, 50, 0.0);
  ASSERT_EQ(on.size(), 1u);
  EXPECT_EQ(on[0].ref.feature, 7);
  EXPECT_DOUBLE_EQ(on[0].distance, 0.0);

  // 3 px off the centreline is still ON the ink (half width 3).
  EXPECT_EQ(idx.HitTest(50, 53, 0.0).size(), 1u);
  // 6 px off is 3 px away from the ink: inside a 3 px tolerance, outside 2.
  EXPECT_EQ(idx.HitTest(50, 56, 3.0).size(), 1u);
  EXPECT_TRUE(idx.HitTest(50, 56, 2.0).empty());
  // Beyond the ends of the run, nothing.
  EXPECT_TRUE(idx.HitTest(200, 50, 3.0).empty());
}

TEST(PickIndex, FillIsHitAnywhereInsideTheRing) {
  fv::PickIndex idx;
  idx.AddFill(Ref(1, 3), 5, {{10, 10}, {60, 10}, {60, 40}, {10, 40}});

  auto inside = idx.HitTest(35, 25, 0.0);
  ASSERT_EQ(inside.size(), 1u);
  EXPECT_EQ(inside[0].ref.feature, 3);
  EXPECT_DOUBLE_EQ(inside[0].distance, 0.0);

  // Just outside the edge: reachable through tolerance, not without it.
  EXPECT_EQ(idx.HitTest(62, 25, 3.0).size(), 1u);
  EXPECT_TRUE(idx.HitTest(62, 25, 1.0).empty());
  EXPECT_TRUE(idx.HitTest(200, 200, 3.0).empty());
}

TEST(PickIndex, BoxCoversSymbolsAndLabels) {
  fv::PickIndex idx;
  fv::PixelRect box;
  box.x = 100;
  box.y = 200;
  box.width = 12;
  box.height = 12;
  idx.AddBox(Ref(2, 9), 40, box);

  EXPECT_EQ(idx.HitTest(105, 205, 0.0).size(), 1u);
  EXPECT_EQ(idx.HitTest(100, 200, 0.0).size(), 1u);
  EXPECT_TRUE(idx.HitTest(120, 205, 3.0).empty());

  // A degenerate box is not indexed at all (nothing was drawn).
  fv::PickIndex empty_box;
  fv::PixelRect zero;
  empty_box.AddBox(Ref(2, 9), 40, zero);
  EXPECT_EQ(empty_box.shape_count(), 0u);
}

TEST(PickIndex, ReturnsTheWholeStackTopmostFirst) {
  fv::PickIndex idx;
  // Same spot, three features at different display priorities. Higher
  // priority is drawn later, so it is on top.
  idx.AddFill(Ref(0, 1), 5, {{0, 0}, {100, 0}, {100, 100}, {0, 100}});
  idx.AddStroke(Ref(0, 2), 20, HLine(50, 0, 100), 2.0);
  idx.AddStroke(Ref(0, 3), 12, HLine(50, 0, 100), 2.0);

  auto hits = idx.HitTest(50, 50, 1.0);
  ASSERT_EQ(hits.size(), 3u);
  EXPECT_EQ(hits[0].ref.feature, 2);  // priority 20
  EXPECT_EQ(hits[1].ref.feature, 3);  // priority 12
  EXPECT_EQ(hits[2].ref.feature, 1);  // priority 5 (the fill underneath)
}

TEST(PickIndex, SamePriorityRanksTheLaterDrawOnTop) {
  fv::PickIndex idx;
  idx.AddStroke(Ref(0, 1), 7, HLine(20, 0, 40), 2.0);
  idx.AddStroke(Ref(0, 2), 7, HLine(20, 0, 40), 2.0);

  auto hits = idx.HitTest(20, 20, 0.0);
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].ref.feature, 2);
  EXPECT_EQ(hits[1].ref.feature, 1);
}

TEST(PickIndex, OneFeatureDrawnSeveralTimesIsReportedOnce) {
  fv::PickIndex idx;
  const fv::FeatureRef r = Ref(4, 11, 2);
  // A clipped line contributes several runs, and GeoSym gives an area feature
  // a fill pass AND a boundary pass — all one feature to the user.
  idx.AddFill(r, 3, {{0, 0}, {80, 0}, {80, 80}, {0, 80}});
  idx.AddStroke(r, 9, HLine(40, 0, 30), 1.0);
  idx.AddStroke(r, 9, HLine(40, 50, 80), 1.0);

  auto hits = idx.HitTest(20, 40, 0.0);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].ref.feature, 11);
  EXPECT_EQ(hits[0].ref.tile, 2);
  EXPECT_EQ(hits[0].priority, 9) << "reported at its topmost pass";
  EXPECT_DOUBLE_EQ(hits[0].distance, 0.0);
}

TEST(PickIndex, DistinguishesFeaturesByEveryFieldOfTheRef) {
  fv::PickIndex idx;
  // Same feature id, different tile: two different features.
  idx.AddStroke(Ref(0, 5, 1), 1, HLine(10, 0, 20), 1.0);
  idx.AddStroke(Ref(0, 5, 2), 1, HLine(10, 0, 20), 1.0);
  EXPECT_EQ(idx.HitTest(10, 10, 0.0).size(), 2u);
}

TEST(PickIndex, ClearDropsEverything) {
  fv::PickIndex idx;
  idx.AddStroke(Ref(0, 1), 1, HLine(10, 0, 20), 1.0);
  ASSERT_EQ(idx.shape_count(), 1u);
  idx.Clear();
  EXPECT_TRUE(idx.empty());
  EXPECT_TRUE(idx.HitTest(10, 10).empty());
}

}  // namespace
