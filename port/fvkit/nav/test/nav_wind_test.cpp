// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fvkit/nav/wind.h.
//
// The committed fixture data/nws-chs-82-68-2026-09-26.json is a real
// api.weather.gov gridpoint document for Kiawah's beach (office CHS, cell
// 82,68), trimmed to the three wind series.

#include "fvkit/nav/wind.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace fv {
namespace nav {
namespace {

constexpr double kKmh = 1000.0 / 3600.0;
/// 2026-09-26T12:00:00Z, the fixture's first hour.
constexpr double kStart = 1790424000.0;

std::string Fixture() {
  std::ifstream f(FV_WIND_FIXTURE);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

WindForecast Loaded() {
  WindForecast w;
  const Status s = w.Parse(Fixture());
  EXPECT_TRUE(s.ok()) << s.message;
  return w;
}

TEST(WindForecast, ReadsTheNwsFixture) {
  const WindForecast w = Loaded();
  ASSERT_FALSE(w.empty());
  EXPECT_DOUBLE_EQ(w.UpdateTime(), 1790448650.0);  // 2026-09-26T18:50:50Z
  EXPECT_DOUBLE_EQ(w.ValidUntil(), 1791075600.0);  // 2026-10-04T01:00:00Z

  WindSample s;
  ASSERT_TRUE(w.At(kStart, &s).ok());
  EXPECT_NEAR(s.speed_mps, 16.668 * kKmh, 1e-9);
  EXPECT_NEAR(s.gust_mps, 27.78 * kKmh, 1e-9);
  EXPECT_DOUBLE_EQ(s.from_deg, 0.0);

  // Inside a three-hour speed interval and a four-hour gust interval.
  ASSERT_TRUE(w.At(kStart + 1.5 * 3600, &s).ok());
  EXPECT_NEAR(s.speed_mps, 18.52 * kKmh, 1e-9);
  EXPECT_NEAR(s.gust_mps, 27.78 * kKmh, 1e-9);

  // Each series changes on its own hour.
  ASSERT_TRUE(w.At(kStart + 5.5 * 3600, &s).ok());
  EXPECT_DOUBLE_EQ(s.from_deg, 350.0);
}

TEST(WindForecast, NothingOutsideTheForecast) {
  const WindForecast w = Loaded();
  WindSample s;
  EXPECT_EQ(w.At(kStart - 1, &s).code, kOutOfCoverage);
  EXPECT_EQ(w.At(w.ValidUntil(), &s).code, kOutOfCoverage);
  EXPECT_TRUE(w.At(w.ValidUntil() - 1, &s).ok());
}

TEST(WindForecast, MissingGustIsNaNAndNullValuesAreSkipped) {
  WindForecast w;
  const Status st = w.Parse(R"j({"properties": {
      "windSpeed": {"uom": "wmoUnit:m_s-1", "values": [
        {"validTime": "2026-01-01T00:00:00Z/PT2H", "value": 5},
        {"validTime": "2026-01-01T02:00:00Z/PT1H", "value": null}]},
      "windDirection": {"uom": "wmoUnit:degree_(angle)", "values": [
        {"validTime": "2026-01-01T00:00:00+00:00/P1DT1H", "value": 90}]}}})j");
  ASSERT_TRUE(st.ok()) << st.message;
  WindSample s;
  ASSERT_TRUE(w.At(1767225600.0 + 3600, &s).ok());
  EXPECT_DOUBLE_EQ(s.speed_mps, 5.0);
  EXPECT_TRUE(std::isnan(s.gust_mps));
  // The null hour is a gap, not a zero.
  EXPECT_EQ(w.At(1767225600.0 + 2.5 * 3600, &s).code, kOutOfCoverage);
}

TEST(WindForecast, RefusesWhatItCannotRead) {
  WindForecast w;
  EXPECT_EQ(w.Parse("not json").code, kInvalidArg);
  EXPECT_EQ(w.Parse(R"j({"properties": {}})j").code, kInvalidArg);
  EXPECT_EQ(w.Parse(R"j({"properties": {
      "windSpeed": {"uom": "wmoUnit:kn", "values": []},
      "windDirection": {"uom": "wmoUnit:degree_(angle)", "values": []}}})j")
                .code,
            kUnsupported);
  EXPECT_EQ(w.Parse(R"j({"properties": {
      "windSpeed": {"uom": "wmoUnit:km_h-1", "values": [
        {"validTime": "2026-01-01T00:00:00Z/P1M", "value": 5}]},
      "windDirection": {"uom": "wmoUnit:degree_(angle)", "values": []}}})j")
                .code,
            kInvalidArg);
  EXPECT_TRUE(w.empty());
}

TEST(WindForecast, WrongTypesAreRefusedNotThrown) {
  WindForecast w;
  EXPECT_EQ(w.Parse(R"j({"properties": {
      "windSpeed": {"uom": null, "values": []},
      "windDirection": {"uom": "wmoUnit:degree_(angle)", "values": []}}})j")
                .code,
            kUnsupported);
  EXPECT_EQ(w.Parse(R"j({"properties": {
      "windSpeed": {"uom": "wmoUnit:km_h-1", "values": [{"validTime": 7, "value": 5}]},
      "windDirection": {"uom": "wmoUnit:degree_(angle)", "values": []}}})j")
                .code,
            kInvalidArg);
  // A duration too long for a double is refused rather than thrown.
  const std::string huge = "2026-01-01T00:00:00Z/PT" + std::string(400, '9') + "H";
  EXPECT_EQ(w.Parse(R"j({"properties": {
      "windSpeed": {"uom": "wmoUnit:km_h-1", "values": [{"validTime": ")j" + huge +
                    R"j(", "value": 5}]},
      "windDirection": {"uom": "wmoUnit:degree_(angle)", "values": []}}})j")
                .code,
            kInvalidArg);
  // A null updateTime is no update time, not a failure.
  ASSERT_TRUE(w.Parse(R"j({"properties": {"updateTime": null,
      "windSpeed": {"uom": "wmoUnit:km_h-1", "values": [
        {"validTime": "2026-01-01T00:00:00Z/PT1H", "value": 5}]},
      "windDirection": {"uom": "wmoUnit:degree_(angle)", "values": [
        {"validTime": "2026-01-01T00:00:00Z/PT1H", "value": 90}]}}})j")
                  .ok());
  EXPECT_EQ(w.UpdateTime(), 0.0);
}

TEST(WindForecast, IntervalsCarryTheirOffsetAndDuration) {
  double b = 0, e = 0;
  ASSERT_TRUE(ParseNwsInterval("2026-09-26T08:00:00-04:00/P1DT2H30M", &b, &e));
  EXPECT_DOUBLE_EQ(b, kStart);
  EXPECT_DOUBLE_EQ(e - b, 86400 + 2 * 3600 + 30 * 60);
  EXPECT_FALSE(ParseNwsInterval("2026-09-26T12:00:00Z", &b, &e));
  EXPECT_FALSE(ParseNwsInterval("2026-09-26T12:00:00Z/PT", &b, &e));
}

TEST(WindComponents, TailwindHeadwindAndCrosswind) {
  const WindSample from_ne{10.0, NAN, 45.0};
  // Blowing toward the SW: behind a rider heading SW, into one heading NE.
  EXPECT_NEAR(TailwindComponent(from_ne, 225.0), 10.0, 1e-9);
  EXPECT_NEAR(TailwindComponent(from_ne, 45.0), -10.0, 1e-9);
  EXPECT_NEAR(TailwindComponent(from_ne, 135.0), 0.0, 1e-9);
  // Kiawah's beach runs about 077/257; a NE wind is 32 degrees off the line
  // behind a rider heading west.
  EXPECT_NEAR(TailwindComponent(from_ne, 257.0), 10.0 * std::cos(32.0 * M_PI / 180), 1e-9);
}

TEST(WindComponents, TrueBearing) {
  EXPECT_NEAR(TrueBearingDeg({0, 0}, {1, 0}), 0.0, 1e-9);
  EXPECT_NEAR(TrueBearingDeg({0, 0}, {0, 1}), 90.0, 1e-9);
  EXPECT_NEAR(TrueBearingDeg({0, 0}, {0, -1}), 270.0, 1e-9);
  // Boardwalk 29 to Boardwalk 41 along Kiawah's beach.
  EXPECT_NEAR(TrueBearingDeg({32.602374, -80.0838337}, {32.6100451, -80.045189}), 76.7, 0.1);
  EXPECT_DOUBLE_EQ(TrueBearingDeg({32.6, -80.1}, {32.6, -80.1}), 0.0);
}

TEST(WindComponents, OnshoreAndTheSeawardSide) {
  // Kiawah faces about 167; riding east along it (077) the sea is on the right.
  EXPECT_NEAR(SeawardOf(77.0, 167.0), 167.0, 1e-9);
  // Riding west (257) the sea is on the left, still 167.
  EXPECT_NEAR(SeawardOf(257.0, 167.0), 167.0, 1e-9);
  // Round the east end, where the shore bends north-east, the side holds.
  EXPECT_NEAR(SeawardOf(30.0, 167.0), 120.0, 1e-9);

  const WindSample from_sse{8.0, NAN, 167.0};
  EXPECT_NEAR(OnshoreComponent(from_sse, 167.0), 8.0, 1e-9);
  const WindSample from_nnw{8.0, NAN, 347.0};
  EXPECT_NEAR(OnshoreComponent(from_nnw, 167.0), -8.0, 1e-9);
}

}  // namespace
}  // namespace nav
}  // namespace fv
