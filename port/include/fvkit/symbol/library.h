// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// FvKit symbol libraries (draw plan G2) — "where does a symbol_id come from".
//
// This header holds the two symbol REPRESENTATIONS (VectorSymbol, a
// product-neutral display list, and SymbolPixmap, a tile) plus the interface
// that hands them out. They lived in fvkit/vector/style.h until G2; style.h
// now includes this file and re-exports them, so every existing include of
// style.h is unchanged.
//
// WHY THE INTERFACE IS NEW AND THE CODE IS NOT. ISymbolLibrary is, exactly and
// deliberately, three methods IStyleEngine already declared. So
// `IStyleEngine : public ISymbolLibrary` is a two-line change and every
// existing engine — GeoSym, S-52, OSM, LookupTableStyleEngine — becomes a
// symbol library with no other edit. That is the whole of "expose the point
// drawing the vector-map/GeoSym code already does": name the interface that
// was sitting there, do not move the code.
//
// SYMBOL UNITS ARE HIMETRIC (0.01 mm), y UP. That is the CGM VDC convention
// GeoSym symbols are authored in and what GDI's HIMETRICtoDP consumed on
// Windows; the drawer converts to pixels and flips y, because only it knows
// the device. A product whose symbols are authored in pixels just multiplies
// by 25.4 on the way in (kHimetricPerHundredthInch in fvkit/vector/renderer.h).

#ifndef FVKIT_SYMBOL_LIBRARY_H_
#define FVKIT_SYMBOL_LIBRARY_H_

#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"  // FvColor, PixelBuffer

namespace fv {

// ---------------------------------------------------------------------------
// Symbol display list
// ---------------------------------------------------------------------------

enum class SymbolPrimitiveType {
  kPolyline = 0,
  kPolygon,   // even-odd filled, optionally edged
  kEllipse,   // center + two conjugate radius vectors (handles rotation)
  kText,
};

struct SymbolPoint {
  double x = 0.0;
  double y = 0.0;
};

struct SymbolPrimitive {
  SymbolPrimitiveType type = SymbolPrimitiveType::kPolyline;

  std::vector<SymbolPoint> points;  // polyline / polygon vertices

  // kEllipse: CGM's conjugate-diameter encoding, kept rather than reduced to
  // an axis-aligned box so rotated ellipses survive the seam.
  SymbolPoint center;
  SymbolPoint radius1;
  SymbolPoint radius2;

  // kText
  std::string text;
  double text_height = 0.0;  // HIMETRIC

  bool has_fill = false;
  bool has_stroke = false;
  FvColor fill_color;
  FvColor stroke_color;
  double stroke_width = 0.0;  // HIMETRIC; 0 = thinnest the device can draw
};

// A resolution-independent symbol. Extent is in the same HIMETRIC units and
// is informational — the drawer anchors at the symbol's own (0,0), which is
// where the authoring convention puts the hot spot.
struct VectorSymbol {
  std::vector<SymbolPrimitive> primitives;
  double min_x = 0.0, min_y = 0.0, max_x = 0.0, max_y = 0.0;
};

// ---------------------------------------------------------------------------
// Pixmap symbols
// ---------------------------------------------------------------------------
//
// Not every symbol a product ships is a display list. S-52's delivered library
// defines 679 of its 1018 symbols as RASTER ONLY — a tile in a symbol sheet
// with no HPGL at all — and they are not a fringe: the lateral buoys, beacons
// and daymarks of a harbour are in there. OSM sprite sheets are the same idea
// again, and GeoSym's stipple fills were bitmaps before they were CGM.
//
// So a symbol_id resolves through TWO accessors, vector first. This is a
// deliberate second slot rather than a rasterize-on-demand VectorSymbol: a
// pixmap has no geometry to hand back, and pretending otherwise would make
// every consumer of Symbol() (the pick index, the placer) reason about a
// display list that is really a rectangle.
//
// UNITS ARE PIXELS, unlike everything else at this seam — the tile is
// authored at a nominal display resolution and the drawer blits it. The
// pivot is where the feature's own position lands, in tile pixels with y DOWN
// (image convention, matching PixelBuffer), so it is subtracted from the
// anchor to get the tile's top-left.
struct SymbolPixmap {
  PixelBuffer tile;  // RGBA8, straight (non-premultiplied) alpha
  double pivot_x = 0.0;
  double pivot_y = 0.0;

  // TILE PIXELS PER NOMINAL PIXEL (G2). A sprite sheet states this per sprite
  // (`pixelRatio` in sprite.json) and a loose file states it in its `@2x`
  // suffix: the artwork is 2 tile pixels wide for every 1 pixel it should
  // occupy. DrawResolvedSymbol divides the requested scale by it, so a 2x
  // tile blits at half size and comes out the same size as its 1x twin.
  //
  // DEFAULT 1.0 IS THE PRE-G2 BEHAVIOUR EXACTLY — the division is by one, and
  // dividing an IEEE double by 1.0 is the identity — which is what keeps the
  // S-52 goldens (the only pixmaps in the tree before G2) byte-identical.
  double pixel_ratio = 1.0;
};

// ---------------------------------------------------------------------------
// The library
// ---------------------------------------------------------------------------

// Where a symbol_id turns into something drawable. Every style engine is one
// (IStyleEngine derives from it), and G2 adds four that are not style engines
// at all — a directory of PNGs, a directory of CGMs, the port's own builtins,
// and an ordered composite of any of them.
//
// LIFETIME: the pointers handed back are OWNED AND CACHED BY THE LIBRARY and
// stay valid until it is destroyed. That is what lets a caller resolve an id
// once and stamp it hundreds of times (an area pattern does exactly that).
class ISymbolLibrary {
 public:
  virtual ~ISymbolLibrary() = default;

  // Display list for `symbol_id`. nullptr = this library does not have it.
  virtual const VectorSymbol* Symbol(const std::string& symbol_id) = 0;

  // The pixmap form of the same id, consulted ONLY when Symbol() has no
  // geometry — a product that authors a symbol both ways keeps its vector
  // definition, which scales and rotates without resampling. Default: this
  // library has no raster symbology at all.
  virtual const SymbolPixmap* Pixmap(const std::string& /*symbol_id*/) {
    return nullptr;
  }

  // HIMETRIC units per NOMINAL SYMBOL PIXEL — the size of the grid this
  // library's symbol artists drew on. The drawer divides a display list's
  // HIMETRIC coordinates by it to get pixels, so it is the one number that
  // decides how big a symbol comes out, and a library that authors a symbol
  // BOTH ways (display list and tile) is only self-consistent when this
  // number is its own.
  //
  //   GeoSym / the default: 25.4 = 1/100 inch. FalconView's CCGMSymbol
  //     carries it as s_dblConversionFactor and treats 1/100 inch as one pixel
  //     regardless of the device; preserved under the bit-faithful rule.
  //   S-52: 32 = 0.32 mm, the presentation library's nominal pixel (the same
  //     unit its HPGL `SWn` pen widths count in). Measured, not assumed: over
  //     the 316 delivered symbols that carry BOTH a <vector> box and a
  //     <bitmap> box, the median ratio of the two is 32.11.
  //
  // NOT the device DPI: no product's symbols honour it yet (line widths and
  // text do) — see PORTING.md's standing defect. This is about the two FORMS
  // of one library's symbology agreeing with each other.
  virtual double himetric_per_symbol_pixel() const { return 25.4; }
};

using SymbolLibraryPtr = std::shared_ptr<ISymbolLibrary>;

// ---------------------------------------------------------------------------
// CompositeSymbolLibrary — ordered lookup across several
// ---------------------------------------------------------------------------
//
// "Mine first, then GeoSym's". Members are consulted in the order they were
// added and the FIRST that answers wins; Symbol() and Pixmap() are resolved
// INDEPENDENTLY, so a library that has only the pixmap form of an id does not
// shadow a later library's display list of it.
//
// THE UNIT IS ONE NUMBER FOR THE WHOLE COMPOSITE, and that is a real
// limitation rather than an oversight: himetric_per_symbol_pixel() is asked
// without an id (it sizes the pen widths and the text height as well as the
// coordinates), so there is nowhere to put a per-member answer. It defaults to
// the FIRST member's, which is right for the intended use — the port's own
// builtins are authored on the default 1/100-inch grid, so putting them in
// front of GeoSym's costs nothing. Composing libraries whose grids genuinely
// differ (GeoSym's 25.4 with S-52's 32) sizes one of them wrong; set the
// number explicitly and accept it, or draw them through two libraries.
class CompositeSymbolLibrary : public ISymbolLibrary {
 public:
  // Members are BORROWED — the composite does not own them and they must
  // outlive it. This is the common case (a style engine owned by the map
  // engine, a builtin library owned by the overlay) and it keeps the composite
  // free of a lifetime policy of its own.
  void Add(ISymbolLibrary* lib);

  size_t size() const { return members_.size(); }

  const VectorSymbol* Symbol(const std::string& symbol_id) override;
  const SymbolPixmap* Pixmap(const std::string& symbol_id) override;
  double himetric_per_symbol_pixel() const override;

  // Overrides the inherited-from-the-first-member default. A non-positive
  // value restores it.
  void set_himetric_per_symbol_pixel(double v) { himetric_override_ = v; }

 private:
  std::vector<ISymbolLibrary*> members_;
  double himetric_override_ = 0.0;
};

}  // namespace fv

#endif  // FVKIT_SYMBOL_LIBRARY_H_
