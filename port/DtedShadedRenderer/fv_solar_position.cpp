// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// NOAA solar-position implementation — see fv_solar_position.h.

#include "fv_solar_position.h"

#include <cmath>

namespace fv {

namespace {

constexpr double kPi = 3.14159265358979323846;
inline double Rad(double d) { return d * kPi / 180.0; }
inline double Deg(double r) { return r * 180.0 / kPi; }

// Julian day at 00:00 UTC of a Gregorian calendar date (Fliegel-Van Flandern),
// returned as a whole-day value; the fractional day is added by the caller.
double JulianDay(int year, int month, int day) {
  if (month <= 2) {
    year -= 1;
    month += 12;
  }
  const int a = year / 100;
  const int b = 2 - a + a / 4;
  return std::floor(365.25 * (year + 4716)) +
         std::floor(30.6001 * (month + 1)) + day + b - 1524.5;
}

}  // namespace

// Equations from the NOAA Solar Calculator spreadsheet. Angles are worked in
// degrees except where a trig call needs radians.
SolarPosition SolarAzimuthElevation(int year, int month, int day,
                                    double hours_utc, double lat_deg,
                                    double lon_deg) {
  const double jd = JulianDay(year, month, day) + hours_utc / 24.0;
  const double t = (jd - 2451545.0) / 36525.0;  // Julian centuries since J2000

  const double geom_mean_long =
      std::fmod(280.46646 + t * (36000.76983 + t * 0.0003032), 360.0);
  const double geom_mean_anom = 357.52911 + t * (35999.05029 - 0.0001537 * t);
  const double eccent = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);

  const double m = geom_mean_anom;
  const double sun_eq_ctr = std::sin(Rad(m)) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
                            std::sin(Rad(2 * m)) * (0.019993 - 0.000101 * t) +
                            std::sin(Rad(3 * m)) * 0.000289;
  const double sun_true_long = geom_mean_long + sun_eq_ctr;
  const double sun_app_long =
      sun_true_long - 0.00569 - 0.00478 * std::sin(Rad(125.04 - 1934.136 * t));

  const double mean_obliq =
      23.0 + (26.0 + ((21.448 - t * (46.815 + t * (0.00059 - t * 0.001813)))) / 60.0) / 60.0;
  const double obliq_corr = mean_obliq + 0.00256 * std::cos(Rad(125.04 - 1934.136 * t));

  const double declination =
      Deg(std::asin(std::sin(Rad(obliq_corr)) * std::sin(Rad(sun_app_long))));

  const double var_y = std::tan(Rad(obliq_corr / 2.0)) * std::tan(Rad(obliq_corr / 2.0));
  const double eq_of_time =  // minutes
      4.0 * Deg(var_y * std::sin(2 * Rad(geom_mean_long)) -
                2.0 * eccent * std::sin(Rad(m)) +
                4.0 * eccent * var_y * std::sin(Rad(m)) * std::cos(2 * Rad(geom_mean_long)) -
                0.5 * var_y * var_y * std::sin(4 * Rad(geom_mean_long)) -
                1.25 * eccent * eccent * std::sin(2 * Rad(m)));

  // True solar time in minutes (timezone 0 because the caller passes UTC).
  double true_solar_time =
      std::fmod(hours_utc * 60.0 + eq_of_time + 4.0 * lon_deg, 1440.0);
  if (true_solar_time < 0) true_solar_time += 1440.0;

  double hour_angle = true_solar_time / 4.0 - 180.0;  // degrees
  if (true_solar_time / 4.0 < 0) hour_angle = true_solar_time / 4.0 + 180.0;

  const double lat = Rad(lat_deg);
  const double decl = Rad(declination);
  double cos_zenith = std::sin(lat) * std::sin(decl) +
                      std::cos(lat) * std::cos(decl) * std::cos(Rad(hour_angle));
  if (cos_zenith > 1.0) cos_zenith = 1.0;
  if (cos_zenith < -1.0) cos_zenith = -1.0;
  const double zenith = Deg(std::acos(cos_zenith));

  SolarPosition out;
  out.elevation_deg = 90.0 - zenith;

  const double sin_zenith = std::sin(Rad(zenith));
  if (std::fabs(sin_zenith) < 1e-9) {
    // Sun at the zenith/nadir — azimuth is undefined; report due south.
    out.azimuth_deg = 180.0;
    return out;
  }
  double az_arg = (std::sin(lat) * std::cos(Rad(zenith)) - std::sin(decl)) /
                  (std::cos(lat) * sin_zenith);
  if (az_arg > 1.0) az_arg = 1.0;
  if (az_arg < -1.0) az_arg = -1.0;
  const double az_acos = Deg(std::acos(az_arg));
  out.azimuth_deg = (hour_angle > 0) ? std::fmod(az_acos + 180.0, 360.0)
                                     : std::fmod(540.0 - az_acos, 360.0);
  return out;
}

}  // namespace fv
