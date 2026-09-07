// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPPoint+Internal.h — how a point photograph is TAKEN, and how one goes back
// down. PRIVATE to PippinKit for `PPFix+Internal.h`'s reason: there is a C++
// type in the signature, so it is not in the umbrella header and Swift never
// sees it.

#pragma once

#import <PippinKit/PPPoint.h>

#include "fvkit/overlay/point_overlay.h"

NS_ASSUME_NONNULL_BEGIN

@interface PPPointSymbol ()

/// One palette row, converted once. The blob is copied into the `NSData`,
/// because `fv::PointSymbol::image` belongs to the overlay's vector, which
/// reallocates when the palette changes, and a bridge value must not point
/// into it.
- (instancetype)initWithPointSymbol:(const fv::PointSymbol &)symbol;

@end

@interface PPMapPoint ()

/// One row, converted once.
- (instancetype)initWithMapPoint:(const fv::MapPoint &)point;

/// The other direction, for the two callers that have an edited value and want
/// it in the document.
///
/// Everything the editor does not offer is carried through rather than
/// defaulted: `symbol_id` above all, but `category` and `elevation_ft` too. A
/// document authored elsewhere keeps its icons and attributes when a rider
/// fixes a typo in a name, which holds only because this conversion is
/// total.
- (fv::MapPoint)mapPoint;

@end

NS_ASSUME_NONNULL_END
