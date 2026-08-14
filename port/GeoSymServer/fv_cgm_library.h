// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// CgmSymbolLibrary (draw plan G2) — a directory of .cgm as an ISymbolLibrary.
//
// GeoSym's symbol set is ~1500 CGM files under <DataDir>/GeoSymbol/Graphics,
// and until G2 the only way to draw one was to have a GeoSymStyleEngine open
// and to be a VPF feature it symbolized. This class is the same symbols with
// none of that: an overlay says `lib.Symbol("0051")` and gets the black dot.
//
// The library is a strict superset of what GeoSymStyleEngine::LoadSymbol does
// — same ToVectorSymbol, same adjuster, same `.cgm` extension rule — so the
// two agree by construction rather than by test. It is deliberately NOT wired
// into the style engine: the engine caches through LookupTableStyleEngine and
// gets scale/rule handling with it, and swapping that for this would be a
// behaviour change in a session whose acceptance test is that nothing moved.
//
// UNITS: the inherited 25.4 (1/100 inch), which is GeoSym's own grid.

#ifndef FV_CGM_LIBRARY_H_
#define FV_CGM_LIBRARY_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/symbol/library.h"

// CSymColorAdjuster is held by POINTER and only forward-declared, for the same
// reason GeoSymStyleEngine pimpls its own: SymColors.h is MFC-shaped and drags
// stdafx.h in with it, and an overlay that wants GeoSym's symbols should not
// have to compile MFC's shims to include this header.
class CSymColorAdjuster;

namespace fv {

class CgmSymbolLibrary : public ISymbolLibrary {
 public:
  CgmSymbolLibrary();
  ~CgmSymbolLibrary() override;

  CgmSymbolLibrary(const CgmSymbolLibrary&) = delete;
  CgmSymbolLibrary& operator=(const CgmSymbolLibrary&) = delete;

  // Catalogues `dir` (non-recursive) for `*.cgm`. The id is the stem, so
  // "0051.cgm" answers to "0051" — the bare number every GeoSym table names a
  // symbol by. A lookup that carries the extension is accepted too, because
  // SAMI point elements spell it that way (the same allowance
  // fv_geosym_style.cpp's StripCgmExtension makes).
  //
  // Nothing is parsed here: a set of 1500 symbols costs 1500 filenames until
  // somebody asks for one.
  Status OpenDirectory(const std::string& dir);

  // Brightness/contrast, in the units CSymColorAdjuster takes. Changing it
  // drops every cached display list, since the colours were baked in.
  void SetColorAdjust(int brightness, int contrast);

  std::vector<std::string> ids() const;
  size_t size() const { return files_.size(); }
  size_t loaded() const { return cache_.size(); }

  const VectorSymbol* Symbol(const std::string& symbol_id) override;

 private:
  std::map<std::string, std::string> files_;  // id -> path
  std::map<std::string, VectorSymbol> cache_;
  std::unique_ptr<CSymColorAdjuster> adjuster_;
};

}  // namespace fv

#endif  // FV_CGM_LIBRARY_H_
