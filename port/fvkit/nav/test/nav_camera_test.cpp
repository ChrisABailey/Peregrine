// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// The moving-map camera (nav plan MM2), ported from gps_draw.cpp's
// map_update / auto_center_bounding_box_calc / set_new_map / get_delta_xy_*.
// Every expectation below is hand-computed from the original's formulas —
// there is no golden and no reference render, so the arithmetic is written
// out in the comments where it is not obvious.

#include "fvkit/nav/camera.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

using fv::ApronRect;
using fv::CameraModes;
using fv::CameraTarget;
using fv::ComputeApron;
using fv::DeltaXyContinuous;
using fv::DeltaXyDiscrete;
using fv::DeltaXyTrackUp;
using fv::GeoPoint;
using fv::MapProjection;
using fv::MovingMapCamera;

// A harbour-scale map over Charleston, 600x600, so a pixel is a small number
// of metres and the apron arithmetic is in round numbers.
MapProjection MakeProj(int w = 600, int h = 600, double scale = 250000.0) {
  MapProjection proj;
  EXPECT_TRUE(proj.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(proj.SetCenter(GeoPoint{32.78, -79.93}).ok());
  EXPECT_TRUE(proj.SetScale(scale).ok());
  return proj;
}

GeoPoint AtSurface(const MapProjection& proj, double sx, double sy) {
  GeoPoint p;
  EXPECT_TRUE(proj.SurfaceToGeo(sx, sy, &p).ok());
  return p;
}

// --- The 3x3 placement ----------------------------------------------------

// THE TABLE THE PLAN ASKED FOR. On a square window the eight compass points
// put the ship in the eight perimeter boxes, each a third of the window from
// the centre, and the offset is always OPPOSITE the heading — which is the
// whole point of the algorithm: the map goes where the ship is going, so the
// ship sits at the back of it.
TEST(MovingMapPlacement, TheEightCompassPointsChooseTheEightPerimeterBoxes) {
  struct Case {
    double angle;
    double dx;
    double dy;
  };
  // W = H = 600, so a box offset is 600/3 = 200 px.
  const Case cases[] = {
      {0.0, 0.0, -200.0},      // north: centre above the ship
      {45.0, 200.0, -200.0},   // north-east
      {90.0, 200.0, 0.0},      // east
      {135.0, 200.0, 200.0},   // south-east
      {180.0, 0.0, 200.0},     // south
      {225.0, -200.0, 200.0},  // south-west
      {270.0, -200.0, 0.0},    // west
      {315.0, -200.0, -200.0}, // north-west
  };
  for (const Case& c : cases) {
    double dx = 0.0;
    double dy = 0.0;
    DeltaXyDiscrete(600, 600, c.angle, &dx, &dy);
    EXPECT_NEAR(c.dx, dx, 1e-9) << "heading " << c.angle;
    EXPECT_NEAR(c.dy, dy, 1e-9) << "heading " << c.angle;
  }
}

// The unreachable else-branch of the original's `!= 90 || != 270` tautology
// hard-codes (i=0,j=1) at due east and (i=2,j=1) at due west. This pins that
// the live branch — the only one this port carries — produces exactly those
// two boxes anyway, which is why dropping the dead code changes nothing.
TEST(MovingMapPlacement, DueEastAndDueWestAgreeWithTheUnreachableBranch) {
  double dx = 0.0;
  double dy = 0.0;
  DeltaXyDiscrete(600, 600, 90.0, &dx, &dy);
  // box (0, 1): dx = W*(0.5 - 1/6) = +200, dy = H*(0.5 - 0.5) = 0.
  EXPECT_NEAR(200.0, dx, 1e-9);
  EXPECT_NEAR(0.0, dy, 1e-9);
  DeltaXyDiscrete(600, 600, 270.0, &dx, &dy);
  // box (2, 1): dx = W*(0.5 - 5/6) = -200, dy = 0.
  EXPECT_NEAR(-200.0, dx, 1e-9);
  EXPECT_NEAR(0.0, dy, 1e-9);
}

// The placement is ASPECT-AWARE, and that is what `w_to_h` is for: on a wide
// window a north-easterly course leaves through the TOP rather than the side,
// so the ship belongs at the bottom middle and not at the bottom left.
TEST(MovingMapPlacement, AWideWindowMovesTheDiagonalBoxToTheBottomMiddle) {
  double dx = 0.0;
  double dy = 0.0;
  DeltaXyDiscrete(800, 400, 45.0, &dx, &dy);
  // w_to_h = 2, mid_angle = atan(2) = 63.4 deg, so 45 takes the first case:
  // i = trunc(1 - tan(45)/2 + 0.5) = trunc(1.0) = 1, j = 2.
  EXPECT_NEAR(0.0, dx, 1e-9);
  EXPECT_NEAR(400.0 * (0.5 - 5.0 / 6.0), dy, 1e-9);  // -133.33
  // The same heading on a square window goes to the corner box.
  DeltaXyDiscrete(400, 400, 45.0, &dx, &dy);
  EXPECT_NEAR(400.0 / 3.0, dx, 1e-9);
}

// The continuous placement is NOT the discrete one with the rounding removed:
// the original leaves the `+ 0.5` in, so a continuous placement sits half a
// box — W/6 — from the discrete box it corresponds to. Preserved quirk.
TEST(MovingMapPlacement, ContinuousCarriesTheDiscreteRoundingTermAsAnOffset) {
  double disc_x = 0.0;
  double disc_y = 0.0;
  double cont_x = 0.0;
  double cont_y = 0.0;
  DeltaXyDiscrete(600, 600, 0.0, &disc_x, &disc_y);
  DeltaXyContinuous(600, 600, 0.0, &cont_x, &cont_y);
  EXPECT_NEAR(0.0, disc_x, 1e-9);
  EXPECT_NEAR(-100.0, cont_x, 1e-9);  // i = 1.5 -> W*(0.5 - 4/6)
  EXPECT_NEAR(600.0 / 6.0, disc_x - cont_x, 1e-9);
  EXPECT_NEAR(disc_y, cont_y, 1e-9);  // j is the literal 2 in both
}

// FINDING, pinned rather than fixed: "continuous" centring is continuous
// only WITHIN one of the placement's four branches. Sweeping the heading a
// degree at a time slides the ship a few pixels round the perimeter — until
// the derivation changes case, where the two formulas do not meet and the
// ship jumps most of a box. That is inherent to the algorithm and not to the
// `+ 0.5`: the discrete version is CHOOSING between a corner box and an edge
// box there, and interpolating an index does not make that choice gradual.
// It is worth knowing because it says continuous mode needs MM3's slew as
// much as the discrete recentres do.
TEST(MovingMapPlacement, ContinuousIsSmoothInsideACaseAndJumpsBetweenThem) {
  double prev_x = 0.0;
  double prev_y = 0.0;
  DeltaXyContinuous(600, 600, 0.0, &prev_x, &prev_y);
  int jumps = 0;
  double biggest_smooth_step = 0.0;
  for (double a = 1.0; a < 360.0; a += 1.0) {
    double cx = 0.0;
    double cy = 0.0;
    DeltaXyContinuous(600, 600, a, &cx, &cy);
    const double step = std::hypot(cx - prev_x, cy - prev_y);
    if (step > 50.0) {
      ++jumps;
    } else if (step > biggest_smooth_step) {
      biggest_smooth_step = step;
    }
    prev_x = cx;
    prev_y = cy;
  }
  // Five of them on a square window: at the three diagonals where the case
  // changes (46, 136, 316), at due east, and at 226. Due west does NOT jump,
  // because there the two cases happen to agree — the boundaries are not
  // symmetric, which is a consequence of `<=` and the `f` sign flip landing
  // on different sides of each one.
  EXPECT_EQ(5, jumps);
  EXPECT_LT(biggest_smooth_step, 10.0);

  // The discrete placement, over the same sweep, only ever takes one of nine
  // positions.
  for (double a = 0.0; a < 360.0; a += 1.0) {
    double dx = 0.0;
    double dy = 0.0;
    DeltaXyDiscrete(600, 600, a, &dx, &dy);
    EXPECT_NEAR(0.0, std::fmod(std::fabs(dx), 200.0), 1e-9) << "heading " << a;
    EXPECT_NEAR(0.0, std::fmod(std::fabs(dy), 200.0), 1e-9) << "heading " << a;
  }
}

// --- Track-up -------------------------------------------------------------

// The anchor puts the ship a third of the window BEHIND the centre, measured
// along the course — so on a north-up screen it is about 83% of the way down,
// and as the course swings the offset swings with it.
TEST(MovingMapTrackUp, TheShipIsAnchoredAThirdOfTheWindowBehindTheCentre) {
  double dx = 0.0;
  double dy = 0.0;
  DeltaXyTrackUp(600, 600, 0.0, 0.0, 0.33333333, &dx, &dy);
  EXPECT_NEAR(0.0, dx, 1e-6);
  EXPECT_NEAR(-200.0, dy, 1e-5);  // centre 200 px above the ship
  DeltaXyTrackUp(600, 600, 90.0, 0.0, 0.33333333, &dx, &dy);
  EXPECT_NEAR(200.0, dx, 1e-5);  // heading east: centre 200 px to the right
  EXPECT_NEAR(0.0, dy, 1e-5);
  // The offset magnitude is the same at every heading — it is a rotation of
  // one fixed anchor, which is the property the shear below breaks.
  for (double a = 0.0; a < 360.0; a += 30.0) {
    DeltaXyTrackUp(600, 600, a, 0.0, 0.33333333, &dx, &dy);
    EXPECT_NEAR(200.0, std::hypot(dx, dy), 1e-5) << "heading " << a;
  }
}

// THE ANCHOR IS TWO UNIT VECTORS, NOT A ROTATION, and this is the assertion
// that says which. The original's matrix reads as a rotation with a sign
// error in its second row; it is instead `d_x * right-of-course + d_y *
// ahead` in a Y-DOWN surface, where ahead is (sin, -cos) and right is
// (cos, sin). A port that "corrected" the sign would put the ownship ABOVE
// the map centre while heading north, which is backwards, and no golden
// would catch it — so the decomposition is asserted directly, per the
// ledger's rule that asymmetric behaviour needs its own directional test.
TEST(MovingMapTrackUp, TheOffsetDecomposesIntoAheadAndRightOfCourse) {
  const double kAheadPx = 200.0;   // 1/3 of 600
  const double kRightPx = 150.0;   // 0.25 of 600
  for (double a = 0.0; a < 360.0; a += 15.0) {
    double dx = 0.0;
    double dy = 0.0;
    DeltaXyTrackUp(600, 600, a, 0.25, 0.33333333, &dx, &dy);
    const double rad = a * 3.14159265358979323846 / 180.0;
    const double ahead_x = std::sin(rad);
    const double ahead_y = -std::cos(rad);
    const double right_x = std::cos(rad);
    const double right_y = std::sin(rad);
    EXPECT_NEAR(kRightPx * right_x + kAheadPx * ahead_x, dx, 1e-4)
        << "heading " << a;
    EXPECT_NEAR(kRightPx * right_y + kAheadPx * ahead_y, dy, 1e-4)
        << "heading " << a;
  }
  // Spelled out at due east, where "right of course" is south: the map
  // centre goes ahead (east, +x) and to the ship's right (south, +y).
  double dx = 0.0;
  double dy = 0.0;
  DeltaXyTrackUp(600, 600, 90.0, 0.25, 0.33333333, &dx, &dy);
  EXPECT_NEAR(kAheadPx, dx, 1e-4);
  EXPECT_NEAR(kRightPx, dy, 1e-4);
}

// --- The apron ------------------------------------------------------------

TEST(MovingMapApron, TrackUpIsAFixedBoxLowAndCentral) {
  CameraModes modes;
  modes.auto_rotate = true;
  // W/5 = 200, 2H/5 = 240, ul = (2*200, 600/2) = (400, 300).
  const ApronRect r = ComputeApron(modes, 1000, 600, 500, 500);
  EXPECT_EQ(400, r.left);
  EXPECT_EQ(300, r.top);
  EXPECT_EQ(601, r.right);
  EXPECT_EQ(541, r.bottom);
  // It does not depend on where the ship is — the chart moves, not the ship.
  const ApronRect elsewhere = ComputeApron(modes, 1000, 600, 10, 10);
  EXPECT_EQ(r.left, elsewhere.left);
  EXPECT_EQ(r.bottom, elsewhere.bottom);
}

// QUIRK, PINNED: the track-up box's left edge is 2*(W/5), not 2W/5. On a
// window whose width is not a multiple of 5 those differ.
TEST(MovingMapApron, TheTrackUpLeftEdgeIsTwiceTheTruncatedFifth) {
  CameraModes modes;
  modes.auto_rotate = true;
  const ApronRect r = ComputeApron(modes, 999, 600, 500, 300);
  EXPECT_EQ(398, r.left);      // 2 * (999/5) = 2 * 199
  EXPECT_NE(2 * 999 / 5, r.left);  // 399, which is what the comment says
}

TEST(MovingMapApron, NorthUpNarrowsTheBoxWhenTheShipIsCentral) {
  CameraModes modes;
  // Outer box: 4W/5 x 4H/5 = 960 x 480 at (120, 60).
  // Ship dead centre: both thirds index 1, so the inner box is W/4 x H/4 at
  // (3W/8, 3H/8) = (450, 225) — and it lies wholly inside the outer box.
  const ApronRect central = ComputeApron(modes, 1200, 600, 600, 300);
  EXPECT_EQ(450, central.left);
  EXPECT_EQ(225, central.top);
  EXPECT_EQ(751, central.right);
  EXPECT_EQ(376, central.bottom);

  // Ship in the left third: the inner box is TWICE as wide (W/2 at index*W/4
  // = 0), and the outer box clips its left edge at 120.
  const ApronRect left = ComputeApron(modes, 1200, 600, 200, 300);
  EXPECT_EQ(120, left.left);
  EXPECT_EQ(601, left.right);
  EXPECT_GT(left.right - left.left, central.right - central.left);
}

// A ship already outside the outer box gets no inner box at all — the apron
// is the outer box, and the ship is outside it, so the map recentres.
TEST(MovingMapApron, AShipOutsideTheOuterBoxLeavesTheOuterBoxAlone) {
  CameraModes modes;
  const ApronRect r = ComputeApron(modes, 1200, 600, 10, 10);
  EXPECT_EQ(120, r.left);
  EXPECT_EQ(60, r.top);
  EXPECT_EQ(1081, r.right);
  EXPECT_EQ(541, r.bottom);
  EXPECT_FALSE(r.Contains(10, 10));
}

TEST(MovingMapApron, ContinuousAndAutoCenterOffBothGiveAnEmptyBox) {
  CameraModes continuous;
  continuous.continuous = true;
  EXPECT_TRUE(ComputeApron(continuous, 1200, 600, 600, 300).empty());
  CameraModes off;
  off.auto_center = false;
  EXPECT_TRUE(ComputeApron(off, 1200, 600, 600, 300).empty());
  // An empty box contains nothing, which is what makes continuous mode
  // recentre on every fix rather than never.
  EXPECT_FALSE(ApronRect{}.Contains(0, 0));
}

TEST(MovingMapApron, TwoBoxesThatMissIntersectToNothing) {
  const ApronRect a{0, 0, 10, 10};
  const ApronRect b{20, 20, 30, 30};
  EXPECT_TRUE(IntersectApron(a, b).empty());
  const ApronRect c{5, 5, 30, 30};
  const ApronRect hit = IntersectApron(a, c);
  EXPECT_EQ(5, hit.left);
  EXPECT_EQ(10, hit.right);
}

// --- The trigger ----------------------------------------------------------

TEST(MovingMapCameraTrigger, AShipInsideTheApronDoesNotMoveTheMap) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  camera.RecomputeApron(600, 600, 300, 300);
  ASSERT_FALSE(camera.apron().empty());

  // A point a few pixels off centre is still inside the apron.
  const CameraTarget t =
      camera.Update(proj, AtSurface(proj, 305.0, 295.0), 0.0, 0.0);
  EXPECT_FALSE(t.changed);
}

TEST(MovingMapCameraTrigger, LeavingTheApronRecentresAndPlacesTheShipAstern) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  camera.RecomputeApron(600, 600, 300, 300);
  // The central apron is (225, 225)..(376, 376) on a 600x600 window, so
  // x = 500 is outside it (and still inside the window).
  const GeoPoint ship = AtSurface(proj, 500.0, 300.0);
  const CameraTarget t = camera.Update(proj, ship, 90.0, 0.0);
  ASSERT_TRUE(t.changed);
  EXPECT_FALSE(t.world_escape);
  EXPECT_FALSE(t.rotation_changed);
  EXPECT_NEAR(200.0, t.delta_x, 1e-9);  // heading east -> ship to the west
  EXPECT_NEAR(0.0, t.delta_y, 1e-9);
  // The new centre really is 200 px east of the ship, in geography.
  double sx = 0.0;
  double sy = 0.0;
  ASSERT_TRUE(proj.GeoToSurface(t.center, &sx, &sy).ok());
  EXPECT_NEAR(700.0, sx, 0.5);
  EXPECT_NEAR(300.0, sy, 0.5);
  EXPECT_GT(t.center.lon, ship.lon);
}

// An unrecomputed apron is empty, so the very first fix recentres. That is
// the safe direction and it is the state a camera is in before its first
// draw.
TEST(MovingMapCameraTrigger, ACameraThatHasNeverDrawnRecentresImmediately) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  EXPECT_TRUE(camera.apron().empty());
  EXPECT_TRUE(camera.Update(proj, AtSurface(proj, 300.0, 300.0), 0.0, 0.0).changed);
}

TEST(MovingMapCameraTrigger, ForceAndContinuousBothRecentreFromInsideTheApron) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  camera.RecomputeApron(600, 600, 300, 300);
  const GeoPoint ship = AtSurface(proj, 300.0, 300.0);
  EXPECT_FALSE(camera.Update(proj, ship, 0.0, 0.0).changed);
  EXPECT_TRUE(camera.Update(proj, ship, 0.0, 0.0, 0.0, /*force=*/true).changed);

  CameraModes modes;
  modes.continuous = true;
  camera.SetModes(modes);
  camera.RecomputeApron(600, 600, 300, 300);
  EXPECT_TRUE(camera.apron().empty());
  EXPECT_TRUE(camera.Update(proj, ship, 0.0, 0.0).changed);
}

TEST(MovingMapCameraTrigger, AShipOutOfViewRecentresEvenIfTheApronSaysNothing) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  camera.RecomputeApron(600, 600, 300, 300);
  // Well off the left edge of the surface.
  EXPECT_TRUE(camera.Update(proj, AtSurface(proj, -400.0, 300.0), 0.0, 0.0).changed);
}

TEST(MovingMapCameraTrigger, AutoCenterOffIsTheMasterSwitch) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  CameraModes modes;
  modes.auto_center = false;
  camera.SetModes(modes);
  // Off the surface entirely, forced, and still nothing happens.
  EXPECT_FALSE(camera
                   .Update(proj, AtSurface(proj, -400.0, 300.0), 0.0, 0.0, 0.0,
                           /*force=*/true)
                   .changed);
  // And the apron is left as it was rather than being recomputed.
  camera.RecomputeApron(600, 600, 300, 300);
  EXPECT_TRUE(camera.apron().empty());
}

// The apron is built from where the ship was DRAWN and tested against where
// it has just moved to. Recomputing it from the new position would ask "may
// the ship be here?" of a box built around the ship being here — always yes,
// and the map would never move again.
TEST(MovingMapCameraTrigger, TheApronIsBuiltFromTheDrawnPositionNotTheNewOne) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  camera.RecomputeApron(600, 600, 300, 300);
  const ApronRect first = camera.apron();
  ASSERT_TRUE(first.Contains(300, 300));
  ASSERT_FALSE(first.Contains(500, 300));

  EXPECT_TRUE(camera.Update(proj, AtSurface(proj, 500.0, 300.0), 90.0, 0.0).changed);
  // Had the camera rebuilt the apron around (500, 300) first, that same fix
  // would have been inside it.
  const ApronRect around_new = ComputeApron(camera.modes(), 600, 600, 500, 300);
  EXPECT_TRUE(around_new.Contains(500, 300));
}

// --- Rotation -------------------------------------------------------------

TEST(MovingMapRotation, TrackUpTurnsTheMapUntilTheCourseIsUp) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  CameraModes modes;
  modes.auto_rotate = true;
  camera.SetModes(modes);

  const CameraTarget t =
      camera.Update(proj, AtSurface(proj, 300.0, 300.0), 90.0, 0.0);
  ASSERT_TRUE(t.changed);
  EXPECT_TRUE(t.rotation_changed);
  EXPECT_NEAR(270.0, t.rotation_deg, 1e-9);  // 0 - 90, wrapped
  // Discrete track-up uses the hard-coded (0, 1/3) anchor: heading east puts
  // the new centre a third of the window to the east of the ship.
  EXPECT_NEAR(0.333333 * 600.0, t.delta_x, 1e-6);
  EXPECT_NEAR(0.0, t.delta_y, 1e-6);
}

TEST(MovingMapRotation, ANearZeroRotationSnapsToZero) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  CameraModes modes;
  modes.auto_rotate = true;
  camera.SetModes(modes);

  // heading 359.95 -> rotation 0.05 -> snapped to exactly 0.
  const CameraTarget snapped =
      camera.Update(proj, AtSurface(proj, 300.0, 300.0), 359.95, 0.0);
  EXPECT_DOUBLE_EQ(0.0, snapped.rotation_deg);
  EXPECT_FALSE(snapped.rotation_changed);  // it was already 0
  // heading 359.8 is outside the snap and keeps its 0.2 degrees.
  const CameraTarget kept =
      camera.Update(proj, AtSurface(proj, 300.0, 300.0), 359.8, 0.0);
  EXPECT_NEAR(0.2, kept.rotation_deg, 1e-9);
  EXPECT_TRUE(kept.rotation_changed);
}

TEST(MovingMapRotation, ContinuousTrackUpHonoursTheSettableAnchor) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  CameraModes modes;
  modes.auto_rotate = true;
  modes.continuous = true;
  camera.SetModes(modes);
  camera.SetTrackUpAnchor(0.0, 0.1);

  const CameraTarget t =
      camera.Update(proj, AtSurface(proj, 300.0, 300.0), 0.0, 0.0);
  ASSERT_TRUE(t.changed);
  EXPECT_NEAR(-60.0, t.delta_y, 1e-6);  // 0.1 * 600
  // The discrete branch ignores the same setting, by the original's own
  // comment: its apron is built around the 1/3 anchor.
  modes.continuous = false;
  camera.SetModes(modes);
  const CameraTarget discrete =
      camera.Update(proj, AtSurface(proj, 300.0, 300.0), 0.0, 0.0);
  EXPECT_NEAR(-0.333333 * 600.0, discrete.delta_y, 1e-6);
}

// The convergence term is the one everybody forgets. It is zero on every
// projection the port has, so this pins that it is CARRIED — a caller with a
// conic projection gets it applied — rather than that it does something here.
TEST(MovingMapRotation, ConvergenceEntersThePointAngleLikeARotation) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  const GeoPoint ship = AtSurface(proj, 500.0, 300.0);

  const CameraTarget without = camera.Update(proj, ship, 45.0, 0.0, 0.0);
  const CameraTarget with = camera.Update(proj, ship, 0.0, 0.0, 45.0);
  ASSERT_TRUE(without.changed);
  ASSERT_TRUE(with.changed);
  EXPECT_NEAR(without.delta_x, with.delta_x, 1e-9);
  EXPECT_NEAR(without.delta_y, with.delta_y, 1e-9);
}

// QUIRK, PINNED, and this is also the test that shows how small it is. The
// 360 wrap is applied ONCE rather than in a loop. A heading in [0, 360) plus
// a rotation in [0, 360) cannot exceed 720, so for the two terms the port
// supplies one subtraction is always enough — 250 + 200 really does come out
// as 90 and is placed accordingly.
TEST(MovingMapRotation, AHeadingAndARotationNeverDefeatTheSingleWrap) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  const GeoPoint ship = AtSurface(proj, 500.0, 300.0);

  const CameraTarget t = camera.Update(proj, ship, 250.0, 200.0);
  ASSERT_TRUE(t.changed);
  double dx = 0.0;
  double dy = 0.0;
  DeltaXyDiscrete(600, 600, 90.0, &dx, &dy);
  EXPECT_NEAR(dx, t.delta_x, 1e-9);
  EXPECT_NEAR(dy, t.delta_y, 1e-9);
}

// Only the third term can defeat it, because nothing bounds it: a large
// convergence leaves `point_angle` above 360, where the placement's four
// branches are no longer the four quadrants. Preserved as FalconView has it,
// unreachable on every projection this port owns (all of which report a
// convergence of exactly zero), and pinned so that it is a recorded choice.
TEST(MovingMapRotation, AWildConvergenceCanLeaveThePointAngleOverThreeSixty) {
  MapProjection proj = MakeProj();
  MovingMapCamera camera;
  const GeoPoint ship = AtSurface(proj, 500.0, 300.0);

  // 350 + 350 + 100 = 800, wrapped once to 440.
  const CameraTarget over = camera.Update(proj, ship, 350.0, 350.0, 100.0);
  ASSERT_TRUE(over.changed);
  double dx = 0.0;
  double dy = 0.0;
  DeltaXyDiscrete(600, 600, 440.0, &dx, &dy);
  EXPECT_NEAR(dx, over.delta_x, 1e-9);
  EXPECT_NEAR(dy, over.delta_y, 1e-9);
  // Had it wrapped to 80 degrees the ship would have been placed in a
  // different box entirely.
  double wrapped_x = 0.0;
  double wrapped_y = 0.0;
  DeltaXyDiscrete(600, 600, 80.0, &wrapped_x, &wrapped_y);
  EXPECT_TRUE(wrapped_x != dx || wrapped_y != dy);
  // And whatever branch it lands in, the answer is still one of the nine
  // boxes rather than an undefined cast — that is the port's clamp.
  EXPECT_LE(std::fabs(over.delta_x), 200.0 + 1e-9);
  EXPECT_LE(std::fabs(over.delta_y), 200.0 + 1e-9);
}

// --- The world-scale escape ----------------------------------------------

TEST(MovingMapCameraTrigger, WorldScaleJustCentresOnTheShip) {
  MapProjection proj = MakeProj(600, 600, 80000000.0);
  MovingMapCamera camera;
  camera.RecomputeApron(600, 600, 300, 300);
  const GeoPoint ship = AtSurface(proj, 500.0, 300.0);

  const CameraTarget t = camera.Update(proj, ship, 90.0, 0.0);
  ASSERT_TRUE(t.changed);
  EXPECT_TRUE(t.world_escape);
  EXPECT_DOUBLE_EQ(ship.lat, t.center.lat);
  EXPECT_DOUBLE_EQ(ship.lon, t.center.lon);
  EXPECT_DOUBLE_EQ(0.0, t.delta_x);
  EXPECT_DOUBLE_EQ(0.0, t.delta_y);

  // One step in from the threshold and the 3x3 grid is back.
  MapProjection closer = MakeProj(600, 600, 5000000.0);
  const CameraTarget placed =
      camera.Update(closer, AtSurface(closer, 500.0, 300.0), 90.0, 0.0);
  ASSERT_TRUE(placed.changed);
  EXPECT_FALSE(placed.world_escape);
  EXPECT_NEAR(200.0, placed.delta_x, 1e-9);
}

}  // namespace
