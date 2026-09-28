// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file fvkit/nav/beach.h
 * Whether the tide lets a rider along a beach stretch at the time they reach it.
 *
 * A stretch is timed relative to the route's departure; the verdict takes the
 * highest predicted water from entering the stretch to leaving it plus a
 * margin. Times are Unix seconds (UTC) and heights metres above the table's
 * datum, as in fvkit/nav/tide.h. Nothing is guessed outside the table's span.
 */

#ifndef FVKIT_NAV_BEACH_H_
#define FVKIT_NAV_BEACH_H_

#include <vector>

#include "fvkit/nav/tide.h"

namespace fv {
namespace nav {

/// How the water treats one beach stretch.
enum class BeachVerdict {
  kGood,      ///< Peak water at or below `good_below_m`.
  kMarginal,  ///< Peak above `good_below_m` but not above `rideable_below_m`.
  kPoor,      ///< Peak above `rideable_below_m`: the stretch is not passable.
  kUnknown,   ///< No table, or some of the stretch falls outside its span.
};

/// The limits a stretch is judged against, from `[beach]` in pippin.ini.
/// Heights are metres above the table's datum.
struct BeachTideLimits {
  /// The beach is passable while the water is at or below this height.
  /// Infinity makes it passable at any tide, so no stretch is ever kPoor.
  double rideable_below_m = 0.5;
  /// Peaks above this height are marginal (soft sand likely). At or below
  /// `rideable_below_m`.
  double good_below_m = 0.35;
  /// Slack after the stretch ends that must also stay passable.
  double exit_margin_s = 600.0;
  /// How far past arrival the next passable or good window is looked for.
  double search_horizon_s = 48.0 * 3600.0;
};

/// One stretch's time on the sand, in seconds from the route's departure.
struct BeachStretchTiming {
  double enter_s = 0.0;
  double exit_s = 0.0;
};

/// The verdict on one stretch. Times are absolute; a NaN field is "none".
struct BeachStretchVerdict {
  BeachVerdict verdict = BeachVerdict::kUnknown;
  /// When the rider reaches and leaves the stretch.
  double enter_at_s = 0.0;
  double exit_at_s = 0.0;
  /// The water on arrival.
  double enter_height_m;
  /// The highest water over [enter_at_s, exit_at_s + exit_margin_s], and when.
  double peak_m;
  double peak_at_s;
  /// When the water rises above the threshold within that span; NaN if it
  /// does not, or if it was already above it on arrival.
  double covered_at_s;
  /// The earliest time at or after arrival the stretch could be entered and
  /// ridden, margin included, with the water at or below `rideable_below_m`
  /// (`passable_from_s`) or `good_below_m` (`good_from_s`); NaN if
  /// no such window starts within the search horizon and the table's span.
  double passable_from_s;
  double good_from_s;

  BeachStretchVerdict();
};

/// Judges each stretch for a departure at `depart_s`. A null or empty table
/// makes every verdict kUnknown.
std::vector<BeachStretchVerdict> BeachTideVerdicts(
    const std::vector<BeachStretchTiming>& stretches, const TideTable* table,
    double depart_s, const BeachTideLimits& limits);

}  // namespace nav
}  // namespace fv

#endif  // FVKIT_NAV_BEACH_H_
