// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// OGC API – Features client: landing page, collections, schema or
/// queryables, and items paged by `next` links.

#include "fvkit/feature/web/ogc_api.h"

#include <cstdio>
#include <map>
#include <mutex>
#include <set>

#include "../geojson_detail.h"
#include "fvkit/log.h"

namespace fv {
namespace web {
namespace {

using geojson_detail::json;

constexpr const char* kRelData = "http://www.opengis.net/def/rel/ogc/1.0/data";
constexpr const char* kRelSchema = "http://www.opengis.net/def/rel/ogc/1.0/schema";
constexpr const char* kRelQueryables = "http://www.opengis.net/def/rel/ogc/1.0/queryables";

std::string Str(const json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return "";
  return j[key].get<std::string>();
}

/// `href` made absolute against the document it came from.
std::string ResolveUrl(const std::string& base, const std::string& href) {
  if (href.find("://") != std::string::npos) return href;
  const size_t scheme_end = base.find("://");
  if (scheme_end == std::string::npos) return href;
  if (href.rfind("//", 0) == 0) return base.substr(0, scheme_end + 1) + href;
  const size_t path_start = base.find('/', scheme_end + 3);
  const std::string origin = path_start == std::string::npos ? base : base.substr(0, path_start);
  if (!href.empty() && href[0] == '/') return origin + href;
  std::string dir = base.substr(0, base.find('?'));
  dir = dir.substr(0, dir.rfind('/') + 1);
  if (dir.size() <= origin.size()) dir = origin + "/";
  return dir + href;
}

std::string WithQuery(const std::string& url, const std::string& query) {
  return url + (url.find('?') == std::string::npos ? "?" : "&") + query;
}

/// The href of the first link with relation `rel` (or `alt_rel`), preferring
/// one whose type contains `prefer`; empty when there is none.
std::string FindLink(const json& doc, const std::string& base, const std::string& rel,
                     const std::string& alt_rel, const std::vector<std::string>& prefer) {
  if (!doc.is_object() || !doc.contains("links") || !doc["links"].is_array()) return "";
  std::string fallback;
  for (const std::string& want : prefer) {
    for (const json& link : doc["links"]) {
      const std::string r = Str(link, "rel");
      if (r != rel && (alt_rel.empty() || r != alt_rel)) continue;
      const std::string type = Str(link, "type");
      const std::string href = Str(link, "href");
      if (href.empty()) continue;
      if (fallback.empty() && type.empty()) fallback = href;
      if (!want.empty() && type.find(want) != std::string::npos) return ResolveUrl(base, href);
    }
  }
  return fallback.empty() ? "" : ResolveUrl(base, fallback);
}

const std::vector<std::string> kJsonTypes = {"geo+json", "schema+json", "application/json"};

uint64_t Fnv1a(const std::string& text) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : text) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

/// A JSON Schema property as a field. False for the geometry property.
bool FieldFromSchema(const std::string& name, const json& p, FieldDef* out, GeometryType* geometry) {
  if (!p.is_object()) return false;
  const std::string format = Str(p, "format");
  const std::string ref = Str(p, "$ref");
  if (Str(p, "x-ogc-role") == "primary-geometry" || format.rfind("geometry", 0) == 0 ||
      ref.find("geojson") != std::string::npos) {
    static const std::map<std::string, GeometryType> kFormats = {
        {"geometry-point", GeometryType::kPoint},
        {"geometry-multipoint", GeometryType::kMultiPoint},
        {"geometry-linestring", GeometryType::kLineString},
        {"geometry-multilinestring", GeometryType::kMultiLineString},
        {"geometry-polygon", GeometryType::kPolygon},
        {"geometry-multipolygon", GeometryType::kMultiPolygon}};
    auto it = kFormats.find(format);
    if (it != kFormats.end()) *geometry = it->second;
    return false;
  }
  FieldDef f;
  f.name = name;
  // Servers backed by Elasticsearch (pygeoapi, GeoMet) publish the index's
  // own type names, and a nullable property may list its types as an array.
  static const std::map<std::string, std::string> kStoreTypes = {
      {"long", "integer"},  {"int", "integer"},     {"short", "integer"},       {"byte", "integer"},
      {"double", "number"}, {"float", "number"},    {"half_float", "number"},   {"scaled_float", "number"},
      {"bool", "boolean"},  {"keyword", "string"},  {"text", "string"}};
  std::string type = Str(p, "type");
  if (type.empty() && p.contains("type") && p["type"].is_array())
    for (const json& t : p["type"])
      if (t.is_string() && t.get<std::string>() != "null") {
        type = t.get<std::string>();
        break;
      }
  if (auto it = kStoreTypes.find(type); it != kStoreTypes.end()) type = it->second;
  if (type == "integer") {
    f.type = FieldType::kInteger;
  } else if (type == "number") {
    f.type = FieldType::kReal;
  } else if (type == "boolean") {
    f.type = FieldType::kBoolean;
  } else if (type == "string" && format == "date") {
    f.type = FieldType::kDate;
  } else if (type == "string" && format == "date-time") {
    f.type = FieldType::kDateTime;
  } else {
    f.type = FieldType::kText;
  }
  f.title = Str(p, "title");
  if (f.title == name) f.title.clear();
  f.description = Str(p, "description");
  while (!f.description.empty() && (f.description.back() == '\n' || f.description.back() == ' '))
    f.description.pop_back();
  *out = f;
  return true;
}

/// A property value as the field's type; a value of another type becomes the
/// nearest the field can hold, or null.
FieldValue ValueFor(const json& j, FieldType type) {
  FieldValue v;
  if (geojson_detail::JsonToValue(j, type, &v)) return v;
  if (type == FieldType::kInteger && j.is_number()) {
    double d = j.get<double>();
    if (d == static_cast<double>(static_cast<int64_t>(d))) return static_cast<int64_t>(d);
  }
  if ((type == FieldType::kDate || type == FieldType::kDateTime) && !j.is_null()) return j.dump();
  return std::monostate{};
}

}  // namespace

int64_t OgcFeatureId(const std::string& text) {
  if (!text.empty() && text.size() <= 18 && text.find_first_not_of("0123456789") == std::string::npos) {
    int64_t v = std::stoll(text);
    if (v > 0) return v;
  }
  int64_t h = static_cast<int64_t>(Fnv1a(text) & 0x7fffffffffffffffull);
  return h == 0 ? 1 : h;
}

struct OgcApiFeatures::Impl {
  std::shared_ptr<IHttpClient> http;
  OgcApiOptions options;
  std::string collections_url;
  std::string title;

  struct Collection {
    RemoteLayerInfo info;
    std::string items_url;
    std::string schema_url;      ///< Part 5 schema document
    std::string queryables_url;  ///< Part 3 queryables, tried when the schema is missing
    bool described = false;
  };
  std::mutex mu;
  bool listed = false;
  std::vector<Collection> collections;

  /// GETs `url` as JSON. A server that answers HTML is asked again with `f=json`.
  Status GetJson(const std::string& url, json* out, const std::atomic<bool>* cancel) {
    for (int attempt = 0; attempt < 2; ++attempt) {
      HttpRequest request;
      request.url = attempt == 0 ? url : WithQuery(url, "f=json");
      request.headers.emplace_back("Accept",
                                   "application/geo+json, application/schema+json, application/json;q=0.9");
      HttpResponse response;
      Status s = http->Get(request, cancel, &response);
      if (!s.ok()) return s;
      if (response.status == 404)
        return Status::Error(kNotFound, "HTTP 404: " + request.url);
      if (response.status != 200)
        return Status::Error(kIoError, "HTTP " + std::to_string(response.status) + ": " + request.url);
      *out = json::parse(response.body, nullptr, /*allow_exceptions=*/false);
      if (out->is_object()) return Status::Ok();
      if (url.find("f=") != std::string::npos) break;
    }
    return Status::Error(kInvalidArg, "not a JSON document: " + url);
  }

  /// Reads the collections document once.
  Status List(const std::atomic<bool>* cancel) {
    {
      std::lock_guard<std::mutex> lock(mu);
      if (listed) return Status::Ok();
    }
    json doc;
    Status s = GetJson(collections_url, &doc, cancel);
    if (!s.ok()) return s;
    if (!doc.contains("collections") || !doc["collections"].is_array())
      return Status::Error(kInvalidArg, "no collections in " + collections_url);
    std::vector<Collection> found;
    for (const json& c : doc["collections"]) {
      const std::string item_type = Str(c, "itemType");
      if (!item_type.empty() && item_type != "feature") continue;
      Collection col;
      col.info.id = Str(c, "id");
      if (col.info.id.empty()) continue;
      col.info.title = Str(c, "title");
      if (col.info.title.empty()) col.info.title = col.info.id;
      col.info.description = Str(c, "description");
      if (c.contains("extent") && c["extent"].is_object() && c["extent"].contains("spatial")) {
        const json& bbox = c["extent"]["spatial"].value("bbox", json());
        const json& b = bbox.is_array() && !bbox.empty() && bbox[0].is_array() ? bbox[0] : bbox;
        if (b.is_array() && (b.size() == 4 || b.size() == 6) && b[0].is_number()) {
          const size_t k = b.size() / 2;
          col.info.extent.ll = GeoPoint{b[1].get<double>(), b[0].get<double>()};
          col.info.extent.ur = GeoPoint{b[k + 1].get<double>(), b[k].get<double>()};
          col.info.has_extent = true;
        }
      }
      const std::string own = collections_url.substr(0, collections_url.find('?')) + "/" + col.info.id;
      col.items_url = FindLink(c, collections_url, "items", "", kJsonTypes);
      if (col.items_url.empty()) col.items_url = own + "/items";
      col.schema_url = FindLink(c, collections_url, kRelSchema, "schema", kJsonTypes);
      col.queryables_url = FindLink(c, collections_url, kRelQueryables, "queryables", kJsonTypes);
      if (col.queryables_url.empty()) col.queryables_url = own + "/queryables";
      found.push_back(std::move(col));
    }
    std::lock_guard<std::mutex> lock(mu);
    if (!listed) {
      collections = std::move(found);
      listed = true;
    }
    return Status::Ok();
  }

  /// Copies the collection `id` into `*out`. kNotFound when there is none.
  Status Find(const std::string& id, Collection* out) {
    std::lock_guard<std::mutex> lock(mu);
    for (const Collection& c : collections)
      if (c.info.id == id) {
        *out = c;
        return Status::Ok();
      }
    return Status::Error(kNotFound, "no feature collection \"" + id + "\"");
  }

  /// Fills the schema of collection `id` from its schema document, else its
  /// queryables, else fields inferred from a sample of its items.
  Status DescribeOne(const std::string& id, RemoteLayerInfo* out, const std::atomic<bool>* cancel) {
    Status s = List(cancel);
    if (!s.ok()) return s;
    Collection col;
    s = Find(id, &col);
    if (!s.ok()) return s;
    if (col.described) {
      *out = col.info;
      return Status::Ok();
    }

    FeatureSchema schema;
    json doc;
    s = Status::Error(kNotFound, "");
    for (const std::string& url : {col.schema_url, col.queryables_url}) {
      if (url.empty()) continue;
      s = GetJson(url, &doc, cancel);
      if (s.code == kInterrupted) return s;
      if (s.ok() && doc.contains("properties") && doc["properties"].is_object()) break;
      if (s.ok()) s = Status::Error(kInvalidArg, "no properties in " + url);
    }
    if (s.ok()) {
      for (const auto& item : doc["properties"].items()) {
        FieldDef f;
        if (FieldFromSchema(item.key(), item.value(), &f, &schema.geometry_type))
          schema.fields.push_back(f);
      }
    } else {
      if (!s.ok() && s.code != kNotFound)
        FV_LOG_WARNING("OGC API " << id << ": schema: " << s.message);
      json items;
      s = GetJson(WithQuery(col.items_url, "limit=" + std::to_string(options.sample_limit)), &items, cancel);
      if (!s.ok()) return s;
      std::vector<std::string> order;
      std::map<std::string, geojson_detail::Seen> seen;
      std::set<GeometryType> types;
      for (const json& f : items.value("features", json::array())) {
        Geometry g;
        if (f.contains("geometry") && geojson_detail::ReadGeometry(f["geometry"], &g, 0).ok() && !g.IsNull())
          types.insert(g.type);
        if (!f.contains("properties") || !f["properties"].is_object()) continue;
        for (const auto& item : f["properties"].items()) {
          if (seen.find(item.key()) == seen.end()) order.push_back(item.key());
          geojson_detail::Observe(item.value(), &seen[item.key()]);
        }
      }
      for (const std::string& key : order) schema.fields.push_back(geojson_detail::Infer(key, seen[key]));
      if (types.size() == 1) schema.geometry_type = *types.begin();
    }

    std::lock_guard<std::mutex> lock(mu);
    for (Collection& c : collections)
      if (c.info.id == id) {
        c.info.schema = schema;
        c.described = true;
        *out = c.info;
      }
    return Status::Ok();
  }
};

OgcApiFeatures::OgcApiFeatures(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OgcApiFeatures::~OgcApiFeatures() = default;

Status OgcApiFeatures::Open(std::shared_ptr<IHttpClient> http, const std::string& url,
                           OgcApiOptions options, std::shared_ptr<OgcApiFeatures>* out,
                           const std::atomic<bool>* cancel) {
  if (!http) return Status::Error(kInvalidArg, "OGC API: no HTTP client");
  auto impl = std::make_unique<Impl>();
  impl->http = std::move(http);
  impl->options = options;
  json doc;
  Status s = impl->GetJson(url, &doc, cancel);
  if (!s.ok()) return s;
  if (doc.contains("collections") && doc["collections"].is_array()) {
    impl->collections_url = url;
  } else {
    impl->collections_url = FindLink(doc, url, "data", kRelData, {"application/json", ""});
    if (impl->collections_url.empty())
      return Status::Error(kUnsupported, "not an OGC API landing page: " + url);
  }
  impl->title = Str(doc, "title");
  out->reset(new OgcApiFeatures(std::move(impl)));
  return Status::Ok();
}

Status OgcApiFeatures::ListLayers(std::vector<RemoteLayerInfo>* out, const std::atomic<bool>* cancel) {
  Status s = impl_->List(cancel);
  if (!s.ok()) return s;
  std::lock_guard<std::mutex> lock(impl_->mu);
  for (const Impl::Collection& c : impl_->collections) out->push_back(c.info);
  return Status::Ok();
}

Status OgcApiFeatures::Describe(const std::string& layer_id, RemoteLayerInfo* out,
                                const std::atomic<bool>* cancel) {
  return impl_->DescribeOne(layer_id, out, cancel);
}

Status OgcApiFeatures::FetchPage(const std::string& layer_id, const GeoRect& box,
                                 const std::string& cursor, std::vector<Feature>* out,
                                 std::string* next, const std::atomic<bool>* cancel) {
  next->clear();
  RemoteLayerInfo info;
  Status s = impl_->DescribeOne(layer_id, &info, cancel);
  if (!s.ok()) return s;
  Impl::Collection col;
  s = impl_->Find(layer_id, &col);
  if (!s.ok()) return s;

  std::string url = cursor;
  if (url.empty()) {
    char bbox[128];
    std::snprintf(bbox, sizeof bbox, "bbox=%.9g,%.9g,%.9g,%.9g", box.ll.lon, box.ll.lat, box.ur.lon,
                  box.ur.lat);
    url = WithQuery(col.items_url, std::string(bbox) + "&limit=" + std::to_string(impl_->options.limit));
  }
  json doc;
  s = impl_->GetJson(url, &doc, cancel);
  if (!s.ok()) return s;
  if (!doc.contains("features") || !doc["features"].is_array())
    return Status::Error(kInvalidArg, "no features array: " + url);

  const FeatureSchema& schema = info.schema;
  for (const json& f : doc["features"]) {
    if (!f.is_object()) continue;
    Feature feature;
    Status g = geojson_detail::ReadGeometry(f.contains("geometry") ? f["geometry"] : json(nullptr),
                                            &feature.geometry, 0);
    if (!g.ok()) {
      FV_LOG_WARNING("OGC API " << layer_id << ": a feature skipped: " << g.message);
      continue;
    }
    const json id = f.value("id", json());
    if (id.is_number_integer() && id.get<int64_t>() > 0)
      feature.id = id.get<int64_t>();
    else
      feature.id = OgcFeatureId(id.is_string() ? id.get<std::string>() : f.dump());
    feature.values.resize(schema.fields.size());
    if (f.contains("properties") && f["properties"].is_object()) {
      const json& props = f["properties"];
      for (size_t k = 0; k < schema.fields.size(); ++k) {
        auto it = props.find(schema.fields[k].name);
        if (it != props.end()) feature.values[k] = ValueFor(*it, schema.fields[k].type);
      }
    }
    out->push_back(std::move(feature));
  }
  if (!doc["features"].empty()) {
    std::string n = FindLink(doc, url, "next", "", kJsonTypes);
    if (n != url) *next = n;
  }
  return Status::Ok();
}

const std::string& OgcApiFeatures::title() const { return impl_->title; }

}  // namespace web
}  // namespace fv
