// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "fvkit/feature/geojson.h"

namespace {

using fv::FieldRole;
using fv::FieldType;
using fv::GeometryType;
using fv::MemoryFeatureLayer;

std::shared_ptr<MemoryFeatureLayer> Read(const std::string& text) {
  std::shared_ptr<MemoryFeatureLayer> layer;
  const fv::Status s = fv::ReadGeoJson(text, "test", &layer);
  EXPECT_TRUE(s.ok()) << s.message;
  return layer;
}

fv::Status ReadStatus(const std::string& text) {
  std::shared_ptr<MemoryFeatureLayer> layer;
  return fv::ReadGeoJson(text, "test", &layer);
}

const fv::FieldDef& Field(const MemoryFeatureLayer& layer, const char* name) {
  const int i = layer.Schema().Find(name);
  EXPECT_GE(i, 0) << name;
  return layer.Schema().fields[i < 0 ? 0 : i];
}

// Pizza restaurants as another tool would write them: no schema member.
const char* kPizza = R"({
  "type": "FeatureCollection",
  "features": [
    {"type": "Feature", "geometry": {"type": "Point", "coordinates": [-79.93, 32.78]},
     "properties": {"name": "Andolini's", "phone": "(843) 722-7437", "style": "New York",
                    "rating": 4, "website": "https://andolinis.com", "delivery": true,
                    "opened": "1996-05-01"}},
    {"type": "Feature", "geometry": {"type": "Point", "coordinates": [-79.95, 32.80]},
     "properties": {"name": "Paulie Gee's", "phone": "843 555 0100", "style": "Neapolitan",
                    "rating": 4.5, "website": "http://pauliegee.example", "delivery": false,
                    "opened": "2019-03-15", "notes": {"oven": "wood"}}}
  ]
})";

TEST(FeatureGeoJson, TypesAreInferredWithoutASchema) {
  auto layer = Read(kPizza);
  ASSERT_NE(layer, nullptr);
  EXPECT_EQ(layer->Name(), "test");
  EXPECT_EQ(layer->Count(), 2u);
  EXPECT_EQ(layer->Schema().geometry_type, GeometryType::kPoint);
  EXPECT_EQ(Field(*layer, "name").type, FieldType::kText);
  EXPECT_EQ(Field(*layer, "phone").role, FieldRole::kPhone);
  EXPECT_EQ(Field(*layer, "rating").type, FieldType::kReal);  // 4 and 4.5
  EXPECT_EQ(Field(*layer, "website").role, FieldRole::kUrl);
  EXPECT_EQ(Field(*layer, "delivery").type, FieldType::kBoolean);
  EXPECT_EQ(Field(*layer, "opened").type, FieldType::kDate);
  // An object value is kept as its JSON text.
  EXPECT_EQ(Field(*layer, "notes").type, FieldType::kText);
  fv::Feature second;
  ASSERT_TRUE(layer->Get(2, &second).ok());
  const int notes = layer->Schema().Find("notes");
  EXPECT_EQ(std::get<std::string>(second.values[notes]), R"({"oven":"wood"})");
  EXPECT_DOUBLE_EQ(second.geometry.paths[0][0].lat, 32.80);
  EXPECT_DOUBLE_EQ(second.geometry.paths[0][0].lon, -79.95);
}

TEST(FeatureGeoJson, MixedValuesFallBackToText) {
  auto layer = Read(R"({"type": "FeatureCollection", "features": [
    {"type": "Feature", "geometry": null, "properties": {"code": 12, "when": "2026-01-01"}},
    {"type": "Feature", "geometry": null, "properties": {"code": "A7", "when": "2026-01-01T09:00Z"}},
    {"type": "Feature", "geometry": null, "properties": {"code": null, "empty": null}}]})");
  ASSERT_NE(layer, nullptr);
  EXPECT_EQ(Field(*layer, "code").type, FieldType::kText);
  EXPECT_EQ(Field(*layer, "when").type, FieldType::kText);  // date and datetime mixed
  EXPECT_EQ(Field(*layer, "empty").type, FieldType::kText);
  EXPECT_EQ(layer->Schema().geometry_type, GeometryType::kGeometry);
  fv::Feature first;
  ASSERT_TRUE(layer->Get(1, &first).ok());
  EXPECT_EQ(std::get<std::string>(first.values[0]), "12");
  EXPECT_TRUE(first.geometry.IsNull());
}

// Historical monuments, written by us: the schema member carries what
// inference cannot see — titles, units, choices, a date range.
fv::FeatureSchema MonumentSchema() {
  fv::FeatureSchema s;
  s.geometry_type = GeometryType::kPoint;
  fv::FieldDef name;
  name.name = "name";
  name.title = "Name";
  name.required = true;
  fv::FieldDef built;
  built.name = "built";
  built.type = FieldType::kDate;
  built.title = "Date built";
  fv::FieldDef from;
  from.name = "commemorates_from";
  from.type = FieldType::kDate;
  from.title = "Commemorates";
  from.range_end = "commemorates_to";
  fv::FieldDef to;
  to.name = "commemorates_to";
  to.type = FieldType::kDate;
  fv::FieldDef kind;
  kind.name = "kind";
  kind.role = FieldRole::kEnumeration;
  kind.choices = {{"statue", "Statue"}, {"obelisk", "Obelisk"}, {"plaque", ""}};
  kind.default_value = std::string("plaque");
  fv::FieldDef height;
  height.name = "height_ft";
  height.type = FieldType::kReal;
  height.role = FieldRole::kMeasurement;
  height.unit = "ft";
  height.min = 0.0;
  s.fields = {name, built, from, to, kind, height};
  return s;
}

TEST(FeatureGeoJson, OurSchemaRoundTrips) {
  MemoryFeatureLayer layer("Monuments", MonumentSchema());
  fv::Feature f;
  f.geometry = fv::Geometry::Point(fv::GeoPoint{32.7765, -79.9311});
  f.values = {std::string("Fort Sumter memorial"), std::string("1932-04-12"),
              std::string("1861-04-12"), std::string("1861-04-13"), std::string("obelisk"), 21.5};
  ASSERT_TRUE(layer.Insert(&f).ok());
  fv::Feature g;
  g.geometry = fv::Geometry::Point(fv::GeoPoint{32.79, -79.94});
  g.values = {std::string("Unnamed plaque")};
  ASSERT_TRUE(layer.Insert(&g).ok());

  std::string text;
  ASSERT_TRUE(fv::WriteGeoJson(layer, &text).ok());
  auto back = Read(text);
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->Name(), "Monuments");
  EXPECT_EQ(back->Schema(), layer.Schema());
  std::vector<fv::Feature> a, b;
  ASSERT_TRUE(layer.All(&a).ok());
  ASSERT_TRUE(back->All(&b).ok());
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].id, b[i].id);
    EXPECT_EQ(a[i].geometry, b[i].geometry);
    EXPECT_EQ(a[i].values, b[i].values);
  }
}

// Rivers as lines, with a Z on one of them, written to a file and back.
TEST(FeatureGeoJson, RiversRoundTripThroughAFile) {
  auto layer = Read(R"({"type": "FeatureCollection", "name": "Rivers", "features": [
    {"type": "Feature", "id": 10, "geometry": {"type": "LineString",
      "coordinates": [[-80.10, 32.60], [-80.05, 32.62], [-80.00, 32.65]]},
     "properties": {"name": "Kiawah River", "depth_m": 3.2, "flow_m3s": 12.0}},
    {"type": "Feature", "id": 11, "geometry": {"type": "MultiLineString",
      "coordinates": [[[-80.2, 32.7, 1.0], [-80.1, 32.7, 2.0]], [[-80.1, 32.7], [-80.0, 32.8]]]},
     "properties": {"name": "Stono River", "depth_m": 5, "flow_m3s": 40.5}}]})");
  ASSERT_NE(layer, nullptr);
  EXPECT_EQ(layer->Name(), "Rivers");
  EXPECT_EQ(layer->Schema().geometry_type, GeometryType::kGeometry);  // line and multiline
  EXPECT_EQ(Field(*layer, "depth_m").type, FieldType::kReal);
  EXPECT_EQ(layer->Schema().Find("name"), 0);  // fields in file order, not sorted
  fv::Feature stono;
  ASSERT_TRUE(layer->Get(11, &stono).ok());  // the file's ids are kept
  ASSERT_EQ(stono.geometry.parts.size(), 2u);
  EXPECT_TRUE(stono.geometry.has_z);
  EXPECT_TRUE(stono.geometry.parts[1].has_z);  // read as z = 0
  EXPECT_EQ(stono.geometry.parts[0].paths[0][1].z, 2.0);
  EXPECT_EQ(std::get<double>(stono.values[1]), 5.0);  // 5 in a real field

  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "fv_feature_geojson_rivers.geojson";
  ASSERT_TRUE(fv::WriteGeoJsonFile(*layer, path.string()).ok());
  std::shared_ptr<MemoryFeatureLayer> back;
  ASSERT_TRUE(fv::ReadGeoJsonFile(path.string(), &back).ok());
  std::filesystem::remove(path);
  EXPECT_EQ(back->Schema(), layer->Schema());
  fv::Feature again;
  ASSERT_TRUE(back->Get(11, &again).ok());
  EXPECT_EQ(again.geometry, stono.geometry);
  fv::GeoRect box;
  ASSERT_TRUE(back->Extent(&box).ok());
  EXPECT_DOUBLE_EQ(box.ll.lon, -80.2);
  EXPECT_DOUBLE_EQ(box.ur.lat, 32.8);
}

TEST(FeatureGeoJson, RingsAreWrittenRightHanded) {
  // Exterior clockwise, hole counter-clockwise: both get reversed.
  MemoryFeatureLayer layer("areas", fv::FeatureSchema{});
  fv::Feature f;
  f.geometry = fv::Geometry::Polygon({{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                                      {{2, 2}, {2, 4}, {4, 4}, {4, 2}}});
  ASSERT_TRUE(layer.Insert(&f).ok());
  std::string text;
  ASSERT_TRUE(fv::WriteGeoJson(layer, &text).ok());
  auto back = Read(text);
  fv::Feature g;
  ASSERT_TRUE(back->Get(1, &g).ok());
  const fv::Path& outer = g.geometry.paths[0];
  // Counter-clockwise in lon/lat from (0,0): east first.
  EXPECT_EQ(outer[1].lon, 10);
  EXPECT_EQ(outer[1].lat, 0);
  const fv::Path& hole = g.geometry.paths[1];
  EXPECT_EQ(hole[1].lon, 2);
  EXPECT_EQ(hole[1].lat, 4);
}

TEST(FeatureGeoJson, SingleFeaturesAndBareGeometriesRead) {
  auto one = Read(R"({"type": "Feature", "geometry": {"type": "Point", "coordinates": [1, 2]},
                     "properties": {"a": 1}})");
  ASSERT_NE(one, nullptr);
  EXPECT_EQ(one->Count(), 1u);
  auto bare = Read(R"({"type": "Polygon", "coordinates": [[[0, 0], [1, 0], [1, 1]]]})");
  ASSERT_NE(bare, nullptr);
  fv::Feature f;
  ASSERT_TRUE(bare->Get(1, &f).ok());
  EXPECT_EQ(f.geometry.paths[0].size(), 4u);  // the open ring was closed
  EXPECT_TRUE(bare->Schema().fields.empty());
}

TEST(FeatureGeoJson, AnEmptyMemberPointRoundTrips) {
  MemoryFeatureLayer layer("points", fv::FeatureSchema{});
  fv::Geometry empty;
  empty.type = GeometryType::kPoint;
  fv::Feature f;
  f.geometry = fv::Geometry::Collection(GeometryType::kMultiPoint,
                                        {fv::Geometry::Point(fv::GeoPoint{2, 1}), empty});
  ASSERT_TRUE(layer.Insert(&f).ok());
  std::string text;
  ASSERT_TRUE(fv::WriteGeoJson(layer, &text).ok());
  auto back = Read(text);
  ASSERT_NE(back, nullptr);
  fv::Feature g;
  ASSERT_TRUE(back->Get(1, &g).ok());
  EXPECT_EQ(g.geometry, f.geometry);
}

TEST(FeatureGeoJson, DuplicateIdsAreRenumbered) {
  auto layer = Read(R"({"type": "FeatureCollection", "features": [
    {"type": "Feature", "id": 5, "geometry": null, "properties": {}},
    {"type": "Feature", "id": 5, "geometry": null, "properties": {}}]})");
  std::vector<fv::Feature> all;
  ASSERT_TRUE(layer->All(&all).ok());
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].id, 1);
  EXPECT_EQ(all[1].id, 2);
}

TEST(FeatureGeoJson, BadInputIsRefusedWithTheFeatureNamed) {
  EXPECT_EQ(ReadStatus("{not json").code, fv::kInvalidArg);
  EXPECT_EQ(ReadStatus("[]").code, fv::kInvalidArg);
  const fv::Status s = ReadStatus(R"({"type": "FeatureCollection", "features": [
    {"type": "Feature", "geometry": {"type": "Point", "coordinates": [1, 2]}, "properties": {}},
    {"type": "Feature", "geometry": {"type": "Point", "coordinates": ["x", 2]}, "properties": {}}]})");
  EXPECT_EQ(s.code, fv::kInvalidArg);
  EXPECT_NE(s.message.find("feature 2"), std::string::npos) << s.message;
  EXPECT_EQ(ReadStatus(R"({"type": "Feature", "geometry": {"type": "Curve"}})").code,
            fv::kInvalidArg);
  // A value that contradicts our own schema member.
  EXPECT_EQ(ReadStatus(R"({"type": "FeatureCollection",
    "fv_schema": {"version": 1, "fields": [{"name": "n", "type": "integer"}]},
    "features": [{"type": "Feature", "geometry": null, "properties": {"n": "three"}}]})").code,
            fv::kInvalidArg);
}

TEST(FeatureGeoJson, BlobFieldsCannotBeWritten) {
  fv::FeatureSchema schema;
  fv::FieldDef blob;
  blob.name = "photo";
  blob.type = FieldType::kBlob;
  schema.fields = {blob};
  MemoryFeatureLayer layer("photos", schema);
  std::string text;
  EXPECT_EQ(fv::WriteGeoJson(layer, &text).code, fv::kUnsupported);
}

}  // namespace
