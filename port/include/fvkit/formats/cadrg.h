// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/formats/cadrg.h — CADRG/RPF adapter (contracts D6) over the ported
// RPF frame decoder (RPFRenderer, port/CadrgDecoder) and the frame
// georeferencing (fv::CadrgFrame, port/CadrgMapServer).
//
// One CadrgRasterSource == one RPF frame file (1536 x 1536 palette chart).
// Because the decoder always expands the whole frame, the source decodes it
// once on the first ReadBlock and crops from the cached RGBA buffer; a frame
// held resident is ~9.4 MB. CadrgFrameCache is the LRU that bounds how many
// decoded frames stay resident during a pan (the plan's #1 risk — a viewport
// touches many frames), keyed by absolute path.
//
// Transforms: non-polar (equal-arc) frames map pixel<->geo linearly from the
// frame's WGS-84 corners and degrees-per-pixel. Polar-zone frames (none in
// CONUS/TestData) report kUnsupported from PixelToGeo/GeoToPixel for now.
//
// Both the source and the cache are pimpl'd: the decoder headers still use
// CString / std::auto_ptr-era code, so their TU is pinned to C++14 and the
// legacy types never leak into this header.

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/formats/enumerate.h"
#include "fvkit/formats/source.h"

namespace fv {

class CadrgRasterSource : public IRasterSource {
 public:
  CadrgRasterSource();
  ~CadrgRasterSource() override;

  Status Open(const std::string& path) override;
  GeoRect Bounds() const override;
  Status Info(ImageInfo* info) const override;
  Status ReadBlock(const PixelRect& px_rect, PixelBuffer* out) override;
  Status PixelToGeo(double px, double py, GeoPoint* p) const override;
  Status GeoToPixel(const GeoPoint& p, double* px, double* py) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Lists RPF frame files under a directory tree (recursively); bounds come
// from fv::CadrgFrame (no pixels decoded). Frames whose series/zone can't be
// resolved are skipped.
class CadrgFrameEnumerator : public IFrameEnumerator {
 public:
  CadrgFrameEnumerator() = default;

  Status Begin(const std::string& dir) override;
  bool Next(FrameInfo* info) override;

 private:
  std::vector<FrameInfo> frames_;  // sorted by path
  size_t next_ = 0;
};

// LRU cache of opened+decoded frames, keyed by absolute path. get() opens on
// miss (decode is still lazy, on the source's first ReadBlock), moves to
// most-recent on hit, and evicts the least-recently-used source past
// capacity. Not thread-safe (single decode thread; GIL-released in pyfvw).
class CadrgFrameCache {
 public:
  explicit CadrgFrameCache(size_t capacity = 24);
  ~CadrgFrameCache();

  // Returns an opened source for path (nullptr + status on open failure).
  std::shared_ptr<CadrgRasterSource> Get(const std::string& path,
                                         Status* status);
  size_t Size() const;
  size_t Capacity() const { return capacity_; }

 private:
  struct Entry {
    std::string path;
    std::shared_ptr<CadrgRasterSource> source;
  };
  size_t capacity_;
  std::vector<Entry> entries_;  // front = most-recently-used
};

}  // namespace fv
