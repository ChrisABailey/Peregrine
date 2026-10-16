// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fv_desk_fake.h"
#include "fv_desk_rule_style.h"
#include "fv_desk_style_rules.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/raster.h"
#include "scratch.h"

namespace {

using fv::FeatureOverlay;
using fv::GeoPoint;
using fv::desk::FakeDesk;
using fv::desk::RuleProperty;
using fv::desk::RulePropertyValue;
using fv::desk::RuleRow;
using fv::desk::RuleValueMode;
using json = nlohmann::json;

const std::string kRivers = FV_FEATURE_RIVERS;
const std::string kGrid = std::string("overlay.toggle.") + fv::app::kGridTypeId;

/// Property `key` of the `type` pages.
const RuleProperty& Prop(const std::string& type, const std::string& key) {
  for (const RuleProperty& p : fv::desk::RuleProperties(type))
    if (p.key == key) return p;
  ADD_FAILURE() << key;
  static RuleProperty none;
  return none;
}

std::string Write(const RuleProperty& p, const RulePropertyValue& v) {
  std::string text;
  const fv::Status s = fv::desk::WriteRuleValue(p, v, &text);
  EXPECT_TRUE(s.ok()) << s.message;
  return text;
}

TEST(RuleValue, EachModeReadsAndWritesBack) {
  const RuleProperty& color = Prop("line", "line-color");
  const RuleProperty& width = Prop("line", "line-width");

  RulePropertyValue v = fv::desk::ReadRuleValue(color, "");
  EXPECT_EQ(v.mode, RuleValueMode::kDefault);
  EXPECT_EQ(Write(color, v), "");

  v = fv::desk::ReadRuleValue(color, R"("#e892a2")");
  EXPECT_EQ(v.mode, RuleValueMode::kConstant);
  EXPECT_EQ(v.constant, "#e892a2");
  EXPECT_EQ(Write(color, v), R"("#e892a2")");

  v = fv::desk::ReadRuleValue(width, "2.5");
  EXPECT_EQ(v.constant, "2.5");
  v.constant = "3";
  EXPECT_EQ(Write(width, v), "3");

  // The generators' categorised form.
  const std::string match = R"(["match",["to-string",["get","class"]],"motorway","#e892a2","trunk","#f9b29c","#ffffff"])";
  v = fv::desk::ReadRuleValue(color, match);
  ASSERT_EQ(v.mode, RuleValueMode::kByField);
  EXPECT_EQ(v.field, "class");
  EXPECT_EQ(v.constant, "#ffffff");
  ASSERT_EQ(v.stops.size(), 2u);
  EXPECT_EQ(v.stops[1].key, "trunk");
  EXPECT_EQ(v.stops[1].value, "#f9b29c");
  EXPECT_EQ(Write(color, v), match);

  const std::string step = R"(["step",["get","depth"],1,5,2,10,4])";
  v = fv::desk::ReadRuleValue(width, step);
  ASSERT_EQ(v.mode, RuleValueMode::kByClass);
  EXPECT_EQ(v.constant, "1");
  EXPECT_EQ(v.stops[1].key, "10");
  EXPECT_EQ(Write(width, v), step);

  const std::string zoom = R"(["interpolate",["linear"],["zoom"],10,1,16,6.5])";
  v = fv::desk::ReadRuleValue(width, zoom);
  ASSERT_EQ(v.mode, RuleValueMode::kByZoom);
  EXPECT_EQ(v.stops[1].value, "6.5");
  EXPECT_EQ(Write(width, v), zoom);

  // Anything else is an expression, kept as written.
  const std::string exp = R"(["interpolate",["exponential",1.2],["zoom"],12,0.5,16,4])";
  v = fv::desk::ReadRuleValue(width, exp);
  EXPECT_EQ(v.mode, RuleValueMode::kExpression);
  EXPECT_EQ(v.expression, exp);
  EXPECT_EQ(Write(width, v), exp);

  const RuleProperty& label = Prop("symbol", "text-field");
  v = fv::desk::ReadRuleValue(label, R"(["get","name"])");
  EXPECT_EQ(v.mode, RuleValueMode::kConstant);
  EXPECT_EQ(v.constant, "name");
  const RuleProperty& dash = Prop("line", "line-dasharray");
  v = fv::desk::ReadRuleValue(dash, "[4,2]");
  EXPECT_EQ(v.constant, "4 2");
  v.constant = "3 1.5";
  EXPECT_EQ(Write(dash, v), "[3,1.5]");
}

TEST(RuleValue, BadValuesAreRefusedWithTheProperty) {
  const RuleProperty& color = Prop("line", "line-color");
  const RuleProperty& opacity = Prop("line", "line-opacity");
  std::string text;
  RulePropertyValue v;
  v.mode = RuleValueMode::kConstant;
  v.constant = "bluish";
  EXPECT_EQ(fv::desk::WriteRuleValue(color, v, &text).code, fv::kInvalidArg);
  v.constant = "1.5";
  // Errors name the page as well as the property, since "Size" appears on several pages.
  EXPECT_NE(fv::desk::WriteRuleValue(opacity, v, &text).message.find("Line opacity"), std::string::npos);

  v.mode = RuleValueMode::kByField;
  v.field = "class";
  v.constant = "#fff";
  v.stops = {{"a", "#000"}, {"a", "#111"}};
  EXPECT_NE(fv::desk::WriteRuleValue(color, v, &text).message.find("twice"), std::string::npos);

  v.mode = RuleValueMode::kByZoom;
  v.stops = {{"12", "#000"}, {"10", "#111"}};
  EXPECT_EQ(fv::desk::WriteRuleValue(color, v, &text).code, fv::kInvalidArg);

  // A choice property offers no zoom table.
  v.mode = RuleValueMode::kByZoom;
  EXPECT_EQ(fv::desk::WriteRuleValue(Prop("symbol", "symbol-placement"), v, &text).code, fv::kInvalidArg);
}

/// A FakeDesk on Charleston harbour with the grid and a copy of the rivers
/// sample (three lines, fields name and depth) open.
class RuleStyleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto& mv = fd_.desk().map_view();
    mv.SetViewport(mv.View().WithCamera(GeoPoint{32.86, -79.95}, 250000, 0));
    path_ = dir_.Path("rivers.geojson");
    std::filesystem::copy_file(kRivers, path_);
    ASSERT_TRUE(fd_.Run({kGrid}).ok());
    fd_.shell().files_to_open = {path_};
    ASSERT_TRUE(fd_.Run({"overlay.open"}).ok());
    rivers_ = std::dynamic_pointer_cast<FeatureOverlay>(fd_.desk().overlays().OfType(FeatureOverlay::kTypeId)[0]);
    ASSERT_TRUE(rivers_);
  }

  fv::desk::Desk& desk() { return fd_.desk(); }
  size_t Row() {
    const auto rows = desk().layers().Rows();
    for (size_t i = 0; i < rows.size(); ++i)
      if (rows[i].overlay == rivers_.get()) return i;
    ADD_FAILURE() << "rivers not listed";
    return 0;
  }
  std::vector<RuleRow> Rules() { return desk().layers().Rows()[Row()].rules; }
  /// The rule `id` as the overlay draws it.
  fv::StyleRule Rule(const std::string& id) {
    fv::FeatureStyle rules;
    EXPECT_TRUE(fv::FeatureStyleToRules(rivers_->style(), &rules).ok());
    for (const fv::StyleRule& r : rules.rules)
      if (r.id == id) return r;
    ADD_FAILURE() << id;
    return {};
  }
  size_t K(const std::string& key) {
    const auto& props = desk().rule_style()->properties();
    for (size_t k = 0; k < props.size(); ++k)
      if (props[k].key == key) return k;
    ADD_FAILURE() << key;
    return 0;
  }
  std::vector<uint8_t> Pixels() {
    fv::PixelBuffer buf;
    EXPECT_TRUE(fd_.Render(&buf).ok());
    std::vector<uint8_t> out;
    for (int y = 0; y < buf.Height(); ++y) out.insert(out.end(), buf.Row(y), buf.Row(y) + 4 * buf.Width());
    return out;
  }

  fv::desk::test::Scratch dir_;
  FakeDesk fd_;
  std::string path_;
  std::shared_ptr<FeatureOverlay> rivers_;
};

TEST_F(RuleStyleTest, ARoadsStyleIsBuiltByHandAndEachStepUndoes) {
  // A "Roads" line rule, coloured by name, its width by zoom.
  ASSERT_TRUE(desk().AddRule(Row(), "Roads", "line").ok());
  const std::vector<uint8_t> plain = Pixels();
  ASSERT_TRUE(fd_.Run({"layers.rule.edit_style"}).ok());
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  ASSERT_TRUE(desk().rule_style());
  auto& m = *desk().rule_style();
  EXPECT_EQ(m.rule_id(), "Roads");
  EXPECT_EQ(m.type(), "line");
  EXPECT_FALSE(m.dirty());

  const size_t color = K("line-color");
  ASSERT_TRUE(m.SetMode(color, RuleValueMode::kByField).ok());
  RulePropertyValue v = m.value(color);
  EXPECT_EQ(v.constant, "#1e5ac8");  // the constant carries across as the other value
  v.field = "name";
  ASSERT_TRUE(m.SetValue(color, v).ok());
  ASSERT_TRUE(m.FillFieldValues(color).ok());
  v = m.value(color);
  ASSERT_EQ(v.stops.size(), 3u);  // the three rivers, sorted
  v.stops[0].value = "#e892a2";
  ASSERT_TRUE(m.SetValue(color, v).ok());
  v.stops[1].value = "not a colour";
  EXPECT_EQ(m.SetValue(color, v).code, fv::kInvalidArg);

  const size_t width = K("line-width");
  ASSERT_TRUE(m.SetMode(width, RuleValueMode::kByZoom).ok());
  v = m.value(width);
  v.stops = {{"8", "2"}, {"14", "6"}};
  ASSERT_TRUE(m.SetValue(width, v).ok());
  m.SetLegend("Main roads");
  EXPECT_TRUE(m.dirty());

  ASSERT_TRUE(desk().ApplyRuleStyle().ok());
  EXPECT_FALSE(desk().rule_style()->dirty());
  EXPECT_EQ(desk().undo().UndoLabel(), "Edit Rule Style");
  const json paint = json::parse(Rule("Roads").paint);
  EXPECT_EQ(paint["line-color"][0], "match");
  EXPECT_EQ(paint["line-width"], json::parse(R"(["interpolate",["linear"],["zoom"],8,2,14,6])"));
  EXPECT_EQ(Rule("Roads").legend, "Main roads");
  const std::vector<uint8_t> styled = Pixels();
  EXPECT_NE(styled, plain);

  // A casing beneath, 2 px wider at every zoom stop.
  ASSERT_TRUE(fd_.Run({"layers.rule.add_casing"}).ok());
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  EXPECT_EQ(Rules()[0].id, "Roads");
  EXPECT_EQ(Rules()[1].id, "Roads casing");
  EXPECT_EQ(desk().CurrentRule(), 1);
  EXPECT_EQ(json::parse(Rule("Roads casing").paint)["line-width"],
            json::parse(R"(["interpolate",["linear"],["zoom"],8,4.0,14,8.0])"));

  // A label rule over the name field.
  ASSERT_TRUE(desk().AddRule(Row(), "Road names", "symbol").ok());
  ASSERT_TRUE(desk().OpenRuleStyle().ok());
  EXPECT_EQ(desk().rule_style()->rule_id(), "Road names");
  const size_t field = K("text-field");
  v = desk().rule_style()->value(field);
  v.mode = RuleValueMode::kConstant;
  v.constant = "name";
  ASSERT_TRUE(desk().rule_style()->SetValue(field, v).ok());
  const size_t placement = K("symbol-placement");
  v = desk().rule_style()->value(placement);
  v.mode = RuleValueMode::kConstant;
  v.constant = "line";
  ASSERT_TRUE(desk().rule_style()->SetValue(placement, v).ok());
  ASSERT_TRUE(desk().ApplyRuleStyle().ok());
  EXPECT_EQ(json::parse(Rule("Road names").layout)["symbol-placement"], "line");

  // Each step undoes: label style, label rule, casing, roads style.
  ASSERT_TRUE(desk().undo().Undo().ok());
  EXPECT_EQ(json::parse(Rule("Road names").layout).count("symbol-placement"), 0u);
  ASSERT_TRUE(desk().undo().Undo().ok());  // Add Rule: the open pages' rule is gone, so they close
  EXPECT_FALSE(desk().rule_style());
  ASSERT_TRUE(desk().undo().Undo().ok());
  EXPECT_NE(Rules()[1].id, "Roads casing");
  EXPECT_EQ(Pixels(), styled);
  ASSERT_TRUE(desk().undo().Undo().ok());
  EXPECT_EQ(json::parse(Rule("Roads").paint)["line-color"], "#1e5ac8");
  EXPECT_EQ(Pixels(), plain);
}

TEST_F(RuleStyleTest, UneditedPropertiesKeepTheirJsonAndExpressionsVerify) {
  ASSERT_TRUE(desk().AddRule(Row(), "Roads", "line").ok());
  // A width no table can show, set as an expression.
  const std::string exp = R"(["interpolate",["exponential",1.2],["zoom"],12,0.5,16,4])";
  ASSERT_TRUE(desk().OpenRuleStyle().ok());
  auto& m = *desk().rule_style();
  const size_t width = K("line-width");
  EXPECT_EQ(m.Verify(width, R"(["no-such-op",1])").code, fv::kInvalidArg);
  EXPECT_EQ(m.Verify(width, "[1,").code, fv::kInvalidArg);
  ASSERT_TRUE(m.Verify(width, exp).ok());
  ASSERT_TRUE(m.SetMode(width, RuleValueMode::kExpression).ok());
  EXPECT_EQ(m.value(width).expression, "2");  // starts from the constant it held
  RulePropertyValue v = m.value(width);
  v.expression = exp;
  ASSERT_TRUE(m.SetValue(width, v).ok());
  ASSERT_TRUE(desk().ApplyRuleStyle().ok());
  EXPECT_EQ(json::parse(Rule("Roads").paint)["line-width"], json::parse(exp));
  EXPECT_EQ(desk().rule_style()->value(width).mode, RuleValueMode::kExpression);

  // Editing the colour leaves the width's JSON as it was.
  const size_t color = K("line-color");
  v = desk().rule_style()->value(color);
  v.constant = "#000000";
  ASSERT_TRUE(desk().rule_style()->SetValue(color, v).ok());
  ASSERT_TRUE(desk().ApplyRuleStyle().ok());
  const json paint = json::parse(Rule("Roads").paint);
  EXPECT_EQ(paint["line-color"], "#000000");
  EXPECT_EQ(paint["line-width"], json::parse(exp));

  // A table without rows does not apply, and the rule is unchanged.
  ASSERT_TRUE(desk().rule_style()->SetMode(color, RuleValueMode::kByZoom).ok());
  v = desk().rule_style()->value(color);
  v.stops.clear();
  ASSERT_TRUE(desk().rule_style()->SetValue(color, v).ok());
  EXPECT_EQ(desk().ApplyRuleStyle().code, fv::kInvalidArg);
  EXPECT_EQ(json::parse(Rule("Roads").paint)["line-color"], "#000000");
  ASSERT_TRUE(desk().RevertRuleStyle().ok());
  EXPECT_FALSE(desk().rule_style()->dirty());
}

TEST_F(RuleStyleTest, AConstantStartsFromTheEngineDefaultAndTextTakesAnOffset) {
  ASSERT_TRUE(desk().AddRule(Row(), "Names", "symbol").ok());
  ASSERT_TRUE(desk().OpenRuleStyle().ok());
  auto& m = *desk().rule_style();
  // No icon properties: a feature layer has no sprite sheet to name.
  for (const RuleProperty& p : m.properties()) EXPECT_EQ(p.key.rfind("icon-", 0), std::string::npos) << p.key;

  const size_t halo = K("text-halo-width");
  ASSERT_TRUE(m.SetMode(halo, RuleValueMode::kDefault).ok());
  ASSERT_TRUE(m.SetMode(halo, RuleValueMode::kConstant).ok());
  EXPECT_EQ(m.value(halo).constant, "0");  // never an empty box

  const size_t offset = K("text-offset");
  ASSERT_TRUE(m.SetMode(offset, RuleValueMode::kConstant).ok());
  RulePropertyValue v = m.value(offset);
  EXPECT_EQ(v.constant, "0 0");
  v.constant = "1.5";
  EXPECT_NE(m.SetValue(offset, v).message.find("Text offset"), std::string::npos);
  v.constant = "0 -1.2";
  ASSERT_TRUE(m.SetValue(offset, v).ok());
  const size_t anchor = K("text-anchor");
  v = m.value(anchor);
  v.mode = RuleValueMode::kConstant;
  v.constant = "bottom";
  ASSERT_TRUE(m.SetValue(anchor, v).ok());
  ASSERT_TRUE(desk().ApplyRuleStyle().ok());
  const json layout = json::parse(Rule("Names").layout);
  EXPECT_EQ(layout["text-offset"], json::parse("[0,-1.2]"));
  EXPECT_EQ(layout["text-anchor"], "bottom");
  EXPECT_EQ(fv::desk::ReadRuleValue(Prop("symbol", "text-offset"), "[0,-1.2]").constant, "0 -1.2");
}

TEST_F(RuleStyleTest, PagesFollowAnUneditedRuleAndCloseWhenItGoes) {
  ASSERT_TRUE(desk().AddRule(Row(), "Roads", "line").ok());
  ASSERT_TRUE(desk().OpenRuleStyle().ok());
  ASSERT_TRUE(desk().SetRuleType(Row(), 0, "circle").ok());
  ASSERT_TRUE(desk().rule_style());
  EXPECT_EQ(desk().rule_style()->type(), "circle");
  EXPECT_EQ(desk().rule_style()->properties().front().key, "circle-radius");
  // Add Casing is for line rules only.
  EXPECT_FALSE(desk().commands().IsEnabled("layers.rule.add_casing"));
  ASSERT_TRUE(desk().DeleteRule(Row(), 0).ok());
  EXPECT_FALSE(desk().rule_style());
}

}  // namespace
