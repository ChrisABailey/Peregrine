// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// The slew layer (nav plan MM3). There is no Windows original to pin against
// — FalconView jumps — so every expectation here is either arithmetic written
// out in the comment, or a property the header states as a decision. The
// clock is an argument, so all of it is exact.

#include "fvkit/nav/camera_slew.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

using fv::CameraModes;
using fv::CameraSlew;
using fv::CameraTarget;
using fv::GeoPoint;
using fv::MapProjection;
using fv::MovingMapCamera;
using fv::ShortestRotationDelta;
using fv::SlewEasing;
using fv::SlewSettings;
using fv::SlewState;

// The same harbour-scale Charleston map the MM2 tests use.
MapProjection MakeProj(int w = 600, int h = 600, double scale = 250000.0) {
  MapProjection proj;
  EXPECT_TRUE(proj.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(proj.SetCenter(GeoPoint{32.78, -79.93}).ok());
  EXPECT_TRUE(proj.SetScale(scale).ok());
  return proj;
}

// A slew with the caps off, so a test about easing is about easing only.
CameraSlew UncappedSlew(double duration, SlewEasing easing) {
  CameraSlew slew;
  SlewSettings s;
  s.duration_s = duration;
  s.easing = easing;
  s.max_pan_px_per_s = 0.0;
  s.max_rotation_deg_per_s = 0.0;
  slew.SetSettings(s);
  return slew;
}

const GeoPoint kHome{32.78, -79.93};

// --- The rotation arc -----------------------------------------------------

TEST(SlewRotation, TheDeltaIsTheShortWayRound) {
  EXPECT_DOUBLE_EQ(ShortestRotationDelta(350.0, 10.0), 20.0);
  EXPECT_DOUBLE_EQ(ShortestRotationDelta(10.0, 350.0), -20.0);
  EXPECT_DOUBLE_EQ(ShortestRotationDelta(0.0, 90.0), 90.0);
  EXPECT_DOUBLE_EQ(ShortestRotationDelta(90.0, 0.0), -90.0);
  // The half turn is the range's open end: (-180, +180], so it resolves
  // clockwise from either side rather than being a coin toss per call.
  EXPECT_DOUBLE_EQ(ShortestRotationDelta(0.0, 180.0), 180.0);
  EXPECT_DOUBLE_EQ(ShortestRotationDelta(180.0, 0.0), 180.0);
}

TEST(SlewRotation, ATurnThrough360GoesUpwardsAndNotAllTheWayRound) {
  const MapProjection proj = MakeProj();
  CameraSlew slew = UncappedSlew(1.0, SlewEasing::kLinear);
  slew.Reset(kHome, 350.0);
  slew.RetargetTo(proj, kHome, 10.0);

  // Half way through a +20-degree arc from 350 is 360, i.e. 0.
  const SlewState mid = slew.Advance(0.5);
  EXPECT_NEAR(mid.rotation_deg, 0.0, 1e-9);
  // And never anywhere near the long way round.
  EXPECT_LT(std::fabs(fv::ShortestRotationDelta(0.0, mid.rotation_deg)), 1.0);

  const SlewState end = slew.Advance(0.5);
  EXPECT_DOUBLE_EQ(end.rotation_deg, 10.0);
  EXPECT_FALSE(end.active);
}

// --- Duration 0 is MM2 ----------------------------------------------------

TEST(SlewJump, AZeroDurationLandsImmediatelyAndIsReportedOnce) {
  const MapProjection proj = MakeProj();
  CameraSlew slew;
  SlewSettings s;
  s.duration_s = 0.0;
  slew.SetSettings(s);
  slew.Reset(kHome, 0.0);

  const GeoPoint goal{32.80, -79.90};
  slew.RetargetTo(proj, goal, 45.0);

  // The map is already there — before any time has passed at all.
  EXPECT_DOUBLE_EQ(slew.center().lat, goal.lat);
  EXPECT_DOUBLE_EQ(slew.center().lon, goal.lon);
  EXPECT_DOUBLE_EQ(slew.rotation_deg(), 45.0);
  EXPECT_FALSE(slew.active());

  // Advance(0) is the legal way to collect it, and it is reported once.
  const SlewState first = slew.Advance(0.0);
  EXPECT_TRUE(first.changed);
  EXPECT_FALSE(first.active);
  EXPECT_DOUBLE_EQ(first.center.lat, goal.lat);
  EXPECT_FALSE(slew.Advance(0.016).changed);
}

TEST(SlewJump, TheRateCapsDoNotResurrectAJump) {
  const MapProjection proj = MakeProj();
  CameraSlew slew;
  SlewSettings s;
  s.duration_s = 0.0;
  s.max_pan_px_per_s = 1.0;        // absurdly slow, and irrelevant
  s.max_rotation_deg_per_s = 1.0;  // likewise
  slew.SetSettings(s);
  slew.Reset(kHome, 0.0);

  slew.RetargetTo(proj, GeoPoint{32.90, -79.80}, 180.0);
  EXPECT_FALSE(slew.active());
  EXPECT_DOUBLE_EQ(slew.duration_s(), 0.0);
  EXPECT_DOUBLE_EQ(slew.rotation_deg(), 180.0);
}

// --- The ease -------------------------------------------------------------

TEST(SlewEase, LinearIsTheFractionOfTheWayThere) {
  const MapProjection proj = MakeProj();
  CameraSlew slew = UncappedSlew(1.0, SlewEasing::kLinear);
  slew.Reset(kHome, 0.0);
  const GeoPoint goal{32.88, -79.83};  // +0.10 lat, +0.10 lon
  slew.RetargetTo(proj, goal, 0.0);

  const SlewState q = slew.Advance(0.25);
  EXPECT_NEAR(q.center.lat, 32.78 + 0.25 * 0.10, 1e-12);
  EXPECT_NEAR(q.center.lon, -79.93 + 0.25 * 0.10, 1e-12);
  EXPECT_TRUE(q.active);
  EXPECT_TRUE(q.changed);
}

TEST(SlewEase, SmoothstepStartsBehindLinearAndEndsAheadOfIt) {
  const MapProjection proj = MakeProj();
  const GeoPoint goal{32.88, -79.93};  // due north, +0.10 lat

  CameraSlew linear = UncappedSlew(1.0, SlewEasing::kLinear);
  CameraSlew eased = UncappedSlew(1.0, SlewEasing::kEaseInOut);
  linear.Reset(kHome, 0.0);
  eased.Reset(kHome, 0.0);
  linear.RetargetTo(proj, goal, 0.0);
  eased.RetargetTo(proj, goal, 0.0);

  // smoothstep(0.25) = 3(0.0625) - 2(0.015625) = 0.15625 < 0.25.
  EXPECT_NEAR(eased.Advance(0.25).center.lat, 32.78 + 0.15625 * 0.10, 1e-12);
  EXPECT_LT(eased.center().lat, linear.Advance(0.25).center.lat);

  // They meet exactly at the midpoint: smoothstep(0.5) = 0.5.
  EXPECT_NEAR(eased.Advance(0.25).center.lat, 32.78 + 0.5 * 0.10, 1e-12);

  // smoothstep(0.75) = 3(0.5625) - 2(0.421875) = 0.84375 > 0.75.
  EXPECT_NEAR(eased.Advance(0.25).center.lat, 32.78 + 0.84375 * 0.10, 1e-12);
  EXPECT_GT(eased.center().lat, linear.Advance(0.25).center.lat);
}

TEST(SlewEase, TheArrivalIsExactAndThenNothingMovesAgain) {
  const MapProjection proj = MakeProj();
  CameraSlew slew = UncappedSlew(0.5, SlewEasing::kEaseInOut);
  slew.Reset(kHome, 10.0);
  const GeoPoint goal{32.88, -79.83};
  slew.RetargetTo(proj, goal, 40.0);

  // Overshooting the duration lands ON the goal, not past it and not short of
  // it by the last ease step.
  const SlewState end = slew.Advance(5.0);
  EXPECT_DOUBLE_EQ(end.center.lat, goal.lat);
  EXPECT_DOUBLE_EQ(end.center.lon, goal.lon);
  EXPECT_DOUBLE_EQ(end.rotation_deg, 40.0);
  EXPECT_TRUE(end.changed);
  EXPECT_FALSE(end.active);

  const SlewState after = slew.Advance(1.0);
  EXPECT_FALSE(after.changed);
  EXPECT_FALSE(after.active);
}

TEST(SlewEase, ATickOfNoTimeMovesNothingButKeepsTheAnimationAlive) {
  const MapProjection proj = MakeProj();
  CameraSlew slew = UncappedSlew(1.0, SlewEasing::kLinear);
  slew.Reset(kHome, 0.0);
  slew.RetargetTo(proj, GeoPoint{32.88, -79.93}, 0.0);
  slew.Advance(0.25);

  const double lat = slew.center().lat;
  const SlewState still = slew.Advance(0.0);
  EXPECT_DOUBLE_EQ(still.center.lat, lat);
  EXPECT_FALSE(still.changed);
  EXPECT_TRUE(still.active);

  // A NaN dt is a dropped frame, not a teleport.
  const SlewState nan_tick = slew.Advance(std::nan(""));
  EXPECT_DOUBLE_EQ(nan_tick.center.lat, lat);
  EXPECT_TRUE(nan_tick.active);
}

// --- The rate caps --------------------------------------------------------

TEST(SlewCaps, APanTooFastForTheCapTakesLongerRatherThanArrivingShort) {
  const MapProjection proj = MakeProj();
  CameraSlew slew;
  SlewSettings s;
  s.duration_s = 0.35;
  s.easing = SlewEasing::kLinear;
  s.max_pan_px_per_s = 400.0;
  s.max_rotation_deg_per_s = 0.0;
  slew.SetSettings(s);
  slew.Reset(kHome, 0.0);

  // A pan of exactly 800 surface pixels due east: two seconds at 400 px/s.
  const double dlon = 800.0 * proj.DegPerPixelLon();
  const GeoPoint goal{kHome.lat, kHome.lon + dlon};
  slew.RetargetTo(proj, goal, 0.0);

  EXPECT_NEAR(slew.duration_s(), 2.0, 1e-9);
  EXPECT_GT(slew.duration_s(), s.duration_s);
  // Still lands on the camera's answer — the cap extends, it never clips.
  // (The duration is 2 s to within the round trip through the goal's
  // longitude, so the tick is taken from the slew rather than written out.)
  const SlewState end = slew.Advance(slew.duration_s());
  EXPECT_NEAR(end.center.lon, goal.lon, 1e-12);
  EXPECT_FALSE(end.active);
}

TEST(SlewCaps, AHalfTurnIsSpreadOverTheRotationCap) {
  const MapProjection proj = MakeProj();
  CameraSlew slew;
  SlewSettings s;
  s.duration_s = 0.35;
  s.max_pan_px_per_s = 0.0;
  s.max_rotation_deg_per_s = 120.0;
  slew.SetSettings(s);
  slew.Reset(kHome, 0.0);

  slew.RetargetTo(proj, kHome, 180.0);
  // 180 degrees at 120 deg/s = 1.5 s, not the 0.35 s asked for.
  EXPECT_NEAR(slew.duration_s(), 1.5, 1e-9);
}

TEST(SlewCaps, TheCentreAndTheRotationLandTogetherOnTheLongerOfTheTwo) {
  const MapProjection proj = MakeProj();
  CameraSlew slew;
  SlewSettings s;
  s.duration_s = 0.1;
  s.easing = SlewEasing::kLinear;
  s.max_pan_px_per_s = 400.0;       // 800 px -> 2.0 s
  s.max_rotation_deg_per_s = 120.0;  // 60 deg -> 0.5 s
  slew.SetSettings(s);
  slew.Reset(kHome, 0.0);

  const GeoPoint goal{kHome.lat, kHome.lon + 800.0 * proj.DegPerPixelLon()};
  slew.RetargetTo(proj, goal, 60.0);
  EXPECT_NEAR(slew.duration_s(), 2.0, 1e-9);

  // At 0.6 s the rotation would have finished on its own cap; it has not,
  // because it is riding the pan's duration.
  const SlewState mid = slew.Advance(0.6);
  EXPECT_NEAR(mid.rotation_deg, 60.0 * 0.3, 1e-9);
  EXPECT_TRUE(mid.active);
}

// --- Retargeting in flight ------------------------------------------------

TEST(SlewRetarget, ANewTargetRestartsFromWhereTheMapIsAndNothingJumps) {
  const MapProjection proj = MakeProj();
  CameraSlew slew = UncappedSlew(1.0, SlewEasing::kLinear);
  slew.Reset(kHome, 0.0);
  slew.RetargetTo(proj, GeoPoint{32.88, -79.93}, 0.0);
  const SlewState mid = slew.Advance(0.5);  // half way: lat 32.83
  EXPECT_NEAR(mid.center.lat, 32.83, 1e-12);

  const GeoPoint second{32.78, -79.83};
  slew.RetargetTo(proj, second, 0.0);
  // The retarget itself moves nothing — the map is where it was.
  const SlewState resumed = slew.Advance(0.0);
  EXPECT_NEAR(resumed.center.lat, 32.83, 1e-12);
  EXPECT_FALSE(resumed.changed);
  EXPECT_TRUE(resumed.active);

  // And the clock restarted: a full duration from here lands on the NEW goal,
  // with the abandoned one simply forgotten rather than queued.
  const SlewState end = slew.Advance(1.0);
  EXPECT_DOUBLE_EQ(end.center.lat, second.lat);
  EXPECT_DOUBLE_EQ(end.center.lon, second.lon);
  EXPECT_FALSE(end.active);
}

TEST(SlewRetarget, RetargetingAtThePlaceWeAreAlreadyAtStopsRatherThanRestarts) {
  const MapProjection proj = MakeProj();
  CameraSlew slew = UncappedSlew(1.0, SlewEasing::kLinear);
  slew.Reset(kHome, 0.0);
  slew.RetargetTo(proj, GeoPoint{32.88, -79.93}, 0.0);
  slew.Advance(0.5);
  const GeoPoint here = slew.center();

  slew.RetargetTo(proj, here, slew.rotation_deg());
  EXPECT_FALSE(slew.active());
  const SlewState state = slew.Advance(1.0);
  EXPECT_FALSE(state.changed);
  EXPECT_DOUBLE_EQ(state.center.lat, here.lat);
}

TEST(SlewRetarget, AFirstTargetWithNoResetLandsImmediately) {
  const MapProjection proj = MakeProj();
  CameraSlew slew = UncappedSlew(1.0, SlewEasing::kLinear);
  EXPECT_FALSE(slew.started());

  const GeoPoint goal{32.88, -79.83};
  slew.RetargetTo(proj, goal, 30.0);
  EXPECT_TRUE(slew.started());
  EXPECT_FALSE(slew.active());
  const SlewState state = slew.Advance(0.0);
  EXPECT_TRUE(state.changed);
  EXPECT_DOUBLE_EQ(state.center.lat, goal.lat);
  EXPECT_DOUBLE_EQ(state.rotation_deg, 30.0);
}

TEST(SlewRetarget, ResetCancelsAnAnimationAndReportsNothingBack) {
  const MapProjection proj = MakeProj();
  CameraSlew slew = UncappedSlew(1.0, SlewEasing::kLinear);
  slew.Reset(kHome, 0.0);
  slew.RetargetTo(proj, GeoPoint{32.88, -79.83}, 90.0);
  slew.Advance(0.5);

  // The user grabbed the map; the shell says where it now is.
  const GeoPoint panned{33.10, -80.20};
  slew.Reset(panned, 12.0);
  EXPECT_FALSE(slew.active());
  const SlewState state = slew.Advance(1.0);
  EXPECT_FALSE(state.changed);
  EXPECT_DOUBLE_EQ(state.center.lat, panned.lat);
  EXPECT_DOUBLE_EQ(state.rotation_deg, 12.0);
}

TEST(SlewRetarget, FinishLandsOnTheGoalNow) {
  const MapProjection proj = MakeProj();
  CameraSlew slew = UncappedSlew(10.0, SlewEasing::kEaseInOut);
  slew.Reset(kHome, 0.0);
  const GeoPoint goal{32.88, -79.83};
  slew.RetargetTo(proj, goal, 90.0);
  slew.Advance(0.1);

  slew.Finish();
  EXPECT_FALSE(slew.active());
  const SlewState state = slew.Advance(0.0);
  EXPECT_TRUE(state.changed);
  EXPECT_DOUBLE_EQ(state.center.lat, goal.lat);
  EXPECT_DOUBLE_EQ(state.rotation_deg, 90.0);
  EXPECT_FALSE(slew.Advance(0.0).changed);
}

// --- Longitude ------------------------------------------------------------

TEST(SlewGeo, ARecentreAcrossTheAntimeridianGoesTheShortWay) {
  MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(600, 600).ok());
  ASSERT_TRUE(proj.SetCenter(GeoPoint{0.0, 179.9}).ok());
  ASSERT_TRUE(proj.SetScale(250000.0).ok());

  CameraSlew slew = UncappedSlew(1.0, SlewEasing::kLinear);
  slew.Reset(GeoPoint{0.0, 179.9}, 0.0);
  slew.RetargetTo(proj, GeoPoint{0.0, -179.9}, 0.0);

  // Half way across a 0.2-degree gap is the antimeridian itself, NOT the
  // prime meridian on the far side of the world.
  const SlewState mid = slew.Advance(0.5);
  EXPECT_GT(std::fabs(mid.center.lon), 179.0);
  const SlewState end = slew.Advance(0.5);
  EXPECT_NEAR(end.center.lon, -179.9, 1e-12);
}

// --- The camera in front of it --------------------------------------------

TEST(SlewCamera, ADiscreteRecentreArrivesWhereTheCameraSaid) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  CameraModes modes;  // auto-centre on, north-up, not continuous
  camera.SetModes(modes);

  // The ship is in the top-left corner of the window, well outside any apron,
  // heading north — so MM2 recentres and puts it in the bottom-middle box.
  camera.RecomputeApron(600, 600, 300, 300);
  GeoPoint ship;
  ASSERT_TRUE(proj.SurfaceToGeo(50.0, 50.0, &ship).ok());
  const CameraTarget target = camera.Update(proj, ship, 0.0, 0.0);
  ASSERT_TRUE(target.changed);

  CameraSlew slew = UncappedSlew(0.5, SlewEasing::kEaseInOut);
  slew.Reset(proj.Center(), 0.0);
  slew.Retarget(proj, target);
  EXPECT_TRUE(slew.active());

  // It gets there, and it gets there by moving each frame rather than at the
  // last one.
  const SlewState first = slew.Advance(0.1);
  EXPECT_TRUE(first.changed);
  EXPECT_NE(first.center.lat, proj.Center().lat);
  const SlewState end = slew.Advance(0.4);
  EXPECT_DOUBLE_EQ(end.center.lat, target.center.lat);
  EXPECT_DOUBLE_EQ(end.center.lon, target.center.lon);
  EXPECT_FALSE(end.active);
}

TEST(SlewCamera, ATargetTheCameraDidNotChangeDoesNotInterruptAnAnimation) {
  const MapProjection proj = MakeProj();
  CameraSlew slew = UncappedSlew(1.0, SlewEasing::kLinear);
  slew.Reset(kHome, 0.0);
  const GeoPoint goal{32.88, -79.93};
  slew.RetargetTo(proj, goal, 0.0);
  slew.Advance(0.5);

  // "The ship is still inside its apron" — nothing to aim at, and above all
  // not a reason to restart the ease or to stop half way.
  CameraTarget unchanged;
  slew.Retarget(proj, unchanged);
  EXPECT_TRUE(slew.active());
  const SlewState end = slew.Advance(0.5);
  EXPECT_DOUBLE_EQ(end.center.lat, goal.lat);
  EXPECT_FALSE(end.active);
}

}  // namespace
