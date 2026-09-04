// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/analysis/measure.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "geo_tool.h"  // fv_geo_tool: GEO_magnetic_variation

namespace fv {
namespace analysis {
namespace {

// CUnits, verbatim.
constexpr double kMetersPerFoot = 0.3048;
constexpr double kMetersPerYard = 0.9144;
constexpr double kMetersPerNaut = 1852.0;
constexpr double kMetersPerMile = 1609.344;

// The literal in TerrainMaskPropDlg.cpp's convert_angular_units. It is
// 6400/360 written out, and it is written out here for the same reason the
// unit constants are: so a label matches digit for digit.
constexpr double kMilsPerDegree = 17.777777777777777;

std::string Ftoa(double value, int decimal_places) {
  // utils.cpp builds "%0.Nf" and sprintfs into a 256-byte buffer. The zero
  // flag with no width does nothing, so "%.*f" is the same conversion.
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", decimal_places, value);
  return std::string(buf);
}

}  // namespace

const char kDegreeSign[] = "\xC2\xB0";  // U+00B0, UTF-8

double MetersPerUnit(RangeUnit unit) {
  switch (unit) {
    case RangeUnit::kNauticalMiles: return kMetersPerNaut;
    case RangeUnit::kMiles:         return kMetersPerMile;
    case RangeUnit::kKilometers:    return 1000.0;
    case RangeUnit::kMeters:        return 1.0;
    case RangeUnit::kYards:         return kMetersPerYard;
    case RangeUnit::kFeet:          return kMetersPerFoot;
  }
  return 1.0;
}

double ConvertRange(double meters, RangeUnit unit) {
  return meters / MetersPerUnit(unit);
}

double ConvertArea(double square_meters, RangeUnit unit) {
  const double m = MetersPerUnit(unit);
  return square_meters / (m * m);
}

const char* RangeUnitName(RangeUnit unit) {
  switch (unit) {
    case RangeUnit::kNauticalMiles: return "NM";
    case RangeUnit::kMiles:         return "miles";
    case RangeUnit::kKilometers:    return "km";
    case RangeUnit::kMeters:        return "m";
    case RangeUnit::kYards:         return "yards";
    case RangeUnit::kFeet:          return "ft";
  }
  return "m";
}

std::string AreaUnitName(RangeUnit unit) {
  return std::string("sq ") + RangeUnitName(unit);
}

double DegreesToMils(double degrees) { return degrees * kMilsPerDegree; }
double MilsToDegrees(double mils) { return mils / kMilsPerDegree; }

int RangeDecimals(double value_in_units) {
  if (value_in_units < 100.0) return 2;
  if (value_in_units < 1000.0) return 1;
  return 0;
}

double TrueToMagnetic(double true_bearing_deg, GeoPoint at,
                      MagneticEpoch epoch) {
  int year = epoch.year;
  int month = epoch.month;
  if (epoch.is_now()) {
    // GetSystemTime() is UTC, so gmtime is the like-for-like call.
    const std::time_t now = std::time(nullptr);
    std::tm utc {};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    year = utc.tm_year + 1900;
    month = utc.tm_mon + 1;
  }

  double magvar = 0.0;
  if (GEO_magnetic_variation(at.lat, at.lon, year, month, epoch.altitude_m,
                             &magvar) != SUCCESS) {
    magvar = 0.0;
  }

  double bearing = true_bearing_deg - magvar;
  if (bearing < 0.0)
    bearing += 360.0;
  else if (bearing > 360.0)
    bearing -= 360.0;
  return bearing;
}

std::string FormatBearing(double degrees, const MeasureStyle& style) {
  char buf[64];

  if (style.angle_units == AngleUnit::kMils) {
    // Mils win over the display format, and the trailing space is inside the
    // string in FalconView too.
    std::snprintf(buf, sizeof(buf), "%06.1fMils ", DegreesToMils(degrees));
    return std::string(buf);
  }

  if (style.bearing_format == BearingFormat::kDegrees) {
    std::snprintf(buf, sizeof(buf), "%05.1f", degrees);
    return std::string(buf) + kDegreeSign;
  }

  double whole = 0.0;
  const double minutes = std::modf(degrees, &whole) * 60.0;

  if (style.bearing_format == BearingFormat::kDegreesMinutes) {
    std::snprintf(buf, sizeof(buf), "%.3d", static_cast<int>(degrees));
    std::string out(buf);
    out += kDegreeSign;
    std::snprintf(buf, sizeof(buf), " %04.1f'", minutes);
    return out + buf;
  }

  // Degrees, minutes and seconds. Every field TRUNCATES: the seconds come
  // from what is left of the minutes, and nothing is ever rounded up into
  // the field above it.
  const double seconds = std::modf(minutes, &whole) * 60.0;
  std::snprintf(buf, sizeof(buf), "%.3d", static_cast<int>(degrees));
  std::string out(buf);
  out += kDegreeSign;
  std::snprintf(buf, sizeof(buf), " %.2d' %.2d\"", static_cast<int>(minutes),
                static_cast<int>(seconds));
  return out + buf;
}

std::string FormatRange(double meters, const MeasureStyle& style,
                        int decimals) {
  const double value = ConvertRange(meters, style.units);
  const int places = decimals < 0 ? RangeDecimals(value) : decimals;
  return Ftoa(value, places) + " " + RangeUnitName(style.units);
}

std::string FormatArea(double square_meters, const MeasureStyle& style) {
  return Ftoa(ConvertArea(square_meters, style.units), 0) + " " +
         AreaUnitName(style.units);
}

Measurement::Measurement(MeasurementKind kind, GeoPath path, MeasureStyle style)
    : kind_(kind), path_(std::move(path)), style_(style) {}

LegMeasurement Measurement::Leg(size_t i) const {
  LegMeasurement out;
  if (i >= path_.LegCount()) return out;

  const PathLeg& leg = path_.Leg(i);
  out.range_m = leg.range_m;
  out.bearing_deg = leg.bearing_deg;
  out.ok = leg.ok;
  out.cumulative_m = path_.CumulativeAt(i + 1);

  if (style_.bearing_reference == BearingReference::kMagnetic && leg.ok) {
    // The variation is taken at the START of the leg — utils.cpp passes
    // lat1/lon1 — so a long leg is labelled with its departure declination.
    out.bearing_deg =
        TrueToMagnetic(out.bearing_deg, path_.points()[i], style_.epoch);
  }
  return out;
}

std::string Measurement::LegLabel(size_t i) const {
  if (i >= path_.LegCount()) return std::string();
  const LegMeasurement leg = Leg(i);
  // calc_range_bearing_txt returns an EMPTY string when the geodesy refuses,
  // and the overlay draws nothing. Same here.
  if (!leg.ok) return std::string();

  const char* true_mag =
      style_.bearing_reference == BearingReference::kTrue ? "T" : "M";
  return " " + FormatBearing(leg.bearing_deg, style_) + true_mag + " / " +
         FormatRange(leg.range_m, style_) + " ";
}

std::string Measurement::LegBearingLabel(size_t i) const {
  if (i >= path_.LegCount()) return std::string();
  const LegMeasurement leg = Leg(i);
  const char* true_mag =
      style_.bearing_reference == BearingReference::kTrue ? "T" : "M";
  // calc_bearing_txt formats whatever is in `bearing` even when the geodesy
  // failed, because the variable is declared outside the `if`. A refused leg
  // is a bearing of zero there and here.
  return " " + FormatBearing(leg.ok ? leg.bearing_deg : 0.0, style_) +
         true_mag + " ";
}

std::string Measurement::SummaryLabel() const {
  switch (kind_) {
    case MeasurementKind::kRangeBearing:
      return LegLabel(0);
    case MeasurementKind::kMultiPoint:
      // display_multi_point_RB_range: two places, no prefix, no padding.
      return FormatRange(path_.TotalLength(), style_, 2);
    case MeasurementKind::kTotalDistance:
      return " Total Distance: " + FormatRange(path_.TotalLength(), style_, 2);
    case MeasurementKind::kArea:
      return " " + FormatArea(path_.AreaSquareMeters(), style_);
  }
  return std::string();
}

std::vector<std::string> Measurement::Labels() const {
  std::vector<std::string> out;
  switch (kind_) {
    case MeasurementKind::kRangeBearing: {
      std::string label = LegLabel(0);
      if (!label.empty()) out.push_back(std::move(label));
      break;
    }
    case MeasurementKind::kMultiPoint: {
      // redraw_dist_str draws the RUNNING TOTAL at each turning point after
      // the first — not the leg's own range, and not its bearing. A leg the
      // geodesy refused contributes no label at all, which is the `if (...
      // == SUCCESS)` wrapped round the whole body over there.
      out.reserve(path_.LegCount());
      for (size_t i = 0; i < path_.LegCount(); ++i) {
        if (!path_.Leg(i).ok) continue;
        out.push_back(FormatRange(path_.CumulativeAt(i + 1), style_, 2));
      }
      break;
    }
    case MeasurementKind::kTotalDistance:
    case MeasurementKind::kArea:
      out.push_back(SummaryLabel());
      break;
  }
  return out;
}

}  // namespace analysis
}  // namespace fv
