// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/feature/points_layer.h"
#include "fvkit/feature/value_text.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/overlay/point_edit.h"
#include "fvkit/overlay/point_overlay.h"

namespace {

using fv::Feature;
using fv::FieldDef;
using fv::FieldRole;
using fv::FieldType;
using fv::FieldValue;
using fv::GeoPoint;
using fv::Geometry;
using fv::PointsFeatureLayer;

FieldDef Def(FieldType type, FieldRole role = FieldRole::kNone) {
  FieldDef f;
  f.name = "f";
  f.title = "Field";
  f.type = type;
  f.role = role;
  return f;
}

FieldValue Parse(const FieldDef& f, const std::string& text) {
  FieldValue v;
  EXPECT_TRUE(fv::ParseFieldValue(f, text, &v).ok()) << text;
  return v;
}

bool Rejects(const FieldDef& f, const std::string& text) {
  FieldValue v;
  return !fv::ParseFieldValue(f, text, &v).ok();
}

TEST(FeatureValueText, ParsesEachStorageType) {
  EXPECT_TRUE(fv::IsNull(Parse(Def(FieldType::kInteger), "  ")));
  EXPECT_EQ(std::get<int64_t>(Parse(Def(FieldType::kInteger), " -42 ")), -42);
  EXPECT_TRUE(Rejects(Def(FieldType::kInteger), "4.5"));
  EXPECT_DOUBLE_EQ(std::get<double>(Parse(Def(FieldType::kReal), "2.75")), 2.75);
  EXPECT_TRUE(Rejects(Def(FieldType::kReal), "12 m"));
  EXPECT_TRUE(Rejects(Def(FieldType::kReal), "inf"));
  EXPECT_EQ(std::get<bool>(Parse(Def(FieldType::kBoolean), "Yes")), true);
  EXPECT_EQ(std::get<bool>(Parse(Def(FieldType::kBoolean), "0")), false);
  EXPECT_TRUE(Rejects(Def(FieldType::kBoolean), "maybe"));
  EXPECT_EQ(std::get<std::string>(Parse(Def(FieldType::kText), "  Fort Sumter ")), "Fort Sumter");
  // Multi-line text is kept as typed, leading and trailing lines included.
  EXPECT_EQ(std::get<std::string>(Parse(Def(FieldType::kText, FieldRole::kMultilineText), "a\nb\n")), "a\nb\n");
}

TEST(FeatureValueText, ChecksDatesAndTimes) {
  const FieldDef date = Def(FieldType::kDate);
  EXPECT_EQ(std::get<std::string>(Parse(date, "2024-02-29")), "2024-02-29");
  EXPECT_TRUE(Rejects(date, "2023-02-29"));
  EXPECT_TRUE(Rejects(date, "2024-13-01"));
  EXPECT_TRUE(Rejects(date, "1861-4-12"));
  const FieldDef when = Def(FieldType::kDateTime);
  EXPECT_EQ(std::get<std::string>(Parse(when, "1861-04-12 04:30")), "1861-04-12T04:30");
  EXPECT_EQ(std::get<std::string>(Parse(when, "2026-10-10T14:30:05.25Z")), "2026-10-10T14:30:05.25Z");
  EXPECT_EQ(std::get<std::string>(Parse(when, "2026-10-10T14:30-04:00")), "2026-10-10T14:30-04:00");
  EXPECT_TRUE(Rejects(when, "2026-10-10"));
  EXPECT_TRUE(Rejects(when, "2026-10-10T25:00"));
  EXPECT_TRUE(Rejects(when, "2026-10-10T14:30+4"));
}

TEST(FeatureValueText, EnumerationsUseLabelsAndValues) {
  FieldDef style = Def(FieldType::kText, FieldRole::kEnumeration);
  style.choices = {{"ny", "New York"}, {"detroit", ""}};
  EXPECT_EQ(fv::FormatFieldValue(style, std::string("ny")), "New York");
  EXPECT_EQ(fv::FormatFieldValue(style, std::string("detroit")), "detroit");
  EXPECT_EQ(fv::FormatFieldValue(style, std::string("chicago")), "chicago");
  EXPECT_EQ(std::get<std::string>(Parse(style, "New York")), "ny");
  EXPECT_EQ(std::get<std::string>(Parse(style, "ny")), "ny");
  FieldDef level = Def(FieldType::kInteger, FieldRole::kEnumeration);
  level.choices = {{"1", "Low"}, {"2", "High"}};
  EXPECT_EQ(std::get<int64_t>(Parse(level, "High")), 2);
  EXPECT_EQ(fv::FormatFieldValue(level, int64_t{1}), "Low");
}

TEST(FeatureValueText, FormatsAndActions) {
  EXPECT_EQ(fv::FormatFieldValue(Def(FieldType::kReal), 1.50), "1.5");
  EXPECT_EQ(fv::FormatFieldValue(Def(FieldType::kBoolean), true), "Yes");
  EXPECT_EQ(fv::FormatFieldValue(Def(FieldType::kText), FieldValue{}), "");
  EXPECT_EQ(fv::FieldActionUrl(Def(FieldType::kText, FieldRole::kPhone), "+1 (843) 555-0100"), "tel:+18435550100");
  EXPECT_EQ(fv::FieldActionUrl(Def(FieldType::kText, FieldRole::kPhone), "none"), "");
  EXPECT_EQ(fv::FieldActionUrl(Def(FieldType::kText, FieldRole::kEmail), "a@b.org"), "mailto:a@b.org");
  EXPECT_EQ(fv::FieldActionUrl(Def(FieldType::kText, FieldRole::kUrl), "http://x.org"), "http://x.org");
  EXPECT_EQ(fv::FieldActionUrl(Def(FieldType::kText, FieldRole::kUrl), "x.org"), "https://x.org");
  EXPECT_EQ(fv::FieldActionUrl(Def(FieldType::kText), "x.org"), "");
}

/// Two points, added in reverse id order of their draw position.
std::shared_ptr<fv::PointOverlay> TwoPoints() {
  auto o = std::make_shared<fv::PointOverlay>("Harbour");
  fv::MapPoint a;
  a.id = 7;
  a.name = "Fort Sumter";
  a.position = {32.7523, -79.8747};
  a.shape = fv::PointShape::kStar;
  a.elevation_ft = 12;
  fv::MapPoint b;
  b.id = 3;
  b.name = "Castle Pinckney";
  b.position = {32.7731, -79.9103};
  o->SetPoints({a, b});
  o->set_dirty(false);
  return o;
}

TEST(PointsFeatureLayer, ReadsPointsInIdOrder) {
  PointsFeatureLayer layer(TwoPoints());
  EXPECT_EQ(layer.Name(), "Harbour");
  EXPECT_EQ(layer.Count(), 2u);
  EXPECT_TRUE(layer.Schema().Validate().ok());
  std::vector<Feature> all;
  ASSERT_TRUE(layer.All(&all).ok());
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].id, 3);
  EXPECT_EQ(std::get<std::string>(all[1].values[PointsFeatureLayer::kName]), "Fort Sumter");
  EXPECT_DOUBLE_EQ(std::get<double>(all[1].values[PointsFeatureLayer::kElevation]), 12);
  std::vector<Feature> near_sumter;
  ASSERT_TRUE(layer.Query(fv::GeoRect{{32.74, -79.89}, {32.76, -79.86}}, &near_sumter).ok());
  ASSERT_EQ(near_sumter.size(), 1u);
  EXPECT_EQ(near_sumter[0].id, 7);
  fv::GeoRect box;
  ASSERT_TRUE(layer.Extent(&box).ok());
  EXPECT_DOUBLE_EQ(box.ll.lon, -79.9103);
  EXPECT_FALSE(layer.Get(99, &all[0]).ok());
}

TEST(PointsFeatureLayer, EditsGoThroughTheEditSession) {
  auto points = TwoPoints();
  PointsFeatureLayer layer(points);
  Feature f;
  ASSERT_TRUE(layer.Get(7, &f).ok());
  f.values[PointsFeatureLayer::kPhone] = std::string("843-883-3123");
  f.values[PointsFeatureLayer::kElevation] = FieldValue{};
  f.geometry = Geometry();  // null: the point stays where it is
  ASSERT_TRUE(layer.Update(f).ok());
  const fv::MapPoint* p = points->Find(7);
  EXPECT_EQ(p->phone, "843-883-3123");
  EXPECT_DOUBLE_EQ(p->elevation_ft, 0);
  EXPECT_EQ(p->shape, fv::PointShape::kStar);
  EXPECT_DOUBLE_EQ(p->position.lat, 32.7523);
  EXPECT_TRUE(points->is_dirty());
  ASSERT_TRUE(points->edit().CanUndo());
  points->edit().Undo();
  EXPECT_EQ(points->Find(7)->phone, "");

  Feature added;
  added.geometry = Geometry::Point(GeoPoint{32.78, -79.93});
  added.values = {std::string("Battery")};
  ASSERT_TRUE(layer.Insert(&added).ok());
  EXPECT_NE(added.id, 0);
  EXPECT_EQ(points->selected(), added.id);
  EXPECT_EQ(points->Find(added.id)->name, "Battery");

  Feature no_point;
  no_point.values = {std::string("Nowhere")};
  EXPECT_FALSE(layer.Insert(&no_point).ok());
  f.values.push_back(std::string("extra"));
  EXPECT_FALSE(layer.Update(f).ok());
  EXPECT_TRUE(layer.Delete(3).ok());
  EXPECT_FALSE(layer.Delete(3).ok());
  EXPECT_EQ(layer.Count(), 2u);
}

TEST(FeatureOverlaySelection, TheSelectedFeatureHasAHalo) {
  fv::FeatureSchema s;
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("one", s);
  Feature f;
  f.geometry = Geometry::Point(GeoPoint{32.86, -79.95});
  ASSERT_TRUE(layer->Insert(&f).ok());
  fv::FeatureOverlay overlay;
  overlay.SetLayer(layer);
  fv::MapProjection proj;
  proj.SetSurfaceSize(100, 100);
  proj.SetCenter({32.86, -79.95});
  proj.SetScale(250000.0);
  auto yellow = [](const fv::CpuCanvas& c) {
    int n = 0;
    const fv::PixelBuffer& b = c.Buffer();
    for (int y = 0; y < b.Height(); ++y)
      for (int x = 0; x < b.Width(); ++x) {
        const unsigned char* p = b.Row(y) + 4 * x;
        if (p[0] > 230 && p[1] > 180 && p[1] < 220 && p[2] < 40) ++n;
      }
    return n;
  };
  fv::CpuCanvas plain(100, 100);
  plain.Clear({255, 255, 255, 255});
  ASSERT_TRUE(overlay.OnDraw(proj, plain).ok());
  EXPECT_EQ(yellow(plain), 0);
  overlay.SetSelected(f.id);
  fv::CpuCanvas selected(100, 100);
  selected.Clear({255, 255, 255, 255});
  ASSERT_TRUE(overlay.OnDraw(proj, selected).ok());
  EXPECT_GT(yellow(selected), 20);
  // A new layer clears the selection.
  overlay.SetLayer(layer);
  EXPECT_EQ(overlay.selected(), 0);
}

}  // namespace
