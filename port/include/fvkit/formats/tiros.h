// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/formats/tiros.h — TIROS adapter (contracts D6) over the ported CJpeg
// reader (ImageLibCore) and fv::TirosFrame georeferencing (port/TirosMapServer).
//
// One TirosRasterSource == one TIROS JPEG tile (1350x1350, RGB). Like the
// CADRG adapter it decodes the whole tile once on the first ReadBlock and
// crops from the cached RGBA; georef/transforms are equal-arc from the
// filename-derived bounds. Enumeration lists *.wld/*.WLD (case-insensitive)
// tiles and derives bounds from the name (no pixels decoded).
//
// The source is pimpl'd (CJpeg's header pulls in legacy CString-based types).

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fvkit/formats/enumerate.h"
#include "fvkit/formats/source.h"

namespace fv {

class TirosRasterSource : public IRasterSource {
 public:
  TirosRasterSource();
  ~TirosRasterSource() override;

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

class TirosFrameEnumerator : public IFrameEnumerator {
 public:
  TirosFrameEnumerator() = default;

  Status Begin(const std::string& dir) override;
  bool Next(FrameInfo* info) override;

 private:
  std::vector<FrameInfo> frames_;  // sorted by path
  size_t next_ = 0;
};

}  // namespace fv
