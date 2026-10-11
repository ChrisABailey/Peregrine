// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <string>

#include "fv_desk_fake.h"
#include "fv_desk_point_editor.h"
#include "fvkit/feature/geojson.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/overlay/point_edit.h"
#include "fvkit/overlay/point_overlay.h"
#include "scratch.h"

namespace {

using fv::Feature;
using fv::FeatureOverlay;
using fv::FieldDef;
using fv::FieldRole;
using fv::FieldType;
using fv::GeoPoint;
using fv::Geometry;
using fv::PointOverlay;
using fv::desk::Desk;
using fv::desk::FakeDesk;
using fv::desk::FeatureFormModel;
using fv::desk::FormControl;

/// Pizza restaurants with a full schema: a style pick-list, a phone, a rating
/// from 0 to 5 and an opening date range.
std::shared_ptr<fv::MemoryFeatureLayer> PizzaLayer() {
  fv::FeatureSchema s;
  s.geometry_type = fv::GeometryType::kPoint;
  FieldDef name;
  name.name = "name";
  name.title = "Name";
  name.required = true;
  FieldDef phone;
  phone.name = "phone";
  phone.title = "Phone";
  phone.role = FieldRole::kPhone;
  FieldDef style;
  style.name = "style";
  style.title = "Style";
  style.role = FieldRole::kEnumeration;
  style.choices = {{"ny", "New York"}, {"neapolitan", "Neapolitan"}, {"detroit", "Detroit"}};
  FieldDef rating;
  rating.name = "rating";
  rating.title = "Rating";
  rating.type = FieldType::kReal;
  rating.min = 0;
  rating.max = 5;
  FieldDef from;
  from.name = "open_from";
  from.title = "Open";
  from.type = FieldType::kDate;
  from.range_end = "open_to";
  FieldDef to;
  to.name = "open_to";
  to.type = FieldType::kDate;
  s.fields = {name, phone, style, rating, from, to};
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("Pizza", s);
  Feature a;
  a.geometry = Geometry::Point(GeoPoint{32.78, -79.93});
  a.values = {std::string("Andolini's"), std::string("(843) 722-7437"), std::string("ny"), 4.0,
              std::string("1996-05-01"), fv::FieldValue{}};
  Feature b;
  b.geometry = Geometry::Point(GeoPoint{32.80, -79.95});
  b.values = {std::string("Paulie Gee's"), std::string("843 555 0100"), std::string("neapolitan"), 4.5,
              std::string("2019-03-15"), fv::FieldValue{}};
  Feature c;
  c.geometry = Geometry::Point(GeoPoint{32.79, -79.94});
  c.values = {std::string("Slice Box"), fv::FieldValue{}, std::string("detroit"), 3.0, fv::FieldValue{},
              fv::FieldValue{}};
  layer->Insert(&a);
  layer->Insert(&b);
  layer->Insert(&c);
  return layer;
}

/// The index of the form control titled `title`, or -1.
int ControlNamed(const FeatureFormModel& form, const std::string& title) {
  for (size_t i = 0; i < form.fields().size(); ++i)
    if (form.fields()[i].title == title) return static_cast<int>(i);
  return -1;
}

/// A FakeDesk with the pizza layer open from a GeoJSON file, current.
class FeatureFormTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(fv::WriteGeoJsonFile(*PizzaLayer(), path_).ok());
    fd_.shell().files_to_open = {path_};
    ASSERT_TRUE(fd_.Run({"overlay.open"}).ok());
    ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
    auto found = desk().overlays().OfType(FeatureOverlay::kTypeId);
    ASSERT_EQ(found.size(), 1u);
    overlay_ = std::static_pointer_cast<FeatureOverlay>(found[0]);
    ASSERT_EQ(desk().overlays().current(), overlay_.get());
  }
  Desk& desk() { return fd_.desk(); }
  fv::FieldValue Value(int64_t id, const std::string& field) {
    Feature f;
    EXPECT_TRUE(overlay_->layer()->Get(id, &f).ok());
    return f.values[overlay_->layer()->Schema().Find(field)];
  }

  fv::desk::test::Scratch scratch_;
  std::string path_ = scratch_.Path("pizza.geojson");
  FakeDesk fd_{400, 400};
  std::shared_ptr<FeatureOverlay> overlay_;
};

TEST_F(FeatureFormTest, EditFeatureNeedsASelection) {
  EXPECT_FALSE(desk().commands().IsEnabled("overlay.edit_feature"));
  EXPECT_TRUE(desk().commands().IsEnabled("overlay.attribute_table"));
  overlay_->SetSelected(2);
  EXPECT_TRUE(desk().commands().IsEnabled("overlay.edit_feature"));
}

TEST_F(FeatureFormTest, ControlsFollowTheSchema) {
  overlay_->SetSelected(1);
  ASSERT_TRUE(fd_.Run({"overlay.edit_feature"}).ok());
  EXPECT_EQ(fd_.shell().feature_form_shown, 1);
  const auto& form = desk().feature_form();
  ASSERT_NE(form, nullptr);
  EXPECT_EQ(form->title(), "Edit Feature — Andolini's");
  // The range's end field has no control of its own.
  ASSERT_EQ(form->fields().size(), 5u);
  const auto& f = form->fields();
  EXPECT_EQ(f[0].control, FormControl::kText);
  EXPECT_TRUE(f[0].required);
  EXPECT_EQ(f[1].control, FormControl::kPhone);
  EXPECT_EQ(f[1].text, "(843) 722-7437");
  EXPECT_EQ(form->ActionUrl(1), "tel:8437227437");
  EXPECT_EQ(f[2].control, FormControl::kChoice);
  EXPECT_EQ(f[2].text, "New York");  // the label, not the stored "ny"
  EXPECT_EQ(f[2].choices, (std::vector<std::string>{"New York", "Neapolitan", "Detroit"}));
  EXPECT_EQ(f[3].control, FormControl::kNumber);
  EXPECT_EQ(f[3].text, "4");
  EXPECT_EQ(f[4].control, FormControl::kDateRange);
  EXPECT_EQ(f[4].text, "1996-05-01");
  EXPECT_EQ(f[4].end_text, "");
}

TEST_F(FeatureFormTest, EditsAPizzaRestaurantsPhoneAndStyle) {
  overlay_->SetSelected(2);
  ASSERT_TRUE(fd_.Run({"overlay.edit_feature"}).ok());
  auto form = desk().feature_form();
  ASSERT_NE(form, nullptr);
  const int phone = ControlNamed(*form, "Phone");
  const int style = ControlNamed(*form, "Style");
  ASSERT_TRUE(form->SetText(phone, "843-555-0199").ok());
  ASSERT_TRUE(form->SetText(style, "Detroit").ok());
  EXPECT_TRUE(form->dirty());
  ASSERT_TRUE(desk().ApplyFeatureForm().ok());
  EXPECT_FALSE(form->dirty());
  EXPECT_EQ(std::get<std::string>(Value(2, "phone")), "843-555-0199");
  EXPECT_EQ(std::get<std::string>(Value(2, "style")), "detroit");  // stored as the value
  EXPECT_TRUE(overlay_->AsPersistence()->is_dirty());

  // Saved and reopened, the edit is in the file.
  const std::string gpkg = scratch_.Path("pizza.gpkg");
  ASSERT_TRUE(overlay_->FileSaveAs(gpkg, FeatureOverlay::kFormatGeoPackage).ok());
  FeatureOverlay reopened;
  ASSERT_TRUE(reopened.FileOpen(gpkg).ok());
  Feature f;
  ASSERT_TRUE(reopened.layer()->Get(2, &f).ok());
  EXPECT_EQ(std::get<std::string>(f.values[1]), "843-555-0199");
  EXPECT_EQ(std::get<std::string>(f.values[2]), "detroit");
}

TEST_F(FeatureFormTest, BadValuesAreReportedAndNothingIsWritten) {
  overlay_->SetSelected(1);
  ASSERT_TRUE(fd_.Run({"overlay.edit_feature"}).ok());
  auto form = desk().feature_form();
  const int phone = ControlNamed(*form, "Phone");
  const int rating = ControlNamed(*form, "Rating");
  const int open = ControlNamed(*form, "Open");
  ASSERT_TRUE(form->SetText(phone, "555 0000").ok());
  ASSERT_TRUE(form->SetText(rating, "9").ok());
  ASSERT_TRUE(form->SetEndText(open, "1990-01-01").ok());
  const fv::Status s = desk().ApplyFeatureForm();
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(form->fields()[rating].error, "\"Rating\" is above its maximum");
  EXPECT_EQ(form->fields()[open].error, "\"Open\" ends before it starts");
  EXPECT_TRUE(form->fields()[phone].error.empty());
  EXPECT_EQ(std::get<std::string>(Value(1, "phone")), "(843) 722-7437");
  EXPECT_FALSE(overlay_->AsPersistence()->is_dirty());

  ASSERT_TRUE(form->SetText(rating, "abc").ok());
  EXPECT_FALSE(form->Validate().ok());
  EXPECT_EQ(form->fields()[rating].error, "\"Rating\" needs a number");
  ASSERT_TRUE(form->SetText(0, "").ok());
  EXPECT_FALSE(form->Validate().ok());
  EXPECT_EQ(form->fields()[0].error, "\"Name\" is required");

  ASSERT_TRUE(form->Revert().ok());
  EXPECT_FALSE(form->dirty());
  EXPECT_TRUE(form->Validate().ok());
}

TEST_F(FeatureFormTest, TableSortsFiltersAndSelects) {
  ASSERT_TRUE(fd_.Run({"overlay.attribute_table"}).ok());
  EXPECT_EQ(fd_.shell().attribute_table_shown, 1);
  auto table = desk().attribute_table();
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->title(), "Pizza");
  ASSERT_EQ(table->columns().size(), 6u);
  EXPECT_EQ(table->columns()[3].title, "Rating");
  EXPECT_TRUE(table->columns()[3].numeric);
  EXPECT_FALSE(table->columns()[2].numeric);
  ASSERT_EQ(table->RowCount(), 3u);
  EXPECT_EQ(table->Cell(0, 0), "Andolini's");
  EXPECT_EQ(table->Cell(1, 2), "Neapolitan");

  table->Sort(3, false);  // rating, highest first
  EXPECT_EQ(table->Cell(0, 0), "Paulie Gee's");
  EXPECT_EQ(table->Cell(2, 0), "Slice Box");
  table->Sort(1, true);  // phone: the empty one last
  EXPECT_EQ(table->Cell(2, 0), "Slice Box");
  table->Sort(1, false);
  EXPECT_EQ(table->Cell(2, 0), "Slice Box");

  table->SetFilter("NEAP");
  ASSERT_EQ(table->RowCount(), 1u);
  EXPECT_EQ(table->FeatureAt(0), 2);
  table->SetFilter("");
  table->Sort(-1, true);

  ASSERT_TRUE(desk().SelectTableRow(2).ok());
  EXPECT_EQ(overlay_->selected(), 3);
  EXPECT_EQ(desk().SelectedTableRow(), 2);
  EXPECT_FALSE(desk().SelectTableRow(7).ok());

  // A form opened from the table updates the table when applied.
  ASSERT_TRUE(desk().EditTableRow(0).ok());
  auto form = desk().feature_form();
  ASSERT_NE(form, nullptr);
  EXPECT_EQ(overlay_->selected(), 1);
  ASSERT_TRUE(form->SetText(ControlNamed(*form, "Name"), "Andolini's Pizzeria").ok());
  ASSERT_TRUE(desk().ApplyFeatureForm().ok());
  EXPECT_EQ(table->Cell(0, 0), "Andolini's Pizzeria");
}

TEST_F(FeatureFormTest, ClosingTheOverlayClosesItsViews) {
  overlay_->SetSelected(1);
  ASSERT_TRUE(fd_.Run({"overlay.edit_feature", "overlay.attribute_table"}).ok());
  ASSERT_NE(desk().feature_form(), nullptr);
  const uint64_t gen = desk().FeatureViewsGeneration();
  ASSERT_TRUE(fd_.Run({"file.close"}).ok());
  EXPECT_EQ(desk().feature_form(), nullptr);
  EXPECT_EQ(desk().attribute_table(), nullptr);
  EXPECT_GT(desk().FeatureViewsGeneration(), gen);
}

TEST_F(FeatureFormTest, SearchFindsFeaturesByTitle) {
  fv::app::SearchSession search(desk().overlays());
  fv::app::SearchQuery q;
  q.text = "paul";
  auto results = search.Search(q);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].title, "Paulie Gee's");
  EXPECT_EQ(results[0].detail, "feature \xc2\xb7 Pizza");
  EXPECT_EQ(results[0].feature, 2u);
  EXPECT_EQ(results[0].overlay, overlay_.get());
  EXPECT_DOUBLE_EQ(results[0].position.lat, 32.80);
}

/// The Edit Point dialog is the feature form over a `.fvpoints` file.
class EditPointTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(PointOverlay::WriteSampleFile(path_).ok());
    fd_.shell().files_to_open = {path_};
    ASSERT_TRUE(fd_.Run({"overlay.open"}).ok());
    ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
    auto found = fd_.desk().overlays().OfType(PointOverlay::kTypeId);
    ASSERT_EQ(found.size(), 1u);
    points_ = std::static_pointer_cast<PointOverlay>(found[0]);
    ASSERT_FALSE(points_->points().empty());
    target_ = points_->points().front();
  }

  fv::desk::test::Scratch scratch_;
  std::string path_ = scratch_.Path("sample.fvpoints");
  FakeDesk fd_{400, 400};
  std::shared_ptr<PointOverlay> points_;
  fv::MapPoint target_;
};

TEST_F(EditPointTest, TheFormEditsAPointAndUndoRestoresIt) {
  points_->SetSelected(target_.id);
  ASSERT_TRUE(fd_.Run({"overlay.edit_feature"}).ok());
  auto form = fd_.desk().feature_form();
  ASSERT_NE(form, nullptr);
  EXPECT_EQ(form->title(), "Edit Point — " + target_.name);
  ASSERT_EQ(form->fields().size(), 6u);
  EXPECT_EQ(form->fields()[0].text, target_.name);
  EXPECT_EQ(form->fields()[2].control, FormControl::kNumber);
  EXPECT_EQ(form->fields()[2].unit, "ft");
  EXPECT_EQ(form->fields()[3].control, FormControl::kMultiline);
  EXPECT_EQ(form->fields()[4].control, FormControl::kPhone);
  EXPECT_EQ(form->fields()[5].control, FormControl::kUrl);

  ASSERT_TRUE(form->SetText(3, "Gate code 1234\nRing twice").ok());
  ASSERT_TRUE(form->SetText(4, "(843) 555 0100").ok());
  ASSERT_TRUE(form->SetText(5, "kiawahresort.com").ok());
  EXPECT_EQ(form->ActionUrl(5), "https://kiawahresort.com");
  ASSERT_TRUE(fd_.desk().ApplyFeatureForm().ok());

  const fv::MapPoint* p = points_->Find(target_.id);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->remarks, "Gate code 1234\nRing twice");
  EXPECT_EQ(p->phone, "(843) 555 0100");
  EXPECT_EQ(p->url, "kiawahresort.com");
  // What the form does not show is kept.
  EXPECT_EQ(p->shape, target_.shape);
  EXPECT_EQ(p->symbol_id, target_.symbol_id);
  EXPECT_DOUBLE_EQ(p->position.lat, target_.position.lat);
  EXPECT_TRUE(points_->is_dirty());

  ASSERT_TRUE(points_->edit().CanUndo());
  points_->edit().Undo();
  EXPECT_EQ(points_->Find(target_.id)->remarks, target_.remarks);
}

TEST_F(EditPointTest, ThePointEditorOffersEditPoint) {
  int opened = 0;
  fv::desk::PointEditor editor([this] { return points_.get(); }, [&opened] { ++opened; });
  auto find = [&](const std::vector<fv::app::MenuNode>& tools) -> const fv::app::MenuNode* {
    for (const auto& t : tools)
      if (t.icon == "edit_point") return &t;
    return nullptr;
  };
  auto tools = editor.Tools();
  ASSERT_NE(find(tools), nullptr);
  EXPECT_FALSE(find(tools)->enabled);
  points_->SetSelected(target_.id);
  tools = editor.Tools();
  ASSERT_TRUE(find(tools)->enabled);
  find(tools)->action();
  EXPECT_EQ(opened, 1);
}

TEST_F(EditPointTest, TheTableListsThePoints) {
  ASSERT_TRUE(fd_.Run({"overlay.attribute_table"}).ok());
  auto table = fd_.desk().attribute_table();
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->RowCount(), points_->points().size());
  EXPECT_EQ(table->columns()[2].title, "Elevation (ft)");
  ASSERT_TRUE(fd_.desk().SelectTableRow(0).ok());
  EXPECT_EQ(points_->selected(), table->FeatureAt(0));
}

}  // namespace
