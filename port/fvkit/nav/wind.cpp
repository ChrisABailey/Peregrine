// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/wind.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include <nlohmann/json.hpp>

namespace fv {
namespace nav {
namespace {

using json = nlohmann::json;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// `o[key]` when `o` is an object holding a string there, else empty. Unlike
/// json::value(), never throws on a key of another type.
std::string StringAt(const json& o, const char* key) {
  if (!o.is_object()) return std::string();
  const auto it = o.find(key);
  if (it == o.end() || !it->is_string()) return std::string();
  return it->get<std::string>();
}

/// Days from 1970-01-01 to a proleptic Gregorian date (Hinnant's algorithm),
/// so no time-zone state is touched.
int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

/// "2026-09-26T12:00:00+00:00" or "...Z" to Unix seconds.
bool ParseTimestamp(const std::string& s, double* t) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0, used = 0;
  if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi,
                  &sec, &used) != 6) {
    return false;
  }
  const std::string zone = s.substr(static_cast<size_t>(used));
  int offset_s = 0;
  if (zone != "Z") {
    int oh = 0, om = 0;
    char sign = 0;
    if (std::sscanf(zone.c_str(), "%c%2d:%2d", &sign, &oh, &om) != 3 ||
        (sign != '+' && sign != '-')) {
      return false;
    }
    offset_s = (sign == '-' ? -1 : 1) * (oh * 3600 + om * 60);
  }
  *t = static_cast<double>(DaysFromCivil(y, static_cast<unsigned>(mo),
                                         static_cast<unsigned>(d)) * 86400 +
                           h * 3600 + mi * 60 + sec - offset_s);
  return true;
}

/// "P1DT3H", "PT6H", "PT30M" to seconds. Years and months are refused: NWS
/// does not use them and their length depends on the start.
bool ParseDuration(const std::string& s, double* seconds) {
  if (s.size() < 3 || s[0] != 'P') return false;
  double total = 0.0;
  bool in_time = false;
  bool any = false;
  size_t i = 1;
  while (i < s.size()) {
    if (s[i] == 'T') {
      in_time = true;
      ++i;
      continue;
    }
    size_t digits = i;
    while (digits < s.size() && s[digits] >= '0' && s[digits] <= '9') ++digits;
    if (digits == i || digits == s.size()) return false;
    // strtod, not stod: an overlong digit run from the network must not throw.
    const double n = std::strtod(s.substr(i, digits - i).c_str(), nullptr);
    if (!std::isfinite(n)) return false;
    const char unit = s[digits];
    if (!in_time && unit == 'D') {
      total += n * 86400;
    } else if (in_time && unit == 'H') {
      total += n * 3600;
    } else if (in_time && unit == 'M') {
      total += n * 60;
    } else if (in_time && unit == 'S') {
      total += n;
    } else {
      return false;
    }
    any = true;
    i = digits + 1;
  }
  if (!any || !std::isfinite(total)) return false;
  *seconds = total;
  return true;
}

}  // namespace

bool ParseNwsInterval(const std::string& text, double* begin_s, double* end_s) {
  const size_t slash = text.find('/');
  if (slash == std::string::npos) return false;
  double begin = 0.0;
  double length = 0.0;
  if (!ParseTimestamp(text.substr(0, slash), &begin) ||
      !ParseDuration(text.substr(slash + 1), &length)) {
    return false;
  }
  *begin_s = begin;
  *end_s = begin + length;
  return true;
}

Status WindForecast::Parse(const std::string& json_text) {
  *this = WindForecast();
  const json doc = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object()) {
    return Status::Error(kInvalidArg, "wind forecast is not JSON");
  }
  const json& props = doc.contains("properties") ? doc["properties"] : doc;

  // Speed units: NWS writes "wmoUnit:km_h-1"; m/s is accepted for a document
  // from another source in the same shape.
  auto scale_of = [](const json& series, double* scale) {
    const std::string uom = StringAt(series, "uom");
    if (uom.find("km_h-1") != std::string::npos) {
      *scale = 1000.0 / 3600.0;
    } else if (uom.find("m_s-1") != std::string::npos) {
      *scale = 1.0;
    } else {
      return false;
    }
    return true;
  };
  auto read = [](const json& series, double scale, std::vector<Value>* out) {
    const auto values = series.find("values");
    if (values == series.end() || !values->is_array()) return false;
    for (const json& v : *values) {
      if (!v.is_object()) return false;
      const auto value = v.find("value");
      if (value == v.end() || value->is_null()) continue;
      if (!value->is_number()) return false;
      Value parsed;
      if (!ParseNwsInterval(StringAt(v, "validTime"), &parsed.begin_s,
                            &parsed.end_s)) {
        return false;
      }
      parsed.value = value->get<double>() * scale;
      out->push_back(parsed);
    }
    std::sort(out->begin(), out->end(),
              [](const Value& a, const Value& b) { return a.begin_s < b.begin_s; });
    return true;
  };

  WindForecast f;
  const auto speed = props.find("windSpeed");
  const auto direction = props.find("windDirection");
  if (speed == props.end() || direction == props.end() || !speed->is_object() ||
      !direction->is_object()) {
    return Status::Error(kInvalidArg, "wind forecast lacks speed or direction");
  }
  double speed_scale = 0.0;
  if (!scale_of(*speed, &speed_scale)) {
    return Status::Error(kUnsupported, "wind speed unit not km/h or m/s");
  }
  if (!read(*speed, speed_scale, &f.speed_) || !read(*direction, 1.0, &f.direction_)) {
    return Status::Error(kInvalidArg, "wind forecast has an unreadable value");
  }
  const auto gust = props.find("windGust");
  if (gust != props.end() && gust->is_object()) {
    double gust_scale = 0.0;
    if (!scale_of(*gust, &gust_scale)) {
      return Status::Error(kUnsupported, "wind gust unit not km/h or m/s");
    }
    if (!read(*gust, gust_scale, &f.gust_)) {
      return Status::Error(kInvalidArg, "wind forecast has an unreadable gust");
    }
  }
  if (f.speed_.empty() || f.direction_.empty()) {
    return Status::Error(kInvalidArg, "wind forecast has no values");
  }
  double updated = 0.0;
  if (ParseTimestamp(StringAt(props, "updateTime"), &updated)) {
    f.update_time_ = updated;
  }
  *this = std::move(f);
  return Status::Ok();
}

double WindForecast::ValidUntil() const {
  if (empty()) return 0.0;
  return std::min(speed_.back().end_s, direction_.back().end_s);
}

double WindForecast::Lookup(const std::vector<Value>& series, double t) {
  // The last interval starting at or before t, if it still holds t.
  auto it = std::upper_bound(series.begin(), series.end(), t,
                             [](double x, const Value& v) { return x < v.begin_s; });
  if (it == series.begin()) return kNaN;
  --it;
  return t < it->end_s ? it->value : kNaN;
}

Status WindForecast::At(double t, WindSample* sample) const {
  const double speed = Lookup(speed_, t);
  const double from = Lookup(direction_, t);
  if (std::isnan(speed) || std::isnan(from)) {
    return Status::Error(kOutOfCoverage, "no wind forecast for that time");
  }
  sample->speed_mps = speed;
  sample->from_deg = from;
  sample->gust_mps = Lookup(gust_, t);
  return Status::Ok();
}

double TrueBearingDeg(const GeoPoint& from, const GeoPoint& to) {
  if (from.lat == to.lat && from.lon == to.lon) return 0.0;
  const double p1 = from.lat * kDeg;
  const double p2 = to.lat * kDeg;
  const double dl = (to.lon - from.lon) * kDeg;
  const double y = std::sin(dl) * std::cos(p2);
  const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
  return std::fmod(std::atan2(y, x) / kDeg + 360.0, 360.0);
}

double TailwindComponent(const WindSample& wind, double heading_deg) {
  // The wind travels toward from_deg + 180.
  return wind.speed_mps * std::cos((wind.from_deg + 180.0 - heading_deg) * kDeg);
}

double OnshoreComponent(const WindSample& wind, double seaward_deg) {
  return wind.speed_mps * std::cos((wind.from_deg - seaward_deg) * kDeg);
}

double SeawardOf(double heading_deg, double faces_deg) {
  const double right = std::fmod(heading_deg + 90.0 + 360.0, 360.0);
  const double left = std::fmod(heading_deg + 270.0, 360.0);
  auto gap = [faces_deg](double a) {
    return std::fabs(std::fmod(a - faces_deg + 540.0, 360.0) - 180.0);
  };
  return gap(right) <= gap(left) ? right : left;
}

}  // namespace nav
}  // namespace fv
