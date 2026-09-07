// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPPixelProbe.h — the deliberate test image for the pixel format.
//
// A screenshot settles the format for whoever took it; this ships the
// measurement, so it can be re-run on any phone with one launch argument:
//
//     Pippin -PPPixelProbe YES
//
// The three candidate formats differ only in the half-alpha quadrant, which
// is why this exists: an opaque map frame looks perfect under all three, so a
// wrong choice would surface much later as a too-bright translucent
// overlay.

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface PPPixelProbe : NSObject

/// A `size` x `size` quad drawn into an `fv::PixelBuffer` and converted by the
/// same code path `PPMap` hands frames through: opaque red, green and blue,
/// with half-alpha white bottom-right. Display it over black; the fourth
/// quadrant must read mid-grey, and reads white if the alpha is being taken
/// as premultiplied.
+ (nullable CGImageRef)quadImageOfSize:(size_t)size CF_RETURNS_RETAINED
    NS_SWIFT_NAME(quadImage(size:));

/// Composites that quad over black under each candidate `CGBitmapInfo` and
/// reads the pixels back. The measurement as text, with the verdict last.
+ (NSString *)report;

/// YES when the shipped format reproduces all four quadrants exactly.
+ (BOOL)passes;

@end

NS_ASSUME_NONNULL_END
