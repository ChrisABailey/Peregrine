// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/templates.h"

namespace fv {

namespace {

FieldDef Field(const char* name, const char* title, FieldType type = FieldType::kText,
               FieldRole role = FieldRole::kNone) {
  FieldDef f;
  f.name = name;
  f.title = title;
  f.type = type;
  f.role = role;
  return f;
}

FeatureLayerTemplate Blank() {
  FeatureLayerTemplate t{"blank", "Blank", {}};
  t.schema.fields = {Field("name", "Name")};
  return t;
}

FeatureLayerTemplate Monuments() {
  FeatureLayerTemplate t{"monuments", "Historical monuments", {}};
  t.schema.geometry_type = GeometryType::kPoint;
  FieldDef name = Field("name", "Name");
  name.required = true;
  name.default_value = std::string("Unnamed");
  FieldDef built = Field("built", "Date built", FieldType::kDate);
  FieldDef from = Field("commemorates_from", "Commemorates", FieldType::kDate);
  from.range_end = "commemorates_to";
  FieldDef to = Field("commemorates_to", "Commemorates to", FieldType::kDate);
  FieldDef notes = Field("notes", "Notes", FieldType::kText, FieldRole::kMultilineText);
  t.schema.fields = {name, built, from, to, notes};
  return t;
}

FeatureLayerTemplate Restaurants() {
  FeatureLayerTemplate t{"restaurants", "Restaurants", {}};
  t.schema.geometry_type = GeometryType::kPoint;
  FieldDef name = Field("name", "Name");
  name.required = true;
  name.default_value = std::string("Unnamed");
  FieldDef phone = Field("phone", "Phone", FieldType::kText, FieldRole::kPhone);
  FieldDef style = Field("style", "Style", FieldType::kText, FieldRole::kEnumeration);
  style.choices = {{"neapolitan", "Neapolitan"}, {"new_york", "New York"}, {"detroit", "Detroit"},
                   {"other", "Other"}};
  FieldDef web = Field("website", "Website", FieldType::kText, FieldRole::kUrl);
  FieldDef rating = Field("rating", "Rating", FieldType::kInteger);
  rating.min = 1;
  rating.max = 5;
  t.schema.fields = {name, phone, style, web, rating};
  return t;
}

FeatureLayerTemplate Rivers() {
  FeatureLayerTemplate t{"rivers", "Rivers", {}};
  t.schema.geometry_type = GeometryType::kLineString;
  FieldDef depth = Field("depth", "Depth", FieldType::kReal, FieldRole::kMeasurement);
  depth.unit = "m";
  depth.min = 0;
  FieldDef flow = Field("flow", "Average flow", FieldType::kReal, FieldRole::kMeasurement);
  flow.unit = "m\xc2\xb3/s";
  flow.min = 0;
  t.schema.fields = {Field("name", "Name"), depth, flow};
  return t;
}

}  // namespace

const std::vector<FeatureLayerTemplate>& FeatureLayerTemplates() {
  static const std::vector<FeatureLayerTemplate> kTemplates = {Blank(), Monuments(), Restaurants(),
                                                               Rivers()};
  return kTemplates;
}

const FeatureLayerTemplate* FindFeatureLayerTemplate(const std::string& id) {
  for (const FeatureLayerTemplate& t : FeatureLayerTemplates())
    if (t.id == id) return &t;
  return nullptr;
}

}  // namespace fv
