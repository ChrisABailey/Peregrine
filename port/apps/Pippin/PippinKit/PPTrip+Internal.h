// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPTrip+Internal.h — how a trip value is MADE. PRIVATE to PippinKit for
// `PPFix+Internal.h`'s reason: there is a C++ type in the signature, so it is
// not in the umbrella header and Swift never sees it.

#pragma once

#import <PippinKit/PPTrip.h>

#include "fvkit/nav/trip.h"

NS_ASSUME_NONNULL_BEGIN

@interface PPTrip ()

- (instancetype)initWithStats:(const fv::TripStats &)stats
    NS_DESIGNATED_INITIALIZER;

@end

NS_ASSUME_NONNULL_END
