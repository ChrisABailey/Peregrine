// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/feature_filter.h"

#include <cctype>

#include "fvkit/feature/style.h"

namespace fv {
namespace {

/// The geometry-type token of `g`; a collection of mixed members has none.
std::string GeometryToken(const Geometry& g) {
  switch (g.type) {
    case GeometryType::kPoint:
    case GeometryType::kMultiPoint: return "Point";
    case GeometryType::kLineString:
    case GeometryType::kMultiLineString: return "LineString";
    case GeometryType::kPolygon:
    case GeometryType::kMultiPolygon: return "Polygon";
    default: break;
  }
  std::string token;
  for (const Geometry& part : g.parts) {
    const std::string t = GeometryToken(part);
    if (t.empty() || (!token.empty() && t != token)) return std::string();
    token = t;
  }
  return token;
}

}  // namespace

Status FeatureFilter::Parse(const std::string& json, FeatureFilter* out) {
  size_t i = 0;
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
  if (i == json.size()) {
    *out = FeatureFilter();
    return Status::Ok();
  }
  FeatureFilter f;
  Status s = StyleExpression::Parse(json, &f.expr_);
  if (!s.ok()) return s;
  f.text_ = json;
  *out = std::move(f);
  return Status::Ok();
}

bool FeatureFilter::Accepts(const Feature& f, const FeatureSchema& schema) const {
  if (expr_.empty()) return true;
  StyleExpressionInput in;
  in.get = [&f, &schema](const std::string& key, StyleValue* v) {
    const int i = schema.Find(key);
    if (i < 0 || static_cast<size_t>(i) >= f.values.size()) return false;
    const FieldValue& value = f.values[i];
    if (IsNull(value) || std::holds_alternative<std::vector<uint8_t>>(value)) return false;
    *v = StyleValue::String(FeatureCategoryKey(value));
    return true;
  };
  in.has_id = true;
  in.id = f.id;
  in.geometry_type = GeometryToken(f.geometry);
  const StyleValue r = expr_.Evaluate(in);
  return r.type == StyleValue::Type::kBool && r.boolean;
}

}  // namespace fv
