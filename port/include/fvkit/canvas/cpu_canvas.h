// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/canvas/cpu_canvas.h — portable CPU rasterizer over PixelBuffer.
// Non-antialiased by design (deterministic goldens; the interface is the
// deliverable, per the plan's timebox note). Scanline even-odd polygon fill,
// Bresenham lines with a stamped square nib, stb_truetype text.

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"

namespace fv {

class CpuCanvas : public ICanvas {
 public:
  CpuCanvas(int width, int height);
  ~CpuCanvas() override;

  PixelSize Size() const override;
  void Clear(const FvColor& c) override;
  Status DrawLines(const std::vector<PixelPoint>& pts, const Pen& pen) override;
  Status DrawPolyPolygon(const std::vector<std::vector<PixelPoint>>& rings,
                         const Brush* fill, const Pen* outline) override;
  Status DrawRectangle(const PixelRect& r, const Brush* fill,
                       const Pen* outline) override;
  Status DrawEllipse(const PixelRect& bbox, const Brush* fill,
                     const Pen* outline) override;
  Status DrawPixmap(const PixelBuffer& src, int x, int y) override;
  Status DrawTextString(const std::string& utf8, int x, int y,
                        const TextStyle& style) override;
  Status DrawRotatedTextString(const std::string& utf8, double x, double y,
                               double angle_rad,
                               const TextStyle& style) override;
  Status GetTextExtent(const std::string& utf8, const TextStyle& style,
                       PixelSize* out) override;

  // Sets the font used when TextStyle.font_path is empty.
  Status SetDefaultFont(const std::string& font_path);

  PixelBuffer& Buffer() { return buf_; }
  const PixelBuffer& Buffer() const { return buf_; }

 private:
  struct FontEntry;  // cached, parsed TTF (stb_truetype state)

  void BlendPixel(int x, int y, const FvColor& c);
  // BlendPixel over a clamped horizontal run of an already-resolved row.
  void BlendSpan(unsigned char* row, int x0, int x1, const FvColor& c);
  void Stamp(int x, int y, const Pen& pen);
  void FillScanlines(const std::vector<std::vector<PixelPoint>>& rings,
                     const FvColor& c);
  FontEntry* LoadFont(const std::string& path, Status* status);

  PixelBuffer buf_;
  std::string default_font_;
  std::map<std::string, std::unique_ptr<FontEntry>> fonts_;
};

}  // namespace fv
