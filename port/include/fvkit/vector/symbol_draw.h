// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// FvKit symbol drawing (draw plan G2) — "stamp this symbol at this pixel".
//
// EXTRACTED, NOT WRITTEN. Every function here was already in the tree, in
// vector/renderer.cpp's anonymous namespace, where nothing but the chart
// renderer could reach it — ~240 lines that every overlay wants. G2 moves them
// out verbatim and re-types them against ISymbolLibrary instead of
// IStyleEngine (a style engine IS one, so the renderer's own calls are
// unchanged). The acceptance test for the move is that every pinned golden is
// byte-identical; a golden that moved would mean the move was not mechanical.
//
// The one addition is SymbolPixmap::pixel_ratio, which DrawResolvedSymbol
// divides the requested scale by. It defaults to 1.0 and dividing by 1.0 is
// the identity, so the goldens do not see it.

#ifndef FVKIT_VECTOR_SYMBOL_DRAW_H_
#define FVKIT_VECTOR_SYMBOL_DRAW_H_

#include <algorithm>
#include <cmath>
#include <string>

#include "fvkit/canvas/canvas.h"
#include "fvkit/symbol/library.h"

namespace fv {

// Accumulates the pixel extent a symbol actually drew into, which is what the
// pick index uses as the symbol's hit box (vector plan §5.3: hit-test the
// glyph, not the anchor pixel).
struct InkBox {
  double minx = 0, miny = 0, maxx = 0, maxy = 0;
  bool any = false;
  void Add(double x, double y) {
    if (!any) {
      minx = maxx = x;
      miny = maxy = y;
      any = true;
      return;
    }
    minx = std::min(minx, x);
    maxx = std::max(maxx, x);
    miny = std::min(miny, y);
    maxy = std::max(maxy, y);
  }
  PixelRect ToRect(double pad) const {
    PixelRect r;
    if (!any) return r;
    r.x = static_cast<int>(std::floor(minx - pad));
    r.y = static_cast<int>(std::floor(miny - pad));
    r.width = static_cast<int>(std::ceil(maxx + pad)) - r.x + 1;
    r.height = static_cast<int>(std::ceil(maxy + pad)) - r.y + 1;
    return r;
  }
};

// Draws one symbol display list anchored at (ax, ay) pixels.
//
// Mapping (verbatim from CCGMSymbol::DrawSymbol's DC setup): logical (0,0) is
// the anchor, viewport extent is (+k, -k) so y flips, and k px per HIMETRIC
// unit is scale/25.4 — the s_dblConversionFactor path, which treats 1/100
// inch as one pixel for symbols regardless of the device.
//
// `ink` (optional) collects the extent drawn, for the pick index.
void DrawSymbolAt(ICanvas* canvas, const VectorSymbol& sym, double ax,
                  double ay, double px_per_himetric, double rotation_rad,
                  InkBox* ink);

// Draws a pixmap symbol so that its PIVOT lands on (ax, ay).
//
// The tile is authored in pixels, so the identity case — no user zoom, no
// rotation — is a straight blit at an integer offset and the sheet's own
// anti-aliased edges reach the canvas untouched. That is the case that must
// stay exact, and it is also every point symbol on a default chart.
//
// Otherwise the tile is resampled NEAREST-NEIGHBOUR into a temporary buffer by
// inverse-mapping each destination pixel. Nearest, not bilinear: these glyphs
// are 9-46 px of hard-edged chart symbology, and interpolating them smears the
// one-pixel strokes a buoy is drawn with. A rotated raster symbol is a
// degradation either way — the vector twin is what a product should ship — so
// the cheap sampler is the honest one.
//
// `scale` is in TILE pixels: pixel_ratio has already been divided out by the
// caller (DrawResolvedSymbol does it), because this function is the one that
// samples the tile's own grid.
void DrawPixmapSymbolAt(ICanvas* canvas, const SymbolPixmap& sym, double ax,
                        double ay, double scale, double rotation_rad,
                        InkBox* ink);

// A symbol id resolved to whichever form the library has for it. Looked up
// ONCE and then stamped as many times as the placer asks — an area pattern is
// hundreds of stamps of the same id, and R3b did not make the fill fast so a
// hash lookup could be added back per stamp.
struct ResolvedSymbol {
  const VectorSymbol* vec = nullptr;
  const SymbolPixmap* pix = nullptr;
  bool drawable() const { return vec != nullptr || pix != nullptr; }
};

// Display list first: a library that has a symbol both ways keeps its vector
// definition, which scales and rotates without resampling.
ResolvedSymbol ResolveSymbol(ISymbolLibrary* lib, const std::string& id);

// Draws a resolved symbol at (ax, ay). Returns true when something reached the
// canvas, so a caller only records a pick box for ink that exists.
//
// `px_per_himetric` sizes a display list; `pixmap_scale` sizes a tile, which is
// already in pixels. They are the same zoom in each form's own units and are
// passed SEPARATELY rather than derived from one another: a tile's scale must
// be exact (2.0, not 2.0 divided and re-multiplied by 25.4), because the
// nearest sampler decides the tile's first row and column on a boundary that
// lands exactly on a half-pixel at integer zooms.
bool DrawResolvedSymbol(ICanvas* canvas, const ResolvedSymbol& sym, double ax,
                        double ay, double px_per_himetric, double pixmap_scale,
                        double rotation_rad, InkBox* ink);

}  // namespace fv

#endif  // FVKIT_VECTOR_SYMBOL_DRAW_H_
