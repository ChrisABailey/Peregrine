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

// PR2 — A POINT SYMBOL'S ANGLE ON A TURNED CHART, AND THE ONE PLACE THAT
// DECIDES IT.
//
// Two kinds of angle reach DrawResolvedSymbol and only one of them is affected
// by `MapProjection::SetRotation`:
//
//   SCREEN-DERIVED angles need nothing. A pattern symbol's angle comes from
//   `atan2` over the PROJECTED tangent (PlaceAlongPath), and the projection is
//   already turned, so the tangent is already turned. Adding the chart's
//   rotation here would apply it twice. Likewise a symbol anchored at a PIXEL
//   (GeoDraw::DrawSymbolAtPixel — map furniture, the ownship): the caller is
//   working in screen space and owns the whole angle.
//
//   NORTH-UP angles are what this is for. `PointSymbolStyle::rotation_deg` is
//   authored against the chart's own north (a buoy's ORIENT, a runway's
//   bearing), so when the chart turns clockwise by R the symbol must turn
//   clockwise by R with it.
//
// SIGN. `rotation_deg` reaching DrawResolvedSymbol turns a symbol COUNTER-
// clockwise on screen (canvas.h's convention; a product authoring in compass
// bearings negates, which is why MM4 and S52StyleEngine both do). A clockwise
// chart turn is therefore a SUBTRACTION. Check it on the north arrow: authored
// pointing up at rotation 0, chart turned 90 clockwise, north now lies to the
// right of the screen, and 0 - 90 = -90 is a quarter turn clockwise. Check it
// against PR1's own pin from the other end: at rotation 270 a point due east
// lands above the centre, and a symbol pointing east (bearing 090, so
// rotation_deg -90) comes out at -90 - 270 = -360 = up.
//
// GATED, NOT COMPUTED. Rotation 0 returns the argument itself rather than
// `deg - 0.0`, for the reason PR1 gave: every pinned golden in the tree was
// made at rotation 0 and the unrotated path must execute the same terms in the
// same order it did before rotation existed. (`deg - 0.0` would in fact be
// exact for every finite deg; the rule is what survives the next edit.)
inline double SymbolAngleOnChart(double deg, double chart_rotation_deg) {
  return chart_rotation_deg != 0.0 ? deg - chart_rotation_deg : deg;
}

// Draws one symbol display list anchored at (ax, ay) pixels.
//
// Mapping (verbatim from CCGMSymbol::DrawSymbol's DC setup): logical (0,0) is
// the anchor, viewport extent is (+k, -k) so y flips, and k px per HIMETRIC
// unit is scale/25.4 — the s_dblConversionFactor path, which treats 1/100
// inch as one pixel for symbols regardless of the device.
//
// `ink` (optional) collects the extent drawn, for the pick index.
//
// `tint` (optional, G4) replaces every colour the symbol carries with one flat
// colour, keeping its SHAPE — which is what a halo pass is: the silhouette,
// stamped offset, under the symbol itself. It is deliberately not a blend: a
// halo that kept the symbol's own colours would be the symbol drawn twice.
// Null is the identity, so nothing that existed before G4 sees it.
void DrawSymbolAt(ICanvas* canvas, const VectorSymbol& sym, double ax,
                  double ay, double px_per_himetric, double rotation_rad,
                  InkBox* ink, const FvColor* tint = nullptr);

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
//
// `tint` (G4) recolours the tile's RGB and scales its alpha, so a black-on-
// transparent icon becomes a coloured silhouette of itself. It is done into a
// temporary copy which is then drawn by exactly the paths below — a tinted
// stamp at scale 1 is still a straight blit — so tinting costs one buffer per
// stamp and changes nothing about how the tile lands. That per-stamp cost is
// why the highlight pass is the caller's choice and not the default.
void DrawPixmapSymbolAt(ICanvas* canvas, const SymbolPixmap& sym, double ax,
                        double ay, double scale, double rotation_rad,
                        InkBox* ink, const FvColor* tint = nullptr);

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
                        double rotation_rad, InkBox* ink,
                        const FvColor* tint = nullptr);

}  // namespace fv

#endif  // FVKIT_VECTOR_SYMBOL_DRAW_H_
