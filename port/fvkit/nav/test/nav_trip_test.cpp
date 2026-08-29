// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fvkit/nav/trip.h (Pippin P8).
//
// Synthetic fixes pin every rule the header states, and then the real ride —
// testdata/kiawah_cycle.gpx, 1705 points at 1 Hz over 1704 s — pins the three
// numbers the plan named: elapsed, total distance, and a distance-to-go that
// reaches zero at the end of the route it was ridden along.

#include "fvkit/nav/trip.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fvkit/nav/gpx.h"

namespace fv {
namespace {

namespace fs = std::filesystem;

constexpr double kPi = 3.14159265358979323846;
// The same metre the computer uses, so a test that says "10 m apart" means it.
constexpr double kMetersPerDegLat = 6371008.8 * kPi / 180.0;

std::string TestDataDir() {
  const char* dir = std::getenv("FVW_TESTDATA_DIR");
  return (dir != nullptr && dir[0] != '\0') ? std::string(dir) : std::string("testdata");
}

std::string KiawahGpxPath() { return TestDataDir() + "/kiawah_cycle.gpx"; }

// A fix `north_m` north of 32.6N 80.0W, stamped at `time_s`.
PositionFix FixAt(double north_m, double time_s) {
  PositionFix fix;
  fix.SetPosition(32.6 + north_m / kMetersPerDegLat, -80.0);
  fix.time_s = time_s;
  fix.has_time = true;
  return fix;
}

GeoPoint PointAt(double north_m) {
  return GeoPoint{32.6 + north_m / kMetersPerDegLat, -80.0};
}

// ---------------------------------------------------------------------------
// Before anything has happened
// ---------------------------------------------------------------------------

TEST(TripTest, NotStartedPublishesNothing) {
  TripComputer trip;
  const TripStats stats = trip.Stats(1000.0);
  EXPECT_FALSE(stats.has_elapsed);
  EXPECT_FALSE(stats.has_odometer);
  EXPECT_FALSE(stats.has_speed);
  EXPECT_FALSE(stats.has_remaining);
  EXPECT_FALSE(stats.has_eta);
  EXPECT_FALSE(trip.running());
  EXPECT_FALSE(trip.started());
}

// A fix that arrives before the rider presses start belongs to no trip.
TEST(TripTest, FixesBeforeStartAreIgnored) {
  TripComputer trip;
  trip.OnFix(FixAt(0.0, 100.0));
  trip.OnFix(FixAt(500.0, 200.0));
  trip.Start(1000.0);
  const TripStats stats = trip.Stats(1000.0);
  EXPECT_TRUE(stats.has_odometer);
  EXPECT_DOUBLE_EQ(stats.odometer_m, 0.0);
}

// ---------------------------------------------------------------------------
// Elapsed — wall clock, not fix time
// ---------------------------------------------------------------------------

// THE RULE THE HEADER MAKES A POINT OF: a rider who presses GPS and waits
// twenty seconds for a lock has been riding for twenty seconds. The first
// fix's own timestamp — here a thousand seconds adrift, as a replayed log's
// would be — must not rewrite that.
TEST(TripTest, ElapsedRunsFromStartAndNotFromTheFirstFix) {
  TripComputer trip;
  trip.Start(1000.0);
  trip.OnFix(FixAt(0.0, 999999.0));
  EXPECT_NEAR(trip.Stats(1020.0).elapsed_s, 20.0, 1e-9);
}

TEST(TripTest, StopFreezesElapsedAndStatsStayReadable) {
  TripComputer trip;
  trip.Start(1000.0);
  trip.OnFix(FixAt(0.0, 1000.0));
  trip.OnFix(FixAt(100.0, 1010.0));
  trip.Stop(1010.0);

  const TripStats stats = trip.Stats(9999.0);
  EXPECT_NEAR(stats.elapsed_s, 10.0, 1e-9);
  EXPECT_TRUE(stats.has_odometer);
  EXPECT_NEAR(stats.odometer_m, 100.0, 0.05);
  EXPECT_FALSE(trip.running());
  EXPECT_TRUE(trip.started());
}

// Fixes after Stop do not extend the ride.
TEST(TripTest, FixesAfterStopAreIgnored) {
  TripComputer trip;
  trip.Start(1000.0);
  trip.OnFix(FixAt(0.0, 1000.0));
  trip.Stop(1001.0);
  trip.OnFix(FixAt(1000.0, 1002.0));
  EXPECT_NEAR(trip.Stats(2000.0).odometer_m, 0.0, 1e-9);
}

TEST(TripTest, StartRestartsARunningTrip) {
  TripComputer trip;
  trip.Start(1000.0);
  trip.OnFix(FixAt(0.0, 1000.0));
  trip.OnFix(FixAt(100.0, 1010.0));
  trip.Start(2000.0);
  const TripStats stats = trip.Stats(2005.0);
  EXPECT_NEAR(stats.elapsed_s, 5.0, 1e-9);
  EXPECT_DOUBLE_EQ(stats.odometer_m, 0.0);
}

// A clock that jumps backwards (a system time correction mid-ride) must not
// shorten a ride already ridden.
TEST(TripTest, ABackwardClockDoesNotShortenTheRide) {
  TripComputer trip;
  trip.Start(1000.0);
  EXPECT_NEAR(trip.Stats(900.0).elapsed_s, 0.0, 1e-9);
  trip.Stop(900.0);
  EXPECT_NEAR(trip.Stats(5000.0).elapsed_s, 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// The odometer and the anchor
// ---------------------------------------------------------------------------

TEST(TripTest, OdometerSumsAStraightLine) {
  TripComputer trip;
  trip.Start(0.0);
  for (int i = 0; i <= 10; ++i) trip.OnFix(FixAt(i * 10.0, i));
  EXPECT_NEAR(trip.Stats(10.0).odometer_m, 100.0, 0.05);
}

// A parked bike. Every step is 1.6 m — under the 2 m floor and therefore
// never counted — while a naive fix-to-fix sum would report 160 m of riding
// from a bike locked to a rack.
TEST(TripTest, JitterAtAStandstillDoesNotCreepTheOdometer) {
  TripComputer trip;
  trip.Start(0.0);
  for (int i = 0; i <= 100; ++i) {
    trip.OnFix(FixAt((i % 2 == 0) ? 0.8 : -0.8, i));
  }
  EXPECT_NEAR(trip.Stats(100.0).odometer_m, 0.0, 1e-9);
}

// THE OTHER END OF THE RANGE, and the reason the floor is measured from an
// anchor rather than from the previous fix: a walker at 1.4 m/s never moves
// 2 m between two 1 Hz fixes, and a per-fix floor would record the whole walk
// as a standstill. Against a stationary anchor the steps accumulate and are
// counted in pairs, and the total comes out exact.
TEST(TripTest, AWalkerBelowTheFloorStillAccumulatesDistance) {
  TripComputer trip;
  trip.Start(0.0);
  for (int i = 0; i <= 100; ++i) trip.OnFix(FixAt(i * 1.4, i));
  EXPECT_NEAR(trip.Stats(100.0).odometer_m, 140.0, 0.05);
}

TEST(TripTest, FixesWithNoPositionAreIgnored) {
  TripComputer trip;
  trip.Start(0.0);
  trip.OnFix(FixAt(0.0, 0.0));
  PositionFix empty;
  empty.time_s = 1.0;
  empty.has_time = true;
  trip.OnFix(empty);
  trip.OnFix(FixAt(100.0, 2.0));
  EXPECT_NEAR(trip.Stats(2.0).odometer_m, 100.0, 0.05);
}

// ---------------------------------------------------------------------------
// Speed
// ---------------------------------------------------------------------------

TEST(TripTest, ReportedSpeedBeatsDerived) {
  TripComputer trip;
  trip.Start(0.0);
  trip.OnFix(FixAt(0.0, 0.0));
  PositionFix fix = FixAt(100.0, 1.0);  // 100 m/s if derived
  fix.speed_mps = 4.0;
  fix.has_speed = true;
  trip.OnFix(fix);
  const TripStats stats = trip.Stats(1.0);
  ASSERT_TRUE(stats.has_speed);
  EXPECT_NEAR(stats.speed_mps, 4.0, 1e-9);
}

TEST(TripTest, SpeedIsDerivedWhenTheFixReportsNone) {
  TripComputer trip;
  trip.Start(0.0);
  trip.OnFix(FixAt(0.0, 0.0));
  trip.OnFix(FixAt(20.0, 2.0));
  const TripStats stats = trip.Stats(2.0);
  ASSERT_TRUE(stats.has_speed);
  EXPECT_NEAR(stats.speed_mps, 10.0, 0.01);
}

// The anchor's other job. Standing still, the numerator stays bounded while
// dt keeps growing, so the derived speed falls toward zero instead of
// reporting the jitter as movement.
TEST(TripTest, DerivedSpeedFallsTowardZeroAtAStandstill) {
  TripComputer trip;
  trip.Start(0.0);
  for (int i = 0; i <= 60; ++i) {
    trip.OnFix(FixAt((i % 2 == 0) ? 0.8 : -0.8, i));
  }
  const TripStats stats = trip.Stats(60.0);
  ASSERT_TRUE(stats.has_speed);
  EXPECT_LT(stats.speed_mps, 0.05);
}

TEST(TripTest, NoSpeedBeforeTheSecondFix) {
  TripComputer trip;
  trip.Start(0.0);
  trip.OnFix(FixAt(0.0, 0.0));
  EXPECT_FALSE(trip.Stats(0.0).has_speed);
}

// ---------------------------------------------------------------------------
// Distance to go
// ---------------------------------------------------------------------------

TEST(TripTest, NoRouteMeansNoRemaining) {
  TripComputer trip;
  trip.Start(0.0);
  trip.OnFix(FixAt(0.0, 0.0));
  EXPECT_FALSE(trip.Stats(0.0).has_remaining);
  EXPECT_FALSE(trip.has_route());
}

TEST(TripTest, ASingleRoutePointIsNoRoute) {
  TripComputer trip;
  trip.SetRoute({PointAt(0.0)});
  EXPECT_FALSE(trip.has_route());
}

TEST(TripTest, RemainingCountsDownAlongTheRoute) {
  TripComputer trip;
  trip.SetRoute({PointAt(0.0), PointAt(500.0), PointAt(1000.0)});
  trip.Start(0.0);

  trip.OnFix(FixAt(0.0, 0.0));
  EXPECT_NEAR(trip.Stats(0.0).remaining_m, 1000.0, 0.5);

  trip.OnFix(FixAt(250.0, 1.0));
  EXPECT_NEAR(trip.Stats(1.0).remaining_m, 750.0, 0.5);

  trip.OnFix(FixAt(1000.0, 2.0));
  EXPECT_NEAR(trip.Stats(2.0).remaining_m, 0.0, 0.5);
}

// Past the end of the line the projection clamps, so the answer is zero and
// not a negative distance.
TEST(TripTest, PastTheEndRemainingIsZeroAndNotNegative) {
  TripComputer trip;
  trip.SetRoute({PointAt(0.0), PointAt(1000.0)});
  trip.Start(0.0);
  trip.OnFix(FixAt(1500.0, 0.0));
  const TripStats stats = trip.Stats(0.0);
  ASSERT_TRUE(stats.has_remaining);
  EXPECT_NEAR(stats.remaining_m, 0.0, 1e-6);
}

// A route arriving mid-ride is a rider planning one from the saddle; the
// distance behind them is still theirs.
TEST(TripTest, ARouteSetMidRideKeepsTheOdometer) {
  TripComputer trip;
  trip.Start(0.0);
  trip.OnFix(FixAt(0.0, 0.0));
  trip.OnFix(FixAt(200.0, 10.0));
  trip.SetRoute({PointAt(200.0), PointAt(1200.0)});
  const TripStats stats = trip.Stats(10.0);
  EXPECT_NEAR(stats.odometer_m, 200.0, 0.05);
  ASSERT_TRUE(stats.has_remaining);
  EXPECT_NEAR(stats.remaining_m, 1000.0, 0.5);
}

TEST(TripTest, ClearRouteRemovesRemaining) {
  TripComputer trip;
  trip.SetRoute({PointAt(0.0), PointAt(1000.0)});
  trip.Start(0.0);
  trip.OnFix(FixAt(0.0, 0.0));
  ASSERT_TRUE(trip.Stats(0.0).has_remaining);
  trip.ClearRoute();
  EXPECT_FALSE(trip.Stats(0.0).has_remaining);
}

// ---------------------------------------------------------------------------
// ETA
// ---------------------------------------------------------------------------

TEST(TripTest, EtaIsNowPlusRemainingOverSmoothedSpeed) {
  TripComputer trip;
  trip.SetRoute({PointAt(0.0), PointAt(1000.0)});
  trip.Start(0.0);
  PositionFix fix = FixAt(0.0, 0.0);
  fix.speed_mps = 5.0;
  fix.has_speed = true;
  trip.OnFix(fix);

  const TripStats stats = trip.Stats(100.0);
  ASSERT_TRUE(stats.has_eta);
  EXPECT_NEAR(stats.average_speed_mps, 5.0, 1e-9);
  EXPECT_NEAR(stats.eta_epoch_s, 100.0 + 200.0, 0.5);
}

// Below half walking pace an arrival time is a fiction, and the bar shows an
// en-dash instead.
TEST(TripTest, NoEtaBelowWalkingPace) {
  TripComputer trip;
  trip.SetRoute({PointAt(0.0), PointAt(1000.0)});
  trip.Start(0.0);
  PositionFix fix = FixAt(0.0, 0.0);
  fix.speed_mps = 0.2;
  fix.has_speed = true;
  trip.OnFix(fix);
  const TripStats stats = trip.Stats(0.0);
  EXPECT_TRUE(stats.has_remaining);
  EXPECT_FALSE(stats.has_eta);
}

TEST(TripTest, NoEtaWithoutARoute) {
  TripComputer trip;
  trip.Start(0.0);
  PositionFix fix = FixAt(0.0, 0.0);
  fix.speed_mps = 5.0;
  fix.has_speed = true;
  trip.OnFix(fix);
  EXPECT_FALSE(trip.Stats(0.0).has_eta);
}

// The smoothing is weighted by TIME, so a step change is followed with the
// stated time constant rather than with a per-fix weight. One tau of 5 m/s
// after a start at 0 leaves (1 - 1/e) of the way there.
TEST(TripTest, TheAverageFollowsWithTheStatedTimeConstant) {
  TripSettings settings;
  settings.speed_ema_tau_s = 30.0;
  TripComputer trip(settings);
  trip.Start(0.0);

  PositionFix first = FixAt(0.0, 0.0);
  first.speed_mps = 0.0;
  first.has_speed = true;
  trip.OnFix(first);
  ASSERT_NEAR(trip.Stats(0.0).average_speed_mps, 0.0, 1e-9);

  PositionFix later = FixAt(0.0, 30.0);
  later.speed_mps = 5.0;
  later.has_speed = true;
  trip.OnFix(later);
  EXPECT_NEAR(trip.Stats(30.0).average_speed_mps, 5.0 * (1.0 - std::exp(-1.0)),
              1e-6);
}

// ---------------------------------------------------------------------------
// The real ride
// ---------------------------------------------------------------------------

// Exact numbers on ONE NAMED file, per the ledger's rule: if the fixture is
// ever re-exported these move together and the failure says so plainly.
TEST(TripKiawahTest, ComputesTheRecordedRide) {
  if (!fs::exists(KiawahGpxPath())) GTEST_SKIP() << "kiawah_cycle.gpx not present";
  GpxDocument document;
  ASSERT_TRUE(ReadGpxFile(KiawahGpxPath(), &document).ok());
  const std::vector<PositionFix> fixes = FlattenGpxFixes(document);
  ASSERT_EQ(fixes.size(), 1705u);

  // The ride's own track, used as the route: a rider following exactly the
  // line they rode must arrive with nothing left to go.
  std::vector<GeoPoint> route;
  route.reserve(fixes.size());
  for (const PositionFix& fix : fixes) route.push_back(fix.position());

  TripComputer trip;
  trip.SetRoute(route);
  const double t0 = fixes.front().time_s;
  trip.Start(t0);
  for (const PositionFix& fix : fixes) trip.OnFix(fix);

  const double t_end = fixes.back().time_s;
  const TripStats stats = trip.Stats(t_end);

  EXPECT_NEAR(stats.elapsed_s, 1704.0, 1e-6);

  // A 28-minute bike ride on Kiawah, pinned to the metre. 6097.99 m against
  // the 6100.84 m a naive fix-to-fix sum gives: the anchor floor costs 2.85 m
  // over 6.1 km, which is the whole argument for it — it deletes the
  // standstill jitter and almost none of the ride. Cross-checked against an
  // independent implementation of the same rule outside this codebase.
  ASSERT_TRUE(stats.has_odometer);
  EXPECT_NEAR(stats.odometer_m, 6097.99, 0.5);

  // At the end of the line there is nothing left to go.
  ASSERT_TRUE(stats.has_remaining);
  EXPECT_NEAR(stats.remaining_m, 0.0, 1.0);

  // AND THE RIDE ENDS STOPPED, which is worth a test of its own rather than a
  // loose bound: the last 60 s of this file cover 6.0 m — the rider is off the
  // bike — so the 30 s average has decayed to 0.34 m/s and the ETA is
  // correctly WITHDRAWN. A bar showing "arriving in 4 minutes" while the rider
  // stands still is the exact failure min_eta_speed_mps exists to prevent.
  ASSERT_TRUE(stats.has_average_speed);
  EXPECT_NEAR(stats.average_speed_mps, 0.344, 0.01);
  EXPECT_FALSE(stats.has_eta);
}

// Halfway through the ride, the odometer and the distance-to-go should
// account for the whole route between them — the one relation that catches a
// projection landing on the wrong limb of a loop.
TEST(TripKiawahTest, OdometerAndRemainingAccountForTheWholeRoute) {
  if (!fs::exists(KiawahGpxPath())) GTEST_SKIP() << "kiawah_cycle.gpx not present";
  GpxDocument document;
  ASSERT_TRUE(ReadGpxFile(KiawahGpxPath(), &document).ok());
  const std::vector<PositionFix> fixes = FlattenGpxFixes(document);

  std::vector<GeoPoint> route;
  route.reserve(fixes.size());
  for (const PositionFix& fix : fixes) route.push_back(fix.position());

  TripComputer trip;
  trip.SetRoute(route);
  trip.Start(fixes.front().time_s);

  const std::size_t half = fixes.size() / 2;
  for (std::size_t i = 0; i <= half; ++i) trip.OnFix(fixes[i]);

  TripComputer whole;
  whole.SetRoute(route);
  whole.Start(fixes.front().time_s);
  for (const PositionFix& fix : fixes) whole.OnFix(fix);
  const double total_m = whole.Stats(fixes.back().time_s).odometer_m;

  const TripStats stats = trip.Stats(fixes[half].time_s);
  ASSERT_TRUE(stats.has_remaining);
  EXPECT_NEAR(stats.odometer_m + stats.remaining_m, total_m, 0.02 * total_m);
}

}  // namespace
}  // namespace fv
