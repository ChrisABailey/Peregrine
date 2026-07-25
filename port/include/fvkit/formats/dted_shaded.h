// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/formats/dted_shaded.h — "dted-shaded" raster adapter (contracts D6).
//
// Wraps fv::DtedShadedRenderer (port/DtedShadedRenderer) as an IRasterSource so
// DTED shaded-relief drops into the catalog/engine/pan-viewer like any other
// map format. One source == one DTED cell. Like the CADRG/TIROS adapters it
// renders the whole cell once on the first ReadBlock and crops from the cached
// RGBA; the transforms are the renderer's exact equal-arc PixelToGeo/GeoToPixel.
//
// Enumeration reuses DtedFrameEnumerator (the same cell tree feeds the
// elevation-query "dted" format and this rendering format), so this header adds
// only the raster source. The renderer is pimpl'd (its facade is clean, but the
// pattern keeps the include here dependency-light).
//
// Default configuration = FalconView's: rendered colour, NW sun, no contours.
// Callers wanting elevation-tint/slope/contour/time-of-day reach through
// RendererForConfig() before the first ReadBlock.

#pragma once

#include <memory>
#include <string>

#include "fvkit/formats/source.h"

namespace fv {

class DtedShadedRenderer;  // port/DtedShadedRenderer/fv_dted_shaded_renderer.h

class DtedShadedRasterSource : public IRasterSource {
 public:
  DtedShadedRasterSource();
  ~DtedShadedRasterSource() override;

  Status Open(const std::string& path) override;
  GeoRect Bounds() const override;
  Status Info(ImageInfo* info) const override;
  Status ReadBlock(const PixelRect& px_rect, PixelBuffer* out) override;
  Status PixelToGeo(double px, double py, GeoPoint* p) const override;
  Status GeoToPixel(const GeoPoint& p, double* px, double* py) const override;

  // Access the underlying renderer to change display mode / lighting / bands
  // BEFORE the first ReadBlock (afterwards the cell is already rendered and
  // cached). Returns null before Open.
  DtedShadedRenderer* RendererForConfig();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fv
