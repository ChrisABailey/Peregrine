// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_profile.h — the route elevation profile chart, as a layout a
/// native shell draws.
///
/// The C++ form of PythonView's `ProfileWindow` (port/apps/analysis.py): the
/// samples come from `analysis::SampleTerrainProfile`, and everything a chart
/// needs short of the drawing itself — plot box, axis ticks and labels, the
/// terrain in contiguous runs, the waypoint markers, the summary line and the
/// hover readout — is computed here, so the macOS and GTK windows draw the
/// same chart from the same numbers.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fvkit/analysis/measure.h"
#include "fvkit/analysis/path.h"
#include "fvkit/analysis/profile.h"

namespace fv {

class RouteOverlay;

namespace desk {

/// The route as a path: the planned road legs joined end to end when the
/// route follows roads, else its waypoints. Rhumb legs give a rhumb path.
/// nullopt with fewer than two points. `waypoints`, when given, receives the
/// path vertex index of each waypoint (each leg's end on a planned path).
std::optional<analysis::GeoPath> RouteGeoPath(const RouteOverlay& route,
                                              std::vector<size_t>* waypoints = nullptr);

/// The steepest grade drawn at full chart height. A flatter path gets a
/// taller elevation window, so level ground does not fill the chart.
constexpr double kFullScaleGrade = 0.02;

/// (lo, hi) for the elevation axis in display units (`per_m` per metre):
/// the data range plus 8% each side, widened about its middle to at least
/// `kFullScaleGrade` of the path length.
std::pair<double, double> ElevationWindow(double min_m, double max_m, double total_distance_m,
                                          double per_m);

/// "nm", "miles", "km", "m", "yards" or "ft" (any case); nautical miles otherwise.
analysis::RangeUnit ParseRangeUnit(const std::string& name);

/// A point in the chart's own coordinates: points, top-left origin.
struct ChartPoint {
  double x = 0;
  double y = 0;
};

/// One labelled axis tick; `pos` is x on the distance axis, y on the elevation axis.
struct ChartTick {
  double pos = 0;
  std::string label;
};

/// What to draw for one chart size. With an empty `runs` and no plot, only
/// `message` is drawn, centred.
struct ProfileLayout {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  ///< the plot box
  bool has_plot = false;
  std::vector<ChartTick> x_ticks;
  std::vector<ChartTick> y_ticks;
  /// Terrain in runs of known samples; a run ends at a sample with no data.
  /// A run of one point is drawn as a dot.
  std::vector<std::vector<ChartPoint>> runs;
  /// x of each interior marker (waypoint), for a dashed vertical line.
  std::vector<double> vertex_x;
  std::string message;
};

/// A sampled profile and the units it is labelled in.
class ProfileChart {
 public:
  static constexpr double kPadLeft = 62, kPadRight = 30, kPadTop = 12, kPadBottom = 34;

  /// Samples `src` along `path` with `samples` evenly spaced points (at least 3).
  /// `markers` are path vertex indices to mark, such as the waypoints. A null
  /// `src` leaves the chart empty with a message saying DTED is missing.
  void Sample(IElevationSource* src, const analysis::GeoPath& path, int samples,
              const std::vector<size_t>& markers = {});
  /// Empties the chart; `message` is what it shows instead.
  void Clear(std::string message);

  void SetUnits(analysis::RangeUnit units) { style_.units = units; }
  analysis::RangeUnit units() const { return style_.units; }
  /// Elevation unit name and multiplier from metres: "m" for km and m, "ft" otherwise.
  std::pair<const char*, double> ElevationUnit() const;

  bool HasResult() const { return result_.has_value(); }
  const analysis::ProfileResult* result() const { return result_ ? &*result_ : nullptr; }

  ProfileLayout Layout(double width_pt, double height_pt) const;
  /// "2.41 NM  ·  min 3 ft  ·  max 18 ft  ·  gain 40 ft  ·  loss 37 ft  ·  200 samples".
  std::string Stats() const;
  /// The nearest sample to `x_pt` in a chart of that size: distance, elevation
  /// and position. "" outside the plot or without a result.
  std::string ReadoutAt(double x_pt, double width_pt, double height_pt) const;

 private:
  std::optional<analysis::ProfileResult> result_;
  std::vector<double> markers_m_;
  std::string message_;
  analysis::MeasureStyle style_;
};

}  // namespace desk
}  // namespace fv
