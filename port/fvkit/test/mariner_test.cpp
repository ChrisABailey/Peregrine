// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
/// FormatSounding in each depth unit.

#include "fvkit/vector/mariner.h"

#include <gtest/gtest.h>

namespace {

std::pair<std::string, std::string> Fig(double m, fv::DepthUnit u) {
  const fv::SoundingFigure f = fv::FormatSounding(m, u);
  return {f.whole, f.fraction};
}

using P = std::pair<std::string, std::string>;

TEST(FormatSounding, MetresKeepTheDecimetreBelow31) {
  EXPECT_EQ(Fig(9.4, fv::DepthUnit::kMetres), P("9", "4"));
  EXPECT_EQ(Fig(12.6, fv::DepthUnit::kMetres), P("12", "6"));
  EXPECT_EQ(Fig(5.0, fv::DepthUnit::kMetres), P("5", ""));
  EXPECT_EQ(Fig(9.97, fv::DepthUnit::kMetres), P("10", ""));
  EXPECT_EQ(Fig(31.4, fv::DepthUnit::kMetres), P("31", ""));
  EXPECT_EQ(Fig(127.0, fv::DepthUnit::kMetres), P("127", ""));
}

TEST(FormatSounding, ADryingHeightPrintsItsMagnitude) {
  EXPECT_EQ(Fig(-1.2, fv::DepthUnit::kMetres), P("1", "2"));
  EXPECT_EQ(Fig(-1.2, fv::DepthUnit::kFeet), P("4", ""));
}

TEST(FormatSounding, FeetAreWholeFeet) {
  EXPECT_EQ(Fig(3.3, fv::DepthUnit::kFeet), P("11", ""));
  EXPECT_EQ(Fig(10.0, fv::DepthUnit::kFeet), P("33", ""));
}

TEST(FormatSounding, FathomsCarryFeetBelowElevenFathoms) {
  EXPECT_EQ(Fig(10.0, fv::DepthUnit::kFathomsFeet), P("5", "3"));  // 33 ft
  EXPECT_EQ(Fig(5.4864, fv::DepthUnit::kFathomsFeet), P("3", ""));  // 18 ft
  EXPECT_EQ(Fig(30.0, fv::DepthUnit::kFathomsFeet), P("16", ""));  // 16.4 fm
}

}  // namespace
