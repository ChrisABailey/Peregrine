// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fvkit/nav/sun.h.
//
// Reference times are the US Naval Observatory's (aa.usno.navy.mil/api/rstt,
// fetched 2026-09-26, tz=0), a different algorithm from NOAA's. USNO rounds
// to the minute, so the tolerance allows half a minute of rounding on top of
// the method's own minute.

#include "fvkit/nav/sun.h"

#include <ctime>
#include <vector>

#include <gtest/gtest.h>

namespace {

using fv::nav::SunEvent;
using fv::nav::SunEvents;

constexpr double kDay = 86400.0;
constexpr double kKiawahLat = 32.6033;
constexpr double kKiawahLon = -80.1317;

/// Unix seconds for a UTC calendar time.
double Utc(int year, int month, int day, int hour = 0, int minute = 0) {
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  return static_cast<double>(timegm(&tm));
}

/// The events inside one UTC date.
std::vector<SunEvent> OnDate(double lat, double lon, int y, int m, int d) {
  const double start = Utc(y, m, d);
  return SunEvents(lat, lon, start, start + kDay - 1);
}

struct Expected {
  bool rise;
  int hour;
  int minute;
};

void ExpectEvents(const std::vector<SunEvent>& got, int y, int m, int d,
                  const std::vector<Expected>& want, double tolerance_s) {
  ASSERT_EQ(got.size(), want.size());
  for (size_t i = 0; i < want.size(); ++i) {
    EXPECT_EQ(got[i].rise, want[i].rise) << "event " << i;
    EXPECT_NEAR(got[i].time_s, Utc(y, m, d, want[i].hour, want[i].minute),
                tolerance_s)
        << "event " << i;
  }
}

TEST(Sun, KiawahAgreesWithUsnoThroughTheYear) {
  ExpectEvents(OnDate(kKiawahLat, kKiawahLon, 2026, 3, 20), 2026, 3, 20,
               {{true, 11, 24}, {false, 23, 32}}, 90);
  // The UTC date opens with the previous local evening's sunset.
  ExpectEvents(OnDate(kKiawahLat, kKiawahLon, 2026, 6, 21), 2026, 6, 21,
               {{false, 0, 31}, {true, 10, 13}}, 90);
  ExpectEvents(OnDate(kKiawahLat, kKiawahLon, 2026, 9, 27), 2026, 9, 27,
               {{true, 11, 12}, {false, 23, 10}}, 90);
  ExpectEvents(OnDate(kKiawahLat, kKiawahLon, 2026, 12, 21), 2026, 12, 21,
               {{true, 12, 18}, {false, 22, 19}}, 90);
}

TEST(Sun, FairbanksAndSydneyAgreeWithUsno) {
  // A three-hour night at 65 N, where the sun grazes the horizon.
  ExpectEvents(OnDate(64.8378, -147.7164, 2026, 6, 21), 2026, 6, 21,
               {{false, 8, 47}, {true, 10, 58}}, 120);
  // Southern hemisphere, east of Greenwich.
  ExpectEvents(OnDate(-33.8688, 151.2093, 2026, 12, 21), 2026, 12, 21,
               {{false, 9, 5}, {true, 18, 41}}, 90);
}

TEST(Sun, PolarDayAndNightHaveNoEvents) {
  EXPECT_TRUE(OnDate(70.0, 20.0, 2026, 6, 21).empty());
  EXPECT_TRUE(OnDate(70.0, 20.0, 2026, 12, 21).empty());
}

TEST(Sun, RisesAndSetsAlternateThroughAYearAtKiawah) {
  const double t0 = Utc(2026, 1, 1);
  const auto events = SunEvents(kKiawahLat, kKiawahLon, t0, t0 + 365 * kDay);
  ASSERT_GE(events.size(), 729u);
  ASSERT_LE(events.size(), 731u);
  for (size_t i = 1; i < events.size(); ++i) {
    EXPECT_NE(events[i].rise, events[i - 1].rise) << i;
    const double gap = events[i].time_s - events[i - 1].time_s;
    // Day and night at 32.6 N each run between about 10 and 14.5 hours.
    EXPECT_GT(gap, 9.5 * 3600) << i;
    EXPECT_LT(gap, 14.6 * 3600) << i;
  }
}

TEST(Sun, RangeIsInclusiveAndEmptyWhenReversed) {
  const auto day = OnDate(kKiawahLat, kKiawahLon, 2026, 9, 27);
  ASSERT_EQ(day.size(), 2u);
  const double rise = day[0].time_s;
  const auto exact = SunEvents(kKiawahLat, kKiawahLon, rise, rise);
  ASSERT_EQ(exact.size(), 1u);
  EXPECT_TRUE(exact[0].rise);
  EXPECT_TRUE(SunEvents(kKiawahLat, kKiawahLon, rise, rise - 1).empty());
}

}  // namespace
