// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_rule_style.h"
#include "fv_desk_style_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/style/expression.h"
#include "fvkit/symbol/builtin_icons.h"

namespace fv {
namespace desk {

namespace {

using json = nlohmann::json;
using M = RuleValueMode;
using K = RulePropertyKind;

Status Bad(const std::string& what) { return Status::Error(kInvalidArg, what); }

const std::vector<M> kAllModes = {M::kDefault, M::kConstant, M::kByField, M::kByClass, M::kByZoom, M::kExpression};
const std::vector<M> kDiscreteModes = {M::kDefault, M::kConstant, M::kByField, M::kExpression};
const std::vector<M> kConstantModes = {M::kDefault, M::kConstant, M::kExpression};
// The engine reads these as string constants only.
const std::vector<M> kFixedModes = {M::kDefault, M::kConstant};

RuleProperty Color(const char* key, const char* page, const char* title, const char* initial) {
  return {key, false, page, title, K::kColor, 0, 0, {}, kAllModes, initial};
}
RuleProperty Number(const char* key, bool layout, const char* page, const char* title, double min, double max,
                    const char* initial) {
  return {key, layout, page, title, K::kNumber, min, max, {}, kAllModes, initial};
}
RuleProperty Fixed(const char* key, bool layout, const char* page, const char* title, K kind,
                   std::vector<std::string> choices, std::vector<M> modes, const char* initial) {
  return {key, layout, page, title, kind, 0, 0, std::move(choices), std::move(modes), initial};
}

/// The property as an error names it: its page and title, as "Text size".
std::string Named(const RuleProperty& p) {
  std::string title = p.title;
  if (!title.empty()) title[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(title[0])));
  return p.page + " " + title;
}

bool IsNumberText(const std::string& text, double* out) {
  if (text.empty()) return false;
  char* end = nullptr;
  const double v = std::strtod(text.c_str(), &end);
  if (end == nullptr || *end != '\0' || !std::isfinite(v)) return false;
  *out = v;
  return true;
}

/// A number as JSON: integral values as integers, so 2 stays "2" and not "2.0".
json NumberJson(double v) {
  if (v == std::floor(v) && std::fabs(v) < 1e15) return json(static_cast<int64_t>(v));
  return json(v);
}

/// A JSON constant as the pages show it; false when it is not one the kind takes.
bool ConstantText(const RuleProperty& p, const json& v, std::string* out) {
  switch (p.kind) {
    case K::kNumber:
      if (!v.is_number()) return false;
      *out = v.dump();
      return true;
    case K::kColor:
    case K::kChoice:
    case K::kText:
      if (!v.is_string()) return false;
      *out = v.get<std::string>();
      return true;
    case K::kField:
      if (!v.is_array() || v.size() != 2 || v[0] != "get" || !v[1].is_string()) return false;
      *out = v[1].get<std::string>();
      return true;
    case K::kOffset:
      if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) return false;
      *out = v[0].dump() + " " + v[1].dump();
      return true;
    case K::kDash: {
      if (!v.is_array() || v.empty()) return false;
      std::string text;
      for (const json& n : v) {
        if (!n.is_number()) return false;
        text += (text.empty() ? "" : " ") + n.dump();
      }
      *out = text;
      return true;
    }
  }
  return false;
}

Status ConstantJson(const RuleProperty& p, const std::string& text, json* out) {
  switch (p.kind) {
    case K::kNumber: {
      double v = 0;
      if (!IsNumberText(text, &v)) return Bad(Named(p) + ": \"" + text + "\" is not a number");
      if (p.max > p.min && (v < p.min || v > p.max))
        return Bad(Named(p) + ": " + text + " is outside " + json(p.min).dump() + "–" + json(p.max).dump());
      *out = NumberJson(v);
      return Status::Ok();
    }
    case K::kColor: {
      FvColor c;
      if (!ParseCssColor(text, &c)) return Bad(Named(p) + ": \"" + text + "\" is not a colour");
      *out = text;
      return Status::Ok();
    }
    case K::kChoice:
      if (std::find(p.choices.begin(), p.choices.end(), text) == p.choices.end())
        return Bad(Named(p) + ": \"" + text + "\" is not one of its choices");
      *out = text;
      return Status::Ok();
    case K::kText:
      if (text.empty()) return Bad(Named(p) + " is empty");
      *out = text;
      return Status::Ok();
    case K::kField:
      if (text.empty()) return Bad(Named(p) + ": choose a field");
      *out = json::array({"get", text});
      return Status::Ok();
    case K::kOffset: {
      std::istringstream in(text);
      double x = 0, y = 0;
      std::string rest;
      if (!(in >> x >> y) || (in >> rest)) return Bad(Named(p) + ": give two numbers, x and y");
      *out = json::array({NumberJson(x), NumberJson(y)});
      return Status::Ok();
    }
    case K::kDash: {
      std::istringstream in(text);
      json dash = json::array();
      std::string word;
      while (in >> word) {
        double v = 0;
        if (!IsNumberText(word, &v) || v < 0) return Bad(Named(p) + ": \"" + word + "\" is not a length");
        dash.push_back(NumberJson(v));
      }
      if (dash.empty()) return Bad(Named(p) + " is empty");
      *out = dash;
      return Status::Ok();
    }
  }
  return Bad(Named(p) + ": unknown kind");
}

/// The field a `match` or `step` input reads: `["get", f]` or `["to-string", ["get", f]]`.
bool InputField(const json& input, std::string* field) {
  const json* get = &input;
  if (input.is_array() && input.size() == 2 && input[0] == "to-string") get = &input[1];
  if (!get->is_array() || get->size() != 2 || (*get)[0] != "get" || !(*get)[1].is_string()) return false;
  *field = (*get)[1].get<std::string>();
  return true;
}

bool Offers(const RuleProperty& p, M mode) { return std::find(p.modes.begin(), p.modes.end(), mode) != p.modes.end(); }

/// Reads `v` into `out` in the first mode that fits it; false when none does.
bool ReadStructured(const RuleProperty& p, const json& v, RulePropertyValue* out) {
  std::string text;
  if (ConstantText(p, v, &text)) {
    out->mode = M::kConstant;
    out->constant = text;
    return true;
  }
  if (!v.is_array() || v.empty() || !v[0].is_string()) return false;
  const std::string op = v[0].get<std::string>();
  // ["match", input, key, value, …, other]
  if (op == "match" && Offers(p, M::kByField) && v.size() >= 5 && v.size() % 2 == 1) {
    if (!InputField(v[1], &out->field) || !ConstantText(p, v[v.size() - 1], &out->constant)) return false;
    for (size_t i = 2; i + 1 < v.size(); i += 2) {
      RuleStop s;
      if (v[i].is_string()) s.key = v[i].get<std::string>();
      else if (v[i].is_number()) s.key = v[i].dump();
      else return false;
      if (!ConstantText(p, v[i + 1], &s.value)) return false;
      out->stops.push_back(std::move(s));
    }
    out->mode = M::kByField;
    return true;
  }
  // ["step", ["get", field], base, lower, value, …]
  if (op == "step" && Offers(p, M::kByClass) && v.size() >= 5 && v.size() % 2 == 1) {
    if (!v[1].is_array() || v[1].size() != 2 || v[1][0] != "get" || !InputField(v[1], &out->field)) return false;
    if (!ConstantText(p, v[2], &out->constant)) return false;
    for (size_t i = 3; i + 1 < v.size(); i += 2) {
      RuleStop s;
      if (!v[i].is_number() || !ConstantText(p, v[i + 1], &s.value)) return false;
      s.key = v[i].dump();
      out->stops.push_back(std::move(s));
    }
    out->mode = M::kByClass;
    return true;
  }
  // ["interpolate", ["linear"], ["zoom"], zoom, value, …]
  if (op == "interpolate" && Offers(p, M::kByZoom) && v.size() >= 5 && v.size() % 2 == 1 &&
      v[1] == json::array({"linear"}) && v[2] == json::array({"zoom"})) {
    for (size_t i = 3; i + 1 < v.size(); i += 2) {
      RuleStop s;
      if (!v[i].is_number() || !ConstantText(p, v[i + 1], &s.value)) return false;
      s.key = v[i].dump();
      out->stops.push_back(std::move(s));
    }
    out->mode = M::kByZoom;
    return true;
  }
  return false;
}

/// Stops keyed by numbers in strictly ascending order, as `step` and `interpolate` need.
Status AscendingKeys(const RuleProperty& p, const std::vector<RuleStop>& stops, const char* what) {
  double last = -INFINITY;
  for (const RuleStop& s : stops) {
    double k = 0;
    if (!IsNumberText(s.key, &k)) return Bad(Named(p) + ": " + what + " \"" + s.key + "\" is not a number");
    if (k <= last) return Bad(Named(p) + ": each " + what + " must be above the one before");
    last = k;
  }
  return Status::Ok();
}

/// Checks the values `value` holds so far, leaving what is missing (a field,
/// a stop, their order) to `WriteRuleValue`, so a table can be built up a row at a time.
Status CheckParts(const RuleProperty& p, const RulePropertyValue& value) {
  json scratch;
  Status s;
  if (value.mode == M::kConstant || (!value.constant.empty() && value.mode != M::kByZoom))
    if (!(s = ConstantJson(p, value.constant, &scratch)).ok()) return s;
  if (value.mode != M::kByField && value.mode != M::kByClass && value.mode != M::kByZoom) return s;
  for (const RuleStop& stop : value.stops) {
    double k = 0;
    if (value.mode != M::kByField && !IsNumberText(stop.key, &k))
      return Bad(Named(p) + ": \"" + stop.key + "\" is not a number");
    if (!(s = ConstantJson(p, stop.value, &scratch)).ok()) return s;
  }
  return s;
}

/// The member `key` of a parsed paint or layout object, as compact JSON; "" when absent.
std::string Member(const json& o, const std::string& key) {
  if (!o.is_object() || !o.contains(key)) return "";
  return o[key].dump();
}

}  // namespace

const std::vector<RuleProperty>& RuleProperties(const std::string& type) {
  static const std::map<std::string, std::vector<RuleProperty>> kPages = [] {
    std::map<std::string, std::vector<RuleProperty>> pages;
    pages["fill"] = {
        Color("fill-color", "Fill", "Colour", "#000000"),
        Number("fill-opacity", false, "Fill", "Opacity", 0, 1, "1"),
        Color("fill-outline-color", "Fill", "Outline colour", "#000000"),
        Fixed("fill-pattern", false, "Fill", "Pattern", K::kText, {}, kDiscreteModes, ""),
    };
    pages["line"] = {
        Color("line-color", "Line", "Colour", "#000000"),
        Number("line-width", false, "Line", "Width", 0, 64, "1"),
        Number("line-opacity", false, "Line", "Opacity", 0, 1, "1"),
        Fixed("line-dasharray", false, "Line", "Dash", K::kDash, {}, kConstantModes, "4 2"),
    };
    pages["circle"] = {
        Number("circle-radius", false, "Circle", "Radius", 0, 64, "5"),
        Color("circle-color", "Circle", "Fill colour", "#000000"),
        Number("circle-opacity", false, "Circle", "Opacity", 0, 1, "1"),
        Color("circle-stroke-color", "Circle", "Stroke colour", "#000000"),
        Number("circle-stroke-width", false, "Circle", "Stroke width", 0, 32, "0"),
        Fixed("fv-circle-shape", false, "Circle", "Shape", K::kChoice, {"circle", "square"}, kDiscreteModes, "circle"),
    };
    // Only the icon properties the engine draws; anchor, offset and overlap are not read.
    RuleProperty icon = Fixed("icon-image", true, "Icon", "Icon", K::kChoice, RuleIconNames(), kDiscreteModes,
                              "circle");
    icon.icons = true;
    pages["symbol"] = {
        icon,
        Number("icon-size", true, "Icon", "Size", 0, 10, "1"),
        Color("icon-color", "Icon", "Colour", "#000000"),
        Number("icon-opacity", false, "Icon", "Opacity", 0, 1, "1"),
        Number("icon-rotate", true, "Icon", "Rotation", 0, 360, "0"),
        Fixed("text-field", true, "Text", "Label field", K::kField, {}, kConstantModes, ""),
        Number("text-size", true, "Text", "Size", 1, 96, "16"),
        Color("text-color", "Text", "Colour", "#000000"),
        Color("text-halo-color", "Text", "Halo colour", "#ffffff"),
        Number("text-halo-width", false, "Text", "Halo width", 0, 16, "0"),
        Fixed("text-anchor", true, "Text", "Anchor", K::kChoice,
              {"center", "left", "right", "top", "bottom", "top-left", "top-right", "bottom-left", "bottom-right"},
              kFixedModes, "center"),
        Fixed("text-offset", true, "Text", "Offset", K::kOffset, {}, kFixedModes, "0 0"),
        Fixed("symbol-placement", true, "Text", "Placement", K::kChoice, {"point", "line", "line-center"},
              kFixedModes, "point"),
    };
    return pages;
  }();
  static const std::vector<RuleProperty> kNone;
  const auto it = kPages.find(type);
  return it == kPages.end() ? kNone : it->second;
}

const std::vector<std::string>& RuleIconNames() {
  static const std::vector<std::string> kNames = [] {
    std::vector<std::string> names = BuiltinIconSheet()->ids();
    std::sort(names.begin(), names.end());
    return names;
  }();
  return kNames;
}

Status RenderRuleIcon(const std::string& name, int size, uint32_t rgba, PixelBuffer* out) {
  if (size <= 0) return Bad("the preview size must be positive");
  const SymbolPixmap* pixmap = BuiltinIconSheet()->Pixmap(name);
  if (pixmap == nullptr || pixmap->tile.Empty()) return Status::Error(kNotFound, "no icon \"" + name + "\"");
  const PixelBuffer& tile = pixmap->tile;
  const int tw = tile.Width(), th = tile.Height();
  const double scale = static_cast<double>(size) / std::max(tw, th);
  const double ox = (size - tw * scale) / 2, oy = (size - th * scale) / 2;
  const double tint_a = (rgba & 0xff) / 255.0;
  // Premultiplied tile sample at (x, y), clamped to the tile; the tint replaces an SDF icon's colour.
  auto texel = [&](int x, int y, double px[4]) {
    if (x < 0 || y < 0 || x >= tw || y >= th) {
      px[0] = px[1] = px[2] = px[3] = 0;
      return;
    }
    const unsigned char* p = tile.Row(y) + 4 * x;
    const double a = p[3] / 255.0 * (pixmap->sdf ? tint_a : 1.0);
    for (int c = 0; c < 3; ++c) {
      const double v = pixmap->sdf ? ((rgba >> (24 - 8 * c)) & 0xff) / 255.0 : p[c] / 255.0;
      px[c] = v * a;
    }
    px[3] = a;
  };
  *out = PixelBuffer(size, size);
  for (int y = 0; y < size; ++y) {
    unsigned char* row = out->Row(y);
    for (int x = 0; x < size; ++x) {
      // Bilinear from the pixel centre back into tile space.
      const double sx = (x + 0.5 - ox) / scale - 0.5, sy = (y + 0.5 - oy) / scale - 0.5;
      const int x0 = static_cast<int>(std::floor(sx)), y0 = static_cast<int>(std::floor(sy));
      const double fx = sx - x0, fy = sy - y0;
      double a[4], b[4], c[4], d[4];
      texel(x0, y0, a);
      texel(x0 + 1, y0, b);
      texel(x0, y0 + 1, c);
      texel(x0 + 1, y0 + 1, d);
      for (int ch = 0; ch < 4; ++ch) {
        const double v = (a[ch] * (1 - fx) + b[ch] * fx) * (1 - fy) + (c[ch] * (1 - fx) + d[ch] * fx) * fy;
        row[4 * x + ch] = static_cast<unsigned char>(std::lround(std::clamp(v, 0.0, 1.0) * 255));
      }
    }
  }
  return Status::Ok();
}

RulePropertyValue ReadRuleValue(const RuleProperty& property, const std::string& json_text) {
  RulePropertyValue out;
  if (json_text.empty()) return out;
  const json v = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
  if (!v.is_discarded() && ReadStructured(property, v, &out)) return out;
  out = RulePropertyValue();
  out.mode = M::kExpression;
  out.expression = v.is_discarded() ? json_text : v.dump();
  return out;
}

Status WriteRuleValue(const RuleProperty& p, const RulePropertyValue& value, std::string* json_text) {
  if (!Offers(p, value.mode)) return Bad(Named(p) + " cannot take that kind of value");
  json out;
  Status s;
  switch (value.mode) {
    case M::kDefault:
      json_text->clear();
      return Status::Ok();
    case M::kConstant:
      s = ConstantJson(p, value.constant, &out);
      break;
    case M::kByField: {
      if (value.field.empty()) return Bad(Named(p) + ": choose a field");
      if (value.stops.empty()) return Bad(Named(p) + ": add a value");
      out = json::array({"match", json::array({"to-string", json::array({"get", value.field})})});
      std::set<std::string> seen;
      for (const RuleStop& stop : value.stops) {
        if (!seen.insert(stop.key).second) return Bad(Named(p) + ": \"" + stop.key + "\" is listed twice");
        json v;
        if (!(s = ConstantJson(p, stop.value, &v)).ok()) return s;
        out.push_back(stop.key);
        out.push_back(v);
      }
      json other;
      if (!(s = ConstantJson(p, value.constant, &other)).ok()) return s;
      out.push_back(other);
      break;
    }
    case M::kByClass:
    case M::kByZoom: {
      const bool zoom = value.mode == M::kByZoom;
      if (!zoom && value.field.empty()) return Bad(Named(p) + ": choose a field");
      if (value.stops.empty()) return Bad(Named(p) + (zoom ? ": add a zoom level" : ": add a class"));
      if (!(s = AscendingKeys(p, value.stops, zoom ? "zoom" : "class")).ok()) return s;
      if (zoom) {
        out = json::array({"interpolate", json::array({"linear"}), json::array({"zoom"})});
      } else {
        json base;
        if (!(s = ConstantJson(p, value.constant, &base)).ok()) return s;
        out = json::array({"step", json::array({"get", value.field}), base});
      }
      for (const RuleStop& stop : value.stops) {
        double k = 0;
        IsNumberText(stop.key, &k);
        json v;
        if (!(s = ConstantJson(p, stop.value, &v)).ok()) return s;
        out.push_back(NumberJson(k));
        out.push_back(v);
      }
      break;
    }
    case M::kExpression:
      out = json::parse(value.expression, nullptr, /*allow_exceptions=*/false);
      if (out.is_discarded()) return Bad(Named(p) + ": the expression is not valid JSON");
      break;
  }
  if (!s.ok()) return s;
  *json_text = out.dump();
  return Status::Ok();
}

Status RuleStyleModel::Open(const std::shared_ptr<FeatureOverlay>& overlay, const std::string& rule_id,
                            std::shared_ptr<RuleStyleModel>* out) {
  if (!overlay || !overlay->layer()) return Status::Error(kNotFound, "no feature layer");
  std::shared_ptr<RuleStyleModel> m(new RuleStyleModel());
  m->overlay_ = overlay;
  m->rule_id_ = rule_id;
  Status s = m->Revert();
  if (!s.ok()) return s;
  *out = std::move(m);
  return Status::Ok();
}

std::string RuleStyleModel::Title() const { return "Rule Style — " + rule_id_ + " (" + overlay_->Name() + ")"; }

void RuleStyleModel::Load(const StyleRule& rule) {
  rule_ = rule;
  draft_ = rule;
  values_.clear();
  edited_.clear();
  const json paint = json::parse(rule.paint, nullptr, /*allow_exceptions=*/false);
  const json layout = json::parse(rule.layout, nullptr, /*allow_exceptions=*/false);
  for (const RuleProperty& p : RuleProperties(rule.type))
    values_.push_back(ReadRuleValue(p, Member(p.layout ? layout : paint, p.key)));
}

void RuleStyleModel::MarkEdited(size_t k) {
  if (std::find(edited_.begin(), edited_.end(), k) == edited_.end()) edited_.push_back(k);
}

Status RuleStyleModel::Revert() {
  FeatureStyle rules;
  Status s = FeatureStyleToRules(overlay_->style(), &rules, LayerGeometry(*overlay_));
  if (!s.ok()) return s;
  for (const StyleRule& r : rules.rules) {
    if (r.id != rule_id_) continue;
    Load(r);
    return Status::Ok();
  }
  return Status::Error(kNotFound, "the rule \"" + rule_id_ + "\" is gone");
}

Status RuleStyleModel::SetValue(size_t k, const RulePropertyValue& value) {
  const std::vector<RuleProperty>& props = properties();
  if (k >= props.size()) return Bad("no property " + std::to_string(k));
  if (!Offers(props[k], value.mode)) return Bad(Named(props[k]) + " cannot take that kind of value");
  // An expression is checked on Verify and Apply, so a half-typed one can be kept.
  Status s = CheckParts(props[k], value);
  if (!s.ok()) return s;
  values_[k] = value;
  MarkEdited(k);
  return Status::Ok();
}

Status RuleStyleModel::SetMode(size_t k, RuleValueMode mode) {
  const std::vector<RuleProperty>& props = properties();
  if (k >= props.size()) return Bad("no property " + std::to_string(k));
  const RuleProperty& p = props[k];
  if (!Offers(p, mode)) return Bad(Named(p) + " cannot take that kind of value");
  const RulePropertyValue& old = values_[k];
  if (old.mode == mode) return Status::Ok();
  RulePropertyValue next;
  next.mode = mode;
  // A value to carry across: the old constant, else its first table value, else the engine's default.
  std::string seed = old.constant;
  if (seed.empty() && !old.stops.empty()) seed = old.stops.front().value;
  if (seed.empty()) seed = p.initial;
  switch (mode) {
    case M::kDefault:
      break;
    case M::kConstant:
      next.constant = seed;
      break;
    case M::kByField:
    case M::kByClass:
      next.constant = seed;
      next.field = old.field;
      break;
    case M::kByZoom:
      next.stops = {{"0", seed}, {"24", seed}};
      break;
    case M::kExpression: {
      std::string text;
      if (WriteRuleValue(p, old, &text).ok()) next.expression = text;
      break;
    }
  }
  values_[k] = next;
  MarkEdited(k);
  return Status::Ok();
}

std::vector<std::string> RuleStyleModel::Fields(bool numeric) const {
  std::vector<std::string> out;
  for (const FieldDef& f : overlay_->layer()->Schema().fields) {
    if (f.type == FieldType::kBlob) continue;
    if (numeric && f.type != FieldType::kInteger && f.type != FieldType::kReal) continue;
    out.push_back(f.name);
  }
  return out;
}

Status RuleStyleModel::FillFieldValues(size_t k) {
  if (k >= values_.size()) return Bad("no property " + std::to_string(k));
  RulePropertyValue v = values_[k];
  if (v.mode != M::kByField) return Bad("the value does not vary by field");
  const FeatureSchema& schema = overlay_->layer()->Schema();
  const int fi = schema.Find(v.field);
  if (fi < 0) return Status::Error(kNotFound, "no field \"" + v.field + "\"");
  std::vector<Feature> all;
  Status s = overlay_->layer()->All(&all);
  if (!s.ok()) return s;
  std::set<std::string> keys;
  for (const Feature& f : all) {
    const std::string key = FeatureCategoryKey(f.values[static_cast<size_t>(fi)]);
    if (!key.empty()) keys.insert(key);
    if (keys.size() >= 50) break;
  }
  if (keys.empty()) return Status::Error(kNotFound, "no feature has a value for \"" + v.field + "\"");
  v.stops.clear();
  for (const std::string& key : keys) v.stops.push_back({key, v.constant});
  return SetValue(k, v);
}

Status RuleStyleModel::Verify(size_t k, const std::string& expression) const {
  const std::vector<RuleProperty>& props = properties();
  if (k >= props.size()) return Bad("no property " + std::to_string(k));
  RulePropertyValue v;
  v.mode = M::kExpression;
  v.expression = expression;
  std::string text;
  Status s = WriteRuleValue(props[k], v, &text);
  if (!s.ok()) return s;
  // The engine checks a value when the style is read, so read a one-rule style holding it.
  StyleRule probe = rule_;
  probe.filter.clear();
  probe.paint = "{}";
  probe.layout = "{}";
  json object = json::object();
  object[props[k].key] = json::parse(text);
  (props[k].layout ? probe.layout : probe.paint) = object.dump();
  FeatureStyle style;
  style.renderer = FeatureRenderer::kRules;
  style.rules.push_back(probe);
  FeatureStyle checked;
  s = WriteFeatureStyleJson(style, &text);
  if (s.ok()) s = ReadFeatureStyleJson(text, &checked);
  return s.ok() ? s : Bad(Named(props[k]) + ": " + s.message);
}

Status RuleStyleModel::SetZoomRange(double min_zoom, double max_zoom) {
  if (!(min_zoom >= 0 && min_zoom < max_zoom && max_zoom <= 24))
    return Bad("the zoom range must run from 0 to 24, minimum below maximum");
  draft_.min_zoom = min_zoom;
  draft_.max_zoom = max_zoom;
  return Status::Ok();
}

Status RuleStyleModel::Draft(StyleRule* out) const {
  StyleRule r = draft_;
  json paint = json::parse(rule_.paint, nullptr, /*allow_exceptions=*/false);
  json layout = json::parse(rule_.layout, nullptr, /*allow_exceptions=*/false);
  if (!paint.is_object()) paint = json::object();
  if (!layout.is_object()) layout = json::object();
  const std::vector<RuleProperty>& props = properties();
  // Only edited properties are rewritten, so the rest keep their JSON as read.
  for (size_t k : edited_) {
    std::string text;
    Status s = WriteRuleValue(props[k], values_[k], &text);
    if (!s.ok()) return s;
    json& object = props[k].layout ? layout : paint;
    if (text.empty()) object.erase(props[k].key);
    else object[props[k].key] = json::parse(text);
  }
  r.paint = paint.dump();
  r.layout = layout.dump();
  *out = std::move(r);
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
