// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "fv_desk_fake.h"
#include "fv_desk_layer_fields.h"
#include "fvkit/feature/geojson.h"
#include "fvkit/feature/templates.h"
#include "fvkit/overlay/feature_overlay.h"
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
  ASSERT_TRUE(fd.Run({"overlay.new_layer"}).ok());
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

}  // namespace
