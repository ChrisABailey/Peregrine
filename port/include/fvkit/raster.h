// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/raster.h — FvKit L0 raster primitives.
// Contracts: port/fvkit-contracts.md (D4 pixel contract).
//
// PixelBuffer is the ONLY pixel currency above the format adapters:
// owning, interleaved RGBA8, row-major, top-down (row 0 = north/top),
// stride_bytes >= width*4, alpha 255 = opaque. In Python it surfaces as a
// zero-copy numpy uint8 array of shape (h, w, 4).
//
// NOTE (plan deviation, documented): the plan sketched ImageInfo as
// {size, geotransform}. A degree-space affine is only approximate for
// projected imagery (the DOQ quarter-quads are UTM), so ImageInfo carries
// size + WGS-84 bounds and the EXACT per-source transforms live on
// IRasterSource::PixelToGeo/GeoToPixel instead.
//
// This header must stay C++14-parseable: it is included by adapter TUs
// pinned to C++14 (ImageLib's geotiff.h still uses std::auto_ptr).

#pragma once

#include <vector>

#include "fvkit/geo.h"

namespace fv {

class PixelBuffer {
 public:
  PixelBuffer() = default;
  PixelBuffer(int width, int height)
      : data_((size_t)width * 4 * height, 0),
        width_(width),
        height_(height),
        stride_(width * 4) {}

  int Width() const { return width_; }
  int Height() const { return height_; }
  int StrideBytes() const { return stride_; }
  bool Empty() const { return data_.empty(); }

  unsigned char* Data() { return data_.data(); }
  const unsigned char* Data() const { return data_.data(); }
  unsigned char* Row(int y) { return data_.data() + (size_t)y * stride_; }
  const unsigned char* Row(int y) const {
    return data_.data() + (size_t)y * stride_;
  }

 private:
  std::vector<unsigned char> data_;
  int width_ = 0;
  int height_ = 0;
  int stride_ = 0;
};

struct ImageInfo {
  PixelSize size;
  GeoRect bounds;  // WGS-84 bounding box (projected images: box of corners)
};

}  // namespace fv
