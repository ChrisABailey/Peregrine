// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// OgcApiFeatures (port/web-features-plan.md WF2) against responses recorded
/// from USGS Water Data and Environment Canada GeoMet on 2026-11-17
/// (test/data/web/ogc; collection lists trimmed to a few entries).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "fvkit/feature/web/ogc_api.h"
#include "fvkit/feature/web/remote_layer.h"
#include "web_fake_http.h"

namespace fv {
namespace web {
namespace {

namespace fs = std::filesystem;

std::string Fixture(const std::string& name) {
  std::ifstream in(std::string(FV_WEB_FIXTURES) + "/ogc/" + name, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  EXPECT_FALSE(ss.str().empty()) << name;
  return ss.str();
}

const std::string kUsgs = "https://api.waterdata.usgs.gov/ogcapi/v0";
const std::string kUsgsItems = "https://api.waterdata.usgs.gov/ogcapi/v1/collections/monitoring-locations/items";
const std::string kGeoMet = "https://api.weather.gc.ca/";
const std::string kGeoMetItems = "https://api.weather.gc.ca/collections/hydrometric-stations/items";

std::shared_ptr<FakeHttp> Usgs() {
  auto http = std::make_shared<FakeHttp>();
  http->Route(kUsgs, Fixture("usgs/landing.json"));
  http->Route(kUsgs + "/collections", Fixture("usgs/collections.json"));
  http->Route("https://api.waterdata.usgs.gov/ogcapi/v1/collections/monitoring-locations/schema?f=json",
              Fixture("usgs/schema.json"));
  http->Route(kUsgsItems + "?f=json&bbox=-80,32.7,-79.9,32.8&limit=3", Fixture("usgs/items_page1.json"));
  http->Route(kUsgsItems + "?cursor=VVNHUy0wMjE3MjA3MTE&bbox=-80.0,32.7,-79.9,32.8&limit=3",
              Fixture("usgs/items_page2.json"));
  return http;
}

/// GeoMet with its schema and queryables documents left out, so fields are inferred.
std::shared_ptr<FakeHttp> GeoMet() {
  auto http = std::make_shared<FakeHttp>();
  http->Route(kGeoMet, Fixture("geomet/landing.json"));
  http->Route("https://api.weather.gc.ca/collections", Fixture("geomet/collections.json"));
  http->Route(kGeoMetItems + "?f=json&limit=50", Fixture("geomet/items_page1.json"));
  http->Route(kGeoMetItems + "?f=json&bbox=-80,43.5,-79,44&limit=4", Fixture("geomet/items_page1.json"));
  http->Route(kGeoMetItems + "?offset=4&bbox=-80,43.5,-79,44&limit=4", Fixture("geomet/items_page2.json"));
  return http;
}

std::shared_ptr<OgcApiFeatures> OpenService(std::shared_ptr<FakeHttp> http, const std::string& url,
                                            int limit) {
  OgcApiOptions options;
  options.limit = limit;
  std::shared_ptr<OgcApiFeatures> service;
  Status s = OgcApiFeatures::Open(http, url, options, &service);
  EXPECT_TRUE(s.ok()) << s.message;
  return service;
}

const FieldDef* Field(const FeatureSchema& schema, const std::string& name) {
  int i = schema.Find(name);
  return i < 0 ? nullptr : &schema.fields[i];
}

TEST(OgcApi, FeatureIdsKeepPositiveIntegersAndHashTheRest) {
  EXPECT_EQ(OgcFeatureId("123"), 123);
  EXPECT_NE(OgcFeatureId("0"), 0);
  EXPECT_NE(OgcFeatureId("USGS-02172110"), 0);
  EXPECT_GT(OgcFeatureId("USGS-02172110"), 0);
  EXPECT_EQ(OgcFeatureId("USGS-02172110"), OgcFeatureId("USGS-02172110"));
  EXPECT_NE(OgcFeatureId("USGS-02172110"), OgcFeatureId("USGS-02172111"));
}

TEST(OgcApi, ListsTheFeatureCollectionsWithTheirExtents) {
  auto service = OpenService(Usgs(), kUsgs, 3);
  ASSERT_TRUE(service);
  std::vector<RemoteLayerInfo> layers;
  ASSERT_TRUE(service->ListLayers(&layers, nullptr).ok());
  ASSERT_EQ(layers.size(), 3u);
  EXPECT_EQ(layers[1].id, "monitoring-locations");
  EXPECT_EQ(layers[1].title, "Monitoring locations");
  ASSERT_TRUE(layers[1].has_extent);
  EXPECT_DOUBLE_EQ(layers[1].extent.ll.lon, -180.0);
  EXPECT_DOUBLE_EQ(layers[1].extent.ur.lat, 90.0);

  auto geomet = OpenService(GeoMet(), kGeoMet, 4);
  layers.clear();
  ASSERT_TRUE(geomet->ListLayers(&layers, nullptr).ok());
  ASSERT_EQ(layers.size(), 2u);
  EXPECT_EQ(layers[0].id, "hydrometric-stations");
  EXPECT_DOUBLE_EQ(layers[0].extent.ll.lon, -142.0);
  EXPECT_DOUBLE_EQ(layers[0].extent.ur.lat, 84.0);
}

TEST(OgcApi, FieldsComeFromThePublishedSchema) {
  auto service = OpenService(Usgs(), kUsgs, 3);
  RemoteLayerInfo info;
  ASSERT_TRUE(service->Describe("monitoring-locations", &info, nullptr).ok());
  EXPECT_EQ(info.schema.fields.size(), 43u);  // 44 properties less the geometry
  EXPECT_EQ(Field(info.schema, "geometry"), nullptr);
  const FieldDef* name = Field(info.schema, "agency_name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->type, FieldType::kText);
  EXPECT_EQ(name->title, "Agency name");
  ASSERT_NE(Field(info.schema, "altitude"), nullptr);
  EXPECT_EQ(Field(info.schema, "altitude")->type, FieldType::kReal);
  EXPECT_EQ(info.schema.geometry_type, GeometryType::kGeometry);  // geometry-any
}

TEST(OgcApi, ACollectionWithoutASchemaGetsFieldsInferredFromItsItems) {
  auto http = GeoMet();
  auto service = OpenService(http, kGeoMet, 4);
  RemoteLayerInfo info;
  ASSERT_TRUE(service->Describe("hydrometric-stations", &info, nullptr).ok());
  ASSERT_NE(Field(info.schema, "STATION_NAME"), nullptr);
  EXPECT_EQ(Field(info.schema, "STATION_NAME")->type, FieldType::kText);
  EXPECT_EQ(Field(info.schema, "REAL_TIME")->type, FieldType::kInteger);
  EXPECT_EQ(Field(info.schema, "DRAINAGE_AREA_GROSS")->type, FieldType::kReal);
  EXPECT_EQ(info.schema.geometry_type, GeometryType::kPoint);
  std::vector<std::string> asked = http->requests();
  EXPECT_NE(std::find(asked.begin(), asked.end(),
                      "https://api.weather.gc.ca/collections/hydrometric-stations/queryables?f=json"),
            asked.end());
}

TEST(OgcApi, StoreTypeNamesAndNullableTypeListsReadAsNumbers) {
  auto http = GeoMet();
  // GeoMet's ltce-snowfall queryables, cut down: Elasticsearch type names.
  http->Route("https://api.weather.gc.ca/collections/hydrometric-stations/queryables?f=json",
              R"({"type":"object","properties":{
                   "geometry":{"format":"geometry-any"},
                   "RECORD_SNOWFALL":{"type":"half_float"},
                   "RECORD_SNOWFALL_YR":{"type":"short"},
                   "LOCAL_MONTH":{"type":"byte"},
                   "DEPTH":{"type":["number","null"]},
                   "IDENTIFIER":{"type":"string"}}})");
  auto service = OpenService(http, kGeoMet, 4);
  RemoteLayerInfo info;
  ASSERT_TRUE(service->Describe("hydrometric-stations", &info, nullptr).ok());
  ASSERT_EQ(info.schema.fields.size(), 5u);
  EXPECT_EQ(Field(info.schema, "RECORD_SNOWFALL")->type, FieldType::kReal);
  EXPECT_EQ(Field(info.schema, "RECORD_SNOWFALL_YR")->type, FieldType::kInteger);
  EXPECT_EQ(Field(info.schema, "LOCAL_MONTH")->type, FieldType::kInteger);
  EXPECT_EQ(Field(info.schema, "DEPTH")->type, FieldType::kReal);
  EXPECT_EQ(Field(info.schema, "IDENTIFIER")->type, FieldType::kText);
}

TEST(OgcApi, PagesFollowTheNextLinks) {
  auto service = OpenService(Usgs(), kUsgs, 3);
  GeoRect box{{32.7, -80.0}, {32.8, -79.9}};
  std::vector<Feature> page;
  std::string next;
  ASSERT_TRUE(service->FetchPage("monitoring-locations", box, "", &page, &next, nullptr).ok());
  ASSERT_EQ(page.size(), 3u);
  EXPECT_EQ(page[0].id, OgcFeatureId("SC008-324637079581908"));
  EXPECT_EQ(page[0].geometry.type, GeometryType::kPoint);
  EXPECT_NEAR(page[0].geometry.paths[0][0].lon, -79.97175665293, 1e-9);
  RemoteLayerInfo info;
  service->Describe("monitoring-locations", &info, nullptr);
  int county = info.schema.Find("county_name");
  ASSERT_GE(county, 0);
  EXPECT_EQ(std::get<std::string>(page[0].values[county]), "Charleston County");
  EXPECT_TRUE(IsNull(page[0].values[info.schema.Find("drainage_area")]));
  EXPECT_DOUBLE_EQ(std::get<double>(page[0].values[info.schema.Find("altitude")]), 14.0);
  ASSERT_FALSE(next.empty());

  std::vector<Feature> page2;
  std::string next2;
  ASSERT_TRUE(service->FetchPage("monitoring-locations", box, next, &page2, &next2, nullptr).ok());
  ASSERT_EQ(page2.size(), 3u);
  EXPECT_EQ(page2[0].id, OgcFeatureId("USGS-021720712"));
  EXPECT_NE(next2, next);

  auto geomet = OpenService(GeoMet(), kGeoMet, 4);
  GeoRect toronto{{43.5, -80.0}, {44.0, -79.0}};
  page.clear();
  ASSERT_TRUE(geomet->FetchPage("hydrometric-stations", toronto, "", &page, &next, nullptr).ok());
  ASSERT_EQ(page.size(), 4u);
  EXPECT_EQ(next, kGeoMetItems + "?offset=4&bbox=-80,43.5,-79,44&limit=4");
  page.clear();
  ASSERT_TRUE(geomet->FetchPage("hydrometric-stations", toronto, next, &page, &next2, nullptr).ok());
  EXPECT_EQ(page.size(), 4u);
  EXPECT_EQ(page[0].id, OgcFeatureId("02HB025"));
}

/// A minimal server: relative links, a record collection, and an items
/// endpoint that answers HTML until asked for f=json.
TEST(OgcApi, RelativeLinksResolveAndRecordCollectionsAreLeftOut) {
  auto http = std::make_shared<FakeHttp>();
  http->Route("https://example.test/api/", R"({"title":"Example","links":[
      {"rel":"data","type":"application/json","href":"collections"}]})");
  http->Route("https://example.test/api/collections", R"({"collections":[
      {"id":"stops","links":[{"rel":"items","type":"application/geo+json","href":"/api/collections/stops/items"}]},
      {"id":"catalogue","itemType":"record"}]})");
  http->Route("https://example.test/api/collections/stops/items?limit=50", "<html></html>");
  http->Route("https://example.test/api/collections/stops/items?limit=50&f=json",
              R"({"type":"FeatureCollection","features":[
                  {"type":"Feature","id":7,"geometry":{"type":"Point","coordinates":[1,2]},
                   "properties":{"n":1}}]})");
  http->Route("https://example.test/api/collections/stops/items?bbox=0,0,3,3&limit=1",
              R"({"type":"FeatureCollection","features":[
                  {"type":"Feature","id":7,"geometry":{"type":"Point","coordinates":[1,2]},
                   "properties":{"n":1.0}}],
                  "links":[{"rel":"next","href":"items?offset=1"}]})");
  auto service = OpenService(http, "https://example.test/api/", 1);
  ASSERT_TRUE(service);
  EXPECT_EQ(service->title(), "Example");
  std::vector<RemoteLayerInfo> layers;
  ASSERT_TRUE(service->ListLayers(&layers, nullptr).ok());
  ASSERT_EQ(layers.size(), 1u);
  std::vector<Feature> page;
  std::string next;
  ASSERT_TRUE(service->FetchPage("stops", GeoRect{{0, 0}, {3, 3}}, "", &page, &next, nullptr).ok());
  ASSERT_EQ(page.size(), 1u);
  EXPECT_EQ(page[0].id, 7);
  EXPECT_EQ(std::get<int64_t>(page[0].values[0]), 1);  // 1.0 in an integer field
  EXPECT_EQ(next, "https://example.test/api/collections/stops/items?offset=1");
}

TEST(OgcApi, NotALandingPageIsRefused) {
  auto http = std::make_shared<FakeHttp>();
  http->Route("https://example.test/", R"({"hello":"world"})");
  std::shared_ptr<OgcApiFeatures> service;
  EXPECT_EQ(OgcApiFeatures::Open(http, "https://example.test/", {}, &service).code, kUnsupported);
  EXPECT_EQ(OgcApiFeatures::Open(http, "https://example.test/missing", {}, &service).code, kNotFound);
}

TEST(OgcApi, ARemoteLayerFillsFromTheService) {
  auto http = GeoMet();
  // Every tile gets the first recorded page, with its next link turned off.
  std::string one_page = Fixture("geomet/items_page1.json");
  for (size_t at = one_page.find("\"next\""); at != std::string::npos; at = one_page.find("\"next\""))
    one_page.replace(at, 6, "\"prev\"");
  http->Route(kGeoMetItems + "?f=json&bbox=*", one_page);
  auto service = OpenService(http, kGeoMet, 1000);
  RemoteLayerInfo info;
  ASSERT_TRUE(service->Describe("hydrometric-stations", &info, nullptr).ok());
  fs::path dir = fs::temp_directory_path() / "fv_ogc_ARemoteLayerFillsFromTheService";
  fs::remove_all(dir);
  {
    std::shared_ptr<RemoteFeatureLayer> layer;
    Status s = RemoteFeatureLayer::Open(service, info, WebCachePath(dir.string(), kGeoMet), {}, &layer);
    ASSERT_TRUE(s.ok()) << s.message;
    std::vector<Feature> got;
    layer->Query(GeoRect{{43.5, -80.0}, {44.0, -79.0}}, &got);
    ASSERT_TRUE(layer->WaitIdle(10));
    EXPECT_EQ(layer->Count(), 4u);
    got.clear();
    layer->Query(GeoRect{{43.5, -80.0}, {44.0, -79.0}}, &got);
    ASSERT_FALSE(got.empty());
    int name = layer->Schema().Find("STATION_NAME");
    EXPECT_FALSE(std::get<std::string>(got[0].values[name]).empty());
  }
  fs::remove_all(dir);
}

}  // namespace
}  // namespace web
}  // namespace fv
