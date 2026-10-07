// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_workspace.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fv {
namespace desk {
namespace {

using json = nlohmann::json;

constexpr int kFormatVersion = 1;

struct ProjName {
  ProjectionType type;
  const char* key;
  const char* title;
};
constexpr ProjName kProjections[] = {
    {ProjectionType::kEqualArc, "equal_arc", "Equal Arc"},
    {ProjectionType::kMercator, "mercator", "Mercator"},
    {ProjectionType::kLambert, "lambert", "Lambert Conformal Conic"},
    {ProjectionType::kAzimuthalEquidistant, "azimuthal_equidistant", "Azimuthal Equidistant"},
    {ProjectionType::kOrthographic, "orthographic", "Orthographic"},
};
constexpr ProjectionType kProjectionOrder[] = {
    ProjectionType::kEqualArc, ProjectionType::kMercator, ProjectionType::kLambert,
    ProjectionType::kAzimuthalEquidistant, ProjectionType::kOrthographic};

/// Reads an optional string member; false when present and not a string.
bool OptString(const json& j, const char* key, std::string* out) {
  if (!j.contains(key)) return true;
  if (!j[key].is_string()) return false;
  *out = j[key].get<std::string>();
  return true;
}

bool OptNumber(const json& j, const char* key, double* out) {
  if (!j.contains(key)) return true;
  if (!j[key].is_number()) return false;
  *out = j[key].get<double>();
  return std::isfinite(*out);
}

}  // namespace

const char kWorkspaceExtension[] = "fvws";

const char* ProjectionKey(ProjectionType t) {
  for (const ProjName& p : kProjections)
    if (p.type == t) return p.key;
  return "equal_arc";
}

const char* ProjectionTitle(ProjectionType t) {
  for (const ProjName& p : kProjections)
    if (p.type == t) return p.title;
  return "Equal Arc";
}

bool ParseProjectionKey(const std::string& key, ProjectionType* out) {
  for (const ProjName& p : kProjections) {
    if (key == p.key) {
      if (out) *out = p.type;
      return true;
    }
  }
  return false;
}

const ProjectionType* AllProjectionTypes(size_t* count) {
  if (count) *count = sizeof(kProjectionOrder) / sizeof(kProjectionOrder[0]);
  return kProjectionOrder;
}

std::string Workspace::ToJson() const {
  json doc;
  doc["version"] = kFormatVersion;
  doc["catalog"] = catalog_path;
  doc["map_group"] = map_group;
  doc["product"] = {{"format", product_format}, {"series_key", product_series_key}};
  doc["view"] = {{"lat", center.lat},
                 {"lon", center.lon},
                 {"scale_denom", scale_denom},
                 {"rotation_deg", rotation_deg},
                 {"projection", ProjectionKey(projection)}};
  doc["overlays"] = json::object();
  for (const auto& kv : overlays) doc["overlays"][kv.first] = kv.second;
  doc["active_editor"] = active_editor;
  return doc.dump(2) + "\n";
}

Status Workspace::FromJson(const std::string& text, const std::string& name) {
  auto fail = [&name](const std::string& why) {
    return Status::Error(kInvalidArg, name + ": " + why);
  };
  json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object()) return fail("not a JSON object");
  if (!doc.contains("version") || !doc["version"].is_number_integer())
    return fail("no version");
  if (doc["version"].get<int>() > kFormatVersion)
    return Status::Error(kUnsupported, name + ": written by a newer version");

  Workspace w;
  if (!OptString(doc, "catalog", &w.catalog_path)) return fail("catalog is not a string");
  if (!OptString(doc, "map_group", &w.map_group)) return fail("map_group is not a string");
  if (!OptString(doc, "active_editor", &w.active_editor))
    return fail("active_editor is not a string");
  if (doc.contains("product")) {
    const json& p = doc["product"];
    if (!p.is_object() || !OptString(p, "format", &w.product_format) ||
        !OptString(p, "series_key", &w.product_series_key))
      return fail("product is malformed");
  }
  if (doc.contains("view")) {
    const json& v = doc["view"];
    if (!v.is_object()) return fail("view is not an object");
    if (!OptNumber(v, "lat", &w.center.lat) || !OptNumber(v, "lon", &w.center.lon) ||
        !OptNumber(v, "scale_denom", &w.scale_denom) ||
        !OptNumber(v, "rotation_deg", &w.rotation_deg))
      return fail("view has a non-numeric field");
    if (!(w.scale_denom > 0)) return fail("view scale_denom must be positive");
    std::string proj = ProjectionKey(w.projection);
    if (!OptString(v, "projection", &proj) || !ParseProjectionKey(proj, &w.projection))
      return fail("unknown projection '" + proj + "'");
  }
  if (doc.contains("overlays")) {
    const json& o = doc["overlays"];
    if (!o.is_object()) return fail("overlays is not an object");
    for (auto it = o.begin(); it != o.end(); ++it) {
      if (!it.value().is_string()) return fail("overlay key '" + it.key() + "' is not a string");
      w.overlays[it.key()] = it.value().get<std::string>();
    }
  }
  *this = std::move(w);
  return Status::Ok();
}

Status Workspace::Save(const std::string& path) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::Error(kIoError, "cannot write " + path);
  out << ToJson();
  if (!out) return Status::Error(kIoError, "cannot write " + path);
  return Status::Ok();
}

Status Workspace::Load(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kNotFound, "cannot read " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return FromJson(ss.str(), path);
}

bool Workspace::operator==(const Workspace& o) const {
  return catalog_path == o.catalog_path && map_group == o.map_group &&
         product_format == o.product_format && product_series_key == o.product_series_key &&
         center.lat == o.center.lat && center.lon == o.center.lon &&
         scale_denom == o.scale_denom && rotation_deg == o.rotation_deg &&
         projection == o.projection && overlays == o.overlays &&
         active_editor == o.active_editor;
}

}  // namespace desk
}  // namespace fv
