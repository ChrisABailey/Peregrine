// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/detail/sqlite.h"
#include "fvkit/feature/geopackage.h"
#include "fvkit/feature/schema_change.h"
#include "fvkit/feature/style.h"
#include "fvkit/feature/templates.h"
#include "fvkit/overlay/feature_overlay.h"

namespace {

using fv::Feature;
using fv::FeatureSchema;
using fv::FieldDef;
using fv::FieldRole;
using fv::FieldType;
using fv::FieldValue;
using fv::GeoPackage;
using fv::GeoPackageFeatureLayer;
using fv::Geometry;
using fv::GeometryType;
using fv::SchemaChange;

namespace fs = std::filesystem;

FieldDef Field(const char* name, FieldType type = FieldType::kText) {
  FieldDef f;
  f.name = name;
  f.type = type;
  return f;
}

/// name (required), phone, style (choice), rating 1–5.
FeatureSchema PizzaSchema() {
  FeatureSchema s;
  s.geometry_type = GeometryType::kPoint;
  FieldDef name = Field("name");
  name.required = true;
  FieldDef phone = Field("phone");
  phone.role = FieldRole::kPhone;
  FieldDef style = Field("style");
  style.title = "Style of pizza";
  style.description = "How the pie is made";
  style.role = FieldRole::kEnumeration;
  style.choices = {{"neapolitan", "Neapolitan"}, {"new_york", "New York"}, {"detroit", "Detroit"}};
  FieldDef rating = Field("rating", FieldType::kInteger);
  rating.min = 1;
  rating.max = 5;
  s.fields = {name, phone, style, rating};
  return s;
}

Feature Pizza(double lat, double lon, const char* name, FieldValue style, FieldValue rating) {
  Feature f;
  f.geometry = Geometry::Point({lat, lon});
  f.values = {std::string(name), std::string("+1 843 555 0100"), std::move(style), std::move(rating)};
  return f;
}

/// Keeps every field in place.
SchemaChange Identity(const FeatureSchema& s) {
  SchemaChange c{s, {}};
  for (size_t i = 0; i < s.fields.size(); ++i) c.source.push_back(static_cast<int>(i));
  return c;
}

TEST(FeatureSchemaChange, ConvertsBetweenTypes) {
  bool lost = false;
  const FieldDef text = Field("t"), integer = Field("i", FieldType::kInteger),
                 real = Field("r", FieldType::kReal), boolean = Field("b", FieldType::kBoolean),
                 date = Field("d", FieldType::kDate), datetime = Field("dt", FieldType::kDateTime);

  EXPECT_EQ(std::get<double>(ConvertFieldValue(integer, int64_t{7}, real, &lost)), 7.0);
  EXPECT_FALSE(lost);
  EXPECT_EQ(std::get<int64_t>(ConvertFieldValue(real, 3.0, integer, &lost)), 3);
  EXPECT_FALSE(lost);
  EXPECT_EQ(std::get<int64_t>(ConvertFieldValue(real, 2.6, integer, &lost)), 3);
  EXPECT_TRUE(lost);
  EXPECT_EQ(std::get<int64_t>(ConvertFieldValue(text, std::string(" 42 "), integer, &lost)), 42);
  EXPECT_FALSE(lost);
  EXPECT_TRUE(fv::IsNull(ConvertFieldValue(text, std::string("forty"), integer, &lost)));
  EXPECT_TRUE(lost);
  EXPECT_EQ(std::get<int64_t>(ConvertFieldValue(boolean, true, integer, &lost)), 1);
  EXPECT_EQ(std::get<std::string>(ConvertFieldValue(real, 0.1, text, &lost)), "0.1");
  EXPECT_EQ(std::get<std::string>(ConvertFieldValue(real, 1.0 / 3.0, text, &lost)), "0.33333333333333331");
  EXPECT_EQ(std::get<std::string>(ConvertFieldValue(date, std::string("2026-10-10"), datetime, &lost)),
            "2026-10-10T00:00:00");
  EXPECT_FALSE(lost);
  EXPECT_EQ(std::get<std::string>(ConvertFieldValue(datetime, std::string("2026-10-10T14:30:00Z"), date, &lost)),
            "2026-10-10");
  EXPECT_TRUE(lost);
  EXPECT_TRUE(fv::IsNull(ConvertFieldValue(text, FieldValue{}, integer, &lost)));
  EXPECT_FALSE(lost);

  // Choices and bounds of the new field apply; an enumeration takes a label.
  const FieldDef style = PizzaSchema().fields[2];
  EXPECT_EQ(std::get<std::string>(ConvertFieldValue(text, std::string("New York"), style, &lost)), "new_york");
  EXPECT_FALSE(lost);
  EXPECT_TRUE(fv::IsNull(ConvertFieldValue(text, std::string("chicago"), style, &lost)));
  EXPECT_TRUE(lost);
  const FieldDef rating = PizzaSchema().fields[3];
  EXPECT_TRUE(fv::IsNull(ConvertFieldValue(text, std::string("9"), rating, &lost)));
  EXPECT_TRUE(lost);
  // To text an enumeration gives its value, not its label.
  EXPECT_EQ(std::get<std::string>(ConvertFieldValue(style, std::string("detroit"), text, &lost)), "detroit");
}

TEST(FeatureSchemaChange, ValidatesTheMapping) {
  const FeatureSchema old = PizzaSchema();
  SchemaChange c = Identity(old);
  EXPECT_TRUE(ValidateSchemaChange(old, c).ok());
  c.source[1] = 0;
  EXPECT_EQ(ValidateSchemaChange(old, c).code, fv::kInvalidArg);  // fed twice
  c = Identity(old);
  c.source.pop_back();
  EXPECT_EQ(ValidateSchemaChange(old, c).code, fv::kInvalidArg);
  c = Identity(old);
  c.source[3] = 9;
  EXPECT_EQ(ValidateSchemaChange(old, c).code, fv::kInvalidArg);
  c = Identity(old);
  c.schema.geometry_type = GeometryType::kLineString;
  EXPECT_EQ(ValidateSchemaChange(old, c).code, fv::kInvalidArg);
}

TEST(FeatureSchemaChange, MemoryLayerRenamesReordersAddsAndDrops) {
  fv::MemoryFeatureLayer layer("pizza", PizzaSchema());
  Feature a = Pizza(32.6, -80.0, "Mellow", std::string("neapolitan"), int64_t{4});
  Feature b = Pizza(32.7, -80.1, "Slice", std::string("detroit"), int64_t{5});
  ASSERT_TRUE(layer.Insert(&a).ok());
  ASSERT_TRUE(layer.Insert(&b).ok());
  ASSERT_TRUE(layer.Delete(a.id).ok());
  Feature c = Pizza(32.8, -80.2, "Pie", FieldValue{}, int64_t{2});
  ASSERT_TRUE(layer.Insert(&c).ok());

  // rating first and real, name renamed, phone dropped, style kept, a new field.
  const FeatureSchema old = layer.Schema();
  SchemaChange change;
  change.schema.geometry_type = old.geometry_type;
  FieldDef score = old.fields[3];
  score.name = "score";
  score.type = FieldType::kReal;
  FieldDef title = old.fields[0];
  title.name = "title";
  FieldDef opened = Field("opened", FieldType::kDate);
  change.schema.fields = {score, title, old.fields[2], opened};
  change.source = {3, 0, 2, -1};

  fv::SchemaChangeImpact impact;
  ASSERT_TRUE(AssessSchemaChange(layer, change, &impact).ok());
  ASSERT_EQ(impact.dropped, std::vector<std::string>{"phone"});
  EXPECT_TRUE(impact.converted.empty());
  EXPECT_TRUE(impact.LosesData());
  EXPECT_NE(impact.Describe().find("\"phone\""), std::string::npos);

  ASSERT_TRUE(layer.ChangeSchema(change).ok());
  EXPECT_EQ(layer.Schema(), change.schema);
  std::vector<Feature> all;
  ASSERT_TRUE(layer.All(&all).ok());
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].id, b.id);
  EXPECT_EQ(std::get<double>(all[0].values[0]), 5.0);
  EXPECT_EQ(std::get<std::string>(all[0].values[1]), "Slice");
  EXPECT_EQ(std::get<std::string>(all[0].values[2]), "detroit");
  EXPECT_TRUE(fv::IsNull(all[0].values[3]));
  EXPECT_EQ(all[1].id, c.id);
  EXPECT_TRUE(fv::IsNull(all[1].values[2]));
  // A new feature after the change takes the next id, not a reused one.
  Feature d;
  d.geometry = Geometry::Point({33, -80});
  d.values = {FieldValue{}, std::string("New")};
  ASSERT_TRUE(layer.Insert(&d).ok());
  EXPECT_GT(d.id, c.id);
}

TEST(FeatureSchemaChange, RequiredFieldNeedsADefault) {
  fv::MemoryFeatureLayer layer("pizza", PizzaSchema());
  Feature a = Pizza(32.6, -80.0, "Mellow", std::string("neapolitan"), int64_t{4});
  ASSERT_TRUE(layer.Insert(&a).ok());
  SchemaChange change = Identity(layer.Schema());
  FieldDef owner = Field("owner");
  owner.required = true;
  change.schema.fields.push_back(owner);
  change.source.push_back(-1);

  fv::SchemaChangeImpact impact;
  ASSERT_TRUE(AssessSchemaChange(layer, change, &impact).ok());
  EXPECT_EQ(impact.missing_required, std::vector<std::string>{"owner"});
  EXPECT_FALSE(impact.LosesData());
  EXPECT_EQ(layer.ChangeSchema(change).code, fv::kInvalidArg);
  EXPECT_EQ(layer.Schema(), PizzaSchema());  // unchanged

  change.schema.fields.back().default_value = std::string("unknown");
  ASSERT_TRUE(AssessSchemaChange(layer, change, &impact).ok());
  EXPECT_TRUE(impact.missing_required.empty());
  ASSERT_TRUE(layer.ChangeSchema(change).ok());
  Feature got;
  ASSERT_TRUE(layer.Get(a.id, &got).ok());
  EXPECT_EQ(std::get<std::string>(got.values[4]), "unknown");
}

TEST(FeatureSchemaChange, AssessCountsValuesThatDoNotConvert) {
  fv::MemoryFeatureLayer layer("pizza", PizzaSchema());
  Feature a = Pizza(32.6, -80.0, "12", std::string("neapolitan"), int64_t{4});
  Feature b = Pizza(32.7, -80.1, "Slice", std::string("detroit"), int64_t{5});
  ASSERT_TRUE(layer.Insert(&a).ok());
  ASSERT_TRUE(layer.Insert(&b).ok());
  SchemaChange change = Identity(layer.Schema());
  change.schema.fields[0].type = FieldType::kInteger;
  change.schema.fields[0].required = false;
  fv::SchemaChangeImpact impact;
  ASSERT_TRUE(AssessSchemaChange(layer, change, &impact).ok());
  ASSERT_EQ(impact.converted.size(), 1u);
  EXPECT_EQ(impact.converted[0].first, "name");
  EXPECT_EQ(impact.converted[0].second, 1u);
  EXPECT_TRUE(impact.dropped.empty());
}

/// A fresh GeoPackage path, removed when the test ends.
class FeatureSchemaChangeGpkg : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = (fs::temp_directory_path() / (std::string("fv_fields_") + info->name() + ".gpkg")).string();
    fs::remove(path_);
  }
  void TearDown() override { fs::remove(path_); }

  /// Writes the pizza table with three features (one deleted) and saves.
  void WritePizza() {
    std::shared_ptr<GeoPackage> gp;
    ASSERT_TRUE(GeoPackage::Create(path_, &gp).ok());
    std::shared_ptr<GeoPackageFeatureLayer> layer;
    ASSERT_TRUE(gp->CreateLayer("pizza", PizzaSchema(), {}, &layer).ok());
    Feature a = Pizza(32.6, -80.0, "Mellow", std::string("neapolitan"), int64_t{4});
    Feature b = Pizza(32.7, -80.1, "Slice", std::string("detroit"), int64_t{5});
    Feature c = Pizza(32.8, -80.2, "Top", std::string("new_york"), int64_t{3});
    ASSERT_TRUE(layer->Insert(&a).ok());
    ASSERT_TRUE(layer->Insert(&b).ok());
    ASSERT_TRUE(layer->Insert(&c).ok());
    ASSERT_TRUE(layer->Delete(c.id).ok());
    ASSERT_TRUE(gp->Save().ok());
  }

  std::shared_ptr<GeoPackageFeatureLayer> OpenPizza(bool writable, std::shared_ptr<GeoPackage>* gp) {
    std::shared_ptr<GeoPackageFeatureLayer> layer;
    EXPECT_TRUE(GeoPackage::Open(path_, writable, gp).ok());
    EXPECT_TRUE((*gp)->OpenLayer("pizza", &layer).ok());
    return layer;
  }

  /// One text column of a metadata query, or "<none>".
  std::string Scalar(const std::string& sql) {
    fv::detail::SqliteDb db;
    EXPECT_TRUE(db.OpenReadOnly(path_).ok());
    fv::detail::SqliteStmt st;
    EXPECT_TRUE(st.Prepare(db, sql.c_str()).ok());
    return st.Step() ? st.ColText(0) : std::string("<none>");
  }

  std::string path_;
};

// The FO7 acceptance: adding a field to a saved GeoPackage keeps its data and
// its QGIS field names.
TEST_F(FeatureSchemaChangeGpkg, AddingAFieldKeepsDataAndFieldNames) {
  WritePizza();
  {
    std::shared_ptr<GeoPackage> gp;
    auto layer = OpenPizza(true, &gp);
    SchemaChange change = Identity(layer->Schema());
    FieldDef opened = Field("opened", FieldType::kDate);
    opened.title = "Date opened";
    change.schema.fields.push_back(opened);
    change.source.push_back(-1);
    const fv::Status s = layer->ChangeSchema(change);
    ASSERT_TRUE(s.ok()) << s.message;
    EXPECT_EQ(layer->Schema(), change.schema);
    EXPECT_TRUE(gp->HasUnsavedChanges());
    ASSERT_TRUE(gp->Save().ok());
  }
  std::shared_ptr<GeoPackage> gp;
  auto layer = OpenPizza(false, &gp);
  FeatureSchema expect = PizzaSchema();
  FieldDef opened = Field("opened", FieldType::kDate);
  opened.title = "Date opened";
  expect.fields.push_back(opened);
  EXPECT_EQ(layer->Schema(), expect);
  std::vector<Feature> all;
  ASSERT_TRUE(layer->All(&all).ok());
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].id, 1);
  EXPECT_EQ(std::get<std::string>(all[0].values[0]), "Mellow");
  EXPECT_EQ(std::get<std::string>(all[0].values[2]), "neapolitan");
  EXPECT_EQ(std::get<int64_t>(all[1].values[3]), 5);
  EXPECT_TRUE(fv::IsNull(all[1].values[4]));
  // What QGIS reads: the alias, description and pick-list of the old fields.
  EXPECT_EQ(Scalar("SELECT name FROM gpkg_data_columns WHERE table_name = 'pizza' AND column_name = 'style'"),
            "Style of pizza");
  EXPECT_EQ(Scalar("SELECT description FROM gpkg_data_columns WHERE table_name = 'pizza' "
                   "AND column_name = 'style'"),
            "How the pie is made");
  EXPECT_EQ(Scalar("SELECT COUNT(*) FROM gpkg_data_column_constraints WHERE constraint_name = 'pizza_style'"),
            "3");
  EXPECT_EQ(Scalar("SELECT name FROM gpkg_data_columns WHERE table_name = 'pizza' AND column_name = 'opened'"),
            "Date opened");
  // The R-tree triggers came back with the table.
  EXPECT_EQ(Scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger' AND tbl_name = 'pizza'"), "6");
  std::vector<Feature> hits;
  ASSERT_TRUE(layer->Query(fv::GeoRect{{32.65, -80.15}, {32.75, -80.05}}, &hits).ok());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(std::get<std::string>(hits[0].values[0]), "Slice");
}

TEST_F(FeatureSchemaChangeGpkg, RenameRetypeDeleteAndRevert) {
  WritePizza();
  std::shared_ptr<GeoPackage> gp;
  auto layer = OpenPizza(true, &gp);
  const FeatureSchema old = layer->Schema();
  SchemaChange change;
  change.schema.geometry_type = old.geometry_type;
  FieldDef rating = old.fields[3];
  rating.name = "stars";
  rating.type = FieldType::kText;
  rating.min.reset();
  rating.max.reset();
  change.schema.fields = {old.fields[0], rating, old.fields[2]};
  change.source = {0, 3, 2};
  ASSERT_TRUE(layer->ChangeSchema(change).ok());
  EXPECT_EQ(layer->Schema(), change.schema);
  Feature f;
  ASSERT_TRUE(layer->Get(2, &f).ok());
  EXPECT_EQ(std::get<std::string>(f.values[1]), "5");

  // The next id still follows the deleted third feature.
  Feature n;
  n.geometry = Geometry::Point({33, -80});
  n.values = {std::string("New")};
  ASSERT_TRUE(layer->Insert(&n).ok());
  EXPECT_EQ(n.id, 4);
  std::vector<Feature> hits;
  ASSERT_TRUE(layer->Query(fv::GeoRect{{32.9, -80.1}, {33.1, -79.9}}, &hits).ok());
  EXPECT_EQ(hits.size(), 1u);

  ASSERT_TRUE(gp->Revert().ok());
  std::shared_ptr<GeoPackageFeatureLayer> again;
  ASSERT_TRUE(gp->OpenLayer("pizza", &again).ok());
  EXPECT_EQ(again->Schema(), PizzaSchema());
  EXPECT_EQ(again->Count(), 2u);
}

TEST_F(FeatureSchemaChangeGpkg, RefusesTheIdColumnName) {
  WritePizza();
  std::shared_ptr<GeoPackage> gp;
  auto layer = OpenPizza(true, &gp);
  SchemaChange change = Identity(layer->Schema());
  change.schema.fields.push_back(Field("FID", FieldType::kInteger));
  change.source.push_back(-1);
  EXPECT_EQ(layer->ChangeSchema(change).code, fv::kInvalidArg);
  EXPECT_EQ(layer->Schema(), PizzaSchema());

  std::shared_ptr<GeoPackage> ro;
  auto read_only = OpenPizza(false, &ro);
  EXPECT_EQ(read_only->ChangeSchema(Identity(read_only->Schema())).code, fv::kUnsupported);
}

TEST(FeatureSchemaChange, StyleFollowsRenamesAndDeletes) {
  const FeatureSchema old = PizzaSchema();
  fv::FeatureStyle style;
  style.renderer = fv::FeatureRenderer::kCategorised;
  style.field = "style";
  style.label.field = "name";

  SchemaChange rename = Identity(old);
  rename.schema.fields[2].name = "kind";
  rename.schema.fields[0].name = "title";
  fv::FeatureStyle s = style;
  RemapStyleFields(&s, old, rename);
  EXPECT_EQ(s.renderer, fv::FeatureRenderer::kCategorised);
  EXPECT_EQ(s.field, "kind");
  EXPECT_EQ(s.label.field, "title");

  SchemaChange drop{old, {0, 1, -1, 3}};
  drop.schema.fields[2] = Field("other");
  s = style;
  RemapStyleFields(&s, old, drop);
  EXPECT_EQ(s.renderer, fv::FeatureRenderer::kSingle);
  EXPECT_TRUE(s.field.empty());
  EXPECT_EQ(s.label.field, "name");

  fv::FeatureStyle graduated;
  graduated.renderer = fv::FeatureRenderer::kGraduated;
  graduated.field = "rating";
  SchemaChange retype = Identity(old);
  retype.schema.fields[3].type = FieldType::kText;
  retype.schema.fields[3].min.reset();
  retype.schema.fields[3].max.reset();
  RemapStyleFields(&graduated, old, retype);
  EXPECT_EQ(graduated.renderer, fv::FeatureRenderer::kSingle);
}

TEST(FeatureSchemaChange, OverlayDropsItsEditHistory) {
  fv::FeatureOverlay overlay;
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("pizza", PizzaSchema());
  Feature a = Pizza(32.6, -80.0, "Mellow", std::string("neapolitan"), int64_t{4});
  ASSERT_TRUE(layer->Insert(&a).ok());
  overlay.SetLayer(layer);
  fv::FeatureStyle style;
  style.label.field = "phone";
  overlay.SetStyle(style);
  ASSERT_TRUE(overlay.edit().MoveFeatures({a.id}, 0.1, 0.1));
  ASSERT_TRUE(overlay.CanUndo());

  SchemaChange change{PizzaSchema(), {0, -1, 2, 3}};
  change.schema.fields[1] = Field("email");
  ASSERT_TRUE(overlay.ChangeSchema(change).ok());
  EXPECT_FALSE(overlay.CanUndo());
  EXPECT_TRUE(overlay.style().label.field.empty());
  EXPECT_EQ(overlay.layer()->Schema().fields[1].name, "email");
}

TEST(FeatureSchemaChange, TemplatesValidateAndCreateFiles) {
  const auto& templates = fv::FeatureLayerTemplates();
  ASSERT_GE(templates.size(), 4u);
  EXPECT_EQ(templates.front().id, "blank");
  for (const auto& t : templates) EXPECT_TRUE(t.schema.Validate().ok()) << t.id;
  ASSERT_NE(fv::FindFeatureLayerTemplate("rivers"), nullptr);
  EXPECT_EQ(fv::FindFeatureLayerTemplate("nope"), nullptr);

  const FeatureSchema rivers = fv::FindFeatureLayerTemplate("rivers")->schema;
  for (int format : {fv::FeatureOverlay::kFormatGeoPackage, fv::FeatureOverlay::kFormatGeoJson}) {
    const std::string path =
        (fs::temp_directory_path() /
         (format == fv::FeatureOverlay::kFormatGeoPackage ? "fv_new_rivers.gpkg" : "fv_new_rivers.geojson"))
            .string();
    fs::remove(path);
    ASSERT_TRUE(fv::CreateFeatureLayerFile(path, format, "Rivers", rivers).ok());
    fv::FeatureOverlay overlay;
    ASSERT_TRUE(overlay.FileOpen(path).ok());
    ASSERT_TRUE(overlay.layer() != nullptr);
    EXPECT_EQ(overlay.layer()->Schema(), rivers) << path;
    EXPECT_EQ(overlay.layer()->Count(), 0u);
    EXPECT_EQ(overlay.Name(), "Rivers");
    fs::remove(path);
  }
}

}  // namespace
