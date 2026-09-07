// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPPixelProbe.h"

#import "PPPixelBridge.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/raster.h"

namespace {

// The four quadrants, in the order they are read back: top-left, top-right,
// bottom-left, bottom-right. Non-premultiplied RGBA, which is what
// PixelBuffer holds.
struct Quadrant {
  const char* name;
  fv::FvColor color;
  unsigned char over_black[3];  // what src-over black must produce
};

const Quadrant kQuadrants[4] = {
    {"opaque red", {255, 0, 0, 255}, {255, 0, 0}},
    {"opaque green", {0, 255, 0, 255}, {0, 255, 0}},
    {"opaque blue", {0, 0, 255, 255}, {0, 0, 255}},
    // 255 * 128/255 = 128 exactly, so the expectation is arithmetic and not
    // a rounding guess.
    {"half-alpha white", {255, 255, 255, 128}, {128, 128, 128}},
};

// The quad as PixelBuffer bytes. Written directly rather than through
// CpuCanvas::DrawRectangle, because the point is to control the SOURCE bytes
// exactly — a blend on the way in would be one more thing between the claim
// and the measurement.
fv::PixelBuffer MakeQuad(size_t size) {
  const int n = (int)(size < 2 ? 2 : size);
  fv::PixelBuffer buf(n, n);
  const int half = n / 2;
  for (int y = 0; y < n; ++y) {
    unsigned char* row = buf.Row(y);
    for (int x = 0; x < n; ++x) {
      const int q = (y < half ? 0 : 2) + (x < half ? 0 : 1);
      const fv::FvColor& c = kQuadrants[q].color;
      row[x * 4 + 0] = c.r;
      row[x * 4 + 1] = c.g;
      row[x * 4 + 2] = c.b;
      row[x * 4 + 3] = c.a;
    }
  }
  return buf;
}

// Draw `buf` as a CGImage of `info` over opaque black, and read the four
// quadrant centres back. Returns false if CoreGraphics refuses the format.
bool CompositeOverBlack(const fv::PixelBuffer& buf, CGBitmapInfo info,
                        unsigned char out_rgb[4][3]) {
  const size_t n = (size_t)buf.Width();
  const size_t stride = (size_t)buf.StrideBytes();
  const size_t bytes = stride * (size_t)buf.Height();
  void* pixels = std::malloc(bytes);
  if (pixels == nullptr) return false;
  std::memcpy(pixels, buf.Data(), bytes);
  CGDataProviderRef provider = CGDataProviderCreateWithData(
      nullptr, pixels, bytes,
      [](void*, const void* d, size_t) { std::free(const_cast<void*>(d)); });
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGImageRef image = CGImageCreate(n, n, 8, 32, stride, space, info, provider,
                                   nullptr, false, kCGRenderingIntentDefault);
  CGDataProviderRelease(provider);
  if (image == nullptr) {
    CGColorSpaceRelease(space);
    return false;
  }

  // The destination is a format CoreGraphics documents for bitmap contexts,
  // which is a shorter list than CGImage's. The question is about the source,
  // so the destination must not be part of it.
  std::vector<unsigned char> dst(n * 4 * n, 0);
  CGContextRef ctx = CGBitmapContextCreate(
      dst.data(), n, n, 8, n * 4, space,
      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGColorSpaceRelease(space);
  if (ctx == nullptr) {
    CGImageRelease(image);
    return false;
  }
  CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
  CGContextSetRGBFillColor(ctx, 0.0, 0.0, 0.0, 1.0);
  CGContextFillRect(ctx, CGRectMake(0, 0, (CGFloat)n, (CGFloat)n));
  CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)n, (CGFloat)n), image);
  CGContextRelease(ctx);
  CGImageRelease(image);

  // Quadrant centres. A CGBitmapContext's user space is bottom-up and
  // CGContextDrawImage flips a top-down CGImage to match, so the two
  // cancel and row 0 of `dst` is row 0 of the source — verified, not
  // assumed (get it wrong and the red and blue quadrants swap, loudly).
  const size_t q = n / 4;
  const size_t cx[4] = {q, n - q - 1, q, n - q - 1};
  const size_t cy[4] = {q, q, n - q - 1, n - q - 1};
  for (int i = 0; i < 4; ++i) {
    const unsigned char* p = dst.data() + cy[i] * n * 4 + cx[i] * 4;
    out_rgb[i][0] = p[0];
    out_rgb[i][1] = p[1];
    out_rgb[i][2] = p[2];
  }
  return true;
}

struct Candidate {
  const char* name;
  CGBitmapInfo info;
};

const Candidate kCandidates[3] = {
    {"kCGImageAlphaLast            | 32Big  (shipped)",
     (CGBitmapInfo)kCGImageAlphaLast | kCGBitmapByteOrder32Big},
    {"kCGImageAlphaPremultipliedLast| 32Big",
     (CGBitmapInfo)kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big},
    {"kCGImageAlphaNoneSkipLast    | 32Big",
     (CGBitmapInfo)kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big},
};

bool RunCandidate(const Candidate& c, unsigned char out[4][3]) {
  const fv::PixelBuffer quad = MakeQuad(64);
  return CompositeOverBlack(quad, c.info, out);
}

bool Matches(const unsigned char got[4][3]) {
  for (int i = 0; i < 4; ++i)
    for (int k = 0; k < 3; ++k)
      if (got[i][k] != kQuadrants[i].over_black[k]) return false;
  return true;
}

}  // namespace

@implementation PPPixelProbe

+ (nullable CGImageRef)quadImageOfSize:(size_t)size {
  const fv::PixelBuffer quad = MakeQuad(size);
  return PPCreateCGImageFromPixelBuffer(quad);
}

+ (NSString *)report {
  NSMutableString *out = [NSMutableString string];
  [out appendString:@"PixelBuffer -> CGImage, measured on this device\n"];
  [out appendString:@"PixelBuffer is RGBA8, top-down, NON-premultiplied "
                    @"(fvkit/raster.h, D4).\n"
                    @"Each quadrant composited over opaque black:\n\n"];
  [out appendString:@"expected                      "];
  for (int i = 0; i < 4; ++i)
    [out appendFormat:@" %3d,%3d,%3d", kQuadrants[i].over_black[0],
                      kQuadrants[i].over_black[1], kQuadrants[i].over_black[2]];
  [out appendString:@"\n\n"];

  BOOL shipped_ok = NO;
  for (int c = 0; c < 3; ++c) {
    unsigned char got[4][3] = {{0}};
    if (!RunCandidate(kCandidates[c], got)) {
      [out appendFormat:@"%-46s  REFUSED by CoreGraphics\n",
                        kCandidates[c].name];
      continue;
    }
    const BOOL ok = Matches(got);
    if (c == 0) shipped_ok = ok;
    [out appendFormat:@"%-46s", kCandidates[c].name];
    for (int i = 0; i < 4; ++i)
      [out appendFormat:@" %3d,%3d,%3d", got[i][0], got[i][1], got[i][2]];
    [out appendString:ok ? @"   match\n" : @"   WRONG\n"];
  }

  [out appendString:@"\nOnly the half-alpha quadrant separates these three: "
                    @"an opaque\nframe looks identical under all of them, "
                    @"which is why this is\nmeasured rather than assumed.\n\n"];
  [out appendString:shipped_ok
                        ? @"VERDICT: the shipped format is correct.\n"
                        : @"VERDICT: FAILED — the shipped format is wrong.\n"];
  return out;
}

+ (BOOL)passes {
  unsigned char got[4][3] = {{0}};
  if (!RunCandidate(kCandidates[0], got)) return NO;
  return Matches(got) ? YES : NO;
}

@end
