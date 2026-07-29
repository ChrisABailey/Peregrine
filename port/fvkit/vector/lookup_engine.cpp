// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/vector/lookup_engine.h"

namespace fv {

LookupTableStyleEngine::LookupTableStyleEngine() = default;
LookupTableStyleEngine::~LookupTableStyleEngine() = default;

bool LookupTableStyleEngine::AcceptContext(const StyleContext&) const {
  return true;
}

bool LookupTableStyleEngine::LoadSymbol(const std::string&, VectorSymbol*) {
  return false;
}

// Three independently-bumped counters folded into one number. Mixed rather
// than added so that a change in any one of them moves the result: 1+2 and
// 2+1 are the same sum, and a scene that could not tell those apart would be
// drawn stale exactly when a rule change and a group change cancelled out.
uint64_t LookupTableStyleEngine::style_epoch() const {
  uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
  for (uint64_t v : {rules_.epoch(), groups_.epoch(), own_epoch_}) {
    h ^= v;
    h *= 1099511628211ull;
  }
  return h;
}

size_t LookupTableStyleEngine::symbols_cached() const {
  size_t n = 0;
  for (const auto& kv : symbols_)
    if (kv.second != nullptr) ++n;
  return n;
}

const VectorSymbol* LookupTableStyleEngine::Symbol(
    const std::string& symbol_id) {
  if (symbol_id.empty()) return nullptr;
  auto it = symbols_.find(symbol_id);
  if (it != symbols_.end()) return it->second.get();  // null = known failure

  std::unique_ptr<VectorSymbol> loaded(new VectorSymbol);
  if (!LoadSymbol(symbol_id, loaded.get())) {
    symbols_.emplace(symbol_id, nullptr);
    NoteUnresolvedSymbol(symbol_id);
    return nullptr;
  }
  return symbols_.emplace(symbol_id, std::move(loaded)).first->second.get();
}

Status LookupTableStyleEngine::Style(const VectorFeature& f,
                                    const StyleContext& ctx,
                                    std::vector<StyleResult>* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "null out");
  if (!open_) return Status::Error(kNotFound, "style engine not open");

  // Before the rule layer on purpose — see AcceptContext's contract.
  if (!AcceptContext(ctx)) return Status::Ok();

  // The R2 rule layer. Recompiled only when the viewport scale or a toggle
  // actually moves, so a pan costs nothing and a zoom costs one compile for the
  // whole frame. With no rules the plan is trivial and Evaluate() is a branch.
  if (!plan_compiled_ || !plan_.Valid(rules_, ctx.scale_denominator, groups_)) {
    plan_.Compile(rules_, ctx.scale_denominator, groups_);
    plan_compiled_ = true;
  }
  RuleEffect effect;
  if (!plan_.Evaluate(f, &effect)) return Status::Ok();

  StylePass pass;
  pass.has_symbol_scale = effect.has_symbol_scale;
  pass.rule_symbol_scale = effect.symbol_scale;
  pass.symbol_scale =
      ctx.symbol_scale * (effect.has_symbol_scale ? effect.symbol_scale : 1.0);
  pass.draw_labels = draw_labels_ && (!effect.has_labels || effect.labels);
  pass.has_priority = effect.has_priority;
  pass.priority = effect.priority;

  return StyleFeature(f, ctx, pass, out);
}

}  // namespace fv
