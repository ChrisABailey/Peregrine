// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/analysis/profile.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fv {
namespace analysis {
namespace {

// Metres per degree of latitude on the sphere geo_tool measures with (one
// nautical mile per minute of arc, 1852 m to the nautical mile — the same
// identity GeoPoint.cs leans on). Only ever used to turn a post SPACING into
// a step, never to measure anything.
constexpr double kMetersPerDegreeLat = 60.0 * 1852.0;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

double PostSpacingMeters(IElevationSource& src, const GeoPoint& at) {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  if (!src.PostSpacing(at, &lat_deg, &lon_deg)) return 0.0;
  if (!(lat_deg > 0.0) && !(lon_deg > 0.0)) return 0.0;

  const double lat_m = lat_deg * kMetersPerDegreeLat;
  const double lon_m =
      lon_deg * kMetersPerDegreeLat * std::cos(at.lat * kDegToRad);

  // The FINER of the two axes. The spacing along an arbitrary bearing lies
  // between them, so taking the smaller never coarsens a profile more than
  // the data warrants — and a DTED cell's east-west posts thin toward the
  // poles, which is exactly the case where one number would be wrong.
  double finest = std::numeric_limits<double>::max();
  if (lat_m > 0.0) finest = std::min(finest, lat_m);
  if (lon_m > 0.0) finest = std::min(finest, lon_m);
  return finest == std::numeric_limits<double>::max() ? 0.0 : finest;
}

}  // namespace

Status SampleTerrainProfile(IElevationSource& src, const GeoPath& path,
                            const ProfileOptions& opts, ProfileResult* out) {
  if (out == nullptr) {
    return Status::Error(kInvalidArg, "SampleTerrainProfile: null out");
  }
  *out = ProfileResult{};
  if (path.PointCount() == 0) {
    return Status::Error(kInvalidArg, "SampleTerrainProfile: empty path");
  }
  if (opts.sample_count == 0 && !(opts.step_m > 0.0)) {
    return Status::Error(kInvalidArg,
                         "SampleTerrainProfile: neither sample_count nor "
                         "step_m was given");
  }

  const double total = path.TotalLength();
  out->total_distance_m = total;

  // ---- the distances to sample at ------------------------------------
  std::vector<double> distances;
  std::vector<bool> vertex_flag;

  const bool by_count = opts.sample_count > 0;
  if (by_count) {
    const size_t n = opts.sample_count;
    if (n == 1 || total <= 0.0) {
      distances.push_back(0.0);
    } else {
      distances.reserve(n);
      for (size_t i = 0; i < n; ++i) {
        distances.push_back(total * static_cast<double>(i) / (n - 1));
      }
    }
    out->step_m = distances.size() > 1 ? total / (distances.size() - 1) : 0.0;
  } else {
    double step = opts.step_m;
    const GeoPoint mid = path.points()[path.PointCount() / 2];
    const double posts_m = PostSpacingMeters(src, mid);
    if (opts.clamp_to_post_spacing && posts_m > step) {
      step = posts_m;
      out->step_was_clamped = true;
    }
    if (opts.max_samples > 1 && total > 0.0) {
      const double needed = total / static_cast<double>(opts.max_samples - 1);
      if (needed > step) {
        step = needed;
        out->step_was_clamped = true;
      }
    }
    out->step_m = step;

    if (total <= 0.0) {
      distances.push_back(0.0);
    } else if (opts.include_vertices) {
      // Every vertex, exactly, plus interior samples no further apart than
      // the step. This is the shape the polyline profile needs.
      for (size_t i = 0; i + 1 < path.PointCount(); ++i) {
        const double d0 = path.CumulativeAt(i);
        distances.push_back(d0);
        vertex_flag.push_back(true);
        const PathLeg& leg = path.Leg(i);
        if (leg.ok && leg.range_m > step) {
          const int n = static_cast<int>(std::ceil(leg.range_m / step));
          for (int k = 1; k < n; ++k) {
            distances.push_back(d0 + leg.range_m * k / n);
            vertex_flag.push_back(false);
          }
        }
      }
      distances.push_back(total);
      vertex_flag.push_back(true);
    } else {
      for (double d = 0.0; d < total; d += step) {
        distances.push_back(d);
      }
      distances.push_back(total);
    }
  }

  if (vertex_flag.empty()) {
    // Mark the samples that landed on a turning point. A tolerance rather
    // than equality: an evenly-spaced series hits a vertex only by luck, and
    // "within a millimetre of the vertex" is the vertex for a chart.
    vertex_flag.assign(distances.size(), false);
    size_t v = 0;  // both series ascend, so one walk settles it
    for (size_t i = 0; i < distances.size(); ++i) {
      while (v + 1 < path.PointCount() &&
             path.CumulativeAt(v) < distances[i] - 1e-3) {
        ++v;
      }
      if (std::fabs(distances[i] - path.CumulativeAt(v)) < 1e-3) {
        vertex_flag[i] = true;
      }
    }
  }

  // ---- read the terrain ----------------------------------------------
  out->points.reserve(distances.size());
  double min_m = std::numeric_limits<double>::max();
  double max_m = std::numeric_limits<double>::lowest();
  bool have_any = false;
  double last_known = 0.0;
  bool have_last = false;

  for (size_t i = 0; i < distances.size(); ++i) {
    ProfilePoint pp;
    pp.distance_m = distances[i];
    pp.is_vertex = vertex_flag[i];
    pp.elevation_m = std::numeric_limits<float>::quiet_NaN();

    if (!path.PointAtDistance(distances[i], &pp.at)) {
      // A leg the geodesy refused: the sample exists (the x-axis is still
      // continuous) but there is nowhere to read.
      ++out->no_data_count;
      out->points.push_back(pp);
      continue;
    }

    float elev = 0.0f;
    const Status s = src.GetElevation(pp.at, &elev);
    if (s.ok() && !std::isnan(elev)) {
      pp.elevation_m = elev;
      pp.has_data = true;
      have_any = true;
      min_m = std::min(min_m, static_cast<double>(elev));
      max_m = std::max(max_m, static_cast<double>(elev));
      if (have_last) {
        const double d = elev - last_known;
        if (d > 0.0) {
          out->gain_m += d;
        } else {
          out->loss_m -= d;
        }
      }
      last_known = elev;
      have_last = true;
    } else {
      // Void post, read error or outside the coverage — all one thing to a
      // chart, which is a gap in the line.
      ++out->no_data_count;
    }
    out->points.push_back(pp);
  }

  if (have_any) {
    out->min_m = min_m;
    out->max_m = max_m;
  }
  return Status::Ok();
}

}  // namespace analysis
}  // namespace fv
