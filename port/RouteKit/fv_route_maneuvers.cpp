// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_route_maneuvers.h"

#include <string>

#include "fv_road_graph.h"

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

std::vector<nav::Maneuver> ManeuversOf(const routing::Route& route,
                                       const nav::ManeuverSettings& settings) {
  return nav::BuildManeuvers(ManeuverShapeOf(route), settings);
}

}  // namespace fv
