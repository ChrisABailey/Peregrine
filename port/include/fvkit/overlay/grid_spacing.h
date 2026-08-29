// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/overlay/grid_spacing.h — how far apart the graticule's lines go, per
// map scale. FalconView's grid_map/spacing.cpp, as data.
//
// THIS TABLE IS THE PRODUCT. The numbers are cartographers' choices made in
// 1999 and unchanged since; they are not a formula and they do not want to be
// one. Two properties give the game away: latitude and longitude differ at
// 1:2M (1 degree of latitude but 3 of longitude, because a degree of longitude
// is short at mid-latitudes and its labels would collide), and the ladder is
// not geometric anywhere -- 45, 15, 5, 1 then straight to 15 minutes. Anything
// that computes a "nice interval" from pixels per degree produces a different,
// worse chart. See fvkit/scale_table.h for the general form of this argument.
//
// FOUR NUMBERS PER AXIS, and the hierarchy is the whole design:
//   major line -- drawn heavy, always labelled
//   minor line -- drawn light, labelled only when minor labels are on
//   major tick / minor tick -- unlabelled marks ALONG the lines, subdividing
//     them further without adding a line to the picture. Zero means none, and
//     from 1:100K in they are all zero: at that scale the minor line spacing
//     has already reached 5 minutes and a tick would be noise.
//
// From 1:100K in, major and minor spacing are EQUAL, which is how the original
// got its "every line is a major line at large scale" behaviour. It spelled
// that as an explicit `scale >= ONE_TO_100K` test in the draw loop; here it
// falls out of the table, so there is no second rule to keep in step.
//
// TWO ROWS FROM THE ORIGINAL ARE DELIBERATELY ABSENT.
//   WORLD -- FalconView's world-overview scale has no denominator at all (its
//     degrees-per-pixel is 90/surface_height, so it changes with the window),
//     and ScaleTable is keyed on denominators. Anything coarser than 1:100M
//     clamps to the 1:100M row, whose 60/30 degree graticule is a perfectly
//     good world grid. The lost quirk was WORLD's 90-degree longitude major,
//     which only ever made sense pole-to-pole.
//   NULL_SCALE -- a sentinel row of zeros meaning "no grid", sitting past the
//     finest real scale. Clamping to the 1:1K row (5 arc-seconds) is the
//     better answer for a map zoomed in past 1:1K.

#ifndef FVKIT_OVERLAY_GRID_SPACING_H_
#define FVKIT_OVERLAY_GRID_SPACING_H_

#include "fvkit/scale_table.h"

namespace fv {

// Which family of lines. A parallel is a line of constant LATITUDE and is
// spaced by the latitude row; a meridian by the longitude row.
enum class GridAxis {
  kLatitude = 0,   // parallels
  kLongitude,      // meridians
};

// All four spacings for one axis, in degrees. 0 = draw none of that kind.
struct GraticuleSpacing {
  double major_line_deg = 0.0;
  double minor_line_deg = 0.0;
  double major_tick_deg = 0.0;
  double minor_tick_deg = 0.0;

  bool has_lines() const {
    return major_line_deg > 0.0 && minor_line_deg > 0.0;
  }
  bool has_ticks() const {
    return major_tick_deg > 0.0 && minor_tick_deg > 0.0;
  }
};

// One row of the table: both axes at one scale.
struct GraticuleSpacingRow {
  GraticuleSpacing lat;
  GraticuleSpacing lon;

  const GraticuleSpacing& For(GridAxis axis) const {
    return axis == GridAxis::kLatitude ? lat : lon;
  }
};

// The table itself, built once. Sixteen rows, 1:100M through 1:1K.
const ScaleTable<GraticuleSpacingRow>& GraticuleSpacingTable();

// The spacing to use at `scale_denominator` for one axis. Never fails: a scale
// off either end of the table clamps to that end.
GraticuleSpacing GraticuleSpacingFor(double scale_denominator, GridAxis axis);

}  // namespace fv

#endif  // FVKIT_OVERLAY_GRID_SPACING_H_
