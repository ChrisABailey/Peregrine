// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// PngSymbolLibrary (draw plan G2) — raster symbols from PNG, in the two forms
// the world ships them in.
//
//   LOOSE FILES   OpenDirectory(dir): every `<id>.png` under `dir`, id = the
//                 stem. An optional sidecar `<id>.json` states the pivot; with
//                 no sidecar the pivot is the tile's CENTRE, which is what a
//                 marker wants and what a caller can least often be bothered
//                 to author. A `<id>@2x.png` is the high-DPI twin of `<id>`.
//
//   SPRITE SHEET  OpenSheet(png, json): one image plus MapLibre/Mapbox
//                 `sprite.json` — `{"<id>": {"x","y","width","height",
//                 "pixelRatio"}}`. THIS IS THE FORM THAT CLOSES THE LEDGER'S
//                 "no sprite sheet, so OSM has no icons" GAP (§2b): the same
//                 loader, a second consumer, so it is built sheet-capable here
//                 rather than written twice.
//
// Everything is LAZY past the sheet image itself: a directory of 400 icons
// costs 400 stat-less filenames until somebody asks for one, and a sheet slices
// a tile on demand. What is loaded is cached forever, because ISymbolLibrary
// promises the pointer stays valid.
//
// PIXEL RATIO. Both forms state how many tile pixels make one nominal pixel —
// the sheet per sprite, a loose file in its `@2x`. It lands on
// SymbolPixmap::pixel_ratio and DrawResolvedSymbol divides the requested scale
// by it, so a 2x sprite comes out the same SIZE as its 1x twin and merely
// carries more detail. Which twin is used is the caller's call
// (SetPreferHighDpi), because only the caller knows the device — the same
// division of labour as PickSession::tolerance_px.

#ifndef FVKIT_SYMBOL_PNG_LIBRARY_H_
#define FVKIT_SYMBOL_PNG_LIBRARY_H_

#include <map>
#include <string>
#include <vector>

#include "fvkit/symbol/library.h"

namespace fv {

class PngSymbolLibrary : public ISymbolLibrary {
 public:
  PngSymbolLibrary();
  ~PngSymbolLibrary() override;

  PngSymbolLibrary(const PngSymbolLibrary&) = delete;
  PngSymbolLibrary& operator=(const PngSymbolLibrary&) = delete;

  // Prefer an `@2x` variant / a pixelRatio-2 sprite when the id has both.
  // MUST be set before Open*, since it decides which file an id binds to.
  // Default false: take the 1x, which is what a 96-dpi display wants.
  void SetPreferHighDpi(bool on) { prefer_high_dpi_ = on; }

  // Catalogues `dir` (non-recursive). An empty directory is not an error — a
  // library with nothing in it answers nullptr, which is a normal outcome for
  // a composite member.
  Status OpenDirectory(const std::string& dir);

  // Reads the sheet image and its index. `json_path` empty means the sheet's
  // own path with `.png` swapped for `.json`, which is how sprite sets ship.
  Status OpenSheet(const std::string& png_path,
                   const std::string& json_path = std::string());

  // Ids this library can answer, sorted. Cheap: the catalogue is names.
  std::vector<std::string> ids() const;
  size_t size() const { return entries_.size(); }

  // Number of tiles actually decoded so far — the laziness, observable.
  size_t loaded() const { return cache_.size(); }

  // A PNG library has no display lists at all. Stated rather than inherited so
  // the fall-through in ResolveSymbol is obvious from this class alone.
  const VectorSymbol* Symbol(const std::string& /*symbol_id*/) override {
    return nullptr;
  }
  const SymbolPixmap* Pixmap(const std::string& symbol_id) override;

 private:
  // Where one id's pixels come from: either a file of its own, or a rectangle
  // of the shared sheet.
  struct Entry {
    std::string path;  // loose form; empty for a sheet entry
    int x = 0, y = 0, w = 0, h = 0;  // sheet form
    double pixel_ratio = 1.0;
    bool has_pivot = false;
    double pivot_x = 0.0, pivot_y = 0.0;
  };

  // Drops the catalogue, the decoded cache and the sheet. Called by both
  // Open* once they are past every failure that should leave the library
  // untouched.
  void Reset();

  bool LoadEntry(const Entry& e, SymbolPixmap* out) const;

  std::map<std::string, Entry> entries_;
  std::map<std::string, SymbolPixmap> cache_;
  PixelBuffer sheet_;
  bool prefer_high_dpi_ = false;
};

}  // namespace fv

#endif  // FVKIT_SYMBOL_PNG_LIBRARY_H_
