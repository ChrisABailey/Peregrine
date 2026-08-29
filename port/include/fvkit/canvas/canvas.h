// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/canvas/canvas.h — FvKit L2.5 drawing interface (contracts D1/D3).
// Mirrors the primitive set of FalconView's IGraphicsContext2 (pens, brushes,
// fonts, lines/polygons/ellipses/rectangles/text/blits), which is also
// exactly the GDI surface the GeoSym renderer uses (see vpf-geosym-plan.md).
//
// NOTE (plan deviation, documented): IGraphicsContext2 used GDI-style
// factory objects (CreatePen/CreateBrush/SelectObject). This API passes
// Pen/Brush/TextStyle VALUE structs per call instead — stateless, trivially
// bindable, and it maps 1:1 onto CoreGraphics/Skia immediate-mode backends.
//
// Pixel conventions per D4: integer pixel coords, x right, y down, origin
// top-left; the target is RGBA8 (PixelBuffer). Colors are non-premultiplied;
// drawing is src-over.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/raster.h"

namespace fv {

struct FvColor {
  unsigned char r = 0, g = 0, b = 0, a = 255;
};

struct Pen {
  FvColor color;
  int width = 1;          // pixels; stamped square nib (non-AA)
  std::vector<int> dash;  // on/off run lengths in px; empty = solid
};

struct Brush {
  FvColor color;  // solid fill (hatch styles: later, when GeoSym needs them)
};

struct TextStyle {
  std::string font_path;  // TTF/TTC file; empty = canvas default font
  double size = 12.0;     // pixel height
  FvColor color;
};

struct ICanvas {
  virtual ~ICanvas() = default;
  ICanvas(const ICanvas&) = delete;
  ICanvas& operator=(const ICanvas&) = delete;

  virtual PixelSize Size() const = 0;
  virtual void Clear(const FvColor& c) = 0;

  // Connected polyline through pts (>= 2 points).
  virtual Status DrawLines(const std::vector<PixelPoint>& pts,
                           const Pen& pen) = 0;

  // Even-odd (GDI ALTERNATE) fill of one or more rings, then optional
  // outline. Rings need not be explicitly closed.
  virtual Status DrawPolyPolygon(const std::vector<std::vector<PixelPoint>>& rings,
                                 const Brush* fill, const Pen* outline) = 0;

  virtual Status DrawRectangle(const PixelRect& r, const Brush* fill,
                               const Pen* outline) = 0;

  // Ellipse inscribed in bbox.
  virtual Status DrawEllipse(const PixelRect& bbox, const Brush* fill,
                             const Pen* outline) = 0;

  // Alpha-blends src (RGBA8) with its top-left at (x, y); clips to target.
  virtual Status DrawPixmap(const PixelBuffer& src, int x, int y) = 0;

  // Draws utf8 text with its BASELINE-left at (x, y).
  virtual Status DrawTextString(const std::string& utf8, int x, int y,
                                const TextStyle& style) = 0;

  // The same, rotated about that baseline-left origin, and in sub-pixel
  // coordinates because a glyph laid along a road lands wherever the road is.
  //
  // ANGLE CONVENTION, identical to PlacedSymbol::rotation_deg: positive turns
  // the text COUNTERCLOCKWISE as seen on screen. Screen y grows downward, so
  // the baseline direction is (cos a, -sin a) and the up direction is
  // (sin a, cos a).
  //
  // NOT pure: a canvas that has no rotated text (pyfvw's Python-side ICanvas
  // subclasses, any future thin native backend) keeps compiling and draws the
  // string upright rather than dropping it. Override it to place text along a
  // path properly.
  virtual Status DrawRotatedTextString(const std::string& utf8, double x,
                                       double y, double angle_rad,
                                       const TextStyle& style) {
    (void)angle_rad;
    return DrawTextString(utf8, static_cast<int>(x < 0 ? x - 0.5 : x + 0.5),
                          static_cast<int>(y < 0 ? y - 0.5 : y + 0.5), style);
  }
  virtual Status GetTextExtent(const std::string& utf8, const TextStyle& style,
                               PixelSize* out) = 0;

 protected:
  ICanvas() = default;
};

}  // namespace fv
