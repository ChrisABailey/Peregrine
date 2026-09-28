// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fvkit/nav/tide.h.
//
// Synthetic tables pin each rule in the header. The committed fixture
// data/charleston-2026-03.json holds a month of NOAA's hi/lo predictions for
// Charleston with NOAA's own 6-minute predictions beside them, and measures
// how far the cosine curve departs from the full harmonic one. The five-year
// Kiawah table, testdata/tides/8667062.json, is used when present. Both come
// from port/tools/fetch_tides.py.

#include "fvkit/nav/tide.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace fv {
namespace nav {
namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream f(path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string KiawahTablePath() {
  const char* dir = std::getenv("FVW_TESTDATA_DIR");
  const std::string root = (dir != nullptr && dir[0] != '\0') ? dir : "testdata";
  return root + "/tides/8667062.json";
}

/// A table of the given extremes, valid from the first to the last.
std::string TableJson(const std::string& extremes, double from, double until,
                      const std::string& units = "m") {
  std::ostringstream ss;
  ss << "{\"units\": \"" << units << "\", \"datum\": \"MLLW\", \"valid_from\": " << from
     << ", \"valid_until\": " << until << ", \"station\": {\"id\": \"1\", \"name\": \"Test\"}"
     << ", \"extremes\": [" << extremes << "]}";
  return ss.str();
}

/// Low 0.0 m at 0, high 2.0 m at 6 h, low 0.4 m at 12 h, high 1.6 m at 18 h.
TideTable Simple() {
  TideTable t;
  const Status s = t.Parse(TableJson(
      "[0, 0.0, \"L\"], [21600, 2.0, \"H\"], [43200, 0.4, \"L\"], [64800, 1.6, \"H\"]", 0,
      64800));
  EXPECT_TRUE(s.ok()) << s.message;
  return t;
}

TEST(TideTable, CurvePassesThroughEveryExtreme) {
  const TideTable t = Simple();
  for (const TideExtreme& e : t.Extremes(0, 64800)) {
    double h = -1;
    ASSERT_TRUE(t.HeightAt(e.time_s, &h).ok());
    EXPECT_DOUBLE_EQ(h, e.height_m) << "at " << e.time_s;
  }
}

TEST(TideTable, MidTideIsTheMeanOfItsTwoExtremes) {
  const TideTable t = Simple();
  double h = 0;
  ASSERT_TRUE(t.HeightAt(10800, &h).ok());
  EXPECT_NEAR(h, 1.0, 1e-12);
  ASSERT_TRUE(t.HeightAt(32400, &h).ok());
  EXPECT_NEAR(h, 1.2, 1e-12);
}

TEST(TideTable, TrendFollowsTheHalfCycle) {
  const TideTable t = Simple();
  TideTrend tr;
  ASSERT_TRUE(t.TrendAt(10800, &tr).ok());
  EXPECT_TRUE(tr.rising);
  // Peak rate of a 2 m rise over 6 h: amp * pi / span.
  EXPECT_NEAR(tr.rate_m_per_h, 1.0 * M_PI / 6.0, 1e-12);
  ASSERT_TRUE(t.TrendAt(32400, &tr).ok());
  EXPECT_FALSE(tr.rising);
  EXPECT_LT(tr.rate_m_per_h, 0.0);
  // At the high water itself: falling from here, not yet moving.
  ASSERT_TRUE(t.TrendAt(21600, &tr).ok());
  EXPECT_FALSE(tr.rising);
  EXPECT_NEAR(tr.rate_m_per_h, 0.0, 1e-12);
}

TEST(TideTable, OutsideTheTableIsOutOfCoverageNeverAClamp) {
  const TideTable t = Simple();
  double h = 42;
  EXPECT_EQ(t.HeightAt(-1, &h).code, kOutOfCoverage);
  EXPECT_EQ(t.HeightAt(64801, &h).code, kOutOfCoverage);
  EXPECT_EQ(h, 42);
  TideTrend tr;
  EXPECT_EQ(t.TrendAt(-1, &tr).code, kOutOfCoverage);
  std::vector<TideWindow> w;
  EXPECT_EQ(t.WindowsBelow(1.0, 0, 64801, &w).code, kOutOfCoverage);
  EXPECT_TRUE(t.HeightAt(64800, &h).ok());  // the last instant is inside
  EXPECT_DOUBLE_EQ(h, 1.6);
}

TEST(TideTable, AnEmptyTableAnswersNothing) {
  TideTable t;
  double h;
  EXPECT_EQ(t.HeightAt(0, &h).code, kNotFound);
}

TEST(TideTable, ExtremesAreClippedToTheQuery) {
  const TideTable t = Simple();
  const auto e = t.Extremes(1, 43200);
  ASSERT_EQ(e.size(), 2u);
  EXPECT_TRUE(e[0].high);
  EXPECT_EQ(e[1].time_s, 43200);
}

TEST(TideTable, WindowsBelowSolvesEachCrossing) {
  const TideTable t = Simple();
  std::vector<TideWindow> w;
  ASSERT_TRUE(t.WindowsBelow(1.0, 0, 64800, &w).ok());
  // Below 1.0 m from 0 to mid-rise (3 h), and again across the second low:
  // the fall from 2.0 to 0.4 crosses 1.0 where cos(pi u) = -0.25.
  ASSERT_EQ(w.size(), 2u);
  EXPECT_NEAR(w[0].begin_s, 0, 1e-9);
  EXPECT_NEAR(w[0].end_s, 10800, 1e-9);
  const double fall = 21600 + std::acos(-0.25) / M_PI * 21600;
  EXPECT_NEAR(w[1].begin_s, fall, 1e-6);
  // Rising 0.4 -> 1.6: mean 1.0, so the crossing is at mid-rise.
  EXPECT_NEAR(w[1].end_s, 43200 + 10800, 1e-6);
}

TEST(TideTable, WindowsBelowMergesAcrossALowWater) {
  const TideTable t = Simple();
  std::vector<TideWindow> w;
  ASSERT_TRUE(t.WindowsBelow(0.5, 21600, 64800, &w).ok());
  ASSERT_EQ(w.size(), 1u);
  EXPECT_LT(w[0].begin_s, 43200);
  EXPECT_GT(w[0].end_s, 43200);
}

TEST(TideTable, WindowsBelowAtTheEdges) {
  const TideTable t = Simple();
  std::vector<TideWindow> w;
  ASSERT_TRUE(t.WindowsBelow(3.0, 100, 60000, &w).ok());
  ASSERT_EQ(w.size(), 1u);
  EXPECT_EQ(w[0].begin_s, 100);
  EXPECT_EQ(w[0].end_s, 60000);
  ASSERT_TRUE(t.WindowsBelow(-0.1, 0, 64800, &w).ok());
  EXPECT_TRUE(w.empty());
  // Exactly the second low's height touches it for an instant: no window.
  ASSERT_TRUE(t.WindowsBelow(0.4, 21600, 64800, &w).ok());
  EXPECT_TRUE(w.empty());
  EXPECT_EQ(t.WindowsBelow(1.0, 500, 400, &w).code, kInvalidArg);
}

TEST(TideTable, ParseRejectsWhatTheCurveCannotDraw) {
  TideTable t;
  EXPECT_EQ(t.Parse("not json").code, kInvalidArg);
  EXPECT_EQ(t.Parse(TableJson("[0, 0.0, \"L\"], [100, 1.0, \"H\"]", 0, 100, "ft")).code,
            kUnsupported);
  EXPECT_EQ(t.Parse(TableJson("[0, 0.0, \"L\"], [100, 1.0, \"L\"]", 0, 100)).code,
            kInvalidArg);  // two lows
  EXPECT_EQ(t.Parse(TableJson("[100, 0.0, \"L\"], [0, 1.0, \"H\"]", 0, 100)).code,
            kInvalidArg);  // out of order
  EXPECT_EQ(t.Parse(TableJson("[0, 1.0, \"L\"], [100, 0.0, \"H\"]", 0, 100)).code,
            kInvalidArg);  // a high below its low
  EXPECT_EQ(t.Parse(TableJson("[0, 0.0, \"L\"], [100, 1.0, \"H\"]", 0, 200)).code,
            kInvalidArg);  // does not cover the valid span
  EXPECT_TRUE(t.empty());
  EXPECT_EQ(t.Load("/no/such/tides.json").code, kNotFound);
}

// The cosine curve against NOAA's full harmonic prediction at Charleston over
// March 2026, which holds both spring and neap tides.
TEST(TideTable, InterpolationErrorAgainstNoaaSixMinute) {
  const std::string path = FV_TIDE_FIXTURE;
  TideTable t;
  const Status s = t.Load(path);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(t.station().id, "8665530");

  const auto doc = nlohmann::json::parse(ReadFile(path));
  const auto& samples = doc["samples"];
  const double start = samples["start"].get<double>();
  const double step = samples["step_s"].get<double>();
  const auto& heights = samples["heights_m"];
  ASSERT_EQ(heights.size(), 31u * 240u);

  double max_err = 0, sum_sq = 0;
  for (size_t i = 0; i < heights.size(); ++i) {
    double h = 0;
    ASSERT_TRUE(t.HeightAt(start + i * step, &h).ok());
    const double err = std::fabs(h - heights[i].get<double>());
    max_err = std::max(max_err, err);
    sum_sq += err * err;
  }
  const double rms = std::sqrt(sum_sq / heights.size());
  RecordProperty("max_err_m", std::to_string(max_err));
  RecordProperty("rms_err_m", std::to_string(rms));
  // Measured 0.137 m max, 0.051 m RMS; the worst is mid-tide on a spring range.
  EXPECT_LE(max_err, 0.140);
  EXPECT_LE(rms, 0.052);
}

TEST(TideTable, KiawahFiveYears) {
  const std::string path = KiawahTablePath();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not fetched";
  TideTable t;
  const Status s = t.Load(path);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(t.station().id, "8667062");
  EXPECT_EQ(t.station().reference_id, "8665530");
  EXPECT_EQ(t.datum(), "MLLW");
  EXPECT_EQ(t.ValidFrom(), 1767225600);   // 2026-01-01T00:00Z
  EXPECT_EQ(t.ValidUntil(), 1924992000);  // 2031-01-01T00:00Z

  const auto all = t.Extremes(t.ValidFrom(), t.ValidUntil());
  EXPECT_GT(all.size(), 7000u);
  for (const TideExtreme& e : all) {
    double h = 0;
    ASSERT_TRUE(t.HeightAt(e.time_s, &h).ok());
    ASSERT_DOUBLE_EQ(h, e.height_m);
  }
}

// The analytic windows against the same question asked once a second.
TEST(TideTable, WindowsBelowAgreesWithOneSecondSampling) {
  const std::string path = KiawahTablePath();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not fetched";
  TideTable t;
  ASSERT_TRUE(t.Load(path).ok());
  const double t0 = 1790000000, t1 = t0 + 3 * 86400;
  for (double thr : {0.3, 0.8, 1.5}) {
    std::vector<TideWindow> w;
    ASSERT_TRUE(t.WindowsBelow(thr, t0, t1, &w).ok());
    std::vector<TideWindow> sampled;
    bool in = false;
    for (double s = t0; s <= t1; s += 1) {
      double h;
      ASSERT_TRUE(t.HeightAt(s, &h).ok());
      const bool below = h <= thr;
      if (below && !in) sampled.push_back({s, s});
      if (below) sampled.back().end_s = s;
      in = below;
    }
    ASSERT_EQ(w.size(), sampled.size()) << "threshold " << thr;
    for (size_t i = 0; i < w.size(); ++i) {
      EXPECT_NEAR(w[i].begin_s, sampled[i].begin_s, 1.0) << thr << " #" << i;
      EXPECT_NEAR(w[i].end_s, sampled[i].end_s, 1.0) << thr << " #" << i;
    }
  }
}

}  // namespace
}  // namespace nav
}  // namespace fv
