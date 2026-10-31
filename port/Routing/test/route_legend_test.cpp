// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// CostOfTags, the setting a row adjusts, the tier words and the rider's
/// sentence, pinned against City Rules and Kiawah Rules, and run over the
/// Atlanta Streets and Kiawah Trails legends' own tags.

#include "fv_route_legend.h"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using fv::routing::CostOfTags;
using fv::routing::CostTermKind;
using fv::routing::LegendCost;
using fv::routing::LegendSentence;
using fv::routing::LegendTermsLine;
using fv::routing::LegendTierWord;
using fv::routing::OsmTag;
using fv::routing::RouteProfile;
using fv::routing::RouteRules;

std::shared_ptr<const RouteRules> LoadRules(const char* path) {
  std::shared_ptr<const RouteRules> rules;
  const fv::Status s = RouteRules::Load(path, &rules);
  EXPECT_TRUE(s.ok()) << s.message;
  return rules;
}

struct Row {
  const char* name;
  std::vector<OsmTag> tags;
  double factor;  // ignored when the tier is "Never"
  const char* tier;
};

const std::vector<OsmTag> kCycleway = {{"highway", "cycleway"}};
const std::vector<OsmTag> kTertiaryTrack = {{"highway", "tertiary"}, {"cycleway:right", "track"}};
const std::vector<OsmTag> kResidential = {{"highway", "residential"}};
const std::vector<OsmTag> kTertiaryLane = {{"highway", "tertiary"}, {"cycleway:right", "lane"}};
const std::vector<OsmTag> kUnclassified = {{"highway", "unclassified"}};
const std::vector<OsmTag> kService = {{"highway", "service"}};
const std::vector<OsmTag> kSecondaryLane = {{"highway", "secondary"}, {"cycleway:right", "lane"}};
const std::vector<OsmTag> kSecondary = {{"highway", "secondary"}};
const std::vector<OsmTag> kPrimary = {{"highway", "primary"}};
const std::vector<OsmTag> kFootway = {{"highway", "footway"}};
const std::vector<OsmTag> kTrunk = {{"highway", "trunk"}};
const std::vector<OsmTag> kPrivate = {{"highway", "residential"}, {"access", "private"}};
const std::vector<OsmTag> kMotorway = {{"highway", "motorway"}};
const std::vector<OsmTag> kSteps = {{"highway", "steps"}};
const std::vector<OsmTag> kNoBicycles = {{"highway", "primary"}, {"bicycle", "no"}};

void CheckTable(const RouteProfile& p, const std::vector<Row>& rows) {
  for (const Row& r : rows) {
    SCOPED_TRACE(r.name);
    const LegendCost c = CostOfTags(p, r.tags);
    EXPECT_EQ(LegendTierWord(p, c), r.tier);
    if (std::string(r.tier) == "Never") {
      EXPECT_TRUE(c.excluded);
      continue;
    }
    EXPECT_FALSE(c.excluded);
    EXPECT_NEAR(c.factor, r.factor, 1e-9);
    // The named terms are an account of ArcCost and must multiply to it.
    double product = 1.0;
    for (const auto& t : c.terms) product *= t.factor;
    EXPECT_NEAR(product, c.factor, 1e-9) << LegendTermsLine(c);
  }
}

TEST(RouteLegend, CityRulesTable) {
  const auto rules = LoadRules(FV_CITY_ROUTE_RULES_FILE);
  const RouteProfile* bike = rules->Find("bicycle");
  ASSERT_NE(bike, nullptr);
  CheckTable(*bike, {
                        {"cycleway", kCycleway, 0.6, "Most preferred"},
                        {"tertiary + track", kTertiaryTrack, 0.675, "Most preferred"},
                        {"residential", kResidential, 0.8, "Preferred"},
                        {"tertiary + lane", kTertiaryLane, 0.9, "Preferred"},
                        {"unclassified", kUnclassified, 1.2, "Acceptable"},
                        {"service", kService, 1.5, "Acceptable"},
                        {"secondary + lane", kSecondaryLane, 1.5, "Acceptable"},
                        {"secondary", kSecondary, 2.5, "Avoided"},
                        {"primary", kPrimary, 4.0, "Avoided"},
                        {"footway", kFootway, 1.2 * 14.0 / 5.0, "Avoided"},
                        {"trunk", kTrunk, 8.0, "Last resort"},
                        {"residential + private", kPrivate, 20.0, "Last resort"},
                        {"motorway", kMotorway, 0, "Never"},
                        {"steps", kSteps, 0, "Never"},
                        {"bicycle=no", kNoBicycles, 0, "Never"},
                    });
}

// The same tags under the island's rules: the tags are shared, the rules
// decide the words.
TEST(RouteLegend, KiawahRulesTable) {
  const auto rules = LoadRules(FV_ROUTE_RULES_FILE);
  const RouteProfile* bike = rules->Find("bicycle");
  ASSERT_NE(bike, nullptr);
  CheckTable(*bike, {
                        {"cycleway", kCycleway, 0.7, "Most preferred"},
                        {"tertiary + track", kTertiaryTrack, 2.0, "Acceptable"},
                        {"residential", kResidential, 0.75, "Preferred"},
                        {"tertiary + lane", kTertiaryLane, 2.0, "Acceptable"},
                        {"unclassified", kUnclassified, 3.0, "Avoided"},
                        {"service", kService, 1.0, "Preferred"},
                        {"secondary + lane", kSecondaryLane, 3.0, "Avoided"},
                        {"secondary", kSecondary, 3.0, "Avoided"},
                        {"primary", kPrimary, 0, "Never"},
                        {"footway", kFootway, 1.5, "Acceptable"},
                        {"trunk", kTrunk, 0, "Never"},
                        {"residential + private", kPrivate, 0.675, "Most preferred"},
                        {"motorway", kMotorway, 0, "Never"},
                        {"steps", kSteps, 0, "Never"},
                        {"bicycle=no", kNoBicycles, 0, "Never"},
                    });
}

TEST(RouteLegend, Terms) {
  const auto rules = LoadRules(FV_CITY_ROUTE_RULES_FILE);
  const RouteProfile& bike = *rules->Find("bicycle");
  EXPECT_EQ(LegendTermsLine(CostOfTags(bike, kSecondaryLane)),
            "secondary 2.5 × painted lane 0.6 = 1.5");
  EXPECT_EQ(LegendTermsLine(CostOfTags(bike, kFootway)), "footway 1.2 × at 5 km/h 2.8 = 3.36");
  EXPECT_EQ(LegendTermsLine(CostOfTags(bike, kPrivate)),
            "residential 0.8 × private 25 = 20");
  EXPECT_EQ(LegendTermsLine(CostOfTags(bike, kMotorway)), "excluded");

  // A posted 45 mph secondary: speed band 1.5. On a track the road's speed
  // does not reach the rider.
  const LegendCost fast = CostOfTags(bike, {{"highway", "secondary"}, {"maxspeed", "45 mph"}});
  EXPECT_EQ(LegendTermsLine(fast), "secondary 2.5 × posted 72 km/h 1.5 = 3.75");
  const LegendCost track = CostOfTags(
      bike, {{"highway", "secondary"}, {"maxspeed", "45 mph"}, {"cycleway:right", "track"}});
  EXPECT_NEAR(track.factor, 2.5 * 0.45, 1e-9);

  const LegendCost route = CostOfTags(
      bike, {{"highway", "residential"}, {fv::routing::kRouteNetworkPseudoTag, "national"}});
  EXPECT_EQ(LegendTermsLine(route), "residential 0.8 × national route 0.9 = 0.72");
}

// Sentences are measured against factor 1.0, the shortest way; a modifier
// row against the same road without the modifier.
TEST(RouteLegend, Sentences) {
  const auto rules = LoadRules(FV_CITY_ROUTE_RULES_FILE);
  const RouteProfile& bike = *rules->Find("bicycle");
  auto say = [&](const std::vector<OsmTag>& tags) {
    return LegendSentence(bike, CostOfTags(bike, tags), "Pippin");
  };
  EXPECT_EQ(say(kCycleway),
            "Pippin will ride up to 1.67× as far as the shortest way to stay on this.");
  EXPECT_EQ(say(kResidential),
            "Pippin will ride up to 1.25× as far as the shortest way to stay on this.");
  EXPECT_EQ(say(kTertiaryLane),
            "Pippin will ride up to 1.67× as far to use a road with a painted lane instead of "
            "the same road without one.");
  EXPECT_EQ(say(kSecondary),
            "Pippin uses this only when the alternative is more than 2.5× as far. "
            "Crossing it without a signal adds 15 s.");
  EXPECT_EQ(say(kPrimary),
            "Pippin uses this only when the alternative is more than 4× as far. "
            "Crossing it without a signal adds 45 s.");
  EXPECT_EQ(say(kFootway), "Pippin uses this only when the alternative is more than 3.36× as far.");
  EXPECT_EQ(say({{"highway", "cycleway"}, {"surface", "gravel"}}),
            "Pippin counts a way with a rough surface as 1.6× as far as the same way paved.");
  EXPECT_EQ(say(kMotorway), "Pippin never routes a bike here.");

  // Kiawah Rules price a service road at exactly 1.0 and carry no bike lane factor.
  const auto kiawah = LoadRules(FV_ROUTE_RULES_FILE);
  const RouteProfile& island = *kiawah->Find("bicycle");
  EXPECT_EQ(LegendSentence(island, CostOfTags(island, kService), "Pippin"),
            "Pippin neither seeks this out nor avoids it.");
  EXPECT_EQ(LegendSentence(island, CostOfTags(island, kTertiaryLane), "Pippin"),
            "Pippin treats a road with a painted lane the same as the same road without one.");
}

// A profile whose speed varies by road measures in time.
TEST(RouteLegend, CarSaysAsLong) {
  const auto rules = LoadRules(FV_CITY_ROUTE_RULES_FILE);
  const RouteProfile& car = *rules->Find("car");
  const std::string s = LegendSentence(car, CostOfTags(car, kPrimary), "Peregrine");
  EXPECT_NE(s.find("× as long"), std::string::npos) << s;
  EXPECT_EQ(s.find("× as far"), std::string::npos) << s;
}

// The setting a row's slider moves: the one modifier or condition the way
// carries, else its class. Rule-file exclusions are locked.
TEST(RouteLegend, AdjustableSetting) {
  const auto rules = LoadRules(FV_CITY_ROUTE_RULES_FILE);
  const RouteProfile& bike = *rules->Find("bicycle");
  struct Want {
    std::vector<OsmTag> tags;
    const char* setting;
    CostTermKind kind;
    double factor;
    bool locked;
  };
  const double kInf = std::numeric_limits<double>::infinity();
  const Want kWant[] = {
      {kResidential, "classes.residential", CostTermKind::kClass, 0.8, false},
      {kTertiaryLane, "bike_lane_factor.lane", CostTermKind::kModifier, 0.6, false},
      {kTertiaryTrack, "bike_lane_factor.track", CostTermKind::kModifier, 0.45, false},
      {{{"highway", "residential"}, {fv::routing::kRouteNetworkPseudoTag, "local"}},
       "bike_route_factor.local", CostTermKind::kModifier, 0.9, false},
      {{{"highway", "cycleway"}, {"surface", "gravel"}}, "surface_factor.rough",
       CostTermKind::kModifier, 1.6, false},
      // Paved is the reference surface, not a row of its own.
      {{{"highway", "tertiary"}, {"surface", "asphalt"}}, "classes.tertiary",
       CostTermKind::kClass, 1.5, false},
      // Posted speed and pace belong to the road.
      {{{"highway", "secondary"}, {"maxspeed", "45 mph"}}, "classes.secondary",
       CostTermKind::kClass, 2.5, false},
      {kFootway, "classes.footway", CostTermKind::kClass, 1.2, false},
      {kPrivate, "private_penalty", CostTermKind::kCondition, 25.0, false},
      {kMotorway, "classes.motorway", CostTermKind::kClass, kInf, true},
      {kNoBicycles, "classes.primary", CostTermKind::kClass, 4.0, true},
      // Two modifiers: the label decides, which the tags cannot say.
      {{{"highway", "cycleway"}, {"surface", "gravel"}, {fv::routing::kRouteNetworkPseudoTag, "local"}},
       "", CostTermKind::kFixed, 1.0, false},
  };
  for (const Want& w : kWant) {
    const LegendCost c = CostOfTags(bike, w.tags);
    SCOPED_TRACE(w.setting);
    EXPECT_EQ(c.setting.setting, w.setting);
    EXPECT_EQ(c.setting.kind, w.kind);
    EXPECT_EQ(c.setting.factor, w.factor);
    EXPECT_EQ(c.locked, w.locked);
  }

  // A modifier at 1.0 is still the row's setting, so its slider stays on it.
  const auto kiawah = LoadRules(FV_ROUTE_RULES_FILE);
  const RouteProfile& island = *kiawah->Find("bicycle");
  const LegendCost lane = CostOfTags(island, kTertiaryLane);
  EXPECT_EQ(lane.setting.setting, "bike_lane_factor.lane");
  EXPECT_EQ(lane.setting.factor, 1.0);
  EXPECT_EQ(LegendTermsLine(lane), "tertiary 2 = 2");

}

TEST(RouteLegend, LocksComeFromTheRuleFile) {
  const auto rules = LoadRules(FV_CITY_ROUTE_RULES_FILE);
  const RouteProfile& bike = *rules->Find("bicycle");
  const LegendCost motorway = CostOfTags(bike, kMotorway);
  EXPECT_TRUE(motorway.locked);
  EXPECT_EQ(motorway.lock_reason, "Interstates are closed to bicycles.");
  const LegendCost no_bikes = CostOfTags(bike, kNoBicycles);
  EXPECT_TRUE(no_bikes.locked);
  EXPECT_EQ(no_bikes.lock_reason, "The map marks this way closed to bikes.");
  // Steps are excluded by class but not locked: the rider may allow them.
  const LegendCost steps = CostOfTags(bike, kSteps);
  EXPECT_TRUE(steps.excluded);
  EXPECT_FALSE(steps.locked);
  EXPECT_EQ(steps.setting.setting, "classes.steps");
  const LegendCost primary = CostOfTags(bike, kPrimary);
  EXPECT_FALSE(primary.locked);
  EXPECT_TRUE(primary.lock_reason.empty());

  const auto kiawah = LoadRules(FV_ROUTE_RULES_FILE);
  const LegendCost cartpath =
      CostOfTags(*kiawah->Find("bicycle"), {{"highway", "path"}, {"golf", "cartpath"}});
  EXPECT_TRUE(cartpath.locked);
  EXPECT_EQ(cartpath.lock_reason, "The routing rules exclude this way.");

  // Without key_locks an excluded motorway is just a class at Never.
  RouteProfile unlocked = bike;
  unlocked.key_locks.clear();
  const LegendCost open = CostOfTags(unlocked, kMotorway);
  EXPECT_TRUE(open.excluded);
  EXPECT_FALSE(open.locked);

  // A locked setting's stop is dropped; an excluded class's is applied.
  fv::routing::ProfileOverrides o;
  std::vector<std::string> ignored;
  ASSERT_TRUE(fv::routing::OverridesFromStops(bike, {{"classes.steps", 2}, {"classes.motorway", 2}},
                                              &o, &ignored)
                  .ok());
  EXPECT_EQ(o, (fv::routing::ProfileOverrides{{"classes.steps", 1.5}}));
  EXPECT_EQ(ignored, std::vector<std::string>{"classes.motorway"});
  ASSERT_TRUE(fv::routing::OverridesFromStops(bike, {{"classes.steps", 5}}, &o).ok());
  EXPECT_TRUE(o.empty());
}

TEST(RouteLegend, ParsesKeyLocks) {
  auto parse = [](const char* locks, std::shared_ptr<const RouteRules>* rules) {
    const std::string text = std::string(R"({"version": 1, "default_profile": "b", "profiles": {
      "b": {"mode": "bicycle", "speed": {"source": "fixed", "kph": 15},
            "classes": {"residential": 1.0}, "key_locks": )") +
                             locks + R"(},
      "c": {"extends": "b", "key_locks": {"classes.trunk": "No."}}}})";
    return RouteRules::Parse(text, "t", rules);
  };
  std::shared_ptr<const RouteRules> rules;
  ASSERT_TRUE(parse(R"({"classes.motorway": "Closed."})", &rules).ok());
  EXPECT_EQ(rules->Find("b")->key_locks,
            (std::map<std::string, std::string>{{"classes.motorway", "Closed."}}));
  EXPECT_EQ(rules->Find("c")->key_locks,
            (std::map<std::string, std::string>{{"classes.motorway", "Closed."},
                                                {"classes.trunk", "No."}}));
  EXPECT_FALSE(parse(R"({"classes.hovercraft": "Closed."})", &rules).ok());
  EXPECT_FALSE(parse(R"({"classes.motorway": ""})", &rules).ok());
  EXPECT_FALSE(parse(R"({"classes.motorway": true})", &rules).ok());
  EXPECT_FALSE(parse(R"(["classes.motorway"])", &rules).ok());
}

TEST(RouteLegend, SettingKindsMatchTerms) {
  const auto rules = LoadRules(FV_CITY_ROUTE_RULES_FILE);
  const RouteProfile& bike = *rules->Find("bicycle");
  const std::vector<OsmTag> kTags[] = {
      kTertiaryLane, kTertiaryTrack, kPrivate, kSecondary,
      {{"highway", "cycleway"}, {"surface", "gravel"}},
      {{"highway", "residential"}, {fv::routing::kRouteNetworkPseudoTag, "regional"}},
      {{"highway", "service"}, {"golf", "cartpath"}},
  };
  for (const auto& tags : kTags) {
    for (const auto& t : CostOfTags(bike, tags).terms) {
      SCOPED_TRACE(t.setting);
      EXPECT_EQ(fv::routing::SettingKind(t.setting), t.kind);
    }
  }
  EXPECT_EQ(fv::routing::SettingKind("surface_factor.paved"), CostTermKind::kFixed);
}

TEST(RouteLegend, StopOfValue) {
  using fv::routing::LegendStopOf;
  using fv::routing::LegendStops;
  constexpr double kNever = fv::routing::kClassExcluded;
  ASSERT_EQ(LegendStops(CostTermKind::kClass).size(), 6u);
  ASSERT_EQ(LegendStops(CostTermKind::kModifier).size(), 6u);
  EXPECT_TRUE(LegendStops(CostTermKind::kCondition).empty());
  EXPECT_STREQ(LegendStops(CostTermKind::kClass)[5].word, "Never");
  EXPECT_STREQ(LegendStops(CostTermKind::kModifier)[3].word, "No difference");
  // City Rules' shipped values fall on these stops.
  EXPECT_EQ(LegendStopOf(CostTermKind::kClass, 0.8), 1);   // residential: Preferred
  EXPECT_EQ(LegendStopOf(CostTermKind::kClass, 1.2), 2);   // footway: Acceptable
  EXPECT_EQ(LegendStopOf(CostTermKind::kClass, 4.0), 3);   // primary: Avoided
  EXPECT_EQ(LegendStopOf(CostTermKind::kClass, 25.0), 4);  // beyond the scale: Last resort
  EXPECT_EQ(LegendStopOf(CostTermKind::kClass, kNever), 5);
  EXPECT_EQ(LegendStopOf(CostTermKind::kModifier, 0.6), 1);
  EXPECT_EQ(LegendStopOf(CostTermKind::kModifier, 1.0), 3);
  // A non-positive penalty means no preference; a modifier cannot be Never.
  EXPECT_EQ(LegendStopOf(CostTermKind::kModifier, 0.0), 3);
  EXPECT_EQ(LegendStopOf(CostTermKind::kModifier, kNever), -1);
  EXPECT_EQ(LegendStopOf(CostTermKind::kCondition, 25.0), -1);
}

TEST(RouteLegend, OverridesFromStops) {
  using fv::routing::OverridesFromStops;
  using fv::routing::ProfileOverrides;
  const auto rules = LoadRules(FV_CITY_ROUTE_RULES_FILE);
  const RouteProfile& bike = *rules->Find("bicycle");

  // Every slider on its shipped stop: no overrides, so the exact shipped values stay.
  ProfileOverrides o;
  std::vector<std::string> ignored;
  ASSERT_TRUE(OverridesFromStops(bike,
                                 {{"classes.residential", 1},
                                  {"bike_lane_factor.lane", 1},
                                  {"classes.primary", 3}},
                                 &o, &ignored)
                  .ok());
  EXPECT_TRUE(o.empty());
  EXPECT_TRUE(ignored.empty());

  ASSERT_TRUE(OverridesFromStops(bike,
                                 {{"classes.residential", 3},
                                  {"classes.tertiary", 5},
                                  {"bike_lane_factor.lane", 3},
                                  {"classes.motorway", 0}},
                                 &o, &ignored)
                  .ok());
  EXPECT_EQ(o, (ProfileOverrides{{"classes.residential", 3.5},
                                 {"classes.tertiary", fv::routing::kClassExcluded},
                                 {"bike_lane_factor.lane", 1.0}}));
  // Motorway is barred for bikes, so its stop is dropped, not an error.
  EXPECT_EQ(ignored, std::vector<std::string>{"classes.motorway"});
  RouteProfile adjusted;
  ASSERT_TRUE(fv::routing::ApplyOverrides(bike, o, &adjusted).ok());
  EXPECT_NEAR(CostOfTags(adjusted, kResidential).factor, 3.5, 1e-9);
  EXPECT_TRUE(CostOfTags(adjusted, kTertiaryLane).excluded);

  const ProfileOverrides before = o;
  EXPECT_FALSE(OverridesFromStops(bike, {{"private_penalty", 0}}, &o).ok());
  EXPECT_FALSE(OverridesFromStops(bike, {{"classes.residential", 6}}, &o).ok());
  EXPECT_FALSE(OverridesFromStops(bike, {{"bike_lane_factor.lane", -1}}, &o).ok());
  EXPECT_FALSE(OverridesFromStops(bike, {{"surface_factor.paved", 2}}, &o).ok());
  EXPECT_FALSE(OverridesFromStops(bike, {{"classes.hovercraft", 2}}, &o).ok());
  EXPECT_EQ(o, before);
}

TEST(RouteLegend, ModifierOnRoads) {
  using fv::routing::ModifierOnRoads;
  const auto rules = LoadRules(FV_CITY_ROUTE_RULES_FILE);
  const RouteProfile& bike = *rules->Find("bicycle");

  // The plan's painted-lane example: 0.6 on residential, tertiary, secondary, primary.
  const auto lane = ModifierOnRoads(bike, kTertiaryLane);
  ASSERT_EQ(lane.size(), 4u);
  const char* const kLabel[] = {"Quiet street", "Moderate road", "Busy road", "Arterial"};
  const double kFactor[] = {0.48, 0.9, 1.5, 2.4};
  for (size_t i = 0; i < lane.size(); ++i) {
    SCOPED_TRACE(kLabel[i]);
    EXPECT_EQ(lane[i].label, kLabel[i]);
    EXPECT_EQ(lane[i].cost.setting.setting, "bike_lane_factor.lane");
    EXPECT_NEAR(lane[i].cost.factor, kFactor[i], 1e-9);
  }
  EXPECT_EQ(lane[0].highway, "residential");
  EXPECT_EQ(LegendTierWord(bike, lane[2].cost), "Acceptable");
  EXPECT_EQ(LegendTierWord(bike, lane[3].cost), "Avoided");

  // A road-type slider moves the line.
  RouteProfile adjusted;
  ASSERT_TRUE(
      fv::routing::ApplyOverrides(bike, {{"classes.residential", 3.5}}, &adjusted).ok());
  EXPECT_NEAR(ModifierOnRoads(adjusted, kTertiaryLane)[0].cost.factor, 2.1, 1e-9);

  EXPECT_EQ(ModifierOnRoads(bike, {{"highway", "cycleway"}, {"surface", "gravel"}}).size(), 4u);
  EXPECT_TRUE(ModifierOnRoads(bike, kResidential).empty());
  EXPECT_TRUE(ModifierOnRoads(bike, kPrivate).empty());
  const std::vector<OsmTag> cartpath = {{"highway", "service"}, {"golf", "cartpath"}};
  ASSERT_EQ(CostOfTags(bike, cartpath).setting.setting, "golf_cartpath_penalty");
  EXPECT_TRUE(ModifierOnRoads(bike, cartpath).empty());
}

TEST(RouteLegend, ParsesTiers) {
  const char* kText = R"({
    "version": 1,
    "profiles": { "bicycle": {
      "mode": "bicycle", "speed": {"source": "fixed", "kph": 15},
      "unlisted_classes": 1.0,
      "legend_tiers": [[0.5, "Great"], [null, "Fine"]] } } })";
  std::shared_ptr<const RouteRules> rules;
  ASSERT_TRUE(RouteRules::Parse(kText, "t", &rules).ok());
  const RouteProfile& p = *rules->Find("bicycle");
  ASSERT_EQ(p.legend_tiers.size(), 2u);
  EXPECT_EQ(p.legend_tiers[0].second, "Great");
  EXPECT_TRUE(std::isinf(p.legend_tiers[1].first));
  EXPECT_EQ(LegendTierWord(p, CostOfTags(p, kResidential)), "Fine");
}

TEST(RouteLegend, RejectsBadTiers) {
  auto parse = [](const char* tiers) {
    const std::string text =
        std::string(R"({"version": 1, "profiles": {"b": {"mode": "bicycle", "legend_tiers": )") +
        tiers + "}}}";
    std::shared_ptr<const RouteRules> rules;
    return RouteRules::Parse(text, "t", &rules).ok();
  };
  EXPECT_TRUE(parse(R"([[1.0, "A"], [null, "B"]])"));
  const char* kBad[] = {
      R"([[1.0, "A"], [0.5, "B"], [null, "C"]])",  // not ascending
      R"([[1.0, "A"], [2.0, "B"]])",                 // last bound not null
      R"([[null, "A"], [null, "B"]])",               // null before the end
      R"([[1.0, ""], [null, "B"]])",                 // empty word
      R"([[-1.0, "A"], [null, "B"]])",               // not positive
      R"([])",
  };
  for (const char* tiers : kBad) {
    SCOPED_TRACE(tiers);
    EXPECT_FALSE(parse(tiers));
  }
  // Older packs still carry the baseline key; it is ignored.
  std::shared_ptr<const RouteRules> rules;
  EXPECT_TRUE(RouteRules::Parse(
                   R"({"version": 1, "profiles": {"b": {"legend_baseline": {"highway": "residential"}}}})",
                   "t", &rules)
                   .ok());
}

/// Costs every legend row of the style at `style_path` under the bicycle
/// profile of `rules_path` and checks each row's tier word against `expected`.
void CheckStyleEntries(const char* style_path, const char* rules_path,
                       const std::map<std::string, std::string>& expected) {
  std::ifstream in(style_path);
  ASSERT_TRUE(in) << style_path;
  std::stringstream text;
  text << in.rdbuf();
  const nlohmann::json style = nlohmann::json::parse(text.str());
  const auto rules = LoadRules(rules_path);
  const RouteProfile& bike = *rules->Find("bicycle");

  size_t seen = 0;
  for (const auto& group : style["metadata"]["peregrine:legend"]["groups"]) {
    for (const auto& entry : group["entries"]) {
      const std::string id = entry["id"].get<std::string>();
      SCOPED_TRACE(id);
      const auto want = expected.find(id);
      ASSERT_NE(want, expected.end()) << "no expected tier for a new legend row";
      // A row without tags explains the map only; the key gives it no tier.
      if (!entry.contains("osm")) {
        EXPECT_EQ(want->second, "");
        ++seen;
        continue;
      }
      std::vector<OsmTag> tags;
      for (auto it = entry["osm"].begin(); it != entry["osm"].end(); ++it)
        tags.push_back(OsmTag{it.key(), it.value().get<std::string>()});
      const LegendCost c = CostOfTags(bike, tags);
      EXPECT_EQ(LegendTierWord(bike, c), want->second) << LegendTermsLine(c);
      EXPECT_FALSE(LegendSentence(bike, c, "Pippin").empty());
      ++seen;
    }
  }
  EXPECT_EQ(seen, expected.size());
}

// Every Atlanta Streets legend row's raw OSM tags, costed under City Rules.
TEST(RouteLegend, AtlantaStreetsEntries) {
  CheckStyleEntries(FV_ATLANTA_STYLE_FILE, FV_CITY_ROUTE_RULES_FILE,
                    {
                        {"quiet", "Preferred"},          {"service", "Acceptable"},
                        {"moderate", "Acceptable"},      {"busy", "Avoided"},
                        {"arterial", "Avoided"},         {"designated", ""},
                        {"no-bicycles", "Never"},        {"interstate", "Never"},
                        {"bridge", "Preferred"},         {"bike-lane", "Preferred"},
                        {"shared-lane", "Most preferred"}, {"bike-track", "Most preferred"},
                        {"route-national", "Most preferred"},
                        {"route-regional", "Most preferred"},
                        {"route-local", "Most preferred"}, {"trail", "Most preferred"},
                        {"trail-unpaved", "Preferred"},  {"walk", "Most preferred"},
                        {"sidewalk", "Avoided"},         {"track", "Acceptable"},
                        {"steps", "Never"},
                    });
}

// Every Kiawah Trails legend row's raw OSM tags, costed under the island's rules.
TEST(RouteLegend, KiawahTrailsEntries) {
  CheckStyleEntries(FV_KIAWAH_STYLE_FILE, FV_ROUTE_RULES_FILE,
                    {
                        {"bike", "Most preferred"},  {"bike-bridge", "Most preferred"},
                        {"boardwalk", "Acceptable"}, {"walk", "Acceptable"},
                        {"golf-path", "Never"},      {"track", "Preferred"},
                        {"main-road", "Acceptable"}, {"street", "Preferred"},
                        {"service", "Preferred"},
                    });
}

}  // namespace
