// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// CostOfTags, the tier words and the rider's sentence, pinned against City
/// Rules and Kiawah Rules, and run over the Atlanta Streets legend's own tags.

#include "fv_route_legend.h"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using fv::routing::CostOfTags;
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
                        {"residential", kResidential, 0.75, "Most preferred"},
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

TEST(RouteLegend, Sentences) {
  const auto rules = LoadRules(FV_CITY_ROUTE_RULES_FILE);
  const RouteProfile& bike = *rules->Find("bicycle");
  auto say = [&](const std::vector<OsmTag>& tags) {
    return LegendSentence(bike, CostOfTags(bike, tags), "Pippin");
  };
  EXPECT_EQ(say(kCycleway), "Pippin will ride up to 33% further to use this.");
  EXPECT_EQ(say(kResidential), "Pippin weighs this the same as a residential street.");
  EXPECT_EQ(say(kTertiaryLane), "Pippin will ride up to 13% further to avoid this.");
  EXPECT_EQ(say(kSecondary),
            "Pippin will ride up to 3.1 times as far to avoid this. "
            "Crossing it without a signal adds 15 s.");
  EXPECT_EQ(say(kPrimary),
            "Pippin will ride up to 5 times as far to avoid this. "
            "Crossing it without a signal adds 45 s.");
  EXPECT_EQ(say(kMotorway), "Pippin never routes a bike here.");
}

TEST(RouteLegend, ParsesTiersAndBaseline) {
  const char* kText = R"({
    "version": 1,
    "profiles": { "bicycle": {
      "mode": "bicycle", "speed": {"source": "fixed", "kph": 15},
      "unlisted_classes": 1.0,
      "legend_tiers": [[0.5, "Great"], [null, "Fine"]],
      "legend_baseline": {"highway": "cycleway"} } } })";
  std::shared_ptr<const RouteRules> rules;
  ASSERT_TRUE(RouteRules::Parse(kText, "t", &rules).ok());
  const RouteProfile& p = *rules->Find("bicycle");
  ASSERT_EQ(p.legend_tiers.size(), 2u);
  EXPECT_EQ(p.legend_tiers[0].second, "Great");
  EXPECT_TRUE(std::isinf(p.legend_tiers[1].first));
  ASSERT_EQ(p.legend_baseline.size(), 1u);
  EXPECT_EQ(p.legend_baseline[0].value, "cycleway");
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
  std::shared_ptr<const RouteRules> rules;
  EXPECT_FALSE(RouteRules::Parse(
                   R"({"version": 1, "profiles": {"b": {"legend_baseline": {}}}})", "t", &rules)
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
      std::vector<OsmTag> tags;
      for (auto it = entry["osm"].begin(); it != entry["osm"].end(); ++it)
        tags.push_back(OsmTag{it.key(), it.value().get<std::string>()});
      const LegendCost c = CostOfTags(bike, tags);
      const auto want = expected.find(id);
      ASSERT_NE(want, expected.end()) << "no expected tier for a new legend row";
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
                        {"arterial", "Avoided"},         {"designated", "Preferred"},
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
                        {"main-road", "Acceptable"}, {"street", "Most preferred"},
                        {"service", "Preferred"},
                    });
}

}  // namespace
