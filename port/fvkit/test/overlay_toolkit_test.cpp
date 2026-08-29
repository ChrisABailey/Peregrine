// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The shared overlay toolkit: scale_table.h, canvas/label_placer.h and
// app/properties.h. Each is tested here on its OWN, without a grid, because
// the point of extracting them is that the next overlay uses them.

#include <gtest/gtest.h>

#include <cstdio>

#include "fvkit/app/properties.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/canvas/label_placer.h"
#include "fvkit/proj.h"
#include "fvkit/scale_table.h"
#include "fvkit/settings.h"

namespace {

// A real font, so the label paths are exercised rather than skipped. Same
// candidate list as geo_draw_test's SystemFont.
std::string SystemFont() {
  const char* candidates[] = {
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/Supplemental/Courier New.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  };
  for (const char* f : candidates)
    if (FILE* fp = fopen(f, "rb")) {
      fclose(fp);
      return f;
    }
  return std::string();
}

// ---------------------------------------------------------------------------
// ScaleTable
// ---------------------------------------------------------------------------

TEST(ScaleTable, NearestClampsAtBothEnds) {
  fv::ScaleTable<int> t({{1000000.0, 1}, {100000.0, 2}, {10000.0, 3}});
  EXPECT_EQ(*t.Nearest(50000000.0), 1);  // far coarser than any row
  EXPECT_EQ(*t.Nearest(100.0), 3);       // far finer than any row
  EXPECT_EQ(*t.Nearest(100000.0), 2);    // exact
}

TEST(ScaleTable, SortsRegardlessOfConstructionOrder) {
  fv::ScaleTable<int> t({{10000.0, 3}, {1000000.0, 1}, {100000.0, 2}});
  ASSERT_EQ(t.size(), 3u);
  EXPECT_EQ(t.rows()[0].scale_denominator, 10000.0);
  EXPECT_EQ(t.rows()[2].scale_denominator, 1000000.0);
}

TEST(ScaleTable, NearestPicksTheCloserRow) {
  fv::ScaleTable<int> t({{200000.0, 1}, {1000000.0, 2}});
  EXPECT_EQ(*t.Nearest(300000.0), 1);
  EXPECT_EQ(*t.Nearest(900000.0), 2);
}

TEST(ScaleTable, EmptyAnswersNull) {
  fv::ScaleTable<int> t;
  EXPECT_EQ(t.Nearest(1000.0), nullptr);
  EXPECT_TRUE(t.empty());
}

// The reason ScaleDenominatorFor exists: resolution mode reports Scale() == 0,
// and an overlay keyed on that would draw nothing on a tile pyramid (which is
// the mode Pippin runs in).
TEST(ScaleTable, ScaleDenominatorSurvivesResolutionMode) {
  fv::MapProjection scaled;
  ASSERT_TRUE(scaled.SetSurfaceSize(256, 256).ok());
  ASSERT_TRUE(scaled.SetCenter({33.7488, -84.3882}).ok());
  ASSERT_TRUE(scaled.SetScale(2000000.0).ok());
  EXPECT_DOUBLE_EQ(fv::ScaleDenominatorFor(scaled), 2000000.0);

  // Same map, expressed as degrees per pixel instead of as a scale.
  fv::MapProjection res;
  ASSERT_TRUE(res.SetSurfaceSize(256, 256).ok());
  ASSERT_TRUE(res.SetCenter({33.7488, -84.3882}).ok());
  ASSERT_TRUE(res.SetResolution(scaled.DegPerPixelLat(), scaled.DegPerPixelLon())
                  .ok());
  EXPECT_EQ(res.Scale(), 0.0);  // the trap
  // The inverse is exact to the int truncation in ResolutionToScale.
  EXPECT_NEAR(fv::ScaleDenominatorFor(res), 2000000.0, 1.0);
}

TEST(ScaleTable, NotReadyIsZeroRatherThanGarbage) {
  fv::MapProjection p;
  EXPECT_EQ(fv::ScaleDenominatorFor(p), 0.0);
}

// ---------------------------------------------------------------------------
// LabelPlacer
// ---------------------------------------------------------------------------

class LabelPlacerTest : public ::testing::Test {
 protected:
  LabelPlacerTest() : canvas_(200, 100) {
    canvas_.SetDefaultFont(SystemFont());
    style_.valid = true;
    style_.style.size = 12.0;
    style_.style.color = fv::FvColor{255, 255, 255, 255};
  }
  fv::CpuCanvas canvas_;
  fv::LabelStyle style_;
  fv::LabelPlacer placer_;
};

TEST_F(LabelPlacerTest, SecondLabelOnTheSameSpotIsRefused) {
  fv::LabelInk a, b;
  EXPECT_TRUE(placer_.Place(canvas_, 50, 50, "N 34", style_.style, style_, &a));
  ASSERT_TRUE(a.measured) << "canvas has no text metrics; the rest is vacuous";
  EXPECT_FALSE(placer_.Place(canvas_, 52, 50, "E 084", style_.style, style_, &b));
  EXPECT_EQ(placer_.placed_count(), 1u);
  EXPECT_EQ(placer_.rejected_count(), 1u);
  EXPECT_EQ(placer_.boxes().size(), 1u);
}

TEST_F(LabelPlacerTest, LabelsFarApartBothLand) {
  EXPECT_TRUE(placer_.Place(canvas_, 10, 20, "N 34", style_.style, style_, nullptr));
  EXPECT_TRUE(placer_.Place(canvas_, 10, 80, "N 33", style_.style, style_, nullptr));
  EXPECT_EQ(placer_.rejected_count(), 0u);
}

TEST_F(LabelPlacerTest, CallOrderIsPriority) {
  // The rule the grid depends on: whoever asks first keeps the spot.
  fv::LabelInk first;
  ASSERT_TRUE(placer_.Place(canvas_, 40, 40, "FIRST", style_.style, style_, &first));
  EXPECT_FALSE(placer_.Place(canvas_, 40, 40, "SECOND", style_.style, style_, nullptr));
  ASSERT_EQ(placer_.boxes().size(), 1u);
  EXPECT_EQ(placer_.boxes()[0].x, first.box.x);
}

TEST_F(LabelPlacerTest, OffCanvasIsRefused) {
  EXPECT_FALSE(placer_.Place(canvas_, -80, 50, "N 34", style_.style, style_, nullptr));
  EXPECT_FALSE(placer_.Place(canvas_, 100, 400, "N 34", style_.style, style_, nullptr));
}

TEST_F(LabelPlacerTest, MarginKeepsLabelsOffTheEdge) {
  fv::LabelPlacer tight;
  tight.SetMargin(20);
  // A baseline at y=12 puts the box's top at y≈0, inside the canvas but inside
  // the margin too.
  EXPECT_FALSE(tight.Place(canvas_, 2, 12, "N 34", style_.style, style_, nullptr));
}

TEST_F(LabelPlacerTest, ReserveBlocksARegionWithNoLabelInIt) {
  fv::PixelRect hud;
  hud.x = 0;
  hud.y = 0;
  hud.width = 200;
  hud.height = 40;
  placer_.Reserve(hud);
  EXPECT_FALSE(placer_.Place(canvas_, 20, 30, "N 34", style_.style, style_, nullptr));
  EXPECT_TRUE(placer_.Place(canvas_, 20, 90, "N 34", style_.style, style_, nullptr));
}

TEST_F(LabelPlacerTest, PaddingSeparatesLabelsThatWouldMerelyTouch) {
  fv::LabelInk a;
  ASSERT_TRUE(placer_.Place(canvas_, 10, 50, "AB", style_.style, style_, &a));
  // Right at the edge of the first box: touching, but not overlapping.
  fv::LabelPlacer none;
  none.SetPadding(0);
  fv::LabelInk c;
  ASSERT_TRUE(none.Place(canvas_, 10, 50, "AB", style_.style, style_, &c));
  EXPECT_TRUE(none.Place(canvas_, 10 + c.box.width, 50, "CD", style_.style,
                         style_, nullptr));
  // With the default padding of 2 the same pair is refused.
  EXPECT_FALSE(placer_.Place(canvas_, 10 + a.box.width, 50, "CD", style_.style,
                             style_, nullptr));
}

TEST_F(LabelPlacerTest, FirstFitFallsBackToALaterAnchor) {
  ASSERT_TRUE(placer_.Place(canvas_, 40, 40, "TAKEN", style_.style, style_, nullptr));
  std::vector<fv::SurfacePoint> anchors = {{40, 40}, {40, 90}};
  fv::LabelInk ink;
  size_t which = 99;
  EXPECT_TRUE(placer_.PlaceFirstFit(canvas_, anchors, "N 34", style_.style,
                                    style_, &ink, &which));
  EXPECT_EQ(which, 1u);
}

TEST_F(LabelPlacerTest, ClearForgetsEverything) {
  ASSERT_TRUE(placer_.Place(canvas_, 40, 40, "A", style_.style, style_, nullptr));
  placer_.Clear();
  EXPECT_EQ(placer_.placed_count(), 0u);
  EXPECT_TRUE(placer_.Place(canvas_, 40, 40, "B", style_.style, style_, nullptr));
}

// The box a placer reserves must be the box the draw inks, or the whole thing
// is decoration. Both go through MeasureLabelInk; this pins that they agree
// under an alignment that actually moves the anchor.
TEST_F(LabelPlacerTest, InkBoxMatchesTheAlignedOrigin) {
  fv::LabelStyle s = style_;
  s.halign = fv::LabelHAlign::kCenter;
  s.valign = fv::LabelVAlign::kTop;
  s.dx = 5;
  s.dy = -3;
  const fv::LabelInk ink =
      fv::MeasureLabelInk(canvas_, 100, 50, "N 34", s.style, s);
  ASSERT_TRUE(ink.measured);
  EXPECT_EQ(ink.box.x, ink.origin.x);
  EXPECT_EQ(ink.box.y, ink.origin.y - ink.box.height);
  // Centred horizontally about the offset anchor, and hanging below it.
  EXPECT_EQ(ink.origin.x, 105 - ink.box.width / 2);
  EXPECT_EQ(ink.origin.y, 47 + ink.box.height);
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

// A stand-in overlay: the schema an overlay author writes, with nothing else
// in it. If this works, the mechanism works for every overlay.
class Widget : public fv::app::Properties {
 public:
  Widget() {
    for (const fv::app::PropertySpec& s : Describe())
      values_.push_back(s.default_value);
  }
  const std::vector<fv::app::PropertySpec>& Describe() const override {
    static const std::vector<fv::app::PropertySpec> specs = [] {
      std::vector<fv::app::PropertySpec> v;
      fv::app::PropertySpec on;
      on.key = "on";
      on.label = "Enabled";
      on.type = fv::app::PropertyType::kBool;
      on.default_value = fv::app::PropertyValue::Bool(true);
      v.push_back(on);
      fv::app::PropertySpec w;
      w.key = "width";
      w.label = "Width";
      w.type = fv::app::PropertyType::kInt;
      w.default_value = fv::app::PropertyValue::Int(2);
      w.min = 1;
      w.max = 8;
      v.push_back(w);
      fv::app::PropertySpec c;
      c.key = "colour";
      c.label = "Colour";
      c.type = fv::app::PropertyType::kColor;
      c.default_value =
          fv::app::PropertyValue::Color(fv::FvColor{10, 20, 30, 255});
      v.push_back(c);
      fv::app::PropertySpec f;
      f.key = "opacity";
      f.label = "Opacity";
      f.type = fv::app::PropertyType::kDouble;
      f.default_value = fv::app::PropertyValue::Double(0.5);
      v.push_back(f);
      return v;
    }();
    return specs;
  }
  fv::Status GetProperty(const std::string& key,
                         fv::app::PropertyValue* out) const override {
    const int i = Index(key);
    if (i < 0) return fv::Status::Error(fv::kNotFound, "no such property");
    if (out) *out = values_[i];
    return fv::Status::Ok();
  }
  fv::Status SetProperty(const std::string& key,
                         const fv::app::PropertyValue& v) override {
    const int i = Index(key);
    if (i < 0) return fv::Status::Error(fv::kNotFound, "no such property");
    const fv::app::PropertySpec& s = Describe()[i];
    if (v.type != s.type)
      return fv::Status::Error(fv::kInvalidArg, "wrong type");
    if (s.min != s.max && (v.i < s.min || v.i > s.max))
      return fv::Status::Error(fv::kInvalidArg, "out of range");
    values_[i] = v;
    return fv::Status::Ok();
  }

 private:
  int Index(const std::string& key) const {
    const std::vector<fv::app::PropertySpec>& s = Describe();
    for (size_t i = 0; i < s.size(); ++i)
      if (s[i].key == key) return static_cast<int>(i);
    return -1;
  }
  std::vector<fv::app::PropertyValue> values_;
};

TEST(Properties, UnknownKeyIsNotFoundRatherThanADefault) {
  Widget w;
  fv::app::PropertyValue v;
  EXPECT_EQ(w.GetProperty("nope", &v).code, fv::kNotFound);
  EXPECT_EQ(w.SetProperty("nope", fv::app::PropertyValue::Bool(true)).code,
            fv::kNotFound);
}

TEST(Properties, RangeAndTypeAreEnforced) {
  Widget w;
  EXPECT_EQ(w.SetProperty("width", fv::app::PropertyValue::Int(99)).code,
            fv::kInvalidArg);
  EXPECT_EQ(w.SetProperty("width", fv::app::PropertyValue::Bool(true)).code,
            fv::kInvalidArg);
  EXPECT_EQ(w.GetInt("width", -1), 2);  // unchanged
}

TEST(Properties, ValueStringsRoundTrip) {
  fv::app::PropertyValue c =
      fv::app::PropertyValue::Color(fv::FvColor{1, 2, 3, 4});
  EXPECT_EQ(c.ToString(), "#01020304");
  fv::app::PropertyValue back = fv::app::PropertyValue::Color(fv::FvColor{});
  ASSERT_TRUE(back.FromString("#01020304"));
  EXPECT_EQ(back.color.r, 1);
  EXPECT_EQ(back.color.a, 4);
  // A hand-written colour with no alpha is opaque.
  ASSERT_TRUE(back.FromString("#FF8000"));
  EXPECT_EQ(back.color.a, 255);
  // Garbage keeps the current value rather than zeroing it.
  EXPECT_FALSE(back.FromString("orange"));
  EXPECT_EQ(back.color.r, 255);

  fv::app::PropertyValue b = fv::app::PropertyValue::Bool(false);
  EXPECT_EQ(b.ToString(), "false");
  ASSERT_TRUE(b.FromString("YES"));
  EXPECT_TRUE(b.b);
  EXPECT_FALSE(b.FromString("maybe"));
}

// Colours are read leniently and written canonically. The rgb()/rgba() forms
// are here because they are what a colour picker hands you: the first real
// settings file written against this schema used
// `label_color = "rgba(32, 37, 31, 0.97)"`, a hex-only parser rejected it, and
// the label silently kept its default.
TEST(Properties, ColourAcceptsEverySpellingSomebodyActuallyWrites) {
  fv::app::PropertyValue c =
      fv::app::PropertyValue::Color(fv::FvColor{});
  auto rgba = [&c](const char* text, int r, int g, int b, int a) {
    ASSERT_TRUE(c.FromString(text)) << text;
    EXPECT_EQ(c.color.r, r) << text;
    EXPECT_EQ(c.color.g, g) << text;
    EXPECT_EQ(c.color.b, b) << text;
    EXPECT_EQ(c.color.a, a) << text;
  };

  rgba("#FF8000", 255, 128, 0, 255);       // missing alpha is opaque
  rgba("#ff800040", 255, 128, 0, 64);      // lowercase hex
  rgba("#f80", 255, 136, 0, 255);          // shorthand, each digit doubled
  rgba("#f808", 255, 136, 0, 136);
  rgba("rgb(32, 37, 31)", 32, 37, 31, 255);
  rgba("rgba(32, 37, 31, 0.97)", 32, 37, 31, 247);
  rgba("RGBA(1,2,3,0)", 1, 2, 3, 0);       // case and spacing are free
  rgba("  rgb( 10 , 20 , 30 )  ", 10, 20, 30, 255);

  // CSS's alpha, not a byte: rgba(...,1) is OPAQUE, which is the one place the
  // two conventions disagree.
  rgba("rgba(0, 0, 0, 1)", 0, 0, 0, 255);

  // Out is always canonical, whatever came in.
  ASSERT_TRUE(c.FromString("rgba(32, 37, 31, 0.97)"));
  EXPECT_EQ(c.ToString(), "#20251FF7");

  // And the refusals stay refusals.
  for (const char* bad : {"chartreuse", "rgb(1,2)", "rgba(1,2,3)",
                          "rgb(1,2,3,4)", "#12345", "rgb(1,2,3", "()"}) {
    fv::app::PropertyValue v =
        fv::app::PropertyValue::Color(fv::FvColor{7, 7, 7, 7});
    EXPECT_FALSE(v.FromString(bad)) << bad;
    EXPECT_EQ(v.color.r, 7) << bad << " must leave the value alone";
  }
}

TEST(Properties, SaveThenLoadIsIdentity) {
  Widget a;
  ASSERT_TRUE(a.SetProperty("width", fv::app::PropertyValue::Int(7)).ok());
  ASSERT_TRUE(a.SetProperty("colour", fv::app::PropertyValue::Color(
                                          fv::FvColor{9, 8, 7, 6}))
                  .ok());
  ASSERT_TRUE(a.SetProperty("on", fv::app::PropertyValue::Bool(false)).ok());

  fv::Settings s;
  ASSERT_TRUE(a.SaveTo(&s, "widget.").ok());
  // only_changed: opacity is still at its default and is not written.
  EXPECT_TRUE(s.Has("widget.width"));
  EXPECT_FALSE(s.Has("widget.opacity"));

  Widget b;
  ASSERT_TRUE(b.LoadFrom(s, "widget.").ok());
  EXPECT_EQ(b.GetInt("width", -1), 7);
  EXPECT_EQ(b.GetColor("colour", fv::FvColor{}).g, 8);
  EXPECT_FALSE(b.GetBool("on", true));
  EXPECT_DOUBLE_EQ(b.GetDouble("opacity", -1.0), 0.5);  // untouched default
}

TEST(Properties, OneBadLineDoesNotCostTheOthers) {
  fv::Settings s;
  s.Set("widget.width", "4");
  s.Set("widget.colour", "chartreuse");
  s.Set("widget.on", "false");

  Widget w;
  std::vector<std::string> warnings;
  ASSERT_TRUE(w.LoadFrom(s, "widget.", &warnings).ok());
  EXPECT_EQ(w.GetInt("width", -1), 4);
  EXPECT_FALSE(w.GetBool("on", true));
  EXPECT_EQ(w.GetColor("colour", fv::FvColor{}).r, 10);  // kept the default
  ASSERT_EQ(warnings.size(), 1u);
  EXPECT_NE(warnings[0].find("widget.colour"), std::string::npos);
}

TEST(Properties, OutOfRangeSettingIsWarnedRatherThanApplied) {
  fv::Settings s;
  s.Set("widget.width", "500");
  Widget w;
  std::vector<std::string> warnings;
  ASSERT_TRUE(w.LoadFrom(s, "widget.", &warnings).ok());
  EXPECT_EQ(w.GetInt("width", -1), 2);
  ASSERT_EQ(warnings.size(), 1u);
  EXPECT_NE(warnings[0].find("rejected"), std::string::npos);
}

TEST(Properties, ResetToDefaults) {
  Widget w;
  ASSERT_TRUE(w.SetProperty("width", fv::app::PropertyValue::Int(8)).ok());
  ASSERT_TRUE(w.ResetToDefaults().ok());
  EXPECT_EQ(w.GetInt("width", -1), 2);
}

TEST(Properties, TypedGettersRefuseTheWrongType) {
  Widget w;
  EXPECT_EQ(w.GetInt("colour", -1), -1);
  EXPECT_TRUE(w.GetBool("on", false));
  EXPECT_EQ(w.FindSpec("width")->max, 8);
  EXPECT_EQ(w.FindSpec("nope"), nullptr);
}

}  // namespace
