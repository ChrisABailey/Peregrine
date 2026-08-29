// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::LookupTableStyleEngine — the shared table-driven style-engine core
// extracted in E3c (plan §5.1).
//
// HERMETIC ON PURPOSE, exactly like vector_rules_test and the placer tests:
// nothing here includes VPF, GeoSym, ENC or a canvas. The engine under test is
// a ~40-line third product invented in this file, which is the real claim being
// made — that a new product (OSM next) implements StyleFeature and LoadSymbol
// and gets the rule layer, the label switch, the symbol cache and the
// unresolved-symbol counter for free. If any of that leaked back into a product
// header, this file would not compile.

#include <string>
#include <vector>

#include "fvkit/vector/lookup_engine.h"
#include "gtest/gtest.h"

namespace {

using fv::CompareRuleValues;
using fv::LookupTableStyleEngine;
using fv::Rule;
using fv::RuleAction;
using fv::RuleValueAsNumber;
using fv::RuleValuesEqual;
using fv::Status;
using fv::StyleContext;
using fv::StylePass;
using fv::StyleResult;
using fv::StyleResultBuilder;
using fv::SymbolPoint;
using fv::SymbolPrimitive;
using fv::VectorFeature;
using fv::VectorGeometryType;
using fv::VectorSymbol;

// The invented third product: one stroke per feature, priority 7 from its
// "table", a label when the feature carries `name`, and a symbol library that
// defines exactly one symbol.
class ToyStyleEngine : public LookupTableStyleEngine {
 public:
  void Open() { set_open(true); }

  // What the last StyleFeature() saw, so the base's contract is observable.
  int calls = 0;
  StylePass last_pass;
  StyleContext last_ctx;

  int load_symbol_calls = 0;
  bool accept_context = true;

 protected:
  bool AcceptContext(const StyleContext&) const override {
    return accept_context;
  }

  Status StyleFeature(const VectorFeature& f, const StyleContext& ctx,
                     const StylePass& pass,
                     std::vector<StyleResult>* out) override {
    ++calls;
    last_pass = pass;
    last_ctx = ctx;

    StyleResult r;
    r.priority = pass.PriorityOr(7);
    r.stroke.valid = true;
    r.stroke.pen.width = 1;
    if (pass.draw_labels) {
      const std::string* name = f.Attribute("name");
      if (name != nullptr) {
        r.label.valid = true;
        r.label.text = *name;
      }
    }
    out->push_back(r);
    return Status::Ok();
  }

  bool LoadSymbol(const std::string& id, VectorSymbol* out) override {
    ++load_symbol_calls;
    if (id != "dot") return false;
    SymbolPrimitive p;
    p.type = fv::SymbolPrimitiveType::kPolyline;
    p.points.push_back(SymbolPoint{0.0, 0.0});
    p.points.push_back(SymbolPoint{100.0, 0.0});
    p.has_stroke = true;
    out->primitives.push_back(p);
    return true;
  }
};

VectorFeature MakeFeature(const char* layer, const char* key) {
  VectorFeature f;
  f.type = VectorGeometryType::kLine;
  f.layer = layer;
  f.style_key = key;
  f.parts.push_back({fv::GeoPoint{0.0, 0.0}, fv::GeoPoint{1.0, 1.0}});
  return f;
}

// --- the template method ----------------------------------------------------

TEST(LookupEngine, ClosedEngineAndNullOutAreTheSameTwoErrorsInBothProducts) {
  ToyStyleEngine e;
  std::vector<StyleResult> out;
  const VectorFeature f = MakeFeature("roads", "primary");
  StyleContext ctx;

  Status s = e.Style(f, ctx, &out);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(fv::kNotFound, s.code);
  EXPECT_EQ(0, e.calls);

  e.Open();
  s = e.Style(f, ctx, nullptr);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(fv::kInvalidArg, s.code);
  EXPECT_EQ(0, e.calls) << "a null out must not reach the product";

  EXPECT_TRUE(e.Style(f, ctx, &out).ok());
  EXPECT_EQ(1, e.calls);
  EXPECT_EQ(1u, out.size());
  EXPECT_EQ(7, out[0].priority) << "the product's own table priority";
}

TEST(LookupEngine, AProductNeedsNoRulesToBehave) {
  ToyStyleEngine e;
  e.Open();
  std::vector<StyleResult> out;
  StyleContext ctx;
  ctx.symbol_scale = 1.0;
  ASSERT_TRUE(e.Style(MakeFeature("roads", "primary"), ctx, &out).ok());
  ASSERT_EQ(1u, out.size());
  EXPECT_TRUE(out[0].stroke.valid);
  EXPECT_EQ(1.0, e.last_pass.symbol_scale);
  EXPECT_FALSE(e.last_pass.has_priority);
  EXPECT_FALSE(e.last_pass.draw_labels);
  // The whole point of the neutral state: no rules, no predicate work.
  EXPECT_EQ(0u, e.rule_predicate_evaluations());
}

TEST(LookupEngine, RuleLayerRunsBeforeTheProductSeesTheFeature) {
  ToyStyleEngine e;
  e.Open();
  Rule hide;
  hide.match.style_key = "primary";
  hide.action = RuleAction::kHide;
  e.rules().Add(hide);

  std::vector<StyleResult> out;
  StyleContext ctx;
  EXPECT_TRUE(e.Style(MakeFeature("roads", "primary"), ctx, &out).ok());
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(0, e.calls) << "a hidden feature must not cost the product a lookup";

  EXPECT_TRUE(e.Style(MakeFeature("roads", "service"), ctx, &out).ok());
  EXPECT_EQ(1, e.calls);
  EXPECT_EQ(1u, out.size());
}

TEST(LookupEngine, StylePassCarriesTheRuleEffectSoProductsNeverUnpackIt) {
  ToyStyleEngine e;
  e.Open();
  e.SetDrawLabels(true);
  Rule set;
  set.match.style_key = "primary";
  set.action = RuleAction::kModify;
  set.effect.has_priority = true;
  set.effect.priority = 3;
  set.effect.has_symbol_scale = true;
  set.effect.symbol_scale = 2.0;
  set.effect.has_labels = true;
  set.effect.labels = false;
  e.rules().Add(set);

  VectorFeature f = MakeFeature("roads", "primary");
  f.attributes.push_back({"name", "Bay Street"});

  std::vector<StyleResult> out;
  StyleContext ctx;
  ctx.symbol_scale = 1.5;
  ASSERT_TRUE(e.Style(f, ctx, &out).ok());
  ASSERT_EQ(1u, out.size());

  // symbol_scale is the PRODUCT of the caller's zoom and the rule's multiplier
  // — the line both engines had to write for themselves before E3c.
  EXPECT_DOUBLE_EQ(3.0, e.last_pass.symbol_scale);
  EXPECT_DOUBLE_EQ(2.0, e.last_pass.SymbolScaleOr(1.0));
  EXPECT_EQ(3, out[0].priority) << "the rule overrode the table's 7";
  // The engine's own switch is on, but the rule vetoed this feature's label.
  EXPECT_FALSE(e.last_pass.draw_labels);
  EXPECT_FALSE(out[0].label.valid);

  // Same feature, no rule veto: the engine switch decides.
  out.clear();
  ASSERT_TRUE(e.Style(MakeFeature("roads", "service"), ctx, &out).ok());
  ASSERT_EQ(1u, out.size());
  EXPECT_TRUE(e.last_pass.draw_labels);
}

TEST(LookupEngine, AcceptContextVetoesBeforeThePlanIsEvenCompiled) {
  ToyStyleEngine e;
  e.Open();
  // A rule with a predicate, so any plan evaluation would show up in the count.
  Rule hide;
  hide.match.style_key = "primary";
  hide.action = RuleAction::kHide;
  hide.cond = fv::Predicate::Exists("name");
  e.rules().Add(hide);

  e.accept_context = false;
  std::vector<StyleResult> out;
  StyleContext ctx;
  EXPECT_TRUE(e.Style(MakeFeature("roads", "primary"), ctx, &out).ok())
      << "a vetoed context is a normal outcome, not an error";
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(0, e.calls);
  EXPECT_EQ(0u, e.rule_predicate_evaluations())
      << "GeoSym's 'nothing is viewable at this zoom' cutoff must precede the "
         "rule layer, which is why AcceptContext exists at all";

  e.accept_context = true;
  ASSERT_TRUE(e.Style(MakeFeature("roads", "primary"), ctx, &out).ok());
  EXPECT_EQ(1u, e.rule_predicate_evaluations());
}

// --- the symbol cache -------------------------------------------------------

TEST(LookupEngine, SymbolCacheLoadsOncePerNameAndRemembersFailures) {
  ToyStyleEngine e;
  e.Open();

  EXPECT_EQ(nullptr, e.Symbol("")) << "an empty id never reaches the product";
  EXPECT_EQ(0, e.load_symbol_calls);

  const VectorSymbol* first = e.Symbol("dot");
  ASSERT_NE(nullptr, first);
  EXPECT_EQ(1u, first->primitives.size());
  EXPECT_EQ(1, e.load_symbol_calls);
  EXPECT_EQ(first, e.Symbol("dot")) << "same pointer, so callers may hold it";
  EXPECT_EQ(1, e.load_symbol_calls);
  EXPECT_EQ(1u, e.symbols_cached());
  EXPECT_TRUE(e.unresolved_symbols().empty());

  // A name the product does not define: counted, and looked for exactly once
  // however many features ask for it.
  for (int i = 0; i < 5; ++i) EXPECT_EQ(nullptr, e.Symbol("nope"));
  EXPECT_EQ(2, e.load_symbol_calls);
  EXPECT_EQ(1u, e.symbols_cached()) << "a failure is not a cached display list";
  ASSERT_EQ(1u, e.unresolved_symbols().count("nope"));
  EXPECT_EQ(1u, e.unresolved_symbols().at("nope"))
      << "counted once per LOAD, not once per request — the cache is the point";

  e.ResetUnresolvedSymbols();
  EXPECT_TRUE(e.unresolved_symbols().empty());
}

// --- StyleResultBuilder -----------------------------------------------------

TEST(StyleResultBuilderTest, OneRowsInstructionsMergeIntoOnePass) {
  std::vector<StyleResult> out;
  StyleResultBuilder b(5, &out);
  EXPECT_FALSE(b.AnythingDrawn());

  b.Fill().valid = true;
  b.Stroke().valid = true;
  b.Label().valid = true;
  EXPECT_TRUE(b.AnythingDrawn());
  EXPECT_TRUE(out.empty()) << "nothing is emitted until a slot collides";
  b.Finish();

  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(5, out[0].priority);
  EXPECT_TRUE(out[0].fill.valid);
  EXPECT_TRUE(out[0].stroke.valid);
  EXPECT_TRUE(out[0].label.valid);
}

TEST(StyleResultBuilderTest, ASecondUseOfOneSlotFlushesAtTheSamePriority) {
  std::vector<StyleResult> out;
  StyleResultBuilder b(9, &out);

  b.Stroke().valid = true;
  b.Stroke().valid = true;  // a compound line: two pens, both must survive
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(9, out[0].priority);
  b.Finish();
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ(9, out[1].priority) << "priority is carried across the flush";
  EXPECT_TRUE(out[1].stroke.valid);
}

TEST(StyleResultBuilderTest, AnEmptyBuilderEmitsNothing) {
  std::vector<StyleResult> out;
  StyleResultBuilder b(1, &out);
  b.Finish();
  EXPECT_TRUE(out.empty()) << "a row that put no ink down is not a draw pass";
  EXPECT_FALSE(b.AnythingDrawn());
}

// --- the shared comparison rule ---------------------------------------------

TEST(RuleValueCompare, NumericOnlyWhenBothSidesParseWhole) {
  double v = 0.0;
  EXPECT_TRUE(RuleValueAsNumber("3", &v));
  EXPECT_DOUBLE_EQ(3.0, v);
  EXPECT_TRUE(RuleValueAsNumber("-2.5", &v));
  EXPECT_DOUBLE_EQ(-2.5, v);
  EXPECT_TRUE(RuleValueAsNumber("7 ", &v)) << "trailing whitespace only";
  EXPECT_FALSE(RuleValueAsNumber("3a", &v));
  EXPECT_FALSE(RuleValueAsNumber("", &v));
  EXPECT_FALSE(RuleValueAsNumber("3,1", &v)) << "an S-57 list value is a string";

  // The rule both style engines now share instead of each keeping a copy.
  EXPECT_TRUE(RuleValuesEqual("3", "3.0"));
  EXPECT_TRUE(RuleValuesEqual("03", "3"));
  EXPECT_FALSE(RuleValuesEqual("3,1", "3"));
  EXPECT_TRUE(RuleValuesEqual("CHBLK", "CHBLK"));
  EXPECT_FALSE(RuleValuesEqual("CHBLK", "CHGRD"));
  EXPECT_EQ(-1, CompareRuleValues("2", "10")) << "numeric, not byte-wise";
  EXPECT_EQ(1, CompareRuleValues("b", "a"));
}

}  // namespace
