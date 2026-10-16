// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "fv_desk_fake.h"
#include "fv_desk_layer_fields.h"
#include "fvkit/app/source_spec.h"
#include "fvkit/feature/geopackage.h"
#include "fvkit/feature/geojson.h"
#include "fvkit/feature/templates.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/raster.h"
#include "scratch.h"

namespace {

using fv::FeatureOverlay;
using fv::FieldDef;
using fv::FieldType;
using fv::desk::Desk;
using fv::desk::FakeDesk;
using fv::desk::LayerFieldsModel;
using fv::desk::NewLayerModel;

/// name, style (pick-list), rating; three restaurants.
std::shared_ptr<fv::MemoryFeatureLayer> PizzaLayer() {
  fv::FeatureSchema s;
  s.geometry_type = fv::GeometryType::kPoint;
  FieldDef name;
  name.name = "name";
  name.title = "Name";
  FieldDef style;
  style.name = "style";
  style.title = "Style";
  style.role = fv::FieldRole::kEnumeration;
  style.choices = {{"ny", "New York"}, {"neapolitan", "Neapolitan"}, {"detroit", "Detroit"}};
  FieldDef rating;
  rating.name = "rating";
  rating.type = FieldType::kReal;
  s.fields = {name, style, rating};
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("Pizza", s);
  const struct {
    double lat, lon;
    const char* name;
    const char* style;
    double rating;
  } rows[] = {{32.78, -79.93, "Andolini's", "ny", 4.0},
              {32.80, -79.95, "Paulie Gee's", "neapolitan", 4.5},
              {32.79, -79.94, "Slice Box", "detroit", 3.0}};
  for (const auto& r : rows) {
    fv::Feature f;
    f.geometry = fv::Geometry::Point({r.lat, r.lon});
    f.values = {std::string(r.name), std::string(r.style), r.rating};
    layer->Insert(&f);
  }
  return layer;
}

/// A FakeDesk with the pizza layer saved as a GeoPackage and open.
class LayerFieldsTest : public ::testing::Test {
 protected:
  void Open(const std::string& path) {
    fd_.shell().files_to_open = {path};
    ASSERT_TRUE(fd_.Run({"overlay.open"}).ok());
    ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
    auto found = desk().overlays().OfType(FeatureOverlay::kTypeId);
    ASSERT_FALSE(found.empty());
    overlay_ = std::static_pointer_cast<FeatureOverlay>(found.back());
  }
  void OpenPizzaGpkg() {
    ASSERT_TRUE(fv::WriteGeoJsonFile(*PizzaLayer(), scratch_.Path("pizza.geojson")).ok());
    Open(scratch_.Path("pizza.geojson"));
    fd_.shell().save_spec = scratch_.Path("pizza.gpkg");
    fd_.shell().save_format = FeatureOverlay::kFormatGeoPackage;
    ASSERT_TRUE(fd_.Run({"file.save_as"}).ok());
    ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  }
  void CloseAndReopen(const std::string& path) {
    ASSERT_TRUE(fd_.Run({"file.close"}).ok());
    overlay_.reset();
    Open(path);
  }
  Desk& desk() { return fd_.desk(); }
  LayerFieldsModel& fields() { return *desk().layer_fields(); }

  fv::desk::test::Scratch scratch_;
  FakeDesk fd_{400, 400};
  std::shared_ptr<FeatureOverlay> overlay_;
};

TEST_F(LayerFieldsTest, AddRenameDeleteSavesToTheGeoPackage) {
  EXPECT_FALSE(desk().commands().IsEnabled("overlay.fields"));
  OpenPizzaGpkg();
  ASSERT_TRUE(desk().commands().IsEnabled("overlay.fields"));
  ASSERT_TRUE(fd_.Run({"overlay.fields"}).ok());
  EXPECT_EQ(fd_.shell().layer_fields_shown, 1);
  ASSERT_NE(desk().layer_fields(), nullptr);
  EXPECT_EQ(fields().title(), "Layer Fields — Pizza");
  ASSERT_EQ(fields().FieldCount(), 3u);
  EXPECT_FALSE(fields().dirty());

  // Style by rating and label by name, so the change has to follow both.
  fv::FeatureStyle style;
  style.renderer = fv::FeatureRenderer::kGraduated;
  style.field = "rating";
  style.label.field = "name";
  overlay_->SetStyle(style);

  const size_t phone = fields().AddField();
  EXPECT_EQ(fields().Field(phone).name, "field");
  EXPECT_EQ(fields().OriginalName(phone), "");
  FieldDef p = fields().Field(phone);
  p.name = "phone";
  p.role = fv::FieldRole::kPhone;
  p.title = "Phone";
  ASSERT_TRUE(fields().SetField(phone, p).ok());
  FieldDef r = fields().Field(2);
  r.name = "stars";
  ASSERT_TRUE(fields().SetField(2, r).ok());
  FieldDef clash = fields().Field(2);
  clash.name = "NAME";
  EXPECT_EQ(fields().SetField(2, clash).code, fv::kInvalidArg);
  ASSERT_TRUE(fields().MoveField(phone, 1).ok());
  ASSERT_TRUE(fields().RemoveField(2).ok());  // style
  EXPECT_TRUE(fields().dirty());
  EXPECT_EQ(fields().OriginalName(2), "rating");

  const std::string loss = desk().LayerFieldsLoss();
  EXPECT_NE(loss.find("\"style\""), std::string::npos) << loss;
  ASSERT_TRUE(desk().ApplyLayerFields().ok());
  EXPECT_FALSE(fields().dirty());
  EXPECT_TRUE(overlay_->is_dirty());
  EXPECT_EQ(overlay_->style().field, "stars");
  EXPECT_EQ(overlay_->style().renderer, fv::FeatureRenderer::kGraduated);
  EXPECT_EQ(overlay_->style().label.field, "name");
  EXPECT_EQ(desk().LayerFieldsLoss(), "");

  ASSERT_TRUE(fd_.Run({"file.save"}).ok());
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  CloseAndReopen(scratch_.Path("pizza.gpkg"));
  EXPECT_EQ(desk().layer_fields(), nullptr);  // closed with its overlay
  const fv::FeatureSchema& schema = overlay_->layer()->Schema();
  ASSERT_EQ(schema.fields.size(), 3u);
  EXPECT_EQ(schema.fields[0].name, "name");
  EXPECT_EQ(schema.fields[0].title, "Name");
  EXPECT_EQ(schema.fields[1].name, "phone");
  EXPECT_EQ(schema.fields[1].role, fv::FieldRole::kPhone);
  EXPECT_EQ(schema.fields[2].name, "stars");
  fv::Feature f;
  ASSERT_TRUE(overlay_->layer()->Get(2, &f).ok());
  EXPECT_EQ(std::get<std::string>(f.values[0]), "Paulie Gee's");
  EXPECT_EQ(std::get<double>(f.values[2]), 4.5);
  EXPECT_EQ(overlay_->style().field, "stars");
}

TEST_F(LayerFieldsTest, ApplyClosesTheFormAndStyleAndReloadsTheTable) {
  OpenPizzaGpkg();
  overlay_->SetSelected(1);
  ASSERT_TRUE(fd_.Run({"overlay.attribute_table", "overlay.edit_feature", "overlay.style", "overlay.fields"}).ok());
  ASSERT_NE(desk().feature_form(), nullptr);
  ASSERT_NE(desk().layer_style(), nullptr);
  ASSERT_NE(desk().attribute_table(), nullptr);
  desk().attribute_table()->Sort(2, true);
  ASSERT_TRUE(fields().RemoveField(2).ok());
  const uint64_t gen = desk().FeatureViewsGeneration();
  ASSERT_TRUE(desk().ApplyLayerFields().ok());
  EXPECT_NE(desk().FeatureViewsGeneration(), gen);
  EXPECT_EQ(desk().feature_form(), nullptr);
  EXPECT_EQ(desk().layer_style(), nullptr);
  ASSERT_NE(desk().attribute_table(), nullptr);
  EXPECT_EQ(desk().attribute_table()->columns().size(), 2u);
  EXPECT_EQ(desk().attribute_table()->sort_column(), -1);
  EXPECT_EQ(desk().attribute_table()->RowCount(), 3u);
}

TEST_F(LayerFieldsTest, RetypeAndRequiredField) {
  OpenPizzaGpkg();
  ASSERT_TRUE(fd_.Run({"overlay.fields"}).ok());
  FieldDef rating = fields().Field(2);
  rating.type = FieldType::kInteger;
  rating.default_value = 2.5;
  ASSERT_TRUE(fields().SetField(2, rating).ok());
  // 2.5 rounds to a whole number as the default converts.
  EXPECT_EQ(std::get<int64_t>(fields().Field(2).default_value), 3);
  fv::SchemaChangeImpact impact;
  ASSERT_TRUE(fields().Assess(&impact).ok());
  ASSERT_EQ(impact.converted.size(), 1u);  // 4.5 rounds
  EXPECT_EQ(impact.converted[0].second, 1u);

  const size_t owner = fields().AddField();
  FieldDef o = fields().Field(owner);
  o.name = "owner";
  o.required = true;
  ASSERT_TRUE(fields().SetField(owner, o).ok());
  EXPECT_FALSE(desk().ApplyLayerFields().ok());
  EXPECT_EQ(overlay_->layer()->Schema().fields.size(), 3u);
  o.default_value = std::string("unknown");
  ASSERT_TRUE(fields().SetField(owner, o).ok());
  ASSERT_TRUE(desk().ApplyLayerFields().ok());
  fv::Feature f;
  ASSERT_TRUE(overlay_->layer()->Get(2, &f).ok());
  EXPECT_EQ(std::get<int64_t>(f.values[2]), 5);
  EXPECT_EQ(std::get<std::string>(f.values[3]), "unknown");

  // Revert of the overlay throws away the uncommitted change.
  ASSERT_TRUE(overlay_->Revert(scratch_.Path("pizza.gpkg")).ok());
  EXPECT_EQ(overlay_->layer()->Schema().fields.size(), 3u);
  EXPECT_EQ(fields().Apply().code, fv::kInvalidArg);  // stale draft
  ASSERT_TRUE(fields().Revert().ok());
  EXPECT_EQ(fields().FieldCount(), 3u);
}

TEST_F(LayerFieldsTest, RenameFollowsADateRange) {
  OpenPizzaGpkg();
  ASSERT_TRUE(fd_.Run({"overlay.fields"}).ok());
  const size_t from = fields().AddField();
  const size_t to = fields().AddField();
  FieldDef end = fields().Field(to);
  end.name = "closed";
  end.type = FieldType::kDate;
  ASSERT_TRUE(fields().SetField(to, end).ok());
  FieldDef start = fields().Field(from);
  start.name = "opened";
  start.type = FieldType::kDate;
  start.range_end = "closed";
  ASSERT_TRUE(fields().SetField(from, start).ok());
  end.name = "shut";
  ASSERT_TRUE(fields().SetField(to, end).ok());
  EXPECT_EQ(fields().Field(from).range_end, "shut");
  ASSERT_TRUE(fields().Validate().ok());
  ASSERT_TRUE(fields().RemoveField(to).ok());
  EXPECT_EQ(fields().Field(from).range_end, "");
}

TEST_F(LayerFieldsTest, WarnsWhenAChangeBreaksTheStyle) {
  OpenPizzaGpkg();
  ASSERT_TRUE(fd_.Run({"overlay.fields"}).ok());

  // A graduated style follows a renamed field, so renaming it breaks nothing.
  fv::FeatureStyle graduated;
  graduated.renderer = fv::FeatureRenderer::kGraduated;
  graduated.field = "rating";
  overlay_->SetStyle(graduated);
  FieldDef r = fields().Field(2);
  r.name = "stars";
  ASSERT_TRUE(fields().SetField(2, r).ok());
  EXPECT_EQ(desk().LayerFieldsLoss(), "");

  // Rules name fields in their expressions, which a rename does not reach.
  fv::FeatureStyle rules;
  rules.renderer = fv::FeatureRenderer::kRules;
  fv::StyleRule top;
  top.id = "Top rated";
  top.type = "circle";
  top.filter = R"([">=", ["get", "rating"], 4])";
  fv::StyleRule labels;
  labels.id = "Names";
  labels.type = "symbol";
  labels.layout = R"({"text-field": ["get", "name"]})";
  rules.rules = {top, labels};
  overlay_->SetStyle(rules);
  std::string loss = desk().LayerFieldsLoss();
  EXPECT_NE(loss.find("Style rule \"Top rated\" uses the field \"rating\", which is renamed to \"stars\""),
            std::string::npos)
      << loss;
  EXPECT_EQ(loss.find("Names"), std::string::npos) << loss;

  ASSERT_TRUE(fields().RemoveField(0).ok());  // name
  loss = desk().LayerFieldsLoss();
  EXPECT_NE(loss.find("Style rule \"Names\" uses the field \"name\", which is deleted"), std::string::npos)
      << loss;

  // The warning does not stop the change, and the rules are left as they were.
  ASSERT_TRUE(desk().ApplyLayerFields().ok());
  EXPECT_EQ(overlay_->style().rules[0].filter, top.filter);
}

TEST(LayerFieldsChoices, ParseAndFormat) {
  std::vector<fv::FieldChoice> choices;
  ASSERT_TRUE(fv::desk::ParseChoices("ny = New York\n\n  detroit \nneapolitan=Neapolitan", &choices).ok());
  ASSERT_EQ(choices.size(), 3u);
  EXPECT_EQ(choices[0].value, "ny");
  EXPECT_EQ(choices[0].label, "New York");
  EXPECT_EQ(choices[1].value, "detroit");
  EXPECT_EQ(choices[1].label, "");
  EXPECT_EQ(fv::desk::FormatChoices(choices), "ny = New York\ndetroit\nneapolitan = Neapolitan");
  EXPECT_EQ(fv::desk::ParseChoices("a\na = A", &choices).code, fv::kInvalidArg);
}

TEST(NewLayer, TemplatePathAndFormat) {
  NewLayerModel m;
  EXPECT_EQ(m.TemplateTitle(0), "Blank");
  EXPECT_EQ(m.name(), "New Layer");
  EXPECT_EQ(m.geometry(), fv::GeometryType::kPoint);
  ASSERT_TRUE(m.SetTemplate(3).ok());  // rivers
  EXPECT_EQ(m.name(), "Rivers");
  EXPECT_EQ(m.geometry(), fv::GeometryType::kLineString);
  m.SetName("Ashley River");
  ASSERT_TRUE(m.SetTemplate(1).ok());
  EXPECT_EQ(m.name(), "Ashley River");  // set by hand, kept

  EXPECT_EQ(m.Validate().code, fv::kInvalidArg);  // no path
  m.SetPath("/tmp/x/monuments");
  EXPECT_EQ(m.path(), "/tmp/x/monuments.gpkg");
  ASSERT_TRUE(m.SetFormat(FeatureOverlay::kFormatGeoJson).ok());
  EXPECT_EQ(m.path(), "/tmp/x/monuments.geojson");
  m.SetPath("/tmp/x/a.GPKG");
  EXPECT_EQ(m.format(), FeatureOverlay::kFormatGeoPackage);
  EXPECT_EQ(m.SetGeometry(fv::GeometryType::kGeometryCollection).code, fv::kInvalidArg);
  EXPECT_TRUE(m.Validate().ok());
}

TEST(NewLayer, DeskCreatesAndOpensTheLayer) {
  fv::desk::test::Scratch scratch;
  FakeDesk fd{400, 400};
  // Overlay ▸ New ▸ Feature Layer… opens the dialog rather than an empty overlay.
  ASSERT_TRUE(fd.Run({std::string("overlay.new.") + FeatureOverlay::kTypeId}).ok());
  EXPECT_EQ(fd.shell().new_layer_shown, 1);
  ASSERT_NE(fd.desk().new_layer(), nullptr);
  NewLayerModel& m = *fd.desk().new_layer();
  ASSERT_TRUE(m.SetTemplate(2).ok());  // restaurants
  m.SetPath(scratch.Path("food.gpkg"));
  ASSERT_TRUE(fd.desk().CreateNewLayer().ok());
  EXPECT_EQ(fd.desk().new_layer(), nullptr);
  auto* overlay = dynamic_cast<FeatureOverlay*>(fd.desk().overlays().current());
  ASSERT_NE(overlay, nullptr);
  EXPECT_EQ(overlay->Name(), "Restaurants");
  EXPECT_EQ(overlay->file_spec(), scratch.Path("food.gpkg"));
  EXPECT_EQ(overlay->layer()->Schema(), fv::FindFeatureLayerTemplate("restaurants")->schema);
  EXPECT_FALSE(overlay->is_dirty());

  // Replacing a file that is open is refused.
  fd.desk().OpenNewLayer();
  fd.desk().new_layer()->SetPath(scratch.Path("food.gpkg"));
  EXPECT_EQ(fd.desk().CreateNewLayer().code, fv::kInvalidArg);
  fd.desk().CloseNewLayer();
  EXPECT_EQ(fd.desk().new_layer(), nullptr);
}

TEST(NewLayer, AddsATableToAnOpenGeoPackage) {
  fv::desk::test::Scratch scratch;
  FakeDesk fd{400, 400};
  const std::string path = scratch.Path("food.gpkg");
  fd.desk().OpenNewLayer();
  ASSERT_TRUE(fd.desk().new_layer()->SetTemplate(2).ok());  // restaurants
  fd.desk().new_layer()->SetPath(path);
  ASSERT_TRUE(fd.desk().CreateNewLayer().ok());
  auto* food = dynamic_cast<FeatureOverlay*>(fd.desk().overlays().current());
  ASSERT_NE(food, nullptr);

  // The source menu's New Layer… starts the dialog on this package.
  bool listed = false;
  for (const fv::desk::MenuItem& m : fd.desk().SourceMenu()) listed = listed || m.command_id == "layers.source.new_layer";
  EXPECT_TRUE(listed);
  ASSERT_TRUE(fd.Run({"layers.source.new_layer"}).ok());
  NewLayerModel& m = *fd.desk().new_layer();
  EXPECT_EQ(m.target(), fv::desk::NewLayerTarget::kPackage);
  EXPECT_EQ(m.path(), path);
  EXPECT_EQ(m.SetFormat(FeatureOverlay::kFormatGeoJson).code, fv::kInvalidArg);
  ASSERT_TRUE(m.SetTemplate(3).ok());  // rivers
  ASSERT_TRUE(fd.desk().CreateNewLayer().ok());

  auto* rivers = dynamic_cast<FeatureOverlay*>(fd.desk().overlays().current());
  ASSERT_NE(rivers, nullptr);
  EXPECT_EQ(rivers->Name(), "Rivers");
  EXPECT_EQ(rivers->file_spec(), fv::app::JoinSourceSpec(path, "rivers"));
  EXPECT_EQ(rivers->layer()->Schema().geometry_type, fv::GeometryType::kLineString);
  // The first layer now names its own table, and both share one source.
  EXPECT_EQ(food->file_spec(), fv::app::JoinSourceSpec(path, "food"));
  size_t shared = 0;
  for (const fv::desk::SourceRow& r : fd.desk().sources().Rows())
    if (r.path == path) shared = r.layers.size();
  EXPECT_EQ(shared, 2u);

  // A second layer of the same name gets its own table.
  fd.desk().OpenNewLayer(path);
  ASSERT_TRUE(fd.desk().new_layer()->SetTemplate(3).ok());
  const fv::Status again = fd.desk().CreateNewLayer();
  ASSERT_TRUE(again.ok()) << again.message;
  EXPECT_EQ(dynamic_cast<FeatureOverlay*>(fd.desk().overlays().current())->file_spec(),
            fv::app::JoinSourceSpec(path, "rivers_2"));
  std::shared_ptr<fv::GeoPackage> package;
  ASSERT_TRUE(fv::GeoPackage::Open(path, false, &package).ok());
  std::vector<fv::GeoPackageLayerInfo> tables;
  ASSERT_TRUE(package->Layers(&tables).ok());
  EXPECT_EQ(tables.size(), 3u);
  package.reset();

  // A layer with unsaved edits does not stop a table being added, and its edits stay unsaved.
  fv::Feature f;
  f.geometry = fv::Geometry::Point({32.78, -79.94});
  f.values.resize(food->layer()->Schema().fields.size());
  f.values[food->layer()->Schema().Find("name")] = std::string("Monza");
  const fv::Status inserted = food->layer()->Insert(&f);
  ASSERT_TRUE(inserted.ok()) << inserted.message;
  const size_t food_count = food->layer()->Count();
  fd.desk().OpenNewLayer(path);
  ASSERT_TRUE(fd.desk().new_layer()->SetTemplate(3).ok());
  const fv::Status third = fd.desk().CreateNewLayer();
  ASSERT_TRUE(third.ok()) << third.message;
  EXPECT_EQ(food->layer()->Count(), food_count);
  auto* gp_food = dynamic_cast<fv::GeoPackageFeatureLayer*>(food->layer().get());
  ASSERT_NE(gp_food, nullptr);
  EXPECT_TRUE(gp_food->HasEdits());
}

TEST(NewLayer, AnUnsavedLayerHasItsSchemaAndNoFile) {
  FakeDesk fd{400, 400};
  fd.desk().OpenNewLayer();
  NewLayerModel& m = *fd.desk().new_layer();
  ASSERT_TRUE(m.SetTemplate(2).ok());
  m.SetTarget(fv::desk::NewLayerTarget::kUnsaved);
  ASSERT_TRUE(m.Validate().ok());  // no path needed
  ASSERT_TRUE(fd.desk().CreateNewLayer().ok());
  auto* overlay = dynamic_cast<FeatureOverlay*>(fd.desk().overlays().current());
  ASSERT_NE(overlay, nullptr);
  EXPECT_EQ(overlay->Name(), "Restaurants");
  EXPECT_EQ(overlay->layer()->Schema(), fv::FindFeatureLayerTemplate("restaurants")->schema);
  EXPECT_TRUE(overlay->file_spec().empty());
  EXPECT_FALSE(overlay->has_been_saved());
}

}  // namespace

TEST(NewLayer, AddPointOnATableAddedToAPackage) {
  fv::desk::test::Scratch scratch;
  FakeDesk fd{400, 400};
  fd.desk().GoTo(fv::GeoPoint{32.80, -79.90}, 50000.0);
  const std::string path = scratch.Path("rivers.gpkg");
  fd.desk().OpenNewLayer();
  ASSERT_TRUE(fd.desk().new_layer()->SetTemplate(3).ok());  // rivers
  fd.desk().new_layer()->SetPath(path);
  ASSERT_TRUE(fd.desk().CreateNewLayer().ok());
  fd.desk().OpenNewLayer(path);
  ASSERT_TRUE(fd.desk().new_layer()->SetTemplate(2).ok());  // restaurants
  ASSERT_TRUE(fd.desk().CreateNewLayer().ok());
  auto* food = dynamic_cast<FeatureOverlay*>(fd.desk().overlays().current());
  ASSERT_NE(food, nullptr);
  ASSERT_EQ(food->layer()->Schema().geometry_type, fv::GeometryType::kPoint);
  // As in the Layers panel: the rivers layer is current while the editor starts,
  // then the restaurants row is picked.
  auto select = [&](bool want_food) {
    for (size_t r = 0; r < 2; ++r) {
      ASSERT_TRUE(fd.desk().SelectLayer(r).ok());
      if ((fd.desk().overlays().current() == food) == want_food) return;
    }
    FAIL() << "no such row";
  };
  select(false);
  fv::PixelBuffer out;
  ASSERT_TRUE(fd.Render(&out).ok());
  if (fd.shell().editor_mode != FeatureOverlay::kTypeId)
    ASSERT_TRUE(fd.Run({std::string("editor.mode.") + FeatureOverlay::kTypeId}).ok());
  select(true);
  ASSERT_TRUE(fd.Render(&out).ok());
  std::string add;
  for (const std::string& id : fd.desk().commands().IdsWithPrefix("editor.tool."))
    if (fd.desk().commands().Find(id)->label == "Add Point") add = id;
  ASSERT_FALSE(add.empty());
  ASSERT_TRUE(fd.desk().commands().IsEnabled(add));
  ASSERT_TRUE(fd.Run({add}).ok());
  EXPECT_TRUE(fd.desk().OverlayPointerDown(200, 200));
  fd.desk().OverlayPointerUp(200, 200);
  EXPECT_EQ(food->layer()->Count(), 1u);
}
