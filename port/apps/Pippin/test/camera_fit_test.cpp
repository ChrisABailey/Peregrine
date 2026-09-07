// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// P7: the zoom-out rule, on the mac.
//
// The rule is one sentence of requirement and one line of arithmetic, and the
// reason it is tested here rather than looked at on a phone is that its
// failure mode is invisible: a fit that is 20% too tight puts the route's
// start just off the edge of a screen nobody is measuring, on a bike.

#include "PPCameraFit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

using fv::GeoPoint;
using fv::GeoRect;
using pippin::CameraFitSurface;
using pippin::FitDistanceMeters;
using pippin::ScaleToFitBounds;
using pippin::ScaleToShow;

// An iPhone 17 Pro at the numbers the P3 probe measured with: 1206x2622
// pixels at 3x, so a rendered pixel is one third of a point.
CameraFitSurface Phone() {
  CameraFitSurface s;
  s.pixel_width = 1206;
  s.pixel_height = 2622;
  s.mm_per_pixel = 25.4 / (163.0 * 3.0);
  return s;
}

// Two real positions on Kiawah, 2.16 km apart by road and rather less as the
// tern flies: the P1 link check's pair.
const GeoPoint kRuddyTurnstone{32.6044007, -80.1083007};
const GeoPoint kBeachClub{32.59297596527702, -80.11767417454368};

// The ground between the ship and the nearest edge of the screen at a scale —
// which is what a route start has to fit inside. MM2 draws a track-up ship at
// (W/2, 5H/6), so that is half the width or a sixth of the height, whichever
// is less.
double GroundClearanceAroundShip(const CameraFitSurface& s, double denominator) {
  const double half_width = 0.5 * s.pixel_width;
  const double behind = (0.5 - pippin::kTrackUpAheadFraction) * s.pixel_height;
  return (half_width < behind ? half_width : behind) * s.mm_per_pixel / 1000.0 *
         denominator;
}

TEST(CameraFit, TheDistanceIsInTheSameMetreTheRestOfTheAppUses) {
  // Straight-line distance between the link check's pair. The road between
  // them is 2.16 km; the tangent-plane line is shorter, and this pins the
  // number so a change of Earth model shows up here rather than as a fit that
  // is quietly 0.5% off.
  const double d = FitDistanceMeters(kRuddyTurnstone, kBeachClub);
  EXPECT_NEAR(d, 1544.28, 0.5);
  // NOT symmetric, and by a measurable amount: the tangent plane is taken at
  // the FIRST argument, so the meridians are scaled by that end's cosine and
  // the two answers differ by 6 cm over this 1.5 km. Pinned rather than
  // hidden — it is four parts in a hundred thousand, which is nothing at all
  // to a choice of scale and would be worth caring about in a geodesy.
  EXPECT_NEAR(FitDistanceMeters(kBeachClub, kRuddyTurnstone), d, 0.1);
}

TEST(CameraFit, ARouteStartAlreadyInViewLeavesTheScaleAlone) {
  // 1:20,000 puts about 450 m between this phone's ship and its nearest edge,
  // and the start is 30 m away.
  const GeoPoint near{kRuddyTurnstone.lat + 0.00027, kRuddyTurnstone.lon};
  EXPECT_DOUBLE_EQ(ScaleToShow(kRuddyTurnstone, near, Phone(), 20.0e3), 20.0e3);
}

TEST(CameraFit, ARouteStartOutOfViewWidensUntilItIsIn) {
  const CameraFitSurface phone = Phone();
  const double current = 5.0e3;  // ~350 m across: the start is nowhere in it
  const double widened = ScaleToShow(kRuddyTurnstone, kBeachClub, phone, current);
  ASSERT_GT(widened, current);

  // The property, stated as the requirement states it: the start is in view
  // — the ground between the ship and the nearest edge covers the distance to
  // it, whichever way the chart is turned.
  const double clearance = GroundClearanceAroundShip(phone, widened);
  const double radius = FitDistanceMeters(kRuddyTurnstone, kBeachClub);
  EXPECT_GE(clearance, radius);
  // And not further out than it has to be: the edge margin is the whole of
  // the slack, so the fit is exact at 1.1.
  EXPECT_NEAR(clearance, radius * pippin::kFitEdgeMargin, 1.0);
}

TEST(CameraFit, TheRouteStartFitsBEHINDTheShip) {
  // Decision 3 as an assertion rather than a comment, and the case the first
  // cut of this file got wrong: MM2 puts a track-up ship at 5H/6, so a start
  // the rider has already passed has ONE SIXTH of the height to fit into —
  // not half of it, and on a portrait phone not half the width either.
  const CameraFitSurface phone = Phone();
  const double widened = ScaleToShow(kRuddyTurnstone, kBeachClub, phone, 5.0e3);
  const double radius = FitDistanceMeters(kRuddyTurnstone, kBeachClub);

  const double behind_m = phone.pixel_height / 6.0 * phone.mm_per_pixel / 1000.0 *
                          widened;
  EXPECT_GE(behind_m, radius);
  // And it is the SIXTH that binds on a phone held upright — this is what
  // makes the answer 40% wider than a naive half-the-smaller-side fit.
  const double half_width_m =
      phone.pixel_width / 2.0 * phone.mm_per_pixel / 1000.0 * widened;
  EXPECT_LT(behind_m, half_width_m);
}

TEST(CameraFit, ItNeverNarrows) {
  // Zoomed out past the whole island already: entering GPS mode must not pull
  // the map in, because the rider chose that view.
  const double current = 400.0e3;
  EXPECT_DOUBLE_EQ(ScaleToShow(kRuddyTurnstone, kBeachClub, Phone(), current),
                   current);
}

TEST(CameraFit, TheAnswerIsRotationInvariant) {
  // The disc rather than the box (decision 2): a start due north and a start
  // due east at the same distance get the same scale, so the rule does not
  // change its mind when the chart turns.
  const double r_deg = 0.01;
  const GeoPoint north{kRuddyTurnstone.lat + r_deg, kRuddyTurnstone.lon};
  const double cos_lat = std::cos(kRuddyTurnstone.lat * M_PI / 180.0);
  const GeoPoint east{kRuddyTurnstone.lat, kRuddyTurnstone.lon + r_deg / cos_lat};
  EXPECT_NEAR(ScaleToShow(kRuddyTurnstone, north, Phone(), 5.0e3),
              ScaleToShow(kRuddyTurnstone, east, Phone(), 5.0e3), 1.0);
}

TEST(CameraFit, NothingMeasurableLeavesTheScaleAlone) {
  const double current = 50.0e3;
  CameraFitSurface unmeasured;  // no surface yet — the first frame
  EXPECT_DOUBLE_EQ(ScaleToShow(kRuddyTurnstone, kBeachClub, unmeasured, current),
                   current);

  CameraFitSurface no_pitch = Phone();
  no_pitch.mm_per_pixel = 0.0;
  EXPECT_DOUBLE_EQ(ScaleToShow(kRuddyTurnstone, kBeachClub, no_pitch, current),
                   current);

  // The ship and the start in the same place: a route whose start is under
  // the rider is in view by definition.
  EXPECT_DOUBLE_EQ(
      ScaleToShow(kRuddyTurnstone, kRuddyTurnstone, Phone(), current), current);

  // And a NaN anywhere is answered with the scale the map already has rather
  // than with a NaN, because this number goes straight into a projection.
  const GeoPoint nowhere{std::nan(""), std::nan("")};
  EXPECT_DOUBLE_EQ(ScaleToShow(kRuddyTurnstone, nowhere, Phone(), current),
                   current);
  EXPECT_DOUBLE_EQ(ScaleToShow(nowhere, kBeachClub, Phone(), current), current);
}

// --- Framing a search result ----------------------------------------------

// A different rule rather than the same one with a flag: this is the answer
// to "show me that", so it may narrow as well as widen, and its only hard
// limit is the floor that stops a cul-de-sac filling the screen.

TEST(CameraFit, FramingAPointLeavesTheScaleAlone) {
  // A point's honest bounds is degenerate and no scale fits a dimensionless
  // thing, so the rider keeps the scale they were reading and the map
  // centres.
  const double current = 12.0e3;
  const GeoRect point{kRuddyTurnstone, kRuddyTurnstone};
  EXPECT_DOUBLE_EQ(
      ScaleToFitBounds(point, Phone(), current, 2.5e3, 0.2), current);
}

TEST(CameraFit, FramingAStreetFromTheWholeIslandZoomsIn) {
  // The difference from `ScaleToShow` in one assertion. A rider looking at
  // all of Kiawah at 1:120,000 who searched for a 600 m street asked to be
  // taken there; a rule that only widened would centre a street they still
  // could not see.
  const GeoRect street{{32.6020, -80.1110}, {32.6068, -80.1056}};
  const double framed = ScaleToFitBounds(street, Phone(), 120.0e3, 2.5e3, 0.0);
  EXPECT_LT(framed, 120.0e3);
  // And it really fits: half the diagonal has to land inside the narrower
  // half of the screen, with the edge margin allowed for.
  const double radius_m =
      0.5 * FitDistanceMeters(street.ll, street.ur);
  const CameraFitSurface s = Phone();
  const double clearance_px = std::min(0.5 * s.pixel_width, 0.5 * s.pixel_height);
  const double clearance_m = clearance_px * s.mm_per_pixel / 1000.0 * framed;
  EXPECT_GE(clearance_m, radius_m);
}

TEST(CameraFit, FramingALongRoadFromCloseInZoomsOut) {
  // Kiawah Island Parkway end to end, from a rider's own scale.
  const GeoRect parkway{{32.5900, -80.1700}, {32.6200, -80.0400}};
  EXPECT_GT(ScaleToFitBounds(parkway, Phone(), 5.0e3, 2.5e3, 0.0), 5.0e3);
}

TEST(CameraFit, TheFloorStopsACulDeSacFillingTheScreen) {
  // Forty metres fitted to a phone is arithmetically about 1:300 — a street
  // with no context around it. `search.frame_min_scale` is the scale a rider
  // reads a street at, and the fit never goes in past it.
  const GeoRect cul_de_sac{{32.60440, -80.10830}, {32.60476, -80.10830}};
  EXPECT_DOUBLE_EQ(ScaleToFitBounds(cul_de_sac, Phone(), 50.0e3, 2.5e3, 0.0),
                   2.5e3);
  // With no floor asked for, the arithmetic stands on its own — which is what
  // a `frame_min_scale` of 0 in the pack means.
  EXPECT_LT(ScaleToFitBounds(cul_de_sac, Phone(), 50.0e3, 0.0, 0.0), 2.5e3);
}

TEST(CameraFit, TheSheetsBiasCostsTheFitHeightAndNothingElse) {
  // Pushing the result up the screen so a sheet can sit over the bottom half
  // takes screen away, so the fit is never TIGHTER for it. On a portrait
  // phone half the width is the narrow direction anyway, so a modest bias
  // costs nothing at all — which is why the bias is allowed to be generous.
  const GeoRect street{{32.6020, -80.1110}, {32.6068, -80.1056}};
  const double straight = ScaleToFitBounds(street, Phone(), 50.0e3, 0.0, 0.0);
  const double biased = ScaleToFitBounds(street, Phone(), 50.0e3, 0.0, 0.2);
  EXPECT_GE(biased, straight);
  // A bias big enough to matter does move it, and the clamp keeps a nonsense
  // value (0.5 would be zero height) from dividing by nothing.
  EXPECT_GT(ScaleToFitBounds(street, Phone(), 50.0e3, 0.0, 0.49), straight);
  EXPECT_TRUE(std::isfinite(ScaleToFitBounds(street, Phone(), 50.0e3, 0.0, 5.0)));
}

TEST(CameraFit, NothingMeasurableLeavesTheFramingAlone) {
  const double current = 50.0e3;
  const GeoRect street{{32.6020, -80.1110}, {32.6068, -80.1056}};
  CameraFitSurface unmeasured;
  EXPECT_DOUBLE_EQ(ScaleToFitBounds(street, unmeasured, current, 2.5e3, 0.0),
                   current);
  const GeoRect nowhere{{std::nan(""), std::nan("")}, {0.0, 0.0}};
  EXPECT_DOUBLE_EQ(ScaleToFitBounds(nowhere, Phone(), current, 2.5e3, 0.0),
                   current);
}

}  // namespace
