// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fvkit/nav/maneuver.h (guidance plan GD1).
//
// Junctions are built by hand in metres about a point on Kiawah, which is what
// makes the expected angle a number rather than a measurement: a leg due east
// followed by one due north is a 90 degree left, whatever the geodesy does
// with the fifth decimal place. The route the real router produces is
// RouteKit's test, on the other side of the D6 seam.

#include "fvkit/nav/maneuver.h"

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

// A point `east` and `north` metres from the origin. Good to well under a
// metre over the few hundred these tests use.
GeoPoint At(double east_m, double north_m) {
  GeoPoint p;
  p.lat = kOriginLat + north_m / kMetersPerDegLat;
  p.lon = kOriginLon + east_m / (kMetersPerDegLat * std::cos(kOriginLat * M_PI / 180.0));
  return p;
}

RouteShape::Leg Leg(uint32_t begin, std::string name) {
  RouteShape::Leg leg;
  leg.geometry_begin = begin;
  leg.name = std::move(name);
  leg.klass = "residential";
  return leg;
}

// East 200 m, then a corner at index 2, then 200 m in whichever direction the
// caller's second run goes.
RouteShape Corner(double turn_east, double turn_north) {
  RouteShape s;
  s.geometry = {At(0, 0), At(100, 0), At(200, 0),
                At(200 + turn_east * 0.5, turn_north * 0.5),
                At(200 + turn_east, turn_north)};
  s.legs = {Leg(0, "First St"), Leg(2, "Second St")};
  return s;
}

}  // namespace

TEST(Maneuver, NoLineNoInstructions) {
  EXPECT_TRUE(BuildManeuvers(RouteShape{}).empty());
  RouteShape one;
  one.geometry = {At(0, 0)};
  one.legs = {Leg(0, "First St")};
  EXPECT_TRUE(BuildManeuvers(one).empty());
}

TEST(Maneuver, DepartAndArriveAlwaysBracketTheList) {
  RouteShape s;
  s.geometry = {At(0, 0), At(100, 0), At(200, 0)};
  s.legs = {Leg(0, "First St")};

  const std::vector<Maneuver> m = BuildManeuvers(s);
  ASSERT_EQ(m.size(), 2u);
  EXPECT_EQ(m.front().type, ManeuverType::kDepart);
  EXPECT_EQ(m.front().road, "First St");
  EXPECT_DOUBLE_EQ(m.front().distance_m, 0.0);
  EXPECT_EQ(m.front().geometry_index, 0u);
  EXPECT_EQ(m.back().type, ManeuverType::kArrive);
  EXPECT_EQ(m.back().geometry_index, 2u);
  EXPECT_NEAR(m.back().distance_m, 200.0, 1.0);
}

// Rule 1: a leg boundary that is not a corner is not an instruction.
TEST(Maneuver, NameChangeOnAStraightRoadIsNotATurn) {
  RouteShape s;
  s.geometry = {At(0, 0), At(100, 0), At(200, 0), At(300, 0), At(400, 0)};
  s.legs = {Leg(0, "First St"), Leg(2, "First Ave")};

  const std::vector<Maneuver> m = BuildManeuvers(s);
  ASSERT_EQ(m.size(), 2u);
  EXPECT_EQ(m[0].type, ManeuverType::kDepart);
  EXPECT_EQ(m[1].type, ManeuverType::kArrive);
}

TEST(Maneuver, RightIsPositiveAndLeftIsNegative) {
  const std::vector<Maneuver> right = BuildManeuvers(Corner(0.0, -200.0));
  ASSERT_EQ(right.size(), 3u);
  EXPECT_EQ(right[1].type, ManeuverType::kRight);
  EXPECT_NEAR(right[1].turn_deg, 90.0, 1.0);
  EXPECT_EQ(right[1].road, "Second St");
  EXPECT_EQ(right[1].geometry_index, 2u);
  EXPECT_NEAR(right[1].distance_m, 200.0, 1.0);

  const std::vector<Maneuver> left = BuildManeuvers(Corner(0.0, 200.0));
  ASSERT_EQ(left.size(), 3u);
  EXPECT_EQ(left[1].type, ManeuverType::kLeft);
  EXPECT_NEAR(left[1].turn_deg, -90.0, 1.0);
}

TEST(Maneuver, TheBandsAreTheSettings) {
  // 30 degrees off straight: past straight_deg, short of slight_deg.
  const double r = 200.0;
  const std::vector<Maneuver> slight =
      BuildManeuvers(Corner(r * std::cos(30.0 * M_PI / 180.0),
                            -r * std::sin(30.0 * M_PI / 180.0)));
  ASSERT_EQ(slight.size(), 3u);
  EXPECT_EQ(slight[1].type, ManeuverType::kSlightRight);

  // 130 degrees: past sharp_deg, short of u_turn_deg.
  const std::vector<Maneuver> sharp =
      BuildManeuvers(Corner(-r * std::cos(50.0 * M_PI / 180.0),
                            -r * std::sin(50.0 * M_PI / 180.0)));
  ASSERT_EQ(sharp.size(), 3u);
  EXPECT_EQ(sharp[1].type, ManeuverType::kSharpRight);

  // Straight back the way it came.
  const std::vector<Maneuver> back = BuildManeuvers(Corner(-200.0, 0.0));
  ASSERT_EQ(back.size(), 3u);
  EXPECT_EQ(back[1].type, ManeuverType::kUTurn);
}

TEST(Maneuver, BelowTheStraightThresholdNothingIsReported) {
  const double r = 200.0;
  const double bend = 10.0 * M_PI / 180.0;
  RouteShape s = Corner(r * std::cos(bend), -r * std::sin(bend));
  ASSERT_EQ(BuildManeuvers(s).size(), 2u);

  // ...and the threshold is a setting, not a constant.
  ManeuverSettings tight;
  tight.straight_deg = 5.0;
  EXPECT_EQ(BuildManeuvers(s, tight).size(), 3u);
}

// Rule 2: the window stops at the neighbouring candidate, so two corners
// 20 m apart stay two corners of their own angle rather than one of the sum.
TEST(Maneuver, TwoCornersInsideOneWindowKeepTheirOwnAngles) {
  RouteShape s;
  s.geometry = {At(0, 0),     At(100, 0),   At(200, 0),
                At(200, -20), At(220, -20), At(320, -20)};
  s.legs = {Leg(0, "First St"), Leg(2, "Jog Ln"), Leg(3, "Second St")};

  const std::vector<Maneuver> m = BuildManeuvers(s);
  ASSERT_EQ(m.size(), 4u);
  EXPECT_EQ(m[1].type, ManeuverType::kRight);
  EXPECT_NEAR(m[1].turn_deg, 90.0, 2.0);
  EXPECT_EQ(m[1].road, "Jog Ln");
  EXPECT_EQ(m[2].type, ManeuverType::kLeft);
  EXPECT_NEAR(m[2].turn_deg, -90.0, 2.0);
  EXPECT_EQ(m[2].road, "Second St");
}

// A receiver-grade duplicate point at the junction carries no bearing; the
// walk keeps going rather than reporting a turn it cannot measure.
TEST(Maneuver, CoincidentPointsDoNotHideTheCorner) {
  RouteShape s;
  s.geometry = {At(0, 0),    At(100, 0),   At(200, 0),
                At(200, 0),  At(200, -100), At(200, -200)};
  s.legs = {Leg(0, "First St"), Leg(3, "Second St")};

  const std::vector<Maneuver> m = BuildManeuvers(s);
  ASSERT_EQ(m.size(), 3u);
  EXPECT_EQ(m[1].type, ManeuverType::kRight);
  EXPECT_NEAR(m[1].turn_deg, 90.0, 1.0);
  EXPECT_NEAR(m[1].distance_m, 200.0, 1.0);
}

// A corner within end_margin_m of either end is the snap stub, not a turn.
TEST(Maneuver, CornersOnTopOfEitherEndAreNotInstructions) {
  // East 5 m, left onto a long road, right for the last 5 m: both corners sit
  // inside the margin, at either end.
  RouteShape s;
  s.geometry = {At(0, 0), At(5, 0), At(5, 100), At(5, 195), At(10, 195)};
  s.legs = {Leg(0, "Stub"), Leg(1, "Long Rd"), Leg(3, "Second Rd")};

  EXPECT_EQ(BuildManeuvers(s).size(), 2u);

  ManeuverSettings keep_everything;
  keep_everything.end_margin_m = 0.0;
  EXPECT_EQ(BuildManeuvers(s, keep_everything).size(), 4u);
}

TEST(Maneuver, DistancesAreCumulativeAlongTheLine) {
  const std::vector<Maneuver> m = BuildManeuvers(Corner(0.0, -200.0));
  ASSERT_EQ(m.size(), 3u);
  EXPECT_LT(m[0].distance_m, m[1].distance_m);
  EXPECT_LT(m[1].distance_m, m[2].distance_m);
  EXPECT_NEAR(m[2].distance_m, 400.0, 2.0);
}

}  // namespace nav
}  // namespace fv
