// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_route_maneuvers.h"

namespace fv {

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
  return shape;
}

std::vector<nav::Maneuver> ManeuversOf(const routing::Route& route,
                                       const nav::ManeuverSettings& settings) {
  return nav::BuildManeuvers(ManeuverShapeOf(route), settings);
}

}  // namespace fv
