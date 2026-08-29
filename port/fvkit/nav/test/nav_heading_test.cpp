// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Heading resolution (nav plan MM1), ported from gps_draw.cpp's
// get_current_heading(). The eight-compass-point test below is the one that
// pins this port's single deliberate deviation from the original — see the
// note above it and the header.

#include "fvkit/nav/heading.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

using fv::GeoPoint;
using fv::HeadingResolver;
using fv::PositionFix;
using fv::ResolvedHeading;
using fv::ScreenBearingDeg;

PositionFix At(double lat, double lon) {
  PositionFix fix;
  fix.SetPosition(lat, lon);
  return fix;
}

// The original formula, verbatim, as the oracle this port deliberately does
// NOT reproduce. Kept in the test rather than the code so that the deviation
// is visible and asserted rather than described.
double FalconViewHeading(double delta_lon_px, double delta_lat_px) {
  const double kRadToDeg = 180.0 / 3.14159265358979323846;
  double angle = std::atan2(delta_lon_px, delta_lat_px) * kRadToDeg;
  if (delta_lat_px < 0.0) {
    angle += 180.0;
  } else if (delta_lon_px < 0.0) {
    angle += 360.0;
  }
  return angle;
}

struct CompassCase {
  const char* name;
  double d_lat;
  double d_lon;
  double expected;
};

const CompassCase kCompass[] = {
    {"north", 1.0, 0.0, 0.0},      {"northeast", 1.0, 1.0, 45.0},
    {"east", 0.0, 1.0, 90.0},      {"southeast", -1.0, 1.0, 135.0},
    {"south", -1.0, 0.0, 180.0},   {"southwest", -1.0, -1.0, 225.0},
    {"west", 0.0, -1.0, 270.0},    {"northwest", 1.0, -1.0, 315.0},
};

TEST(ScreenBearing, IsClockwiseFromNorthAtEveryCompassPoint) {
  for (const CompassCase& c : kCompass) {
    double bearing = -1.0;
    ASSERT_TRUE(ScreenBearingDeg(GeoPoint{0.0, 0.0}, GeoPoint{c.d_lat, c.d_lon}, 1.0, 1.0,
                                 &bearing))
        << c.name;
    EXPECT_NEAR(c.expected, bearing, 1e-9) << c.name;
  }
}

// THE DEVIATION, PINNED. The original applies an `atan`-shaped quadrant
// fix-up to `atan2`, which has already resolved the quadrant. The two agree
// exactly on the northern half and on due east/west; where the ship is going
// SOUTH the correction is applied on top of a quadrant that was already
// right, and the answer comes out 180 degrees OPPOSED — a ship tracking
// southeast is drawn tracking northwest. This test asserts both halves of
// that: that the original agrees with us where it is right, and that it is
// exactly reversed where this port stopped following it.
TEST(ScreenBearing, DivergesFromTheOriginalOnTheSouthernHalfOnly) {
  for (const CompassCase& c : kCompass) {
    const double original = fv::NormalizeHeadingDeg(FalconViewHeading(c.d_lon, c.d_lat));
    if (c.d_lat < 0.0) {
      EXPECT_NEAR(fv::NormalizeHeadingDeg(c.expected + 180.0), original, 1e-9)
          << c.name << ": the original points a southbound ship backwards";
    } else {
      EXPECT_NEAR(c.expected, original, 1e-9) << c.name;
    }
  }
}

// With no map to ask, longitude is compressed by cos(lat): two degrees of
// longitude at 60 N are one degree of latitude on the ground, so this is a
// bearing of 045 and not of 063.
TEST(ScreenBearing, CompressesLongitudeByCosLatWhenNoDegPerPixelIsGiven) {
  double bearing = 0.0;
  ASSERT_TRUE(ScreenBearingDeg(GeoPoint{60.0, 0.0}, GeoPoint{61.0, 2.0}, 0.0, 0.0, &bearing));
  EXPECT_NEAR(45.0, bearing, 0.6);  // cos of the midpoint latitude, not of 60
}

// The map's own scaling is what makes the arrow agree with the line the user
// can see: the same geography under a different degrees-per-pixel is a
// different angle ON SCREEN, and the ownship symbol follows the screen.
TEST(ScreenBearing, FollowsTheMapsDegreesPerPixel) {
  double square = 0.0;
  ASSERT_TRUE(ScreenBearingDeg(GeoPoint{0.0, 0.0}, GeoPoint{1.0, 1.0}, 1.0, 1.0, &square));
  EXPECT_NEAR(45.0, square, 1e-9);

  double stretched = 0.0;
  ASSERT_TRUE(ScreenBearingDeg(GeoPoint{0.0, 0.0}, GeoPoint{1.0, 1.0}, 1.0, 2.0, &stretched));
  EXPECT_NEAR(26.565051177, stretched, 1e-6);  // atan(0.5)
}

TEST(ScreenBearing, TakesTheShortWayAcrossTheAntimeridian) {
  double bearing = 0.0;
  ASSERT_TRUE(ScreenBearingDeg(GeoPoint{0.0, 179.5}, GeoPoint{0.0, -179.5}, 1.0, 1.0, &bearing));
  EXPECT_NEAR(90.0, bearing, 1e-9);  // due east, not due west
}

TEST(ScreenBearing, HasNoAnswerForTwoIdenticalPoints) {
  double bearing = 42.0;
  EXPECT_FALSE(ScreenBearingDeg(GeoPoint{1.0, 2.0}, GeoPoint{1.0, 2.0}, 1.0, 1.0, &bearing));
}

// ---------------------------------------------------------------------------
// HeadingResolver
// ---------------------------------------------------------------------------

TEST(HeadingResolver, PrefersTheHeadingTheFixReports) {
  HeadingResolver resolver;
  resolver.SetDegPerPixel(1.0, 1.0);

  PositionFix fix = At(0.0, 0.0);
  fix.true_heading_deg = 275.0;
  fix.has_true_heading = true;

  const ResolvedHeading h = resolver.Update(fix);
  EXPECT_TRUE(h.known);
  EXPECT_TRUE(h.reported);
  EXPECT_DOUBLE_EQ(275.0, h.degrees);
}

TEST(HeadingResolver, DerivesFromMovementWhenTheFixCarriesNone) {
  HeadingResolver resolver;
  resolver.SetDegPerPixel(1.0, 1.0);

  // The first fix has nothing to derive from: north by default, and it says
  // it does not know.
  const ResolvedHeading first = resolver.Update(At(0.0, 0.0));
  EXPECT_FALSE(first.known);
  EXPECT_DOUBLE_EQ(0.0, first.degrees);

  const ResolvedHeading east = resolver.Update(At(0.0, 1.0));
  EXPECT_TRUE(east.known);
  EXPECT_FALSE(east.reported);
  EXPECT_NEAR(90.0, east.degrees, 1e-9);

  // And southbound, which is the half the original gets wrong.
  const ResolvedHeading south = resolver.Update(At(-1.0, 1.0));
  EXPECT_NEAR(180.0, south.degrees, 1e-9);
}

// A ship at a stop light points the way it was going. The original walks its
// whole icon list back to the last DISTINCT point for exactly this reason;
// this port bounds that walk, and holds the last answer when the walk finds
// nothing.
TEST(HeadingResolver, HoldsItsHeadingWhileTheShipIsStationary) {
  HeadingResolver resolver;
  resolver.SetDegPerPixel(1.0, 1.0);
  resolver.Update(At(0.0, 0.0));
  ASSERT_NEAR(90.0, resolver.Update(At(0.0, 1.0)).degrees, 1e-9);

  for (int i = 0; i < 5; ++i) {
    const ResolvedHeading held = resolver.Update(At(0.0, 1.0));
    EXPECT_TRUE(held.known);
    EXPECT_NEAR(90.0, held.degrees, 1e-9);
  }
  // Repeats of one position are not distinct points, so they do not grow the
  // history either.
  EXPECT_EQ(2u, resolver.history_size());
}

TEST(HeadingResolver, KeepsTheLastHeadingThroughAFixWithNoPosition) {
  HeadingResolver resolver;
  resolver.SetDegPerPixel(1.0, 1.0);
  resolver.Update(At(0.0, 0.0));
  ASSERT_NEAR(90.0, resolver.Update(At(0.0, 1.0)).degrees, 1e-9);

  PositionFix empty;  // a dropped sentence
  EXPECT_NEAR(90.0, resolver.Update(empty).degrees, 1e-9);
}

// The history is the whole of the "trail" the moving map keeps, and it is
// bounded: ten minutes at a standstill must not let the ship claim the
// heading it had ten minutes ago.
TEST(HeadingResolver, BoundsTheHistoryItKeeps) {
  HeadingResolver resolver(3);
  for (int i = 0; i < 10; ++i) {
    resolver.Update(At(0.0, static_cast<double>(i)));
  }
  EXPECT_EQ(3u, resolver.history_size());

  resolver.Reset();
  EXPECT_EQ(0u, resolver.history_size());
  EXPECT_FALSE(resolver.current().known);
}

}  // namespace
