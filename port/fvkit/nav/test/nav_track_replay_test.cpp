// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for sampling a recorded track on a replay clock.

#include "fvkit/nav/track_replay.h"

#include <gtest/gtest.h>

#include <vector>

namespace fv {
namespace {

/// Three fixes due east along 10 N, ten seconds apart, climbing 100 m each.
std::vector<PositionFix> EastTrack() {
  std::vector<PositionFix> track(3);
  for (int i = 0; i < 3; ++i) {
    track[i].SetPosition(10.0, 20.0 + i);
    track[i].time_s = 100.0 + 10.0 * i;
    track[i].has_time = true;
    track[i].altitude_msl_m = 100.0 * i;
    track[i].has_altitude = true;
  }
  return track;
}

TEST(TrackReplay, InterpolatesBetweenTheBracketingFixes) {
  std::vector<PositionFix> track = EastTrack();
  track[1].true_heading_deg = 45.0;
  track[1].has_true_heading = true;

  TrackState s = TrackStateAt(track, 105.0);
  EXPECT_EQ(s.phase, ReplayPhase::kDuring);
  EXPECT_EQ(s.past, 1u);
  EXPECT_DOUBLE_EQ(s.fix.lon, 20.5);
  EXPECT_DOUBLE_EQ(s.fix.altitude_msl_m, 50.0);
  EXPECT_DOUBLE_EQ(s.fix.time_s, 105.0);

  s = TrackStateAt(track, 115.0);
  EXPECT_DOUBLE_EQ(s.fix.true_heading_deg, 45.0);  // the fix's own
  EXPECT_TRUE(s.heading.reported);
  EXPECT_EQ(s.past, 2u);

  s = TrackStateAt(track, 50.0);
  EXPECT_EQ(s.phase, ReplayPhase::kBefore);
  EXPECT_EQ(s.past, 0u);
  EXPECT_DOUBLE_EQ(s.fix.lon, 20.0);
  EXPECT_DOUBLE_EQ(s.fix.time_s, 100.0);

  s = TrackStateAt(track, 120.0);
  EXPECT_EQ(s.phase, ReplayPhase::kDuring);
  EXPECT_EQ(s.past, 3u);
  s = TrackStateAt(track, 500.0);
  EXPECT_EQ(s.phase, ReplayPhase::kAfter);
  EXPECT_DOUBLE_EQ(s.fix.lon, 22.0);

  EXPECT_FALSE(TrackStateAt({}, 0.0).fix.has_position);
}

TEST(TrackReplay, ASegmentBearingIsDerivedNotReported) {
  const std::vector<PositionFix> track = EastTrack();
  for (double t : {50.0, 105.0, 120.0, 500.0}) {
    const TrackState s = TrackStateAt(track, t);
    EXPECT_FALSE(s.fix.has_true_heading) << t;
    ASSERT_TRUE(s.heading.known) << t;
    EXPECT_FALSE(s.heading.reported) << t;
    EXPECT_NEAR(s.heading.degrees, 90.0, 0.1) << t;
  }
  // Before the start and after the end the first and last segments stand in.
  EXPECT_DOUBLE_EQ(TrackStateAt(track, 50.0).segment_to.lon, 21.0);
  EXPECT_DOUBLE_EQ(TrackStateAt(track, 500.0).segment_from.lon, 21.0);

  const std::vector<PositionFix> one(track.begin(), track.begin() + 1);
  EXPECT_FALSE(TrackStateAt(one, 100.0).heading.known);
}

TEST(TrackReplay, ReplayableFixesNeedATimeLine) {
  std::vector<PositionFix> fixes(4);
  for (int i = 0; i < 4; ++i) fixes[i].SetPosition(0.0, i);
  std::vector<PositionFix> timed = ReplayableFixes(fixes);
  ASSERT_EQ(timed.size(), 4u);
  EXPECT_DOUBLE_EQ(timed[3].time_s, 3.0);

  const double times[] = {10.0, 12.0, 12.0, 11.0};
  for (int i = 0; i < 4; ++i) {
    fixes[i].time_s = times[i];
    fixes[i].has_time = true;
  }
  fixes.push_back(PositionFix());  // no position
  timed = ReplayableFixes(fixes);
  ASSERT_EQ(timed.size(), 2u);
  EXPECT_DOUBLE_EQ(timed[1].time_s, 12.0);
}

}  // namespace
}  // namespace fv
