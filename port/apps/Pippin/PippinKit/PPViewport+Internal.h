// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPViewport+Internal.h — how a viewport is MINTED, and the projection it
// hands the renderer. PRIVATE to PippinKit: it has `std::`-adjacent C++ in
// it, so it is not in the umbrella header and Swift never sees it.
//
// Only `PPMap` mints the first one, because only `PPMap` has read the pack —
// the home bounds and the pyramid's zoom range are what the limits are made
// of, and a viewport with limits nobody set is the failure this arrangement
// exists to prevent.

#pragma once

#import <PippinKit/PPViewport.h>

#include "fvkit/proj.h"

NS_ASSUME_NONNULL_BEGIN

@interface PPViewport ()

/// The first viewport of a pack: no surface yet (the screen has not been
/// measured), camera at the middle of `home`, limits waiting on a size.
///
/// `mmPerPixelOverride` is `display.mm_per_pixel` from `pippin.ini`, or 0 for
/// "derive it from the backing scale" — the pack gets to say, because a pack
/// may be read on a device whose pitch Apple's baseline does not describe.
- (instancetype)initWithHomeBounds:(PPGeoBounds)home
                       minTileZoom:(int)minZoom
                       maxTileZoom:(int)maxZoom
                mmPerPixelOverride:(double)mmPerPixelOverride
    NS_DESIGNATED_INITIALIZER;

/// The projection this viewport IS — surface size in pixels, centre, physical
/// scale and rotation, already applied. The renderer takes it as it stands,
/// which is what keeps "where the map looks" from being stated twice.
- (const fv::MapProjection &)projection;

/// The pixel surface: `sizeInPoints * displayScale`, rounded once, here.
@property(nonatomic, readonly) int pixelWidth;
@property(nonatomic, readonly) int pixelHeight;

@end

NS_ASSUME_NONNULL_END
