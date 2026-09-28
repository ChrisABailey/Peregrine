// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file fvkit/nav/wind.h
 * A wind forecast from the US National Weather Service, and the wind's
 * components along a beach.
 *
 * The forecast is the `windSpeed`, `windGust` and `windDirection` series of an
 * api.weather.gov gridpoint document (`/gridpoints/{office}/{x},{y}`): each a
 * list of values over ISO 8601 intervals. Speeds are converted to metres per
 * second; directions are degrees true the wind blows FROM. Times are Unix
 * seconds (UTC). Fetching is the shell's job; this only reads the text.
 */

#ifndef FVKIT_NAV_WIND_H_
#define FVKIT_NAV_WIND_H_

#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace nav {

/// The forecast wind at one time. `gust_mps` is NaN when the forecast has no
/// gust for that hour.
struct WindSample {
  double speed_mps = 0.0;
  double gust_mps = 0.0;
  double from_deg = 0.0;
};

/// One NWS gridpoint's wind forecast.
class WindForecast {
 public:
  /// Parses a gridpoint document. kInvalidArg if it is not JSON, lacks the
  /// speed or direction series, or holds an interval it cannot read;
  /// kUnsupported for a speed unit other than km/h or m/s. On failure the
  /// forecast is left empty.
  Status Parse(const std::string& json_text);

  bool empty() const { return speed_.empty(); }
  /// When NWS last updated the forecast, or 0 when the document omits it.
  double UpdateTime() const { return update_time_; }
  /// The first instant no longer covered by both speed and direction.
  double ValidUntil() const;

  /// The wind at `t`. kOutOfCoverage when no speed or direction interval
  /// holds `t`.
  Status At(double t, WindSample* sample) const;

 private:
  struct Value {
    double begin_s = 0.0;
    double end_s = 0.0;
    double value = 0.0;
  };
  /// The value whose interval holds `t`, or NaN.
  static double Lookup(const std::vector<Value>& series, double t);

  double update_time_ = 0.0;
  std::vector<Value> speed_;
  std::vector<Value> gust_;
  std::vector<Value> direction_;
};

/// Reads an ISO 8601 interval of the form NWS uses,
/// "2026-09-26T12:00:00+00:00/PT3H": a start with a UTC offset (or Z) and a
/// duration in days, hours and minutes. False if it is not one.
bool ParseNwsInterval(const std::string& text, double* begin_s, double* end_s);

/// The initial great-circle bearing from `from` to `to`, degrees true in
/// [0, 360). Zero when the points coincide.
double TrueBearingDeg(const GeoPoint& from, const GeoPoint& to);

/// The wind's component along a direction of travel, `heading_deg` true.
/// Positive is a tailwind, negative a headwind.
double TailwindComponent(const WindSample& wind, double heading_deg);

/// The wind's component blowing from the sea onto the land, for a shore whose
/// seaward normal is `seaward_deg`. Positive is onshore.
double OnshoreComponent(const WindSample& wind, double seaward_deg);

/// The seaward normal of a shore run along `heading_deg`: whichever of the
/// two perpendiculars lies nearer `faces_deg`, the direction the beach faces
/// as a whole. Lets a curving beach keep the sea on the correct side.
double SeawardOf(double heading_deg, double faces_deg);

}  // namespace nav
}  // namespace fv

#endif  // FVKIT_NAV_WIND_H_
