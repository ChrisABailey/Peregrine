// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/analysis/path.h"

#include <algorithm>
#include <cmath>

#include "geo_tool.h"  // fv_geo_tool: range/bearing and end point, both lines

namespace fv {
namespace analysis {
namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

boolean_t GreatCircleFlag(LineType t) {
  return t == LineType::kGreatCircle ? TRUE : FALSE;
}

}  // namespace

GeoPath::GeoPath(std::vector<GeoPoint> points, LineType type)
    : points_(std::move(points)), line_type_(type) {
  Remeasure();
}

void GeoPath::SetPoints(std::vector<GeoPoint> points) {
  points_ = std::move(points);
  Remeasure();
}

void GeoPath::SetLineType(LineType type) {
  line_type_ = type;
  Remeasure();
}

void GeoPath::Remeasure() {
  legs_.clear();
  cumulative_.clear();
  if (points_.empty()) return;

  cumulative_.reserve(points_.size());
  cumulative_.push_back(0.0);
  legs_.reserve(points_.size() - 1);

  const boolean_t gc = GreatCircleFlag(line_type_);
  for (size_t i = 0; i + 1 < points_.size(); ++i) {
    PathLeg leg;
    double range = 0.0;
    double bearing = 0.0;
    if (GEO_calc_range_and_bearing(points_[i].lat, points_[i].lon,
                                   points_[i + 1].lat, points_[i + 1].lon,
                                   &range, &bearing, gc) == SUCCESS) {
      leg.range_m = range;
      leg.bearing_deg = bearing;
      leg.ok = true;
    }
    // A refused leg stays zero-length and !ok: the cumulative array must keep
    // one entry per vertex or every index into it is off by however many legs
    // failed.
    cumulative_.push_back(cumulative_.back() + leg.range_m);
    legs_.push_back(leg);
  }
}

bool GeoPath::Measured() const {
  if (points_.empty()) return false;
  for (const PathLeg& leg : legs_) {
    if (!leg.ok) return false;
  }
  return true;
}

bool GeoPath::PointAtDistance(double distance_m, GeoPoint* out) const {
  if (out == nullptr || points_.empty()) return false;
  if (points_.size() == 1 || !(distance_m > 0.0)) {
    *out = points_.front();
    return true;
  }
  if (distance_m >= TotalLength()) {
    *out = points_.back();
    return true;
  }

  // The last leg whose start is at or before `distance_m`. upper_bound gives
  // the first vertex strictly past it; the leg is the one before that.
  const auto it =
      std::upper_bound(cumulative_.begin(), cumulative_.end(), distance_m);
  size_t leg_index = static_cast<size_t>(it - cumulative_.begin());
  if (leg_index == 0) leg_index = 1;
  --leg_index;
  if (leg_index >= legs_.size()) leg_index = legs_.size() - 1;

  const PathLeg& leg = legs_[leg_index];
  if (!leg.ok) return false;

  const double into_leg = distance_m - cumulative_[leg_index];
  if (into_leg <= 0.0) {
    *out = points_[leg_index];
    return true;
  }

  double lat = 0.0;
  double lon = 0.0;
  if (GEO_calc_end_point(points_[leg_index].lat, points_[leg_index].lon,
                         into_leg, leg.bearing_deg, &lat, &lon,
                         GreatCircleFlag(line_type_)) != SUCCESS) {
    return false;
  }
  out->lat = lat;
  out->lon = lon;
  return true;
}

std::vector<GeoPoint> GeoPath::Densify(double step_m, size_t max_points) const {
  if (points_.size() < 2 || !(step_m > 0.0)) return points_;

  double step = step_m;
  if (max_points > points_.size()) {
    // Widen the step until the result fits. One point per leg boundary plus
    // the interior samples; solving exactly is not worth it, so ask for the
    // budget the interior samples have left and take the larger step.
    const size_t budget = max_points - points_.size();
    if (budget == 0) return points_;
    const double needed = TotalLength() / static_cast<double>(budget);
    step = std::max(step, needed);
  }

  std::vector<GeoPoint> out;
  out.push_back(points_.front());
  for (size_t i = 0; i + 1 < points_.size(); ++i) {
    const PathLeg& leg = legs_[i];
    if (leg.ok && leg.range_m > step) {
      const int n = static_cast<int>(std::ceil(leg.range_m / step));
      for (int k = 1; k < n; ++k) {
        const double into_leg = leg.range_m * k / n;
        double lat = 0.0;
        double lon = 0.0;
        if (GEO_calc_end_point(points_[i].lat, points_[i].lon, into_leg,
                              leg.bearing_deg, &lat, &lon,
                              GreatCircleFlag(line_type_)) == SUCCESS) {
          out.push_back(GeoPoint{lat, lon});
        }
      }
    }
    // The vertex itself, always, and exact.
    out.push_back(points_[i + 1]);
  }
  return out;
}

std::vector<GeoPoint> GeoPath::Resample(size_t count) const {
  std::vector<GeoPoint> out;
  if (points_.empty() || count == 0) return out;
  if (count == 1 || points_.size() == 1) {
    out.push_back(points_.front());
    return out;
  }

  out.reserve(count);
  const double total = TotalLength();
  for (size_t i = 0; i < count; ++i) {
    const double d = total * static_cast<double>(i) / (count - 1);
    GeoPoint p;
    if (PointAtDistance(d, &p)) {
      out.push_back(p);
    } else {
      // A refused leg: hold the last good point rather than shortening the
      // series, so index i is still "the i-th of count samples".
      out.push_back(out.empty() ? points_.front() : out.back());
    }
  }
  // The ends are the path's own, to the last digit — an interpolation that
  // lands 3 mm off the endpoint makes a profile's first and last samples
  // disagree with the elevation readout at the same place.
  out.front() = points_.front();
  out.back() = points_.back();
  return out;
}

double GeoPath::AreaSquareMeters() const {
  if (points_.size() < 3) return 0.0;

  const boolean_t gc = GreatCircleFlag(line_type_);
  const GeoPoint& origin = points_.front();

  // Project into a local plane about vertex 0. Vertex 0 is the origin, so the
  // shoelace's first and last terms vanish and the sum runs over the fan of
  // triangles (v0, vi, vi+1) — which is exactly what AreaToolObj does.
  double area = 0.0;
  for (size_t i = 1; i + 1 < points_.size(); ++i) {
    double r1 = 0.0;
    double b1 = 0.0;
    double r2 = 0.0;
    double b2 = 0.0;
    if (GEO_calc_range_and_bearing(origin.lat, origin.lon, points_[i].lat,
                                   points_[i].lon, &r1, &b1, gc) != SUCCESS) {
      continue;
    }
    if (GEO_calc_range_and_bearing(origin.lat, origin.lon, points_[i + 1].lat,
                                   points_[i + 1].lon, &r2, &b2,
                                   gc) != SUCCESS) {
      continue;
    }
    const double x1 = r1 * std::sin(b1 * kDegToRad);
    const double y1 = r1 * std::cos(b1 * kDegToRad);
    const double x2 = r2 * std::sin(b2 * kDegToRad);
    const double y2 = r2 * std::cos(b2 * kDegToRad);
    area += x1 * y2 - x2 * y1;
  }
  return std::fabs(area / 2.0);
}

}  // namespace analysis
}  // namespace fv
