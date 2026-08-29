// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The graticule spacing table — see fvkit/overlay/grid_spacing.h.
// Transcribed from Applications/FalconView/grid_map/spacing.cpp.

#include "fvkit/overlay/grid_spacing.h"

namespace fv {

namespace {

constexpr double Min(double minutes) { return minutes / 60.0; }
constexpr double Sec(double seconds) { return seconds / 3600.0; }

// {major line, minor line, major tick, minor tick}, degrees.
constexpr GraticuleSpacingRow Row(double lat_major, double lat_minor,
                                  double lat_majtick, double lat_mintick,
                                  double lon_major, double lon_minor,
                                  double lon_majtick, double lon_mintick) {
  return GraticuleSpacingRow{
      {lat_major, lat_minor, lat_majtick, lat_mintick},
      {lon_major, lon_minor, lon_majtick, lon_mintick}};
}

}  // namespace

const ScaleTable<GraticuleSpacingRow>& GraticuleSpacingTable() {
  // Function-local static: the same "corrects a static order initialization
  // problem" reason the original wrapped its scale table in a function.
  static const ScaleTable<GraticuleSpacingRow> table({
      {100000000.0, Row(60, 30, 10, 5,          60, 30, 10, 5)},
      { 50000000.0, Row(45, 15,  5, 1,          45, 15,  5, 1)},
      { 20000000.0, Row(30, 10,  5, 1,          30, 10,  5, 1)},
      { 10000000.0, Row(10,  5,  1, Min(12),    10,  5,  1, Min(12))},
      {  5000000.0, Row( 5,  1, Min(15), Min(5), 5,  1, Min(15), Min(5))},
      // The one asymmetric row: 3 degrees of longitude against 1 of latitude,
      // because at 1:2M a 1-degree meridian spacing puts the labels on top of
      // each other away from the equator.
      {  2000000.0, Row( 1,  1, Min(10), Min(2), 3,  1, Min(10), Min(5))},
      {  1000000.0, Row( 1,  1, Min(5), Min(1),  1,  1, Min(5), Min(1))},
      {   500000.0, Row(Min(30), Min(30), Min(5), Min(1),
                        Min(30), Min(30), Min(5), Min(1))},
      {   200000.0, Row(Min(15), Min(15), Min(5), Min(1),
                        Min(15), Min(15), Min(5), Min(1))},
      // From here in: major == minor (every line is a major line) and no ticks.
      {   100000.0, Row(Min(5), Min(5), 0, 0,  Min(5), Min(5), 0, 0)},
      {    50000.0, Row(Min(1), Min(1), 0, 0,  Min(1), Min(1), 0, 0)},
      {    20000.0, Row(Min(1), Min(1), 0, 0,  Min(1), Min(1), 0, 0)},
      {    10000.0, Row(Sec(15), Sec(15), 0, 0,  Sec(15), Sec(15), 0, 0)},
      {     5000.0, Row(Sec(10), Sec(10), 0, 0,  Sec(10), Sec(10), 0, 0)},
      {     2000.0, Row(Sec(5), Sec(5), 0, 0,  Sec(5), Sec(5), 0, 0)},
      {     1000.0, Row(Sec(5), Sec(5), 0, 0,  Sec(5), Sec(5), 0, 0)},
  });
  return table;
}

GraticuleSpacing GraticuleSpacingFor(double scale_denominator, GridAxis axis) {
  const GraticuleSpacingRow* row =
      GraticuleSpacingTable().Nearest(scale_denominator);
  if (row == nullptr) return GraticuleSpacing{};
  return row->For(axis);
}

}  // namespace fv
