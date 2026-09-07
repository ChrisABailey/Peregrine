// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPRoute+Internal.h — how a route photograph is TAKEN. PRIVATE to PippinKit
// for `PPFix+Internal.h`'s reason: there is a C++ type in the signature, so it
// is not in the umbrella header and Swift never sees it.

#pragma once

#import <PippinKit/PPRoute.h>

#include <vector>

#include "PPRouteStore.h"
#include "fvkit/app/capabilities.h"

NS_ASSUME_NONNULL_BEGIN

@interface PPRoute ()

/// The snapshot, converted once. `planMilliseconds` is not part of
/// `RouteSnapshot` — it belongs to the STORE rather than to the route — so it
/// is passed alongside rather than smuggled into the value.
- (instancetype)initWithSnapshot:(const pippin::RouteSnapshot &)snapshot
                planMilliseconds:(double)planMilliseconds
    NS_DESIGNATED_INITIALIZER;

@end

@interface PPPlace ()

- (instancetype)initWithDescription:(const pippin::PlaceDescription &)place
    NS_DESIGNATED_INITIALIZER;

@end

@interface PPSnapTarget ()

/// `distancePoints` is handed in already converted: the surface pixels the
/// capability answers in are divided by the display scale up in `PPMap`, which
/// is the one layer that has the viewport to divide by.
- (instancetype)initWithItem:(const fv::app::SnapToItem &)item
              distancePoints:(double)distancePoints NS_DESIGNATED_INITIALIZER;

@end

/// The other direction, for the one caller that has waypoints from a sheet and
/// wants them in a document.
std::vector<fv::RouteWaypoint> PPWaypointsToRoute(
    NSArray<PPWaypoint *> *waypoints);

NS_ASSUME_NONNULL_END
