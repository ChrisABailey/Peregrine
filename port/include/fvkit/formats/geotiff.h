// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/formats/geotiff.h — GeoTIFF adapter (contracts D6) over ImageLib's
// CGeoTiff pixel decoder and fv::GeoTiffFrame (coverage properties, datum-
// shifted to WGS-84 through the ported GEOTRANS — MSPCCS_DATA must point at
// the GEOTRANS data directory).
//
// Enumerator: lists *.tif/*.tiff (case-insensitive) directly in <dir>.
// Unlike DTED there is no path-derived georeferencing, so enumeration reads
// each file's header via GetFrameProperties — exactly what the Windows
// GenerateCoverage did. Frames the frame reader can't identify
// (props.supported == false: the Windows build fell back to the ImageLib
// COM object / MrSID) are SKIPPED with a stderr note, per D6.
//
// Source: whole-frame reader; get_rgb_subimage output (interleaved RGB,
// grayscale DOQs come back replicated) converts to RGBA8 with alpha 255.
//
// Both classes are pimpl'd: ImageLib's geotiff.h still uses std::auto_ptr,
// so the source's implementation TU is pinned to C++14 and legacy types
// must not leak into this header.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fvkit/formats/enumerate.h"
#include "fvkit/formats/source.h"

namespace fv {

class GeoTiffRasterSource : public IRasterSource {
 public:
  GeoTiffRasterSource();
  ~GeoTiffRasterSource() override;

  Status Open(const std::string& path) override;
  GeoRect Bounds() const override;
  Status Info(ImageInfo* info) const override;
  Status ReadBlock(const PixelRect& px_rect, PixelBuffer* out) override;
  Status PixelToGeo(double px, double py, GeoPoint* p) const override;
  Status GeoToPixel(const GeoPoint& p, double* px, double* py) const override;

 private:
  struct Impl;  // wraps CGeoTiff + frame properties (C++14 TU)
  std::unique_ptr<Impl> impl_;
};

class GeoTiffFrameEnumerator : public IFrameEnumerator {
 public:
  GeoTiffFrameEnumerator() = default;

  Status Begin(const std::string& dir) override;
  bool Next(FrameInfo* info) override;

 private:
  std::vector<FrameInfo> frames_;  // sorted by path
  size_t next_ = 0;
};

}  // namespace fv
