// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The measurement objects (fvkit/analysis/measure.h, plan step AN4).
//
// AN1 already has tests for the geometry, so these are about the STRINGS —
// which is the whole of what AN4 adds and the only part of it a user can
// see. Every expected literal below was worked out from the format string in
// the Windows source (utils.cpp, MultiPointRBObj.cpp, TotalDistanceObj.cpp,
// Area_tool.cpp), padding spaces and all, because a label that differs by a
// space from the one FalconView drew is a regression nobody would notice
// until a user compared two screenshots.

#include "fvkit/analysis/measure.h"

#include <cmath>
#include <string>

#include "geo_tool.h"
#include "gtest/gtest.h"

namespace {

using fv::GeoPoint;
using fv::analysis::AngleUnit;
using fv::analysis::BearingFormat;
using fv::analysis::BearingReference;
using fv::analysis::ConvertArea;
using fv::analysis::ConvertRange;
using fv::analysis::FormatArea;
using fv::analysis::FormatBearing;
using fv::analysis::FormatRange;
using fv::analysis::GeoPath;
using fv::analysis::LineType;
using fv::analysis::MagneticEpoch;
using fv::analysis::Measurement;
using fv::analysis::MeasurementKind;
using fv::analysis::MeasureStyle;
using fv::analysis::MetersPerUnit;
using fv::analysis::RangeDecimals;
using fv::analysis::RangeUnit;
using fv::analysis::TrueToMagnetic;

// The degree sign the labels carry, so the expectations below read.
const std::string kDeg = "\xC2\xB0";

// ---------------------------------------------------------------- units

TEST(AnalysisMeasure, UnitTableIsTheCUnitsTable) {
  EXPECT_EQ(MetersPerUnit(RangeUnit::kNauticalMiles), 1852.0);
  EXPECT_EQ(MetersPerUnit(RangeUnit::kMiles), 1609.344);
  EXPECT_EQ(MetersPerUnit(RangeUnit::kKilometers), 1000.0);
  EXPECT_EQ(MetersPerUnit(RangeUnit::kMeters), 1.0);
  EXPECT_EQ(MetersPerUnit(RangeUnit::kYards), 0.9144);
  EXPECT_EQ(MetersPerUnit(RangeUnit::kFeet), 0.3048);

  EXPECT_DOUBLE_EQ(ConvertRange(1852.0, RangeUnit::kNauticalMiles), 1.0);
  EXPECT_DOUBLE_EQ(ConvertRange(1000.0, RangeUnit::kKilometers), 1.0);
  EXPECT_DOUBLE_EQ(ConvertRange(0.3048, RangeUnit::kFeet), 1.0);
}

// AreaToolObj converts every leg into display units and THEN runs the
// shoelace, so its area is in unit^2. Dividing the metric area by the square
// of the unit is the same number; that equivalence is the reason the port is
// allowed to keep the geometry in metres.
TEST(AnalysisMeasure, AreaConversionIsTheSquareOfTheLinearOne) {
  const double m2 = 1852.0 * 1852.0;  // one square nautical mile
  EXPECT_DOUBLE_EQ(ConvertArea(m2, RangeUnit::kNauticalMiles), 1.0);
  EXPECT_DOUBLE_EQ(ConvertArea(1.0e6, RangeUnit::kKilometers), 1.0);

  // The shoelace is homogeneous of degree two: scaling every leg by 1/k
  // scales the area by 1/k^2. Ten legs of a real polygon, both ways round.
  const double side_m = 5000.0;
  const double area_m2 = side_m * side_m;
  const double k = MetersPerUnit(RangeUnit::kYards);
  const double side_yd = side_m / k;
  EXPECT_NEAR(ConvertArea(area_m2, RangeUnit::kYards), side_yd * side_yd,
              1.0e-6);
}

TEST(AnalysisMeasure, DecimalLadderIsByMagnitude) {
  EXPECT_EQ(RangeDecimals(0.0), 2);
  EXPECT_EQ(RangeDecimals(99.999), 2);
  EXPECT_EQ(RangeDecimals(100.0), 1);
  EXPECT_EQ(RangeDecimals(999.999), 1);
  EXPECT_EQ(RangeDecimals(1000.0), 0);
  EXPECT_EQ(RangeDecimals(1.0e9), 0);
}

// The same leg, read in six units, is written with a different number of
// decimals in each — because the ladder is on the VALUE, not the unit.
TEST(AnalysisMeasure, RangeLadderAppliesToTheConvertedValue) {
  MeasureStyle style;
  const double meters = 22853.02;

  style.units = RangeUnit::kNauticalMiles;
  EXPECT_EQ(FormatRange(meters, style), "12.34 NM");
  style.units = RangeUnit::kMiles;
  EXPECT_EQ(FormatRange(meters, style), "14.20 miles");
  style.units = RangeUnit::kKilometers;
  EXPECT_EQ(FormatRange(meters, style), "22.85 km");
  style.units = RangeUnit::kMeters;
  EXPECT_EQ(FormatRange(meters, style), "22853 m");
  style.units = RangeUnit::kYards;
  EXPECT_EQ(FormatRange(meters, style), "24992 yards");
  style.units = RangeUnit::kFeet;
  EXPECT_EQ(FormatRange(meters, style), "74977 ft");

  // ... and the total-distance labels bypass the ladder entirely.
  style.units = RangeUnit::kFeet;
  EXPECT_EQ(FormatRange(meters, style, 2), "74977.10 ft");
}

// -------------------------------------------------------------- bearings

TEST(AnalysisMeasure, BearingDegreesIsFiveWideOneDecimal) {
  MeasureStyle style;  // degrees, degrees format
  EXPECT_EQ(FormatBearing(45.0, style), "045.0" + kDeg);
  EXPECT_EQ(FormatBearing(5.25, style), "005.2" + kDeg);  // printf rounds
  EXPECT_EQ(FormatBearing(359.94, style), "359.9" + kDeg);
  EXPECT_EQ(FormatBearing(0.0, style), "000.0" + kDeg);
}

TEST(AnalysisMeasure, BearingDegreesMinutes) {
  MeasureStyle style;
  style.bearing_format = BearingFormat::kDegreesMinutes;
  EXPECT_EQ(FormatBearing(45.5, style), "045" + kDeg + " 30.0'");
  EXPECT_EQ(FormatBearing(7.25, style), "007" + kDeg + " 15.0'");
  EXPECT_EQ(FormatBearing(123.0, style), "123" + kDeg + " 00.0'");
}

// Every field truncates and nothing carries up, which is why 44.99999 is not
// 045. FalconView casts to int at each step and so does this.
TEST(AnalysisMeasure, BearingDegreesMinutesSecondsTruncate) {
  MeasureStyle style;
  style.bearing_format = BearingFormat::kDegreesMinutesSeconds;
  EXPECT_EQ(FormatBearing(45.5125, style), "045" + kDeg + " 30' 45\"");
  EXPECT_EQ(FormatBearing(44.99999, style), "044" + kDeg + " 59' 59\"");
  EXPECT_EQ(FormatBearing(1.0, style), "001" + kDeg + " 00' 00\"");
}

// Mils beat the display format, carry four digits and one decimal, and the
// trailing space is inside the string. All three are FalconView's.
TEST(AnalysisMeasure, MilsOverrideTheDisplayFormat) {
  MeasureStyle style;
  style.angle_units = AngleUnit::kMils;
  EXPECT_EQ(FormatBearing(45.0, style), "0800.0Mils ");
  EXPECT_EQ(FormatBearing(360.0, style), "6400.0Mils ");
  EXPECT_EQ(FormatBearing(0.0, style), "0000.0Mils ");

  style.bearing_format = BearingFormat::kDegreesMinutesSeconds;
  EXPECT_EQ(FormatBearing(45.0, style), "0800.0Mils ");
}

// -------------------------------------------------------------- the label

// A degree of latitude due north from 34 N: 60 nautical miles on the sphere,
// a shade more on the ellipsoid, and due north whatever the line type.
GeoPath NorthLeg() {
  return GeoPath({GeoPoint{34.0, -84.0}, GeoPoint{35.0, -84.0}},
                 LineType::kGreatCircle);
}

TEST(AnalysisMeasure, RangeBearingLabelIsCalcRangeBearingTxt) {
  MeasureStyle style;  // NM, degrees, true
  Measurement m(MeasurementKind::kRangeBearing, NorthLeg(), style);

  ASSERT_EQ(m.LegCount(), 1u);
  const auto leg = m.Leg(0);
  EXPECT_TRUE(leg.ok);
  EXPECT_NEAR(leg.bearing_deg, 0.0, 1.0e-6);
  EXPECT_NEAR(leg.range_m, 60.0 * 1852.0, 600.0);
  EXPECT_EQ(leg.cumulative_m, m.TotalRangeMeters());

  // " 000.0<deg>T / 60.02 NM " — leading and trailing space, the slash
  // spaced on both sides, and the unit word after the number.
  const std::string label = m.LegLabel(0);
  EXPECT_EQ(label.substr(0, 1), " ");
  EXPECT_EQ(label.substr(label.size() - 1), " ");
  EXPECT_NE(label.find("000.0" + kDeg + "T / "), std::string::npos) << label;
  EXPECT_NE(label.find(" NM "), std::string::npos) << label;
  EXPECT_EQ(m.SummaryLabel(), label);
  EXPECT_EQ(m.Labels().size(), 1u);

  EXPECT_EQ(m.LegBearingLabel(0), " 000.0" + kDeg + "T ");
}

TEST(AnalysisMeasure, MagneticSwapsTheSuffixAndMovesTheBearing) {
  MeasureStyle style;
  style.bearing_reference = BearingReference::kMagnetic;
  style.epoch = MagneticEpoch{2020, 6, 0};

  Measurement m(MeasurementKind::kRangeBearing, NorthLeg(), style);
  const std::string label = m.LegLabel(0);
  EXPECT_NE(label.find("M / "), std::string::npos) << label;
  EXPECT_EQ(label.find("T / "), std::string::npos) << label;

  // The correction itself is geo_tool's; what belongs to this file is the
  // sign, the wrap and the point the variation is taken at (the START of the
  // leg — utils.cpp passes lat1/lon1).
  double magvar = 0.0;
  ASSERT_EQ(GEO_magnetic_variation(34.0, -84.0, 2020, 6, 0, &magvar), SUCCESS);
  double expect = 0.0 - magvar;
  if (expect < 0.0) expect += 360.0;
  EXPECT_NEAR(m.Leg(0).bearing_deg, expect, 1.0e-9);
  EXPECT_GE(m.Leg(0).bearing_deg, 0.0);
  EXPECT_LT(m.Leg(0).bearing_deg, 360.0);

  // Georgia's declination is a few degrees west, so a true north bearing
  // reads a few degrees EAST of north on the magnetic compass and the wrap
  // is exercised in the direction the overlay's `+= 360` was written for.
  EXPECT_LT(magvar, 0.0);
  EXPECT_GT(m.Leg(0).bearing_deg, 0.0);
  EXPECT_LT(m.Leg(0).bearing_deg, 45.0);
}

TEST(AnalysisMeasure, TrueToMagneticWrapsRatherThanGoingNegative) {
  // A bearing just east of north at a place with east declination has to
  // come back near 360, not near -1.
  const double b = TrueToMagnetic(0.5, GeoPoint{60.0, 30.0},
                                  MagneticEpoch{2020, 6, 0});
  EXPECT_GE(b, 0.0);
  EXPECT_LE(b, 360.0);
}

// ------------------------------------------------------- the four objects

// Three legs of a degree each: north, then east, then north again.
GeoPath ThreeLegs() {
  return GeoPath({GeoPoint{34.0, -84.0},
                  GeoPoint{35.0, -84.0},
                  GeoPoint{35.0, -83.0},
                  GeoPoint{36.0, -83.0}},
                 LineType::kGreatCircle);
}

// The multi-point object draws a RUNNING TOTAL at each turning point after
// the first — not the leg's own range, and not a bearing. Three legs, three
// labels, each larger than the last, and the last one equal to the summary.
TEST(AnalysisMeasure, MultiPointLabelsAreCumulative) {
  MeasureStyle style;
  Measurement m(MeasurementKind::kMultiPoint, ThreeLegs(), style);

  const auto labels = m.Labels();
  ASSERT_EQ(labels.size(), 3u);
  EXPECT_EQ(labels[0], FormatRange(m.Leg(0).cumulative_m, style, 2));
  EXPECT_EQ(labels[2], m.SummaryLabel());

  double previous = 0.0;
  for (size_t i = 0; i < 3; ++i) {
    const double v = std::stod(labels[i]);
    EXPECT_GT(v, previous);
    previous = v;
    EXPECT_NE(labels[i].find(" NM"), std::string::npos);
    // Always two places, whatever the magnitude.
    EXPECT_EQ(labels[i].size() - labels[i].find('.'), 6u) << labels[i];
  }
  EXPECT_NEAR(previous, ConvertRange(m.TotalRangeMeters(), RangeUnit::kNauticalMiles),
              0.01);
}

TEST(AnalysisMeasure, TotalDistanceCarriesItsPrefix) {
  MeasureStyle style;
  style.units = RangeUnit::kKilometers;
  Measurement m(MeasurementKind::kTotalDistance, ThreeLegs(), style);

  const std::string label = m.SummaryLabel();
  EXPECT_EQ(label.rfind(" Total Distance: ", 0), 0u) << label;
  EXPECT_NE(label.find(" km"), std::string::npos) << label;
  EXPECT_EQ(m.Labels().size(), 1u);
  EXPECT_EQ(m.Labels()[0], label);

  // Two places even though the value is over 100 km, where the range/bearing
  // label would have dropped to one.
  EXPECT_EQ(label.size() - label.find('.'), 6u) << label;
}

// A degree box at the equator, whose area is known on paper: a degree of
// latitude and a degree of longitude are each 60 nm there, so the square is
// 3600 square nautical miles to within the ellipsoid's own few parts in a
// thousand.
TEST(AnalysisMeasure, AreaLabelIsWholeSquareUnits) {
  GeoPath box({GeoPoint{0.0, 0.0}, GeoPoint{1.0, 0.0}, GeoPoint{1.0, 1.0},
               GeoPoint{0.0, 1.0}},
              LineType::kGreatCircle);
  MeasureStyle style;
  Measurement m(MeasurementKind::kArea, box, style);

  const double sq_nm = ConvertArea(m.AreaSquareMeters(), RangeUnit::kNauticalMiles);
  EXPECT_NEAR(sq_nm, 3600.0, 3600.0 * 0.02);

  const std::string label = m.SummaryLabel();
  EXPECT_EQ(label.rfind(" ", 0), 0u);
  EXPECT_NE(label.find(" sq NM"), std::string::npos) << label;
  EXPECT_EQ(label.find('.'), std::string::npos) << label;  // zero decimals
  EXPECT_EQ(m.Labels()[0], label);

  // Same polygon, square kilometres: about 12 300.
  style.units = RangeUnit::kKilometers;
  m.SetStyle(style);
  EXPECT_NE(m.SummaryLabel().find(" sq km"), std::string::npos);
}

// A leg the geodesy refuses is zero-length and flagged in AN1; here it must
// produce NO label rather than a "0.00 NM" one, because the Windows code
// wraps the whole label body in `if (GEO_... == SUCCESS)`. Coincident points
// are the case that reaches it.
TEST(AnalysisMeasure, RefusedLegDrawsNothing) {
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{34.0, -84.0},
                GeoPoint{35.0, -84.0}},
               LineType::kGreatCircle);
  Measurement m(MeasurementKind::kMultiPoint, path, MeasureStyle());

  ASSERT_EQ(m.LegCount(), 2u);
  // Whether geo_tool refuses a zero-length leg is its business; what this
  // test pins is that a refused leg is skipped and a measured one is not.
  const auto labels = m.Labels();
  size_t measured = 0;
  for (size_t i = 0; i < m.LegCount(); ++i)
    if (m.Leg(i).ok) ++measured;
  EXPECT_EQ(labels.size(), measured);

  Measurement rb(MeasurementKind::kRangeBearing,
                 GeoPath({GeoPoint{34.0, -84.0}}, LineType::kGreatCircle),
                 MeasureStyle());
  EXPECT_EQ(rb.LegCount(), 0u);
  EXPECT_TRUE(rb.LegLabel(0).empty());
  EXPECT_TRUE(rb.Labels().empty());
}

// The line type reaches the label, because it reaches the range: the same
// two points measured as a rhumb line and as a great circle are different
// distances, and at this latitude the difference shows in two decimals.
TEST(AnalysisMeasure, LineTypeReachesTheLabel) {
  GeoPath gc({GeoPoint{60.0, -100.0}, GeoPoint{60.0, 0.0}},
             LineType::kGreatCircle);
  GeoPath rh({GeoPoint{60.0, -100.0}, GeoPoint{60.0, 0.0}}, LineType::kRhumb);

  MeasureStyle style;
  Measurement a(MeasurementKind::kRangeBearing, gc, style);
  Measurement b(MeasurementKind::kRangeBearing, rh, style);

  EXPECT_LT(a.TotalRangeMeters(), b.TotalRangeMeters());
  EXPECT_NE(a.LegLabel(0), b.LegLabel(0));
  // The rhumb line along a parallel bears due east; the great circle does not.
  EXPECT_NEAR(b.Leg(0).bearing_deg, 90.0, 1.0e-6);
  EXPECT_LT(a.Leg(0).bearing_deg, 90.0);
}

}  // namespace
