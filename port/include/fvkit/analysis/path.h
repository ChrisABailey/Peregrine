// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/analysis/path.h — a geographic path, and the arithmetic every
// analysis tool does over one.
//
// AN1 of port/analysis-plan.md. Ported from the Windows Range & Bearing
// overlay (Plugins/LegacyOverlays/RangeBearing), where the same four lines of
// GEO_calc_range_and_bearing are written out again in CRangeBearingObject,
// CMultiPointRBObj, CTotalDistanceObj, AreaToolObj and Elevation_Chart —
// five copies, each with its own idea of what to do when a leg fails.
//
// Two things are load-bearing:
//
//   * THE LINE TYPE IS A PROPERTY OF THE PATH, not a global. Every
//     GEO_calc_range_and_bearing call in the Windows tree passes
//     `great_circle_not_rhumb_line` through from the object's own property
//     sheet, and a rhumb-line path measured as a great circle is a different
//     path. PointAtDistance interpolates along the leg's OWN type for the
//     same reason: the midpoint of a great-circle leg is on the great
//     circle, not on the rhumb line between its ends.
//
//   * A LEG THAT CANNOT BE MEASURED IS ZERO-LENGTH AND FLAGGED, never an
//     exception and never a silently dropped vertex. GEO_calc_range_and_bearing
//     fails on degenerate inputs (a pole on a rhumb line, coincident points);
//     Windows popped a message box from inside the geometry and gave up on
//     the whole object. Here the leg carries `ok == false`, the cumulative
//     distances still line up with the vertices, and the caller decides.
//
// Everything is METRES and DEGREES (contract D2/D4). Units and formatting are
// AN4's problem, not this file's.

#ifndef FVKIT_ANALYSIS_PATH_H_
#define FVKIT_ANALYSIS_PATH_H_

#include <cstddef>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace analysis {

// Which line a leg is measured and interpolated along. FalconView spells this
// as a `boolean_t great_circle_not_rhumb_line`; it is a two-valued choice a
// user makes in a property sheet, so it gets a name here.
enum class LineType {
  kGreatCircle,
  kRhumb,
};

// One leg, from vertex i to vertex i+1.
struct PathLeg {
  double range_m = 0.0;
  double bearing_deg = 0.0;  // true, at the START of the leg, [0, 360)
  bool ok = false;           // false when the geodesy call refused the pair
};

class GeoPath {
 public:
  GeoPath() = default;
  GeoPath(std::vector<GeoPoint> points, LineType type);

  // Replaces the vertices and re-measures every leg.
  void SetPoints(std::vector<GeoPoint> points);
  void SetLineType(LineType type);

  const std::vector<GeoPoint>& points() const { return points_; }
  LineType line_type() const { return line_type_; }

  size_t PointCount() const { return points_.size(); }
  size_t LegCount() const { return legs_.size(); }
  const PathLeg& Leg(size_t i) const { return legs_[i]; }

  // Distance from vertex 0 to vertex `i`, in metres. Defined for every
  // vertex, including 0 (which is 0.0), so `CumulativeAt(PointCount() - 1)`
  // is TotalLength().
  double CumulativeAt(size_t i) const { return cumulative_[i]; }
  double TotalLength() const {
    return cumulative_.empty() ? 0.0 : cumulative_.back();
  }

  // True when every leg measured. A single-vertex path is trivially measured
  // and a zero-vertex path is not a path.
  bool Measured() const;

  // The point `distance_m` along the path from vertex 0, interpolated along
  // the containing leg's own line type. Distances before the start clamp to
  // the first vertex and distances past the end to the last, because a caller
  // stepping by a fixed interval will overshoot the end by less than one step
  // and that is not an error.
  //
  // Returns false only for an empty path or a leg the geodesy refused.
  bool PointAtDistance(double distance_m, GeoPoint* out) const;

  // Every vertex, plus enough interpolated points that no gap exceeds
  // `step_m`. Vertices are ALWAYS present and always exact — the profile of a
  // polyline has to show its turning points, whatever the step.
  //
  // A step <= 0 returns the vertices unchanged. `max_points` caps the result
  // (the step is widened, the path is never truncated); 0 means no cap.
  std::vector<GeoPoint> Densify(double step_m, size_t max_points = 0) const;

  // `count` points evenly spaced by DISTANCE from end to end, first and last
  // exactly the path's own ends. This is the Windows "segments" box: the
  // vertices in between are NOT guaranteed to be sample points, which is why
  // Densify exists beside it.
  std::vector<GeoPoint> Resample(size_t count) const;

  // Polygon area in square metres, treating the vertices as a closed ring.
  //
  // Ported from AreaToolObj::caculate_Area: project every vertex into a local
  // plane about vertex 0 (x = r sin(bearing), y = r cos(bearing)), run the
  // shoelace, halve, take the magnitude. It is a local-tangent-plane
  // approximation and it is what the numbers in existing FalconView overlays
  // were produced with, so it is transcribed rather than improved. Accurate
  // for the sizes a user drags out; do not reach for it for a country.
  //
  // Fewer than three vertices is zero area, not an error.
  double AreaSquareMeters() const;

 private:
  void Remeasure();

  std::vector<GeoPoint> points_;
  std::vector<PathLeg> legs_;
  std::vector<double> cumulative_;
  LineType line_type_ = LineType::kGreatCircle;
};

}  // namespace analysis
}  // namespace fv

#endif  // FVKIT_ANALYSIS_PATH_H_
