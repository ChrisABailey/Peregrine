// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// EsriFeatureService (port/web-features-plan.md WF3) against responses
/// recorded on 2026-11-17 from the NWS watches/warnings FeatureServer
/// (ArcGIS 11.3) and the CO-OPS water level stations on ArcGIS Online
/// (test/data/web/esri). The server without GeoJSON or pagination is the
/// CO-OPS layer document with those capabilities edited out, answered by the
/// same server's Esri JSON.

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fvkit/feature/web/esri.h"
#include "fvkit/feature/web/remote_layer.h"
#include "web_fake_http.h"

namespace fv {
namespace web {
namespace {

namespace fs = std::filesystem;

std::string Fixture(const std::string& name) {
  std::ifstream in(std::string(FV_WEB_FIXTURES) + "/esri/" + name, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  EXPECT_FALSE(ss.str().empty()) << name;
  return ss.str();
}

/// `text` with every `from` replaced by `to`; the replacement must happen.
std::string Edit(std::string text, const std::string& from, const std::string& to) {
  size_t at = text.find(from);
  EXPECT_NE(at, std::string::npos) << from;
  for (; at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
  return text;
}

const std::string kNws =
    "https://mapservices.weather.noaa.gov/eventdriven/rest/services/WWA/watch_warn_adv/FeatureServer";
const std::string kCoops =
    "https://services2.arcgis.com/FiaPA4ga0iQKduv3/ArcGIS/rest/services/"
    "COOPS_Waterlevel_Active_Stations_v/FeatureServer";
const std::string kCoopsBox =
    "where=1%3D1&geometry=-82,30,-78,34&geometryType=esriGeometryEnvelope&inSR=4326"
    "&spatialRel=esriSpatialRelIntersects";
const std::string kFields = "&outFields=*&returnGeometry=true&outSR=4326";
const GeoRect kCarolinas{{30.0, -82.0}, {34.0, -78.0}};

std::shared_ptr<FakeHttp> Nws() {
  auto http = std::make_shared<FakeHttp>();
  http->Route(kNws + "?f=json", Fixture("nws/service.json"));
  http->Route(kNws + "/1?f=json", Fixture("nws/layer1.json"));
  http->Route(kNws + "/1/query?where=1%3D1&geometry=-122.5,37.5,-121.5,38&geometryType=esriGeometryEnvelope"
                     "&inSR=4326&spatialRel=esriSpatialRelIntersects" + kFields +
                  "&orderByFields=objectid&resultOffset=0&resultRecordCount=2&f=geojson",
              Fixture("nws/query_page1.json"));
  return http;
}

std::shared_ptr<FakeHttp> Coops() {
  auto http = std::make_shared<FakeHttp>();
  http->Route(kCoops + "?f=json", Fixture("coops/service.json"));
  http->Route(kCoops + "/0?f=json", Fixture("coops/layer0.json"));
  for (int page = 0; page < 3; ++page)
    http->Route(kCoops + "/0/query?" + kCoopsBox + kFields + "&orderByFields=objectid&resultOffset=" +
                    std::to_string(page * 3) + "&resultRecordCount=3&f=geojson",
                Fixture("coops/query_page" + std::to_string(page + 1) + ".json"));
  return http;
}

/// CO-OPS as a server that offers neither GeoJSON nor pagination, four
/// records a request.
std::shared_ptr<FakeHttp> OldCoops() {
  auto http = std::make_shared<FakeHttp>();
  http->Route(kCoops + "?f=json", Fixture("coops/service.json"));
  std::string layer = Fixture("coops/layer0.json");
  layer = Edit(layer, "\"supportedQueryFormats\":\"JSON, geoJSON, PBF\"", "\"supportedQueryFormats\":\"JSON, AMF\"");
  layer = Edit(layer, "\"supportsPagination\":true", "\"supportsPagination\":false");
  layer = Edit(layer, "\"maxRecordCount\":2000", "\"maxRecordCount\":4");
  http->Route(kCoops + "/0?f=json", layer);
  http->Route(kCoops + "/0/query?" + kCoopsBox + "&returnIdsOnly=true&f=json", Fixture("coops/ids.json"));
  http->Route(kCoops + "/0/query?objectIds=77,78,79,80" + kFields + "&f=json", Fixture("coops/objects_77-80.json"));
  http->Route(kCoops + "/0/query?objectIds=81,82,83,84" + kFields + "&f=json", Fixture("coops/objects_81-84.json"));
  http->Route(kCoops + "/0/query?objectIds=85" + kFields + "&f=json", Fixture("coops/objects_85.json"));
  return http;
}

std::shared_ptr<EsriFeatureService> OpenService(std::shared_ptr<FakeHttp> http, const std::string& url,
                                                int page_size = 0) {
  EsriOptions options;
  options.page_size = page_size;
  std::shared_ptr<EsriFeatureService> service;
  Status s = EsriFeatureService::Open(http, url, options, &service);
  EXPECT_TRUE(s.ok()) << s.message;
  return service;
}

/// Every page of `layer_id` in `box`.
std::vector<Feature> FetchAll(EsriFeatureService* service, const std::string& layer_id, const GeoRect& box,
                              int* pages) {
  std::vector<Feature> all;
  std::string cursor, next;
  *pages = 0;
  do {
    Status s = service->FetchPage(layer_id, box, cursor, &all, &next, nullptr);
    EXPECT_TRUE(s.ok()) << s.message;
    if (!s.ok()) break;
    ++*pages;
    cursor = next;
  } while (!next.empty() && *pages < 10);
  return all;
}

/// True when the two geometries have the same structure and positions within
/// 1e-9 degrees: a server's Esri JSON and GeoJSON round the last digit differently.
bool SameShape(const Geometry& a, const Geometry& b) {
  if (a.type != b.type || a.paths.size() != b.paths.size() || a.parts.size() != b.parts.size()) return false;
  for (size_t i = 0; i < a.paths.size(); ++i) {
    if (a.paths[i].size() != b.paths[i].size()) return false;
    for (size_t k = 0; k < a.paths[i].size(); ++k)
      if (std::abs(a.paths[i][k].lat - b.paths[i][k].lat) > 1e-9 ||
          std::abs(a.paths[i][k].lon - b.paths[i][k].lon) > 1e-9)
        return false;
  }
  for (size_t i = 0; i < a.parts.size(); ++i)
    if (!SameShape(a.parts[i], b.parts[i])) return false;
  return true;
}

const FieldDef* Field(const FeatureSchema& schema, const std::string& name) {
  int i = schema.Find(name);
  return i < 0 ? nullptr : &schema.fields[i];
}

TEST(Esri, DatesAreIsoUtc) {
  EXPECT_EQ(EsriDateText(0), "1970-01-01T00:00:00Z");
  EXPECT_EQ(EsriDateText(1794953558000), "2026-11-17T22:12:38Z");
  EXPECT_EQ(EsriDateText(951782400123), "2000-02-29T00:00:00.123Z");
  EXPECT_EQ(EsriDateText(-1), "1969-12-31T23:59:59.999Z");
}

TEST(Esri, ListsTheFeatureLayersAndTakesTheTitle) {
  auto service = OpenService(Nws(), kNws + "/?f=pjson");
  ASSERT_TRUE(service);
  EXPECT_EQ(service->title(), "watch_warn_adv");
  std::vector<RemoteLayerInfo> layers;
  ASSERT_TRUE(service->ListLayers(&layers, nullptr).ok());
  ASSERT_EQ(layers.size(), 2u);
  EXPECT_EQ(layers[1].id, "1");
  EXPECT_EQ(layers[1].title, "WatchesWarnings");
  EXPECT_EQ(layers[1].schema.geometry_type, GeometryType::kMultiPolygon);

  auto coops = OpenService(Coops(), kCoops);
  EXPECT_EQ(coops->title(), "COOPS_Waterlevel_Active_Stations_v");
}

TEST(Esri, FieldsAreTypedAndTheExtentIsInDegrees) {
  auto service = OpenService(Nws(), kNws);
  RemoteLayerInfo info;
  ASSERT_TRUE(service->Describe("1", &info, nullptr).ok());
  EXPECT_EQ(info.schema.fields.size(), 15u);
  ASSERT_NE(Field(info.schema, "objectid"), nullptr);
  EXPECT_EQ(Field(info.schema, "objectid")->type, FieldType::kInteger);
  EXPECT_EQ(Field(info.schema, "prod_type")->type, FieldType::kText);
  EXPECT_EQ(Field(info.schema, "prod_type")->title, "Hazard Type");
  EXPECT_EQ(Field(info.schema, "idp_filedate")->type, FieldType::kDateTime);
  ASSERT_TRUE(info.has_extent);  // Web Mercator in the layer document
  EXPECT_NEAR(info.extent.ll.lon, -180.0, 1e-4);
  EXPECT_NEAR(info.extent.ur.lon, 180.0, 1e-4);
  EXPECT_NEAR(info.extent.ll.lat, 18.36, 0.01);
  EXPECT_NEAR(info.extent.ur.lat, 70.09, 0.01);

  auto coops = OpenService(Coops(), kCoops);
  ASSERT_TRUE(coops->Describe("0", &info, nullptr).ok());
  EXPECT_EQ(info.schema.geometry_type, GeometryType::kPoint);
  EXPECT_EQ(Field(info.schema, "latitude")->type, FieldType::kReal);
  // ArcGIS Online wraps a description in JSON; only its text is kept.
  EXPECT_EQ(Field(info.schema, "objectid")->description.rfind("The ObjectID is", 0), 0u);
  EXPECT_DOUBLE_EQ(info.extent.ur.lat, 70.4113899997497);
  EXPECT_EQ(coops->Describe("7", &info, nullptr).code, kNotFound);
  EXPECT_EQ(coops->Describe("x", &info, nullptr).code, kNotFound);
}

TEST(Esri, AGeoJsonQueryIsPagedByOffsetUntilTheLimitIsNoLongerExceeded) {
  auto service = OpenService(Coops(), kCoops, 3);
  int pages = 0;
  std::vector<Feature> all = FetchAll(service.get(), "0", kCarolinas, &pages);
  EXPECT_EQ(pages, 3);
  ASSERT_EQ(all.size(), 9u);
  for (size_t i = 0; i < all.size(); ++i) EXPECT_EQ(all[i].id, 77 + static_cast<int64_t>(i));
  RemoteLayerInfo info;
  service->Describe("0", &info, nullptr);
  const Feature& charleston = all[1];
  EXPECT_EQ(std::get<std::string>(charleston.values[info.schema.Find("name")]), "Charleston");
  EXPECT_EQ(std::get<int64_t>(charleston.values[info.schema.Find("objectid")]), 78);
  EXPECT_EQ(charleston.geometry.type, GeometryType::kPoint);
  EXPECT_NEAR(charleston.geometry.paths[0][0].lat, 32.780834, 1e-6);
}

TEST(Esri, DatesArriveAsDateTimesAndAreasAsMultiPolygons) {
  auto service = OpenService(Nws(), kNws, 2);
  std::vector<Feature> page;
  std::string next;
  ASSERT_TRUE(service->FetchPage("1", GeoRect{{37.5, -122.5}, {38.0, -121.5}}, "", &page, &next, nullptr).ok());
  ASSERT_EQ(page.size(), 2u);
  EXPECT_EQ(next, "offset:2");
  RemoteLayerInfo info;
  service->Describe("1", &info, nullptr);
  const std::string filed = std::get<std::string>(page[0].values[info.schema.Find("idp_filedate")]);
  EXPECT_EQ(filed, "2026-11-17T22:14:38Z");
  EXPECT_EQ(page[0].geometry.type, GeometryType::kMultiPolygon);
}

TEST(Esri, AServerWithoutPaginationIsReadInBlocksOfObjectIds) {
  auto http = OldCoops();
  auto service = OpenService(http, kCoops);
  int pages = 0;
  std::vector<Feature> all = FetchAll(service.get(), "0", kCarolinas, &pages);
  EXPECT_EQ(pages, 3);
  ASSERT_EQ(all.size(), 9u);
  for (size_t i = 0; i < all.size(); ++i) EXPECT_EQ(all[i].id, 77 + static_cast<int64_t>(i));

  // The same features as the GeoJSON answer, from Esri JSON.
  auto modern = OpenService(Coops(), kCoops, 3);
  std::vector<Feature> expected = FetchAll(modern.get(), "0", kCarolinas, &pages);
  ASSERT_EQ(expected.size(), all.size());
  for (size_t i = 0; i < all.size(); ++i) {
    EXPECT_TRUE(SameShape(all[i].geometry, expected[i].geometry)) << i;
    EXPECT_EQ(all[i].values, expected[i].values) << i;
  }
}

TEST(Esri, AnEsriJsonPolygonWithHolesMatchesTheGeoJsonAnswer) {
  const std::string any_query = kNws + "/1/query?*";
  auto geojson_http = Nws();
  geojson_http->Route(any_query, Fixture("nws/hole_geojson.json"));
  auto esri_http = Nws();
  esri_http->Route(kNws + "/1?f=json",
                   Edit(Fixture("nws/layer1.json"), "\"supportedQueryFormats\":\"JSON, geoJSON, PBF\"",
                        "\"supportedQueryFormats\":\"JSON\""));
  esri_http->Route(any_query, Fixture("nws/hole_esri.json"));

  const GeoRect box{{29.0, -86.0}, {30.5, -84.0}};
  std::vector<Feature> from_geojson, from_esri;
  std::string next;
  auto a = OpenService(geojson_http, kNws);
  ASSERT_TRUE(a->FetchPage("1", box, "", &from_geojson, &next, nullptr).ok());
  auto b = OpenService(esri_http, kNws);
  ASSERT_TRUE(b->FetchPage("1", box, "", &from_esri, &next, nullptr).ok());
  ASSERT_EQ(from_geojson.size(), 1u);
  ASSERT_EQ(from_esri.size(), 1u);
  EXPECT_EQ(from_esri[0].id, from_geojson[0].id);
  const Geometry& g = from_esri[0].geometry;
  ASSERT_EQ(g.type, GeometryType::kMultiPolygon);
  ASSERT_EQ(g.parts.size(), 1u);
  EXPECT_EQ(g.parts[0].paths.size(), 26u);  // one exterior, 25 holes
  EXPECT_TRUE(SameShape(g, from_geojson[0].geometry));
  EXPECT_EQ(from_esri[0].values, from_geojson[0].values);
}

TEST(Esri, GroupLayersAndTablesAreLeftOutAndErrorsAreReported) {
  const std::string url = "https://example.test/arcgis/rest/services/Roads/MapServer";
  auto http = std::make_shared<FakeHttp>();
  http->Route(url + "?f=json", R"({"mapName":"Roads","layers":[
      {"id":0,"name":"Transport","parentLayerId":-1,"subLayerIds":[1,2]},
      {"id":1,"name":"Highways","parentLayerId":0,"subLayerIds":null,"geometryType":"esriGeometryPolyline"},
      {"id":2,"name":"Imagery","parentLayerId":0,"subLayerIds":null,"type":"Raster Layer"}],
      "tables":[{"id":3,"name":"Counts"}]})");
  http->Route(url + "/1?f=json", R"({"error":{"code":499,"message":"Token Required","details":[]}})");
  http->Route("https://example.test/arcgis/rest/services/Roads/FeatureServer/1?f=json",
              R"({"type":"Feature Layer","geometryType":"esriGeometryPolyline","fields":[]})");
  auto service = OpenService(http, url);
  std::vector<RemoteLayerInfo> layers;
  ASSERT_TRUE(service->ListLayers(&layers, nullptr).ok());
  ASSERT_EQ(layers.size(), 1u);
  EXPECT_EQ(layers[0].title, "Highways");
  EXPECT_EQ(layers[0].schema.geometry_type, GeometryType::kMultiLineString);
  RemoteLayerInfo info;
  EXPECT_EQ(service->Describe("1", &info, nullptr).code, kUnsupported);

  std::shared_ptr<EsriFeatureService> layer_url;
  Status s = EsriFeatureService::Open(http, "https://example.test/arcgis/rest/services/Roads/FeatureServer/1",
                                      {}, &layer_url);
  EXPECT_EQ(s.code, kUnsupported);
  EXPECT_NE(s.message.find("Roads/FeatureServer instead"), std::string::npos) << s.message;
  EXPECT_EQ(EsriFeatureService::Open(http, "https://example.test/nothing", {}, &layer_url).code, kNotFound);
}

TEST(Esri, ARemoteLayerFillsFromTheService) {
  auto http = Coops();
  std::string one_page = Fixture("coops/query_page3.json");
  http->Route(kCoops + "/0/query?*", one_page);
  auto service = OpenService(http, kCoops);
  RemoteLayerInfo info;
  ASSERT_TRUE(service->Describe("0", &info, nullptr).ok());
  fs::path dir = fs::temp_directory_path() / "fv_esri_ARemoteLayerFillsFromTheService";
  fs::remove_all(dir);
  {
    std::shared_ptr<RemoteFeatureLayer> layer;
    Status s = RemoteFeatureLayer::Open(service, info, WebCachePath(dir.string(), kCoops), {}, &layer);
    ASSERT_TRUE(s.ok()) << s.message;
    std::vector<Feature> got;
    layer->Query(kCarolinas, &got);
    ASSERT_TRUE(layer->WaitIdle(10));
    EXPECT_EQ(layer->Count(), 3u);
    got.clear();
    layer->Query(kCarolinas, &got);
    ASSERT_EQ(got.size(), 3u);
    EXPECT_FALSE(std::get<std::string>(got[0].values[layer->Schema().Find("name")]).empty());
  }
  fs::remove_all(dir);
}

}  // namespace
}  // namespace web
}  // namespace fv
