// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Route rule tests (O5c).
//
// Three things are worth pinning here and nothing else is:
//
//   * the builtin rules reproduce the O5b hard-coded profiles EXACTLY — same
//     route, same reported seconds, on the fixtures router_test.cpp uses for
//     the driving, walking and bicycle profiles. That is what makes the rule
//     file a tuning surface rather than a behaviour change.
//   * a rule file that changes on disk changes the next route, and one that is
//     broken changes nothing at all.
//   * every diagnosable authoring mistake is diagnosed — a typo'd key, a
//     misspelled class, a zero weight — because a rule file that fails quietly
//     is worse than no rule file.
//
// The shipped rules/route-weights.json is checked only for "parses, and has
// the three core profiles". Its numbers are meant to be tuned; a test that
// pinned them would make tuning a test failure.

#include "fv_route_rules.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "fv_road_graph.h"
#include "fv_router.h"

namespace fs = std::filesystem;

namespace {

using fv::routing::RoadClass;
using fv::routing::RoadGraph;
using fv::routing::Route;
using fv::routing::RouteOptions;
using fv::routing::Router;
using fv::routing::RouteRules;
using fv::routing::RouteRulesFile;
using fv::routing::SelectProfile;
using fv::routing::SpeedSource;
using fv::routing::TravelMode;

// Same shape as router_test.cpp's cycle fixture: three ways from n1 to n3 —
// the shortest is a path that bars bicycles, then a cycleway, then a
// residential dogleg that is the only one a car may use.
const char* kCycleOsm = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.7000000" lon="-80.0000000"/>
 <node id="2" lat="32.7100000" lon="-80.0100000"/>
 <node id="3" lat="32.7000000" lon="-80.0200000"/>
 <node id="4" lat="32.7020000" lon="-80.0100000"/>
 <node id="5" lat="32.7000000" lon="-80.0100000"/>
 <way id="201">
  <nd ref="1"/><nd ref="2"/><nd ref="3"/>
  <tag k="highway" v="residential"/><tag k="name" v="The Long Way"/>
 </way>
 <way id="202">
  <nd ref="1"/><nd ref="4"/><nd ref="3"/>
  <tag k="highway" v="cycleway"/><tag k="name" v="The Bike Path"/>
 </way>
 <way id="203">
  <nd ref="1"/><nd ref="5"/><nd ref="3"/>
  <tag k="highway" v="path"/><tag k="bicycle" v="no"/>
  <tag k="name" v="No Bikes"/>
 </way>
</osm>
)";

fs::path ScratchDir() {
  fs::path dir = fs::temp_directory_path() / "fv_route_rules_test";
  fs::create_directories(dir);
  return dir;
}

std::string WriteFile(const std::string& name, const std::string& text) {
  const fs::path path = ScratchDir() / name;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
  out.close();
  return path.string();
}

RoadGraph BuildCycleGraph() {
  RoadGraph g;
  fv::routing::RoadGraphBuildOptions options;
  options.include_non_driveable = true;
  const fv::Status s =
      fv::routing::BuildRoadGraph({WriteFile("rules-cycle.osm", kCycleOsm)}, options, &g, nullptr);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  return g;
}

uint32_t NodeByOsmId(const RoadGraph& g, int64_t osm_id) {
  for (uint32_t i = 0; i < g.node_count(); ++i) {
    if (g.node(i).osm_id == osm_id) return i;
  }
  return 0xFFFFFFFFu;
}

std::shared_ptr<const RouteRules> ParseOk(const std::string& text) {
  std::shared_ptr<const RouteRules> rules;
  const fv::Status s = RouteRules::Parse(text, "<test>", &rules);
  EXPECT_EQ(s.code, fv::kOk) << s.message;
  return rules;
}

std::string ParseError(const std::string& text) {
  std::shared_ptr<const RouteRules> rules;
  const fv::Status s = RouteRules::Parse(text, "<test>", &rules);
  EXPECT_NE(s.code, fv::kOk) << "expected this to be rejected";
  EXPECT_EQ(rules, nullptr) << "a rejected file must not replace the caller's rules";
  return s.message;
}

// A rule file whose profiles are named the same as the builtin ones but whose
// weights are deliberately not, so a route can tell which set is in force.
std::string RulesWithCyclewayWeight(double cycleway) {
  return R"({"version":1,"default_profile":"bicycle","profiles":{"bicycle":{
      "mode":"bicycle","speed":{"source":"fixed","kph":15.0},"metric":"time",
      "turn_restrictions":false,"private_penalty":5.0,
      "unlisted_classes":"exclude",
      "classes":{"cycleway":)" +
         std::to_string(cycleway) + R"(,"residential":0.85,"path":0.65}}}})";
}

// ---------------------------------------------------------------------------
// The builtin rules ARE the O5b profiles
// ---------------------------------------------------------------------------

TEST(RouteRules, BuiltinHasTheThreeProfiles) {
  const auto rules = RouteRules::Builtin();
  ASSERT_NE(rules, nullptr);
  EXPECT_EQ(rules->default_profile_name(), "car");
  ASSERT_NE(rules->Find("car"), nullptr);
  ASSERT_NE(rules->Find("bicycle"), nullptr);
  ASSERT_NE(rules->Find("foot"), nullptr);

  const fv::routing::RouteProfile& car = *rules->Find("car");
  EXPECT_EQ(car.mode, TravelMode::kMotorVehicle);
  EXPECT_EQ(car.speed_source, SpeedSource::kPosted);
  EXPECT_TRUE(car.turn_restrictions);
  EXPECT_TRUE(car.allows(RoadClass::kMotorway));
  EXPECT_FALSE(car.allows(RoadClass::kFootway));
  EXPECT_FALSE(car.allows(RoadClass::kCycleway));

  // The O5b bicycle multipliers, in the one place they are now written down.
  const fv::routing::RouteProfile& bike = *rules->Find("bicycle");
  EXPECT_EQ(bike.mode, TravelMode::kBicycle);
  EXPECT_EQ(bike.speed_source, SpeedSource::kFixed);
  EXPECT_DOUBLE_EQ(bike.fixed_kph, 15.0);
  EXPECT_DOUBLE_EQ(bike.weight(RoadClass::kCycleway), 0.50);
  EXPECT_DOUBLE_EQ(bike.weight(RoadClass::kPath), 0.65);
  EXPECT_DOUBLE_EQ(bike.weight(RoadClass::kResidential), 0.85);
  EXPECT_DOUBLE_EQ(bike.weight(RoadClass::kLivingStreet), 0.85);
  EXPECT_DOUBLE_EQ(bike.weight(RoadClass::kFootway), 1.5);
  EXPECT_DOUBLE_EQ(bike.weight(RoadClass::kPedestrian), 1.5);
  EXPECT_FALSE(bike.allows(RoadClass::kMotorway));
  EXPECT_FALSE(bike.allows(RoadClass::kSteps));

  const fv::routing::RouteProfile& foot = *rules->Find("foot");
  EXPECT_EQ(foot.mode, TravelMode::kFoot);
  EXPECT_DOUBLE_EQ(foot.fixed_kph, 5.0);
  EXPECT_TRUE(foot.allows(RoadClass::kSteps));   // unlisted_classes: 1.0
  EXPECT_TRUE(foot.allows(RoadClass::kMotorway));
}

// The load-bearing one: a route run under a builtin profile and the same route
// run the pre-O5c way must be the same route, with the same reported clock.
TEST(RouteRules, BuiltinProfilesMatchTheHardCodedOnes) {
  const RoadGraph g = BuildCycleGraph();
  const Router router(g);
  const uint32_t n1 = NodeByOsmId(g, 1);
  const uint32_t n3 = NodeByOsmId(g, 3);
  ASSERT_NE(n1, 0xFFFFFFFFu);
  ASSERT_NE(n3, 0xFFFFFFFFu);

  const auto rules = RouteRules::Builtin();
  struct Case {
    const char* profile;
    bool driving;
    bool cycle_only;
  };
  for (const Case& c : {Case{"car", true, false}, Case{"bicycle", false, true},
                        Case{"foot", false, false}}) {
    RouteOptions legacy;
    legacy.driving = c.driving;
    legacy.cycle_only = c.cycle_only;

    RouteOptions ruled;
    ASSERT_EQ(SelectProfile(rules, c.profile, &ruled).code, fv::kOk);

    Route a, b;
    ASSERT_EQ(router.RouteNodes(n1, n3, legacy, &a).code, fv::kOk);
    ASSERT_EQ(router.RouteNodes(n1, n3, ruled, &b).code, fv::kOk);
    ASSERT_TRUE(a.found) << c.profile;
    ASSERT_TRUE(b.found) << c.profile;
    EXPECT_EQ(a.nodes, b.nodes) << c.profile << ": rules picked a different route";
    EXPECT_NEAR(a.seconds, b.seconds, 1e-9) << c.profile;
    EXPECT_NEAR(a.length_m, b.length_m, 1e-6) << c.profile;
    ASSERT_FALSE(b.legs.empty());
  }
}

// ---------------------------------------------------------------------------
// Weights actually steer
// ---------------------------------------------------------------------------

TEST(RouteRules, ClassWeightChangesTheRouteChosen) {
  const RoadGraph g = BuildCycleGraph();
  const Router router(g);
  const uint32_t n1 = NodeByOsmId(g, 1);
  const uint32_t n3 = NodeByOsmId(g, 3);

  auto ride = [&](double cycleway_weight) {
    RouteOptions o;
    EXPECT_EQ(SelectProfile(ParseOk(RulesWithCyclewayWeight(cycleway_weight)), "bicycle", &o).code,
              fv::kOk);
    Route r;
    EXPECT_EQ(router.RouteNodes(n1, n3, o, &r).code, fv::kOk);
    EXPECT_TRUE(r.found);
    return r.legs.empty() ? std::string() : r.legs.front().name;
  };

  // Cheap: the cycleway, which is also the shorter of the two rideable ways.
  EXPECT_EQ(ride(0.50), "The Bike Path");
  // Priced up far enough, the longer residential dogleg wins instead — and
  // that is a weight doing it, not a filter: the cycleway is still usable.
  EXPECT_EQ(ride(20.0), "The Long Way");
}

TEST(RouteRules, ReportedTimeIsUnweighted) {
  const RoadGraph g = BuildCycleGraph();
  const Router router(g);
  const uint32_t n1 = NodeByOsmId(g, 1);
  const uint32_t n3 = NodeByOsmId(g, 3);

  RouteOptions cheap, dear;
  ASSERT_EQ(SelectProfile(ParseOk(RulesWithCyclewayWeight(0.50)), "bicycle", &cheap).code, fv::kOk);
  ASSERT_EQ(SelectProfile(ParseOk(RulesWithCyclewayWeight(0.10)), "bicycle", &dear).code, fv::kOk);

  Route a, b;
  ASSERT_EQ(router.RouteNodes(n1, n3, cheap, &a).code, fv::kOk);
  ASSERT_EQ(router.RouteNodes(n1, n3, dear, &b).code, fv::kOk);
  ASSERT_EQ(a.nodes, b.nodes);  // same route both times
  // A five-fold weight change on the class it rides must not change how long
  // the ride is said to take: weights are a preference, not a clock.
  EXPECT_NEAR(a.seconds, b.seconds, 1e-9);
}

TEST(RouteRules, ExtendsOverlaysAndClassesMerge) {
  const auto rules = ParseOk(R"({"version":1,"profiles":{
      "base":{"mode":"motor_vehicle","speed":{"source":"posted"},"metric":"time",
              "unlisted_classes":"exclude",
              "classes":{"residential":1.0,"service":1.0,"track":1.0}},
      "derived":{"extends":"base","metric":"distance","classes":{"track":9.0}}}})");
  const fv::routing::RouteProfile& d = *rules->Find("derived");
  EXPECT_EQ(d.metric, fv::routing::RouteMetric::kDistance);  // replaced
  EXPECT_DOUBLE_EQ(d.weight(RoadClass::kTrack), 9.0);        // overlaid
  EXPECT_DOUBLE_EQ(d.weight(RoadClass::kResidential), 1.0);  // inherited
  EXPECT_TRUE(d.allows(RoadClass::kService));                // inherited
  EXPECT_FALSE(d.allows(RoadClass::kMotorway));
  // Order in the file is not the resolution order — a base declared after the
  // profile that extends it still resolves.
  const auto reversed = ParseOk(R"({"version":1,"default_profile":"b","profiles":{
      "b":{"extends":"a","metric":"distance"},
      "a":{"mode":"foot","speed":{"source":"fixed","kph":4.0},"unlisted_classes":1.0}}})");
  EXPECT_DOUBLE_EQ(reversed->Find("b")->fixed_kph, 4.0);
}

// ---------------------------------------------------------------------------
// Reload
// ---------------------------------------------------------------------------

TEST(RouteRulesFileTest, PicksUpAnEditWithoutRestarting) {
  const std::string path = WriteFile("reload.json", RulesWithCyclewayWeight(0.50));
  RouteRulesFile file(path);
  file.set_poll_interval_ms(0);  // stat every call; the test is not waiting 500 ms
  ASSERT_EQ(file.last_error(), "");
  EXPECT_EQ(file.generation(), 1u);
  EXPECT_DOUBLE_EQ(file.rules()->Find("bicycle")->weight(RoadClass::kCycleway), 0.50);

  const RoadGraph g = BuildCycleGraph();
  const Router router(g);
  const uint32_t n1 = NodeByOsmId(g, 1);
  const uint32_t n3 = NodeByOsmId(g, 3);

  auto ride_now = [&]() {
    RouteOptions o;
    EXPECT_EQ(SelectProfile(file.rules(), "bicycle", &o).code, fv::kOk);
    Route r;
    EXPECT_EQ(router.RouteNodes(n1, n3, o, &r).code, fv::kOk);
    return r.legs.empty() ? std::string() : r.legs.front().name;
  };
  EXPECT_EQ(ride_now(), "The Bike Path");

  // Some filesystems keep whole-second write times, and the poll compares
  // (mtime, size) — so an edit made within the same second of the same length
  // could go unseen. The rewritten file is a different length, and the sleep
  // keeps this honest on the ones with coarse timestamps.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  WriteFile("reload.json", RulesWithCyclewayWeight(20.0));

  EXPECT_EQ(ride_now(), "The Long Way") << "the edit did not take effect";
  EXPECT_EQ(file.generation(), 2u);
  EXPECT_EQ(file.last_error(), "");
}

TEST(RouteRulesFileTest, ABrokenEditChangesNothing) {
  const std::string path = WriteFile("broken.json", RulesWithCyclewayWeight(0.50));
  RouteRulesFile file(path);
  file.set_poll_interval_ms(0);
  const auto good = file.rules();
  ASSERT_EQ(file.generation(), 1u);

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  WriteFile("broken.json", R"({"version":1,"profiles":{"bicycle":{"mode":"bicy)");  // truncated

  const auto after = file.rules();
  EXPECT_EQ(after, good) << "a broken file must leave the loaded rules in force";
  EXPECT_EQ(file.generation(), 1u);
  EXPECT_FALSE(file.last_error().empty());

  // And a fixed file recovers, without anything having been restarted.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  WriteFile("broken.json", RulesWithCyclewayWeight(3.0));
  EXPECT_DOUBLE_EQ(file.rules()->Find("bicycle")->weight(RoadClass::kCycleway), 3.0);
  EXPECT_EQ(file.generation(), 2u);
  EXPECT_EQ(file.last_error(), "");
}

TEST(RouteRulesFileTest, MissingFileFallsBackToBuiltin) {
  RouteRulesFile file((ScratchDir() / "does-not-exist.json").string());
  file.set_poll_interval_ms(0);
  EXPECT_EQ(file.rules(), RouteRules::Builtin());
  EXPECT_EQ(file.generation(), 0u);
  EXPECT_FALSE(file.last_error().empty());

  // It appearing later is picked up by an ordinary poll — no restart, and no
  // special case for "the file was not there when we started".
  WriteFile("appears-later.json", RulesWithCyclewayWeight(0.75));
  RouteRulesFile late((ScratchDir() / "appears-later.json").string());
  late.set_poll_interval_ms(0);
  EXPECT_DOUBLE_EQ(late.rules()->Find("bicycle")->weight(RoadClass::kCycleway), 0.75);
}

TEST(RouteRulesFileTest, RulesInFlightSurviveAReload) {
  const std::string path = WriteFile("inflight.json", RulesWithCyclewayWeight(0.50));
  RouteRulesFile file(path);
  file.set_poll_interval_ms(0);

  // A query that took its options before the edit keeps the weights it took:
  // the profile pointer shares ownership of the whole rule set.
  RouteOptions taken;
  ASSERT_EQ(SelectProfile(file.rules(), "bicycle", &taken).code, fv::kOk);

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  WriteFile("inflight.json", RulesWithCyclewayWeight(20.0));
  ASSERT_DOUBLE_EQ(file.rules()->Find("bicycle")->weight(RoadClass::kCycleway), 20.0);

  ASSERT_NE(taken.profile, nullptr);
  EXPECT_DOUBLE_EQ(taken.profile->weight(RoadClass::kCycleway), 0.50);
}

// ---------------------------------------------------------------------------
// Every authoring mistake is diagnosed
// ---------------------------------------------------------------------------

TEST(RouteRules, RejectsWhatItCannotUnderstand) {
  const std::string kOk = R"({"version":1,"profiles":{"p":{"mode":"foot",
      "speed":{"source":"fixed","kph":5.0},"unlisted_classes":1.0}}})";
  ASSERT_NE(ParseOk(kOk), nullptr);

  EXPECT_NE(ParseError("{not json").find("not valid JSON"), std::string::npos);
  EXPECT_NE(ParseError(R"({"profiles":{}})").find("version"), std::string::npos);
  EXPECT_NE(ParseError(R"({"version":2,"profiles":{}})").find("version 2"), std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1})").find("'profiles'"), std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{}})").find("empty"), std::string::npos);

  // A misspelled key is an error, not something ignored — the whole point of
  // the file is that an edit either takes effect or says why it did not.
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"mode":"foot","weigths":{}}}})")
                .find("unknown key 'weigths'"),
            std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"profils":{}})").find("unknown key 'profils'"),
            std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"mode":"hovercraft"}}})")
                .find("unknown mode"),
            std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"mode":"foot","metric":"vibes"}}})")
                .find("unknown metric"),
            std::string::npos);
  // A class name that is not a road class: almost always a typo, and silently
  // weighting nothing is exactly the failure this file must not have.
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"mode":"foot",
                          "classes":{"residentail":1.0}}}})")
                .find("not a routable road class"),
            std::string::npos);
  // Zero and negative weights: a free arc is a road the search takes
  // everywhere, and a negative one has no meaning Dijkstra can honour at all.
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"mode":"foot",
                          "classes":{"residential":0.0}}}})")
                .find("greater than zero"),
            std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"mode":"foot",
                          "classes":{"residential":-2.0}}}})")
                .find("greater than zero"),
            std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"mode":"foot",
                          "speed":{"source":"fixed"}}}})")
                .find("kph"),
            std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"extends":"nope","mode":"foot"}}})")
                .find("extends"),
            std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{
                          "a":{"extends":"b"},"b":{"extends":"a"}}})")
                .find("cycle"),
            std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"default_profile":"nope","profiles":{"p":{
                          "mode":"foot"}}})")
                .find("default_profile"),
            std::string::npos);
}

TEST(RouteRules, CommentsAreAllowed) {
  const auto rules = ParseOk(R"({
    // the file is meant to be read by whoever tunes it
    "version": 1,
    "profiles": { "p": { /* a weight without its reason is a number nobody
                            dares change */
      "mode": "foot", "speed": {"source":"fixed","kph":5.0},
      "unlisted_classes": 1.0 } }
  })");
  EXPECT_NE(rules->Find("p"), nullptr);
}

TEST(RouteRules, SelectProfileNamesWhatItCouldNotFind) {
  RouteOptions o;
  const fv::Status s = SelectProfile(RouteRules::Builtin(), "unicycle", &o);
  EXPECT_EQ(s.code, fv::kNotFound);
  EXPECT_NE(s.message.find("unicycle"), std::string::npos);
  EXPECT_EQ(o.profile, nullptr);  // and the options are left alone

  // An empty name is the file's own default, not an error.
  ASSERT_EQ(SelectProfile(RouteRules::Builtin(), "", &o).code, fv::kOk);
  EXPECT_EQ(o.profile->name, "car");
  EXPECT_TRUE(o.driving);        // mirrored onto the pre-O5c fields
  EXPECT_FALSE(o.cycle_only);
}

// ---------------------------------------------------------------------------
// The shipped file
// ---------------------------------------------------------------------------

TEST(RouteRules, ShippedRuleFileParses) {
  const std::string path = FV_ROUTE_RULES_FILE;
  ASSERT_FALSE(path.empty());
  std::shared_ptr<const RouteRules> rules;
  const fv::Status s = RouteRules::Load(path, &rules);
  ASSERT_EQ(s.code, fv::kOk) << s.message;
  // Only what every caller relies on being there. The weights themselves are
  // meant to be tuned, so they are pinned on Builtin() and not here.
  EXPECT_NE(rules->Find("car"), nullptr);
  EXPECT_NE(rules->Find("bicycle"), nullptr);
  EXPECT_NE(rules->Find("foot"), nullptr);
  EXPECT_NE(rules->default_profile(), nullptr);
}

// ---------------------------------------------------------------------------
// Toll and ferry (O5e)
// ---------------------------------------------------------------------------

TEST(RouteRules, TollAndFerryPenaltiesParseAsWeightsOrExclusions) {
  const auto rules = ParseOk(R"({"version":1,"profiles":{
      "priced":  {"mode":"motor_vehicle","toll_penalty":4.5,"ferry_penalty":2.0},
      "refused": {"mode":"motor_vehicle","toll_penalty":"exclude","ferry_penalty":false},
      "silent":  {"mode":"motor_vehicle"}}})");
  ASSERT_NE(rules, nullptr);
  EXPECT_DOUBLE_EQ(rules->Find("priced")->toll_penalty, 4.5);
  EXPECT_DOUBLE_EQ(rules->Find("priced")->ferry_penalty, 2.0);
  // The two spellings of a refusal the rest of the file already uses.
  EXPECT_DOUBLE_EQ(rules->Find("refused")->toll_penalty, fv::routing::kAvoidExcluded);
  EXPECT_DOUBLE_EQ(rules->Find("refused")->ferry_penalty, fv::routing::kAvoidExcluded);
  // Saying nothing means no preference either way, which is what keeps every
  // rule file written before O5e meaning what it meant.
  EXPECT_DOUBLE_EQ(rules->Find("silent")->toll_penalty, 1.0);
  EXPECT_DOUBLE_EQ(rules->Find("silent")->ferry_penalty, 1.0);

  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"toll_penalty":0.0}}})")
                .find("greater than zero"),
            std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"ferry_penalty":true}}})")
                .find("not a weight"),
            std::string::npos);
  EXPECT_NE(ParseError(R"({"version":1,"profiles":{"p":{"toll_penalty":"never"}}})")
                .find("exclude"),
            std::string::npos);
}

TEST(RouteRules, FerryIsAClassAProfileCanWeightByName) {
  // It is not a `highway=*` value, so it resolves only through the by-name
  // lookup — and a profile that excludes what it does not list has to name it.
  const auto rules = ParseOk(R"({"version":1,"profiles":{"p":{
      "mode":"motor_vehicle","unlisted_classes":"exclude",
      "classes":{"primary":1.0,"ferry":2.5}}}})");
  ASSERT_NE(rules, nullptr);
  EXPECT_TRUE(rules->Find("p")->allows(RoadClass::kFerry));
  EXPECT_DOUBLE_EQ(rules->Find("p")->weight(RoadClass::kFerry), 2.5);

  const auto without = ParseOk(R"({"version":1,"profiles":{"p":{
      "mode":"motor_vehicle","unlisted_classes":"exclude",
      "classes":{"primary":1.0}}}})");
  ASSERT_NE(without, nullptr);
  EXPECT_FALSE(without->Find("p")->allows(RoadClass::kFerry));
}

// The builtin profiles must be able to board a boat, or O5e's whole point —
// that a ferry is IN the network — never reaches a default caller.
TEST(RouteRules, BuiltinProfilesAllowFerries) {
  const auto rules = RouteRules::Builtin();
  for (const char* name : {"car", "bicycle", "foot"}) {
    const fv::routing::RouteProfile* p = rules->Find(name);
    ASSERT_NE(p, nullptr) << name;
    EXPECT_TRUE(p->allows(RoadClass::kFerry)) << name;
    EXPECT_DOUBLE_EQ(p->toll_penalty, 1.0) << name;
    EXPECT_DOUBLE_EQ(p->ferry_penalty, 1.0) << name;
  }
}

// SelectProfile mirrors the two onto the options, and that mirror is not
// cosmetic the way the others are: it is where a profile's avoidances take
// effect, and it is why a caller applies its own override afterwards.
TEST(RouteRules, SelectProfileSeedsTheAvoidancesAndTheCallerHasTheLastWord) {
  const auto rules = ParseOk(R"({"version":1,"default_profile":"tolled","profiles":{
      "tolled":{"mode":"motor_vehicle","toll_penalty":"exclude","ferry_penalty":3.0,
                "unlisted_classes":1.0}}})");
  ASSERT_NE(rules, nullptr);

  RouteOptions o;
  ASSERT_EQ(SelectProfile(rules, "tolled", &o).code, fv::kOk);
  EXPECT_DOUBLE_EQ(o.toll_penalty, fv::routing::kAvoidExcluded);
  EXPECT_DOUBLE_EQ(o.ferry_penalty, 3.0);

  // Set afterwards — the order the CLI and the binding both use — and the
  // query wins, so "this profile, but ferries are fine today" needs no
  // profile of its own.
  o.ferry_penalty = 1.0;
  EXPECT_DOUBLE_EQ(o.ferry_penalty, 1.0);
  EXPECT_DOUBLE_EQ(o.toll_penalty, fv::routing::kAvoidExcluded);
}

}  // namespace
