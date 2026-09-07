// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPPixelBridge.h — PixelBuffer to CGImage. Private to PippinKit: it has
// `std::` in it, so it is not in the umbrella header and Swift never sees it.
//
// The format, measured rather than assumed, with `PPPixelProbe` shipping as
// the permanent measurement:
//
//     kCGImageAlphaLast | kCGBitmapByteOrder32Big,  8 bpc, 32 bpp, sRGB
//
// `fv::PixelBuffer` is interleaved RGBA8, row-major, top-down, with
// non-premultiplied alpha (cpu_canvas.cpp blends src-over, non-premultiplied,
// at both blend sites). Those three properties map onto CoreGraphics exactly:
//
//   * `kCGBitmapByteOrder32Big` makes the 32-bit pixel big-endian, so on a
//     little-endian phone the bytes come out in component order, R, G, B, A,
//     which is what PixelBuffer stores. `32Little` would be A, B, G, R.
//   * `kCGImageAlphaLast` is alpha last and not premultiplied. This is the
//     half that is possible to get wrong, because a buffer of opaque pixels
//     looks identical under all three candidate formats, so the bug would
//     have waited for the first translucent overlay fill. Measured over
//     black, source (255,255,255,128) reads back (128,128,128) under
//     AlphaLast and (255,255,255) under PremultipliedLast and NoneSkipLast.
//   * Top-down is CGImage's own convention, so nothing is flipped. The double
//     flip a `CGContext` does — bottom-up user space, and
//     `CGContextDrawImage` flipping the image to match — cancels exactly.
//
// The copy is not settled here: the provider gets its own malloc'd block
// because `CpuCanvas` reuses its buffer for the next frame, so this costs one
// memcpy per frame, about 12 MB and 1-2 ms on a 3x phone. Double-buffering it
// away is the render loop's call, if a measurement asks.

#pragma once

#import <CoreGraphics/CoreGraphics.h>

#include "fvkit/raster.h"

/// The bitmap info every PixelBuffer-backed CGImage in Pippin is made with.
CGBitmapInfo PPPixelBufferBitmapInfo(void);

/// A CGImage over a COPY of `buf`. Caller owns the result (CFRelease).
/// Returns NULL for an empty buffer or if CoreGraphics refuses.
CGImageRef PPCreateCGImageFromPixelBuffer(const fv::PixelBuffer& buf)
    CF_RETURNS_RETAINED;
