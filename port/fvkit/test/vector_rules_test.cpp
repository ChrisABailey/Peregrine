// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/vector/rules.h tests (vpf-geosym plan §5.2, session R2).
//
// Entirely HERMETIC on purpose: no VPF, no GeoSym, no canvas. The rule layer
// is the cross-product middle of the vector seam, so if any product leaks
// into it these tests stop building — the same guarantee vector_renderer_test
// gives for the renderer.

#include "fvkit/vector/rules.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

fv::VectorFeature Feature(
    const std::string& layer, const std::string& key,
    std::vector<std::pair<std::string, std::string>> attrs = {},
    fv::VectorGeometryType type = fv::VectorGeometryType::kLine) {
  fv::VectorFeature f;
  f.layer = layer;
  f.style_key = key;
  f.attributes = std::move(attrs);
  f.type = type;
  return f;
}

// Parses `text` and fails the test with the parser's own message if it can't.
fv::RuleSet Rules(const std::string& text) {
  fv::RuleSet rs;
  std::string err;
  const fv::Status s = rs.LoadText(text, &err);
  EXPECT_TRUE(s.ok()) << err;
  return rs;
}

}  // namespace

// --- Predicate AST ---------------------------------------------------------

TEST(Predicate, AlwaysAndNever) {
  const fv::VectorFeature f = Feature("hydline", "BH140");
  EXPECT_TRUE(fv::Predicate::Always().Evaluate(f));
  EXPECT_FALSE(fv::Predicate::Never().Evaluate(f));
  EXPECT_TRUE(fv::Predicate::Always().is_always());
}

TEST(Predicate, ExistsDistinguishesAbsentFromEmpty) {
  const fv::VectorFeature f = Feature("hydline", "BH140", {{"nam", ""}});
  EXPECT_TRUE(fv::Predicate::Exists("nam").Evaluate(f));
  EXPECT_FALSE(fv::Predicate::Missing("nam").Evaluate(f));
  EXPECT_FALSE(fv::Predicate::Exists("hdp").Evaluate(f));
  EXPECT_TRUE(fv::Predicate::Missing("hdp").Evaluate(f));
}

// The documented comparison rule: numeric when BOTH sides parse whole.
TEST(Predicate, NumericComparisonWhenBothSidesAreNumbers) {
  const fv::VectorFeature f = Feature("soundp", "BE010", {{"cvl", "9"}});
  EXPECT_TRUE(fv::Predicate::Compare(fv::PredicateOp::kLess, "cvl", "10")
                  .Evaluate(f));
  // The trap this rule exists to avoid: "9" < "10" is FALSE as strings.
  EXPECT_FALSE(fv::Predicate::Compare(fv::PredicateOp::kGreaterEqual, "cvl",
                                      "10")
                   .Evaluate(f));
  EXPECT_TRUE(fv::Predicate::Compare(fv::PredicateOp::kGreater, "cvl", "8.5")
                  .Evaluate(f));
}

TEST(Predicate, StringComparisonWhenEitherSideIsNotANumber) {
  const fv::VectorFeature f = Feature("hydline", "BH140", {{"nam", "3a"}});
  // "3a" does not parse whole, so this is a byte-wise compare, not 3 < 10.
  EXPECT_FALSE(fv::Predicate::Compare(fv::PredicateOp::kLess, "nam", "10")
                   .Evaluate(f));
  EXPECT_TRUE(fv::Predicate::Compare(fv::PredicateOp::kEqual, "nam", "3a")
                  .Evaluate(f));
}

TEST(Predicate, MissingAttributeMakesEveryComparisonFalse) {
  const fv::VectorFeature f = Feature("hydline", "BH140");
  for (fv::PredicateOp op :
       {fv::PredicateOp::kEqual, fv::PredicateOp::kNotEqual,
        fv::PredicateOp::kLess, fv::PredicateOp::kGreater}) {
    EXPECT_FALSE(fv::Predicate::Compare(op, "hdp", "1").Evaluate(f))
        << "op " << static_cast<int>(op);
  }
  // kNotIn included: it is a claim about a value that is present.
  EXPECT_FALSE(fv::Predicate::In("hdp", {"1"}, true).Evaluate(f));
}

TEST(Predicate, InAndNotIn) {
  const fv::VectorFeature f = Feature("navaid", "BC050", {{"cat", "3"}});
  EXPECT_TRUE(fv::Predicate::In("cat", {"1", "3", "7"}).Evaluate(f));
  EXPECT_FALSE(fv::Predicate::In("cat", {"1", "7"}).Evaluate(f));
  EXPECT_TRUE(fv::Predicate::In("cat", {"1", "7"}, true).Evaluate(f));
}

TEST(Predicate, BooleanComposition) {
  const fv::VectorFeature f =
      Feature("hydline", "BH140", {{"hdp", "5"}, {"nam", "Ashley"}});
  const fv::Predicate p = fv::Predicate::And(
      {fv::Predicate::Compare(fv::PredicateOp::kGreater, "hdp", "3"),
       fv::Predicate::Or({fv::Predicate::Exists("nam"),
                          fv::Predicate::Exists("zzz")})});
  EXPECT_TRUE(p.Evaluate(f));
  EXPECT_FALSE(fv::Predicate::Not(p).Evaluate(f));
}

// --- ScaleBand -------------------------------------------------------------

TEST(ScaleBand, DenominatorSemantics) {
  fv::ScaleBand zoomed_out;  // 1:25k and coarser
  zoomed_out.min_denom = 25000.0;
  EXPECT_FALSE(zoomed_out.Contains(10000.0));
  EXPECT_TRUE(zoomed_out.Contains(25000.0));
  EXPECT_TRUE(zoomed_out.Contains(1000000.0));

  fv::ScaleBand zoomed_in;  // 1:100k and finer
  zoomed_in.max_denom = 100000.0;
  EXPECT_TRUE(zoomed_in.Contains(50000.0));
  EXPECT_FALSE(zoomed_in.Contains(250000.0));

  // "No scale" must never lose features to thinning nobody asked for.
  EXPECT_TRUE(zoomed_out.Contains(0.0));
  EXPECT_TRUE(zoomed_in.Contains(0.0));
  EXPECT_TRUE(fv::ScaleBand().unbounded());
}

// --- ViewingGroupSet -------------------------------------------------------

TEST(ViewingGroupSet, GroupZeroIsAlwaysOn) {
  fv::ViewingGroupSet g;
  g.SetDefault(false);
  EXPECT_TRUE(g.Enabled(0));
  EXPECT_FALSE(g.Enabled(27070));
  g.Set(27070, true);
  EXPECT_TRUE(g.Enabled(27070));
  // Setting group 0 is a no-op, not an override.
  const uint64_t e = g.epoch();
  g.Set(0, false);
  EXPECT_EQ(e, g.epoch());
  EXPECT_TRUE(g.Enabled(0));
}

TEST(ViewingGroupSet, EpochBumpsOnlyOnRealChange) {
  fv::ViewingGroupSet g;
  const uint64_t start = g.epoch();
  g.Set(100, true);  // default is already on, but an override is recorded
  const uint64_t after = g.epoch();
  EXPECT_GT(after, start);
  g.Set(100, true);  // same value again
  EXPECT_EQ(after, g.epoch());
  g.SetMaxCategory(fv::kDisplayStandard);
  EXPECT_GT(g.epoch(), after);
}

TEST(ViewingGroupSet, DisplayCategoryIsAThreshold) {
  fv::ViewingGroupSet g;
  EXPECT_TRUE(g.CategoryEnabled(fv::kDisplayOther));
  g.SetMaxCategory(fv::kDisplayStandard);
  EXPECT_TRUE(g.CategoryEnabled(fv::kDisplayBase));
  EXPECT_TRUE(g.CategoryEnabled(fv::kDisplayStandard));
  EXPECT_FALSE(g.CategoryEnabled(fv::kDisplayOther));
  // A row the table left blank always shows.
  EXPECT_TRUE(g.CategoryEnabled(fv::kDisplayCategoryNone));
}

// --- Rule file parsing -----------------------------------------------------

TEST(RuleFile, SelectorsAndActions) {
  const fv::RuleSet rs = Rules(R"(
# a comment, and a blank line follows

hide  layer=hydline
show  key=BE010  scale=..50000
set   fcode=DA010  priority=3  labels=off  symbolscale=1.5
hide  geom=area  group=27070  category=other
)");
  ASSERT_EQ(4u, rs.size());

  EXPECT_EQ(fv::RuleAction::kHide, rs.rules()[0].action);
  EXPECT_EQ("hydline", rs.rules()[0].match.layer);
  EXPECT_TRUE(rs.rules()[0].cond.is_always());

  EXPECT_EQ(fv::RuleAction::kShow, rs.rules()[1].action);
  EXPECT_EQ("BE010", rs.rules()[1].match.style_key);
  EXPECT_EQ(0.0, rs.rules()[1].scale.min_denom);
  EXPECT_EQ(50000.0, rs.rules()[1].scale.max_denom);

  const fv::Rule& set = rs.rules()[2];
  EXPECT_EQ(fv::RuleAction::kModify, set.action);
  EXPECT_TRUE(set.effect.has_priority);
  EXPECT_EQ(3, set.effect.priority);
  EXPECT_TRUE(set.effect.has_labels);
  EXPECT_FALSE(set.effect.labels);
  EXPECT_DOUBLE_EQ(1.5, set.effect.symbol_scale);

  const fv::Rule& area = rs.rules()[3];
  EXPECT_TRUE(area.has_geometry);
  EXPECT_EQ(fv::VectorGeometryType::kArea, area.geometry);
  EXPECT_EQ(27070, area.viewing_group);
  EXPECT_EQ(fv::kDisplayOther, area.display_category);
}

TEST(RuleFile, PredicateRoundTrip) {
  const fv::RuleSet rs = Rules(
      "hide key=BH140 where hdp exists and (hdp < 3 or nam in (a, \"b c\"))\n"
      "hide key=BC050 where not cat missing\n"
      "hide key=DA010 where nam != \"Fort Sumter\"\n");
  ASSERT_EQ(3u, rs.size());
  EXPECT_EQ("hdp exists and (hdp < 3 or nam in (a, \"b c\"))",
            rs.rules()[0].cond.ToText());
  EXPECT_EQ("not cat missing", rs.rules()[1].cond.ToText());
  EXPECT_EQ("nam != \"Fort Sumter\"", rs.rules()[2].cond.ToText());

  // …and it means what it says.
  EXPECT_TRUE(rs.rules()[0].cond.Evaluate(
      Feature("l", "BH140", {{"hdp", "2"}})));
  EXPECT_FALSE(rs.rules()[0].cond.Evaluate(
      Feature("l", "BH140", {{"hdp", "9"}})));
  EXPECT_TRUE(rs.rules()[0].cond.Evaluate(
      Feature("l", "BH140", {{"hdp", "9"}, {"nam", "b c"}})));
}

// `not` binds tighter than `and`, so ToText must parenthesize a compound
// child or the text it prints means something else than the tree it came from.
TEST(RuleFile, NotOfACompoundRoundTripsThroughItsOwnText) {
  const fv::RuleSet rs = Rules("hide key=X where not (a = 1 and b = 1)\n");
  const fv::Predicate& p = rs.rules()[0].cond;
  EXPECT_EQ("not (a = 1 and b = 1)", p.ToText());

  const fv::RuleSet again = Rules("hide key=X where " + p.ToText() + "\n");
  const fv::VectorFeature f = Feature("l", "X", {{"a", "1"}});
  EXPECT_EQ(p.Evaluate(f), again.rules()[0].cond.Evaluate(f));
  EXPECT_TRUE(p.Evaluate(f)) << "a=1 but b is absent, so the and is false";
  EXPECT_FALSE(p.Evaluate(Feature("l", "X", {{"a", "1"}, {"b", "1"}})));
}

TEST(RuleFile, AndBindsTighterThanOr) {
  const fv::RuleSet rs = Rules("hide key=X where a = 1 or b = 1 and c = 1\n");
  const fv::Predicate& p = rs.rules()[0].cond;
  EXPECT_EQ(fv::PredicateOp::kOr, p.op);
  // a=1 alone satisfies it; b=1 alone does not (it needs c too).
  EXPECT_TRUE(p.Evaluate(Feature("l", "X", {{"a", "1"}})));
  EXPECT_FALSE(p.Evaluate(Feature("l", "X", {{"b", "1"}})));
  EXPECT_TRUE(p.Evaluate(Feature("l", "X", {{"b", "1"}, {"c", "1"}})));
}

// A rule file is authored by a human; half of one applied is worse than none.
TEST(RuleFile, BadLineIsAllOrNothingAndNamesTheLine) {
  fv::RuleSet rs;
  std::string err;
  const fv::Status s = rs.LoadText(
      "hide layer=hydline\n"
      "hide key=BE010 where hdp <<\n",
      &err);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(fv::kInvalidArg, s.code);
  EXPECT_NE(std::string::npos, err.find("line 2")) << err;
  EXPECT_EQ(0u, rs.size()) << "the good first line must not have been kept";
}

TEST(RuleFile, RejectsUnknownActionAndSelector) {
  fv::RuleSet rs;
  std::string err;
  EXPECT_FALSE(rs.LoadText("paint layer=x\n", &err).ok());
  EXPECT_NE(std::string::npos, err.find("paint")) << err;
  EXPECT_FALSE(rs.LoadText("hide colour=red\n", &err).ok());
  EXPECT_NE(std::string::npos, err.find("colour")) << err;
  EXPECT_FALSE(rs.LoadText("hide scale=50000\n", &err).ok());
}

// --- ResolvedPlan ----------------------------------------------------------

TEST(ResolvedPlan, EmptyRuleSetIsTrivialAndShowsEverything) {
  const fv::RuleSet rs;
  const fv::ViewingGroupSet g;
  fv::ResolvedPlan plan;
  plan.Compile(rs, 50000.0, g);
  EXPECT_TRUE(plan.trivial());
  fv::RuleEffect e;
  EXPECT_TRUE(plan.Evaluate(Feature("hydline", "BH140"), &e));
  EXPECT_TRUE(e.empty());
  EXPECT_EQ(0u, plan.predicate_evaluations());
}

// The plan's headline claim: a key whose rules carry no predicate costs one
// hash lookup and ZERO predicate evaluations, however many features share it.
TEST(ResolvedPlan, KeyOnlyRulesEvaluateNoPredicates) {
  const fv::RuleSet rs = Rules(
      "hide layer=hydline\n"
      "show key=BE010\n");
  const fv::ViewingGroupSet g;
  fv::ResolvedPlan plan;
  plan.Compile(rs, 50000.0, g);
  ASSERT_FALSE(plan.trivial());

  for (int i = 0; i < 1000; ++i) {
    EXPECT_FALSE(plan.Evaluate(Feature("hydline", "BH140"), nullptr));
    EXPECT_TRUE(plan.Evaluate(Feature("hydline", "BE010"), nullptr));
    EXPECT_TRUE(plan.Evaluate(Feature("soundp", "BE010"), nullptr));
  }
  EXPECT_EQ(0u, plan.predicate_evaluations());
  EXPECT_EQ(3u, plan.keys_cached());
  EXPECT_FALSE(plan.Decide({"hydline", "BH140"}).needs_predicates());
}

TEST(ResolvedPlan, ConditionalRulesEvaluateOncePerFeature) {
  const fv::RuleSet rs = Rules("hide key=BE010 where cvl > 20\n");
  const fv::ViewingGroupSet g;
  fv::ResolvedPlan plan;
  plan.Compile(rs, 0.0, g);
  EXPECT_TRUE(plan.Decide({"soundp", "BE010"}).needs_predicates());

  EXPECT_FALSE(plan.Evaluate(Feature("soundp", "BE010", {{"cvl", "30"}}), nullptr));
  EXPECT_TRUE(plan.Evaluate(Feature("soundp", "BE010", {{"cvl", "10"}}), nullptr));
  EXPECT_EQ(2u, plan.predicate_evaluations());
  // A key no rule mentions still short-circuits.
  EXPECT_TRUE(plan.Evaluate(Feature("soundp", "BH140", {{"cvl", "30"}}), nullptr));
  EXPECT_EQ(2u, plan.predicate_evaluations());
}

// Source order decides, and later wins — including when an unconditional rule
// follows a conditional one (the case a naive fold gets backwards).
TEST(ResolvedPlan, LaterRulesWinAcrossTheConditionalBoundary) {
  const fv::RuleSet rs = Rules(
      "show key=BE010 where cvl > 1\n"
      "hide key=BE010\n");
  const fv::ViewingGroupSet g;
  fv::ResolvedPlan plan;
  plan.Compile(rs, 0.0, g);
  const fv::VectorFeature f = Feature("soundp", "BE010", {{"cvl", "9"}});
  EXPECT_FALSE(plan.Evaluate(f, nullptr)) << "the trailing hide must win";

  const fv::RuleSet rs2 = Rules(
      "hide key=BE010\n"
      "show key=BE010 where cvl > 1\n");
  fv::ResolvedPlan plan2;
  plan2.Compile(rs2, 0.0, g);
  EXPECT_TRUE(plan2.Evaluate(f, nullptr)) << "the trailing show must win";
  EXPECT_FALSE(plan2.Evaluate(Feature("soundp", "BE010", {{"cvl", "0"}}),
                              nullptr));
}

TEST(ResolvedPlan, EffectsMergePerFieldWithLaterWinning) {
  const fv::RuleSet rs = Rules(
      "set key=DA010 priority=2 labels=off\n"
      "set key=DA010 priority=7\n");
  const fv::ViewingGroupSet g;
  fv::ResolvedPlan plan;
  plan.Compile(rs, 0.0, g);
  fv::RuleEffect e;
  EXPECT_TRUE(plan.Evaluate(Feature("l", "DA010"), &e));
  EXPECT_TRUE(e.has_priority);
  EXPECT_EQ(7, e.priority);
  EXPECT_TRUE(e.has_labels);
  EXPECT_FALSE(e.labels) << "the second rule set no label field, so the "
                            "first one's must survive";
}

TEST(ResolvedPlan, ScaleFilteringHappensAtCompileTime) {
  const fv::RuleSet rs = Rules("hide key=BE010 scale=250000..\n");
  const fv::ViewingGroupSet g;
  fv::ResolvedPlan plan;

  plan.Compile(rs, 50000.0, g);  // zoomed in: the rule is not in the plan
  EXPECT_TRUE(plan.trivial());
  EXPECT_TRUE(plan.Evaluate(Feature("soundp", "BE010"), nullptr));

  plan.Compile(rs, 1000000.0, g);  // zoomed out: it is
  EXPECT_FALSE(plan.trivial());
  EXPECT_FALSE(plan.Evaluate(Feature("soundp", "BE010"), nullptr));
}

TEST(ResolvedPlan, GeometryScopedRuleIsPerFeature) {
  const fv::RuleSet rs = Rules("hide geom=area key=BE010\n");
  const fv::ViewingGroupSet g;
  fv::ResolvedPlan plan;
  plan.Compile(rs, 0.0, g);
  EXPECT_TRUE(plan.Decide({"", "BE010"}).needs_predicates());
  EXPECT_FALSE(plan.Evaluate(
      Feature("hydarea", "BE010", {}, fv::VectorGeometryType::kArea), nullptr));
  EXPECT_TRUE(plan.Evaluate(
      Feature("hydline", "BE010", {}, fv::VectorGeometryType::kLine), nullptr));
  // …and it costs no predicate evaluations, only the geometry test.
  EXPECT_EQ(0u, plan.predicate_evaluations());
}

TEST(ResolvedPlan, ViewingGroupTogglesInvalidateAndFilter) {
  const fv::RuleSet rs = Rules("hide key=BE010 group=27070\n");
  fv::ViewingGroupSet g;
  fv::ResolvedPlan plan;
  plan.Compile(rs, 0.0, g);
  EXPECT_TRUE(plan.Valid(rs, 0.0, g));
  EXPECT_FALSE(plan.Evaluate(Feature("soundp", "BE010"), nullptr));

  g.Set(27070, false);  // the group is off, so its rule does not apply
  EXPECT_FALSE(plan.Valid(rs, 0.0, g)) << "the epoch bump must invalidate";
  plan.Compile(rs, 0.0, g);
  EXPECT_TRUE(plan.trivial());
  EXPECT_TRUE(plan.Evaluate(Feature("soundp", "BE010"), nullptr));
}

TEST(ResolvedPlan, ValidTracksScaleAndBothEpochs) {
  fv::RuleSet rs = Rules("hide key=BE010\n");
  fv::ViewingGroupSet g;
  fv::ResolvedPlan plan;
  plan.Compile(rs, 50000.0, g);
  EXPECT_TRUE(plan.Valid(rs, 50000.0, g));
  EXPECT_FALSE(plan.Valid(rs, 50001.0, g));
  rs.Add(fv::Rule());
  EXPECT_FALSE(plan.Valid(rs, 50000.0, g));
}
