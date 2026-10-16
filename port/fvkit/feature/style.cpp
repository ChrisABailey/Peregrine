// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace fv {

namespace {

bool SameColor(const FvColor& a, const FvColor& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

std::string Number(double d) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.10g", d);
  return buf;
}

/// The value as a number, for a graduated field; false for anything else.
bool NumericValue(const FieldValue& v, double* out) {
  if (const auto* i = std::get_if<int64_t>(&v)) {
    *out = static_cast<double>(*i);
    return true;
  }
  if (const auto* d = std::get_if<double>(&v)) {
    if (!std::isfinite(*d)) return false;
    *out = *d;
    return true;
  }
  return false;
}

/// Distinct, readable on a light map, in the order categories take them.
const FvColor kPalette[] = {
    {78, 121, 167, 255}, {242, 142, 43, 255}, {225, 87, 89, 255},  {118, 183, 178, 255},
    {89, 161, 79, 255},  {237, 201, 72, 255}, {176, 122, 161, 255}, {255, 157, 167, 255},
    {156, 117, 95, 255}, {186, 176, 172, 255},
};

/// `base` drawn in `c`: marker, line and area outline in `c`, the area fill
/// in `c` at the base fill's opacity.
FeatureSymbol Recoloured(FeatureSymbol base, FvColor c) {
  base.marker_fill = c;
  base.line_color = c;
  base.area_outline = c;
  base.area_fill = FvColor{c.r, c.g, c.b, base.area_fill.a};
  return base;
}

unsigned char Lerp(unsigned char a, unsigned char b, double t) {
  return static_cast<unsigned char>(std::lround(a + (b - a) * t));
}

}  // namespace

bool FeatureSymbol::operator==(const FeatureSymbol& o) const {
  return marker == o.marker && marker_size_px == o.marker_size_px &&
         SameColor(marker_fill, o.marker_fill) && SameColor(marker_outline, o.marker_outline) &&
         marker_outline_px == o.marker_outline_px && SameColor(line_color, o.line_color) &&
         line_width_px == o.line_width_px && dash == o.dash &&
         SameColor(casing_color, o.casing_color) && casing_px == o.casing_px &&
         SameColor(area_fill, o.area_fill) && SameColor(area_outline, o.area_outline) &&
         area_outline_px == o.area_outline_px;
}

bool FeatureLabel::operator==(const FeatureLabel& o) const {
  return field == o.field && size_px == o.size_px && SameColor(color, o.color) &&
         SameColor(halo_color, o.halo_color) && halo_px == o.halo_px;
}

bool StyleRule::operator==(const StyleRule& o) const {
  return id == o.id && type == o.type && filter == o.filter && paint == o.paint && layout == o.layout &&
         min_zoom == o.min_zoom && max_zoom == o.max_zoom && shown == o.shown && legend == o.legend &&
         facet == o.facet && extra == o.extra;
}

bool FeatureStyle::operator==(const FeatureStyle& o) const {
  return renderer == o.renderer && symbol == o.symbol && field == o.field &&
         categories == o.categories && classes == o.classes && overrides == o.overrides &&
         label == o.label && rules == o.rules;
}

const FeatureSymbol& FeatureStyle::SymbolFor(const Feature& f, int field_index) const {
  if (!overrides.empty()) {
    const auto it = overrides.find(f.id);
    if (it != overrides.end()) return it->second;
  }
  if (renderer == FeatureRenderer::kSingle || field_index < 0 ||
      static_cast<size_t>(field_index) >= f.values.size())
    return symbol;
  const FieldValue& v = f.values[field_index];
  if (renderer == FeatureRenderer::kCategorised) {
    if (IsNull(v)) return symbol;
    const std::string key = FeatureCategoryKey(v);
    for (const FeatureCategory& c : categories)
      if (c.value == key) return c.symbol;
    return symbol;
  }
  double x = 0;
  if (!NumericValue(v, &x)) return symbol;
  const FeatureSymbol* found = &symbol;
  for (const FeatureClass& c : classes) {
    if (x < c.lower) break;
    found = &c.symbol;
  }
  return *found;
}

const char* ToString(FeatureRenderer renderer) {
  switch (renderer) {
    case FeatureRenderer::kSingle: return "single";
    case FeatureRenderer::kCategorised: return "categorised";
    case FeatureRenderer::kGraduated: return "graduated";
    case FeatureRenderer::kRules: return "rules";
  }
  return "single";
}

bool FeatureRendererFromString(const std::string& name, FeatureRenderer* out) {
  for (FeatureRenderer r : {FeatureRenderer::kSingle, FeatureRenderer::kCategorised,
                            FeatureRenderer::kGraduated, FeatureRenderer::kRules}) {
    if (name == ToString(r)) {
      *out = r;
      return true;
    }
  }
  return false;
}

std::string FeatureLabelText(const FieldValue& value) {
  if (const auto* s = std::get_if<std::string>(&value)) return *s;
  if (const auto* i = std::get_if<int64_t>(&value)) return std::to_string(*i);
  if (const auto* b = std::get_if<bool>(&value)) return *b ? "yes" : "no";
  if (const auto* d = std::get_if<double>(&value)) return Number(*d);
  return "";
}

std::string FeatureCategoryKey(const FieldValue& value) {
  if (const auto* b = std::get_if<bool>(&value)) return *b ? "true" : "false";
  return FeatureLabelText(value);
}

std::string FeatureClassLabel(const FeatureStyle& style, size_t i) {
  if (i >= style.classes.size()) return "";
  const FeatureClass& c = style.classes[i];
  if (!c.label.empty()) return c.label;
  if (i + 1 < style.classes.size())
    return Number(c.lower) + " \xe2\x80\x93 " + Number(style.classes[i + 1].lower);
  return "\xe2\x89\xa5 " + Number(c.lower);
}

Status ClassifyCategories(const IFeatureLayer& layer, int field_index, const FeatureSymbol& base,
                          std::vector<FeatureCategory>* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "ClassifyCategories: null out");
  const FeatureSchema& schema = layer.Schema();
  if (field_index < 0 || static_cast<size_t>(field_index) >= schema.fields.size())
    return Status::Error(kInvalidArg, "no field " + std::to_string(field_index) + " to classify by");
  const FieldDef& field = schema.fields[field_index];
  if (field.type == FieldType::kBlob)
    return Status::Error(kInvalidArg, "a blob field cannot be categorised");
  std::vector<Feature> features;
  Status s = layer.All(&features);
  if (!s.ok()) return s;

  std::set<std::string> seen;
  std::vector<std::pair<FieldValue, std::string>> values;  // value, key
  for (const Feature& f : features) {
    if (static_cast<size_t>(field_index) >= f.values.size() || IsNull(f.values[field_index])) continue;
    std::string key = FeatureCategoryKey(f.values[field_index]);
    if (seen.insert(key).second) values.emplace_back(f.values[field_index], std::move(key));
  }
  std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
    double x = 0, y = 0;
    if (NumericValue(a.first, &x) && NumericValue(b.first, &y)) return x < y;
    return a.second < b.second;
  });

  out->clear();
  auto add = [&](const std::string& key, const std::string& label) {
    FeatureCategory c;
    c.value = key;
    c.label = label;
    c.symbol = Recoloured(base, kPalette[out->size() % (sizeof kPalette / sizeof kPalette[0])]);
    out->push_back(std::move(c));
  };
  // An enumeration lists every choice, used or not, so a new feature already has its colour.
  std::set<std::string> listed;
  if (field.role == FieldRole::kEnumeration) {
    for (const FieldChoice& choice : field.choices) {
      if (!listed.insert(choice.value).second) continue;
      add(choice.value, choice.label == choice.value ? std::string() : choice.label);
    }
  }
  for (const auto& v : values)
    if (listed.insert(v.second).second) add(v.second, "");
  return Status::Ok();
}

Status ClassifyGraduated(const IFeatureLayer& layer, int field_index, const FeatureSymbol& base,
                         const GraduatedOptions& options, std::vector<FeatureClass>* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "ClassifyGraduated: null out");
  const FeatureSchema& schema = layer.Schema();
  if (field_index < 0 || static_cast<size_t>(field_index) >= schema.fields.size())
    return Status::Error(kInvalidArg, "no field " + std::to_string(field_index) + " to classify by");
  const FieldDef& field = schema.fields[field_index];
  if (field.type != FieldType::kInteger && field.type != FieldType::kReal)
    return Status::Error(kInvalidArg, "\"" + field.DisplayTitle() + "\" is not a number field");
  std::vector<Feature> features;
  Status s = layer.All(&features);
  if (!s.ok()) return s;
  std::vector<double> xs;
  for (const Feature& f : features) {
    double x = 0;
    if (static_cast<size_t>(field_index) < f.values.size() && NumericValue(f.values[field_index], &x))
      xs.push_back(x);
  }
  if (xs.empty()) return Status::Error(kNotFound, "no feature has a \"" + field.DisplayTitle() + "\"");
  std::sort(xs.begin(), xs.end());
  std::vector<double> unique_xs = xs;
  unique_xs.erase(std::unique(unique_xs.begin(), unique_xs.end()), unique_xs.end());
  const size_t n = std::max<size_t>(1, std::min<size_t>(static_cast<size_t>(std::max(options.classes, 1)),
                                                        unique_xs.size()));

  std::vector<double> lowers;
  for (size_t i = 0; i < n; ++i) {
    double lower = 0;
    if (options.mode == GraduatedMode::kQuantile)
      lower = xs[i * xs.size() / n];
    else
      lower = xs.front() + (xs.back() - xs.front()) * static_cast<double>(i) / static_cast<double>(n);
    if (lowers.empty() || lower > lowers.back()) lowers.push_back(lower);
  }

  out->clear();
  for (size_t i = 0; i < lowers.size(); ++i) {
    const double t = lowers.size() > 1 ? static_cast<double>(i) / (lowers.size() - 1) : 0.0;
    FeatureClass c;
    c.lower = lowers[i];
    c.symbol = base;
    if (options.vary == GraduatedVary::kColor) {
      const FvColor col{Lerp(options.from.r, options.to.r, t), Lerp(options.from.g, options.to.g, t),
                        Lerp(options.from.b, options.to.b, t), Lerp(options.from.a, options.to.a, t)};
      c.symbol = Recoloured(base, col);
    } else {
      const double px = options.min_px + (options.max_px - options.min_px) * t;
      c.symbol.marker_size_px = px;
      c.symbol.line_width_px = px;
    }
    out->push_back(std::move(c));
  }
  return Status::Ok();
}

namespace {

/// The new name of old field `name` under `change`; "" when it was deleted.
std::string FollowField(const std::string& name, const FeatureSchema& old, const SchemaChange& change,
                        const FieldDef** def) {
  *def = nullptr;
  const int at = old.Find(name);
  if (at < 0) return std::string();
  for (size_t i = 0; i < change.source.size() && i < change.schema.fields.size(); ++i) {
    if (change.source[i] == at) {
      *def = &change.schema.fields[i];
      return change.schema.fields[i].name;
    }
  }
  return std::string();
}

}  // namespace

void RemapStyleFields(FeatureStyle* style, const FeatureSchema& old, const SchemaChange& change) {
  if (style == nullptr) return;
  const FieldDef* def = nullptr;
  if (!style->field.empty()) {
    style->field = FollowField(style->field, old, change, &def);
    const bool numeric = def != nullptr && (def->type == FieldType::kInteger || def->type == FieldType::kReal);
    const bool usable = def != nullptr && def->type != FieldType::kBlob &&
                        (style->renderer != FeatureRenderer::kGraduated || numeric);
    if (!usable) {
      style->field.clear();
      style->renderer = FeatureRenderer::kSingle;
    }
  }
  if (!style->label.field.empty()) {
    style->label.field = FollowField(style->label.field, old, change, &def);
    if (def != nullptr && def->type == FieldType::kBlob) style->label.field.clear();
  }
}

}  // namespace fv
