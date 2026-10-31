// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fvkit/nav/course_deviation.h on synthetic legs off Charleston,
// laid out in local east/north metres.

#include "fvkit/nav/course_deviation.h"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

namespace fv {
namespace nav {
namespace {

constexpr double kOriginLat = 32.70;
constexpr double kOriginLon = -79.85;
constexpr double kMetersPerDegLat = 111132.0;

GeoPoint At(double east_m, double north_m) {
  GeoPoint p;
  p.lat = kOriginLat + north_m / kMetersPerDegLat;
  p.lon = kOriginLon +
          east_m / (111319.5 * std::cos(kOriginLat * M_PI / 180.0));
  return p;
}

PositionFix FixAt(double east_m, double north_m, double time_s = 0.0,
                  double speed_mps = 3.0) {
  const GeoPoint p = At(east_m, north_m);
  PositionFix fix;
  fix.SetPosition(p.lat, p.lon);
  fix.time_s = time_s;
  fix.has_time = true;
  fix.speed_mps = speed_mps;
  fix.has_speed = true;
  return fix;
}

/// East 2 km, north 2 km, west 2 km.
CourseDeviation Square() {
  CourseDeviation cdi;
  cdi.SetRoute({At(0, 0), At(2000, 0), At(2000, 2000), At(0, 2000)});
  return cdi;
}

std::vector<CourseEventType> Types(const std::vector<CourseEvent>& events) {
  std::vector<CourseEventType> out;
  for (const CourseEvent& e : events) out.push_back(e.type);
  return out;
}

TEST(CourseDeviation, NoRouteIsNoState) {
  CourseDeviation cdi;
  EXPECT_FALSE(cdi.has_route());
  EXPECT_TRUE(cdi.OnFix(FixAt(0, 0)).empty());
  EXPECT_FALSE(cdi.state().valid);

  cdi.SetRoute({At(0, 0)});
  EXPECT_FALSE(cdi.has_route());
}

TEST(CourseDeviation, CrossTrackIsPositiveRightOfTrack) {
  CourseDeviation cdi = Square();
  cdi.OnFix(FixAt(500, -100));  // south of an eastbound leg: starboard side
  const CourseState& s = cdi.state();
  ASSERT_TRUE(s.valid);
  EXPECT_EQ(s.leg_index, 0u);
  EXPECT_EQ(s.waypoint_index, 1u);
  EXPECT_NEAR(s.leg_course_deg, 90.0, 0.2);
  EXPECT_NEAR(s.cross_track_m, 100.0, 1.0);

  cdi.OnFix(FixAt(600, 100));
  EXPECT_NEAR(cdi.state().cross_track_m, -100.0, 1.0);
}

TEST(CourseDeviation, BearingAndDistanceAreToTheActiveWaypoint) {
  CourseDeviation cdi = Square();
  cdi.OnFix(FixAt(1000, -1000));
  const CourseState& s = cdi.state();
  // The waypoint is 1 km east and 1 km north: north-east.
  EXPECT_NEAR(s.bearing_to_waypoint_deg, 45.0, 0.3);
  EXPECT_NEAR(s.distance_to_waypoint_m, std::sqrt(2.0) * 1000.0, 3.0);
  EXPECT_NEAR(s.distance_to_end_m, s.distance_to_waypoint_m + 4000.0, 8.0);
}

TEST(CourseDeviation, TimeToGoAndEtaComeFromSog) {
  CourseDeviation cdi = Square();
  cdi.OnFix(FixAt(1000, 0, 5000.0, 4.0));
  const CourseState& s = cdi.state();
  ASSERT_TRUE(s.has_time_to_go);
  ASSERT_TRUE(s.has_eta);
  EXPECT_NEAR(s.time_to_waypoint_s, s.distance_to_waypoint_m / 4.0, 1e-6);
  EXPECT_NEAR(s.time_to_end_s, s.distance_to_end_m / 4.0, 1e-6);
  EXPECT_NEAR(s.eta_waypoint_s, 5000.0 + s.time_to_waypoint_s, 1e-6);
  EXPECT_NEAR(s.eta_end_s, 5000.0 + s.time_to_end_s, 1e-6);
}

TEST(CourseDeviation, NoTimeToGoWhenStoppedOrUntimed) {
  CourseDeviation cdi = Square();
  cdi.OnFix(FixAt(1000, 0, 10.0, 0.1));
  EXPECT_FALSE(cdi.state().has_time_to_go);
  EXPECT_FALSE(cdi.state().has_eta);

  PositionFix untimed = FixAt(1000, 0, 0.0, 3.0);
  untimed.has_time = false;
  cdi.OnFix(untimed);
  EXPECT_TRUE(cdi.state().has_time_to_go);
  EXPECT_FALSE(cdi.state().has_eta);
}

TEST(CourseDeviation, TheArrivalCircleAdvancesTheLeg) {
  CourseDeviation cdi = Square();
  cdi.OnFix(FixAt(1000, 0));
  const std::vector<CourseEvent> events = cdi.OnFix(FixAt(1940, 20));
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, CourseEventType::kWaypoint);
  EXPECT_EQ(events[0].waypoint_index, 1u);
  EXPECT_EQ(cdi.state().leg_index, 1u);
  EXPECT_EQ(cdi.state().waypoint_index, 2u);
  EXPECT_NEAR(cdi.state().leg_course_deg, 0.0, 0.2);
}

TEST(CourseDeviation, CrossingThePerpendicularAdvancesTheLeg) {
  CourseDeviation cdi = Square();
  cdi.OnFix(FixAt(1800, -150));
  const std::vector<CourseEvent> events = cdi.OnFix(FixAt(2010, -150));
  ASSERT_EQ(Types(events), std::vector<CourseEventType>{CourseEventType::kWaypoint});
  EXPECT_EQ(cdi.state().leg_index, 1u);
}

TEST(CourseDeviation, TheFirstFixSkipsLegsAlreadyBehindWithoutEvents) {
  CourseDeviation cdi = Square();
  EXPECT_TRUE(cdi.OnFix(FixAt(2100, 1000)).empty());
  EXPECT_EQ(cdi.state().leg_index, 1u);
  // East of a northbound leg is starboard.
  EXPECT_NEAR(cdi.state().cross_track_m, 100.0, 1.0);
}

TEST(CourseDeviation, BeyondTheLastPerpendicularIsNotArrivalUntilCrossedOrInside) {
  CourseDeviation cdi;
  cdi.SetRoute({At(0, 0), At(2000, 0)});
  EXPECT_TRUE(cdi.OnFix(FixAt(2500, 0)).empty());
  EXPECT_TRUE(cdi.OnFix(FixAt(2400, 0)).empty());
  EXPECT_FALSE(cdi.state().arrived);
  const std::vector<CourseEvent> events = cdi.OnFix(FixAt(2050, 0));
  ASSERT_EQ(Types(events), std::vector<CourseEventType>{CourseEventType::kArrived});
  EXPECT_EQ(events[0].waypoint_index, 1u);
}

TEST(CourseDeviation, ArrivalIsSaidOnceAndEndsTheIndicator) {
  CourseDeviation cdi;
  cdi.SetRoute({At(0, 0), At(2000, 0), At(2000, 2000)});
  cdi.OnFix(FixAt(1000, 0));
  cdi.OnFix(FixAt(2000, 10));
  cdi.OnFix(FixAt(2000, 1500));
  const std::vector<CourseEvent> events = cdi.OnFix(FixAt(2000, 2010));
  ASSERT_EQ(Types(events), std::vector<CourseEventType>{CourseEventType::kArrived});
  EXPECT_EQ(events[0].waypoint_index, 2u);
  EXPECT_TRUE(cdi.state().arrived);
  EXPECT_FALSE(cdi.state().has_time_to_go);
  EXPECT_TRUE(cdi.OnFix(FixAt(2000, 2100)).empty());
}

TEST(CourseDeviation, OffCourseIsHeldAndSaidOnce) {
  CourseDeviation cdi = Square();
  EXPECT_TRUE(cdi.OnFix(FixAt(200, 300, 0.0)).empty());
  EXPECT_TRUE(cdi.OnFix(FixAt(220, 300, 3.0)).empty());
  const std::vector<CourseEvent> off = cdi.OnFix(FixAt(240, 300, 5.0));
  ASSERT_EQ(Types(off), std::vector<CourseEventType>{CourseEventType::kOffCourse});
  EXPECT_NEAR(off[0].cross_track_m, -300.0, 2.0);
  EXPECT_TRUE(cdi.state().off_course);
  EXPECT_TRUE(cdi.OnFix(FixAt(260, 300, 6.0)).empty());
}

TEST(CourseDeviation, OneStrayFixIsNotOffCourse) {
  CourseDeviation cdi = Square();
  cdi.OnFix(FixAt(200, 0, 0.0));
  cdi.OnFix(FixAt(220, 400, 1.0));
  EXPECT_TRUE(cdi.OnFix(FixAt(240, 0, 2.0)).empty());
  EXPECT_TRUE(cdi.OnFix(FixAt(260, 400, 6.0)).empty());
  EXPECT_FALSE(cdi.state().off_course);
}

TEST(CourseDeviation, BackOnCourseNeedsToBeWellInsideTheLimit) {
  CourseDeviation cdi = Square();
  cdi.OnFix(FixAt(200, 300, 0.0));
  cdi.OnFix(FixAt(220, 300, 10.0));
  ASSERT_TRUE(cdi.state().off_course);
  EXPECT_TRUE(cdi.OnFix(FixAt(240, 170, 11.0)).empty());  // inside 185 m, above 148 m
  EXPECT_TRUE(cdi.state().off_course);
  EXPECT_EQ(Types(cdi.OnFix(FixAt(260, 100, 12.0))),
            std::vector<CourseEventType>{CourseEventType::kBackOnCourse});
  EXPECT_FALSE(cdi.state().off_course);
}

TEST(CourseDeviation, CourseOverGroundIsCarried) {
  CourseDeviation cdi = Square();
  PositionFix fix = FixAt(500, 0);
  fix.true_heading_deg = 95.0;
  fix.has_true_heading = true;
  cdi.OnFix(fix);
  EXPECT_TRUE(cdi.state().has_course_over_ground);
  EXPECT_DOUBLE_EQ(cdi.state().course_over_ground_deg, 95.0);
}

TEST(CourseDeviation, AFixWithNoPositionIsIgnored) {
  CourseDeviation cdi = Square();
  PositionFix fix;
  EXPECT_TRUE(cdi.OnFix(fix).empty());
  EXPECT_FALSE(cdi.state().valid);
}

TEST(CourseDeviation, ResetReturnsToTheFirstLeg) {
  CourseDeviation cdi = Square();
  cdi.OnFix(FixAt(1000, 0));
  cdi.OnFix(FixAt(2000, 0));
  ASSERT_EQ(cdi.state().leg_index, 1u);
  cdi.Reset();
  EXPECT_FALSE(cdi.state().valid);
  cdi.OnFix(FixAt(1000, 0));
  EXPECT_EQ(cdi.state().leg_index, 0u);
}

}  // namespace
}  // namespace nav
}  // namespace fv
