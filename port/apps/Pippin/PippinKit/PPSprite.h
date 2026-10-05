// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPSprite.h — trimming a stamped marker down to its ink.
//
// A billboard is stamped onto a canvas sized generously for its label, then
// cut to the pixels that carry ink so the sprite the shell composites each
// frame is no larger than the marker and its name. Pure C++ so the mac tests it.

#pragma once

#include <algorithm>
#include <cstring>

#include "fvkit/raster.h"

namespace pippin {

/// A rectangle of pixels, origin top-left. Empty when `width` or `height` is 0.
struct InkRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool empty() const { return width <= 0 || height <= 0; }
};

/// The smallest rectangle holding every pixel of `buf` with non-zero alpha.
inline InkRect InkBounds(const fv::PixelBuffer& buf) {
  int x0 = buf.Width(), y0 = buf.Height(), x1 = -1, y1 = -1;
  for (int y = 0; y < buf.Height(); ++y) {
    const unsigned char* row = buf.Row(y);
    for (int x = 0; x < buf.Width(); ++x) {
      if (row[x * 4 + 3] == 0) continue;
      x0 = std::min(x0, x);
      x1 = std::max(x1, x);
      y0 = std::min(y0, y);
      y1 = std::max(y1, y);
    }
  }
  if (x1 < 0) return InkRect{};
  return InkRect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

/// A copy of `r` out of `buf`; `r` must lie inside it.
inline fv::PixelBuffer Crop(const fv::PixelBuffer& buf, const InkRect& r) {
  fv::PixelBuffer out(r.width, r.height);
  for (int y = 0; y < r.height; ++y)
    std::memcpy(out.Row(y), buf.Row(r.y + y) + r.x * 4, (size_t)r.width * 4);
  return out;
}

}  // namespace pippin
