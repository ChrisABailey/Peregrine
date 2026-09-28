// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/beach.h"

#include <algorithm>
#include <limits>

namespace fv {
namespace nav {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// The earliest start in [from, from + horizon] of a window at or below
/// `threshold_m` at least `need_s` long; NaN if there is none in the table.
double NextWindow(const TideTable& table, double threshold_m, double from,
                  double need_s, double horizon_s) {
  const double latest_start = from + horizon_s;
  // Searched past the horizon so a window that opens inside it is not cut short.
  const double to = std::min(latest_start + need_s, table.ValidUntil());
  std::vector<TideWindow> windows;
  if (to < from || !table.WindowsBelow(threshold_m, from, to, &windows).ok()) return kNaN;
  for (const TideWindow& w : windows) {
    if (w.begin_s > latest_start) break;
    if (w.end_s - w.begin_s >= need_s) return w.begin_s;
  }
  return kNaN;
}

BeachStretchVerdict Judge(const BeachStretchTiming& stretch, const TideTable* table,
                          double depart_s, const BeachTideLimits& limits) {
  BeachStretchVerdict v;
  v.enter_at_s = depart_s + stretch.enter_s;
  v.exit_at_s = depart_s + stretch.exit_s;
  if (table == nullptr || table->empty()) return v;

  const double a = v.enter_at_s;
  const double b = v.exit_at_s + limits.exit_margin_s;
  const double threshold = limits.rideable_below_m;
  const double need = b - a;
  v.passable_from_s = NextWindow(*table, threshold, a, need, limits.search_horizon_s);
  v.good_from_s = NextWindow(*table, limits.good_below_m, a, need, limits.search_horizon_s);

  double end_height = 0.0;
  if (!table->HeightAt(a, &v.enter_height_m).ok() || !table->HeightAt(b, &end_height).ok()) {
    v.enter_height_m = kNaN;
    return v;
  }

  // Each half-cycle is monotonic, so the peak is at an end or at a high water.
  v.peak_m = v.enter_height_m;
  v.peak_at_s = a;
  if (end_height > v.peak_m) {
    v.peak_m = end_height;
    v.peak_at_s = b;
  }
  for (const TideExtreme& e : table->Extremes(a, b)) {
    if (e.high && e.height_m > v.peak_m) {
      v.peak_m = e.height_m;
      v.peak_at_s = e.time_s;
    }
  }

  if (v.enter_height_m <= threshold && v.peak_m > threshold) {
    std::vector<TideWindow> below;
    table->WindowsBelow(threshold, a, b, &below);
    // No window at all means the water touched the threshold on arrival while rising.
    v.covered_at_s = below.empty() ? a : below.front().end_s;
  }

  if (v.peak_m > threshold)
    v.verdict = BeachVerdict::kPoor;
  else if (v.peak_m > limits.good_below_m)
    v.verdict = BeachVerdict::kMarginal;
  else
    v.verdict = BeachVerdict::kGood;
  return v;
}

}  // namespace

BeachStretchVerdict::BeachStretchVerdict()
    : enter_height_m(kNaN),
      peak_m(kNaN),
      peak_at_s(kNaN),
      covered_at_s(kNaN),
      passable_from_s(kNaN),
      good_from_s(kNaN) {}

std::vector<BeachStretchVerdict> BeachTideVerdicts(
    const std::vector<BeachStretchTiming>& stretches, const TideTable* table,
    double depart_s, const BeachTideLimits& limits) {
  std::vector<BeachStretchVerdict> out;
  out.reserve(stretches.size());
  for (const BeachStretchTiming& s : stretches) out.push_back(Judge(s, table, depart_s, limits));
  return out;
}

}  // namespace nav
}  // namespace fv
