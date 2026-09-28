// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fvkit/nav/beach.h.
//
// The synthetic table is low 0.0 m at 0, high 2.0 m at 6 h, low 0.4 m at 12 h
// and high 1.6 m at 18 h, so every crossing has a closed form. The Kiawah
// table, testdata/tides/8667062.json, is used when present.

#include "fvkit/nav/beach.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace fv {
namespace nav {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalf = 21600.0;  // six hours

TideTable Simple() {
  TideTable t;
  const Status s = t.Parse(
      "{\"units\": \"m\", \"datum\": \"MLLW\", \"valid_from\": 0, \"valid_until\": 64800,"
      " \"station\": {\"id\": \"1\", \"name\": \"Test\"}, \"extremes\": ["
      "[0, 0.0, \"L\"], [21600, 2.0, \"H\"], [43200, 0.4, \"L\"], [64800, 1.6, \"H\"]]}");
  EXPECT_TRUE(s.ok()) << s.message;
  return t;
}

/// The time on the half-cycle starting at `t0`, from `ha` to `hb`, where the
/// cosine curve reaches `h`.
double Crossing(double t0, double ha, double hb, double h) {
  const double w = (h - hb) / (ha - hb);
  return t0 + std::acos(2.0 * w - 1.0) / kPi * kHalf;
}

BeachStretchVerdict JudgeOne(const TideTable* t, double depart, double enter, double exit) {
  const auto v = BeachTideVerdicts({{enter, exit}}, t, depart, BeachTideLimits{});
  EXPECT_EQ(v.size(), 1u);
  return v.front();
}

TEST(BeachTide, LowWaterIsGood) {
  const TideTable t = Simple();
  const BeachStretchVerdict v = JudgeOne(&t, 0, 600, 1800);
  EXPECT_EQ(v.verdict, BeachVerdict::kGood);
  EXPECT_DOUBLE_EQ(v.enter_at_s, 600);
  EXPECT_DOUBLE_EQ(v.exit_at_s, 1800);
  // Rising from the low, so the peak is at the end of the margin.
  EXPECT_DOUBLE_EQ(v.peak_at_s, 2400);
  EXPECT_NEAR(v.peak_m, 1.0 - std::cos(kPi * 2400 / kHalf), 1e-12);
  EXPECT_TRUE(std::isnan(v.covered_at_s));
  EXPECT_DOUBLE_EQ(v.passable_from_s, 600);
  EXPECT_DOUBLE_EQ(v.good_from_s, 600);
}

// Walking limits: no passable height, easy going below 1.2 m.
TEST(BeachTide, WithNoPassableLimitHighWaterIsMarginalNotPoor) {
  const TideTable t = Simple();
  BeachTideLimits walk;
  walk.rideable_below_m = std::numeric_limits<double>::infinity();
  walk.good_below_m = 1.2;
  const auto v = BeachTideVerdicts({{0, 1200}}, &t, kHalf - 600, walk).front();
  EXPECT_EQ(v.verdict, BeachVerdict::kMarginal);
  EXPECT_DOUBLE_EQ(v.peak_m, 2.0);
  EXPECT_TRUE(std::isnan(v.covered_at_s));
  EXPECT_DOUBLE_EQ(v.passable_from_s, kHalf - 600);
  // Easy again once the falling water reaches 1.2 m.
  EXPECT_NEAR(v.good_from_s, Crossing(kHalf, 2.0, 0.4, 1.2), 1e-6);

  const auto low = BeachTideVerdicts({{0, 1200}}, &t, 0, walk).front();
  EXPECT_EQ(low.verdict, BeachVerdict::kGood);
}

TEST(BeachTide, APeakJustUnderTheThresholdIsMarginal) {
  const TideTable t = Simple();
  // [4800, 6000] with the margin: 0.357 m at the end, inside the 0.15 m band.
  const BeachStretchVerdict v = JudgeOne(&t, 4000, 800, 1400);
  EXPECT_EQ(v.verdict, BeachVerdict::kMarginal);
  EXPECT_DOUBLE_EQ(v.peak_at_s, 6000);
  EXPECT_TRUE(std::isnan(v.covered_at_s));
  EXPECT_DOUBLE_EQ(v.passable_from_s, 4800);
  // The 0.35 m window closes at 5965 s, 35 s too soon, and the next low is 0.4 m.
  EXPECT_TRUE(std::isnan(v.good_from_s));
}

TEST(BeachTide, HighWaterIsPoorUntilTheEbb) {
  const TideTable t = Simple();
  const BeachStretchVerdict v = JudgeOne(&t, kHalf, 0, 600);
  EXPECT_EQ(v.verdict, BeachVerdict::kPoor);
  EXPECT_DOUBLE_EQ(v.enter_height_m, 2.0);
  EXPECT_DOUBLE_EQ(v.peak_m, 2.0);
  EXPECT_DOUBLE_EQ(v.peak_at_s, kHalf);
  EXPECT_TRUE(std::isnan(v.covered_at_s));  // already covered on arrival
  EXPECT_NEAR(v.passable_from_s, Crossing(kHalf, 2.0, 0.4, 0.5), 1e-6);
  EXPECT_TRUE(std::isnan(v.good_from_s));
}

TEST(BeachTide, TheFloodCoversTheStretchPartWay) {
  const TideTable t = Simple();
  // Arrive at 6000 s (0.357 m); the water passes 0.5 m at 7200 s, before 7800 s.
  const BeachStretchVerdict v = JudgeOne(&t, 6000, 0, 1200);
  EXPECT_EQ(v.verdict, BeachVerdict::kPoor);
  EXPECT_LE(v.enter_height_m, 0.5);
  EXPECT_NEAR(v.covered_at_s, 7200, 1e-6);
  EXPECT_DOUBLE_EQ(v.peak_at_s, 7800);
  // 1800 s of passable water is needed; the next such window opens on the ebb.
  EXPECT_NEAR(v.passable_from_s, Crossing(kHalf, 2.0, 0.4, 0.5), 1e-6);
}

TEST(BeachTide, AHighWaterInsideTheStretchIsItsPeak) {
  const TideTable t = Simple();
  const BeachStretchVerdict v = JudgeOne(&t, kHalf - 1800, 0, 3000);
  EXPECT_DOUBLE_EQ(v.peak_m, 2.0);
  EXPECT_DOUBLE_EQ(v.peak_at_s, kHalf);
}

TEST(BeachTide, NoTableOrPastItsEndIsUnknown) {
  const BeachStretchVerdict none = JudgeOne(nullptr, 0, 0, 600);
  EXPECT_EQ(none.verdict, BeachVerdict::kUnknown);
  EXPECT_TRUE(std::isnan(none.peak_m));
  EXPECT_TRUE(std::isnan(none.passable_from_s));

  const TideTable t = Simple();
  const BeachStretchVerdict late = JudgeOne(&t, 64000, 0, 600);  // margin runs past 64800
  EXPECT_EQ(late.verdict, BeachVerdict::kUnknown);
  EXPECT_TRUE(std::isnan(late.enter_height_m));
  EXPECT_TRUE(std::isnan(late.passable_from_s));
}

TEST(BeachTide, EachStretchIsTimedFromTheSameDeparture) {
  const TideTable t = Simple();
  const auto v = BeachTideVerdicts({{0, 600}, {kHalf, kHalf + 600}}, &t, 0, BeachTideLimits{});
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0].verdict, BeachVerdict::kGood);
  EXPECT_EQ(v[1].verdict, BeachVerdict::kPoor);
}

TEST(BeachTide, KiawahLowAndHighWater) {
  const char* dir = std::getenv("FVW_TESTDATA_DIR");
  const std::string path =
      std::string((dir != nullptr && dir[0] != '\0') ? dir : "testdata") + "/tides/8667062.json";
  if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not fetched";
  TideTable t;
  ASSERT_TRUE(t.Load(path).ok());

  // 2026-06-01 onward: the first low and the high after it.
  const auto ex = t.Extremes(1780272000, 1780272000 + 86400);
  ASSERT_GE(ex.size(), 3u);
  const TideExtreme& low = ex[0].high ? ex[1] : ex[0];
  const TideExtreme& high = ex[0].high ? ex[2] : ex[1];

  // A 20-minute stretch centred on the low, and one at the high.
  const BeachStretchVerdict at_low = JudgeOne(&t, low.time_s - 900, 300, 1500);
  EXPECT_EQ(at_low.verdict, BeachVerdict::kGood) << "low " << low.height_m;
  const BeachStretchVerdict at_high = JudgeOne(&t, high.time_s - 600, 0, 1200);
  EXPECT_EQ(at_high.verdict, BeachVerdict::kPoor);
  EXPECT_DOUBLE_EQ(at_high.peak_m, high.height_m);
  EXPECT_GT(at_high.passable_from_s, high.time_s);
}

}  // namespace
}  // namespace nav
}  // namespace fv
