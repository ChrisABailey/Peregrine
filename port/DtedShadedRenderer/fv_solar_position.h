// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_solar_position.h — standard NOAA solar-position calculation.
//
// FalconView's time-of-day DTED shading called SLAC (an ISlac COM component,
// GOV_RELEASE-only) to turn a date + lat/lon into a solar azimuth/altitude,
// then convert_to_cartesian() built a light vector from those. SLAC is not in
// the open tree, so this is the license-clean replacement: the equations from
// the NOAA Solar Calculator (https://gml.noaa.gov/grad/solcalc/), which agree
// with SLAC's astronomy to well under a degree — far finer than the shading
// needs. No atmospheric-refraction correction is applied (it only matters
// within ~1 deg of the horizon and does not affect the light direction
// meaningfully); documented so the omission is deliberate.

#pragma once

namespace fv {

struct SolarPosition {
  double azimuth_deg;    // clockwise from true north, [0, 360)
  double elevation_deg;  // above the horizon (negative = below)
};

// year/month/day = Gregorian calendar date; hours_utc = UTC time of day as
// fractional hours [0, 24); lat/lon in degrees (east and north positive).
SolarPosition SolarAzimuthElevation(int year, int month, int day,
                                    double hours_utc, double lat_deg,
                                    double lon_deg);

}  // namespace fv
