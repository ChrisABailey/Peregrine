// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file fvkit/nav/tide.h
 * A tide curve built from a table of predicted high and low waters.
 *
 * The table is NOAA's own predictions for one station, written by
 * port/tools/fetch_tides.py. Between two extremes the height follows NOAA's
 * cosine interpolation, so the curve passes exactly through every extreme and
 * is monotonic on each half-cycle. Times are Unix seconds (UTC); heights are
 * metres above the table's datum. Nothing outside [ValidFrom, ValidUntil] is
 * answered: such a query is kOutOfCoverage, never a clamp or an extrapolation.
 */

#ifndef FVKIT_NAV_TIDE_H_
#define FVKIT_NAV_TIDE_H_

#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace nav {

/// One predicted high or low water.
struct TideExtreme {
  double time_s = 0.0;
  double height_m = 0.0;
  bool high = false;
};

/// Where the table's predictions apply, as NOAA describes the station.
struct TideStation {
  std::string id;
  std::string name;
  double lat = 0.0;
  double lon = 0.0;
  /// The harmonic station whose extremes this one's are offset from; empty
  /// for a harmonic station.
  std::string reference_id;
};

/// Whether the water is rising, and how fast.
struct TideTrend {
  bool rising = false;
  /// Signed rate of change, metres per hour (negative when falling).
  double rate_m_per_h = 0.0;
};

/// A span of time, [begin_s, end_s].
struct TideWindow {
  double begin_s = 0.0;
  double end_s = 0.0;
};

/// A station's predicted tide over a fixed span, read from a tides.json.
class TideTable {
 public:
  /// Reads a tides.json. kNotFound if the file will not open.
  Status Load(const std::string& path);

  /// Parses a tides.json already in memory. kInvalidArg if it is malformed,
  /// if the extremes do not alternate high/low in time order, or if they do
  /// not enclose [valid_from, valid_until]; kUnsupported for units other
  /// than metres. On failure the table is left empty.
  Status Parse(const std::string& json_text);

  bool empty() const { return extremes_.empty(); }
  const TideStation& station() const { return station_; }
  /// The vertical datum heights are measured from, e.g. "MLLW".
  const std::string& datum() const { return datum_; }
  double ValidFrom() const { return valid_from_; }
  double ValidUntil() const { return valid_until_; }

  /// The predicted height at `t`.
  Status HeightAt(double t, double* height_m) const;

  /// Rising or falling at `t`. At an extreme itself the trend is the one
  /// that follows it, with a rate of zero.
  Status TrendAt(double t, TideTrend* trend) const;

  /// The extremes with t0 <= time <= t1, clipped to the valid span.
  std::vector<TideExtreme> Extremes(double t0, double t1) const;

  /// The spans within [t0, t1] where the height is at or below `threshold_m`,
  /// solved exactly on each half-cycle. Adjacent spans are merged, so a
  /// window runs from one crossing to the next. kOutOfCoverage if [t0, t1]
  /// is not inside the valid span, kInvalidArg if t1 < t0.
  Status WindowsBelow(double threshold_m, double t0, double t1,
                      std::vector<TideWindow>* windows) const;

 private:
  /// Index i of the half-cycle [extremes_[i], extremes_[i+1]) holding `t`;
  /// the caller has already checked `t` against the valid span.
  size_t SegmentAt(double t) const;
  Status CheckCoverage(double t) const;

  TideStation station_;
  std::string datum_;
  double valid_from_ = 0.0;
  double valid_until_ = 0.0;
  std::vector<TideExtreme> extremes_;
};

}  // namespace nav
}  // namespace fv

#endif  // FVKIT_NAV_TIDE_H_
