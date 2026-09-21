// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fvkit/nav/guidance.h (guidance plan GD2).
//
// Synthetic fixes down a synthetic route, because every rule in the header is
// about WHEN something is said and the fixture that makes that legible is one
// where the rider's speed and position are chosen rather than measured. The
// real ride replayed against a real planned route is RouteKit's test, on the
// other side of the seam GD1 established.

#include "fvkit/nav/guidance.h"

#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace fv {
namespace nav {
namespace {

constexpr double kOriginLat = 32.6070;
constexpr double kOriginLon = -80.0850;
constexpr double kMetersPerDegLat = 111320.0;

GeoPoint At(double east_m, double north_m) {
  GeoPoint p;
  p.lat = kOriginLat + north_m / kMetersPerDegLat;
  p.lon = kOriginLon + east_m / (kMetersPerDegLat * std::cos(kOriginLat * M_PI / 180.0));
  return p;
}

PositionFix FixAt(const GeoPoint& p, double time_s, double speed_mps) {
  PositionFix fix;
  fix.SetPosition(p.lat, p.lon);
  fix.time_s = time_s;
  fix.has_time = true;
  fix.speed_mps = speed_mps;
  fix.has_speed = true;
  return fix;
}

Maneuver Turn(double distance_m, ManeuverType type, std::string road) {
  Maneuver m;
  m.distance_m = distance_m;
  m.type = type;
  m.road = std::move(road);
  return m;
}

// 1000 m due east, one right turn at 500 m, then 500 m due south.
// Vertices every 100 m so a projection has segments to choose between.
std::vector<GeoPoint> LGeometry() {
  std::vector<GeoPoint> g;
  for (int e = 0; e <= 500; e += 100) g.push_back(At(e, 0));
  for (int s = 100; s <= 500; s += 100) g.push_back(At(500, -s));
  return g;
}

std::vector<Maneuver> LManeuvers() {
  return {Turn(0.0, ManeuverType::kDepart, "First St"),
          Turn(500.0, ManeuverType::kRight, "Second St"),
          Turn(1000.0, ManeuverType::kArrive, "Second St")};
}

Guidance LRoute() {
  Guidance g;
  g.SetRoute(LGeometry(), LManeuvers());
  return g;
}

// Every event of one type across a run.
std::vector<GuidanceEvent> Only(const std::vector<GuidanceEvent>& all,
                                GuidanceEventType type) {
  std::vector<GuidanceEvent> out;
  for (const GuidanceEvent& e : all)
    if (e.type == type) out.push_back(e);
  return out;
}

// Rides east from `from_m` to `to_m` along the L's first limb at a steady
// speed, one fix a second, collecting everything said.
std::vector<GuidanceEvent> RideEast(Guidance* g, double from_m, double to_m,
                                    double speed_mps, double t0 = 1000.0) {
  std::vector<GuidanceEvent> all;
  double t = t0;
  for (double d = from_m; d <= to_m + 1e-9; d += speed_mps) {
    const std::vector<GuidanceEvent> e = g->OnFix(FixAt(At(d, 0), t, speed_mps));
    all.insert(all.end(), e.begin(), e.end());
    t += 1.0;
  }
  return all;
}

}  // namespace

TEST(Guidance, NoRouteIsNoGuidance) {
  Guidance g;
  EXPECT_FALSE(g.has_route());
  EXPECT_TRUE(g.OnFix(FixAt(At(0, 0), 0.0, 5.0)).empty());
  EXPECT_FALSE(g.state().valid);

  // A route with no maneuvers is not a route to guide along either.
  g.SetRoute(LGeometry(), {});
  EXPECT_FALSE(g.has_route());
}

TEST(Guidance, TheFirstFixEstablishesWhereTheRiderIsAndAnnouncesNothingBehind) {
  Guidance g = LRoute();
  // Dropped in 400 m along, past the depart: no "you have passed" backlog.
  const std::vector<GuidanceEvent> e = g.OnFix(FixAt(At(400, 0), 1000.0, 0.0));
  EXPECT_TRUE(Only(e, GuidanceEventType::kPassed).empty());
  EXPECT_TRUE(g.state().valid);
  EXPECT_TRUE(g.state().on_route);
  EXPECT_EQ(g.state().next_index, 1u);
  EXPECT_NEAR(g.state().along_m, 400.0, 1.0);
  EXPECT_NEAR(g.state().distance_to_next_m, 100.0, 1.0);
}

// Rule 1: the ring is a time, clamped at both ends.
TEST(Guidance, RingsAreTimesClampedToDistances) {
  // 7 m/s: heads-up at 30 s is 210 m, inside the clamps; act-now at 8 s is
  // 56 m, likewise.
  Guidance fast = LRoute();
  const std::vector<GuidanceEvent> quick =
      Only(RideEast(&fast, 200.0, 499.0, 7.0), GuidanceEventType::kApproach);
  ASSERT_GE(quick.size(), 2u);
  EXPECT_EQ(quick[0].ring, GuidanceRing::kHeadsUp);
  EXPECT_NEAR(quick[0].distance_m, 210.0, 8.0);
  EXPECT_EQ(quick[1].ring, GuidanceRing::kActNow);
  EXPECT_NEAR(quick[1].distance_m, 56.0, 8.0);

  // 1.4 m/s walking: 42 m and 11 m would be useless, so both clamp up to
  // their floors.
  Guidance slow = LRoute();
  const std::vector<GuidanceEvent> walked =
      Only(RideEast(&slow, 200.0, 499.0, 1.4), GuidanceEventType::kApproach);
  ASSERT_GE(walked.size(), 2u);
  EXPECT_EQ(walked[0].ring, GuidanceRing::kHeadsUp);
  EXPECT_NEAR(walked[0].distance_m, 100.0, 3.0);
  EXPECT_EQ(walked[1].ring, GuidanceRing::kActNow);
  EXPECT_NEAR(walked[1].distance_m, 20.0, 3.0);
}

TEST(Guidance, EachRingFiresOnceAndOnlyOnce) {
  Guidance g = LRoute();
  const std::vector<GuidanceEvent> all =
      Only(RideEast(&g, 100.0, 499.0, 7.0), GuidanceEventType::kApproach);
  int heads_up = 0, act_now = 0, at = 0;
  for (const GuidanceEvent& e : all) {
    EXPECT_EQ(e.maneuver_index, 1u);
    if (e.ring == GuidanceRing::kHeadsUp) ++heads_up;
    if (e.ring == GuidanceRing::kActNow) ++act_now;
    if (e.ring == GuidanceRing::kAt) ++at;
  }
  EXPECT_EQ(heads_up, 1);
  EXPECT_EQ(act_now, 1);
  EXPECT_EQ(at, 1);
}

// Rule 2: guidance that starts inside two rings says the inner one, once.
TEST(Guidance, StartingMidApproachAnnouncesOnlyTheInnermostRing) {
  Guidance g = LRoute();
  // First fix 30 m from the corner at riding speed: inside heads-up (210 m)
  // and inside act-now (56 m), outside the junction itself.
  const std::vector<GuidanceEvent> e = g.OnFix(FixAt(At(470, 0), 1000.0, 7.0));
  const std::vector<GuidanceEvent> approach = Only(e, GuidanceEventType::kApproach);
  ASSERT_EQ(approach.size(), 1u);
  EXPECT_EQ(approach[0].ring, GuidanceRing::kActNow);

  // ...and the heads-up is spent, not queued behind it.
  const std::vector<GuidanceEvent> next =
      Only(g.OnFix(FixAt(At(477, 0), 1001.0, 7.0)), GuidanceEventType::kApproach);
  EXPECT_TRUE(next.empty());
}

// The junction ring is a time too, because a fixed one smaller than the step
// between two fixes is stepped over: at 8 m/s a 10 m ring catches a 1 Hz fix
// only sometimes, which is an alert that fires only sometimes.
TEST(Guidance, TheJunctionRingIsNotSteppedOverAtSpeed) {
  for (double speed : {4.0, 7.0, 8.0, 11.0}) {
    Guidance g = LRoute();
    const std::vector<GuidanceEvent> all =
        Only(RideEast(&g, 300.0, 499.0, speed), GuidanceEventType::kApproach);
    int at = 0;
    for (const GuidanceEvent& e : all)
      if (e.ring == GuidanceRing::kAt) ++at;
    EXPECT_EQ(at, 1) << "at " << speed << " m/s";
  }
}

TEST(Guidance, PassingAManeuverIsSaidOnceAndMovesToTheNext) {
  Guidance g = LRoute();
  std::vector<GuidanceEvent> all = RideEast(&g, 400.0, 499.0, 7.0);
  // Round the corner and down the second limb: a rider who stops ON the
  // junction has not passed it.
  double t = 1100.0;
  for (double south = 0.0; south <= 100.0; south += 7.0) {
    const std::vector<GuidanceEvent> e =
        g.OnFix(FixAt(At(500, -south), t, 7.0));
    all.insert(all.end(), e.begin(), e.end());
    t += 1.0;
  }
  const std::vector<GuidanceEvent> passed = Only(all, GuidanceEventType::kPassed);
  ASSERT_EQ(passed.size(), 1u);
  EXPECT_EQ(passed[0].maneuver_index, 1u);
  EXPECT_EQ(passed[0].maneuver_type, ManeuverType::kRight);
  EXPECT_EQ(g.state().next_index, 2u);
  EXPECT_EQ(g.state().next_type, ManeuverType::kArrive);
}

// Rule 3, first half: one wandering fix does not silence the guidance.
TEST(Guidance, OneStrayFixIsNotOffRoute) {
  Guidance g = LRoute();
  g.OnFix(FixAt(At(100, 0), 1000.0, 7.0));
  // 40 m off the line, well past the 25 m tolerance, for one second only.
  const std::vector<GuidanceEvent> stray = g.OnFix(FixAt(At(107, 40), 1001.0, 7.0));
  EXPECT_TRUE(Only(stray, GuidanceEventType::kOffRoute).empty());
  EXPECT_TRUE(g.state().on_route);

  g.OnFix(FixAt(At(114, 0), 1002.0, 7.0));
  EXPECT_TRUE(g.state().on_route);
}

TEST(Guidance, LeavingTheRouteIsSustainedAndSaidOnce) {
  Guidance g = LRoute();
  g.OnFix(FixAt(At(100, 0), 1000.0, 7.0));

  std::vector<GuidanceEvent> all;
  for (int i = 1; i <= 10; ++i) {
    // Riding away from the line, 40 m off and staying off.
    const std::vector<GuidanceEvent> e =
        g.OnFix(FixAt(At(100.0 + 7.0 * i, 40.0), 1000.0 + i, 7.0));
    all.insert(all.end(), e.begin(), e.end());
  }
  const std::vector<GuidanceEvent> off = Only(all, GuidanceEventType::kOffRoute);
  ASSERT_EQ(off.size(), 1u) << "off route is said once, not every second";
  EXPECT_FALSE(g.state().on_route);
  EXPECT_FALSE(g.state().has_next) << "silent, rather than counting down a corner";

  // Nothing is announced while it is silent.
  const std::vector<GuidanceEvent> approach = Only(all, GuidanceEventType::kApproach);
  EXPECT_TRUE(approach.empty());
}

// Rule 3, second half: rejoining is immediate, and re-arms the corner that
// was never announced.
TEST(Guidance, RejoiningIsImmediateAndReArmsWhatWasMissed) {
  Guidance g = LRoute();
  g.OnFix(FixAt(At(100, 0), 1000.0, 7.0));
  for (int i = 1; i <= 10; ++i)
    g.OnFix(FixAt(At(100.0 + 7.0 * i, 60.0), 1000.0 + i, 7.0));
  ASSERT_FALSE(g.state().on_route);

  // Back on the line at 250 m, one fix.
  const std::vector<GuidanceEvent> back = g.OnFix(FixAt(At(250, 0), 1020.0, 7.0));
  ASSERT_EQ(Only(back, GuidanceEventType::kRejoined).size(), 1u);
  EXPECT_TRUE(g.state().on_route);

  // The corner at 500 m is 250 m ahead, so its heads-up is still to come and
  // must not have been marked spent while the guidance was silent.
  const std::vector<GuidanceEvent> onward =
      Only(RideEast(&g, 257.0, 499.0, 7.0, 1021.0), GuidanceEventType::kApproach);
  ASSERT_GE(onward.size(), 1u);
  EXPECT_EQ(onward[0].ring, GuidanceRing::kHeadsUp);
}

// The staggered junction GD1 found on the real island: the corner after this
// one falls inside the same act-now ring, so "right" alone is wrong.
TEST(Guidance, AStaggeredPairIsCarriedOnTheEvent) {
  std::vector<GeoPoint> g;
  for (int e = 0; e <= 500; e += 100) g.push_back(At(e, 0));
  g.push_back(At(500, -3));       // 3 m stagger
  for (int e = 600; e <= 800; e += 100) g.push_back(At(e, -3));

  Guidance guide;
  guide.SetRoute(g, {Turn(0.0, ManeuverType::kDepart, "First St"),
                     Turn(500.0, ManeuverType::kRight, "Jog Ln"),
                     Turn(503.0, ManeuverType::kLeft, "Second St"),
                     Turn(703.0, ManeuverType::kArrive, "Second St")});

  const std::vector<GuidanceEvent> all =
      Only(RideEast(&guide, 300.0, 480.0, 7.0), GuidanceEventType::kApproach);
  ASSERT_GE(all.size(), 2u);
  const GuidanceEvent& act_now = all.back();
  EXPECT_EQ(act_now.ring, GuidanceRing::kActNow);
  EXPECT_EQ(act_now.maneuver_type, ManeuverType::kRight);
  ASSERT_TRUE(act_now.has_then) << "a 3 m stagger inside a 56 m ring";
  EXPECT_EQ(act_now.then_index, 2u);
  EXPECT_EQ(act_now.then_type, ManeuverType::kLeft);

  // The pairing is a property of the two corners, not of the ring that
  // happens to be firing, so the heads-up carries it as well: "in 200 m,
  // right then immediately left" is the useful form of that sentence.
  EXPECT_EQ(all.front().ring, GuidanceRing::kHeadsUp);
  EXPECT_TRUE(all.front().has_then);
  EXPECT_EQ(all.front().then_type, ManeuverType::kLeft);
}

TEST(Guidance, ArrivalIsSaidOnceAndEndsTheGuidance) {
  Guidance g = LRoute();
  g.OnFix(FixAt(At(500, -400), 1000.0, 7.0));

  std::vector<GuidanceEvent> all;
  for (int i = 1; i <= 20; ++i) {
    const double south = std::min(400.0 + 7.0 * i, 500.0);
    const std::vector<GuidanceEvent> e =
        g.OnFix(FixAt(At(500, -south), 1000.0 + i, 7.0));
    all.insert(all.end(), e.begin(), e.end());
  }
  ASSERT_EQ(Only(all, GuidanceEventType::kArrived).size(), 1u);
  EXPECT_FALSE(g.state().has_next);
}

// Rule 4: the projection is windowed, and this is the case that needs it.
TEST(Guidance, AnOutAndBackDoesNotAnnounceTheOtherLimb) {
  // East 500 m and straight back, the two limbs 4 m apart — closer than a
  // receiver's own error, which is what makes nearest-segment pick wrongly.
  std::vector<GeoPoint> g;
  for (int e = 0; e <= 500; e += 50) g.push_back(At(e, 0));
  for (int e = 500; e >= 0; e -= 50) g.push_back(At(e, -4));

  Guidance guide;
  guide.SetRoute(g, {Turn(0.0, ManeuverType::kDepart, "First St"),
                     Turn(500.0, ManeuverType::kUTurn, "First St"),
                     Turn(1000.0, ManeuverType::kArrive, "First St")});

  // Outbound. Along-route distance must keep climbing rather than flipping to
  // the return limb, which would put the u-turn behind the rider.
  double previous = -1.0;
  for (double d = 0.0; d <= 400.0; d += 7.0) {
    guide.OnFix(FixAt(At(d, -1.0), 1000.0 + d, 7.0));
    EXPECT_GT(guide.state().along_m, previous) << "at " << d << " m out";
    EXPECT_LT(guide.state().along_m, 500.0) << "jumped to the return limb";
    previous = guide.state().along_m;
  }
  EXPECT_EQ(guide.state().next_index, 1u);
  EXPECT_EQ(guide.state().next_type, ManeuverType::kUTurn);

  // ...and on the way back it is the return limb, not the outbound one.
  for (double d = 500.0; d >= 300.0; d -= 7.0)
    guide.OnFix(FixAt(At(d, -3.0), 2000.0 + (500.0 - d), 7.0));
  EXPECT_GT(guide.state().along_m, 500.0);
  EXPECT_EQ(guide.state().next_type, ManeuverType::kArrive);
}

TEST(Guidance, ResetPutsTheRiderBackAtTheStartWithEveryAlertUnspent) {
  Guidance g = LRoute();
  RideEast(&g, 100.0, 499.0, 7.0);
  ASSERT_TRUE(g.state().valid);

  g.Reset();
  EXPECT_FALSE(g.state().valid);
  EXPECT_TRUE(g.has_route());

  const std::vector<GuidanceEvent> again =
      Only(RideEast(&g, 100.0, 499.0, 7.0), GuidanceEventType::kApproach);
  int heads_up = 0;
  for (const GuidanceEvent& e : again)
    if (e.ring == GuidanceRing::kHeadsUp) ++heads_up;
  EXPECT_EQ(heads_up, 1);
}

TEST(Guidance, AFixWithNoPositionIsIgnored) {
  Guidance g = LRoute();
  g.OnFix(FixAt(At(100, 0), 1000.0, 7.0));
  const double along = g.state().along_m;

  PositionFix empty;
  empty.time_s = 1001.0;
  empty.has_time = true;
  EXPECT_TRUE(g.OnFix(empty).empty());
  EXPECT_DOUBLE_EQ(g.state().along_m, along);
}

}  // namespace nav
}  // namespace fv
