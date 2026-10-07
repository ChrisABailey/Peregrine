// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The two ladder kinds over synthetic candidate lists.

#include "fv_view_ladder.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using fv::view::LadderKind;
using fv::view::LadderOutcome;
using fv::view::LadderProduct;
using fv::view::LadderStep;
using fv::view::NearestProduct;
using fv::view::ScaleLadder;

LadderProduct P(int64_t id, const char* format, const char* key, double denom) {
  return LadderProduct{id, format, key, denom};
}

const LadderProduct kTiros = P(1, "tiros", "TIROS1K", 8.0e6);
const LadderProduct kGnc = P(2, "cadrg", "GNC", 5.0e6);
const LadderProduct kTif = P(3, "geotiff", "Color", 2.0e6);
const LadderProduct kOnc = P(4, "cadrg", "ONC", 1.0e6);

TEST(SeriesLadder, StepsAcrossFormatsByScaleAlone) {
  const ScaleLadder ladder(LadderKind::kSeries);
  const std::vector<LadderProduct> here = {kOnc, kTiros, kTif, kGnc};
  const char* expected[] = {"GNC", "Color", "ONC"};
  LadderProduct cur = kTiros;
  double denom = kTiros.scale_denom;
  for (const char* key : expected) {
    const LadderStep s = ladder.Step(denom, &cur, +1, here);
    ASSERT_EQ(s.outcome, LadderOutcome::kStepped);
    EXPECT_EQ(s.product.series_key, key);
    EXPECT_TRUE(s.product_changed);
    EXPECT_EQ(s.display_denom, s.product.scale_denom);
    cur = s.product;
    denom = s.display_denom;
  }
  const LadderStep end = ladder.Step(denom, &cur, +1, here);
  EXPECT_EQ(end.outcome, LadderOutcome::kEndOfLadder);
  EXPECT_EQ(end.display_denom, denom);
  EXPECT_EQ(end.product.series_key, "ONC");
  EXPECT_FALSE(end.product_changed);

  const LadderStep out = ladder.Step(denom, &cur, -1, here);
  EXPECT_EQ(out.product.series_key, "Color");
}

TEST(SeriesLadder, OnlyWhatCoversTheCursor) {
  const ScaleLadder ladder(LadderKind::kSeries);
  // The GeoTIFF does not reach this point: GNC steps straight to ONC.
  const LadderStep s = ladder.Step(kGnc.scale_denom, &kGnc, +1, {kTiros, kGnc, kOnc});
  EXPECT_EQ(s.product.series_key, "ONC");
}

TEST(SeriesLadder, SameScaleKeepsTheCurrentFormat) {
  const ScaleLadder ladder(LadderKind::kSeries);
  const LadderProduct tif250 = P(10, "geotiff", "Color", 250e3);
  const LadderProduct jog250 = P(11, "cadrg", "JOG", 250e3);
  const LadderProduct tif500 = P(12, "geotiff", "Color", 500e3);
  const LadderStep s = ladder.Step(500e3, &tif500, +1, {jog250, tif250, tif500});
  EXPECT_EQ(s.product.series_id, 10);
  const LadderProduct cadrg500 = P(13, "cadrg", "TPC", 500e3);
  const LadderStep t = ladder.Step(500e3, &cadrg500, +1, {tif250, jog250, cadrg500});
  EXPECT_EQ(t.product.series_id, 11);
}

TEST(SeriesLadder, SettleSnapsToTheNearestNativeScale) {
  const ScaleLadder ladder(LadderKind::kSeries);
  // 1:1.6M is nearer 1:2M than 1:1M by ratio.
  const LadderStep s = ladder.Settle(1.6e6, &kGnc, {kGnc, kTif, kOnc});
  EXPECT_EQ(s.product.series_key, "Color");
  EXPECT_EQ(s.display_denom, 2.0e6);
}

// ENC: coastal 1:350,000 everywhere, harbour 1:12,000 only near the port.
const LadderProduct kCoastal = P(20, "enc", "Coastal", 350e3);
const LadderProduct kHarbour = P(21, "enc", "Harbour", 12e3);

TEST(UniformLadder, MagnifiesCoastalUntilHarbourIsNearer) {
  const ScaleLadder ladder(LadderKind::kUniform);
  const std::vector<LadderProduct> port = {kHarbour, kCoastal};
  LadderProduct cur = kCoastal;
  double denom = kCoastal.scale_denom;
  // Ratio midpoint is sqrt(350k * 12k) = 64.8k: 175k and 87.5k stay coastal,
  // 43.75k is harbour.
  const char* expected[] = {"Coastal", "Coastal", "Harbour", "Harbour"};
  for (const char* key : expected) {
    const LadderStep s = ladder.Step(denom, &cur, +1, port);
    ASSERT_EQ(s.outcome, LadderOutcome::kStepped);
    EXPECT_DOUBLE_EQ(s.display_denom, denom / 2.0);
    EXPECT_EQ(s.product.series_key, key) << "at 1:" << s.display_denom;
    cur = s.product;
    denom = s.display_denom;
  }
}

TEST(UniformLadder, WithoutHarbourUnderTheCursorCoastalKeepsMagnifying) {
  const ScaleLadder ladder(LadderKind::kUniform);
  LadderProduct cur = kCoastal;
  double denom = 43750.0;
  for (int i = 0; i < 3; ++i) {
    const LadderStep s = ladder.Step(denom, &cur, +1, {kCoastal});
    EXPECT_EQ(s.product.series_key, "Coastal");
    denom = s.display_denom;
  }
  // Nothing at all under the cursor: the current product, still magnifying.
  const LadderStep s = ladder.Step(denom, &cur, +1, {});
  EXPECT_EQ(s.outcome, LadderOutcome::kStepped);
  EXPECT_EQ(s.product.series_key, "Coastal");
  EXPECT_FALSE(s.product_changed);
  EXPECT_DOUBLE_EQ(s.display_denom, denom / 2.0);
}

TEST(UniformLadder, SettleKeepsThePinchedScale) {
  const ScaleLadder ladder(LadderKind::kUniform);
  const LadderStep s = ladder.Settle(30000.0, &kCoastal, {kCoastal, kHarbour});
  EXPECT_EQ(s.display_denom, 30000.0);
  EXPECT_EQ(s.product.series_key, "Harbour");
  EXPECT_TRUE(s.product_changed);
}

TEST(UniformLadder, NothingAnywhereIsNoProduct) {
  const ScaleLadder ladder(LadderKind::kUniform);
  const LadderStep s = ladder.Step(1e5, nullptr, +1, {});
  EXPECT_EQ(s.outcome, LadderOutcome::kNoProduct);
  EXPECT_FALSE(s.has_product);
}

TEST(NearestProduct, RatioNotDifference) {
  // 1:30,000 is nearer 1:22,000 than 1:50,000 by ratio (desktop-plan §1d).
  const LadderProduct a = P(1, "enc", "A", 22e3), b = P(2, "enc", "B", 50e3);
  const std::vector<LadderProduct> c = {b, a};
  EXPECT_EQ(NearestProduct(30e3, nullptr, c)->series_key, "A");
}

TEST(NearestProduct, TieGoesToTheCurrentProduct) {
  const LadderProduct a = P(1, "enc", "A", 10e3), b = P(2, "enc", "B", 40e3);
  const std::vector<LadderProduct> c = {a, b};
  EXPECT_EQ(NearestProduct(20e3, &b, c)->series_key, "B");
  EXPECT_EQ(NearestProduct(20e3, nullptr, c)->series_key, "A");
}

}  // namespace
