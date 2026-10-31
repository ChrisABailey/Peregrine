// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Export Selection and Save As into an existing GeoPackage, through FakeDesk.
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "fv_desk_fake.h"
#include "fvkit/app/source_spec.h"
#include "fvkit/feature/geojson.h"
#include "fvkit/feature/geopackage.h"
#include "fvkit/overlay/feature_overlay.h"
#include "scratch.h"

namespace {

using fv::FeatureOverlay;
using fv::FieldDef;
using fv::FieldType;
using fv::desk::FakeDesk;

/// Points: name, rating; three restaurants.
std::shared_ptr<fv::MemoryFeatureLayer> PizzaLayer() {
  fv::FeatureSchema s;
  s.geometry_type = fv::GeometryType::kPoint;
  FieldDef name;
  name.name = "name";
  name.title = "Name";
  FieldDef rating;
  rating.name = "rating";
  rating.type = FieldType::kReal;
  s.fields = {name, rating};
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("Pizza", s);
  const struct {
    double lat, lon;
    const char* name;
    double rating;
  } rows[] = {{32.78, -79.93, "Andolini's", 4.0}, {32.80, -79.95, "Paulie Gee's", 4.5}, {32.79, -79.94, "Slice Box", 3.0}};
  for (const auto& r : rows) {
    fv::Feature f;
    f.geometry = fv::Geometry::Point({r.lat, r.lon});
    f.values = {std::string(r.name), r.rating};
    layer->Insert(&f);
  }
  return layer;
}

/// Lines: name, flow; two rivers.
std::shared_ptr<fv::MemoryFeatureLayer> RiversLayer() {
  fv::FeatureSchema s;
  s.geometry_type = fv::GeometryType::kLineString;
  FieldDef name;
  name.name = "name";
  FieldDef flow;
  flow.name = "flow";
  flow.title = "Flow";
  flow.type = FieldType::kInteger;
  s.fields = {name, flow};
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("Rivers", s);
  const struct {
    double lat0, lon0, lat1, lon1;
    const char* name;
    int64_t flow;
  } rows[] = {{32.7, -80.0, 32.8, -79.9, "Ashley", 120}, {32.9, -80.1, 32.8, -79.8, "Cooper", 300}};
  for (const auto& r : rows) {
    fv::Feature f;
    f.geometry = fv::Geometry::LineString({{r.lat0, r.lon0}, {r.lat1, r.lon1}});
    f.values = {std::string(r.name), r.flow};
    layer->Insert(&f);
  }
  return layer;
}

class ExportSelectionTest : public ::testing::Test {
 protected:
  /// Opens `path` in the desk and returns its overlay, which becomes current.
  std::shared_ptr<FeatureOverlay> Open(const std::string& path) {
    fd_.shell().files_to_open = {path};
    EXPECT_TRUE(fd_.Run({"overlay.open"}).ok());
    EXPECT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
    auto found = fd_.desk().overlays().OfType(FeatureOverlay::kTypeId);
    return found.empty() ? nullptr : std::static_pointer_cast<FeatureOverlay>(found.front());
  }
  std::shared_ptr<FeatureOverlay> OpenGeoJson(const std::shared_ptr<fv::MemoryFeatureLayer>& layer,
                                              const std::string& name) {
    EXPECT_TRUE(fv::WriteGeoJsonFile(*layer, scratch_.Path(name)).ok());
    return Open(scratch_.Path(name));
  }
  /// Selects `ids` of `o`, makes it current and runs Export Selection to
  /// `path`, answering the lists with `answers` in turn: the first picks new
  /// file (0), existing file (1) or an open GeoPackage (2 on).
  void Export(const std::shared_ptr<FeatureOverlay>& o, std::vector<int64_t> ids, const std::string& path,
              std::vector<std::optional<int>> answers) {
    o->SetSelection(std::move(ids));
    fd_.desk().overlays().MakeCurrent(o);
    fd_.shell().save_spec = path;
    fd_.shell().files_to_open = {path};
    fd_.shell().list_choice = std::nullopt;
    fd_.shell().list_choices = std::move(answers);
    fd_.shell().lists.clear();
    ASSERT_TRUE(fd_.Run({"overlay.export_selection"}).ok());
  }
  static std::vector<fv::GeoPackageLayerInfo> Tables(const std::string& path) {
    std::shared_ptr<fv::GeoPackage> package;
    std::vector<fv::GeoPackageLayerInfo> out;
    EXPECT_TRUE(fv::GeoPackage::Open(path, false, &package).ok());
    if (package) EXPECT_TRUE(package->Layers(&out).ok());
    return out;
  }

  fv::desk::test::Scratch scratch_;
  FakeDesk fd_{400, 400};
};

TEST_F(ExportSelectionTest, TwoLayersIntoOneOpenPackageReopenWithFieldsAndStyles) {
  auto pizza = OpenGeoJson(PizzaLayer(), "pizza.geojson");
  auto rivers = OpenGeoJson(RiversLayer(), "rivers.geojson");
  ASSERT_TRUE(pizza && rivers);
  pizza->SetDescription("Best slices");
  rivers->SetDescription("Tidal rivers");
  rivers->SetMapTipField("flow");

  const std::string trip = scratch_.Path("trip.gpkg");
  Export(pizza, {1, 3}, trip, {0});
  ASSERT_EQ(fd_.shell().lists.size(), 1u);
  EXPECT_EQ(fd_.shell().lists[0], (std::vector<std::string>{"New GeoPackage…", "Existing GeoPackage…"}));
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  EXPECT_NE(fd_.desk().CurrentStatus().feedback.find("Exported 2 features"), std::string::npos) << fd_.desk().CurrentStatus().feedback;

  // With the file open, the second selection goes in as a new table through its connection.
  auto opened = Open(trip);
  ASSERT_TRUE(opened);
  EXPECT_TRUE(fv::PackageIsOpen(trip));
  Export(rivers, {2}, trip, {2});
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  // The open file is listed by name; another schema leaves nothing more to ask.
  ASSERT_EQ(fd_.shell().lists.size(), 1u);
  ASSERT_EQ(fd_.shell().lists[0].size(), 3u);
  EXPECT_EQ(fd_.shell().lists[0][2], "trip.gpkg");
  opened.reset();

  const std::vector<fv::GeoPackageLayerInfo> tables = Tables(trip);
  ASSERT_EQ(tables.size(), 2u);
  for (const fv::GeoPackageLayerInfo& t : tables) {
    FeatureOverlay reopened;
    const fv::Status s = reopened.FileOpen(fv::app::JoinSourceSpec(trip, t.table));
    ASSERT_TRUE(s.ok()) << s.message;
    const fv::FeatureSchema& schema = reopened.layer()->Schema();
    ASSERT_EQ(schema.fields.size(), 2u);
    std::vector<fv::Feature> all;
    ASSERT_TRUE(reopened.layer()->All(&all).ok());
    if (schema.geometry_type == fv::GeometryType::kPoint) {
      EXPECT_EQ(schema.fields[0].title, "Name");
      EXPECT_EQ(all.size(), 2u);
      EXPECT_EQ(reopened.description(), "Best slices");
    } else {
      EXPECT_EQ(schema.fields[1].title, "Flow");
      ASSERT_EQ(all.size(), 1u);
      EXPECT_EQ(std::get<std::string>(all[0].values[0]), "Cooper");
      EXPECT_EQ(reopened.description(), "Tidal rivers");
      EXPECT_EQ(reopened.map_tip_field(), "flow");
    }
  }
}

TEST_F(ExportSelectionTest, AppendIsOfferedOnlyForAMatchingSchema) {
  auto pizza = OpenGeoJson(PizzaLayer(), "pizza.geojson");
  auto rivers = OpenGeoJson(RiversLayer(), "rivers.geojson");
  const std::string picks = scratch_.Path("picks.gpkg");
  Export(pizza, {1}, picks, {0});

  // An existing file offers a new layer or the matching table, never replacing it.
  Export(pizza, {2, 3}, picks, {1, 1});
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  ASSERT_EQ(fd_.shell().lists.size(), 2u);
  ASSERT_EQ(fd_.shell().lists[1].size(), 2u);
  EXPECT_EQ(fd_.shell().lists[1][1].rfind("Add to \"", 0), 0u);
  std::vector<fv::GeoPackageLayerInfo> tables = Tables(picks);
  ASSERT_EQ(tables.size(), 1u);
  {
    FeatureOverlay check;
    ASSERT_TRUE(check.FileOpen(picks).ok());
    EXPECT_EQ(check.layer()->Count(), 3u);
  }

  // Rivers have other fields: they go in as a new layer without a second question.
  Export(rivers, {1}, picks, {1});
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  EXPECT_EQ(fd_.shell().lists.size(), 1u);
  EXPECT_EQ(Tables(picks).size(), 2u);

  // A new file over it, through the save panel, leaves the one exported table.
  Export(rivers, {1}, picks, {0});
  tables = Tables(picks);
  ASSERT_EQ(tables.size(), 1u);
  FeatureOverlay check;
  ASSERT_TRUE(check.FileOpen(picks).ok());
  EXPECT_EQ(check.layer()->Schema().geometry_type, fv::GeometryType::kLineString);
}

TEST_F(ExportSelectionTest, AppendToAnOpenTableShowsTheRowsAndRefusesUnsavedEdits) {
  auto pizza = OpenGeoJson(PizzaLayer(), "pizza.geojson");
  const std::string picks = scratch_.Path("picks.gpkg");
  Export(pizza, {1}, picks, {0});
  auto target = Open(picks);
  ASSERT_TRUE(target);
  ASSERT_EQ(target->layer()->Count(), 1u);

  Export(pizza, {2}, picks, {2, 1});
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  EXPECT_EQ(target->layer()->Count(), 2u);
  auto* table = dynamic_cast<fv::GeoPackageFeatureLayer*>(target->layer().get());
  ASSERT_NE(table, nullptr);
  EXPECT_FALSE(table->HasEdits());

  // An open table with unsaved edits is not appended to, and keeps its edits.
  fv::Feature f;
  f.geometry = fv::Geometry::Point({32.7, -79.9});
  f.values = {std::string("Monza"), 4.2};
  ASSERT_TRUE(target->layer()->Insert(&f).ok());
  Export(pizza, {3}, picks, {2, 1});
  ASSERT_EQ(fd_.shell().errors.size(), 1u);
  EXPECT_NE(fd_.shell().errors[0].message.find("unsaved edits"), std::string::npos);
  EXPECT_TRUE(table->HasEdits());
  EXPECT_EQ(target->layer()->Count(), 3u);
}

TEST_F(ExportSelectionTest, SelectedFieldsInAnotherOrderAppendByName) {
  fv::FeatureSchema reversed = PizzaLayer()->Schema();
  std::reverse(reversed.fields.begin(), reversed.fields.end());
  EXPECT_TRUE(fv::SchemasMatchForAppend(PizzaLayer()->Schema(), reversed));
  reversed.fields[0].type = FieldType::kText;  // rating as text
  EXPECT_FALSE(fv::SchemasMatchForAppend(PizzaLayer()->Schema(), reversed));
  EXPECT_FALSE(fv::SchemasMatchForAppend(PizzaLayer()->Schema(), RiversLayer()->Schema()));

  // A table written with its fields reversed takes an append by name.
  const std::string path = scratch_.Path("reversed.gpkg");
  std::shared_ptr<fv::GeoPackage> package;
  ASSERT_TRUE(fv::GeoPackage::Create(path, &package).ok());
  fv::FeatureSchema schema = PizzaLayer()->Schema();
  std::reverse(schema.fields.begin(), schema.fields.end());
  ASSERT_TRUE(package->CreateLayer("pizza", schema, {}, nullptr).ok());
  ASSERT_TRUE(package->Save().ok());
  package.reset();

  auto pizza = OpenGeoJson(PizzaLayer(), "pizza.geojson");
  Export(pizza, {2}, path, {1, 1});
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  FeatureOverlay check;
  ASSERT_TRUE(check.FileOpen(path).ok());
  std::vector<fv::Feature> all;
  ASSERT_TRUE(check.layer()->All(&all).ok());
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(std::get<double>(all[0].values[0]), 4.5);
  EXPECT_EQ(std::get<std::string>(all[0].values[1]), "Paulie Gee's");
}

TEST_F(ExportSelectionTest, SaveAsOntoATwoTableFileAsksAndCanKeepTheOtherTables) {
  auto pizza = OpenGeoJson(PizzaLayer(), "pizza.geojson");
  auto rivers = OpenGeoJson(RiversLayer(), "rivers.geojson");
  const std::string trip = scratch_.Path("trip.gpkg");
  Export(pizza, {1, 2, 3}, trip, {0});
  Export(rivers, {1, 2}, trip, {1});
  ASSERT_EQ(Tables(trip).size(), 2u);

  // Cancelled: nothing written.
  auto fresh = OpenGeoJson(PizzaLayer(), "more.geojson");
  fd_.desk().overlays().MakeCurrent(fresh);
  fd_.shell().save_spec = trip;
  fd_.shell().save_format = FeatureOverlay::kFormatGeoPackage;
  fd_.shell().list_choice = std::nullopt;
  ASSERT_TRUE(fd_.Run({"file.save_as"}).ok());
  ASSERT_EQ(fd_.shell().list_rows.size(), 2u);
  EXPECT_EQ(Tables(trip).size(), 2u);

  // Added as a layer: the other tables stay and the document now names its table.
  fd_.shell().list_choice = 0;
  ASSERT_TRUE(fd_.Run({"file.save_as"}).ok());
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  EXPECT_EQ(Tables(trip).size(), 3u);
  const fv::app::SourceSpec spec = fv::app::SplitSourceSpec(fresh->file_spec());
  EXPECT_EQ(spec.path, trip);
  EXPECT_FALSE(spec.layer.empty());
  EXPECT_FALSE(fresh->is_dirty());
  EXPECT_EQ(fresh->layer()->Count(), 3u);

  // A one-table file is replaced without the question, as before.
  const std::string single = scratch_.Path("single.gpkg");
  Export(pizza, {1}, single, {0});
  fd_.desk().overlays().MakeCurrent(rivers);
  fd_.shell().save_spec = single;
  fd_.shell().list_rows.clear();
  ASSERT_TRUE(fd_.Run({"file.save_as"}).ok());
  EXPECT_TRUE(fd_.shell().list_rows.empty());
  ASSERT_EQ(Tables(single).size(), 1u);
}

TEST_F(ExportSelectionTest, CommandNeedsASelectionAndSitsOnTheLayerMenuAndPalette) {
  auto pizza = OpenGeoJson(PizzaLayer(), "pizza.geojson");
  pizza->SetSelection({});
  EXPECT_FALSE(fd_.desk().commands().IsEnabled("overlay.export_selection"));
  pizza->SetSelection({1});
  EXPECT_TRUE(fd_.desk().commands().IsEnabled("overlay.export_selection"));
  bool listed = false;
  for (const fv::desk::MenuItem& m : fd_.desk().LayerMenu()) listed = listed || m.command_id == "overlay.export_selection";
  EXPECT_TRUE(listed);
}

}  // namespace
