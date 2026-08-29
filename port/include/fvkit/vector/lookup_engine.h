// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::LookupTableStyleEngine — the shared core of a table-driven style engine
// (vpf-geosym plan §5.1, session E3c). This is the extraction §5.1 asked for
// and deliberately deferred twice: it was NOT cut against GeoSym alone, and it
// was NOT cut when only the second TABLE existed (E2). It is cut now, with two
// real products written and rendering, so the abstraction is a description of
// what they share rather than a guess about what they might.
//
// The shape §5.1's mapping table implies is a template method, not a framework:
//
//   Style()        BASE  — null/open checks, the rule layer, the per-feature
//                          StylePass. Identical in both engines, twice.
//   StyleFeature() PRODUCT — dispatch key -> table rows -> draw ops. This is
//                          the part that is genuinely different: GeoSym matches
//                          on FACC + delineation and lets EVERY row whose
//                          ATTEXP condition holds contribute a pass, S-52 picks
//                          the FIRST matching row of one of five tables and
//                          executes its instruction list. Neither is a special
//                          case of the other, which is why the row table itself
//                          stays with the product.
//   Symbol()       BASE  — the display-list cache, filled by the product's own
//                          LoadSymbol(); negative results are cached too and
//                          counted as unresolved.
//
// What the base owns, and why each one is here rather than duplicated:
//
//   * the R2 rule layer (RuleSet + ViewingGroupSet + the memoized ResolvedPlan
//     and its recompile-when-it-moved test) — cross-product by construction;
//   * the label switch, which both products keep off by default for the same
//     host-font reason;
//   * the open flag, so `IsOpen()` and Style()'s "not open" error read the same
//     from either engine;
//   * the symbol cache and the unresolved-symbol counter — a name a table
//     mentions and the library does not define is a data fact worth counting,
//     and both products have it (GeoSym silently remembered failures, S-52
//     counted them; now both count).
//
// A product engine therefore reduces to: load its tables, implement
// StyleFeature, implement LoadSymbol (or override Symbol() when its own
// library already caches display lists, as S-52's presentation library does).

#ifndef FVKIT_VECTOR_LOOKUP_ENGINE_H_
#define FVKIT_VECTOR_LOOKUP_ENGINE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "fvkit/vector/mariner.h"
#include "fvkit/vector/rules.h"
#include "fvkit/vector/style.h"

namespace fv {

// Everything the rule layer decided about ONE feature, computed once by the
// base and handed to the product. It exists so a product engine never touches
// RuleEffect's has_/value pairs — which is where both engines had the same
// four-line incantation.
struct StylePass {
  // ctx.symbol_scale already multiplied by any rule's own scale. This is the
  // number a product should size symbology with; ctx.symbol_scale alone is not.
  double symbol_scale = 1.0;

  // The engine's label switch AND the rule layer's per-feature veto.
  bool draw_labels = false;

  bool has_priority = false;
  int priority = 0;

  // The rule's multiplier on its own, for a product that has to pass it
  // downstream instead of applying it (S-52's PointSymbolStyle::scale — the
  // renderer folds ctx.symbol_scale in itself, so only the rule's half goes
  // into the draw op).
  bool has_symbol_scale = false;
  double rule_symbol_scale = 1.0;

  // The table's priority unless a rule overrode it.
  int PriorityOr(int table_priority) const {
    return has_priority ? priority : table_priority;
  }
  double SymbolScaleOr(double table_scale) const {
    return has_symbol_scale ? rule_symbol_scale : table_scale;
  }
};

class LookupTableStyleEngine : public IStyleEngine {
 public:
  LookupTableStyleEngine();
  ~LookupTableStyleEngine() override;

  LookupTableStyleEngine(const LookupTableStyleEngine&) = delete;
  LookupTableStyleEngine& operator=(const LookupTableStyleEngine&) = delete;

  bool IsOpen() const { return open_; }

  // Labels off by default: label glyphs come from the host font, which would
  // make golden hashes machine-dependent.
  void SetDrawLabels(bool on) {
    if (draw_labels_ != on) ++own_epoch_;
    draw_labels_ = on;
  }
  bool draw_labels() const { return draw_labels_; }

  // --- the cross-product rule layer (R2, plan §5.2) ------------------------
  // Both start neutral — an empty RuleSet and every group and category on — so
  // an engine nobody configures symbolizes exactly what its product tables say.
  RuleSet& rules() { return rules_; }
  const RuleSet& rules() const { return rules_; }
  ViewingGroupSet& viewing_groups() { return groups_; }
  const ViewingGroupSet& viewing_groups() const { return groups_; }

  // --- the mariner's depth numbers (fvkit/vector/mariner.h) ----------------
  // Shared, because S-52's safety/shallow/deep contours and GeoSym's
  // ssdc/msdc/mssc are the same settings under two names — see mariner.h for
  // the row-by-row derivation. A product engine seeds its OWN defaults in its
  // constructor (they differ, and both sets are pinned by goldens) and reads
  // them back out in StyleFeature.
  //
  // TWO ACCESSORS, NOT AN OVERLOAD PAIR, and the naming is the point:
  // mutable_mariner() bumps the style epoch ON CALL, because a caller holding
  // the reference can change anything at any time and the retained scene has
  // to be told — the contract S52StyleEngine::mariner() has carried since E3a.
  // Spelling both `mariner()` would make a plain READ through a non-const
  // engine silently pick the bumping one, which is a scene rebuild per read
  // (it cost this session two failing tests before the names were split).
  MarinerSettings& mutable_mariner() {
    ++own_epoch_;
    ++mariner_epoch_;
    return mariner_;
  }
  const MarinerSettings& mariner() const { return mariner_; }
  void SetMariner(const MarinerSettings& m) {
    if (m == mariner_) return;
    mariner_ = m;
    ++own_epoch_;
    ++mariner_epoch_;
  }
  // Bumped whenever the settings could have changed. A product that derives
  // something from them (GeoSym's CECDISValues) compares this instead of
  // re-deriving per feature.
  uint64_t mariner_epoch() const { return mariner_epoch_; }

  // Diagnostics: predicate evaluations performed by the rule plan since the
  // last Style() that recompiled it. Proves the plan's fast path.
  size_t rule_predicate_evaluations() const {
    return plan_.predicate_evaluations();
  }

  // Symbol names a table asked for that the product's library does not define,
  // name -> times requested. A data fact, not an error: S-52's delivered
  // library has raster-only definitions a vector path cannot draw, and a
  // GeoSym product can name a .cgm that was not delivered.
  const std::map<std::string, size_t>& unresolved_symbols() const {
    return unresolved_;
  }
  void ResetUnresolvedSymbols() { unresolved_.clear(); }

  // Display lists cached so far (failures excluded).
  size_t symbols_cached() const;

  // The template method. Product code implements StyleFeature().
  Status Style(const VectorFeature& f, const StyleContext& ctx,
               std::vector<StyleResult>* out) final;

  // Cached over LoadSymbol(). An engine whose own library already holds stable
  // display lists overrides this instead (S-52).
  const VectorSymbol* Symbol(const std::string& symbol_id) override;

  // The rule layer's two epochs plus the engine's own counter, which the label
  // switch and the product's loader bump. A retained VectorScene compares this
  // and rebuilds when it moves, so anything a product adds that changes what
  // Style() returns must call BumpStyleEpoch() — the same obligation Open()
  // and SetDrawLabels() already discharge here.
  uint64_t style_epoch() const final;

 protected:
  // For a product loader: a table reload, a mariner setting, a colour table
  // swap. Cheap and monotone; over-bumping costs a scene rebuild, and
  // under-bumping draws stale symbology, so bump when in doubt.
  void BumpStyleEpoch() { ++own_epoch_; }

  // What a product's own StyleFeature reads. Deliberately NOT spelled
  // mariner(): that overload set resolves to the MUTABLE one inside a
  // non-const member, so a product reading its safety contour per feature
  // would bump the epoch per feature and the retained scene would rebuild
  // every frame — the exact stall the epoch exists to prevent.
  const MarinerSettings& current_mariner() const { return mariner_; }

  // Called once per feature that survived the rule layer.
  virtual Status StyleFeature(const VectorFeature& f, const StyleContext& ctx,
                             const StylePass& pass,
                             std::vector<StyleResult>* out) = 0;

  // A last-chance veto on the whole render context, evaluated BEFORE the rule
  // plan so a product's own "nothing is viewable at this zoom" cutoff keeps
  // the position it has in the original renderer. Default: accept.
  virtual bool AcceptContext(const StyleContext& ctx) const;

  // Fills *out with the display list for `symbol_id`. false = this product has
  // no such symbol; the base caches that answer and counts it as unresolved.
  virtual bool LoadSymbol(const std::string& symbol_id, VectorSymbol* out);

  void NoteUnresolvedSymbol(const std::string& name) { ++unresolved_[name]; }
  void set_open(bool on) {
    open_ = on;
    ++own_epoch_;
  }
  void ClearSymbolCache() { symbols_.clear(); }

 private:
  bool open_ = false;
  bool draw_labels_ = false;
  uint64_t own_epoch_ = 1;

  MarinerSettings mariner_;
  uint64_t mariner_epoch_ = 1;

  RuleSet rules_;
  ViewingGroupSet groups_;
  ResolvedPlan plan_;
  bool plan_compiled_ = false;

  // A null entry is a remembered failure, so a missing symbol is looked for
  // once per engine rather than once per feature.
  std::unordered_map<std::string, std::unique_ptr<VectorSymbol>> symbols_;
  std::map<std::string, size_t> unresolved_;
};

// Accumulates draw ops into StyleResults. A single table row's instructions
// normally describe ONE pass (a fill plus its boundary pen plus a label), so
// they merge into one result; when an op would overwrite a slot that is already
// set — two strokes, a compound line — the current result is flushed and a new
// one started at the same priority, which is how the renderer keeps both.
//
// Product-neutral: it names no product's instructions, only style.h's slots,
// which is why it moved out of the S-52 engine and into the shared core.
class StyleResultBuilder {
 public:
  StyleResultBuilder(int priority, std::vector<StyleResult>* out) : out_(out) {
    current_.priority = priority;
  }

  StrokeStyle& Stroke() {
    if (current_.stroke.valid) Flush();
    return current_.stroke;
  }
  FillStyle& Fill() {
    if (current_.fill.valid) Flush();
    return current_.fill;
  }
  LinePatternStyle& LinePattern() {
    if (current_.line_pattern.valid) Flush();
    return current_.line_pattern;
  }
  AreaPatternStyle& AreaPattern() {
    if (current_.area_pattern.valid) Flush();
    return current_.area_pattern;
  }
  PointSymbolStyle& Symbol() {
    if (current_.symbol.valid) Flush();
    return current_.symbol;
  }
  LabelStyle& Label() {
    if (current_.label.valid) Flush();
    return current_.label;
  }

  bool AnythingDrawn() const { return drawn_ || Visible(current_); }

  void Finish() {
    if (Visible(current_)) {
      out_->push_back(current_);
      drawn_ = true;
    }
  }

 private:
  static bool Visible(const StyleResult& r) {
    return r.stroke.valid || r.fill.valid || r.symbol.valid || r.label.valid ||
           r.line_pattern.valid || r.area_pattern.valid;
  }
  void Flush() {
    if (Visible(current_)) {
      out_->push_back(current_);
      drawn_ = true;
    }
    const int p = current_.priority;
    current_ = StyleResult();
    current_.priority = p;
  }

  StyleResult current_;
  std::vector<StyleResult>* out_;
  bool drawn_ = false;
};

}  // namespace fv

#endif  // FVKIT_VECTOR_LOOKUP_ENGINE_H_
