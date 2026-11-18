// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_ownship.h"

#include <chrono>
#include <cmath>
#include <cstdio>

#include "fv_desk_format.h"

namespace fv {
namespace desk {

double UtcNowSeconds() {
  // system_clock counts from the Unix epoch in C++20 and on every supported platform.
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

const std::vector<std::string>& OwnshipUnitNames() {
  static const std::vector<std::string> names = {"nautical", "metric"};
  return names;
}

const std::vector<double>& PlaybackRates() {
  static const std::vector<double> rates = {1.0, 2.0, 5.0, 10.0, 60.0};
  return rates;
}

std::string PlaybackRateLabel(double rate) {
  char buf[32];
  if (std::fabs(rate - std::round(rate)) < 1e-9)
    std::snprintf(buf, sizeof buf, "%.0f\xC3\x97", rate);
  else
    std::snprintf(buf, sizeof buf, "%g\xC3\x97", rate);
  return buf;
}

namespace {
const char kUnknown[] = "\xE2\x80\x94";  // em dash

std::string Printf(const char* format, double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, format, v);
  return buf;
}
}  // namespace

OwnshipPanel BuildOwnshipPanel(const OwnshipState& state, const ReplayClock& clock,
                               bool has_range, double start_s, double end_s,
                               OwnshipUnits units) {
  OwnshipPanel p;
  p.mode = state.mode;
  p.time = p.position = p.altitude = p.heading = p.speed = kUnknown;
  switch (state.mode) {
    case OwnshipMode::kNone: p.status = "No moving map"; break;
    case OwnshipMode::kLive: p.status = "Live"; break;
    case OwnshipMode::kReplay: p.status = "Replay"; break;
  }
  if (state.mode != OwnshipMode::kNone) {
    p.source = state.source_name;
    const PositionFix& f = state.fix;
    if (f.has_time) p.time = FormatUtc(f.time_s);
    if (f.has_position) p.position = FormatPosition(GeoPoint{f.lat, f.lon});
    if (f.has_altitude)
      p.altitude = units == OwnshipUnits::kMetric ? Printf("%.0f m", f.altitude_msl_m)
                                                  : FormatFeet(f.altitude_msl_m / 0.3048);
    if (state.heading.known) {
      double deg = std::round(state.heading.degrees);
      if (deg >= 360.0) deg -= 360.0;
      p.heading = Printf("%03.0f\xC2\xB0 T", deg);
      p.heading_derived = !state.heading.reported;
    }
    if (f.has_speed)
      p.speed = units == OwnshipUnits::kMetric ? Printf("%.1f km/h", f.speed_mps * 3.6)
                                               : Printf("%.1f kt", f.speed_mps * 3600.0 / 1852.0);
  }
  p.clock_enabled = has_range;
  p.rate = clock.rate;
  const std::vector<double>& rates = PlaybackRates();
  for (size_t i = 0; i < rates.size(); ++i)
    if (std::fabs(rates[i] - clock.rate) < 1e-9) p.rate_index = static_cast<int>(i);
  if (has_range) {
    p.start_s = start_s;
    p.end_s = end_s;
    p.clock_s = clock.time_s;
    p.clock_text = FormatUtcTimeOfDay(clock.time_s);
    p.playing = clock.playing;
  }
  return p;
}

}  // namespace desk
}  // namespace fv
