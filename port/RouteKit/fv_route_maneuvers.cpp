// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_route_maneuvers.h"

#include <cmath>
#include <string>

#include "fv_road_graph.h"
#include "geo_tool.h"

namespace fv {
namespace {

bool IsClass(const nav::RouteShape::Leg& leg, routing::RoadClass klass) {
  return leg.klass == routing::RoadClassName(klass);
}

/// Folds each beach-access leg into a neighbouring leg, preferring one that is
/// not the beach and then the earlier one. The access arcs are unnamed
/// connectors the graph builder draws from a boardwalk's end to the waterline,
/// so the turns worth giving are onto the beach and onto the way off it.
void AbsorbBeachAccess(nav::RouteShape* shape) {
  std::vector<nav::RouteShape::Leg>& legs = shape->legs;
  for (size_t i = 0; i < legs.size() && legs.size() > 1;) {
    if (!IsClass(legs[i], routing::RoadClass::kBeachAccess)) {
      ++i;
      continue;
    }
    const bool has_prev = i > 0;
    const bool has_next = i + 1 < legs.size();
    const bool prev_is_way = has_prev && !IsClass(legs[i - 1], routing::RoadClass::kBeach);
    const bool next_is_way = has_next && !IsClass(legs[i + 1], routing::RoadClass::kBeach);
    const bool into_prev = prev_is_way || (!next_is_way && has_prev);
    if (!into_prev) legs[i + 1].geometry_begin = legs[i].geometry_begin;
    legs.erase(legs.begin() + static_cast<std::ptrdiff_t>(i));
  }
}

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
// How far out along an exit its bearing is taken; matches the default
// `nav::ManeuverSettings::angle_window_m` the route's own bearings use.
constexpr double kExitWindowM = 15.0;
// Two directions out of a junction closer than this are the same way.
constexpr double kSameWayDeg = 1.0;

/// Great-circle metres on the port's mean-radius sphere.
double Meters(const GeoPoint& a, const GeoPoint& b) {
  const double dphi = (b.lat - a.lat) * kDegToRad;
  const double dlam = NormalizeLon(b.lon - a.lon) * kDegToRad;
  const double s1 = std::sin(dphi * 0.5);
  const double s2 = std::sin(dlam * 0.5);
  double h = s1 * s1 + std::cos(a.lat * kDegToRad) * std::cos(b.lat * kDegToRad) * s2 * s2;
  if (h > 1.0) h = 1.0;
  return 2.0 * 6371008.8 * std::asin(std::sqrt(h));
}

/// True bearing from `a` to `b`; false for coincident points.
bool Bearing(const GeoPoint& a, const GeoPoint& b, double* out) {
  if (a.lat == b.lat && a.lon == b.lon) return false;
  double range = 0.0;
  return GEO_calc_range_and_bearing(a.lat, a.lon, b.lat, b.lon, &range, out, TRUE) == SUCCESS;
}

double AngleBetween(double a_deg, double b_deg) {
  double d = std::fabs(std::fmod(a_deg - b_deg, 360.0));
  return d > 180.0 ? 360.0 - d : d;
}

/// The point `window_m` along `shape` from its start, or its end when shorter.
GeoPoint PointOut(const std::vector<GeoPoint>& shape, double window_m) {
  double walked = 0.0;
  for (size_t k = 1; k < shape.size(); ++k) {
    const double seg = Meters(shape[k - 1], shape[k]);
    if (walked + seg >= window_m && seg > 0.0) {
      const double f = (window_m - walked) / seg;
      return GeoPoint{shape[k - 1].lat + f * (shape[k].lat - shape[k - 1].lat),
                      shape[k - 1].lon + f * (shape[k].lon - shape[k - 1].lon)};
    }
    walked += seg;
  }
  return shape.back();
}

/// Bearing of the route's first move away from `idx` in `dir`, skipping
/// coincident points. False at the line's ends.
bool RouteBearing(const std::vector<GeoPoint>& g, size_t idx, int dir, double* out) {
  for (size_t k = idx;;) {
    if (dir > 0 ? k + 1 >= g.size() : k == 0) return false;
    k = dir > 0 ? k + 1 : k - 1;
    if (Bearing(g[idx], g[k], out)) return true;
  }
}

bool SamePoint(const GeoPoint& a, const GeoPoint& b) {
  return std::fabs(a.lat - b.lat) < 1e-9 && std::fabs(a.lon - b.lon) < 1e-9;
}

/// The junctions along `shape.geometry` at `route.nodes`, with their other exits.
void AddJunctions(const routing::Route& route, const routing::RoadGraph& graph,
                  const routing::RouteOptions& options, nav::RouteShape* shape) {
  const std::vector<GeoPoint>& g = shape->geometry;
  std::vector<GeoPoint> arc_shape;
  size_t cursor = 0;
  for (uint32_t node : route.nodes) {
    if (node >= graph.node_count()) continue;
    const GeoPoint at = graph.location(node);
    size_t idx = cursor;
    while (idx < g.size() && !SamePoint(g[idx], at)) ++idx;
    if (idx >= g.size()) continue;  // not on the line; leave the cursor where it was
    cursor = idx;
    if (idx == 0 || idx + 1 >= g.size()) continue;

    double ahead = 0.0;
    double behind = 0.0;
    if (!RouteBearing(g, idx, +1, &ahead) || !RouteBearing(g, idx, -1, &behind)) continue;

    nav::RouteShape::Junction j;
    j.geometry_index = static_cast<uint32_t>(idx);
    for (uint32_t a = graph.arc_begin(node); a < graph.arc_end(node); ++a) {
      const routing::RoadArc& arc = graph.arc(a);
      if (!arc.forward() || !routing::ArcUsable(arc, options)) continue;
      arc_shape.clear();
      graph.ArcShape(a, &arc_shape);
      if (arc_shape.size() < 2) continue;
      double first = 0.0;
      if (!RouteBearing(arc_shape, 0, +1, &first)) continue;
      if (AngleBetween(first, ahead) < kSameWayDeg) continue;   // the route's own exit
      if (AngleBetween(first, behind) < kSameWayDeg) continue;  // back the way it came
      double exit_deg = 0.0;
      if (!Bearing(at, PointOut(arc_shape, kExitWindowM), &exit_deg)) continue;
      j.exit_bearings_deg.push_back(exit_deg);
    }
    if (!j.exit_bearings_deg.empty()) shape->junctions.push_back(std::move(j));
  }
}

}  // namespace

nav::RouteShape ManeuverShapeOf(const routing::Route& route) {
  nav::RouteShape shape;
  if (!route.found) return shape;

  shape.geometry = route.geometry;
  shape.legs.reserve(route.legs.size());
  for (const routing::RouteLeg& leg : route.legs) {
    nav::RouteShape::Leg out;
    out.geometry_begin = leg.geometry_begin;
    out.name = leg.name;
    out.klass = leg.klass;
    shape.legs.push_back(out);
  }
  AbsorbBeachAccess(&shape);
  return shape;
}

nav::RouteShape ManeuverShapeOf(const routing::Route& route, const routing::RoadGraph& graph,
                                const routing::RouteOptions& options) {
  nav::RouteShape shape = ManeuverShapeOf(route);
  if (!shape.geometry.empty()) AddJunctions(route, graph, options, &shape);
  return shape;
}

std::vector<nav::Maneuver> ManeuversOf(const routing::Route& route,
                                       const nav::ManeuverSettings& settings) {
  return nav::BuildManeuvers(ManeuverShapeOf(route), settings);
}

std::vector<nav::Maneuver> ManeuversOf(const routing::Route& route,
                                       const routing::RoadGraph& graph,
                                       const routing::RouteOptions& options,
                                       const nav::ManeuverSettings& settings) {
  return nav::BuildManeuvers(ManeuverShapeOf(route, graph, options), settings);
}

}  // namespace fv
