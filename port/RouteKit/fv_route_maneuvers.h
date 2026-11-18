// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file fv_route_maneuvers.h
 * Fills `nav::RouteShape` from a routed route and its graph, so the maneuver
 * builder in fvkit, which does not link port/Routing, can give its turn list.
 */

#ifndef FV_ROUTE_MANEUVERS_H_
#define FV_ROUTE_MANEUVERS_H_

#include <vector>

#include "fv_router.h"
#include "fvkit/nav/maneuver.h"

namespace fv {

/**
 * `route` as the maneuver builder wants it: the line and its legs, with no
 * junctions. A route that was not found yields an empty shape. Beach-access
 * legs are folded into their neighbours, so the turns are onto the beach and
 * onto the named way off it rather than onto the unnamed connector.
 */
nav::RouteShape ManeuverShapeOf(const routing::Route& route);

/**
 * As above, plus a junction for each node of `route.nodes` that has another
 * exit usable under `options` (`routing::ArcUsable` and the arc's one-way
 * direction). The route's own exit and the way back along it are not exits.
 * `graph` must be the graph `route` was planned on.
 */
nav::RouteShape ManeuverShapeOf(const routing::Route& route, const routing::RoadGraph& graph,
                                const routing::RouteOptions& options);

/// The turn list from leg boundaries only. Convenience over ManeuverShapeOf.
std::vector<nav::Maneuver> ManeuversOf(
    const routing::Route& route,
    const nav::ManeuverSettings& settings = nav::ManeuverSettings());

/// The turn list from leg boundaries and junctions.
std::vector<nav::Maneuver> ManeuversOf(
    const routing::Route& route, const routing::RoadGraph& graph,
    const routing::RouteOptions& options,
    const nav::ManeuverSettings& settings = nav::ManeuverSettings());

}  // namespace fv

#endif  // FV_ROUTE_MANEUVERS_H_
