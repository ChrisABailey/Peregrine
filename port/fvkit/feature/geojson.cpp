// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/geojson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace fv {

namespace {

// Ordered, so fields keep the order the file gives them.
using json = nlohmann::ordered_json;

constexpr const char* kSchemaMember = "fv_schema";
constexpr const char* kStyleMember = "fv_style";
constexpr int kSchemaVersion = 1;

Status Bad(const std::string& why) { return Status::Error(kInvalidArg, "GeoJSON: " + why); }
/// A geometry problem; the caller names the feature and adds the prefix.
Status Raw(const std::string& why) { return Status::Error(kInvalidArg, why); }

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// MARK: Geometry in

const std::pair<const char*, GeometryType> kGeoJsonTypes[] = {
    {"Point", GeometryType::kPoint},
    {"LineString", GeometryType::kLineString},
    {"Polygon", GeometryType::kPolygon},
    {"MultiPoint", GeometryType::kMultiPoint},
    {"MultiLineString", GeometryType::kMultiLineString},
    {"MultiPolygon", GeometryType::kMultiPolygon},
    {"GeometryCollection", GeometryType::kGeometryCollection},
};

const char* GeoJsonTypeName(GeometryType t) {
  for (const auto& entry : kGeoJsonTypes)
    if (entry.second == t) return entry.first;
  return "GeometryCollection";
}

Status ReadPosition(const json& j, Position* p, bool* has_z) {
  if (!j.is_array() || j.size() < 2 || !j[0].is_number() || !j[1].is_number())
    return Raw("a position is not [lon, lat]");
  p->lon = j[0].get<double>();
  p->lat = j[1].get<double>();
  if (j.size() >= 3 && j[2].is_number()) {
    p->z = j[2].get<double>();
    *has_z = true;
  }
  return Status::Ok();
}

Status ReadPath(const json& j, Path* path, bool* has_z) {
  if (!j.is_array()) return Raw("coordinates are not an array");
  path->resize(j.size());
  for (size_t i = 0; i < j.size(); ++i) {
    Status s = ReadPosition(j[i], &(*path)[i], has_z);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

Status ReadRings(const json& j, std::vector<Path>* rings, bool* has_z) {
  if (!j.is_array()) return Raw("polygon rings are not an array");
  rings->resize(j.size());
  for (size_t i = 0; i < j.size(); ++i) {
    Path& ring = (*rings)[i];
    Status s = ReadPath(j[i], &ring, has_z);
    if (!s.ok()) return s;
    // Unclosed rings are closed rather than refused.
    if (!ring.empty() &&
        (ring.front().lat != ring.back().lat || ring.front().lon != ring.back().lon))
      ring.push_back(ring.front());
  }
  return Status::Ok();
}

Status ReadGeometry(const json& j, Geometry* out, int depth) {
  if (j.is_null()) {
    *out = Geometry{};
    return Status::Ok();
  }
  if (depth > 32) return Raw("geometry nested deeper than 32");
  if (!j.is_object() || !j.contains("type") || !j["type"].is_string())
    return Raw("a geometry has no type");
  const std::string type = j["type"].get<std::string>();
  Geometry g;
  bool known = false;
  for (const auto& entry : kGeoJsonTypes) {
    if (type == entry.first) {
      g.type = entry.second;
      known = true;
    }
  }
  if (!known) return Raw("unknown geometry type \"" + type + "\"");

  if (g.type == GeometryType::kGeometryCollection) {
    if (!j.contains("geometries") || !j["geometries"].is_array())
      return Raw("a GeometryCollection has no geometries");
    for (const json& part : j["geometries"]) {
      Geometry p;
      Status s = ReadGeometry(part, &p, depth + 1);
      if (!s.ok()) return s;
      if (p.IsNull()) return Raw("a GeometryCollection holds a null geometry");
      g.parts.push_back(std::move(p));
    }
    *out = Geometry::Collection(g.type, std::move(g.parts));
    return Status::Ok();
  }

  if (!j.contains("coordinates")) return Raw(type + " has no coordinates");
  const json& c = j["coordinates"];
  bool z = false;
  Status s = Status::Ok();
  switch (g.type) {
    case GeometryType::kPoint: {
      if (c.is_array() && c.empty()) break;  // the empty point
      Position p;
      s = ReadPosition(c, &p, &z);
      if (s.ok()) g.paths.push_back({p});
      break;
    }
    case GeometryType::kLineString:
      g.paths.emplace_back();
      s = ReadPath(c, &g.paths[0], &z);
      break;
    case GeometryType::kPolygon:
      s = ReadRings(c, &g.paths, &z);
      break;
    default: {
      if (!c.is_array()) return Raw(type + " coordinates are not an array");
      const GeometryType member = g.type == GeometryType::kMultiPoint        ? GeometryType::kPoint
                                  : g.type == GeometryType::kMultiLineString ? GeometryType::kLineString
                                                                             : GeometryType::kPolygon;
      for (const json& pc : c) {
        Geometry part;
        part.type = member;
        bool pz = false;
        if (member == GeometryType::kPoint) {
          // [] is an empty member point, as the writer emits it.
          if (pc.is_array() && pc.empty()) {
            g.parts.push_back(std::move(part));
            continue;
          }
          Position p;
          s = ReadPosition(pc, &p, &pz);
          part.paths.push_back({p});
        } else if (member == GeometryType::kLineString) {
          part.paths.emplace_back();
          s = ReadPath(pc, &part.paths[0], &pz);
        } else {
          s = ReadRings(pc, &part.paths, &pz);
        }
        if (!s.ok()) return s;
        part.has_z = pz;
        z = z || pz;
        g.parts.push_back(std::move(part));
      }
      // A Z part makes the whole Z; the others read as z = 0.
      for (Geometry& part : g.parts) part.has_z = z;
      break;
    }
  }
  if (!s.ok()) return s;
  g.has_z = z;
  *out = std::move(g);
  return Status::Ok();
}

// MARK: Geometry out

/// Twice the signed area in lon/lat; positive is counter-clockwise.
double SignedArea2(const Path& ring) {
  double a = 0.0;
  for (size_t i = 0; i + 1 < ring.size(); ++i)
    a += ring[i].lon * ring[i + 1].lat - ring[i + 1].lon * ring[i].lat;
  return a;
}

json WritePosition(const Position& p, bool z) {
  json j = json::array({p.lon, p.lat});
  if (z) j.push_back(p.z);
  return j;
}

json WritePath(const Path& path, bool z) {
  json j = json::array();
  for (const Position& p : path) j.push_back(WritePosition(p, z));
  return j;
}

json WriteRings(const std::vector<Path>& rings, bool z) {
  json j = json::array();
  for (size_t i = 0; i < rings.size(); ++i) {
    Path ring = rings[i];
    const bool ccw = SignedArea2(ring) > 0;
    if ((i == 0) != ccw) std::reverse(ring.begin(), ring.end());
    j.push_back(WritePath(ring, z));
  }
  return j;
}

json WriteGeometry(const Geometry& g) {
  if (g.IsNull()) return nullptr;
  json j;
  j["type"] = GeoJsonTypeName(g.type);
  const bool z = g.has_z;
  switch (g.type) {
    case GeometryType::kPoint:
      j["coordinates"] = g.paths.empty() || g.paths[0].empty() ? json::array()
                                                               : WritePosition(g.paths[0][0], z);
      break;
    case GeometryType::kLineString:
      j["coordinates"] = WritePath(g.paths.empty() ? Path{} : g.paths[0], z);
      break;
    case GeometryType::kPolygon:
      j["coordinates"] = WriteRings(g.paths, z);
      break;
    case GeometryType::kGeometryCollection: {
      json parts = json::array();
      for (const Geometry& p : g.parts) parts.push_back(WriteGeometry(p));
      j["geometries"] = parts;
      break;
    }
    default: {
      json coords = json::array();
      for (const Geometry& p : g.parts) {
        if (p.type == GeometryType::kPoint)
          coords.push_back(p.paths.empty() || p.paths[0].empty() ? json::array()
                                                                 : WritePosition(p.paths[0][0], z));
        else if (p.type == GeometryType::kLineString)
          coords.push_back(WritePath(p.paths.empty() ? Path{} : p.paths[0], z));
        else
          coords.push_back(WriteRings(p.paths, z));
      }
      j["coordinates"] = coords;
      break;
    }
  }
  return j;
}

// MARK: Schema member

json ValueToJson(const FieldValue& v) {
  if (const auto* b = std::get_if<bool>(&v)) return *b;
  if (const auto* i = std::get_if<int64_t>(&v)) return *i;
  if (const auto* d = std::get_if<double>(&v)) return std::isfinite(*d) ? json(*d) : json(nullptr);
  if (const auto* s = std::get_if<std::string>(&v)) return *s;
  return nullptr;
}

/// A JSON value as the field's storage type. False when it cannot be one.
bool JsonToValue(const json& j, FieldType type, FieldValue* out) {
  if (j.is_null()) {
    *out = std::monostate{};
    return true;
  }
  switch (type) {
    case FieldType::kBoolean:
      if (!j.is_boolean()) return false;
      *out = j.get<bool>();
      return true;
    case FieldType::kInteger:
      if (!j.is_number_integer()) return false;
      *out = j.get<int64_t>();
      return true;
    case FieldType::kReal:
      if (!j.is_number()) return false;
      *out = j.get<double>();
      return true;
    case FieldType::kText:
      // Inferred text fields hold whatever the file had, as text.
      *out = j.is_string() ? j.get<std::string>() : j.dump();
      return true;
    case FieldType::kDate:
    case FieldType::kDateTime:
      if (!j.is_string()) return false;
      *out = j.get<std::string>();
      return true;
    case FieldType::kBlob:
      return false;
  }
  return false;
}

json SchemaToJson(const FeatureSchema& schema) {
  json fields = json::array();
  for (const FieldDef& f : schema.fields) {
    json jf;
    jf["name"] = f.name;
    jf["type"] = ToString(f.type);
    if (f.role != FieldRole::kNone) jf["role"] = ToString(f.role);
    if (!f.title.empty()) jf["title"] = f.title;
    if (!f.description.empty()) jf["description"] = f.description;
    if (!f.unit.empty()) jf["unit"] = f.unit;
    if (f.required) jf["required"] = true;
    if (!IsNull(f.default_value)) jf["default"] = ValueToJson(f.default_value);
    if (!f.choices.empty()) {
      json choices = json::array();
      for (const FieldChoice& c : f.choices) {
        json jc;
        jc["value"] = c.value;
        if (!c.label.empty()) jc["label"] = c.label;
        choices.push_back(jc);
      }
      jf["choices"] = choices;
    }
    if (f.min) jf["min"] = *f.min;
    if (f.max) jf["max"] = *f.max;
    if (!f.range_end.empty()) jf["range_end"] = f.range_end;
    fields.push_back(jf);
  }
  json j;
  j["version"] = kSchemaVersion;
  j["geometry_type"] = GeometryTypeName(schema.geometry_type);
  j["fields"] = fields;
  return j;
}

std::string StringMember(const json& j, const char* key) {
  return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

Status SchemaFromJson(const json& j, FeatureSchema* out) {
  if (!j.is_object() || !j.contains("fields") || !j["fields"].is_array())
    return Bad("fv_schema has no fields");
  FeatureSchema schema;
  const std::string gt = StringMember(j, "geometry_type");
  if (!gt.empty() && !GeometryTypeFromName(gt.c_str(), &schema.geometry_type))
    return Bad("fv_schema geometry type \"" + gt + "\" is unknown");
  for (const json& jf : j["fields"]) {
    if (!jf.is_object()) return Bad("fv_schema field is not an object");
    FieldDef f;
    f.name = StringMember(jf, "name");
    if (!FieldTypeFromString(StringMember(jf, "type"), &f.type))
      return Bad("fv_schema field \"" + f.name + "\" has an unknown type");
    if (!FieldRoleFromString(StringMember(jf, "role"), &f.role))
      return Bad("fv_schema field \"" + f.name + "\" has an unknown role");
    f.title = StringMember(jf, "title");
    f.description = StringMember(jf, "description");
    f.unit = StringMember(jf, "unit");
    f.required = jf.contains("required") && jf["required"].is_boolean() && jf["required"].get<bool>();
    if (jf.contains("default") && !JsonToValue(jf["default"], f.type, &f.default_value))
      return Bad("fv_schema field \"" + f.name + "\" has a default of the wrong type");
    if (jf.contains("choices") && jf["choices"].is_array()) {
      for (const json& jc : jf["choices"])
        if (jc.is_object()) f.choices.push_back({StringMember(jc, "value"), StringMember(jc, "label")});
    }
    if (jf.contains("min") && jf["min"].is_number()) f.min = jf["min"].get<double>();
    if (jf.contains("max") && jf["max"].is_number()) f.max = jf["max"].get<double>();
    f.range_end = StringMember(jf, "range_end");
    schema.fields.push_back(std::move(f));
  }
  Status s = schema.Validate();
  if (!s.ok()) return Bad("fv_schema: " + s.message);
  *out = std::move(schema);
  return Status::Ok();
}

// MARK: Inference

const std::regex& DateRe() {
  static const std::regex re(R"(^\d{4}-\d{2}-\d{2}$)");
  return re;
}

const std::regex& DateTimeRe() {
  static const std::regex re(
      R"(^\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}(:\d{2}(\.\d+)?)?(Z|[+-]\d{2}:?\d{2})?$)");
  return re;
}

const std::regex& EmailRe() {
  static const std::regex re(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)");
  return re;
}

bool IsUrl(const std::string& s) {
  const std::string l = Lower(s.substr(0, 8));
  return l.rfind("http://", 0) == 0 || l.rfind("https://", 0) == 0;
}

/// What the values of one property have been seen to be.
struct Seen {
  bool any = false;  ///< a non-null value
  bool boolean = false, integer = false, real = false, other = false;
  bool text = false;  ///< a string that is not a date or datetime
  bool date = false, datetime = false;
  bool all_url = true, all_email = true;
};

void Observe(const json& v, Seen* s) {
  if (v.is_null()) return;
  s->any = true;
  if (v.is_boolean()) {
    s->boolean = true;
  } else if (v.is_number_integer()) {
    s->integer = true;
  } else if (v.is_number()) {
    s->real = true;
  } else if (v.is_string()) {
    const std::string str = v.get<std::string>();
    if (std::regex_match(str, DateRe())) {
      s->date = true;
    } else if (std::regex_match(str, DateTimeRe())) {
      s->datetime = true;
    } else {
      s->text = true;
    }
    s->all_url = s->all_url && IsUrl(str);
    s->all_email = s->all_email && std::regex_match(str, EmailRe());
    return;
  } else {
    s->other = true;
  }
  s->all_url = false;
  s->all_email = false;
}

FieldDef Infer(const std::string& name, const Seen& s) {
  FieldDef f;
  f.name = name;
  const bool strings = s.text || s.date || s.datetime;
  const bool numbers = s.integer || s.real;
  const int kinds = int(s.boolean) + int(numbers) + int(strings) + int(s.other);
  if (!s.any || kinds != 1 || s.other) {
    f.type = FieldType::kText;
  } else if (s.boolean) {
    f.type = FieldType::kBoolean;
  } else if (numbers) {
    f.type = s.real ? FieldType::kReal : FieldType::kInteger;
  } else if (!s.text && s.date && !s.datetime) {
    f.type = FieldType::kDate;
  } else if (!s.text && s.datetime && !s.date) {
    f.type = FieldType::kDateTime;
  } else {
    f.type = FieldType::kText;
  }
  if (f.type == FieldType::kText && s.any && strings && kinds == 1) {
    const std::string l = Lower(name);
    if (s.all_url) {
      f.role = FieldRole::kUrl;
    } else if (s.all_email) {
      f.role = FieldRole::kEmail;
    } else if (l == "phone" || l == "tel" || l == "telephone" || l == "phone_number") {
      f.role = FieldRole::kPhone;
    }
  }
  return f;
}

std::string FeatureLabel(size_t index) { return "feature " + std::to_string(index + 1); }

Status ReadInto(const json& root, const std::string& fallback_name,
                std::shared_ptr<MemoryFeatureLayer>* out) {
  // Normalise the three accepted shapes to a list of Feature objects.
  std::vector<json> features;
  const std::string type = StringMember(root, "type");
  if (type == "FeatureCollection") {
    if (!root.contains("features") || !root["features"].is_array())
      return Bad("a FeatureCollection has no features array");
    for (const json& f : root["features"]) features.push_back(f);
  } else if (type == "Feature") {
    features.push_back(root);
  } else if (!type.empty()) {
    json wrapped;
    wrapped["type"] = "Feature";
    wrapped["geometry"] = root;
    wrapped["properties"] = nullptr;
    features.push_back(wrapped);
  } else {
    return Bad("the document has no type");
  }

  std::vector<Geometry> geometries(features.size());
  for (size_t i = 0; i < features.size(); ++i) {
    const json& f = features[i];
    if (!f.is_object() || StringMember(f, "type") != "Feature")
      return Bad(FeatureLabel(i) + " is not a Feature");
    if (f.contains("properties") && !f["properties"].is_null() && !f["properties"].is_object())
      return Bad(FeatureLabel(i) + " has properties that are not an object");
    Status s = ReadGeometry(f.contains("geometry") ? f["geometry"] : json(nullptr), &geometries[i], 0);
    if (!s.ok()) return Bad(FeatureLabel(i) + ": " + s.message);
  }

  FeatureSchema schema;
  if (root.contains(kSchemaMember)) {
    Status s = SchemaFromJson(root[kSchemaMember], &schema);
    if (!s.ok()) return s;
  } else {
    std::vector<std::string> order;
    std::map<std::string, Seen> seen;
    for (const json& f : features) {
      if (!f.contains("properties") || !f["properties"].is_object()) continue;
      for (const auto& item : f["properties"].items()) {
        if (seen.find(item.key()) == seen.end()) order.push_back(item.key());
        Observe(item.value(), &seen[item.key()]);
      }
    }
    for (const std::string& key : order) schema.fields.push_back(Infer(key, seen[key]));
    // The geometry type is common to every feature that has one, else any.
    std::set<GeometryType> types;
    for (const Geometry& g : geometries)
      if (!g.IsNull()) types.insert(g.type);
    if (types.size() == 1) schema.geometry_type = *types.begin();
  }

  // Ids are kept only when all of them are distinct positive integers.
  bool keep_ids = !features.empty();
  std::set<int64_t> ids;
  for (const json& f : features) {
    if (!f.contains("id") || !f["id"].is_number_integer() || f["id"].get<int64_t>() <= 0 ||
        !ids.insert(f["id"].get<int64_t>()).second) {
      keep_ids = false;
      break;
    }
  }

  std::string name = StringMember(root, "name");
  if (name.empty()) name = fallback_name;
  auto layer = std::make_shared<MemoryFeatureLayer>(name, schema);
  for (size_t i = 0; i < features.size(); ++i) {
    const json& f = features[i];
    Feature feature;
    feature.geometry = std::move(geometries[i]);
    feature.values.resize(schema.fields.size());
    if (f.contains("properties") && f["properties"].is_object()) {
      const json& props = f["properties"];
      for (size_t k = 0; k < schema.fields.size(); ++k) {
        const FieldDef& field = schema.fields[k];
        if (!props.contains(field.name)) continue;
        if (!JsonToValue(props[field.name], field.type, &feature.values[k]))
          return Bad(FeatureLabel(i) + ": \"" + field.name + "\" is not a " + ToString(field.type));
      }
    }
    Status s = Status::Ok();
    if (keep_ids) {
      feature.id = f["id"].get<int64_t>();
      s = layer->InsertWithId(std::move(feature));
    } else {
      s = layer->Insert(&feature);
    }
    if (!s.ok()) return Bad(FeatureLabel(i) + ": " + s.message);
  }
  *out = std::move(layer);
  return Status::Ok();
}

}  // namespace

Status ReadGeoJson(const std::string& text, const std::string& name,
                   std::shared_ptr<MemoryFeatureLayer>* out, std::string* style_json) {
  if (out == nullptr) return Bad("null out");
  try {
    const json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) return Bad("not valid JSON");
    if (!root.is_object()) return Bad("the document is not an object");
    if (style_json != nullptr) {
      const auto style = root.find(kStyleMember);
      *style_json = style != root.end() && style->is_object() ? style->dump(1, '\t') : std::string();
    }
    return ReadInto(root, name, out);
  } catch (const std::exception& e) {
    return Bad(e.what());
  }
}

Status ReadGeoJsonFile(const std::string& path, std::shared_ptr<MemoryFeatureLayer>* out,
                       std::string* style_json) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kIoError, "cannot open " + path);
  std::ostringstream text;
  text << in.rdbuf();
  return ReadGeoJson(text.str(), std::filesystem::path(path).stem().string(), out, style_json);
}

Status WriteGeoJson(const IFeatureLayer& layer, std::string* out, const std::string& style_json) {
  if (out == nullptr) return Bad("null out");
  const FeatureSchema& schema = layer.Schema();
  for (const FieldDef& f : schema.fields)
    if (f.type == FieldType::kBlob)
      return Status::Error(kUnsupported, "GeoJSON cannot hold blob field \"" + f.name + "\"");
  std::vector<Feature> features;
  Status s = layer.All(&features);
  if (!s.ok()) return s;
  try {
    json root;
    root["type"] = "FeatureCollection";
    root["name"] = layer.Name();
    root[kSchemaMember] = SchemaToJson(schema);
    if (!style_json.empty()) {
      json style = json::parse(style_json, nullptr, /*allow_exceptions=*/false);
      if (style.is_discarded() || !style.is_object()) return Bad("the style is not a JSON object");
      root[kStyleMember] = std::move(style);
    }
    json list = json::array();
    for (const Feature& f : features) {
      json jf;
      jf["type"] = "Feature";
      jf["id"] = f.id;
      jf["geometry"] = WriteGeometry(f.geometry);
      json props = json::object();
      for (size_t k = 0; k < schema.fields.size() && k < f.values.size(); ++k)
        props[schema.fields[k].name] = ValueToJson(f.values[k]);
      jf["properties"] = props;
      list.push_back(jf);
    }
    root["features"] = list;
    *out = root.dump(1, '\t');
    out->push_back('\n');
  } catch (const std::exception& e) {
    return Status::Error(kInternal, std::string("GeoJSON: ") + e.what());
  }
  return Status::Ok();
}

Status WriteGeoJsonFile(const IFeatureLayer& layer, const std::string& path,
                        const std::string& style_json) {
  std::string text;
  Status s = WriteGeoJson(layer, &text, style_json);
  if (!s.ok()) return s;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::Error(kIoError, "cannot write " + path);
  out << text;
  out.close();
  if (!out) return Status::Error(kIoError, "write failed: " + path);
  return Status::Ok();
}

}  // namespace fv
