// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The CoreLocation conversion, tested on the mac.
//
// The sentinel table in PPLocationFix.h is a rule with five independent
// branches, and a rule with five branches gets pinned where a test can run.
// What is left on the phone is CLLocationManager's authorization dance and
// its delivery queue, which cannot be tested anywhere else.

#include "PPLocationFix.h"

#include <vector>

#include <gtest/gtest.h>

#include "fvkit/nav/road_snap.h"

namespace {

// A sample a good receiver on Kiawah would produce: everything known.
PPLocationSample GoodSample() {
  PPLocationSample s;
  s.latitude = 32.6075;
  s.longitude = -80.0854;
  s.horizontal_accuracy_m = 12.5;
  s.altitude_m = 3.2;
  s.vertical_accuracy_m = 8.0;
  s.course_deg = 91.5;
  s.course_accuracy_deg = 4.0;
  s.speed_mps = 5.4;
  s.speed_accuracy_mps = 1.0;
  s.timestamp_s = 1787000000.0;
  s.has_timestamp = true;
  return s;
}

TEST(PPLocationFix, EverythingKnownArrivesIntact) {
  const fv::PositionFix f = PPFixFromLocationSample(GoodSample());

  EXPECT_TRUE(f.has_position);
  EXPECT_DOUBLE_EQ(f.lat, 32.6075);
  EXPECT_DOUBLE_EQ(f.lon, -80.0854);
  EXPECT_TRUE(f.has_altitude);
  EXPECT_DOUBLE_EQ(f.altitude_msl_m, 3.2);
  EXPECT_TRUE(f.has_true_heading);
  EXPECT_DOUBLE_EQ(f.true_heading_deg, 91.5);
  EXPECT_TRUE(f.has_speed);
  EXPECT_DOUBLE_EQ(f.speed_mps, 5.4);
  EXPECT_TRUE(f.has_time);
  EXPECT_DOUBLE_EQ(f.time_s, 1787000000.0);

  // Neither is a CLLocation field, so neither is ever set.
  EXPECT_FALSE(f.has_magnetic_heading);
  EXPECT_FALSE(f.has_satellite_count);
}

// The default sample is "nothing is known", which is the aggregate a caller
// gets by writing `PPLocationSample s;` — so an empty one must produce a fix
// that claims nothing at all rather than a fix at null island.
TEST(PPLocationFix, ADefaultSampleClaimsNothing) {
  const fv::PositionFix f = PPFixFromLocationSample(PPLocationSample{});

  EXPECT_FALSE(f.has_position);
  EXPECT_FALSE(f.has_altitude);
  EXPECT_FALSE(f.has_true_heading);
  EXPECT_FALSE(f.has_speed);
  EXPECT_FALSE(f.has_time);
  EXPECT_FALSE(f.has_hdop);
}

// Each sentinel invalidates ITS OWN field and nothing else. Five branches,
// five cases, driven off the good sample so a regression shows up as one
// failing expectation rather than as a wall of them.
TEST(PPLocationFix, ANegativeAccuracyInvalidatesOnlyThePosition) {
  PPLocationSample s = GoodSample();
  s.horizontal_accuracy_m = -1.0;
  const fv::PositionFix f = PPFixFromLocationSample(s);

  EXPECT_FALSE(f.has_position);
  EXPECT_FALSE(f.has_hdop);  // the hdop comes OFF the accuracy, so it goes too
  // A CLLocation with a bad coordinate can still carry a real altitude, and
  // dropping it here would throw away something the receiver knew.
  EXPECT_TRUE(f.has_altitude);
  EXPECT_TRUE(f.has_true_heading);
  EXPECT_TRUE(f.has_speed);
  EXPECT_TRUE(f.has_time);
}

TEST(PPLocationFix, ANegativeVerticalAccuracyInvalidatesOnlyTheAltitude) {
  PPLocationSample s = GoodSample();
  s.vertical_accuracy_m = -1.0;
  const fv::PositionFix f = PPFixFromLocationSample(s);

  EXPECT_FALSE(f.has_altitude);
  EXPECT_TRUE(f.has_position);
  EXPECT_TRUE(f.has_true_heading);
  EXPECT_TRUE(f.has_speed);
}

TEST(PPLocationFix, ANegativeCourseInvalidatesOnlyTheHeading) {
  PPLocationSample s = GoodSample();
  s.course_deg = -1.0;
  const fv::PositionFix f = PPFixFromLocationSample(s);

  EXPECT_FALSE(f.has_true_heading);
  EXPECT_TRUE(f.has_position);
  EXPECT_TRUE(f.has_speed);
}

TEST(PPLocationFix, ANegativeSpeedInvalidatesOnlyTheSpeed) {
  PPLocationSample s = GoodSample();
  s.speed_mps = -1.0;
  const fv::PositionFix f = PPFixFromLocationSample(s);

  EXPECT_FALSE(f.has_speed);
  EXPECT_TRUE(f.has_position);
  EXPECT_TRUE(f.has_true_heading);
}

// The deliberate NON-rule, and the one most likely to be "fixed" by somebody
// reading Apple's docs in isolation: a negative courseAccuracy or
// speedAccuracy is a missing quality number, not a missing course or speed.
// A simulator replaying a GPX reports exactly this shape.
TEST(PPLocationFix, AccuracyOfCourseAndSpeedNeverInvalidates) {
  PPLocationSample s = GoodSample();
  s.course_accuracy_deg = -1.0;
  s.speed_accuracy_mps = -1.0;
  const fv::PositionFix f = PPFixFromLocationSample(s);

  EXPECT_TRUE(f.has_true_heading);
  EXPECT_DOUBLE_EQ(f.true_heading_deg, 91.5);
  EXPECT_TRUE(f.has_speed);
  EXPECT_DOUBLE_EQ(f.speed_mps, 5.4);
}

// Zero is a real value in every one of these fields — a stationary ship
// reports speed 0, a course of due north is 0, and a fix on the geoid is
// altitude 0. `< 0` and not `<= 0` is therefore load-bearing.
TEST(PPLocationFix, ZeroIsAValueAndNotASentinel) {
  PPLocationSample s = GoodSample();
  s.course_deg = 0.0;
  s.speed_mps = 0.0;
  s.altitude_m = 0.0;
  s.vertical_accuracy_m = 0.0;
  s.horizontal_accuracy_m = 0.0;
  const fv::PositionFix f = PPFixFromLocationSample(s);

  EXPECT_TRUE(f.has_position);
  EXPECT_TRUE(f.has_true_heading);
  EXPECT_DOUBLE_EQ(f.true_heading_deg, 0.0);
  EXPECT_TRUE(f.has_speed);
  EXPECT_DOUBLE_EQ(f.speed_mps, 0.0);
  EXPECT_TRUE(f.has_altitude);
  EXPECT_DOUBLE_EQ(f.altitude_msl_m, 0.0);
}

// The hdop mapping, stated as the property it exists for: the snapper's
// search radius must come out as the metres CoreLocation reported. That is
// `hdop * RoadSnapSettings::hdop_scale`, floored and capped by the snapper —
// so this pins the multiplication, and the floor and cap stay MM5's business.
TEST(PPLocationFix, HorizontalAccuracyRoundTripsThroughTheSnapRadius) {
  const fv::RoadSnapSettings defaults;
  ASSERT_DOUBLE_EQ(defaults.hdop_scale, kPPHdopMetresPerUnit)
      << "the snapper's metres-per-HDOP moved and PPLocationFix.h did not";

  for (const double accuracy_m : {5.0, 12.5, 40.0, 100.0}) {
    PPLocationSample s = GoodSample();
    s.horizontal_accuracy_m = accuracy_m;
    const fv::PositionFix f = PPFixFromLocationSample(s);
    ASSERT_TRUE(f.has_hdop);
    EXPECT_DOUBLE_EQ(f.hdop * defaults.hdop_scale, accuracy_m);
  }
}

// A phone reports at ~1 Hz and the fixes reach the overlay through MM1's
// queue, so the shape that actually crosses the thread boundary is worth one
// case: a sample in, a fix out, and the same fix out of the queue.
TEST(PPLocationFix, AConvertedFixSurvivesTheQueue) {
  fv::FixQueue queue;
  queue.Push(PPFixFromLocationSample(GoodSample()));

  std::vector<fv::PositionFix> drained;
  queue.Drain(&drained);
  ASSERT_EQ(drained.size(), 1u);
  EXPECT_TRUE(drained[0].has_position);
  EXPECT_DOUBLE_EQ(drained[0].lat, 32.6075);
  EXPECT_EQ(queue.dropped(), 0u);
}

}  // namespace
