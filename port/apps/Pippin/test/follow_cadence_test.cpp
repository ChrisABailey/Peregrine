// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// P9's compass half: the follow slew's duration, on the mac.
//
// Its failure mode is the one this whole session exists to remove, and it is
// invisible in a screenshot: a duration that disagrees with the fix cadence
// is a map that either stutters once a second or trails permanently behind
// the ship. Neither shows up in a still.

#include "PPFollowCadence.h"

#include <gtest/gtest.h>

namespace {

using pippin::FixCadence;
using pippin::FollowCadenceSettings;

TEST(FollowCadence, StartsAtTheInitialEstimate) {
  FixCadence c;
  EXPECT_DOUBLE_EQ(c.interval_s(), 1.0);
  EXPECT_FALSE(c.has_observed());
}

// One stamp is not an interval — the first fix of a ride says only when it
// arrived.
TEST(FollowCadence, OneFixChangesNothing) {
  FixCadence c;
  c.Observe(100.0);
  EXPECT_TRUE(c.has_observed());
  EXPECT_DOUBLE_EQ(c.interval_s(), 1.0);
}

// A live receiver at 1 Hz: the estimate is already there and stays there.
TEST(FollowCadence, OneHertzStaysAtOneSecond) {
  FixCadence c;
  for (int i = 0; i < 10; ++i) c.Observe(100.0 + i);
  EXPECT_NEAR(c.interval_s(), 1.0, 1e-9);
}

// The demo feed at `demo_time_scale = 4`: four fixes a second, and the slew
// has to shorten to match or the map converges on a place the ride has left.
TEST(FollowCadence, FourHertzSettlesNearAQuarterSecond) {
  FixCadence c;
  double t = 100.0;
  for (int i = 0; i < 20; ++i) {
    t += 0.25;
    c.Observe(t);
  }
  EXPECT_NEAR(c.interval_s(), 0.25, 0.01);
}

// Four fixes is the settling time the smoothing weight was chosen for: from
// 1 Hz to 4 Hz, most of the way within four intervals.
TEST(FollowCadence, SettlesWithinAboutFourIntervals) {
  FixCadence c;
  double t = 100.0;
  // FIVE fixes are FOUR intervals — the first stamp is not one, which is the
  // whole of `OneFixChangesNothing` restated where it is easy to miscount.
  c.Observe(t);
  for (int i = 0; i < 4; ++i) {
    t += 0.25;
    c.Observe(t);
  }
  // 1.0 -> 0.25 with w = 0.4 four times: 0.25 + 0.75*0.6^4 = 0.347.
  EXPECT_NEAR(c.raw_estimate_s(), 0.3472, 1e-3);
  EXPECT_LT(c.interval_s(), 0.4);
}

// A burst — several fixes inside one tick, or a receiver that has just
// flushed a backlog — must not ask for an animation shorter than a frame.
TEST(FollowCadence, ClampsAtTheFloor) {
  FixCadence c;
  double t = 100.0;
  for (int i = 0; i < 30; ++i) {
    t += 0.01;
    c.Observe(t);
  }
  EXPECT_LT(c.raw_estimate_s(), 0.2);
  EXPECT_DOUBLE_EQ(c.interval_s(), 0.2);
}

TEST(FollowCadence, ClampsAtTheCeiling) {
  FixCadence c;
  double t = 100.0;
  for (int i = 0; i < 30; ++i) {
    t += 5.0;
    c.Observe(t);
  }
  EXPECT_GT(c.raw_estimate_s(), 2.0);
  EXPECT_DOUBLE_EQ(c.interval_s(), 2.0);
}

// THE GAP RULE. A tunnel, a pocket, a backgrounded app: the interval that
// spans it describes nothing, and averaging it in would leave the follow
// crawling for a dozen fixes after the feed came back.
TEST(FollowCadence, AGapResetsRatherThanAverages) {
  FixCadence c;
  double t = 100.0;
  for (int i = 0; i < 20; ++i) {
    t += 0.25;
    c.Observe(t);
  }
  ASSERT_NEAR(c.interval_s(), 0.25, 0.01);

  t += 120.0;  // two minutes in a car park
  c.Observe(t);
  EXPECT_DOUBLE_EQ(c.raw_estimate_s(), 1.0);

  // And it re-learns from there, with no memory of the gap.
  for (int i = 0; i < 20; ++i) {
    t += 0.25;
    c.Observe(t);
  }
  EXPECT_NEAR(c.interval_s(), 0.25, 0.01);
}

// Two fixes drained by one tick arrive with one stamp between them, and a
// clock that goes backwards (it does not, but the arithmetic must not care)
// is the same non-answer.
TEST(FollowCadence, NonIntervalsAreIgnored) {
  FixCadence c;
  c.Observe(100.0);
  c.Observe(100.0);
  c.Observe(99.0);
  EXPECT_DOUBLE_EQ(c.interval_s(), 1.0);
}

TEST(FollowCadence, NonFiniteIsIgnored) {
  FixCadence c;
  c.Observe(100.0);
  c.Observe(std::nan(""));
  c.Observe(101.0);
  EXPECT_NEAR(c.interval_s(), 1.0, 1e-9);
}

// The mode going on forgets a ride that ended twenty minutes ago.
TEST(FollowCadence, ResetForgets) {
  FixCadence c;
  double t = 100.0;
  for (int i = 0; i < 20; ++i) {
    t += 0.25;
    c.Observe(t);
  }
  ASSERT_NEAR(c.interval_s(), 0.25, 0.01);
  c.Reset();
  EXPECT_DOUBLE_EQ(c.interval_s(), 1.0);
  EXPECT_FALSE(c.has_observed());
}

// The pack's own numbers are honoured, band and all.
TEST(FollowCadence, SettingsAreUsed) {
  FollowCadenceSettings s;
  s.initial_s = 0.5;
  s.min_s = 0.4;
  s.max_s = 0.6;
  FixCadence c(s);
  EXPECT_DOUBLE_EQ(c.interval_s(), 0.5);
  double t = 100.0;
  for (int i = 0; i < 30; ++i) {
    t += 3.0;
    c.Observe(t);
  }
  EXPECT_DOUBLE_EQ(c.interval_s(), 0.6);
}

}  // namespace
