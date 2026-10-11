// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/points_layer.h"

#include <algorithm>

#include "fvkit/overlay/point_edit.h"
#include "fvkit/overlay/point_overlay.h"

namespace fv {

namespace {

FieldDef Text(const char* name, const char* title, FieldRole role = FieldRole::kNone) {
  FieldDef f;
  f.name = name;
  f.title = title;
  f.role = role;
  return f;
}

Feature ToFeature(const MapPoint& p) {
  Feature f;
  f.id = p.id;
  f.geometry = Geometry::Point(p.position);
  f.values = {p.name, p.category, p.elevation_ft, p.remarks, p.phone, p.url};
  return f;
}

std::string TextOf(const FieldValue& v) {
  const auto* s = std::get_if<std::string>(&v);
  return s ? *s : std::string();
}

/// Checks `f` and copies its values and position onto `p`. A missing trailing
/// value is null, which writes as "" or 0.
Status Apply(const Feature& f, bool need_position, MapPoint* p) {
  const FeatureSchema& schema = PointsFeatureLayer::PointsSchema();
  if (f.values.size() > schema.fields.size())
    return Status::Error(kInvalidArg, "a point has " + std::to_string(schema.fields.size()) + " fields, given " +
                                          std::to_string(f.values.size()));
  std::vector<FieldValue> v = f.values;
  v.resize(schema.fields.size());
  for (size_t i = 0; i < v.size(); ++i) {
    Status s = ValidateValue(schema.fields[i], v[i]);
    if (!s.ok()) return s;
  }
  const bool has_point = f.geometry.type == GeometryType::kPoint && !f.geometry.IsEmpty();
  if (!has_point && (need_position || !f.geometry.IsNull()))
    return Status::Error(kInvalidArg, "a point needs a point geometry");
  if (has_point) p->position = f.geometry.paths[0][0].point();
  p->name = TextOf(v[PointsFeatureLayer::kName]);
  p->category = TextOf(v[PointsFeatureLayer::kCategory]);
  const FieldValue& elev = v[PointsFeatureLayer::kElevation];
  p->elevation_ft = std::holds_alternative<double>(elev)    ? std::get<double>(elev)
                    : std::holds_alternative<int64_t>(elev) ? static_cast<double>(std::get<int64_t>(elev))
                                                            : 0.0;
  p->remarks = TextOf(v[PointsFeatureLayer::kRemarks]);
  p->phone = TextOf(v[PointsFeatureLayer::kPhone]);
  p->url = TextOf(v[PointsFeatureLayer::kUrl]);
  return Status::Ok();
}

}  // namespace

PointsFeatureLayer::PointsFeatureLayer(std::shared_ptr<PointOverlay> points) : points_(std::move(points)) {}

const FeatureSchema& PointsFeatureLayer::PointsSchema() {
  static const FeatureSchema schema = [] {
    FeatureSchema s;
    s.geometry_type = GeometryType::kPoint;
    FieldDef elev;
    elev.name = "elevation_ft";
    elev.title = "Elevation";
    elev.type = FieldType::kReal;
    elev.role = FieldRole::kMeasurement;
    elev.unit = "ft";
    s.fields = {Text("name", "Name"),
                Text("category", "Category"),
                elev,
                Text("remarks", "Remarks", FieldRole::kMultilineText),
                Text("phone", "Phone", FieldRole::kPhone),
                Text("url", "Web Page", FieldRole::kUrl)};
    return s;
  }();
  return schema;
}

std::string PointsFeatureLayer::Name() const { return points_->Name(); }

size_t PointsFeatureLayer::Count() const { return points_->points().size(); }

Status PointsFeatureLayer::Extent(GeoRect* out) const {
  const auto& pts = points_->points();
  if (pts.empty()) return Status::Error(kNotFound, "the point set is empty");
  GeoRect box{pts[0].position, pts[0].position};
  for (const MapPoint& p : pts) {
    box.ll.lat = std::min(box.ll.lat, p.position.lat);
    box.ll.lon = std::min(box.ll.lon, p.position.lon);
    box.ur.lat = std::max(box.ur.lat, p.position.lat);
    box.ur.lon = std::max(box.ur.lon, p.position.lon);
  }
  *out = box;
  return Status::Ok();
}

Status PointsFeatureLayer::All(std::vector<Feature>* out) const {
  out->clear();
  for (const MapPoint& p : points_->points()) out->push_back(ToFeature(p));
  // The overlay keeps draw order; the interface promises id order.
  std::sort(out->begin(), out->end(), [](const Feature& a, const Feature& b) { return a.id < b.id; });
  return Status::Ok();
}

Status PointsFeatureLayer::Query(const GeoRect& box, std::vector<Feature>* out) const {
  Status s = All(out);
  if (!s.ok()) return s;
  out->erase(std::remove_if(out->begin(), out->end(),
                            [&](const Feature& f) { return !box.Contains(f.geometry.paths[0][0].point()); }),
             out->end());
  return Status::Ok();
}

Status PointsFeatureLayer::Get(int64_t id, Feature* out) const {
  const MapPoint* p = points_->Find(id);
  if (p == nullptr) return Status::Error(kNotFound, "no point " + std::to_string(id));
  *out = ToFeature(*p);
  return Status::Ok();
}

Status PointsFeatureLayer::Insert(Feature* feature) {
  MapPoint p;
  Status s = Apply(*feature, true, &p);
  if (!s.ok()) return s;
  const int64_t id = points_->edit().AddAt(p, p.position);
  if (id == 0) return Status::Error(kIoError, "the point was not added");
  feature->id = id;
  return Status::Ok();
}

Status PointsFeatureLayer::Update(const Feature& feature) {
  const MapPoint* found = points_->Find(feature.id);
  if (found == nullptr) return Status::Error(kNotFound, "no point " + std::to_string(feature.id));
  MapPoint p = *found;
  Status s = Apply(feature, false, &p);
  if (!s.ok()) return s;
  if (!points_->edit().Update(p)) return Status::Error(kNotFound, "no point " + std::to_string(feature.id));
  return Status::Ok();
}

Status PointsFeatureLayer::Delete(int64_t id) {
  if (!points_->edit().Delete(id)) return Status::Error(kNotFound, "no point " + std::to_string(id));
  return Status::Ok();
}

}  // namespace fv
