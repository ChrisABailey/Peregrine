// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for `pippin::DetourTrigger`: when the re-route button appears.

#include "PPDetourTrigger.h"

#include <cmath>

#include <gtest/gtest.h>

namespace {

using fv::nav::GuidanceEvent;
using fv::nav::GuidanceEventType;
using fv::nav::GuidanceState;
using pippin::DetourTrigger;

/// A fix `north_m` north of a fixed origin at time `t`.
fv::PositionFix FixAt(double t, double north_m, bool timed = true) {
  fv::PositionFix f;
  f.lat = 33.8 + north_m / 111195.0;
  f.lon = -84.44;
  f.has_position = true;
  f.time_s = t;
  f.has_time = timed;
  return f;
}

GuidanceState State(bool on_route, double along, double cross) {
  GuidanceState s;
  s.valid = true;
  s.on_route = on_route;
  s.along_m = along;
  s.cross_track_m = cross;
  return s;
}

std::vector<GuidanceEvent> Event(GuidanceEventType type) {
  GuidanceEvent e;
  e.type = type;
  return {e};
}

/// Rides on route to along 500 m, then off route from t=10, cross-track
/// growing by `per_s` each second from 30 m.
void LeaveRoute(DetourTrigger& trigger, double per_s, double until_t, bool* ready_at_19,
                bool* ready_at_20) {
  for (int t = 0; t < 10; ++t) {
    trigger.OnFix(FixAt(t, 5.0 * t), State(true, 450.0 + 5.0 * t, 3.0), {});
  }
  for (int t = 10; t <= until_t; ++t) {
    const double cross = 30.0 + per_s * (t - 10);
    trigger.OnFix(FixAt(t, 5.0 * t), State(false, 600.0, cross),
                  t == 10 ? Event(GuidanceEventType::kOffRoute) : std::vector<GuidanceEvent>{});
    if (t == 19 && ready_at_19) *ready_at_19 = trigger.ready();
    if (t == 20 && ready_at_20) *ready_at_20 = trigger.ready();
  }
}

TEST(DetourTrigger, AppearsTenSecondsAfterOffRouteWhileMovingAway) {
  DetourTrigger trigger;
  bool at19 = true, at20 = false;
  LeaveRoute(trigger, 3.0, 20, &at19, &at20);
  EXPECT_FALSE(at19);
  EXPECT_TRUE(at20);
  EXPECT_DOUBLE_EQ(trigger.last_along_m(), 495.0);
}

TEST(DetourTrigger, DoesNotAppearWhileHeadingBack) {
  DetourTrigger trigger;
  LeaveRoute(trigger, -1.0, 25, nullptr, nullptr);
  EXPECT_FALSE(trigger.ready());
}

TEST(DetourTrigger, HoldingTheSameDistanceCountsAsNotComingBack) {
  DetourTrigger trigger;
  LeaveRoute(trigger, 0.0, 20, nullptr, nullptr);
  EXPECT_TRUE(trigger.ready());
}

TEST(DetourTrigger, StaysUntilTheRiderRejoins) {
  DetourTrigger trigger;
  LeaveRoute(trigger, 3.0, 20, nullptr, nullptr);
  ASSERT_TRUE(trigger.ready());
  // Coming back does not take it away; rejoining does.
  EXPECT_FALSE(trigger.OnFix(FixAt(21, 0.0), State(false, 600.0, 20.0), {}));
  EXPECT_TRUE(trigger.ready());
  EXPECT_TRUE(trigger.OnFix(FixAt(22, 0.0), State(true, 600.0, 10.0),
                            Event(GuidanceEventType::kRejoined)));
  EXPECT_FALSE(trigger.ready());
}

TEST(DetourTrigger, FixesWithoutATimeNeverOfferIt) {
  DetourTrigger trigger;
  trigger.OnFix(FixAt(0, 0.0, false), State(false, 0.0, 40.0),
                Event(GuidanceEventType::kOffRoute));
  for (int t = 1; t < 30; ++t) {
    trigger.OnFix(FixAt(t, 5.0 * t, false), State(false, 0.0, 40.0 + t), {});
  }
  EXPECT_FALSE(trigger.ready());
}

TEST(DetourTrigger, WithoutAFixOnTheLineAlongIsTheLatestProjection) {
  DetourTrigger trigger;
  trigger.OnFix(FixAt(0, 0.0), State(true, 2400.0, 60.0), {});
  trigger.OnFix(FixAt(1, 5.0), State(false, 2410.0, 65.0), Event(GuidanceEventType::kOffRoute));
  EXPECT_DOUBLE_EQ(trigger.last_along_m(), 2410.0);
  trigger.OnFix(FixAt(2, 10.0), State(true, 2420.0, 5.0), Event(GuidanceEventType::kRejoined));
  trigger.OnFix(FixAt(3, 15.0), State(false, 3000.0, 80.0), {});
  EXPECT_DOUBLE_EQ(trigger.last_along_m(), 2420.0) << "a fix on the line wins from then on";
}

TEST(DetourTrigger, HeadingComesFromTheReceiverOrFromMovement) {
  DetourTrigger trigger;
  EXPECT_TRUE(std::isnan(trigger.heading_deg()));
  trigger.OnFix(FixAt(0, 0.0), State(true, 0.0, 0.0), {});
  trigger.OnFix(FixAt(1, 1.0), State(true, 1.0, 0.0), {});
  EXPECT_TRUE(std::isnan(trigger.heading_deg())) << "1 m is too little to measure";
  trigger.OnFix(FixAt(2, 10.0), State(true, 10.0, 0.0), {});
  EXPECT_NEAR(trigger.heading_deg(), 0.0, 0.5);

  fv::PositionFix f = FixAt(3, 20.0);
  f.true_heading_deg = 270.0;
  f.has_true_heading = true;
  trigger.OnFix(f, State(true, 20.0, 0.0), {});
  EXPECT_DOUBLE_EQ(trigger.heading_deg(), 270.0);

  trigger.Reset();
  EXPECT_TRUE(std::isnan(trigger.heading_deg()));
  EXPECT_FALSE(trigger.has_position());
}

}  // namespace
