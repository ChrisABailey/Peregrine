// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/maneuver.h"

#include <algorithm>
#include <cmath>

#include "geo_tool.h"  // fv_geo_tool: GEO_calc_range_and_bearing

namespace fv {
namespace nav {
namespace {

constexpr double kManeuverDegToRad = 3.14159265358979323846 / 180.0;
// THE PORT'S METRE (WGS-84 mean radius), the one port/Routing measures arc
// lengths in and road_snap.h and the trip computer project in. geo_tool's
// range is ellipsoidal and differs from it by about 0.3 percent, which over a
// 2 km route is 6 m — enough to put a countdown past the end of the line it is
// counting down. Bearings still come from geo_tool; only the ranges are here.
constexpr double kManeuverEarthRadiusM = 6371008.8;

double GreatCircleMeters(const GeoPoint& a, const GeoPoint& b) {
  const double phi1 = a.lat * kManeuverDegToRad;
  const double phi2 = b.lat * kManeuverDegToRad;
  const double dphi = (b.lat - a.lat) * kManeuverDegToRad;
  const double dlam = NormalizeLon(b.lon - a.lon) * kManeuverDegToRad;
  const double s1 = std::sin(dphi * 0.5);
  const double s2 = std::sin(dlam * 0.5);
  double h = s1 * s1 + std::cos(phi1) * std::cos(phi2) * s2 * s2;
  if (h > 1.0) h = 1.0;
  return 2.0 * kManeuverEarthRadiusM * std::asin(std::sqrt(h));
}

// True bearing from `a` to `b`, great circle. Returns false for a pair the
// geodesy refuses, which for this file's purposes means coincident points.
bool BearingBetween(const GeoPoint& a, const GeoPoint& b, double* out) {
  double range = 0.0;
  double bearing = 0.0;
  if (GEO_calc_range_and_bearing(a.lat, a.lon, b.lat, b.lon, &range, &bearing,
                                 TRUE) != SUCCESS) {
    return false;
  }
  *out = bearing;
  return true;
}

// Into (-180, +180].
double SignedDelta(double from_deg, double to_deg) {
  double d = std::fmod(to_deg - from_deg, 360.0);
  if (d <= -180.0) d += 360.0;
  if (d > 180.0) d -= 360.0;
  return d;
}

ManeuverType Classify(double turn_deg, const ManeuverSettings& s) {
  const double mag = std::fabs(turn_deg);
  if (mag >= s.u_turn_deg) return ManeuverType::kUTurn;
  const bool right = turn_deg > 0.0;
  if (mag >= s.sharp_deg)
    return right ? ManeuverType::kSharpRight : ManeuverType::kSharpLeft;
  if (mag >= s.slight_deg)
    return right ? ManeuverType::kRight : ManeuverType::kLeft;
  return right ? ManeuverType::kSlightRight : ManeuverType::kSlightLeft;
}

// The farthest point from `at` in `dir` that is still inside the window and
// not past `limit`, preferring distance but never returning `at` itself: a
// coincident neighbour carries no bearing, so the walk keeps going until the
// points separate. Returns false when every point that way is coincident.
bool WindowEnd(const std::vector<double>& cum, size_t at, size_t limit, int dir,
               double window_m, size_t* out) {
  size_t best = at;
  size_t k = at;
  while (k != limit) {
    k = dir > 0 ? k + 1 : k - 1;
    const double span = std::fabs(cum[k] - cum[at]);
    if (span <= 0.0) continue;      // coincident so far; keep walking
    best = k;
    if (span >= window_m) break;    // far enough to be a road, not a jitter
  }
  if (best == at) return false;
  *out = best;
  return true;
}

}  // namespace

const char* ManeuverTypeName(ManeuverType type) {
  switch (type) {
    case ManeuverType::kDepart: return "depart";
    case ManeuverType::kStraight: return "straight";
    case ManeuverType::kSlightLeft: return "slight left";
    case ManeuverType::kLeft: return "left";
    case ManeuverType::kSharpLeft: return "sharp left";
    case ManeuverType::kSlightRight: return "slight right";
    case ManeuverType::kRight: return "right";
    case ManeuverType::kSharpRight: return "sharp right";
    case ManeuverType::kUTurn: return "u-turn";
    case ManeuverType::kArrive: return "arrive";
  }
  return "?";
}

std::vector<Maneuver> BuildManeuvers(const RouteShape& shape,
                                     const ManeuverSettings& settings) {
  std::vector<Maneuver> out;
  const std::vector<GeoPoint>& g = shape.geometry;
  if (g.size() < 2) return out;
  const size_t last = g.size() - 1;

  std::vector<double> cum(g.size(), 0.0);
  for (size_t i = 0; i + 1 < g.size(); ++i)
    cum[i + 1] = cum[i] + GreatCircleMeters(g[i], g[i + 1]);

  // Candidate junctions: every leg boundary strictly inside the line, with the
  // leg that starts there. A leg beginning at 0 or at the last point is the
  // route's own end and is handled by depart/arrive.
  struct Candidate {
    size_t index;
    const RouteShape::Leg* leg;
  };
  std::vector<Candidate> candidates;
  for (size_t i = 1; i < shape.legs.size(); ++i) {
    const size_t idx = shape.legs[i].geometry_begin;
    if (idx == 0 || idx >= last) continue;
    if (!candidates.empty() && candidates.back().index == idx) continue;
    candidates.push_back({idx, &shape.legs[i]});
  }

  const RouteShape::Leg* first_leg =
      shape.legs.empty() ? nullptr : &shape.legs.front();
  {
    Maneuver depart;
    depart.at = g.front();
    depart.distance_m = 0.0;
    depart.type = ManeuverType::kDepart;
    if (first_leg != nullptr) {
      depart.road = first_leg->name;
      depart.klass = first_leg->klass;
    }
    depart.geometry_index = 0;
    out.push_back(depart);
  }

  for (size_t c = 0; c < candidates.size(); ++c) {
    const size_t idx = candidates[c].index;
    // The window stops at the neighbouring candidates: reaching through the
    // next corner would average two turns into one angle that is neither.
    const size_t back_limit = c == 0 ? 0 : candidates[c - 1].index;
    const size_t fwd_limit = c + 1 < candidates.size() ? candidates[c + 1].index : last;

    size_t back = 0;
    size_t fwd = 0;
    if (!WindowEnd(cum, idx, back_limit, -1, settings.angle_window_m, &back)) continue;
    if (!WindowEnd(cum, idx, fwd_limit, +1, settings.angle_window_m, &fwd)) continue;

    double in_deg = 0.0;
    double out_deg = 0.0;
    if (!BearingBetween(g[back], g[idx], &in_deg)) continue;
    if (!BearingBetween(g[idx], g[fwd], &out_deg)) continue;

    const double turn = SignedDelta(in_deg, out_deg);
    if (std::fabs(turn) < settings.straight_deg) continue;  // a name change
    if (cum[idx] < settings.end_margin_m) continue;         // still departing
    if (cum.back() - cum[idx] < settings.end_margin_m) continue;  // arriving

    Maneuver m;
    m.at = g[idx];
    m.distance_m = cum[idx];
    m.turn_deg = turn;
    m.type = Classify(turn, settings);
    m.road = candidates[c].leg->name;
    m.klass = candidates[c].leg->klass;
    m.geometry_index = static_cast<uint32_t>(idx);
    out.push_back(m);
  }

  {
    Maneuver arrive;
    arrive.at = g.back();
    arrive.distance_m = cum.back();
    arrive.type = ManeuverType::kArrive;
    if (!shape.legs.empty()) {
      arrive.road = shape.legs.back().name;
      arrive.klass = shape.legs.back().klass;
    }
    arrive.geometry_index = static_cast<uint32_t>(last);
    out.push_back(arrive);
  }

  return out;
}

}  // namespace nav
}  // namespace fv
