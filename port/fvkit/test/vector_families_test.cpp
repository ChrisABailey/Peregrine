// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/vector/families.h tests.
//
// HERMETIC like the rule tests it sits on: no VPF, no ENC, no OSM, no canvas.
// The one place real product data appears is the last block, which loads the
// three files actually shipped under port/families/ — because a starter file
// that does not parse is worse than none, and the selectors in them are the
// only part of this feature a user edits.

#include "fvkit/vector/families.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

fv::VectorFeature Feature(const std::string& layer, const std::string& key,
                          std::vector<std::pair<std::string, std::string>> attrs = {}) {
  fv::VectorFeature f;
  f.layer = layer;
  f.style_key = key;
  f.attributes = std::move(attrs);
  f.type = fv::VectorGeometryType::kLine;
  return f;
}

// Loads families, compiles a plan over them, and answers "is this drawn?".
class Visibility {
 public:
  explicit Visibility(const fv::FamilySet& fams) {
    std::string err;
    EXPECT_TRUE(fams.AppendRules(&rules_, &err).ok()) << err;
    plan_.Compile(rules_, 0.0, groups_);
  }
  bool operator()(const fv::VectorFeature& f) const {
    return plan_.Evaluate(f, nullptr);
  }
  const fv::RuleSet& rules() const { return rules_; }

 private:
  fv::RuleSet rules_;
  fv::ViewingGroupSet groups_;
  fv::ResolvedPlan plan_;
};

const char* kTwoFamilies = R"({
  "product": "test",
  "families": [
    { "name": "navaids", "title": "Aids to Navigation",
      "select": ["layer=buoybcnp", "layer=lightsp"] },
    { "name": "bottom", "select": ["layer=botcharp"] }
  ]
})";

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST(Families, LoadsNamesTitlesAndSelectors) {
  fv::FamilySet f;
  std::string err;
  ASSERT_TRUE(f.LoadJson(kTwoFamilies, &err).ok()) << err;
  EXPECT_EQ(f.product(), "test");
  ASSERT_EQ(f.families().size(), 2u);
  EXPECT_EQ(f.families()[0].name, "navaids");
  EXPECT_EQ(f.families()[0].title, "Aids to Navigation");
  ASSERT_EQ(f.families()[0].select.size(), 2u);
  // Everything is on until someone says otherwise.
  EXPECT_TRUE(f.Enabled("navaids"));
  EXPECT_EQ(f.disabled_count(), 0u);
}

// A name nobody declared cannot be hiding anything.
TEST(Families, AnUnknownFamilyIsEnabledAndCannotBeSet) {
  fv::FamilySet f;
  ASSERT_TRUE(f.LoadJson(kTwoFamilies).ok());
  EXPECT_TRUE(f.Enabled("no_such_family"));
  EXPECT_FALSE(f.SetEnabled("no_such_family", false));
  EXPECT_EQ(f.disabled_count(), 0u);
}

// JSON WITH COMMENTS, deliberately: this file is authored by a human, and S1
// rejected JSON for settings on exactly the grounds that it cannot be
// annotated. The loader turns comments on so that objection does not apply.
TEST(Families, CommentsAreAllowed) {
  fv::FamilySet f;
  std::string err;
  ASSERT_TRUE(f.LoadJson(R"({
    // which product this is for
    "product": "test",
    "families": [
      { "name": "a", "select": ["layer=x"] }  // one family
    ]
  })", &err).ok()) << err;
  EXPECT_EQ(f.families().size(), 1u);
}

// All-or-nothing, like every other authored file in the tree. A typo in the
// FOURTH family must not leave the first three loaded.
TEST(Families, ABadSelectorRejectsTheWholeFileAndNamesIt) {
  fv::FamilySet f;
  ASSERT_TRUE(f.LoadJson(kTwoFamilies).ok());
  const size_t before = f.families().size();

  std::string err;
  const fv::Status s = f.LoadJson(R"({
    "families": [
      { "name": "ok", "select": ["layer=a"] },
      { "name": "typo", "select": ["layr=a"] }
    ]
  })", &err);
  EXPECT_FALSE(s.ok());
  EXPECT_NE(err.find("typo"), std::string::npos) << err;
  EXPECT_NE(err.find("layr=a"), std::string::npos) << err;
  // The previously loaded set is untouched.
  EXPECT_EQ(f.families().size(), before);
  EXPECT_TRUE(f.Enabled("navaids"));
}

TEST(Families, StructuralErrorsAreReportedNotIgnored) {
  fv::FamilySet f;
  std::string err;
  EXPECT_FALSE(f.LoadJson("not json at all", &err).ok());
  EXPECT_FALSE(f.LoadJson(R"({"families": [], "x": )", &err).ok());
  EXPECT_FALSE(f.LoadJson(R"({"nothing": 1})", &err).ok());
  EXPECT_FALSE(f.LoadJson(R"({"families":[{"select":["layer=a"]}]})", &err).ok());
  EXPECT_FALSE(f.LoadJson(R"({"families":[{"name":"a"}]})", &err).ok());
  EXPECT_FALSE(
      f.LoadJson(R"({"families":[{"name":"a","select":["layer=a"],
                                  "enabled":"yes"}]})", &err).ok());
  // A duplicate name would make SetEnabled ambiguous, so it is an error.
  EXPECT_FALSE(f.LoadJson(R"({"families":[{"name":"a","select":["layer=a"]},
                                          {"name":"a","select":["layer=b"]}]})",
                          &err).ok());
  EXPECT_FALSE(err.empty());
}

TEST(Families, MissingFileIsNotFound) {
  fv::FamilySet f;
  std::string err;
  const fv::Status s = f.LoadFile("/no/such/families.json", &err);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code, fv::kNotFound);
  EXPECT_FALSE(err.empty());
}

// ---------------------------------------------------------------------------
// What switching one off actually does
// ---------------------------------------------------------------------------

TEST(Families, EnabledFamiliesEmitNoRulesAtAll) {
  fv::FamilySet f;
  ASSERT_TRUE(f.LoadJson(kTwoFamilies).ok());
  fv::RuleSet rules;
  ASSERT_TRUE(f.AppendRules(&rules).ok());
  // Not `show` rules: a show would beat a hide authored before it, and
  // families are not meant to fight each other or a user's rule file.
  EXPECT_TRUE(rules.empty());
}

TEST(Families, DisablingAFamilyHidesEveryLayerItNames) {
  fv::FamilySet f;
  ASSERT_TRUE(f.LoadJson(kTwoFamilies).ok());
  ASSERT_TRUE(f.SetEnabled("navaids", false));
  EXPECT_EQ(f.disabled_count(), 1u);

  Visibility visible(f);
  EXPECT_EQ(visible.rules().size(), 2u);  // one per selector
  EXPECT_FALSE(visible(Feature("buoybcnp", "BB120")));
  EXPECT_FALSE(visible(Feature("lightsp", "BC050")));
  // The other family, and anything in no family at all, is untouched.
  EXPECT_TRUE(visible(Feature("botcharp", "BA040")));
  EXPECT_TRUE(visible(Feature("hydline", "BE010")));
}

// Chris's own example: on DNC the navigational aids and the bottom
// characteristics live in different coverages, so they switch independently.
TEST(Families, TwoFamiliesSwitchIndependently) {
  fv::FamilySet f;
  ASSERT_TRUE(f.LoadJson(kTwoFamilies).ok());
  ASSERT_TRUE(f.SetEnabled("bottom", false));

  Visibility visible(f);
  EXPECT_TRUE(visible(Feature("buoybcnp", "BB120")));
  EXPECT_FALSE(visible(Feature("botcharp", "BA040")));
}

// A selector is a full rule-file selector, not just a layer name — which is
// what makes an ARBITRARY family possible rather than one family per layer.
TEST(Families, ASelectorCanCarryAPredicate) {
  fv::FamilySet f;
  std::string err;
  ASSERT_TRUE(f.LoadJson(R"({
    "families": [
      { "name": "deep", "select": ["layer=soundp where hdp > 30"] }
    ]
  })", &err).ok()) << err;
  ASSERT_TRUE(f.SetEnabled("deep", false));

  Visibility visible(f);
  EXPECT_FALSE(visible(Feature("soundp", "BE020", {{"hdp", "55"}})));
  EXPECT_TRUE(visible(Feature("soundp", "BE020", {{"hdp", "4"}})));
}

TEST(Families, SetEnabledMovesTheEpochOnlyWhenSomethingChanged) {
  fv::FamilySet f;
  ASSERT_TRUE(f.LoadJson(kTwoFamilies).ok());
  const uint64_t e0 = f.epoch();
  EXPECT_TRUE(f.SetEnabled("navaids", true));  // already on
  EXPECT_EQ(f.epoch(), e0);
  EXPECT_TRUE(f.SetEnabled("navaids", false));
  EXPECT_NE(f.epoch(), e0);
}

// The override direction the header promises: families load FIRST, a user rule
// file second, so a rule file can put one thing back.
TEST(Families, AUserRuleFileLoadedAfterwardsWinsOverAFamily) {
  fv::FamilySet f;
  ASSERT_TRUE(f.LoadJson(kTwoFamilies).ok());
  ASSERT_TRUE(f.SetEnabled("navaids", false));

  fv::RuleSet rules;
  ASSERT_TRUE(f.AppendRules(&rules).ok());
  std::string err;
  ASSERT_TRUE(rules.LoadText("show layer=lightsp\n", &err).ok()) << err;

  fv::ViewingGroupSet groups;
  fv::ResolvedPlan plan;
  plan.Compile(rules, 0.0, groups);
  EXPECT_FALSE(plan.Evaluate(Feature("buoybcnp", "BB120"), nullptr));
  EXPECT_TRUE(plan.Evaluate(Feature("lightsp", "BC050"), nullptr));
}

// ---------------------------------------------------------------------------
// The files actually shipped
// ---------------------------------------------------------------------------

// WHAT THIS DOES NOT PIN: which families are ON. These files are a user's
// settings — Chris switched five ENC families off the day after they shipped —
// so asserting "nothing is disabled" would make editing the settings break the
// build. That is the same trap as pinning a total over a data directory, one
// step up: pin the MECHANISM (every selector parses, and the rules that come
// out match what the file says), never the choices.
TEST(Families, TheShippedFilesLoadAndAgreeWithTheirOwnFlags) {
  struct Case { const char* path; const char* product; size_t least; };
  const Case cases[] = {
      {FV_FAMILIES_DNC, "dnc", 12},
      {FV_FAMILIES_ENC, "enc", 15},
      {FV_FAMILIES_OSM, "osm", 8},
  };
  for (const Case& c : cases) {
    fv::FamilySet f;
    std::string err;
    // Loading at all is most of the value: every selector is validated here,
    // so a typo in a hand-edited file fails this test rather than silently
    // hiding nothing at run time.
    ASSERT_TRUE(f.LoadFile(c.path, &err).ok()) << err;
    EXPECT_EQ(f.product(), c.product);
    EXPECT_GE(f.families().size(), c.least) << c.path;

    // Every family must be able to hide something, or it is a typo nobody
    // will notice until they switch it off.
    size_t selectors_of_disabled = 0;
    for (const fv::FeatureFamily& fam : f.families()) {
      EXPECT_FALSE(fam.name.empty());
      EXPECT_FALSE(fam.select.empty()) << fam.name;
      if (!fam.enabled) selectors_of_disabled += fam.select.size();
    }
    // One hide rule per selector of every disabled family, and nothing for
    // the enabled ones — whatever the file happens to say today.
    fv::RuleSet rules;
    ASSERT_TRUE(f.AppendRules(&rules, &err).ok()) << err;
    EXPECT_EQ(rules.size(), selectors_of_disabled) << c.path;
    EXPECT_EQ(f.disabled_count() == 0, rules.empty()) << c.path;
  }
}

// The DNC file is the one that answers the question this feature came from,
// so it gets its own assertion rather than riding the loop above.
TEST(Families, TheDncFileSeparatesNavaidsFromBottomCharacteristics) {
  fv::FamilySet f;
  std::string err;
  ASSERT_TRUE(f.LoadFile(FV_FAMILIES_DNC, &err).ok()) << err;
  ASSERT_NE(f.Find("navaids"), nullptr);
  ASSERT_NE(f.Find("bottom"), nullptr);
  // Set both explicitly rather than trusting the file's current flags.
  ASSERT_TRUE(f.SetEnabled("navaids", true));
  ASSERT_TRUE(f.SetEnabled("bottom", false));

  Visibility visible(f);
  EXPECT_FALSE(visible(Feature("botcharp", "BA040")));
  EXPECT_TRUE(visible(Feature("buoybcnp", "BB120")));
  EXPECT_TRUE(visible(Feature("hydarea", "BE010")));
}

}  // namespace
