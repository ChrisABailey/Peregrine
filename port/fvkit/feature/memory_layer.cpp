// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <algorithm>
#include <utility>

#include "fvkit/feature/layer.h"
#include "fvkit/feature/schema_change.h"

namespace fv {

MemoryFeatureLayer::MemoryFeatureLayer(std::string name, FeatureSchema schema)
    : name_(std::move(name)), schema_(std::move(schema)) {}

Status MemoryFeatureLayer::Extent(GeoRect* out) const {
  bool any = false;
  GeoRect box{};
  for (const auto& entry : features_) {
    GeoRect b;
    if (!entry.second.geometry.Envelope(&b)) continue;
    if (!any) {
      box = b;
      any = true;
      continue;
    }
    box.ll.lat = std::min(box.ll.lat, b.ll.lat);
    box.ll.lon = std::min(box.ll.lon, b.ll.lon);
    box.ur.lat = std::max(box.ur.lat, b.ur.lat);
    box.ur.lon = std::max(box.ur.lon, b.ur.lon);
  }
  if (!any) return Status::Error(kNotFound, "layer \"" + name_ + "\" has no geometry");
  if (out != nullptr) *out = box;
  return Status::Ok();
}

Status MemoryFeatureLayer::All(std::vector<Feature>* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "All: null out");
  out->clear();
  for (const auto& entry : features_) out->push_back(entry.second);
  return Status::Ok();
}

Status MemoryFeatureLayer::Query(const GeoRect& box, std::vector<Feature>* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "Query: null out");
  out->clear();
  for (const auto& entry : features_) {
    GeoRect b;
    if (entry.second.geometry.Envelope(&b) && box.Intersects(b)) out->push_back(entry.second);
  }
  return Status::Ok();
}

Status MemoryFeatureLayer::Get(int64_t id, Feature* out) const {
  const auto it = features_.find(id);
  if (it == features_.end())
    return Status::Error(kNotFound, "no feature " + std::to_string(id) + " in \"" + name_ + "\"");
  if (out != nullptr) *out = it->second;
  return Status::Ok();
}

Status MemoryFeatureLayer::Conform(Feature* f) const {
  if (f->values.size() > schema_.fields.size())
    return Status::Error(kInvalidArg, "feature has " + std::to_string(f->values.size()) +
                                          " values for " + std::to_string(schema_.fields.size()) +
                                          " fields");
  f->values.resize(schema_.fields.size());
  for (size_t i = 0; i < f->values.size(); ++i) {
    Status s = ValidateValue(schema_.fields[i], f->values[i]);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

Status MemoryFeatureLayer::Insert(Feature* feature) {
  if (feature == nullptr) return Status::Error(kInvalidArg, "Insert: null feature");
  Feature f = *feature;
  Status s = Conform(&f);
  if (!s.ok()) return s;
  f.id = next_id_++;
  feature->id = f.id;
  feature->values = f.values;
  features_.emplace(f.id, std::move(f));
  return Status::Ok();
}

Status MemoryFeatureLayer::InsertWithId(const Feature& stored) {
  Feature feature = stored;
  if (feature.id <= 0) return Status::Error(kInvalidArg, "feature id must be positive");
  if (features_.count(feature.id) != 0)
    return Status::Error(kInvalidArg, "feature id " + std::to_string(feature.id) + " is in use");
  Status s = Conform(&feature);
  if (!s.ok()) return s;
  next_id_ = std::max(next_id_, feature.id + 1);
  features_.emplace(feature.id, std::move(feature));
  return Status::Ok();
}

Status MemoryFeatureLayer::Update(const Feature& feature) {
  const auto it = features_.find(feature.id);
  if (it == features_.end())
    return Status::Error(kNotFound, "no feature " + std::to_string(feature.id) + " in \"" + name_ + "\"");
  Feature f = feature;
  Status s = Conform(&f);
  if (!s.ok()) return s;
  it->second = std::move(f);
  return Status::Ok();
}

Status MemoryFeatureLayer::Delete(int64_t id) {
  if (features_.erase(id) == 0)
    return Status::Error(kNotFound, "no feature " + std::to_string(id) + " in \"" + name_ + "\"");
  return Status::Ok();
}

Status MemoryFeatureLayer::ChangeSchema(const SchemaChange& change) {
  Status s = ValidateSchemaChange(schema_, change);
  if (!s.ok()) return s;
  std::map<int64_t, Feature> next;
  for (const auto& entry : features_) {
    Feature f;
    RemapFeature(entry.second, schema_, change, &f);
    for (size_t i = 0; i < f.values.size(); ++i) {
      s = ValidateValue(change.schema.fields[i], f.values[i]);
      if (!s.ok()) return s;
    }
    next.emplace(entry.first, std::move(f));
  }
  schema_ = change.schema;
  features_ = std::move(next);
  return Status::Ok();
}

}  // namespace fv
