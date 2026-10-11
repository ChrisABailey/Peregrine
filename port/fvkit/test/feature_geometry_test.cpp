// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <vector>

#include "fvkit/feature/geometry.h"
#include "fvkit/feature/layer.h"
#include "fvkit/feature/schema.h"

namespace {

using fv::Geometry;
using fv::GeometryType;
using fv::GeoPoint;

std::vector<uint8_t> Wkb(const Geometry& g) {
  std::vector<uint8_t> out;
  EXPECT_TRUE(fv::WriteWkb(g, &out).ok());
  return out;
}

Geometry RoundTrip(const Geometry& g) {
  const std::vector<uint8_t> bytes = Wkb(g);
  Geometry back;
  size_t used = 0;
  EXPECT_TRUE(fv::ReadWkb(bytes.data(), bytes.size(), &back, &used).ok());
  EXPECT_EQ(used, bytes.size());
  return back;
}

Geometry Square(double lat, double lon, double d) {
  return Geometry::Polygon({{{lat, lon}, {lat, lon + d}, {lat + d, lon + d}, {lat + d, lon}}});
}

TEST(FeatureGeometry, PointWkbIsTheIsoEncoding) {
  // POINT(1 2): x = lon = 1, y = lat = 2.
  const std::vector<uint8_t> expect = {0x01, 0x01, 0x00, 0x00, 0x00,                     //
                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F,   //
                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40};
  EXPECT_EQ(Wkb(Geometry::Point(GeoPoint{2.0, 1.0})), expect);
}

TEST(FeatureGeometry, BigEndianAndEwkbFlagsRead) {
  // Big-endian POINT Z (1 2 3) with the EWKB Z flag.
  const std::vector<uint8_t> bytes = {0x00, 0x80, 0x00, 0x00, 0x01,                     //
                                      0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   //
                                      0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   //
                                      0x40, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  Geometry g;
  ASSERT_TRUE(fv::ReadWkb(bytes.data(), bytes.size(), &g).ok());
  EXPECT_EQ(g.type, GeometryType::kPoint);
  EXPECT_TRUE(g.has_z);
  EXPECT_FALSE(g.has_m);
  EXPECT_EQ(g.paths[0][0].lon, 1.0);
  EXPECT_EQ(g.paths[0][0].lat, 2.0);
  EXPECT_EQ(g.paths[0][0].z, 3.0);
}

TEST(FeatureGeometry, EveryTypeRoundTrips) {
  const Geometry line = Geometry::LineString({{32.6, -80.1}, {32.7, -80.0}, {32.8, -79.9}});
  Geometry holed = Square(0, 0, 10);
  holed.paths.push_back(Square(2, 2, 2).paths[0]);
  const std::vector<Geometry> all = {
      Geometry::Point(GeoPoint{32.75, -79.93}),
      line,
      holed,
      Geometry::Collection(GeometryType::kMultiPoint,
                           {Geometry::Point(GeoPoint{1, 2}), Geometry::Point(GeoPoint{3, 4})}),
      Geometry::Collection(GeometryType::kMultiLineString, {line, line}),
      Geometry::Collection(GeometryType::kMultiPolygon, {Square(0, 0, 1), Square(5, 5, 1)}),
      Geometry::Collection(GeometryType::kGeometryCollection,
                           {Geometry::Point(GeoPoint{1, 2}), line, holed}),
  };
  for (const Geometry& g : all) EXPECT_EQ(RoundTrip(g), g) << fv::GeometryTypeName(g.type);
}

TEST(FeatureGeometry, ZAndMSurviveTheRoundTrip) {
  Geometry g = Geometry::LineString({{1, 2}, {3, 4}});
  g.has_z = true;
  g.has_m = true;
  g.paths[0][0].z = 10;
  g.paths[0][0].m = 0.5;
  g.paths[0][1].z = 20;
  g.paths[0][1].m = 1.5;
  const std::vector<uint8_t> bytes = Wkb(g);
  EXPECT_EQ(bytes[1], 0xBA);  // 3002 = LineString ZM, little-endian
  EXPECT_EQ(bytes[2], 0x0B);
  EXPECT_EQ(RoundTrip(g), g);
}

TEST(FeatureGeometry, EmptyPointIsNaN) {
  Geometry empty;
  empty.type = GeometryType::kPoint;
  const std::vector<uint8_t> bytes = Wkb(empty);
  ASSERT_EQ(bytes.size(), 21u);
  Geometry back;
  ASSERT_TRUE(fv::ReadWkb(bytes.data(), bytes.size(), &back).ok());
  EXPECT_EQ(back.type, GeometryType::kPoint);
  EXPECT_TRUE(back.IsEmpty());
  EXPECT_FALSE(back.Envelope(nullptr));
}

TEST(FeatureGeometry, CorruptWkbIsRefused) {
  std::vector<uint8_t> bytes = Wkb(Geometry::LineString({{1, 2}, {3, 4}}));
  Geometry g;
  EXPECT_EQ(fv::ReadWkb(bytes.data(), bytes.size() - 1, &g).code, fv::kInvalidArg);
  // A count of four billion points in a 41-byte blob fails without allocating.
  bytes[5] = bytes[6] = bytes[7] = bytes[8] = 0xFF;
  EXPECT_EQ(fv::ReadWkb(bytes.data(), bytes.size(), &g).code, fv::kInvalidArg);
  const std::vector<uint8_t> unknown = {0x01, 0x09, 0x00, 0x00, 0x00};
  EXPECT_EQ(fv::ReadWkb(unknown.data(), unknown.size(), &g).code, fv::kInvalidArg);
  // A MultiPoint may not hold a LineString.
  std::vector<uint8_t> multi = {0x01, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
  const std::vector<uint8_t> line = Wkb(Geometry::LineString({{1, 2}, {3, 4}}));
  multi.insert(multi.end(), line.begin(), line.end());
  EXPECT_EQ(fv::ReadWkb(multi.data(), multi.size(), &g).code, fv::kInvalidArg);
  Geometry null_geometry;
  std::vector<uint8_t> out;
  EXPECT_EQ(fv::WriteWkb(null_geometry, &out).code, fv::kInvalidArg);
}

TEST(FeatureGeometry, PolygonsCloseAndEnvelopesCover) {
  const Geometry sq = Square(10, 20, 2);
  ASSERT_EQ(sq.paths[0].size(), 5u);
  fv::GeoRect box;
  ASSERT_TRUE(sq.Envelope(&box));
  EXPECT_EQ(box.ll.lat, 10);
  EXPECT_EQ(box.ll.lon, 20);
  EXPECT_EQ(box.ur.lat, 12);
  EXPECT_EQ(box.ur.lon, 22);
  const Geometry multi =
      Geometry::Collection(GeometryType::kMultiPolygon, {Square(0, 0, 1), Square(5, 5, 1)});
  ASSERT_TRUE(multi.Envelope(&box));
  EXPECT_EQ(box.ur.lat, 6);
  EXPECT_EQ(multi.PositionCount(), 10u);
  EXPECT_TRUE(Geometry{}.IsNull());
}

TEST(FeatureGeometry, TypeNamesMatchGeoPackage) {
  GeometryType t;
  ASSERT_TRUE(fv::GeometryTypeFromName("multilinestring", &t));
  EXPECT_EQ(t, GeometryType::kMultiLineString);
  EXPECT_STREQ(fv::GeometryTypeName(GeometryType::kGeometry), "GEOMETRY");
  EXPECT_FALSE(fv::GeometryTypeFromName("CURVE", &t));
}

// MARK: Schema and the memory layer

fv::FeatureSchema RiverSchema() {
  fv::FeatureSchema s;
  s.geometry_type = GeometryType::kLineString;
  fv::FieldDef name;
  name.name = "name";
  name.required = true;
  fv::FieldDef depth;
  depth.name = "depth_m";
  depth.type = fv::FieldType::kReal;
  depth.role = fv::FieldRole::kMeasurement;
  depth.unit = "m";
  depth.min = 0.0;
  fv::FieldDef kind;
  kind.name = "kind";
  kind.role = fv::FieldRole::kEnumeration;
  kind.choices = {{"river", "River"}, {"creek", "Creek"}};
  s.fields = {name, depth, kind};
  return s;
}

TEST(FeatureSchema, ValidateNamesTheProblem) {
  fv::FeatureSchema s = RiverSchema();
  EXPECT_TRUE(s.Validate().ok());
  EXPECT_EQ(s.Find("DEPTH_M"), 1);
  s.fields.push_back(s.fields[0]);
  s.fields.back().name = "Name";
  EXPECT_NE(s.Validate().message.find("twice"), std::string::npos);
  s = RiverSchema();
  s.fields[2].choices.clear();
  EXPECT_NE(s.Validate().message.find("no choices"), std::string::npos);
  s = RiverSchema();
  s.fields[0].range_end = "depth_m";
  EXPECT_NE(s.Validate().message.find("not a date"), std::string::npos);
}

TEST(FeatureSchema, ValuesAreCheckedAgainstTheirField) {
  const fv::FeatureSchema s = RiverSchema();
  EXPECT_EQ(fv::ValidateValue(s.fields[0], fv::FieldValue{}).code, fv::kInvalidArg);
  EXPECT_TRUE(fv::ValidateValue(s.fields[1], fv::FieldValue{int64_t{3}}).ok());
  EXPECT_EQ(fv::ValidateValue(s.fields[1], fv::FieldValue{-1.0}).code, fv::kInvalidArg);
  EXPECT_EQ(fv::ValidateValue(s.fields[1], fv::FieldValue{std::string("deep")}).code,
            fv::kInvalidArg);
  EXPECT_TRUE(fv::ValidateValue(s.fields[2], fv::FieldValue{std::string("creek")}).ok());
  EXPECT_EQ(fv::ValidateValue(s.fields[2], fv::FieldValue{std::string("canal")}).code,
            fv::kInvalidArg);
}

TEST(FeatureLayer, MemoryLayerInsertsQueriesAndEdits) {
  fv::MemoryFeatureLayer layer("rivers", RiverSchema());
  fv::Feature f;
  f.geometry = Geometry::LineString({{32.6, -80.1}, {32.7, -80.0}});
  f.values = {std::string("Kiawah"), 2.5};
  ASSERT_TRUE(layer.Insert(&f).ok());
  EXPECT_EQ(f.id, 1);
  ASSERT_EQ(f.values.size(), 3u);  // padded with a null
  fv::Feature far;
  far.geometry = Geometry::LineString({{10, 170}, {11, 179}});
  far.values = {std::string("Far")};
  ASSERT_TRUE(layer.Insert(&far).ok());
  fv::Feature bad;
  bad.values = {fv::FieldValue{}};
  EXPECT_EQ(layer.Insert(&bad).code, fv::kInvalidArg);  // name is required

  std::vector<fv::Feature> hits;
  ASSERT_TRUE(layer.Query(fv::GeoRect{{32, -81}, {33, -79}}, &hits).ok());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].id, 1);
  // A box across the antimeridian finds the feature east of 170.
  ASSERT_TRUE(layer.Query(fv::GeoRect{{0, 175}, {20, -175}}, &hits).ok());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].id, 2);

  f.values[2] = std::string("river");
  ASSERT_TRUE(layer.Update(f).ok());
  fv::Feature got;
  ASSERT_TRUE(layer.Get(1, &got).ok());
  EXPECT_EQ(std::get<std::string>(got.values[2]), "river");
  ASSERT_TRUE(layer.Delete(1).ok());
  EXPECT_EQ(layer.Get(1, &got).code, fv::kNotFound);
  EXPECT_EQ(layer.Count(), 1u);
  fv::Feature next;
  next.values = {std::string("Next")};
  ASSERT_TRUE(layer.Insert(&next).ok());
  EXPECT_EQ(next.id, 3);  // ids are never reused
}

}  // namespace
