// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "fvkit/detail/sqlite.h"
#include "fvkit/feature/geopackage.h"

namespace {

using fv::Feature;
using fv::FeatureSchema;
using fv::FieldDef;
using fv::FieldRole;
using fv::FieldType;
using fv::GeoPackage;
using fv::GeoPackageFeatureLayer;
using fv::Geometry;
using fv::GeometryType;
using fv::GeoPoint;
using fv::GeoRect;

namespace fs = std::filesystem;

/// A fresh path in the temp directory, removed when the fixture ends.
class FeatureGeoPackage : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = (fs::temp_directory_path() / (std::string("fv_gpkg_") + info->name() + ".gpkg")).string();
    fs::remove(path_);
  }
  void TearDown() override { fs::remove(path_); }

  std::shared_ptr<GeoPackage> Create() {
    std::shared_ptr<GeoPackage> gp;
    const fv::Status s = GeoPackage::Create(path_, &gp);
    EXPECT_TRUE(s.ok()) << s.message;
    return gp;
  }
  std::shared_ptr<GeoPackage> Open(bool writable) {
    std::shared_ptr<GeoPackage> gp;
    const fv::Status s = GeoPackage::Open(path_, writable, &gp);
    EXPECT_TRUE(s.ok()) << s.message;
    return gp;
  }

  std::string path_;
};

FieldDef Field(const char* name, FieldType type = FieldType::kText) {
  FieldDef f;
  f.name = name;
  f.type = type;
  return f;
}

FeatureSchema MonumentSchema() {
  FeatureSchema s;
  s.geometry_type = GeometryType::kPoint;
  FieldDef name = Field("name");
  name.title = "Name";
  name.required = true;
  FieldDef built = Field("built", FieldType::kDate);
  built.title = "Date built";
  built.description = "Year the monument was dedicated";
  FieldDef from = Field("commemorates_from", FieldType::kDate);
  from.title = "Commemorates";
  from.range_end = "commemorates_to";
  FieldDef to = Field("commemorates_to", FieldType::kDate);
  FieldDef kind = Field("kind");
  kind.role = FieldRole::kEnumeration;
  kind.choices = {{"statue", "Statue"}, {"obelisk", "Obelisk"}, {"plaque", ""}};
  kind.default_value = std::string("plaque");
  s.fields = {name, built, from, to, kind};
  return s;
}

FeatureSchema PizzaSchema() {
  FeatureSchema s;
  s.geometry_type = GeometryType::kPoint;
  FieldDef name = Field("name");
  name.required = true;
  FieldDef phone = Field("phone");
  phone.role = FieldRole::kPhone;
  FieldDef style = Field("style");
  style.title = "Style of pizza";
  style.role = FieldRole::kEnumeration;
  style.choices = {{"neapolitan", "Neapolitan"}, {"new_york", "New York"}, {"detroit", "Detroit"}};
  FieldDef web = Field("website");
  web.role = FieldRole::kUrl;
  FieldDef delivery = Field("delivery", FieldType::kBoolean);
  delivery.default_value = false;
  FieldDef rating = Field("rating", FieldType::kInteger);
  rating.min = 1;
  rating.max = 5;
  s.fields = {name, phone, style, web, delivery, rating};
  return s;
}

FeatureSchema RiverSchema() {
  FeatureSchema s;
  s.geometry_type = GeometryType::kLineString;
  FieldDef name = Field("name");
  FieldDef depth = Field("depth", FieldType::kReal);
  depth.role = FieldRole::kMeasurement;
  depth.unit = "m";
  depth.min = 0;
  FieldDef flow = Field("flow", FieldType::kReal);
  flow.title = "Average flow";
  flow.role = FieldRole::kMeasurement;
  flow.unit = "m³/s";
  s.fields = {name, depth, flow};
  return s;
}

Feature Make(Geometry g, std::vector<fv::FieldValue> values) {
  Feature f;
  f.geometry = std::move(g);
  f.values = std::move(values);
  return f;
}

/// The three plan layers written into one file and saved.
void WriteExamples(GeoPackage& gp) {
  std::shared_ptr<GeoPackageFeatureLayer> monuments, pizza, rivers;
  fv::GeoPackageLayerOptions opts;
  opts.identifier = "Historical monuments";
  opts.description = "Charleston peninsula";
  ASSERT_TRUE(gp.CreateLayer("monuments", MonumentSchema(), opts, &monuments).ok());
  ASSERT_TRUE(gp.CreateLayer("pizza", PizzaSchema(), {}, &pizza).ok());
  ASSERT_TRUE(gp.CreateLayer("rivers", RiverSchema(), {}, &rivers).ok());

  Feature m = Make(Geometry::Point({32.7765, -79.9311}),
                   {std::string("Calhoun Monument"), std::string("1896-06-27"),
                    std::string("1782-03-18"), std::string("1850-03-31"), std::string("statue")});
  ASSERT_TRUE(monuments->Insert(&m).ok());
  Feature p1 = Make(Geometry::Point({32.78, -79.93}),
                    {std::string("Andolini's"), std::string("(843) 722-7437"),
                     std::string("new_york"), std::string("https://andolinis.com"), true,
                     int64_t{4}});
  Feature p2 = Make(Geometry::Point({32.80, -79.95}), {std::string("Paulie Gee's")});
  ASSERT_TRUE(pizza->Insert(&p1).ok());
  ASSERT_TRUE(pizza->Insert(&p2).ok());
  Feature r = Make(Geometry::LineString({{32.90, -79.97}, {32.85, -79.95}, {32.78, -79.92}}),
                   {std::string("Cooper River"), 12.5, 2300.0});
  ASSERT_TRUE(rivers->Insert(&r).ok());
  const fv::Status s = gp.Save();
  ASSERT_TRUE(s.ok()) << s.message;
}

int64_t Scalar(fv::detail::SqliteDb& db, const std::string& sql) {
  fv::detail::SqliteStmt st;
  EXPECT_TRUE(st.Prepare(db, sql.c_str()).ok()) << sql;
  return st.Step() ? st.ColInt64(0) : -999;
}

// MARK: Geometry blob

TEST(FeatureGpkgBlob, PointHasNoEnvelopeLineHasOne) {
  std::vector<uint8_t> blob;
  ASSERT_TRUE(fv::WriteGpkgGeometry(Geometry::Point({32.5, -79.5}), 4326, &blob).ok());
  ASSERT_EQ(blob.size(), 8u + 21u);
  EXPECT_EQ(blob[0], 'G');
  EXPECT_EQ(blob[1], 'P');
  EXPECT_EQ(blob[2], 0);
  EXPECT_EQ(blob[3], 0x01);  // little-endian, no envelope
  Geometry back;
  int32_t srs = 0;
  ASSERT_TRUE(fv::ReadGpkgGeometry(blob.data(), blob.size(), &back, &srs).ok());
  EXPECT_EQ(srs, 4326);
  EXPECT_EQ(back, Geometry::Point({32.5, -79.5}));

  const Geometry line = Geometry::LineString({{10, 20}, {11, 25}});
  ASSERT_TRUE(fv::WriteGpkgGeometry(line, 4326, &blob).ok());
  EXPECT_EQ(blob[3], 0x03);  // little-endian, x/y envelope
  double env[4];
  std::memcpy(env, blob.data() + 8, sizeof env);
  EXPECT_EQ(env[0], 20.0);  // minx = lon
  EXPECT_EQ(env[1], 25.0);
  EXPECT_EQ(env[2], 10.0);  // miny = lat
  EXPECT_EQ(env[3], 11.0);
  ASSERT_TRUE(fv::ReadGpkgGeometry(blob.data(), blob.size(), &back).ok());
  EXPECT_EQ(back, line);
}

TEST(FeatureGpkgBlob, EmptyGeometrySetsTheFlag) {
  Geometry empty;
  empty.type = GeometryType::kPoint;
  std::vector<uint8_t> blob;
  ASSERT_TRUE(fv::WriteGpkgGeometry(empty, 4326, &blob).ok());
  EXPECT_EQ(blob[3], 0x11);
  Geometry back;
  ASSERT_TRUE(fv::ReadGpkgGeometry(blob.data(), blob.size(), &back).ok());
  EXPECT_EQ(back.type, GeometryType::kPoint);
  EXPECT_TRUE(back.IsEmpty());
  EXPECT_FALSE(fv::WriteGpkgGeometry(Geometry{}, 4326, &blob).ok());
}

TEST(FeatureGpkgBlob, BigEndianHeaderAndRefusals) {
  // Big-endian header, srs 4326, x/y envelope, then a little-endian WKB point.
  std::vector<uint8_t> blob = {'G', 'P', 0, 0x02, 0x00, 0x00, 0x10, 0xE6};
  auto put_be = [&](double d) {
    uint8_t b[8];
    std::memcpy(b, &d, 8);
    for (int i = 7; i >= 0; --i) blob.push_back(b[i]);
  };
  put_be(-79.0);
  put_be(-79.0);
  put_be(32.0);
  put_be(32.0);
  std::vector<uint8_t> wkb;
  ASSERT_TRUE(fv::WriteWkb(Geometry::Point({32.0, -79.0}), &wkb).ok());
  blob.insert(blob.end(), wkb.begin(), wkb.end());
  Geometry g;
  int32_t srs = 0;
  ASSERT_TRUE(fv::ReadGpkgGeometry(blob.data(), blob.size(), &g, &srs).ok());
  EXPECT_EQ(srs, 4326);
  EXPECT_EQ(g, Geometry::Point({32.0, -79.0}));

  std::vector<uint8_t> extended = blob;
  extended[3] |= 0x20;
  EXPECT_EQ(fv::ReadGpkgGeometry(extended.data(), extended.size(), &g).code, fv::kUnsupported);
  std::vector<uint8_t> truncated(blob.begin(), blob.begin() + 20);
  EXPECT_EQ(fv::ReadGpkgGeometry(truncated.data(), truncated.size(), &g).code, fv::kInvalidArg);
  const uint8_t not_gpkg[] = {'X', 'P', 0, 1, 0, 0, 0, 0};
  EXPECT_EQ(fv::ReadGpkgGeometry(not_gpkg, sizeof not_gpkg, &g).code, fv::kInvalidArg);
}

// MARK: Layers

TEST_F(FeatureGeoPackage, ExampleLayersRoundTripWithTheirSchemas) {
  {
    auto gp = Create();
    ASSERT_NE(gp, nullptr);
    WriteExamples(*gp);
  }
  auto gp = Open(false);
  ASSERT_NE(gp, nullptr);
  std::vector<fv::GeoPackageLayerInfo> infos;
  ASSERT_TRUE(gp->Layers(&infos).ok());
  ASSERT_EQ(infos.size(), 3u);
  EXPECT_EQ(infos[0].table, "monuments");
  EXPECT_EQ(infos[0].identifier, "Historical monuments");
  EXPECT_EQ(infos[0].description, "Charleston peninsula");
  EXPECT_EQ(infos[2].geometry_type, GeometryType::kLineString);

  const std::pair<const char*, FeatureSchema> expected[] = {
      {"monuments", MonumentSchema()}, {"pizza", PizzaSchema()}, {"rivers", RiverSchema()}};
  for (const auto& e : expected) {
    std::shared_ptr<GeoPackageFeatureLayer> layer;
    const fv::Status s = gp->OpenLayer(e.first, &layer);
    ASSERT_TRUE(s.ok()) << s.message;
    const FeatureSchema& got = layer->Schema();
    ASSERT_EQ(got.fields.size(), e.second.fields.size()) << e.first;
    for (size_t i = 0; i < got.fields.size(); ++i)
      EXPECT_TRUE(got.fields[i] == e.second.fields[i]) << e.first << "." << got.fields[i].name;
    EXPECT_EQ(got.geometry_type, e.second.geometry_type);
  }

  std::shared_ptr<GeoPackageFeatureLayer> pizza;
  ASSERT_TRUE(gp->OpenLayer("pizza", &pizza).ok());
  EXPECT_EQ(pizza->Name(), "pizza");
  EXPECT_EQ(pizza->Count(), 2u);
  Feature andolinis;
  ASSERT_TRUE(pizza->Get(1, &andolinis).ok());
  EXPECT_EQ(std::get<std::string>(andolinis.values[1]), "(843) 722-7437");
  EXPECT_EQ(std::get<bool>(andolinis.values[4]), true);
  EXPECT_EQ(std::get<int64_t>(andolinis.values[5]), 4);
  EXPECT_DOUBLE_EQ(andolinis.geometry.paths[0][0].lat, 32.78);
  Feature paulie;
  ASSERT_TRUE(pizza->Get(2, &paulie).ok());
  EXPECT_TRUE(fv::IsNull(paulie.values[1]));
  EXPECT_TRUE(fv::IsNull(paulie.values[4]));  // the default is for forms, not inserts

  std::shared_ptr<GeoPackageFeatureLayer> rivers;
  ASSERT_TRUE(gp->OpenLayer("rivers", &rivers).ok());
  std::vector<Feature> all;
  ASSERT_TRUE(rivers->All(&all).ok());
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(all[0].geometry,
            Geometry::LineString({{32.90, -79.97}, {32.85, -79.95}, {32.78, -79.92}}));
  EXPECT_DOUBLE_EQ(std::get<double>(all[0].values[2]), 2300.0);
  GeoRect box;
  ASSERT_TRUE(rivers->Extent(&box).ok());
  EXPECT_DOUBLE_EQ(box.ll.lat, 32.78);
  EXPECT_DOUBLE_EQ(box.ur.lon, -79.92);
}

// The OGC validator's core requirements for a features GeoPackage, checked
// directly against the file (GDAL's validate_gpkg.py is not in this build).
TEST_F(FeatureGeoPackage, WrittenFilePassesCoreChecks) {
  {
    auto gp = Create();
    ASSERT_NE(gp, nullptr);
    WriteExamples(*gp);
  }
  fv::detail::SqliteDb db;
  ASSERT_TRUE(db.OpenReadOnly(path_).ok());
  // Req 2: application_id 'GPKG', user_version 1.2.
  EXPECT_EQ(Scalar(db, "PRAGMA application_id"), 0x47504B47);
  EXPECT_EQ(Scalar(db, "PRAGMA user_version"), 10200);
  // Req 6: integrity and foreign keys.
  {
    fv::detail::SqliteStmt st;
    ASSERT_TRUE(st.Prepare(db, "PRAGMA integrity_check").ok());
    ASSERT_TRUE(st.Step());
    EXPECT_EQ(st.ColText(0), "ok");
  }
  EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM pragma_foreign_key_check"), 0);
  // Req 11: the three required SRS rows.
  EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM gpkg_spatial_ref_sys WHERE srs_id IN (4326, 0, -1)"), 3);
  EXPECT_EQ(Scalar(db, "SELECT organization_coordsys_id FROM gpkg_spatial_ref_sys "
                       "WHERE srs_id = 4326 AND organization = 'EPSG'"),
            4326);
  // Req 22/27/29: each features row has a table and one geometry column of the declared type.
  EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM gpkg_contents c LEFT JOIN gpkg_geometry_columns g "
                       "ON g.table_name = c.table_name WHERE c.data_type = 'features' "
                       "AND g.table_name IS NULL"),
            0);
  for (const char* table : {"monuments", "pizza", "rivers"}) {
    const std::string t = table;
    SCOPED_TRACE(t);
    // Req 29: the declared type of the geometry column is the geometry type name.
    fv::detail::SqliteStmt info;
    ASSERT_TRUE(info.Prepare(db, ("SELECT type, pk FROM pragma_table_info('" + t +
                                  "') WHERE name = 'geom'").c_str())
                    .ok());
    ASSERT_TRUE(info.Step());
    EXPECT_EQ(info.ColText(0), t == "rivers" ? "LINESTRING" : "POINT");
    // Req 29: an INTEGER PRIMARY KEY AUTOINCREMENT id.
    EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM pragma_table_info('" + t +
                             "') WHERE pk = 1 AND type = 'INTEGER'"),
              1);
    EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM sqlite_sequence WHERE name = '" + t + "'"), 1);
    // Req 19/20/33: every blob is GP version 0, srs 4326, of the column's type.
    fv::detail::SqliteStmt geoms;
    ASSERT_TRUE(geoms.Prepare(db, ("SELECT geom FROM \"" + t + "\"").c_str()).ok());
    while (geoms.Step()) {
      Geometry g;
      int32_t srs = 0;
      const auto* data = static_cast<const uint8_t*>(geoms.ColBlob(0));
      ASSERT_TRUE(fv::ReadGpkgGeometry(data, geoms.ColBytes(0), &g, &srs).ok());
      EXPECT_EQ(srs, 4326);
      EXPECT_EQ(g.type, t == "rivers" ? GeometryType::kLineString : GeometryType::kPoint);
    }
    // R-tree extension: table, extension row, six triggers, one row per geometry.
    const std::string rtree = "rtree_" + t + "_geom";
    EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM gpkg_extensions WHERE table_name = '" + t +
                             "' AND column_name = 'geom' AND extension_name = 'gpkg_rtree_index'"),
              1);
    EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger' AND "
                         "tbl_name = '" + t + "' AND name LIKE '" + rtree + "_%'"),
              6);
    EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM \"" + rtree + "\""),
              Scalar(db, "SELECT COUNT(*) FROM \"" + t + "\" WHERE geom IS NOT NULL"));
    // gpkg_contents bounds were brought up to date on Save.
    EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM gpkg_contents WHERE table_name = '" + t +
                             "' AND min_x IS NOT NULL AND srs_id = 4326"),
              1);
  }
  // Schema extension: every data column names a real column, every constraint exists.
  EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM gpkg_extensions WHERE extension_name = 'gpkg_schema'"), 2);
  EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM gpkg_data_columns d WHERE d.constraint_name IS NOT NULL "
                       "AND NOT EXISTS (SELECT 1 FROM gpkg_data_column_constraints c "
                       "WHERE c.constraint_name = d.constraint_name)"),
            0);
  EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM gpkg_data_column_constraints "
                       "WHERE constraint_name = 'pizza_style' AND constraint_type = 'enum'"),
            3);
  EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM gpkg_data_column_constraints "
                       "WHERE constraint_name = 'pizza_rating' AND constraint_type = 'range' "
                       "AND min = 1 AND max = 5 AND min_is_inclusive AND max_is_inclusive"),
            1);
  {
    fv::detail::SqliteStmt st;
    ASSERT_TRUE(st.Prepare(db, "SELECT name, title FROM gpkg_data_columns "
                               "WHERE table_name = 'rivers' AND column_name = 'flow'").ok());
    ASSERT_TRUE(st.Step());
    EXPECT_EQ(st.ColText(0), "Average flow");  // GDAL's alias
    EXPECT_EQ(st.ColText(1), "Average flow");
  }
}

TEST_F(FeatureGeoPackage, RtreeFollowsEditsAndQueries) {
  auto gp = Create();
  ASSERT_NE(gp, nullptr);
  std::shared_ptr<GeoPackageFeatureLayer> layer;
  FeatureSchema schema;
  schema.geometry_type = GeometryType::kGeometry;
  schema.fields = {Field("name")};
  ASSERT_TRUE(gp->CreateLayer("places", schema, {}, &layer).ok());

  Feature tokyo = Make(Geometry::Point({35.7, 139.7}), {std::string("Tokyo")});
  Feature fiji = Make(Geometry::Point({-17.8, 178.4}), {std::string("Suva")});
  Feature samoa = Make(Geometry::Point({-13.8, -171.8}), {std::string("Apia")});
  Feature nowhere = Make(Geometry{}, {std::string("no geometry")});
  for (Feature* f : {&tokyo, &fiji, &samoa, &nowhere}) ASSERT_TRUE(layer->Insert(f).ok());
  EXPECT_EQ(tokyo.id, 1);
  EXPECT_EQ(nowhere.id, 4);

  std::vector<Feature> hits;
  // A box across the antimeridian finds Suva and Apia.
  ASSERT_TRUE(layer->Query(GeoRect{{-20, 170}, {-10, -170}}, &hits).ok());
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].id, fiji.id);
  EXPECT_EQ(hits[1].id, samoa.id);

  // Move Tokyo to Osaka: the index follows the update.
  tokyo.geometry = Geometry::Point({34.7, 135.5});
  ASSERT_TRUE(layer->Update(tokyo).ok());
  ASSERT_TRUE(layer->Query(GeoRect{{35.5, 139.5}, {36, 140}}, &hits).ok());
  EXPECT_TRUE(hits.empty());
  ASSERT_TRUE(layer->Query(GeoRect{{34.5, 135}, {35, 136}}, &hits).ok());
  ASSERT_EQ(hits.size(), 1u);

  // Clearing a geometry and deleting a feature both leave the index.
  fiji.geometry = Geometry{};
  ASSERT_TRUE(layer->Update(fiji).ok());
  ASSERT_TRUE(layer->Delete(samoa.id).ok());
  ASSERT_TRUE(layer->Query(GeoRect::World(), &hits).ok());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].id, tokyo.id);
  EXPECT_EQ(layer->Count(), 3u);
  EXPECT_EQ(layer->Delete(samoa.id).code, fv::kNotFound);
  EXPECT_EQ(layer->Update(samoa).code, fv::kNotFound);
  ASSERT_TRUE(gp->Save().ok());

  fv::detail::SqliteDb db;
  ASSERT_TRUE(db.OpenReadOnly(path_).ok());
  EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM rtree_places_geom"), 1);
  EXPECT_EQ(Scalar(db, "SELECT id FROM rtree_places_geom"), tokyo.id);
}

TEST_F(FeatureGeoPackage, SaveCommitsAndRevertDiscards) {
  {
    auto gp = Create();
    ASSERT_NE(gp, nullptr);
    EXPECT_FALSE(gp->HasUnsavedChanges());
    WriteExamples(*gp);
    EXPECT_FALSE(gp->HasUnsavedChanges());
  }
  auto gp = Open(true);
  ASSERT_NE(gp, nullptr);
  std::shared_ptr<GeoPackageFeatureLayer> pizza;
  ASSERT_TRUE(gp->OpenLayer("pizza", &pizza).ok());
  EXPECT_EQ(pizza->Delete(999).code, fv::kNotFound);
  EXPECT_FALSE(gp->HasUnsavedChanges());  // a failed edit changes nothing
  Feature f;
  ASSERT_TRUE(pizza->Get(2, &f).ok());
  f.values[1] = std::string("843 555 0100");
  ASSERT_TRUE(pizza->Update(f).ok());
  ASSERT_TRUE(pizza->Delete(1).ok());
  EXPECT_TRUE(gp->HasUnsavedChanges());
  EXPECT_EQ(pizza->Count(), 1u);  // edits are visible before Save
  ASSERT_TRUE(gp->Revert().ok());
  EXPECT_FALSE(gp->HasUnsavedChanges());
  EXPECT_EQ(pizza->Count(), 2u);
  ASSERT_TRUE(pizza->Get(2, &f).ok());
  EXPECT_TRUE(fv::IsNull(f.values[1]));

  // A layer created and reverted is gone.
  std::shared_ptr<GeoPackageFeatureLayer> scratch;
  ASSERT_TRUE(gp->CreateLayer("scratch", RiverSchema(), {}, &scratch).ok());
  ASSERT_TRUE(gp->Revert().ok());
  std::vector<fv::GeoPackageLayerInfo> infos;
  ASSERT_TRUE(gp->Layers(&infos).ok());
  EXPECT_EQ(infos.size(), 3u);
  std::vector<Feature> none;
  EXPECT_FALSE(scratch->All(&none).ok());

  f.values[1] = std::string("843 555 0100");
  ASSERT_TRUE(pizza->Update(f).ok());
  ASSERT_TRUE(gp->Save().ok());
  gp.reset();
  pizza.reset();
  scratch.reset();

  auto ro = Open(false);
  ASSERT_NE(ro, nullptr);
  ASSERT_TRUE(ro->OpenLayer("pizza", &pizza).ok());
  ASSERT_TRUE(pizza->Get(2, &f).ok());
  EXPECT_EQ(std::get<std::string>(f.values[1]), "843 555 0100");
  EXPECT_EQ(pizza->Delete(2).code, fv::kUnsupported);
  EXPECT_EQ(ro->CreateLayer("more", RiverSchema(), {}, nullptr).code, fv::kUnsupported);
}

TEST_F(FeatureGeoPackage, ALayerSavesWithoutTheOthersEdits) {
  {
    auto gp = Create();
    ASSERT_NE(gp, nullptr);
    WriteExamples(*gp);
  }
  auto gp = Open(true);
  ASSERT_NE(gp, nullptr);
  std::shared_ptr<GeoPackageFeatureLayer> pizza, rivers;
  ASSERT_TRUE(gp->OpenLayer("pizza", &pizza).ok());
  ASSERT_TRUE(gp->OpenLayer("rivers", &rivers).ok());
  ASSERT_TRUE(pizza->Delete(1).ok());
  Feature river;
  ASSERT_TRUE(rivers->Get(1, &river).ok());
  river.values[0] = std::string("Cooper");
  ASSERT_TRUE(rivers->Update(river).ok());

  ASSERT_TRUE(pizza->Save().ok());
  EXPECT_FALSE(pizza->HasEdits());
  EXPECT_TRUE(rivers->HasEdits());
  EXPECT_TRUE(gp->HasUnsavedChanges());
  {
    fv::detail::SqliteDb db;
    ASSERT_TRUE(db.OpenReadOnly(path_).ok());
    EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM pizza"), 1);
    EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM rivers WHERE name = 'Cooper'"), 0);
  }

  // A schema change after a move rebuilds the table on save; the R-tree follows the move.
  river.geometry = Geometry::LineString({{10.0, 20.0}, {10.5, 20.5}});
  ASSERT_TRUE(rivers->Update(river).ok());
  fv::SchemaChange change;
  change.schema = RiverSchema();
  change.schema.fields[0].name = "label";
  change.source = {0, 1, 2};
  ASSERT_TRUE(rivers->ChangeSchema(change).ok());
  EXPECT_EQ(rivers->Schema().fields[0].name, "label");
  ASSERT_TRUE(rivers->Save().ok());
  EXPECT_FALSE(gp->HasUnsavedChanges());
  fv::detail::SqliteDb db;
  ASSERT_TRUE(db.OpenReadOnly(path_).ok());
  EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM rivers WHERE label = 'Cooper'"), 1);
  EXPECT_EQ(Scalar(db, "SELECT COUNT(*) FROM rtree_rivers_geom WHERE minx >= 19.9 AND maxx <= 20.6"), 1);
  std::vector<Feature> hits;
  ASSERT_TRUE(rivers->Query(GeoRect{{9.9, 19.9}, {10.6, 20.6}}, &hits).ok());
  EXPECT_EQ(hits.size(), 1u);
}

TEST_F(FeatureGeoPackage, CopiesAMemoryLayerKeepingIdsAndZ) {
  FeatureSchema schema = RiverSchema();
  fv::MemoryFeatureLayer src("Rivers of the Lowcountry", schema);
  Feature a = Make(Geometry::LineString({{32.9, -79.9}, {32.8, -79.8}}), {std::string("Ashley")});
  a.id = 7;
  a.geometry.has_z = true;
  a.geometry.paths[0][0].z = 3.0;
  Feature b = Make(Geometry::LineString({{33.0, -80.0}, {32.9, -80.1}}), {std::string("Stono")});
  b.id = 12;
  b.geometry.has_z = true;
  ASSERT_TRUE(src.InsertWithId(a).ok());
  ASSERT_TRUE(src.InsertWithId(b).ok());

  auto gp = Create();
  ASSERT_NE(gp, nullptr);
  std::shared_ptr<GeoPackageFeatureLayer> layer;
  const fv::Status s = gp->WriteLayer(src, "rivers", &layer);
  ASSERT_TRUE(s.ok()) << s.message;
  ASSERT_TRUE(gp->Save().ok());
  EXPECT_EQ(layer->Name(), "Rivers of the Lowcountry");
  Feature back;
  ASSERT_TRUE(layer->Get(7, &back).ok());
  EXPECT_EQ(back.geometry, a.geometry);
  ASSERT_TRUE(layer->Get(12, &back).ok());
  // A 2-D line in a Z layer is refused; a new feature gets the next id.
  Feature flat = Make(Geometry::LineString({{1, 1}, {2, 2}}), {std::string("flat")});
  EXPECT_EQ(layer->Insert(&flat).code, fv::kInvalidArg);
  flat.geometry.has_z = true;
  ASSERT_TRUE(layer->Insert(&flat).ok());
  EXPECT_EQ(flat.id, 13);
}

TEST_F(FeatureGeoPackage, OneLayerReadsFromTwoThreadsAtOnce) {
  fv::MemoryFeatureLayer src("Rivers", RiverSchema());
  for (int i = 0; i < 2000; ++i) {
    Feature f = Make(Geometry::LineString({{32.0 + i * 1e-4, -80.0}, {32.1, -79.9}}),
                     {std::string("river ") + std::to_string(i)});
    ASSERT_TRUE(src.Insert(&f).ok());
  }
  auto gp = Create();
  ASSERT_NE(gp, nullptr);
  std::shared_ptr<GeoPackageFeatureLayer> layer;
  ASSERT_TRUE(gp->WriteLayer(src, "rivers", &layer).ok());
  ASSERT_TRUE(gp->Save().ok());

  // The render thread scans the layer while the UI thread fetches features
  // from it, as a frame and the toolbar's tool state do.
  std::atomic<bool> scan_ok{true};
  std::thread scan([&] {
    for (int pass = 0; pass < 10; ++pass) {
      std::vector<Feature> all;
      if (!layer->All(&all).ok() || all.size() != 2000) scan_ok = false;
    }
  });
  bool get_ok = true;
  for (int i = 0; i < 4000; ++i) {
    Feature f;
    if (!layer->Get(1 + i % 2000, &f).ok()) get_ok = false;
  }
  scan.join();
  EXPECT_TRUE(scan_ok);
  EXPECT_TRUE(get_ok);
}

TEST_F(FeatureGeoPackage, RefusesWhatTheLayerCannotHold) {
  auto gp = Create();
  ASSERT_NE(gp, nullptr);
  std::shared_ptr<GeoPackageFeatureLayer> pizza;
  ASSERT_TRUE(gp->CreateLayer("pizza", PizzaSchema(), {}, &pizza).ok());

  Feature line = Make(Geometry::LineString({{1, 1}, {2, 2}}), {std::string("x")});
  EXPECT_EQ(pizza->Insert(&line).code, fv::kInvalidArg);
  Feature unnamed = Make(Geometry::Point({1, 1}), {});
  EXPECT_EQ(pizza->Insert(&unnamed).code, fv::kInvalidArg);  // name is required
  Feature bad_style = Make(Geometry::Point({1, 1}), {std::string("x"), {}, std::string("chicago")});
  EXPECT_EQ(pizza->Insert(&bad_style).code, fv::kInvalidArg);
  Feature high = Make(Geometry::Point({1, 1}), {std::string("x"), {}, {}, {}, {}, int64_t{9}});
  EXPECT_EQ(pizza->Insert(&high).code, fv::kInvalidArg);
  Feature z = Make(Geometry::Point({1, 1}), {std::string("x")});
  z.geometry.has_z = true;
  EXPECT_EQ(pizza->Insert(&z).code, fv::kInvalidArg);
  EXPECT_EQ(pizza->InsertWithId(Make(Geometry::Point({1, 1}), {std::string("x")})).code,
            fv::kInvalidArg);  // id 0

  EXPECT_EQ(gp->CreateLayer("pizza", PizzaSchema(), {}, nullptr).code, fv::kInvalidArg);
  EXPECT_EQ(gp->CreateLayer("gpkg_mine", PizzaSchema(), {}, nullptr).code, fv::kInvalidArg);
  EXPECT_EQ(gp->CreateLayer("", PizzaSchema(), {}, nullptr).code, fv::kInvalidArg);
  EXPECT_EQ(gp->OpenLayer("nothing", &pizza).code, fv::kNotFound);
  std::shared_ptr<GeoPackage> again;
  EXPECT_EQ(GeoPackage::Create(path_, &again).code, fv::kInvalidArg);
}

TEST_F(FeatureGeoPackage, FieldsNamedFidAndGeomGetOtherColumns) {
  auto gp = Create();
  ASSERT_NE(gp, nullptr);
  FeatureSchema schema;
  schema.geometry_type = GeometryType::kPoint;
  schema.fields = {Field("fid", FieldType::kInteger), Field("geom")};
  std::shared_ptr<GeoPackageFeatureLayer> layer;
  ASSERT_TRUE(gp->CreateLayer("odd", schema, {}, &layer).ok());
  Feature f = Make(Geometry::Point({5, 6}), {int64_t{42}, std::string("wkt?")});
  ASSERT_TRUE(layer->Insert(&f).ok());
  ASSERT_TRUE(gp->Save().ok());
  ASSERT_TRUE(gp->OpenLayer("odd", &layer).ok());
  EXPECT_TRUE(layer->Schema() == schema);
  Feature back;
  ASSERT_TRUE(layer->Get(f.id, &back).ok());
  EXPECT_EQ(std::get<int64_t>(back.values[0]), 42);
  EXPECT_EQ(back.geometry, Geometry::Point({5, 6}));
}

// A file as another tool writes it: no fv metadata, spec types like TEXT(30)
// and MEDIUMINT, an enum constraint with labels, and a second SRS.
TEST_F(FeatureGeoPackage, ReadsAForeignFile) {
  {
    auto gp = Create();
    ASSERT_NE(gp, nullptr);
  }
  {
    fv::detail::SqliteDb db;
    ASSERT_TRUE(db.Open(path_).ok());
    std::vector<uint8_t> blob;
    ASSERT_TRUE(fv::WriteGpkgGeometry(Geometry::Point({51.5, -0.12}), 4326, &blob).ok());
    const fv::Status s = db.Exec(
        "CREATE TABLE gpkg_data_columns(table_name TEXT NOT NULL, column_name TEXT NOT NULL,"
        " name TEXT, title TEXT, description TEXT, mime_type TEXT, constraint_name TEXT);"
        "CREATE TABLE gpkg_data_column_constraints(constraint_name TEXT NOT NULL,"
        " constraint_type TEXT NOT NULL, value TEXT, min NUMERIC, min_is_inclusive BOOLEAN,"
        " max NUMERIC, max_is_inclusive BOOLEAN, description TEXT);"
        "CREATE TABLE pubs(fid INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, geom POINT,"
        " name TEXT(30) NOT NULL DEFAULT 'The Crown', seats MEDIUMINT DEFAULT 20,"
        " opened DATETIME DEFAULT CURRENT_TIMESTAMP, beer TEXT);"
        "INSERT INTO gpkg_contents(table_name, data_type, srs_id) VALUES('pubs','features',4326);"
        "INSERT INTO gpkg_geometry_columns VALUES('pubs','geom','POINT',4326,0,0);"
        "INSERT INTO gpkg_data_columns VALUES('pubs','beer','Beer on tap',NULL,'Cask only',NULL,'beers');"
        "INSERT INTO gpkg_data_column_constraints VALUES('beers','enum','ipa',NULL,NULL,NULL,NULL,'India pale ale');"
        "INSERT INTO gpkg_data_column_constraints VALUES('beers','enum','stout',NULL,NULL,NULL,NULL,NULL);"
        "INSERT INTO gpkg_spatial_ref_sys VALUES('OSGB 1936 / British National Grid',27700,"
        " 'EPSG',27700,'PROJCS[...]',NULL);"
        "CREATE TABLE grid(fid INTEGER PRIMARY KEY AUTOINCREMENT, geom POINT);"
        "INSERT INTO gpkg_contents(table_name, data_type, srs_id) VALUES('grid','features',27700);"
        "INSERT INTO gpkg_geometry_columns VALUES('grid','geom','POINT',27700,0,0);"
        "CREATE TABLE curves(fid INTEGER PRIMARY KEY AUTOINCREMENT, geom CURVEPOLYGON);"
        "INSERT INTO gpkg_contents(table_name, data_type, srs_id) VALUES('curves','features',4326);"
        "INSERT INTO gpkg_geometry_columns VALUES('curves','geom','CURVEPOLYGON',4326,0,0);");
    ASSERT_TRUE(s.ok()) << s.message;
    fv::detail::SqliteStmt ins;
    ASSERT_TRUE(ins.Prepare(db, "INSERT INTO pubs(geom, name, seats, beer) VALUES(?, 'The Lamb', 40, 'stout')").ok());
    ins.BindBlob(1, blob.data(), blob.size());
    ins.Step();
  }
  auto gp = Open(false);
  ASSERT_NE(gp, nullptr);
  std::shared_ptr<GeoPackageFeatureLayer> pubs;
  const fv::Status s = gp->OpenLayer("pubs", &pubs);
  ASSERT_TRUE(s.ok()) << s.message;
  const FeatureSchema& schema = pubs->Schema();
  ASSERT_EQ(schema.fields.size(), 4u);
  EXPECT_EQ(schema.fields[0].type, FieldType::kText);
  EXPECT_TRUE(schema.fields[0].required);
  EXPECT_EQ(std::get<std::string>(schema.fields[0].default_value), "The Crown");
  EXPECT_EQ(schema.fields[1].type, FieldType::kInteger);
  EXPECT_EQ(std::get<int64_t>(schema.fields[1].default_value), 20);
  EXPECT_EQ(schema.fields[2].type, FieldType::kDateTime);
  EXPECT_TRUE(fv::IsNull(schema.fields[2].default_value));  // an expression, not a value
  const FieldDef& beer = schema.fields[3];
  EXPECT_EQ(beer.title, "Beer on tap");
  EXPECT_EQ(beer.description, "Cask only");
  EXPECT_EQ(beer.role, FieldRole::kEnumeration);
  ASSERT_EQ(beer.choices.size(), 2u);
  EXPECT_EQ(beer.choices[0].label, "India pale ale");
  EXPECT_EQ(beer.choices[1].value, "stout");
  Feature lamb;
  ASSERT_TRUE(pubs->Get(1, &lamb).ok());
  EXPECT_EQ(std::get<int64_t>(lamb.values[1]), 40);
  std::vector<Feature> hits;
  ASSERT_TRUE(pubs->Query(GeoRect{{51, -1}, {52, 0}}, &hits).ok());  // no R-tree: scans
  EXPECT_EQ(hits.size(), 1u);

  std::shared_ptr<GeoPackageFeatureLayer> other;
  const fv::Status grid = gp->OpenLayer("grid", &other);
  EXPECT_EQ(grid.code, fv::kUnsupported);
  EXPECT_NE(grid.message.find("EPSG:27700"), std::string::npos) << grid.message;
  EXPECT_EQ(gp->OpenLayer("curves", &other).code, fv::kUnsupported);
}

TEST_F(FeatureGeoPackage, OpenRefusesNonGeoPackages) {
  {
    fv::detail::SqliteDb db;
    ASSERT_TRUE(db.Open(path_).ok());
    ASSERT_TRUE(db.Exec("CREATE TABLE t(x)").ok());
  }
  std::shared_ptr<GeoPackage> gp;
  EXPECT_EQ(GeoPackage::Open(path_, false, &gp).code, fv::kInvalidArg);
  EXPECT_EQ(GeoPackage::Open(path_ + ".missing", false, &gp).code, fv::kNotFound);
}

#ifdef FV_FEATURE_SAMPLE_GPKG
// Writes the three example layers where Chris can open them in QGIS.
TEST(FeatureGpkgSample, WritesTheExampleFile) {
  const std::string path = FV_FEATURE_SAMPLE_GPKG;
  fs::remove(path);
  std::shared_ptr<GeoPackage> gp;
  ASSERT_TRUE(GeoPackage::Create(path, &gp).ok());
  WriteExamples(*gp);
}
#endif

}  // namespace
