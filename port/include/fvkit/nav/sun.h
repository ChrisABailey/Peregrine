// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file fvkit/nav/sun.h
 * Sunrise and sunset, computed offline from a position and a time.
 *
 * NOAA's solar-calculator method (Meeus, *Astronomical Algorithms*, low
 * precision), refined once at each event: within about a minute of the US
 * Naval Observatory between the polar circles. Rise and set are the upper limb
 * on a sea-level horizon with standard refraction (zenith 90.833 degrees).
 * Times are Unix seconds (UTC); the shell formats them.
 */

#ifndef FVKIT_NAV_SUN_H_
#define FVKIT_NAV_SUN_H_

#include <vector>

namespace fv {
namespace nav {

/// One sunrise or sunset.
struct SunEvent {
  double time_s = 0.0;
  bool rise = false;
};

/// The sunrises and sunsets at (`lat_deg`, `lon_deg`) with t0 <= time <= t1,
/// in time order. Empty when t1 < t0. A day on which the sun neither rises nor
/// sets (polar day or night) contributes nothing.
std::vector<SunEvent> SunEvents(double lat_deg, double lon_deg, double t0,
                                double t1);

}  // namespace nav
}  // namespace fv

#endif  // FVKIT_NAV_SUN_H_
