// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::GeoSymStyleEngine — GeoSym as an FvKit IStyleEngine (vpf-geosym plan
// phase V5b). This is the portable replacement for the Windows-only
// SanSymbol.cpp: it makes the same symbol-assignment DECISIONS, but emits
// fvkit draw ops instead of drawing on a CDC through CSymLayeredDisplay.
//
// What it reads (all through the V3/V4 rule-table layer already compiled in
// place, so the parsing is FalconView's own code):
//
//   SymAssign\fullsym.txt   (pid, fcode, delin) -> point/line/area symbol
//                           number + display priority + label attribute
//   SymAssign\ATTEXP.TXT    per-row attribute-expression conditions
//   SymAssign\COLOR.TXT     the colour table + brightness/contrast adjuster
//   SymAssign\TEXT.TXT      label font/size/colour/offset rows
//   Graphics\<num>.cgm      the symbol itself, via fv::CgmSymbol (V4)
//
// ASSIGNMENT SEMANTICS, verbatim from CSymFACCEntries (SanSymbol.cpp):
//   * rows are matched on FACC + delineation, and EVERY row whose ATTEXP
//     condition evaluates true contributes a draw (they carry their own
//     priorities, so a feature can paint at several layers);
//   * unknown FACC falls back to the pseudo-entries "icon" (points, symbol
//     5000) and "line" (lines, symbol 5001) — the 1st-chance fallback;
//   * a known FACC where no condition evaluated true falls back to "defpt"
//     (0051, black dot) and "defln" (3113, black line) — the 2nd chance.
// Those four fallbacks are hardcoded in the original too, not table data.
//
// UNITS: symbol geometry and line widths are HIMETRIC (0.01 mm) — see
// fvkit/vector/style.h. Dash run lengths are NOT: the CGM reader leaves
// ElementLength unscaled (unlike LineWidth and VerticalDisplacement, which it
// multiplies by 100) and DrawLine then compares it directly against device
// pixel distances. Preserved as-is per the bit-faithful rule; see kDashNote
// in the .cpp.

#ifndef FV_GEOSYM_STYLE_H_
#define FV_GEOSYM_STYLE_H_

#include <memory>
#include <string>
#include <vector>

#include "fvkit/vector/lookup_engine.h"
#include "fvkit/vector/rules.h"
#include "fvkit/vector/style.h"

namespace fv {

// VPF product identifiers (fullsym.txt's `pid` column, decoded by CODE.TXT).
enum GeoSymProduct {
  kGeoSymVmapLevel0 = 1,
  kGeoSymVmapLevel1 = 2,
  kGeoSymVmapLevel2 = 3,
  kGeoSymUvmap = 4,
  kGeoSymDnc = 5,
  kGeoSymVitd = 9,
  kGeoSymVvod = 16,
};

// The open flag, the label switch, the R2 rule layer (rules() /
// viewing_groups() / rule_predicate_evaluations()) and the symbol display-list
// cache all live in fv::LookupTableStyleEngine as of E3c — this class is the
// GeoSym LOADER over that core: the fullsym.txt/ATTEXP/COLOR/TEXT tables plus
// the assignment semantics above. `rules()` matches on the FACC as style_key
// and the VPF feature class as layer.
class GeoSymStyleEngine : public LookupTableStyleEngine {
 public:
  GeoSymStyleEngine();
  ~GeoSymStyleEngine() override;

  GeoSymStyleEngine(const GeoSymStyleEngine&) = delete;
  GeoSymStyleEngine& operator=(const GeoSymStyleEngine&) = delete;

  // `data_dir` is GeoSym's DataDir (TestData in this tree): the reader
  // composes <data_dir>\GeoSymbol\SymAssign\… and \GeoSymbol\Graphics\.
  // Backslashes and case resolve at the file-open boundary as everywhere else.
  Status Open(const std::string& data_dir, int product_id = kGeoSymDnc);

  // CSymColorAdjuster knobs, -100..100 each. Applied to every colour the
  // engine hands out, exactly where sld.m_ColorAdjuster sat on Windows.
  void SetColorAdjust(int brightness, int contrast);

  // Diagnostics / tests.
  size_t row_count() const;             // assignment rows kept for the product
  size_t symbols_loaded() const;        // CGM files parsed so far
  const std::string& data_dir() const;

 protected:
  Status StyleFeature(const VectorFeature& f, const StyleContext& ctx,
                     const StylePass& rule_pass,
                     std::vector<StyleResult>* out) override;
  bool LoadSymbol(const std::string& symbol_id, VectorSymbol* out) override;
  // SanSymbol/SymText's "make sure it is something viewable" cutoff.
  bool AcceptContext(const StyleContext& ctx) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fv

#endif  // FV_GEOSYM_STYLE_H_
