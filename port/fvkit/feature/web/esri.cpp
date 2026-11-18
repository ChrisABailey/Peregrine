// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Esri GeoServices REST client: service and layer documents, field mapping,
/// queries paged by offset or by object id, and Esri JSON geometry.

#include "fvkit/feature/web/esri.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>

#include "../geojson_detail.h"
#include "fvkit/log.h"

namespace fv {
namespace web {
namespace {

using geojson_detail::json;

constexpr double kPi = 3.14159265358979323846;
constexpr double kMercatorRadius = 6378137.0;

std::string Str(const json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return "";
  return j[key].get<std::string>();
}

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/// The layer geometry type an `esriGeometry*` name stands for. Lines and
/// areas are always the multi types (see the header).
GeometryType LayerGeometryType(const std::string& esri) {
  if (esri == "esriGeometryPoint") return GeometryType::kPoint;
  if (esri == "esriGeometryMultipoint") return GeometryType::kMultiPoint;
  if (esri == "esriGeometryPolyline") return GeometryType::kMultiLineString;
  if (esri == "esriGeometryPolygon") return GeometryType::kMultiPolygon;
  return GeometryType::kGeometry;
}

/// An Esri field as a `FieldDef`. False for a field with no feature value
/// (geometry, blob, raster).
bool FieldFromEsri(const json& f, FieldDef* out) {
  const std::string type = Str(f, "type");
  FieldDef d;
  d.name = Str(f, "name");
  if (d.name.empty()) return false;
  if (type == "esriFieldTypeOID" || type == "esriFieldTypeInteger" ||
      type == "esriFieldTypeSmallInteger" || type == "esriFieldTypeBigInteger") {
    d.type = FieldType::kInteger;
  } else if (type == "esriFieldTypeDouble" || type == "esriFieldTypeSingle") {
    d.type = FieldType::kReal;
  } else if (type == "esriFieldTypeDate" || type == "esriFieldTypeTimestampOffset") {
    d.type = FieldType::kDateTime;
  } else if (type == "esriFieldTypeDateOnly") {
    d.type = FieldType::kDate;
  } else if (type == "esriFieldTypeString" || type == "esriFieldTypeGlobalID" ||
             type == "esriFieldTypeGUID" || type == "esriFieldTypeXML" ||
             type == "esriFieldTypeTimeOnly") {
    d.type = FieldType::kText;
  } else {
    return false;
  }
  d.title = Str(f, "alias");
  if (d.title == d.name) d.title.clear();
  // ArcGIS Online stores a field description as JSON text: {"value": "...", ...}.
  d.description = Str(f, "description");
  if (!d.description.empty() && d.description[0] == '{') {
    json inner = json::parse(d.description, nullptr, /*allow_exceptions=*/false);
    d.description = Str(inner, "value");
  }
  *out = d;
  return true;
}

/// True for a spatial reference given in WGS 84 degrees.
bool IsGeographic(const json& sr) {
  for (const char* key : {"latestWkid", "wkid"}) {
    if (!sr.is_object() || !sr.contains(key) || !sr[key].is_number_integer()) continue;
    const int wkid = sr[key].get<int>();
    return wkid == 4326 || wkid == 4269 || wkid == 4258;
  }
  return false;
}

bool IsWebMercator(const json& sr) {
  for (const char* key : {"latestWkid", "wkid"}) {
    if (!sr.is_object() || !sr.contains(key) || !sr[key].is_number_integer()) continue;
    const int wkid = sr[key].get<int>();
    return wkid == 3857 || wkid == 102100 || wkid == 102113 || wkid == 900913;
  }
  return false;
}

/// An Esri extent as a WGS 84 box. False when it is missing, empty, or in a
/// projection other than Web Mercator.
bool ReadExtent(const json& e, GeoRect* out) {
  if (!e.is_object()) return false;
  for (const char* k : {"xmin", "ymin", "xmax", "ymax"})
    if (!e.contains(k) || !e[k].is_number()) return false;
  double x0 = e["xmin"].get<double>(), y0 = e["ymin"].get<double>();
  double x1 = e["xmax"].get<double>(), y1 = e["ymax"].get<double>();
  if (!(x0 <= x1 && y0 <= y1)) return false;
  const json sr = e.value("spatialReference", json());
  if (IsWebMercator(sr)) {
    auto lon = [](double x) { return x / kMercatorRadius * 180.0 / kPi; };
    auto lat = [](double y) { return std::atan(std::sinh(y / kMercatorRadius)) * 180.0 / kPi; };
    x0 = lon(x0), x1 = lon(x1), y0 = lat(y0), y1 = lat(y1);
  } else if (!IsGeographic(sr)) {
    return false;
  }
  out->ll = GeoPoint{std::max(y0, -90.0), std::max(x0, -180.0)};
  out->ur = GeoPoint{std::min(y1, 90.0), std::min(x1, 180.0)};
  return true;
}

/// One Esri JSON coordinate array as a position; false when it is not one.
bool ReadXY(const json& c, Position* out) {
  if (!c.is_array() || c.size() < 2 || !c[0].is_number() || !c[1].is_number()) return false;
  out->lon = c[0].get<double>();
  out->lat = c[1].get<double>();
  return true;
}

bool ReadPath(const json& a, Path* out) {
  if (!a.is_array()) return false;
  out->reserve(a.size());
  for (const json& c : a) {
    Position p;
    if (!ReadXY(c, &p)) return false;
    out->push_back(p);
  }
  return true;
}

/// Twice the signed area in degrees²; negative for a clockwise ring.
double SignedArea2(const Path& r) {
  double a = 0;
  for (size_t i = 0; i + 1 < r.size(); ++i) a += r[i].lon * r[i + 1].lat - r[i + 1].lon * r[i].lat;
  return a;
}

bool InsideRing(const Position& p, const Path& r) {
  bool in = false;
  for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
    if ((r[i].lat > p.lat) != (r[j].lat > p.lat) &&
        p.lon < (r[j].lon - r[i].lon) * (p.lat - r[i].lat) / (r[j].lat - r[i].lat) + r[i].lon)
      in = !in;
  }
  return in;
}

/// Esri rings as a MultiPolygon. Esri exteriors are clockwise and holes
/// counter-clockwise, in any order; each hole goes to the smallest exterior
/// holding it, and every ring is reversed to the GeoJSON winding. A hole no
/// exterior holds becomes an exterior of its own.
Geometry PolygonFromRings(std::vector<Path> rings) {
  struct Outer {
    Path ring;
    double area;
    std::vector<Path> holes;
  };
  std::vector<Outer> outers;
  std::vector<Path> holes;
  for (Path& r : rings) {
    if (r.size() < 3) continue;
    if (r.front().lat != r.back().lat || r.front().lon != r.back().lon) r.push_back(r.front());
    const double a = SignedArea2(r);
    if (a == 0) continue;
    std::reverse(r.begin(), r.end());
    if (a < 0)
      outers.push_back(Outer{std::move(r), -a, {}});
    else
      holes.push_back(std::move(r));
  }
  for (Path& h : holes) {
    Outer* best = nullptr;
    for (Outer& o : outers)
      if ((!best || o.area < best->area) && InsideRing(h[0], o.ring)) best = &o;
    if (best) {
      best->holes.push_back(std::move(h));
    } else {
      std::reverse(h.begin(), h.end());
      outers.push_back(Outer{std::move(h), 0, {}});
    }
  }
  std::vector<Geometry> parts;
  for (Outer& o : outers) {
    Geometry p;
    p.type = GeometryType::kPolygon;
    p.paths.push_back(std::move(o.ring));
    for (Path& h : o.holes) p.paths.push_back(std::move(h));
    parts.push_back(std::move(p));
  }
  return Geometry::Collection(GeometryType::kMultiPolygon, std::move(parts));
}

/// An Esri JSON geometry (point, multipoint, polyline, polygon) as a
/// `Geometry`; null for a missing one. kInvalidArg when malformed.
Status ReadEsriGeometry(const json& j, Geometry* out) {
  *out = Geometry();
  if (j.is_null() || (j.is_object() && j.empty())) return Status::Ok();
  if (!j.is_object()) return Status::Error(kInvalidArg, "Esri geometry is not an object");
  if (j.contains("x")) {
    out->type = GeometryType::kPoint;
    if (j["x"].is_number() && j.contains("y") && j["y"].is_number())
      out->paths.push_back(Path{Position{j["y"].get<double>(), j["x"].get<double>(), 0, 0}});
    return Status::Ok();
  }
  if (j.contains("points")) {
    Path pts;
    if (!ReadPath(j["points"], &pts)) return Status::Error(kInvalidArg, "bad Esri multipoint");
    std::vector<Geometry> parts;
    for (const Position& p : pts) {
      Geometry g;
      g.type = GeometryType::kPoint;
      g.paths.push_back(Path{p});
      parts.push_back(std::move(g));
    }
    *out = Geometry::Collection(GeometryType::kMultiPoint, std::move(parts));
    return Status::Ok();
  }
  if (j.contains("paths") && j["paths"].is_array()) {
    std::vector<Geometry> parts;
    for (const json& a : j["paths"]) {
      Geometry g;
      g.type = GeometryType::kLineString;
      g.paths.emplace_back();
      if (!ReadPath(a, &g.paths[0])) return Status::Error(kInvalidArg, "bad Esri polyline");
      parts.push_back(std::move(g));
    }
    *out = Geometry::Collection(GeometryType::kMultiLineString, std::move(parts));
    return Status::Ok();
  }
  if (j.contains("rings") && j["rings"].is_array()) {
    std::vector<Path> rings;
    for (const json& a : j["rings"]) {
      rings.emplace_back();
      if (!ReadPath(a, &rings.back())) return Status::Error(kInvalidArg, "bad Esri polygon");
    }
    *out = PolygonFromRings(std::move(rings));
    return Status::Ok();
  }
  return Status::Error(kInvalidArg, "unsupported Esri geometry (curves are not read)");
}

/// A single geometry wrapped in the layer's multi type, so every feature of a
/// line or area layer has the same type.
Geometry ToLayerType(Geometry g, GeometryType layer) {
  const bool wrap = (layer == GeometryType::kMultiPoint && g.type == GeometryType::kPoint) ||
                    (layer == GeometryType::kMultiLineString && g.type == GeometryType::kLineString) ||
                    (layer == GeometryType::kMultiPolygon && g.type == GeometryType::kPolygon);
  if (!wrap) return g;
  std::vector<Geometry> parts;
  parts.push_back(std::move(g));
  return Geometry::Collection(layer, std::move(parts));
}

/// A property value as the field's type; Esri dates arrive as epoch ms.
FieldValue ValueFor(const json& j, FieldType type) {
  if ((type == FieldType::kDateTime || type == FieldType::kDate) && j.is_number()) {
    std::string text = EsriDateText(static_cast<int64_t>(j.get<double>()));
    return type == FieldType::kDate ? text.substr(0, 10) : text;
  }
  FieldValue v;
  if (geojson_detail::JsonToValue(j, type, &v)) return v;
  if (type == FieldType::kInteger && j.is_number()) {
    const double d = j.get<double>();
    if (d == static_cast<double>(static_cast<int64_t>(d))) return static_cast<int64_t>(d);
  }
  return std::monostate{};
}

std::string Box(const GeoRect& box) {
  char text[128];
  std::snprintf(text, sizeof text, "%.9g,%.9g,%.9g,%.9g", box.ll.lon, box.ll.lat, box.ur.lon,
                box.ur.lat);
  return text;
}

std::string JoinIds(std::vector<int64_t>::const_iterator begin,
                    std::vector<int64_t>::const_iterator end) {
  std::string s;
  for (auto it = begin; it != end; ++it) {
    if (!s.empty()) s += ',';
    s += std::to_string(*it);
  }
  return s;
}

/// The ids in an "ids:1,2,3" cursor. False when it is malformed.
bool ParseIds(const std::string& list, std::vector<int64_t>* out) {
  size_t at = 0;
  while (at < list.size()) {
    size_t end = list.find(',', at);
    if (end == std::string::npos) end = list.size();
    const std::string part = list.substr(at, end - at);
    if (part.empty() || part.find_first_not_of("0123456789-") != std::string::npos) return false;
    out->push_back(std::stoll(part));
    at = end + 1;
  }
  return true;
}

}  // namespace

std::string EsriDateText(int64_t epoch_ms) {
  int64_t secs = epoch_ms / 1000;
  int ms = static_cast<int>(epoch_ms % 1000);
  if (ms < 0) ms += 1000, --secs;
  int64_t days = secs / 86400;
  int64_t rem = secs % 86400;
  if (rem < 0) rem += 86400, --days;
  // Civil date from days since 1970-01-01 (Hinnant's algorithm).
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const int64_t m = mp < 10 ? mp + 3 : mp - 9;
  const int64_t y = yoe + era * 400 + (m <= 2 ? 1 : 0);
  char text[40];
  std::snprintf(text, sizeof text, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld", static_cast<long long>(y),
                static_cast<long long>(m), static_cast<long long>(d), static_cast<long long>(rem / 3600),
                static_cast<long long>(rem / 60 % 60), static_cast<long long>(rem % 60));
  std::string out = text;
  if (ms != 0) {
    std::snprintf(text, sizeof text, ".%03d", ms);
    out += text;
  }
  return out + "Z";
}

struct EsriFeatureService::Impl {
  std::shared_ptr<IHttpClient> http;
  EsriOptions options;
  std::string service_url;
  std::string title;
  json layers;  ///< the service document's `layers`

  struct Layer {
    RemoteLayerInfo info;
    std::vector<std::string> esri_names;  ///< property name of each schema field
    std::string oid_field;
    std::string drawing_info;  ///< the layer's `drawingInfo` as JSON text; empty when none
    bool geojson = false;
    bool paginate = false;
    int max_records = 1000;
  };
  std::mutex mu;
  std::map<std::string, Layer> described;

  /// GETs `url` as JSON. An Esri error document is a failure.
  Status GetJson(const std::string& url, json* out, const std::atomic<bool>* cancel) {
    HttpRequest request;
    request.url = url;
    request.headers.emplace_back("Accept", "application/geo+json, application/json");
    HttpResponse response;
    Status s = http->Get(request, cancel, &response);
    if (!s.ok()) return s;
    if (response.status == 404) return Status::Error(kNotFound, "HTTP 404: " + url);
    if (response.status != 200)
      return Status::Error(kIoError, "HTTP " + std::to_string(response.status) + ": " + url);
    *out = json::parse(response.body, nullptr, /*allow_exceptions=*/false);
    if (!out->is_object()) return Status::Error(kInvalidArg, "not a JSON document: " + url);
    if (out->contains("error") && (*out)["error"].is_object()) {
      const json& e = (*out)["error"];
      const int code = e.value("code", 0);
      const std::string message = "Esri error " + std::to_string(code) + ": " + Str(e, "message");
      if (code == 404) return Status::Error(kNotFound, message);
      if (code == 498 || code == 499) return Status::Error(kUnsupported, message + " (needs a sign-in)");
      return Status::Error(kIoError, message);
    }
    return Status::Ok();
  }

  Status DescribeOne(const std::string& id, Layer* out, const std::atomic<bool>* cancel) {
    {
      std::lock_guard<std::mutex> lock(mu);
      auto it = described.find(id);
      if (it != described.end()) {
        *out = it->second;
        return Status::Ok();
      }
    }
    if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos)
      return Status::Error(kNotFound, "no Esri layer \"" + id + "\"");
    json doc;
    Status s = GetJson(service_url + "/" + id + "?f=json", &doc, cancel);
    if (!s.ok()) return s;
    const std::string type = Str(doc, "type");
    if (!type.empty() && type != "Feature Layer")
      return Status::Error(kUnsupported, "Esri layer " + id + " is a " + type + ", not a feature layer");
    const std::string capabilities = Str(doc, "capabilities");
    if (!capabilities.empty() && capabilities.find("Query") == std::string::npos)
      return Status::Error(kUnsupported, "Esri layer " + id + " does not allow queries");

    Layer layer;
    layer.info.id = id;
    layer.info.title = Str(doc, "name");
    if (layer.info.title.empty()) layer.info.title = id;
    layer.info.description = Str(doc, "description");
    layer.info.schema.geometry_type = LayerGeometryType(Str(doc, "geometryType"));
    layer.info.has_extent = ReadExtent(doc.value("extent", json()), &layer.info.extent);
    layer.oid_field = Str(doc, "objectIdField");
    for (const json& f : doc.value("fields", json::array())) {
      FieldDef d;
      if (!FieldFromEsri(f, &d)) continue;
      if (layer.oid_field.empty() && Str(f, "type") == "esriFieldTypeOID") layer.oid_field = d.name;
      layer.info.schema.fields.push_back(d);
      layer.esri_names.push_back(d.name);
    }
    if (layer.oid_field.empty())
      return Status::Error(kUnsupported, "Esri layer " + id + " has no object id field");
    if (doc.contains("drawingInfo") && doc["drawingInfo"].is_object())
      layer.drawing_info = doc["drawingInfo"].dump();
    layer.geojson = Lower(Str(doc, "supportedQueryFormats")).find("geojson") != std::string::npos;
    const json advanced = doc.value("advancedQueryCapabilities", json());
    layer.paginate = advanced.is_object() && advanced.value("supportsPagination", false);
    if (doc.contains("maxRecordCount") && doc["maxRecordCount"].is_number_integer() &&
        doc["maxRecordCount"].get<int>() > 0)
      layer.max_records = doc["maxRecordCount"].get<int>();

    std::lock_guard<std::mutex> lock(mu);
    described[id] = layer;
    *out = layer;
    return Status::Ok();
  }

  /// The features of one query response, in either format.
  Status ReadFeatures(const Layer& layer, const json& doc, const std::string& url,
                      std::vector<Feature>* out) {
    if (!doc.contains("features") || !doc["features"].is_array())
      return Status::Error(kInvalidArg, "no features array: " + url);
    const bool esri = !layer.geojson;
    if (esri && doc.contains("spatialReference") && !IsGeographic(doc["spatialReference"]))
      return Status::Error(kUnsupported, "Esri layer " + layer.info.id + " answered in a projected "
                                         "coordinate system, not WGS 84");
    const FeatureSchema& schema = layer.info.schema;
    for (const json& f : doc["features"]) {
      if (!f.is_object()) continue;
      Feature feature;
      const json geometry = f.contains("geometry") ? f["geometry"] : json(nullptr);
      Status g = esri ? ReadEsriGeometry(geometry, &feature.geometry)
                      : geojson_detail::ReadGeometry(geometry, &feature.geometry, 0);
      if (!g.ok()) {
        FV_LOG_WARNING("Esri layer " << layer.info.id << ": a feature skipped: " << g.message);
        continue;
      }
      feature.geometry = ToLayerType(std::move(feature.geometry), schema.geometry_type);
      const char* props_key = esri ? "attributes" : "properties";
      const json props = f.contains(props_key) && f[props_key].is_object() ? f[props_key] : json::object();
      json id = esri ? json() : f.value("id", json());
      if (!id.is_number_integer()) id = props.value(layer.oid_field, json());
      if (!id.is_number_integer() || id.get<int64_t>() == 0) {
        FV_LOG_WARNING("Esri layer " << layer.info.id << ": a feature without an object id skipped");
        continue;
      }
      feature.id = id.get<int64_t>();
      feature.values.resize(schema.fields.size());
      for (size_t k = 0; k < schema.fields.size(); ++k) {
        auto it = props.find(layer.esri_names[k]);
        if (it != props.end()) feature.values[k] = ValueFor(*it, schema.fields[k].type);
      }
      out->push_back(std::move(feature));
    }
    return Status::Ok();
  }
};

EsriFeatureService::EsriFeatureService(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EsriFeatureService::~EsriFeatureService() = default;

Status EsriFeatureService::Open(std::shared_ptr<IHttpClient> http, const std::string& url,
                                EsriOptions options, std::shared_ptr<EsriFeatureService>* out,
                                const std::atomic<bool>* cancel) {
  if (!http) return Status::Error(kInvalidArg, "Esri: no HTTP client");
  auto impl = std::make_unique<Impl>();
  impl->http = std::move(http);
  impl->options = options;
  std::string base = url.substr(0, url.find('?'));
  while (!base.empty() && base.back() == '/') base.pop_back();
  impl->service_url = base;
  json doc;
  Status s = impl->GetJson(base + "?f=json", &doc, cancel);
  if (!s.ok()) return s;
  if (!doc.contains("layers") || !doc["layers"].is_array()) {
    if (doc.contains("fields") && doc.contains("geometryType"))
      return Status::Error(kUnsupported, "an Esri layer, not a service; open " +
                                             base.substr(0, base.rfind('/')) + " instead");
    return Status::Error(kUnsupported, "not an Esri FeatureServer or MapServer: " + url);
  }
  impl->layers = doc["layers"];
  impl->title = Str(doc.value("documentInfo", json()), "Title");
  if (impl->title.empty()) {
    // .../services/<folder>/<name>/FeatureServer: the name is the segment before the type.
    const size_t type_at = base.rfind('/');
    const size_t name_at = type_at == std::string::npos ? std::string::npos : base.rfind('/', type_at - 1);
    if (name_at != std::string::npos) impl->title = base.substr(name_at + 1, type_at - name_at - 1);
  }
  out->reset(new EsriFeatureService(std::move(impl)));
  return Status::Ok();
}

Status EsriFeatureService::ListLayers(std::vector<RemoteLayerInfo>* out, const std::atomic<bool>*) {
  for (const json& l : impl_->layers) {
    if (!l.is_object() || !l.contains("id") || !l["id"].is_number_integer()) continue;
    const json sub = l.value("subLayerIds", json());
    if (sub.is_array() && !sub.empty()) continue;  // a group layer; its members are listed too
    const std::string type = Str(l, "type");
    if (!type.empty() && type != "Feature Layer") continue;
    RemoteLayerInfo info;
    info.id = std::to_string(l["id"].get<int64_t>());
    info.title = Str(l, "name");
    if (info.title.empty()) info.title = info.id;
    info.schema.geometry_type = LayerGeometryType(Str(l, "geometryType"));
    out->push_back(std::move(info));
  }
  return Status::Ok();
}

Status EsriFeatureService::Describe(const std::string& layer_id, RemoteLayerInfo* out,
                                    const std::atomic<bool>* cancel) {
  Impl::Layer layer;
  Status s = impl_->DescribeOne(layer_id, &layer, cancel);
  if (s.ok()) *out = layer.info;
  return s;
}

Status EsriFeatureService::FetchPage(const std::string& layer_id, const GeoRect& box,
                                     const std::string& cursor, std::vector<Feature>* out,
                                     std::string* next, const std::atomic<bool>* cancel) {
  next->clear();
  Impl::Layer layer;
  Status s = impl_->DescribeOne(layer_id, &layer, cancel);
  if (!s.ok()) return s;
  int page = layer.max_records;
  if (impl_->options.page_size > 0 && impl_->options.page_size < page) page = impl_->options.page_size;
  const std::string query = impl_->service_url + "/" + layer_id + "/query?";
  const std::string spatial = "where=1%3D1&geometry=" + Box(box) +
                              "&geometryType=esriGeometryEnvelope&inSR=4326"
                              "&spatialRel=esriSpatialRelIntersects";
  const std::string format = layer.geojson ? "&f=geojson" : "&f=json";
  const std::string fields = "&outFields=*&returnGeometry=true&outSR=4326";

  if (cursor.empty() ? layer.paginate : cursor.rfind("offset:", 0) == 0) {
    const long long offset = cursor.empty() ? 0 : std::atoll(cursor.c_str() + 7);
    const std::string url = query + spatial + fields + "&orderByFields=" + layer.oid_field +
                            "&resultOffset=" + std::to_string(offset) +
                            "&resultRecordCount=" + std::to_string(page) + format;
    json doc;
    s = impl_->GetJson(url, &doc, cancel);
    if (!s.ok()) return s;
    s = impl_->ReadFeatures(layer, doc, url, out);
    if (!s.ok()) return s;
    // Esri GeoJSON puts the flag in `properties`; Esri JSON at the top.
    const bool exceeded = doc.value("exceededTransferLimit", false) ||
                          doc.value("properties", json::object()).value("exceededTransferLimit", false);
    const size_t received = doc["features"].size();
    if (exceeded && received > 0) *next = "offset:" + std::to_string(offset + received);
    return Status::Ok();
  }

  // No pagination: the ids in the box, then the features in blocks of ids.
  std::vector<int64_t> ids;
  if (cursor.empty()) {
    json doc;
    s = impl_->GetJson(query + spatial + "&returnIdsOnly=true&f=json", &doc, cancel);
    if (!s.ok()) return s;
    for (const json& id : doc.value("objectIds", json::array()))
      if (id.is_number_integer()) ids.push_back(id.get<int64_t>());
    std::sort(ids.begin(), ids.end());
  } else if (cursor.rfind("ids:", 0) != 0 || !ParseIds(cursor.substr(4), &ids)) {
    return Status::Error(kInvalidArg, "bad Esri page cursor: " + cursor);
  }
  if (ids.empty()) return Status::Ok();
  const size_t take = std::min(ids.size(), static_cast<size_t>(page));
  const std::string url = query + "objectIds=" + JoinIds(ids.begin(), ids.begin() + take) + fields + format;
  json doc;
  s = impl_->GetJson(url, &doc, cancel);
  if (!s.ok()) return s;
  s = impl_->ReadFeatures(layer, doc, url, out);
  if (!s.ok()) return s;
  if (take < ids.size()) *next = "ids:" + JoinIds(ids.begin() + take, ids.end());
  return Status::Ok();
}

Status EsriFeatureService::DrawingInfo(const std::string& layer_id, std::string* out,
                                       const std::atomic<bool>* cancel) {
  Impl::Layer layer;
  Status s = impl_->DescribeOne(layer_id, &layer, cancel);
  if (s.ok()) *out = layer.drawing_info;
  return s;
}

const std::string& EsriFeatureService::title() const { return impl_->title; }

}  // namespace web
}  // namespace fv
