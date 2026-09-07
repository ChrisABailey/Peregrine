// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPPixelBridge.h"

#include <cstdlib>
#include <cstring>

CGBitmapInfo PPPixelBufferBitmapInfo(void) {
  return (CGBitmapInfo)kCGImageAlphaLast | kCGBitmapByteOrder32Big;
}

namespace {

void ReleaseMallocedPixels(void* /*info*/, const void* data, size_t /*size*/) {
  std::free(const_cast<void*>(data));
}

}  // namespace

CGImageRef PPCreateCGImageFromPixelBuffer(const fv::PixelBuffer& buf) {
  const int w = buf.Width();
  const int h = buf.Height();
  if (w <= 0 || h <= 0 || buf.Empty()) return nullptr;

  const size_t stride = (size_t)buf.StrideBytes();
  const size_t bytes = stride * (size_t)h;
  void* pixels = std::malloc(bytes);
  if (pixels == nullptr) return nullptr;
  std::memcpy(pixels, buf.Data(), bytes);

  CGDataProviderRef provider = CGDataProviderCreateWithData(
      nullptr, pixels, bytes, ReleaseMallocedPixels);
  if (provider == nullptr) {
    std::free(pixels);
    return nullptr;
  }
  // sRGB by name, not `DeviceRGB`: the style sheet's colours are sRGB byte
  // triples and naming the space is what keeps them from being reinterpreted
  // in whatever the device profile happens to be.
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGImageRef image =
      CGImageCreate((size_t)w, (size_t)h, 8, 32, stride, space,
                    PPPixelBufferBitmapInfo(), provider, nullptr,
                    /*shouldInterpolate=*/false, kCGRenderingIntentDefault);
  CGColorSpaceRelease(space);
  CGDataProviderRelease(provider);
  return image;
}
