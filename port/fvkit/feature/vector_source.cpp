// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// A feature layer as an IVectorSource (fvkit/feature/vector_source.h).
#include "fvkit/feature/vector_source.h"

#include <algorithm>

#include "fvkit/feature/style.h"

namespace fv {

namespace {

/// One vector feature of `type` from `paths`, with the shared attributes.
void Emit(VectorGeometryType type, const std::vector<Path>& paths, const VectorFeature& proto,
          std::vector<VectorFeature>* out) {
  VectorFeature v = proto;
  v.type = type;
  bool first = true;
  for (const Path& path : paths) {
    if (path.empty()) continue;
    std::vector<GeoPoint> pts;
    pts.reserve(path.size());
    for (const Position& p : path) {
      const GeoPoint g = p.point();
      pts.push_back(g);
      if (first) {
        v.bounds = GeoRect{g, g};
        first = false;
      } else {
        v.bounds.ll.lat = std::min(v.bounds.ll.lat, g.lat);
        v.bounds.ll.lon = std::min(v.bounds.ll.lon, g.lon);
        v.bounds.ur.lat = std::max(v.bounds.ur.lat, g.lat);
        v.bounds.ur.lon = std::max(v.bounds.ur.lon, g.lon);
      }
    }
    v.parts.push_back(std::move(pts));
  }
  if (!v.parts.empty()) out->push_back(std::move(v));
}

void EmitGeometry(const Geometry& g, const VectorFeature& proto, std::vector<VectorFeature>* out) {
  switch (g.type) {
    case GeometryType::kPoint:
      Emit(VectorGeometryType::kPoint, g.paths, proto, out);
      return;
    case GeometryType::kLineString:
      Emit(VectorGeometryType::kLine, g.paths, proto, out);
      return;
    case GeometryType::kPolygon:
      Emit(VectorGeometryType::kArea, g.paths, proto, out);
      return;
    default:
      for (const Geometry& part : g.parts) EmitGeometry(part, proto, out);
      return;
  }
}

}  // namespace

void FeatureToVector(const Feature& f, const FeatureSchema& schema,
                     std::vector<VectorFeature>* out) {
  VectorFeature proto;
  proto.layer = kFeatureSourceLayer;
  proto.has_id = true;
  proto.id = f.id;
  proto.ref.layer = 0;
  proto.ref.feature = static_cast<int32_t>(f.id);
  for (size_t i = 0; i < schema.fields.size() && i < f.values.size(); ++i) {
    const FieldValue& v = f.values[i];
    if (IsNull(v) || std::holds_alternative<std::vector<uint8_t>>(v)) continue;
    proto.attributes.emplace_back(schema.fields[i].name, FeatureCategoryKey(v));
  }
  EmitGeometry(f.geometry, proto, out);
}

FeatureVectorSource::FeatureVectorSource(std::shared_ptr<const IFeatureLayer> layer)
    : layer_(std::move(layer)) {}

Status FeatureVectorSource::Open(const std::string&) { return Status::Ok(); }

GeoRect FeatureVectorSource::Bounds() const {
  GeoRect r = GeoRect::World();
  if (layer_) layer_->Extent(&r);
  return r;
}

Status FeatureVectorSource::Query(const VectorQuery& q, std::vector<VectorFeature>* out) {
  features_returned_ = 0;
  if (!layer_) return Status::Ok();
  std::vector<Feature> features;
  Status s = layer_->Query(q.area, &features);
  if (!s.ok()) return s;
  const FeatureSchema& schema = layer_->Schema();
  for (const Feature& f : features) {
    if (keep_ && !keep_(f)) continue;
    FeatureToVector(f, schema, out);
    ++features_returned_;
  }
  return Status::Ok();
}

}  // namespace fv
