// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/sun.h"

#include <algorithm>
#include <cmath>

namespace fv {
namespace nav {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kDay = 86400.0;
/// Upper limb on the horizon with standard refraction.
constexpr double kZenithDeg = 90.833;

/// The sun's declination (radians) and the equation of time (minutes) at `t`.
struct Solar {
  double declination = 0.0;
  double equation_of_time_min = 0.0;
};

Solar SolarAt(double t) {
  const double jd = t / kDay + 2440587.5;
  const double c = (jd - 2451545.0) / 36525.0;  // Julian centuries from J2000
  const double l0 =
      std::fmod(280.46646 + c * (36000.76983 + c * 0.0003032), 360.0);
  const double m = 357.52911 + c * (35999.05029 - 0.0001537 * c);
  const double e = 0.016708634 - c * (0.000042037 + 0.0000001267 * c);
  const double mr = m * kDeg;
  const double center = std::sin(mr) * (1.914602 - c * (0.004817 + 0.000014 * c)) +
                        std::sin(2 * mr) * (0.019993 - 0.000101 * c) +
                        std::sin(3 * mr) * 0.000289;
  const double omega = (125.04 - 1934.136 * c) * kDeg;
  const double lambda =
      (l0 + center - 0.00569 - 0.00478 * std::sin(omega)) * kDeg;
  const double eps0 =
      23.0 + (26.0 + (21.448 - c * (46.815 + c * (0.00059 - c * 0.001813))) /
                         60.0) / 60.0;
  const double eps = (eps0 + 0.00256 * std::cos(omega)) * kDeg;

  Solar s;
  s.declination = std::asin(std::sin(eps) * std::sin(lambda));
  const double y = std::pow(std::tan(eps / 2), 2);
  const double l0r = l0 * kDeg;
  const double eot = y * std::sin(2 * l0r) - 2 * e * std::sin(mr) +
                     4 * e * y * std::sin(mr) * std::cos(2 * l0r) -
                     0.5 * y * y * std::sin(4 * l0r) -
                     1.25 * e * e * std::sin(2 * mr);
  s.equation_of_time_min = 4.0 * eot / kDeg;
  return s;
}

/// The rise or set of the solar day whose UTC date starts at `day_start`,
/// evaluated with the sun's position at `at`. False when the sun does not
/// cross the horizon that day.
bool EventFrom(double lat, double lon, double day_start, bool rise, double at,
               double* time) {
  const Solar s = SolarAt(at);
  const double latr = lat * kDeg;
  const double cos_ha = std::cos(kZenithDeg * kDeg) /
                            (std::cos(latr) * std::cos(s.declination)) -
                        std::tan(latr) * std::tan(s.declination);
  if (cos_ha < -1.0 || cos_ha > 1.0) return false;
  const double ha_deg = std::acos(cos_ha) / kDeg;
  const double noon_min = 720.0 - 4.0 * lon - s.equation_of_time_min;
  const double event_min = noon_min + (rise ? -4.0 : 4.0) * ha_deg;
  *time = day_start + event_min * 60.0;
  return true;
}

}  // namespace

std::vector<SunEvent> SunEvents(double lat_deg, double lon_deg, double t0,
                                double t1) {
  std::vector<SunEvent> out;
  if (t1 < t0) return out;
  // A solar day's events can fall on the UTC date either side of it, so the
  // scan starts a day early and ends a day late, then filters.
  const double first = std::floor(t0 / kDay) - 1;
  const double last = std::floor(t1 / kDay) + 1;
  for (double d = first; d <= last; d += 1) {
    const double day_start = d * kDay;
    const double noon = day_start + (720.0 - 4.0 * lon_deg) * 60.0;
    for (bool rise : {true, false}) {
      double estimate = 0.0;
      double time = 0.0;
      // The second pass re-evaluates the sun at the first estimate.
      if (!EventFrom(lat_deg, lon_deg, day_start, rise, noon, &estimate) ||
          !EventFrom(lat_deg, lon_deg, day_start, rise, estimate, &time)) {
        continue;
      }
      if (time >= t0 && time <= t1) out.push_back({time, rise});
    }
  }
  std::sort(out.begin(), out.end(),
            [](const SunEvent& a, const SunEvent& b) { return a.time_s < b.time_s; });
  return out;
}

}  // namespace nav
}  // namespace fv
