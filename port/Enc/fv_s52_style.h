// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::S52StyleEngine — IHO S-52 as an FvKit IStyleEngine (ENC phase E3a, plan
// §5.1 and §7). The second implementation of the seam GeoSym's
// GeoSymStyleEngine was the first of, and the point of §5.1's claim that the
// two are one machine: nothing in fvkit changed to accept it.
//
// The pipeline, all of it grounded in E2's loaded tables — no symbology is
// written from memory here:
//
//   feature type + mariner settings  ->  which of the five LOOKUP TABLES
//   object acronym                   ->  that table's rows, in file order
//   attribute conditions             ->  the FIRST row that matches (S-52's
//                                        own rule; the rows are authored
//                                        most-specific-first)
//   the row's INSTRUCTIONS           ->  StyleResults
//
// INSTRUCTIONS IMPLEMENTED: SY (point symbol), LS (simple line), AC (area
// colour), TX/TE (text), CS (conditional symbology, below). Two are
// APPROXIMATED and say so at the call site: LC (complex line) strokes with the
// line-style's own pen instead of stamping the pattern along the path, and AP
// (area pattern) fills with the pattern's ink colour at reduced alpha, exactly
// the deviation GeoSym's stipple fills already carry. Both become exact when
// the along-path placer lands (E3b) — that placer is shared with GeoSym's SAMI
// lines, which is why it is not written twice here.
//
// CONDITIONAL SYMBOLOGY. S-52's CS procedures are the one thing in §5.1's
// mapping table with no GeoSym equivalent: code, not table rows. They are
// registered by name, so an unimplemented one is a missing entry rather than a
// silent hole — `unhandled_cs()` reports what a chart asked for and did not
// get, and the feature still draws, with the library's own QUESMRK1 (the S-52
// "unknown symbol" mark) as a visible placeholder. Plan §7's rule is never to
// drop a feature quietly, and an invisible rock is exactly the failure a chart
// must not have.
//
// Implemented in E3a, by weight over the Charleston cells: DEPARE01 (the depth
// ramp — the single biggest visual), DEPCNT02, SLCONS03, QUAPOS01, TOPMAR01,
// SOUNDG02. Left for E3b with placeholders: LIGHTS05, OBSTRN04, WRECKS02,
// RESTRN01, RESARE01/02, DATCVR01.
//
// UNITS: S-52 line widths are in 0.32 mm units (the same nominal pen the HPGL
// `SWn` uses), converted to pixels here with the device DPI the renderer
// supplies — a style engine owns that conversion per fvkit/vector/style.h.

#ifndef FV_S52_STYLE_H_
#define FV_S52_STYLE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fv_s52_preslib.h"
#include "fvkit/vector/lookup_engine.h"
#include "fvkit/vector/rules.h"
#include "fvkit/vector/style.h"

namespace fv {

// The mariner's own chart settings. S-52 makes these the mariner's, not the
// producer's, and they change what is drawn, not just how: the safety contour
// is the single most important line on an ECDIS display.
//
// This is the ENC half of the "MarinerSettings API" the F1 row left open. The
// GeoSym/DNC half (CECDISValues' ssdc/msdc/mssc) is the same idea under VPF
// names and is still driven by its own defaults — see the ledger.
struct S52MarinerSettings {
  // Metres. Depths are compared against these exactly as S-52 §8.4 does.
  double safety_contour = 30.0;   // the bold contour; DEPARE/DEPCNT pivot here
  double shallow_contour = 2.0;   // inner edge of the shallow-water shade
  double deep_contour = 30.0;     // outer edge of the deep-water shade
  double safety_depth = 30.0;     // soundings at or below this print bold/black
  bool two_shades = false;        // two depth shades instead of four
  bool shallow_pattern = false;   // DIAMOND1 over the shallowest band
};

enum class S52ColorScheme { kDay = 0, kDusk, kNight };

// The two mutually exclusive point tables, and the two area tables. Which pair
// is in force is a display setting, which is why E2 loads all five.
enum class S52PointStyle { kPaperChart = 0, kSimplified };
enum class S52AreaStyle { kPlainBoundaries = 0, kSymbolizedBoundaries };

// The open flag, the label switch, the R2 rule layer (rules() /
// viewing_groups()) and the unresolved-symbol counter live in
// fv::LookupTableStyleEngine as of E3c — this class is the S-52 LOADER over that
// core: the presentation library, the five lookup tables, the mariner settings
// and the CS registry. GeoSym is the other loader, and §5.1's claim that the two
// are one machine is now a shared base class rather than an observation.
class S52StyleEngine : public LookupTableStyleEngine {
 public:
  S52StyleEngine();
  ~S52StyleEngine() override;

  S52StyleEngine(const S52StyleEngine&) = delete;
  S52StyleEngine& operator=(const S52StyleEngine&) = delete;

  // `data_dir` holds chartsymbols.xml (TestData/enc in this tree); a path to
  // the file itself works too.
  Status Open(const std::string& data_dir);

  // --- mariner / display settings -----------------------------------------
  Status SetColorScheme(S52ColorScheme scheme);
  S52ColorScheme color_scheme() const;
  // S-57 META objects (M_QUAL, M_COVR, M_NSYS, ...) describe the DATASET, not
  // the world. OFF by default: M_QUAL's zones-of-confidence triangles are a
  // full-area pattern that covers the chart, and an ECDIS shows them only when
  // the mariner asks. They are NOT a display category — M_QUAL is category
  // OTHER, and so are soundings and depth contours, so a category threshold
  // that hides the quality overlay also hides half the chart.
  void SetShowMetaObjects(bool on);
  bool show_meta_objects() const;

  void SetPointStyle(S52PointStyle s);
  void SetAreaStyle(S52AreaStyle s);
  S52MarinerSettings& mariner();
  const S52MarinerSettings& mariner() const;

  // NOTE labels, rules() and viewing_groups() come from the shared core.
  // S-52's own display category (BASE/STANDARD/OTHER) is carried by each lookup
  // row and is filtered through ViewingGroupSet::CategoryEnabled, which is
  // exactly the axis GeoSym's `dispcat` column feeds — one mechanism, two
  // products, per §5.1.

  // The presentation library already holds every symbol's display list, keyed
  // and cached, so this goes straight to it rather than through the core's
  // LoadSymbol() cache.
  const VectorSymbol* Symbol(const std::string& symbol_id) override;

  // The raster half of the same library. 679 of its 1018 symbols are defined
  // as a sheet tile and nothing else, which is why a chart drawn from the
  // vector definitions alone showed a question mark over its buoys and
  // beacons; the renderer consults this whenever Symbol() has no geometry.
  const SymbolPixmap* Pixmap(const std::string& symbol_id) override;

  // The loaded library, for callers that want the tables themselves.
  const S52PresentationLibrary& library() const;

  // The lookup row `f` dispatches to — the first row of its table whose
  // attribute conditions all hold, which is the decision every StyleResult
  // below flows from. Exposed because it is the one thing worth showing in an
  // identify panel ("this drew as BOYLAT row 12") and the one thing a test can
  // assert about dispatch without going through the pixels. nullptr when the
  // engine is closed or the object class has no rows in that table.
  const S52Lookup* SelectLookup(const VectorFeature& f) const;

  // --- diagnostics ---------------------------------------------------------
  // CS procedures a styled feature asked for and this engine does not
  // implement, name -> times requested. Empty is the goal; non-empty is the
  // E3b worklist, and every one of them drew a placeholder.
  const std::map<std::string, size_t>& unhandled_cs() const;
  // Symbols an instruction named that the library does not define (E2 counted
  // 24 such names) are counted the same way, by the shared core's
  // unresolved_symbols().
  size_t placeholders_drawn() const;
  // Sector lights LIGHTS05 drew as a plain flare because the seam cannot
  // express the sector legs and arc (see the procedure's comment). Counted so
  // the deviation is measurable rather than remembered.
  size_t sector_lights_simplified() const;
  void ResetDiagnostics();

 protected:
  Status StyleFeature(const VectorFeature& f, const StyleContext& ctx,
                     const StylePass& pass,
                     std::vector<StyleResult>* out) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fv

#endif  // FV_S52_STYLE_H_
