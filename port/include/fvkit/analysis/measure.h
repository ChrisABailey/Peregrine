// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/analysis/measure.h — the measurement objects: range and bearing,
// multi-point, total distance, area. What a user reads off the map.
//
// AN4 of port/analysis-plan.md. AN1 (`path.h`) already does every piece of
// geometry these four need, so what is left here is what the Windows Range &
// Bearing overlay spends its `utils.cpp` on: A UNIT TABLE AND A SET OF
// FORMATTING RULES. Those rules are transcribed, not improved, because they
// are what the labels in a user's existing overlays look like:
//
//   * the range decimal ladder is by MAGNITUDE, not by unit — under 100 gets
//     two places, under 1000 gets one, 1000 and over gets none — so the same
//     leg reads "12.34 NM" and "22853.02 m"... except that the total-distance
//     and multi-point labels DO NOT USE THE LADDER, they are always two
//     places. Both behaviours are here, and `FormatRange`'s `decimals`
//     argument is which one you get;
//   * mils beat the display format: `angle_units == kMils` renders mils
//     whatever `bearing_format` says, in `%06.1f` with the word "Mils" and a
//     TRAILING SPACE inside the string, which is FalconView's and is why the
//     range/bearing label reads " 0800.0Mils T / 12.34 NM ";
//   * degrees/minutes and degrees/minutes/seconds TRUNCATE rather than round
//     (`(int)degrees`, then `modf` on what is left), so 44.99999 degrees is
//     044 59' 59" and never 045;
//   * the leading and trailing spaces in every label are FalconView's own
//     padding around the text it hands its symbol layer. They are kept so a
//     label compares equal to the one the Windows overlay drew.
//
// Three decisions of our own, all small:
//
//   * THE DEGREE SIGN IS UTF-8. The Windows literals carry a lone 0xB0 byte,
//     which is CP-1252's degree sign and mojibake anywhere else. `kDegreeSign`
//     is the two-byte UTF-8 sequence; nothing else about the strings moved.
//   * AREA IS COMPUTED IN METRES AND CONVERTED, not computed in display
//     units. AreaToolObj converts every leg into the display unit BEFORE the
//     shoelace, so its area comes out in unit^2; dividing the metric area by
//     (metres per unit)^2 is the same number (the shoelace is homogeneous of
//     degree two) and does not make the geometry depend on a UI setting.
//   * THE MAGNETIC EPOCH IS AN ARGUMENT. FalconView calls GetSystemTime()
//     inside the formatting function, which makes a label untestable and a
//     saved measurement drift. A default-constructed MagneticEpoch still
//     means "now", so the behaviour is unchanged unless a caller asks.

#ifndef FVKIT_ANALYSIS_MEASURE_H_
#define FVKIT_ANALYSIS_MEASURE_H_

#include <cstddef>
#include <string>
#include <vector>

#include "fvkit/analysis/path.h"
#include "fvkit/geo.h"

namespace fv {
namespace analysis {

// `rb::units_t`, in FalconView's own order.
enum class RangeUnit {
  kNauticalMiles,
  kMiles,
  kKilometers,
  kMeters,
  kYards,
  kFeet,
};

// `angular_units_t`.
enum class AngleUnit { kDegrees, kMils };

// `display_format_t`.
enum class BearingFormat { kDegrees, kDegreesMinutes, kDegreesMinutesSeconds };

// The overlay's `is_heading_true_not_mag`.
enum class BearingReference { kTrue, kMagnetic };

// UTF-8 U+00B0. See the header comment.
extern const char kDegreeSign[];

// Exactly the CUnits constants: 1852 m to the nautical mile, 1609.344 to the
// statute mile, 0.9144 to the yard, 0.3048 to the foot.
double MetersPerUnit(RangeUnit unit);

double ConvertRange(double meters, RangeUnit unit);
double ConvertArea(double square_meters, RangeUnit unit);

// "NM", "miles", "km", "m", "yards", "ft" — the words FalconView appends.
const char* RangeUnitName(RangeUnit unit);
// The same with FalconView's "sq " in front: "sq NM", "sq ft", ...
std::string AreaUnitName(RangeUnit unit);

// 6400 mils to the circle, written in the Windows source as the repeating
// decimal 17.777777777777777 rather than as 6400/360. Kept as the same
// literal so a mils label matches digit for digit.
double DegreesToMils(double degrees);
double MilsToDegrees(double mils);

// `get_num_decimal_places`: < 100 -> 2, < 1000 -> 1, otherwise 0. The value
// is the one already converted into display units.
int RangeDecimals(double value_in_units);

// Which world magnetic model epoch to ask for. A default-constructed epoch
// (year 0) means "now", in UTC, which is FalconView's GetSystemTime().
struct MagneticEpoch {
  int year = 0;
  int month = 0;
  int altitude_m = 0;

  bool is_now() const { return year <= 0; }
};

// The overlay's true-to-magnetic step, sign and wrap included: subtract the
// variation (east positive), add 360 if that went negative, subtract 360 if
// it went ABOVE 360 — note that `> 360.0`, not `>= 360.0`, is FalconView's
// own comparison and it is kept.
//
// A model failure is a variation of zero, i.e. the true bearing comes back
// unchanged. FalconView does the same and says nothing about it.
double TrueToMagnetic(double true_bearing_deg, GeoPoint at,
                      MagneticEpoch epoch = MagneticEpoch());

// Everything a measurement needs that is not geometry — the property sheet.
struct MeasureStyle {
  RangeUnit units = RangeUnit::kNauticalMiles;
  AngleUnit angle_units = AngleUnit::kDegrees;
  BearingFormat bearing_format = BearingFormat::kDegrees;
  BearingReference bearing_reference = BearingReference::kTrue;
  MagneticEpoch epoch;  // read only when bearing_reference == kMagnetic
};

// `format_bearing`. The bearing is in degrees true or magnetic already —
// this function only renders it.
std::string FormatBearing(double degrees, const MeasureStyle& style);

// The range in display units and its unit word. `decimals < 0` uses the
// magnitude ladder (the range/bearing label); `decimals >= 0` fixes the
// places (the total-distance and multi-point labels pass 2).
std::string FormatRange(double meters, const MeasureStyle& style,
                        int decimals = -1);

// `ftoa(area, 0) + " sq NM"` — no leading space; the callers add their own.
std::string FormatArea(double square_meters, const MeasureStyle& style);

// The four objects of the Range & Bearing overlay. They differ only in what
// they label, which is why they are one class here and five copies of the
// same geodesy there.
enum class MeasurementKind {
  kRangeBearing,     // CRangeBearingObject: one leg, bearing and range
  kMultiPoint,       // CMultiPointRBObj: a cumulative label per turning point
  kTotalDistance,    // CTotalDistanceObj: one "Total Distance:" label
  kArea,             // AreaToolObj: the shoelace, in sq units
};

// A leg as a measurement reports it: metres, the bearing in the style's own
// reference, and the running total to the leg's END vertex.
struct LegMeasurement {
  double range_m = 0.0;
  double bearing_deg = 0.0;  // true or magnetic, per the style
  double cumulative_m = 0.0;
  bool ok = false;
};

class Measurement {
 public:
  Measurement() = default;
  Measurement(MeasurementKind kind, GeoPath path, MeasureStyle style);

  MeasurementKind kind() const { return kind_; }
  const GeoPath& path() const { return path_; }
  const MeasureStyle& style() const { return style_; }

  void SetKind(MeasurementKind kind) { kind_ = kind; }
  void SetPath(GeoPath path) { path_ = std::move(path); }
  void SetStyle(const MeasureStyle& style) { style_ = style; }

  size_t LegCount() const { return path_.LegCount(); }

  // The leg's own numbers, with the magnetic correction already applied when
  // the style asks for one. An unmeasurable leg is zero-length and `ok ==
  // false`, exactly as AN1 leaves it.
  LegMeasurement Leg(size_t i) const;

  double TotalRangeMeters() const { return path_.TotalLength(); }
  double AreaSquareMeters() const { return path_.AreaSquareMeters(); }

  // " 045.0<deg>T / 12.34 NM " — `calc_range_bearing_txt`, padding included.
  std::string LegLabel(size_t i) const;
  // " 045.0<deg>T " — `calc_bearing_txt`.
  std::string LegBearingLabel(size_t i) const;

  // The one label that names the whole object, per kind:
  //   kRangeBearing   the first leg's LegLabel
  //   kMultiPoint     "12.34 NM"                  (total, two places)
  //   kTotalDistance  " Total Distance: 12.34 NM"
  //   kArea           " 1234 sq NM"
  std::string SummaryLabel() const;

  // Everything the overlay would draw on the map for this object, in the
  // order it drew it. For kMultiPoint that is the CUMULATIVE distance at
  // every turning point after the first — which is what the Windows code
  // draws, and is not the same as a per-leg range.
  std::vector<std::string> Labels() const;

 private:
  MeasurementKind kind_ = MeasurementKind::kRangeBearing;
  GeoPath path_;
  MeasureStyle style_;
};

}  // namespace analysis
}  // namespace fv

#endif  // FVKIT_ANALYSIS_MEASURE_H_
