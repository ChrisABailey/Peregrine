// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_route_maneuvers.h — the turn list of a routed route (guidance plan GD1).
//
// The D6 seam between the two halves of GD1. `fvkit/nav/maneuver.h` derives
// maneuvers from a line and its leg boundaries and knows nothing about a road
// graph; `fv::routing::Route` is what the router produces. This is the six
// lines that carry one into the other, and it lives in RouteKit for the
// module's whole reason for existing: fvkit does not link port/Routing.

#ifndef FV_ROUTE_MANEUVERS_H_
#define FV_ROUTE_MANEUVERS_H_

#include <vector>

#include "fv_router.h"
#include "fvkit/nav/maneuver.h"

namespace fv {

// `route` as the maneuver builder wants it. A route that was not found has no
// geometry and yields an empty shape, which yields no maneuvers.
nav::RouteShape ManeuverShapeOf(const routing::Route& route);

// The turn list, in travel order. Convenience over ManeuverShapeOf.
std::vector<nav::Maneuver> ManeuversOf(
    const routing::Route& route,
    const nav::ManeuverSettings& settings = nav::ManeuverSettings());

}  // namespace fv

#endif  // FV_ROUTE_MANEUVERS_H_
