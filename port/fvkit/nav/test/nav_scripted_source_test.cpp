// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// The scripted feed (nav plan MM1). Every test here drives a FAKE CLOCK the
// test advances by hand — no sleeping, no tolerance, no flake, which is the
// property the source's injectable clock exists to buy.

#include "fvkit/nav/scripted_source.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "fvkit/nav/heading.h"

namespace {

using fv::GeoPoint;
using fv::PositionFix;
using fv::ScriptedFix;
using fv::ScriptedSource;
using fv::ScriptedTrackOptions;

// A clock the test owns.
struct FakeClock {
  double now = 1000.0;  // a non-zero origin, so nothing can assume one
  fv::MonotonicClock fn() {
    return [this] { return now; };
  }
};

std::vector<ScriptedFix> ThreeFixesOneSecondApart() {
  std::vector<ScriptedFix> track;
  for (int i = 0; i < 3; ++i) {
    ScriptedFix entry;
    entry.t_s = static_cast<double>(i);
    entry.fix.SetPosition(static_cast<double>(i), 0.0);
    track.push_back(entry);
  }
  return track;
}

// Great-circle metres, local to the test so geo_tool's macros stay out of it.
double MetresBetween(const GeoPoint& a, const GeoPoint& b) {
  const double kR = 6378137.0;
  const double kDegToRad = 3.14159265358979323846 / 180.0;
  const double dlat = (b.lat - a.lat) * kDegToRad;
  const double dlon = (b.lon - a.lon) * kDegToRad;
  const double lat1 = a.lat * kDegToRad;
  const double lat2 = b.lat * kDegToRad;
  const double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
                   std::cos(lat1) * std::cos(lat2) * std::sin(dlon / 2) * std::sin(dlon / 2);
  return 2.0 * kR * std::asin(std::sqrt(h));
}

TEST(ScriptedSource, RefusesToStartWithNoTrack) {
  ScriptedSource source;
  const fv::Status status = source.Start();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(fv::kInvalidArg, status.code);
  EXPECT_FALSE(source.running());
}

// A source is allowed to deliver from inside Start(), and this one does —
// which is why the seam documents that the listener is set first.
TEST(ScriptedSource, EmitsTheFixDueAtZeroFromInsideStart) {
  FakeClock clock;
  ScriptedSource source(ThreeFixesOneSecondApart());
  source.SetClock(clock.fn());

  std::vector<PositionFix> seen;
  source.SetListener([&seen](const PositionFix& fix) { seen.push_back(fix); });

  ASSERT_TRUE(source.Start().ok());
  ASSERT_EQ(1u, seen.size());
  EXPECT_DOUBLE_EQ(0.0, seen[0].lat);
}

TEST(ScriptedSource, EmitsEachFixWhenItsTimeArrivesAndNotBefore) {
  FakeClock clock;
  ScriptedSource source(ThreeFixesOneSecondApart());
  source.SetClock(clock.fn());
  std::vector<PositionFix> seen;
  source.SetListener([&seen](const PositionFix& fix) { seen.push_back(fix); });
  ASSERT_TRUE(source.Start().ok());

  clock.now += 0.9;
  EXPECT_EQ(0u, source.Poll());
  EXPECT_EQ(1u, seen.size());

  clock.now += 0.1;  // t = 1.0 exactly
  EXPECT_EQ(1u, source.Poll());
  EXPECT_DOUBLE_EQ(1.0, seen.back().lat);

  // A jump forward past several fixes delivers them all, in order: the
  // consumer's tick is not required to be as fast as the script.
  clock.now += 5.0;
  EXPECT_EQ(1u, source.Poll());
  ASSERT_EQ(3u, seen.size());
  EXPECT_DOUBLE_EQ(2.0, seen.back().lat);
  EXPECT_TRUE(source.finished());
  EXPECT_EQ(0u, source.Poll());
}

TEST(ScriptedSource, TimeScaleReplaysFasterThanRealTime) {
  FakeClock clock;
  ScriptedSource source(ThreeFixesOneSecondApart());
  source.SetClock(clock.fn());
  source.SetTimeScale(10.0);
  std::vector<PositionFix> seen;
  source.SetListener([&seen](const PositionFix& fix) { seen.push_back(fix); });
  ASSERT_TRUE(source.Start().ok());

  clock.now += 0.2;  // 2 script seconds
  EXPECT_EQ(2u, source.Poll());
  EXPECT_EQ(3u, seen.size());
  EXPECT_NEAR(2.0, source.script_time_s(), 1e-9);

  source.SetTimeScale(0.0);  // ignored: a scale must be positive
  EXPECT_DOUBLE_EQ(10.0, source.time_scale());
}

TEST(ScriptedSource, RestartsFromTheTop) {
  FakeClock clock;
  ScriptedSource source(ThreeFixesOneSecondApart());
  source.SetClock(clock.fn());
  std::vector<PositionFix> seen;
  source.SetListener([&seen](const PositionFix& fix) { seen.push_back(fix); });

  ASSERT_TRUE(source.Start().ok());
  clock.now += 10.0;
  source.Poll();
  ASSERT_EQ(3u, seen.size());

  source.Stop();
  seen.clear();
  ASSERT_TRUE(source.Start().ok());
  ASSERT_EQ(1u, seen.size());
  EXPECT_DOUBLE_EQ(0.0, seen[0].lat);
}

TEST(ScriptedSource, LoopsWithoutDriftingAcrossTheLapBoundary) {
  FakeClock clock;
  ScriptedSource source(ThreeFixesOneSecondApart());
  source.SetClock(clock.fn());
  source.SetLooping(true);
  std::vector<PositionFix> seen;
  source.SetListener([&seen](const PositionFix& fix) { seen.push_back(fix); });
  ASSERT_TRUE(source.Start().ok());

  // The script spans 2 seconds, so the lap boundary falls at exactly 2.0 —
  // where the script's last instant and the next lap's first are the same
  // one, which is the closed-circuit note on SetLooping.
  clock.now += 2.0;
  source.Poll();
  ASSERT_EQ(4u, seen.size());
  EXPECT_DOUBLE_EQ(2.0, seen[2].lat);  // end of lap one
  EXPECT_DOUBLE_EQ(0.0, seen[3].lat);  // start of lap two, same instant

  // And the new lap runs on the same clock as the old one: no drift, so the
  // second fix of lap two is due at 3.0 exactly and not a tick later.
  clock.now += 0.9;
  EXPECT_EQ(0u, source.Poll());
  clock.now += 0.1;
  EXPECT_EQ(1u, source.Poll());
  EXPECT_DOUBLE_EQ(1.0, seen.back().lat);
  EXPECT_FALSE(source.finished());
}

// A listener is allowed to stop the source it is being called from — a shell
// that closes the overlay on a fix must not then be handed the rest of them.
TEST(ScriptedSource, StopsInsideItsOwnListener) {
  FakeClock clock;
  ScriptedSource source(ThreeFixesOneSecondApart());
  source.SetClock(clock.fn());
  int count = 0;
  source.SetListener([&](const PositionFix&) {
    ++count;
    source.Stop();
  });
  ASSERT_TRUE(source.Start().ok());
  clock.now += 10.0;
  source.Poll();
  EXPECT_EQ(1, count);
}

// ---------------------------------------------------------------------------
// BuildScriptedTrack
// ---------------------------------------------------------------------------

// A due-east leg off Kiawah at 10 m/s, sampled at 1 Hz.
std::vector<GeoPoint> EastLeg() {
  return {GeoPoint{32.60, -80.10}, GeoPoint{32.60, -80.00}};
}

TEST(BuildScriptedTrack, SamplesAPathAtAConstantGroundSpeed) {
  const auto track = fv::BuildScriptedTrack(EastLeg(), 10.0);
  ASSERT_GE(track.size(), 3u);

  // One second and ten metres between consecutive samples, everywhere but the
  // final endpoint sample, which lands where the path ends.
  for (std::size_t i = 1; i + 1 < track.size(); ++i) {
    EXPECT_NEAR(1.0, track[i].t_s - track[i - 1].t_s, 1e-9);
    const double d = MetresBetween(track[i - 1].fix.position(), track[i].fix.position());
    EXPECT_NEAR(10.0, d, 0.05) << "sample " << i;
  }

  EXPECT_DOUBLE_EQ(0.0, track.front().t_s);
  EXPECT_NEAR(32.60, track.back().fix.lat, 1e-9);
  EXPECT_NEAR(-80.00, track.back().fix.lon, 1e-9);
  EXPECT_GT(track.back().t_s, track[track.size() - 2].t_s);
}

TEST(BuildScriptedTrack, FillsInTheFieldsAReceiverWouldReport) {
  ScriptedTrackOptions options;
  options.sample_interval_s = 2.0;
  options.start_time_s = 1786000000.0;
  options.set_altitude = true;
  options.altitude_msl_m = 3.0;
  options.set_hdop = true;
  options.hdop = 0.8;

  const auto track = fv::BuildScriptedTrack(EastLeg(), 20.0, options);
  ASSERT_FALSE(track.empty());

  const PositionFix& fix = track[1].fix;
  EXPECT_TRUE(fix.has_position);
  EXPECT_TRUE(fix.has_speed);
  EXPECT_DOUBLE_EQ(20.0, fix.speed_mps);
  EXPECT_TRUE(fix.has_true_heading);
  EXPECT_NEAR(90.0, fix.true_heading_deg, 0.05);  // due east
  EXPECT_TRUE(fix.has_altitude);
  EXPECT_DOUBLE_EQ(3.0, fix.altitude_msl_m);
  EXPECT_TRUE(fix.has_hdop);
  EXPECT_TRUE(fix.has_time);
  EXPECT_DOUBLE_EQ(options.start_time_s + track[1].t_s, fix.time_s);

  // A track with no start time carries no time at all: a 1970 stamp is worse
  // than an absent one.
  const auto untimed = fv::BuildScriptedTrack(EastLeg(), 20.0);
  EXPECT_FALSE(untimed.front().fix.has_time);
}

TEST(BuildScriptedTrack, TurnsACornerAndTheHeadingTurnsWithIt) {
  const std::vector<GeoPoint> corner = {
      GeoPoint{32.60, -80.02}, GeoPoint{32.60, -80.00}, GeoPoint{32.62, -80.00}};
  const auto track = fv::BuildScriptedTrack(corner, 50.0);
  ASSERT_GE(track.size(), 4u);
  EXPECT_NEAR(90.0, track.front().fix.true_heading_deg, 0.5);   // east
  EXPECT_NEAR(0.0, fv::NormalizeHeadingDeg(track.back().fix.true_heading_deg), 0.5);  // north
}

TEST(BuildScriptedTrack, IsEmptyWhenThereIsNothingToPlay) {
  EXPECT_TRUE(fv::BuildScriptedTrack({}, 10.0).empty());
  EXPECT_TRUE(fv::BuildScriptedTrack({GeoPoint{1.0, 2.0}}, 10.0).empty());
  EXPECT_TRUE(fv::BuildScriptedTrack(EastLeg(), 0.0).empty());

  ScriptedTrackOptions bad;
  bad.sample_interval_s = 0.0;
  EXPECT_TRUE(fv::BuildScriptedTrack(EastLeg(), 10.0, bad).empty());

  // A path of two identical points has no length and so no track.
  EXPECT_TRUE(fv::BuildScriptedTrack({GeoPoint{1.0, 2.0}, GeoPoint{1.0, 2.0}}, 10.0).empty());
}

// The two halves of MM1 meeting: a built track drives the resolver's REPORTED
// branch, and the same track with the heading left out drives its DERIVED
// one — to the same answer, which is what says the derivation is right.
TEST(BuildScriptedTrack, DrivesTheResolverByEitherBranchToTheSameHeading) {
  ScriptedTrackOptions no_heading;
  no_heading.set_true_heading = false;

  const auto reported = fv::BuildScriptedTrack(EastLeg(), 10.0);
  const auto derived = fv::BuildScriptedTrack(EastLeg(), 10.0, no_heading);
  ASSERT_EQ(reported.size(), derived.size());
  ASSERT_GE(reported.size(), 3u);

  fv::HeadingResolver a;
  fv::HeadingResolver b;
  for (std::size_t i = 0; i < reported.size(); ++i) {
    const fv::ResolvedHeading ra = a.Update(reported[i].fix);
    const fv::ResolvedHeading rb = b.Update(derived[i].fix);
    if (i == 0) continue;  // nothing to derive from yet
    EXPECT_TRUE(ra.reported);
    EXPECT_FALSE(rb.reported);
    EXPECT_NEAR(ra.degrees, rb.degrees, 0.05) << "sample " << i;
    EXPECT_NEAR(90.0, rb.degrees, 0.05);
  }
}

// ---------------------------------------------------------------------------
// BuildScriptedTrackFromFixes (MM6): a RECORDED track's own clock is the
// schedule. ReadGpxFile and ReadNmeaLog both arrive here.
// ---------------------------------------------------------------------------

fv::PositionFix StampedFix(double lat, double time_s) {
  fv::PositionFix fix;
  fix.SetPosition(lat, -80.0);
  fix.time_s = time_s;
  fix.has_time = true;
  return fix;
}

TEST(BuildScriptedTrackFromFixes, SchedulesFromTheFixesOwnTimestamps) {
  const std::vector<fv::PositionFix> fixes = {
      StampedFix(32.60, 1000.0), StampedFix(32.61, 1002.5), StampedFix(32.62, 1010.0)};
  const std::vector<fv::ScriptedFix> track = fv::BuildScriptedTrackFromFixes(fixes);
  ASSERT_EQ(track.size(), 3u);
  EXPECT_NEAR(track[0].t_s, 0.0, 1e-9);
  EXPECT_NEAR(track[1].t_s, 2.5, 1e-9);
  EXPECT_NEAR(track[2].t_s, 10.0, 1e-9);
  // The fixes themselves are passed through untouched.
  EXPECT_NEAR(track[2].fix.time_s, 1010.0, 1e-9);
}

TEST(BuildScriptedTrackFromFixes, CapsAGapAndDropsARepeatedStamp) {
  std::vector<fv::PositionFix> fixes = {StampedFix(32.60, 1000.0), StampedFix(32.61, 1000.0),
                                        StampedFix(32.62, 4000.0)};
  fv::FixScriptOptions options;
  options.max_gap_s = 5.0;
  const std::vector<fv::ScriptedFix> track = fv::BuildScriptedTrackFromFixes(fixes, options);
  ASSERT_EQ(track.size(), 2u);  // the repeated stamp went
  EXPECT_NEAR(track[1].t_s, 5.0, 1e-9);  // the 50-minute gap is not replayed
}

// A fix with no timestamp inside an otherwise stamped recording takes ONE
// interval and is CHARGED AGAINST the recording's clock. Without that, the
// next stamped fix adds its full delta on top and everything after the
// dropout drifts one interval later.
TEST(BuildScriptedTrackFromFixes, AnUnstampedFixDoesNotShiftTheRestOfTheTrack) {
  fv::PositionFix unstamped;
  unstamped.SetPosition(32.605, -80.0);
  const std::vector<fv::PositionFix> fixes = {StampedFix(32.60, 1000.0), unstamped,
                                              StampedFix(32.62, 1002.0),
                                              StampedFix(32.63, 1003.0)};
  const std::vector<fv::ScriptedFix> track = fv::BuildScriptedTrackFromFixes(fixes);
  ASSERT_EQ(track.size(), 4u);
  EXPECT_NEAR(track[0].t_s, 0.0, 1e-9);
  EXPECT_NEAR(track[1].t_s, 1.0, 1e-9);  // the fallback interval
  EXPECT_NEAR(track[2].t_s, 2.0, 1e-9);  // and NOT 3.0
  EXPECT_NEAR(track[3].t_s, 3.0, 1e-9);
}

TEST(BuildScriptedTrackFromFixes, WithNoClockAtAllItFallsBackToAConstantInterval) {
  std::vector<fv::PositionFix> fixes(4);
  for (fv::PositionFix& fix : fixes) fix.SetPosition(32.60, -80.0);
  fv::FixScriptOptions options;
  options.fallback_interval_s = 0.5;
  const std::vector<fv::ScriptedFix> track = fv::BuildScriptedTrackFromFixes(fixes, options);
  ASSERT_EQ(track.size(), 4u);
  EXPECT_NEAR(track.back().t_s, 1.5, 1e-9);
}

TEST(BuildScriptedTrackFromFixes, FewerThanTwoFixesIsNoTrack) {
  EXPECT_TRUE(fv::BuildScriptedTrackFromFixes({}).empty());
  EXPECT_TRUE(fv::BuildScriptedTrackFromFixes({StampedFix(32.60, 1000.0)}).empty());
}

}  // namespace
