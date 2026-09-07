// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPLocationFix.h — a CLLocation, as a `fv::PositionFix`.
//
// No CoreLocation in this header, on purpose. `CLLocation` is a dozen doubles
// and a date, and the interesting half of the conversion is not the reading
// but which of those doubles mean "I do not know" — which CoreLocation says
// with negative numbers in five places. That is a truth table, and a truth
// table belongs under ctest.
//
// So `PPLocationSample` is the fields as plain numbers,
// `PPFixFromLocationSample` is the whole of the logic, and
// `PPLocationSource.mm` is the ten lines that read the ObjC object into the
// struct. The mac test bed exercises this file; the phone gets only the part
// that cannot be tested anywhere else.
//
// The rule implemented is MM1's first: every field of a `PositionFix` carries
// its own validity, and the -1000.0 sentinels of the Windows original do not
// port. CoreLocation has sentinels of its own, and this is where they stop:
//
//   horizontalAccuracy < 0   the POSITION is invalid (Apple documents exactly
//                            this, and it is the only way a CLLocation says a
//                            coordinate is a guess). lat/lon are then not read.
//   verticalAccuracy   < 0   the ALTITUDE is invalid.
//   course             < 0   the COURSE is invalid.
//   speed              < 0   the SPEED is invalid.
//
// `courseAccuracy` and `speedAccuracy` (iOS 13.4+) are read but invalidate
// nothing on their own. They are quality numbers `PositionFix` has nowhere to
// put, and treating a negative one as invalidating an otherwise-valid course
// would be stricter than Apple's contract — biting where a heading is most
// wanted, a simulator replaying a GPX, where the course is derived and its
// accuracy often unreported. A dropped course is not lost either:
// `HeadingResolver` derives one from successive positions.
//
// Two conversions worth stating.
//
// 1. Altitude is already MSL. `CLLocation.altitude` is documented as height
//    above mean sea level (`ellipsoidalAltitude` is the WGS84 one) and
//    `PositionFix::altitude_msl_m` is MSL by contract, so this is a copy and
//    not a datum shift — the one place in the port where the two agree
//    without a geoid lookup.
//
// 2. Horizontal accuracy becomes an HDOP. `PositionFix` carries hdop because
//    MM5's snapper sizes its search radius off it: `radius = hdop *
//    hdop_scale`, floored at 15 m, capped at 60 m, with `hdop_scale` 5.0 m
//    per unit. CoreLocation reports metres directly, which is what the
//    snapper is reconstructing, so dividing by that same 5.0 makes the search
//    radius come out as the accuracy the receiver reported.
//    `kPPHdopMetresPerUnit` is the coupling, named here so changing
//    `RoadSnapSettings::hdop_scale` and forgetting this line is a grep.

#ifndef PIPPIN_PPLOCATIONFIX_H_
#define PIPPIN_PPLOCATIONFIX_H_

#include "fvkit/nav/position.h"

// The metres of error one unit of HDOP is worth: `RoadSnapSettings::
// hdop_scale`'s default, repeated because this is the file that must agree
// with it. See note 2 above.
inline constexpr double kPPHdopMetresPerUnit = 5.0;

// A `CLLocation`'s fields as plain numbers. Every `*_accuracy` is in metres,
// degrees or m/s, and negative means the field beside it is invalid. The
// defaults are therefore "nothing is known".
struct PPLocationSample {
  double latitude = 0.0;
  double longitude = 0.0;
  double horizontal_accuracy_m = -1.0;  // < 0: the coordinate is invalid

  double altitude_m = 0.0;              // MSL, per CLLocation
  double vertical_accuracy_m = -1.0;    // < 0: the altitude is invalid

  double course_deg = -1.0;             // true, clockwise; < 0: invalid
  double course_accuracy_deg = -1.0;    // carried, never invalidating

  double speed_mps = -1.0;              // < 0: invalid
  double speed_accuracy_mps = -1.0;     // carried, never invalidating

  double timestamp_s = 0.0;             // epoch seconds, UTC
  bool has_timestamp = false;
};

// The conversion, and every sentinel in it. A sample with an invalid position
// still comes back as a fix, with `has_position` false and whatever else it
// knew, because a CLLocation with a bad coordinate can still carry a real
// altitude. The caller decides that a fix with no position is not worth
// queueing.
inline fv::PositionFix PPFixFromLocationSample(const PPLocationSample& s) {
  fv::PositionFix fix;

  if (s.horizontal_accuracy_m >= 0.0) {
    fix.SetPosition(s.latitude, s.longitude);
    // Metres back into the unitless multiplier the snapper expects.
    fix.hdop = s.horizontal_accuracy_m / kPPHdopMetresPerUnit;
    fix.has_hdop = true;
  }

  if (s.vertical_accuracy_m >= 0.0) {
    fix.altitude_msl_m = s.altitude_m;  // already MSL
    fix.has_altitude = true;
  }

  if (s.course_deg >= 0.0) {
    // Left exactly as reported, per position.h: a heading a source reports is
    // never normalized behind its back. CoreLocation's range is [0, 360).
    fix.true_heading_deg = s.course_deg;
    fix.has_true_heading = true;
  }

  if (s.speed_mps >= 0.0) {
    fix.speed_mps = s.speed_mps;
    fix.has_speed = true;
  }

  if (s.has_timestamp) {
    fix.time_s = s.timestamp_s;
    fix.has_time = true;
  }

  // No magnetic heading and no satellite count: CLLocation reports neither.
  // CLHeading has a magnetic one, but that is the direction the device is
  // pointing rather than a course over ground, which is not what the moving
  // map wants.
  return fix;
}

#endif  // PIPPIN_PPLOCATIONFIX_H_
