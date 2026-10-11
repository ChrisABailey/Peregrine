// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the shared MapLibre expression evaluator.
#include <map>

#include <gtest/gtest.h>

#include "fvkit/style/expression.h"

namespace fv {
namespace {

/// A feature with text attributes, as the vector seam carries them.
StyleExpressionInput Input(std::map<std::string, std::string> props, double zoom = 10,
                           const std::string& geometry = "Point") {
  StyleExpressionInput in;
  auto shared = std::make_shared<std::map<std::string, std::string>>(std::move(props));
  in.get = [shared](const std::string& key, StyleValue* v) {
    auto it = shared->find(key);
    if (it == shared->end()) return false;
    *v = StyleValue::String(it->second);
    return true;
  };
  in.geometry_type = geometry;
  in.zoom = zoom;
  return in;
}

StyleValue Eval(const std::string& text, const StyleExpressionInput& in) {
  StyleExpression e;
  const Status s = StyleExpression::Parse(text, &e);
  EXPECT_TRUE(s.ok()) << text << ": " << s.message;
  return e.Evaluate(in);
}

double Num(const std::string& text, const StyleExpressionInput& in) {
  double v = -999;
  EXPECT_TRUE(Eval(text, in).AsNumber(&v)) << text;
  return v;
}

std::string Str(const std::string& text, const StyleExpressionInput& in) {
  return Eval(text, in).ToString();
}

bool Bool(const std::string& text, const StyleExpressionInput& in) {
  const StyleValue v = Eval(text, in);
  EXPECT_EQ(v.type, StyleValue::Type::kBool) << text;
  return v.boolean;
}

TEST(StyleExpression, LiteralsAndLookups) {
  const auto in = Input({{"class", "motorway"}, {"admin_level", "2"}}, 12.5, "LineString");
  EXPECT_EQ(Num("3.5", in), 3.5);
  EXPECT_EQ(Str("\"x\"", in), "x");
  EXPECT_EQ(Str("[\"get\", \"class\"]", in), "motorway");
  EXPECT_TRUE(Eval("[\"get\", \"missing\"]", in).is_null());
  EXPECT_TRUE(Bool("[\"has\", \"class\"]", in));
  EXPECT_FALSE(Bool("[\"has\", \"missing\"]", in));
  EXPECT_EQ(Str("[\"geometry-type\"]", in), "LineString");
  EXPECT_EQ(Num("[\"zoom\"]", in), 12.5);
  EXPECT_TRUE(Eval("[\"id\"]", in).is_null());
  EXPECT_EQ(Str("[\"literal\", [1, \"a\"]]", in), "[1,\"a\"]");
}

TEST(StyleExpression, ComparisonsTreatNumericTextAsNumbers) {
  const auto in = Input({{"admin_level", "2"}, {"oneway", "true"}, {"name", "Elm"}});
  EXPECT_TRUE(Bool("[\"==\", [\"get\", \"admin_level\"], 2]", in));
  EXPECT_TRUE(Bool("[\"==\", [\"get\", \"admin_level\"], \"2\"]", in));
  EXPECT_TRUE(Bool("[\"==\", [\"get\", \"oneway\"], true]", in));
  EXPECT_TRUE(Bool("[\"<\", [\"get\", \"admin_level\"], 4]", in));
  EXPECT_FALSE(Bool("[\">=\", [\"get\", \"admin_level\"], 4]", in));
  EXPECT_TRUE(Bool("[\"<\", [\"get\", \"name\"], \"Oak\"]", in));
  // A missing value compares false, never true.
  EXPECT_FALSE(Bool("[\"<\", [\"get\", \"missing\"], 4]", in));
  EXPECT_TRUE(Bool("[\"!=\", [\"get\", \"missing\"], 4]", in));
}

TEST(StyleExpression, Logic) {
  const auto in = Input({{"class", "river"}, {"brunnel", "bridge"}});
  EXPECT_TRUE(Bool("[\"all\", [\"==\", [\"get\", \"class\"], \"river\"], [\"has\", \"brunnel\"]]", in));
  EXPECT_FALSE(Bool("[\"all\", [\"==\", [\"get\", \"class\"], \"river\"], [\"has\", \"x\"]]", in));
  EXPECT_TRUE(Bool("[\"any\", [\"has\", \"x\"], [\"has\", \"class\"]]", in));
  EXPECT_TRUE(Bool("[\"!\", [\"has\", \"x\"]]", in));
  EXPECT_TRUE(Bool("[\"in\", [\"get\", \"class\"], [\"literal\", [\"stream\", \"river\"]]]", in));
  EXPECT_TRUE(Bool("[\"in\", \"idg\", [\"get\", \"brunnel\"]]", in));
  EXPECT_TRUE(Bool("[\"all\"]", in));
  EXPECT_FALSE(Bool("[\"any\"]", in));
}

TEST(StyleExpression, MatchCaseCoalesce) {
  const auto in = Input({{"class", "primary"}, {"rank", "3"}, {"name", "Main"}});
  const std::string m =
      "[\"match\", [\"get\", \"class\"], [\"motorway\", \"trunk\"], 1, \"primary\", 2, 0]";
  EXPECT_EQ(Num(m, in), 2);
  EXPECT_EQ(Num(m, Input({{"class", "trunk"}})), 1);
  EXPECT_EQ(Num(m, Input({{"class", "track"}})), 0);
  EXPECT_EQ(Num(m, Input({})), 0);
  EXPECT_EQ(Str("[\"match\", [\"get\", \"rank\"], 3, \"three\", \"other\"]", in), "three");
  EXPECT_EQ(Str("[\"case\", [\"has\", \"x\"], \"a\", [\"has\", \"rank\"], \"b\", \"c\"]", in), "b");
  EXPECT_EQ(Str("[\"coalesce\", [\"get\", \"name:latin\"], [\"get\", \"name\"]]", in), "Main");
}

TEST(StyleExpression, StepAndNumericInterpolation) {
  const std::string step = "[\"step\", [\"zoom\"], 1, 10, 2, 14, 3]";
  EXPECT_EQ(Num(step, Input({}, 9.9)), 1);
  EXPECT_EQ(Num(step, Input({}, 10)), 2);
  EXPECT_EQ(Num(step, Input({}, 15)), 3);
  // A missing input takes the base output.
  EXPECT_EQ(Num("[\"step\", [\"get\", \"flow\"], 1, 10, 2]", Input({})), 1);
  EXPECT_EQ(Num("[\"step\", [\"get\", \"flow\"], 1, 10, 2]", Input({{"flow", "12"}})), 2);

  const std::string lin = "[\"interpolate\", [\"linear\"], [\"zoom\"], 10, 1, 14, 5]";
  EXPECT_EQ(Num(lin, Input({}, 8)), 1);
  EXPECT_DOUBLE_EQ(Num(lin, Input({}, 12)), 3);
  EXPECT_EQ(Num(lin, Input({}, 20)), 5);
  // MapLibre's exponential form: (b^p - 1) / (b^d - 1).
  const std::string exp = "[\"interpolate\", [\"exponential\", 2], [\"zoom\"], 10, 0, 12, 3]";
  EXPECT_DOUBLE_EQ(Num(exp, Input({}, 11)), 1.0);
}

TEST(StyleExpression, ColoursInterpolate) {
  const std::string e =
      "[\"interpolate\", [\"linear\"], [\"zoom\"], 10, \"#000000\", 20, \"rgba(200, 100, 50, 0.5)\"]";
  FvColor c;
  ASSERT_TRUE(Eval(e, Input({}, 15)).AsColor(&c));
  EXPECT_EQ(c.r, 100);
  EXPECT_EQ(c.g, 50);
  EXPECT_EQ(c.b, 25);
  EXPECT_EQ(c.a, 192);  // 255 + 0.5 * (128 - 255), rounded
  ASSERT_TRUE(Eval(e, Input({}, 5)).AsColor(&c));
  EXPECT_EQ(c.r, 0);
  EXPECT_EQ(c.a, 255);
}

TEST(StyleExpression, StringsAndMath) {
  const auto in = Input({{"name", "Elm"}, {"ref", "12"}});
  EXPECT_EQ(Str("[\"concat\", [\"get\", \"name\"], \" \", [\"get\", \"ref\"]]", in), "Elm 12");
  EXPECT_EQ(Str("[\"upcase\", [\"get\", \"name\"]]", in), "ELM");
  EXPECT_EQ(Str("[\"to-string\", 2.50]", in), "2.5");
  EXPECT_EQ(Num("[\"to-number\", [\"get\", \"ref\"]]", in), 12);
  EXPECT_EQ(Num("[\"*\", [\"get\", \"ref\"], 2, 0.5]", in), 12);
  EXPECT_EQ(Num("[\"-\", 3]", in), -3);
  EXPECT_EQ(Num("[\"max\", 1, 7, 3]", in), 7);
  EXPECT_TRUE(Eval("[\"/\", [\"get\", \"name\"], 2]", in).is_null());
  FvColor c;
  ASSERT_TRUE(Eval("[\"rgba\", 10, 20, 30, 1]", in).AsColor(&c));
  EXPECT_EQ(c.g, 20);
}

TEST(StyleExpression, ConstantDetection) {
  StyleExpression e;
  ASSERT_TRUE(StyleExpression::Parse("[\"*\", 2, 3]", &e).ok());
  EXPECT_TRUE(e.is_constant());
  ASSERT_TRUE(StyleExpression::Parse("[\"step\", [\"zoom\"], 1, 10, 2]", &e).ok());
  EXPECT_FALSE(e.is_constant());
  EXPECT_TRUE(e.reads_zoom());
  ASSERT_TRUE(StyleExpression::Parse("[\"get\", \"x\"]", &e).ok());
  EXPECT_FALSE(e.is_constant());
  EXPECT_FALSE(e.reads_zoom());
}

TEST(StyleExpression, ParseFailuresNameTheProblem) {
  StyleExpression e;
  Status s = StyleExpression::Parse("[\"format\", \"a\", {}]", &e);
  EXPECT_EQ(s.code, kUnsupported);
  EXPECT_NE(s.message.find("format"), std::string::npos);
  s = StyleExpression::Parse("[\"interpolate\", [\"cubic-bezier\", 0, 0, 1, 1], [\"zoom\"], 1, 1, 2, 2]", &e);
  EXPECT_EQ(s.code, kUnsupported);
  EXPECT_FALSE(StyleExpression::Parse("[\"step\", [\"zoom\"], 1, 10, 2, 5, 3]", &e).ok());  // descending
  EXPECT_FALSE(StyleExpression::Parse("[\"match\", [\"get\", \"x\"], 1, 2]", &e).ok());     // no fallback
  EXPECT_FALSE(StyleExpression::Parse("[\"==\", 1]", &e).ok());
  EXPECT_FALSE(StyleExpression::Parse("{\"a\": 1}", &e).ok());
  EXPECT_FALSE(StyleExpression::Parse("[", &e).ok());
}

TEST(StyleExpression, CssColours) {
  FvColor c;
  ASSERT_TRUE(ParseCssColor("#abc", &c));
  EXPECT_EQ(c.r, 0xaa);
  ASSERT_TRUE(ParseCssColor("hsl(120, 100%, 50%)", &c));
  EXPECT_EQ(c.g, 255);
  EXPECT_EQ(c.r, 0);
  ASSERT_TRUE(ParseCssColor("rgba(1, 2, 3, 0)", &c));
  EXPECT_EQ(c.a, 0);
  EXPECT_FALSE(ParseCssColor("chartreuse", &c));
}

}  // namespace
}  // namespace fv
