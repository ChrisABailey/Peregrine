// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The MapLibre JSON form of `FeatureStyle` (feature-overlay-plan §3,
/// layer-manager-plan §5). The root `metadata` names the renderer and its
/// classes. For the generated renderers each paint value is a constant or an
/// expression that the reader evaluates once per class through the shared
/// `StyleExpression` evaluator; a rules style keeps each layer as written.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <set>

#include <nlohmann/json.hpp>

#include "fvkit/app/properties.h"
#include "fvkit/feature/style.h"
#include "fvkit/feature/vector_source.h"
#include "fvkit/style/expression.h"
#include "fvkit/style/maplibre_style.h"

namespace fv {

namespace {

using json = nlohmann::ordered_json;

/// The glyph source an exported style names, for MapLibre to draw its labels.
constexpr const char kExportGlyphs[] = "https://demotiles.maplibre.org/font/{fontstack}/{range}.pbf";

Status Bad(const std::string& why) { return Status::Error(kInvalidArg, "feature style: " + why); }

enum class Kind { kColor, kNumber, kText };

/// One symbol property and where it lives in the document.
struct SymbolProp {
  const char* layer;
  const char* name;
  Kind kind;
  std::function<json(const FeatureSymbol&)> get;
  std::function<bool(const json&, FeatureSymbol*)> set;
};

std::string ColorText(const FvColor& c) {
  char buf[48];
  if (c.a == 255)
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
  else
    std::snprintf(buf, sizeof buf, "rgba(%d, %d, %d, %.6g)", c.r, c.g, c.b, c.a / 255.0);
  return buf;
}

bool ColorFrom(const json& j, FvColor* out) {
  if (!j.is_string()) return false;
  app::PropertyValue v = app::PropertyValue::Color(FvColor{});
  if (!v.FromString(j.get<std::string>())) return false;
  *out = v.color;
  return true;
}

bool NumberFrom(const json& j, double* out) {
  if (!j.is_number()) return false;
  *out = j.get<double>();
  return std::isfinite(*out);
}

SymbolProp ColorProp(const char* layer, const char* name, FvColor FeatureSymbol::*m) {
  return {layer, name, Kind::kColor, [m](const FeatureSymbol& s) { return json(ColorText(s.*m)); },
          [m](const json& j, FeatureSymbol* s) { return ColorFrom(j, &(s->*m)); }};
}

SymbolProp NumberProp(const char* layer, const char* name, double FeatureSymbol::*m) {
  return {layer, name, Kind::kNumber, [m](const FeatureSymbol& s) { return json(s.*m); },
          [m](const json& j, FeatureSymbol* s) { return NumberFrom(j, &(s->*m)); }};
}

const std::vector<SymbolProp>& Props() {
  static const std::vector<SymbolProp> props = {
      ColorProp("fv-fill", "fill-color", &FeatureSymbol::area_fill),
      ColorProp("fv-area-outline", "line-color", &FeatureSymbol::area_outline),
      NumberProp("fv-area-outline", "line-width", &FeatureSymbol::area_outline_px),
      ColorProp("fv-line-casing", "line-color", &FeatureSymbol::casing_color),
      // The casing layer's width is the whole casing, line included; the
      // reader turns it back into the width past each side of the line.
      {"fv-line-casing", "line-width", Kind::kNumber,
       [](const FeatureSymbol& s) {
         return json(s.casing_px > 0 ? s.line_width_px + 2 * s.casing_px : 0.0);
       },
       [](const json& j, FeatureSymbol* s) { return NumberFrom(j, &s->casing_px); }},
      ColorProp("fv-line", "line-color", &FeatureSymbol::line_color),
      NumberProp("fv-line", "line-width", &FeatureSymbol::line_width_px),
      {"fv-line", "fv-line-dash", Kind::kText, [](const FeatureSymbol& s) { return json(s.dash); },
       [](const json& j, FeatureSymbol* s) {
         if (!j.is_string()) return false;
         s->dash = j.get<std::string>();
         return true;
       }},
      // MapLibre sizes a circle by radius; the symbol by its diameter.
      {"fv-circle", "circle-radius", Kind::kNumber,
       [](const FeatureSymbol& s) { return json(s.marker_size_px / 2); },
       [](const json& j, FeatureSymbol* s) {
         double r = 0;
         if (!NumberFrom(j, &r)) return false;
         s->marker_size_px = r * 2;
         return true;
       }},
      ColorProp("fv-circle", "circle-color", &FeatureSymbol::marker_fill),
      ColorProp("fv-circle", "circle-stroke-color", &FeatureSymbol::marker_outline),
      NumberProp("fv-circle", "circle-stroke-width", &FeatureSymbol::marker_outline_px),
      {"fv-circle", "fv-circle-shape", Kind::kText,
       [](const FeatureSymbol& s) { return json(s.marker == FeatureMarker::kSquare ? "square" : "circle"); },
       [](const json& j, FeatureSymbol* s) {
         if (!j.is_string()) return false;
         const std::string v = j.get<std::string>();
         if (v != "square" && v != "circle") return false;
         s->marker = v == "square" ? FeatureMarker::kSquare : FeatureMarker::kCircle;
         return true;
       }},
  };
  return props;
}

struct LayerDef {
  const char* id;
  const char* type;
  const char* geometry;  ///< "Polygon", "LineString", "Point"
};

const LayerDef kLayers[] = {
    {"fv-fill", "fill", "Polygon"},          {"fv-area-outline", "line", "Polygon"},
    {"fv-line-casing", "line", "LineString"}, {"fv-line", "line", "LineString"},
    {"fv-circle", "circle", "Point"},
};

json GeometryFilter(const std::string& geometry) {
  return json::array({"match", json::array({"geometry-type"}),
                      json::array({geometry, "Multi" + geometry}), true, false});
}

/// Every symbol a style can draw with.
std::vector<const FeatureSymbol*> AllSymbols(const FeatureStyle& style) {
  std::vector<const FeatureSymbol*> out{&style.symbol};
  for (const FeatureCategory& c : style.categories) out.push_back(&c.symbol);
  for (const FeatureClass& c : style.classes) out.push_back(&c.symbol);
  for (const auto& entry : style.overrides) out.push_back(&entry.second);
  return out;
}

/// The paint value of one property across the renderer and overrides.
Status PaintValue(const FeatureStyle& style, const SymbolProp& p, json* out) {
  const json other = p.get(style.symbol);
  json inner = other;
  bool varies = false;
  if (style.renderer == FeatureRenderer::kCategorised) {
    json m = json::array({"match", json::array({"to-string", json::array({"get", style.field})})});
    std::set<std::string> keys;
    for (const FeatureCategory& c : style.categories) {
      if (!keys.insert(c.value).second) continue;  // the first category with a key wins, as in SymbolFor
      const json v = p.get(c.symbol);
      varies = varies || v != other;
      m.push_back(c.value);
      m.push_back(v);
    }
    m.push_back(other);
    if (varies && !keys.empty()) inner = m;
  } else if (style.renderer == FeatureRenderer::kGraduated) {
    json st = json::array({"step", json::array({"get", style.field}), other});
    for (size_t i = 0; i < style.classes.size(); ++i) {
      const FeatureClass& c = style.classes[i];
      if (!std::isfinite(c.lower)) return Bad("a class bound is not a number");
      if (i > 0 && !(c.lower > style.classes[i - 1].lower))
        return Bad("class bounds must ascend: " + std::to_string(c.lower));
      const json v = p.get(c.symbol);
      varies = varies || v != other;
      st.push_back(c.lower);
      st.push_back(v);
    }
    if (varies) inner = st;
  }
  bool overridden = false;
  json ids = json::array({"match", json::array({"id"})});
  for (const auto& [id, sym] : style.overrides) {
    const json v = p.get(sym);
    overridden = overridden || varies || v != other;
    ids.push_back(id);
    ids.push_back(v);
  }
  ids.push_back(inner);
  *out = overridden ? ids : inner;
  return Status::Ok();
}

/// Which class a paint expression is evaluated for.
struct Context {
  std::optional<int64_t> id;
  std::optional<std::string> key;  ///< categorised
  std::optional<double> number;    ///< graduated
};

bool IsGet(const json& j) { return j.is_array() && j.size() == 2 && j[0] == "get" && j[1].is_string(); }

/// A paint value evaluated for one class: `field` reads as the class's key or
/// number, `["id"]` as the override's feature id.
Status Evaluate(const StyleExpression& e, const std::string& field, const Context& ctx, json* out) {
  StyleExpressionInput in;
  in.get = [&](const std::string& key, StyleValue* v) {
    if (key != field) return false;
    if (ctx.key) *v = StyleValue::String(*ctx.key);
    else if (ctx.number) *v = StyleValue::Number(*ctx.number);
    else return false;
    return true;
  };
  in.has_id = ctx.id.has_value();
  in.id = ctx.id.value_or(0);
  const StyleValue v = e.Evaluate(in);
  switch (v.type) {
    case StyleValue::Type::kNumber: *out = v.number; break;
    case StyleValue::Type::kString: *out = v.string; break;
    case StyleValue::Type::kBool: *out = v.boolean; break;
    case StyleValue::Type::kColor: *out = ColorText(v.color); break;
    default: return Bad("expression has no value: " + e.text());
  }
  return Status::Ok();
}

const json* FindLayer(const json& root, const char* id) {
  const auto it = root.find("layers");
  if (it == root.end() || !it->is_array()) return nullptr;
  for (const json& l : *it)
    if (l.is_object() && l.value("id", "") == id) return &l;
  return nullptr;
}

const json* FindProperty(const json& layer, const char* name) {
  for (const char* section : {"paint", "layout"}) {
    const auto s = layer.find(section);
    if (s == layer.end() || !s->is_object()) continue;
    const auto p = s->find(name);
    if (p != s->end()) return &*p;
  }
  return nullptr;
}


// MARK: Rules

/// Layer ids the generated renderers write.
bool IsGeneratedId(const std::string& id) {
  for (const LayerDef& def : kLayers)
    if (id == def.id) return true;
  return id == "fv-label" || id == "fv-label-shapes";
}

/// The rule name a generated layer converts to.
std::string GeneratedRuleName(const std::string& id) {
  static const std::map<std::string, std::string> kNames = {
      {"fv-fill", "Areas"},        {"fv-area-outline", "Area outlines"}, {"fv-line-casing", "Line casings"},
      {"fv-line", "Lines"},        {"fv-circle", "Points"},              {"fv-label", "Point labels"},
      {"fv-label-shapes", "Labels"}};
  const auto it = kNames.find(id);
  return it == kNames.end() ? id : it->second;
}

/// The override facet a rule of `type` takes when it names none.
std::string DefaultFacet(const std::string& type) {
  if (type == "fill") return "fv-fill";
  if (type == "line") return "fv-line";
  if (type == "circle") return "fv-circle";
  return std::string();
}

bool IsRuleType(const std::string& type) {
  return type == "fill" || type == "line" || type == "circle" || type == "symbol" || type == "background";
}

/// `sym` as `{"fv-fill": {"fill-color": …}, "fv-line": {…}, …}`, keyed like
/// the generated layers.
json SymbolJson(const FeatureSymbol& sym) {
  json out = json::object();
  for (const SymbolProp& p : Props()) out[p.layer][p.name] = p.get(sym);
  return out;
}

Status SymbolFromJson(const json& j, FeatureSymbol* sym) {
  if (!j.is_object()) return Bad("an override symbol is not an object");
  for (const SymbolProp& p : Props()) {
    const auto layer = j.find(p.layer);
    if (layer == j.end() || !layer->is_object()) continue;
    const auto v = layer->find(p.name);
    if (v == layer->end()) continue;
    if (!p.set(*v, sym)) return Bad(std::string("override ") + p.layer + " " + p.name + " has a bad value");
  }
  // Back from the casing's whole width to the width past the line.
  sym->casing_px = sym->casing_px > 0 ? std::max(0.0, (sym->casing_px - sym->line_width_px) / 2) : 0.0;
  return Status::Ok();
}

/// Parses `text` as a JSON object; "" is the empty object.
Status ObjectFrom(const std::string& text, const std::string& what, json* out) {
  if (text.empty()) {
    *out = json::object();
    return Status::Ok();
  }
  *out = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (out->is_discarded() || !out->is_object()) return Bad(what + " is not a JSON object");
  return Status::Ok();
}

/// `width` widened by `by`: a number plus `by`, an expression wrapped in
/// `["+", …]`, and `fallback + by` when the rule sets none.
json Widened(const json* width, double fallback, double by) {
  if (width == nullptr) return json(fallback + by);
  if (width->is_number()) return json(width->get<double>() + by);
  return json::array({"+", *width, by});
}

/// kInvalidArg unless rule `id`'s filter and every value in `section` use
/// expression syntax: no legacy filters, no `{"stops": …}` functions.
Status CheckExpressions(const std::string& id, const json* filter, const json& section) {
  if (filter != nullptr && !IsMapLibreExpressionFilter(filter->dump()))
    return Bad("rule " + id + ": the filter must be an expression, such as [\"==\", [\"get\", \"class\"], \"river\"]");
  for (auto it = section.begin(); it != section.end(); ++it)
    if (it.value().is_object())
      return Bad("rule " + id + ": " + it.key() + " must be a constant or an expression");
  return Status::Ok();
}

/// One rule as a MapLibre layer, with `overrides` wrapped around the paint
/// values of its facet.
Status RuleLayer(const StyleRule& rule, const std::map<int64_t, FeatureSymbol>& overrides, json* out) {
  if (rule.id.empty()) return Bad("a rule needs an id");
  if (!IsRuleType(rule.type)) return Bad("rule " + rule.id + ": unknown type \"" + rule.type + "\"");
  json extra, layout, paint;
  Status s = ObjectFrom(rule.extra, "rule " + rule.id + " extra", &extra);
  if (s.ok()) s = ObjectFrom(rule.layout, "rule " + rule.id + " layout", &layout);
  if (s.ok()) s = ObjectFrom(rule.paint, "rule " + rule.id + " paint", &paint);
  if (!s.ok()) return s;

  json l;
  l["id"] = rule.id;
  l["type"] = rule.type;
  if (rule.type != "background") {
    l["source"] = "features";
    l["source-layer"] = kFeatureSourceLayer;
  }
  if (rule.min_zoom > 0) l["minzoom"] = rule.min_zoom;
  if (rule.max_zoom < 24) l["maxzoom"] = rule.max_zoom;
  if (!rule.filter.empty()) {
    json f = json::parse(rule.filter, nullptr, /*allow_exceptions=*/false);
    if (f.is_discarded()) return Bad("rule " + rule.id + ": the filter is not JSON");
    l["filter"] = f;
  }
  s = CheckExpressions(rule.id, l.contains("filter") ? &l["filter"] : nullptr, paint);
  if (s.ok()) s = CheckExpressions(rule.id, nullptr, layout);
  if (!s.ok()) return s;
  if (!rule.shown) layout["visibility"] = "none";
  if (!layout.empty()) l["layout"] = layout;

  json meta = extra.value("metadata", json::object());
  if (!meta.is_object()) return Bad("rule " + rule.id + ": metadata is not an object");
  const std::string facet = rule.facet.empty() ? DefaultFacet(rule.type) : rule.facet;
  if (!overrides.empty() && !facet.empty()) {
    json wrapped = json::array();
    for (const SymbolProp& p : Props()) {
      if (facet != p.layer || !paint.contains(p.name)) continue;
      json m = json::array({"match", json::array({"id"})});
      for (const auto& [id, sym] : overrides) {
        m.push_back(id);
        m.push_back(p.get(sym));
      }
      m.push_back(paint[p.name]);
      paint[p.name] = m;
      wrapped.push_back(p.name);
    }
    if (!wrapped.empty()) meta["fv:overridden"] = wrapped;
  }
  if (!paint.empty()) l["paint"] = paint;
  if (!rule.legend.empty()) meta["fv:legend"] = rule.legend;
  if (!rule.facet.empty()) meta["fv:facet"] = rule.facet;
  if (!meta.empty()) l["metadata"] = meta;
  for (auto it = extra.begin(); it != extra.end(); ++it)
    if (it.key() != "metadata" && !l.contains(it.key())) l[it.key()] = it.value();
  *out = std::move(l);
  return Status::Ok();
}

/// A layer of a style document as a rule; what `RuleLayer` added is taken
/// back out. `source` and `source-layer` are dropped.
Status LayerRule(const json& l, StyleRule* out) {
  if (!l.is_object()) return Bad("a layer is not an object");
  StyleRule rule;
  const auto id = l.find("id");
  if (id == l.end() || !id->is_string() || id->get<std::string>().empty()) return Bad("a layer has no id");
  rule.id = id->get<std::string>();
  const auto type = l.find("type");
  if (type == l.end() || !type->is_string() || !IsRuleType(type->get<std::string>()))
    return Bad("layer " + rule.id + ": type must be fill, line, circle, symbol or background");
  rule.type = type->get<std::string>();
  json extra = json::object();
  json meta = json::object();
  json paint = json::object();
  for (auto it = l.begin(); it != l.end(); ++it) {
    const std::string& key = it.key();
    const json& v = it.value();
    if (key == "id" || key == "type" || key == "source" || key == "source-layer") continue;
    if (key == "filter") {
      rule.filter = v.dump();
    } else if (key == "minzoom" || key == "maxzoom") {
      if (!v.is_number()) return Bad("layer " + rule.id + ": " + key + " is not a number");
      (key == "minzoom" ? rule.min_zoom : rule.max_zoom) = v.get<double>();
    } else if (key == "layout") {
      if (!v.is_object()) return Bad("layer " + rule.id + ": layout is not an object");
      json layout = v;
      const auto vis = layout.find("visibility");
      if (vis != layout.end()) {
        rule.shown = !(vis->is_string() && vis->get<std::string>() == "none");
        layout.erase("visibility");
      }
      rule.layout = layout.dump();
    } else if (key == "paint") {
      if (!v.is_object()) return Bad("layer " + rule.id + ": paint is not an object");
      paint = v;
    } else if (key == "metadata") {
      if (!v.is_object()) return Bad("layer " + rule.id + ": metadata is not an object");
      meta = v;
    } else {
      extra[key] = v;
    }
  }
  for (const json& name : meta.value("fv:overridden", json::array())) {
    if (!name.is_string()) continue;
    const auto p = paint.find(name.get<std::string>());
    if (p != paint.end() && p->is_array() && p->size() >= 3 && (*p)[0] == "match" &&
        (*p)[1] == json::array({"id"}))
      *p = json((*p).back());
  }
  rule.paint = paint.dump();
  const auto text = [&meta](const char* key) {
    const auto it = meta.find(key);
    return it != meta.end() && it->is_string() ? it->get<std::string>() : std::string();
  };
  rule.legend = text("fv:legend");
  rule.facet = text("fv:facet");
  for (const char* key : {"fv:legend", "fv:facet", "fv:overridden"}) meta.erase(key);
  if (!meta.empty()) extra["metadata"] = meta;
  rule.extra = extra.dump();
  *out = std::move(rule);
  return Status::Ok();
}

/// The halo layers of one rule for the selected features, or none.
void RuleHalos(const StyleRule& rule, const json& selected, const FeatureSelectionStyle& selection,
               size_t index, json* layers) {
  if (!rule.shown || rule.type == "background") return;
  json paint, filter = json::array({"all", selected});
  if (!rule.paint.empty()) paint = json::parse(rule.paint, nullptr, false);
  if (paint.is_discarded() || !paint.is_object()) paint = json::object();
  if (!rule.filter.empty()) {
    const json f = json::parse(rule.filter, nullptr, false);
    if (!f.is_discarded()) filter.push_back(f);
  }
  const std::string color = ColorText(selection.color);
  const double w = selection.width_px;
  json l;
  l["id"] = "fv-halo-rule-" + std::to_string(index);
  l["source"] = "features";
  l["source-layer"] = kFeatureSourceLayer;
  if (rule.min_zoom > 0) l["minzoom"] = rule.min_zoom;
  if (rule.max_zoom < 24) l["maxzoom"] = rule.max_zoom;
  if (rule.type == "fill") {
    l["type"] = "line";
    filter.push_back(GeometryFilter("Polygon"));
    l["paint"] = json{{"line-color", color}, {"line-width", w + 1}};
  } else if (rule.type == "line") {
    l["type"] = "line";
    const auto width = paint.find("line-width");
    l["paint"] = json{{"line-color", color},
                      {"line-width", Widened(width == paint.end() ? nullptr : &*width, 1.0, w + 2)}};
  } else if (rule.type == "circle") {
    l["type"] = "circle";
    const auto radius = paint.find("circle-radius");
    l["paint"] = json{{"circle-color", color},
                      {"circle-radius", Widened(radius == paint.end() ? nullptr : &*radius, 5.0, w)}};
  } else {
    l["type"] = "circle";
    filter.push_back(GeometryFilter("Point"));
    l["paint"] = json{{"circle-color", color}, {"circle-radius", 6.0 + w}};
  }
  l["filter"] = filter;
  l["metadata"] = json{{"fv:halo", true}};
  layers->push_back(l);
}

/// The selection filter over `ids`.
json SelectedFilter(const std::vector<int64_t>& ids) {
  return ids.size() == 1 ? json::array({"==", json::array({"id"}), ids.front()})
                         : json::array({"in", json::array({"id"}), json::array({"literal", ids})});
}

Status WriteRulesJson(const FeatureStyle& style, const std::vector<int64_t>& highlight_ids,
                      const FeatureSelectionStyle& selection, std::string* out) {
  json meta = json::object();
  meta["fv:renderer"] = ToString(FeatureRenderer::kRules);
  if (!style.overrides.empty()) {
    json syms = json::object();
    for (const auto& [id, sym] : style.overrides) syms[std::to_string(id)] = SymbolJson(sym);
    meta["fv:override-symbols"] = syms;
  }
  json layers = json::array();
  if (!highlight_ids.empty()) {
    const json selected = SelectedFilter(highlight_ids);
    for (size_t i = 0; i < style.rules.size(); ++i) RuleHalos(style.rules[i], selected, selection, i, &layers);
  }
  std::set<std::string> ids;
  for (const StyleRule& rule : style.rules) {
    if (!ids.insert(rule.id).second) return Bad("two rules are named " + rule.id);
    json l;
    Status s = RuleLayer(rule, style.overrides, &l);
    if (!s.ok()) return s;
    layers.push_back(std::move(l));
  }
  json root;
  root["version"] = 8;
  root["metadata"] = meta;
  root["sources"] = json{{"features", json{{"type", "geojson"},
                                           {"data", json{{"type", "FeatureCollection"},
                                                         {"features", json::array()}}}}}};
  root["layers"] = layers;
  *out = root.dump(1, '\t');
  return Status::Ok();
}

/// Reads the rules of `root`, and their overrides, into `style`.
Status ReadRules(const json& root, const json& meta, FeatureStyle* style) {
  style->renderer = FeatureRenderer::kRules;
  const json syms = meta.value("fv:override-symbols", json::object());
  if (!syms.is_object()) return Bad("fv:override-symbols is not an object");
  for (auto it = syms.begin(); it != syms.end(); ++it) {
    char* end = nullptr;
    const long long id = std::strtoll(it.key().c_str(), &end, 10);
    if (it.key().empty() || *end != '\0') return Bad("an override id is not an integer: " + it.key());
    FeatureSymbol sym;
    Status s = SymbolFromJson(it.value(), &sym);
    if (!s.ok()) return s;
    style->overrides[id] = sym;
  }
  const json layers = root.value("layers", json::array());
  if (!layers.is_array()) return Bad("layers is not an array");
  std::set<std::string> ids;
  for (const json& l : layers) {
    if (l.is_object() && l.value("metadata", json::object()).value("fv:halo", false)) continue;
    StyleRule rule;
    Status s = LayerRule(l, &rule);
    if (!s.ok()) return s;
    if (!ids.insert(rule.id).second) return Bad("two layers are named " + rule.id);
    if (style->label.field.empty() && rule.type == "symbol") {
      const json layout = json::parse(rule.layout, nullptr, false);
      const auto field = layout.find("text-field");
      if (field != layout.end() && IsGet(*field)) style->label.field = (*field)[1].get<std::string>();
    }
    style->rules.push_back(std::move(rule));
  }
  // The engine is the judge of what draws; a rule it rejects is a bad style.
  std::string text;
  Status s = WriteRulesJson(*style, {}, FeatureSelectionStyle(), &text);
  if (!s.ok()) return s;
  MapLibreStyleEngine engine;
  s = engine.LoadText(text);
  if (!s.ok()) return Bad(s.message);
  return Status::Ok();
}

/// Puts each layer's `metadata.fv:paint` and `fv:layout` back in its paint
/// and layout, undoing `ExportFeatureStyleJson`.
Status RestoreExported(json* root) {
  const auto layers = root->find("layers");
  if (layers == root->end() || !layers->is_array()) return Status::Ok();
  for (json& l : *layers) {
    if (!l.is_object()) continue;
    const auto meta = l.find("metadata");
    if (meta == l.end() || !meta->is_object()) continue;
    for (const char* section : {"paint", "layout"}) {
      const std::string key = std::string("fv:") + section;
      const auto moved = meta->find(key);
      if (moved == meta->end()) continue;
      if (!moved->is_object()) return Bad("layer metadata " + key + " is not an object");
      json& target = l[section];
      if (target.is_null()) target = json::object();
      if (!target.is_object()) return Bad(std::string("a layer's ") + section + " is not an object");
      for (auto it = moved->begin(); it != moved->end(); ++it) target[it.key()] = it.value();
      meta->erase(key);
    }
    if (meta->empty()) l.erase("metadata");
  }
  return Status::Ok();
}

/// True for a document a hand wrote rather than one of the generators: no
/// renderer named, and some layer that is not a generated one.
bool IsPlainStyle(const json& root, const json& meta) {
  if (meta.contains("fv:renderer")) return false;
  const auto layers = root.find("layers");
  if (layers == root.end() || !layers->is_array()) return false;
  for (const json& l : *layers)
    if (l.is_object() && l.contains("id") && l["id"].is_string() && !IsGeneratedId(l["id"].get<std::string>()))
      return true;
  return false;
}

}  // namespace

Status WriteFeatureStyleJson(const FeatureStyle& style, std::string* out, int64_t highlight_id) {
  std::vector<int64_t> ids;
  if (highlight_id != 0) ids.push_back(highlight_id);
  return WriteFeatureStyleJson(style, out, ids);
}

Status WriteFeatureStyleJson(const FeatureStyle& style, std::string* out,
                             const std::vector<int64_t>& highlight_ids, const FeatureSelectionStyle& selection) {
  if (out == nullptr) return Bad("null out");
  if (style.renderer == FeatureRenderer::kRules) {
    try {
      return WriteRulesJson(style, highlight_ids, selection, out);
    } catch (const std::exception& e) {
      return Status::Error(kInternal, std::string("feature style: ") + e.what());
    }
  }
  if (style.renderer != FeatureRenderer::kSingle && style.field.empty())
    return Bad(std::string("a ") + ToString(style.renderer) + " style needs a field");
  try {
    json meta = json::object();
    meta["fv:renderer"] = ToString(style.renderer);
    if (style.renderer != FeatureRenderer::kSingle) meta["fv:field"] = style.field;
    if (style.renderer == FeatureRenderer::kCategorised) {
      json cats = json::array();
      for (const FeatureCategory& c : style.categories)
        cats.push_back(json{{"value", c.value}, {"label", c.label}});
      meta["fv:categories"] = cats;
    }
    if (style.renderer == FeatureRenderer::kGraduated) {
      json classes = json::array();
      for (const FeatureClass& c : style.classes)
        classes.push_back(json{{"lower", c.lower}, {"label", c.label}});
      meta["fv:classes"] = classes;
    }
    if (!style.overrides.empty()) {
      json ids = json::array();
      for (const auto& entry : style.overrides) ids.push_back(entry.first);
      meta["fv:overrides"] = ids;
    }

    json layers = json::array();
    if (!highlight_ids.empty()) {
      // The selection halo: each facet widened, under everything.
      const std::string halo_color = ColorText(selection.color);
      const double w = selection.width_px;
      struct HaloDef {
        const char* id;
        const char* type;
        const char* geometry;
        const char* prop;
        std::function<json(const FeatureSymbol&)> get;
      };
      const HaloDef halos[] = {
          {"fv-halo-area", "line", "Polygon", "line-width",
           [w](const FeatureSymbol& s) { return json(s.area_outline_px + w); }},
          {"fv-halo-line", "line", "LineString", "line-width",
           [w](const FeatureSymbol& s) { return json(s.line_width_px + w + 2); }},
          {"fv-halo-circle", "circle", "Point", "circle-radius",
           [w](const FeatureSymbol& s) { return json(s.marker_size_px / 2 + w); }},
      };
      for (const HaloDef& h : halos) {
        json v;
        Status s = PaintValue(style, SymbolProp{h.id, h.prop, Kind::kNumber, h.get, nullptr}, &v);
        if (!s.ok()) return s;
        json l;
        l["id"] = h.id;
        l["type"] = h.type;
        l["source"] = "features";
        l["source-layer"] = kFeatureSourceLayer;
        l["filter"] = json::array({"all", GeometryFilter(h.geometry),
                                   highlight_ids.size() == 1
                                       ? json::array({"==", json::array({"id"}), highlight_ids.front()})
                                       : json::array({"in", json::array({"id"}),
                                                      json::array({"literal", highlight_ids})})});
        l["paint"] = std::string(h.type) == "circle"
                         ? json{{"circle-color", halo_color}, {"circle-radius", v}}
                         : json{{"line-color", halo_color}, {"line-width", v}};
        layers.push_back(l);
      }
    }
    for (const LayerDef& def : kLayers) {
      json l;
      l["id"] = def.id;
      l["type"] = def.type;
      l["source"] = "features";
      l["source-layer"] = kFeatureSourceLayer;
      l["filter"] = GeometryFilter(def.geometry);
      json paint = json::object();
      for (const SymbolProp& p : Props()) {
        if (std::string(p.layer) != def.id) continue;
        json v;
        Status s = PaintValue(style, p, &v);
        if (!s.ok()) return s;
        paint[p.name] = v;
      }
      l["paint"] = paint;
      layers.push_back(l);
    }
    if (!style.label.field.empty()) {
      // A point's label sits above its largest marker; a line's or an area's
      // is centred on it.
      double marker = 0;
      for (const FeatureSymbol* sym : AllSymbols(style)) marker = std::max(marker, sym->marker_size_px);
      const double size = style.label.size_px > 0 ? style.label.size_px : 12.0;
      for (const bool points : {true, false}) {
        json l;
        l["id"] = points ? "fv-label" : "fv-label-shapes";
        l["type"] = "symbol";
        l["source"] = "features";
        l["source-layer"] = kFeatureSourceLayer;
        l["filter"] = points ? GeometryFilter("Point")
                             : json::array({"!", GeometryFilter("Point")});
        l["layout"] = json{{"text-field", json::array({"get", style.label.field})},
                           {"text-size", style.label.size_px},
                           {"text-anchor", "center"}};
        if (points) l["layout"]["text-offset"] = json::array({0, -(marker + size / 2) / size});
        l["paint"] = json{{"text-color", ColorText(style.label.color)},
                          {"text-halo-color", ColorText(style.label.halo_color)},
                          {"text-halo-width", style.label.halo_px}};
        layers.push_back(l);
      }
    }

    json root;
    root["version"] = 8;
    root["metadata"] = meta;
    root["sources"] = json{{"features", json{{"type", "geojson"},
                                             {"data", json{{"type", "FeatureCollection"},
                                                           {"features", json::array()}}}}}};
    root["layers"] = layers;
    *out = root.dump(1, '\t');
  } catch (const std::exception& e) {
    return Status::Error(kInternal, std::string("feature style: ") + e.what());
  }
  return Status::Ok();
}

Status ReadFeatureStyleJson(const std::string& text, FeatureStyle* out) {
  if (out == nullptr) return Bad("null out");
  try {
    json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) return Bad("not a JSON object");
    if (Status s = RestoreExported(&root); !s.ok()) return s;
    FeatureStyle style;
    const json meta = root.value("metadata", json::object());
    if (!meta.is_object()) return Bad("metadata is not an object");
    if (!FeatureRendererFromString(meta.value("fv:renderer", "single"), &style.renderer))
      return Bad("unknown renderer \"" + meta.value("fv:renderer", "") + "\"");
    if (style.renderer == FeatureRenderer::kRules || IsPlainStyle(root, meta)) {
      Status s = ReadRules(root, meta, &style);
      if (!s.ok()) return s;
      *out = std::move(style);
      return Status::Ok();
    }
    style.field = meta.value("fv:field", "");
    if (style.renderer != FeatureRenderer::kSingle && style.field.empty())
      return Bad(std::string("a ") + ToString(style.renderer) + " style needs a field");
    for (const json& c : meta.value("fv:categories", json::array())) {
      if (!c.is_object() || !c.contains("value") || !c["value"].is_string())
        return Bad("a category needs a text value");
      FeatureCategory cat;
      cat.value = c["value"].get<std::string>();
      cat.label = c.value("label", "");
      style.categories.push_back(std::move(cat));
    }
    for (const json& c : meta.value("fv:classes", json::array())) {
      if (!c.is_object() || !c.contains("lower") || !c["lower"].is_number())
        return Bad("a class needs a numeric lower bound");
      FeatureClass cls;
      cls.lower = c["lower"].get<double>();
      cls.label = c.value("label", "");
      style.classes.push_back(std::move(cls));
    }
    for (const json& id : meta.value("fv:overrides", json::array())) {
      if (!id.is_number_integer()) return Bad("an override id is not an integer");
      style.overrides[id.get<int64_t>()] = FeatureSymbol{};
    }

    for (const SymbolProp& p : Props()) {
      const json* layer = FindLayer(root, p.layer);
      const json* expr = layer ? FindProperty(*layer, p.name) : nullptr;
      if (expr == nullptr) continue;
      StyleExpression parsed;
      if (Status s = StyleExpression::Parse(expr->dump(), &parsed); !s.ok())
        return Bad(std::string(p.layer) + " " + p.name + ": " + s.message);
      // A feature symbol has one value per class; it cannot vary with zoom.
      if (parsed.reads_zoom())
        return Bad(std::string(p.layer) + " " + p.name + " varies with zoom");
      auto apply = [&](const Context& ctx, FeatureSymbol* sym) -> Status {
        json v;
        Status s = Evaluate(parsed, style.field, ctx, &v);
        if (!s.ok()) return s;
        if (!p.set(v, sym)) return Bad(std::string(p.layer) + " " + p.name + " has a bad value " + v.dump());
        return Status::Ok();
      };
      Status s = apply(Context{}, &style.symbol);
      for (FeatureCategory& c : style.categories)
        if (s.ok()) s = apply(Context{std::nullopt, c.value, std::nullopt}, &c.symbol);
      for (FeatureClass& c : style.classes)
        if (s.ok()) s = apply(Context{std::nullopt, std::nullopt, c.lower}, &c.symbol);
      for (auto& [id, sym] : style.overrides)
        if (s.ok()) s = apply(Context{id, std::nullopt, std::nullopt}, &sym);
      if (!s.ok()) return s;
    }

    // Back from the casing layer's whole width to the width past the line.
    std::vector<FeatureSymbol*> syms{&style.symbol};
    for (FeatureCategory& c : style.categories) syms.push_back(&c.symbol);
    for (FeatureClass& c : style.classes) syms.push_back(&c.symbol);
    for (auto& entry : style.overrides) syms.push_back(&entry.second);
    for (FeatureSymbol* sym : syms)
      sym->casing_px = sym->casing_px > 0 ? std::max(0.0, (sym->casing_px - sym->line_width_px) / 2) : 0.0;

    if (const json* label = FindLayer(root, "fv-label")) {
      const json* field = FindProperty(*label, "text-field");
      if (field != nullptr) {
        if (!IsGet(*field)) return Bad("text-field must be [\"get\", field]");
        style.label.field = (*field)[1].get<std::string>();
      }
      const json* v = FindProperty(*label, "text-size");
      if (v != nullptr && !NumberFrom(*v, &style.label.size_px)) return Bad("text-size is not a number");
      v = FindProperty(*label, "text-color");
      if (v != nullptr && !ColorFrom(*v, &style.label.color)) return Bad("text-color is not a colour");
      v = FindProperty(*label, "text-halo-color");
      if (v != nullptr && !ColorFrom(*v, &style.label.halo_color)) return Bad("text-halo-color is not a colour");
      v = FindProperty(*label, "text-halo-width");
      if (v != nullptr && !NumberFrom(*v, &style.label.halo_px)) return Bad("text-halo-width is not a number");
    }
    *out = std::move(style);
  } catch (const std::exception& e) {
    return Bad(e.what());
  }
  return Status::Ok();
}

/// Whether generated layer `id` can draw a feature of `geometry`.
bool DrawsGeometry(const std::string& id, GeometryType geometry) {
  switch (geometry) {
    case GeometryType::kPoint:
    case GeometryType::kMultiPoint:
      return id == "fv-circle" || id == "fv-label";
    case GeometryType::kLineString:
    case GeometryType::kMultiLineString:
      return id == "fv-line-casing" || id == "fv-line" || id == "fv-label-shapes";
    case GeometryType::kPolygon:
    case GeometryType::kMultiPolygon:
      return id == "fv-fill" || id == "fv-area-outline" || id == "fv-label-shapes";
    default:
      return true;
  }
}

Status FeatureStyleToRules(const FeatureStyle& style, FeatureStyle* out, GeometryType geometry) {
  if (out == nullptr) return Bad("null out");
  if (style.renderer == FeatureRenderer::kRules) {
    *out = style;
    return Status::Ok();
  }
  FeatureStyle generated = style;
  generated.overrides.clear();
  std::string text;
  Status s = WriteFeatureStyleJson(generated, &text);
  if (!s.ok()) return s;
  try {
    const json root = json::parse(text);
    FeatureStyle rules;
    rules.renderer = FeatureRenderer::kRules;
    rules.overrides = style.overrides;
    rules.label.field = style.label.field;
    for (const json& l : root["layers"]) {
      if (!DrawsGeometry(l.value("id", std::string()), geometry)) continue;
      StyleRule rule;
      s = LayerRule(l, &rule);
      if (!s.ok()) return s;
      if (rule.type != "symbol") rule.facet = rule.id;
      rule.id = GeneratedRuleName(rule.id);
      rules.rules.push_back(std::move(rule));
    }
    *out = std::move(rules);
  } catch (const std::exception& e) {
    return Status::Error(kInternal, std::string("feature style: ") + e.what());
  }
  return Status::Ok();
}

Status CategoriesToRules(const FeatureStyle& style, FeatureStyle* out, GeometryType geometry) {
  if (out == nullptr) return Bad("null out");
  if (style.renderer != FeatureRenderer::kCategorised) return Bad("only a categorised style splits into rules");
  FeatureStyle base;
  Status s = FeatureStyleToRules(style, &base, geometry);
  if (!s.ok()) return s;

  // The generated rules of one symbol, by facet, for a category's or Other's paint.
  auto rules_of = [&](const FeatureSymbol& symbol, std::map<std::string, StyleRule>* by_facet) {
    FeatureStyle single;
    single.symbol = symbol;
    single.label = style.label;
    FeatureStyle rules;
    Status st = FeatureStyleToRules(single, &rules, geometry);
    if (!st.ok()) return st;
    for (StyleRule& r : rules.rules)
      if (!r.facet.empty()) (*by_facet)[r.facet] = std::move(r);
    return Status::Ok();
  };
  struct Value {
    std::string key, title;
    std::map<std::string, StyleRule> rules;
  };
  std::vector<Value> values;
  std::set<std::string> keys;
  for (const FeatureCategory& c : style.categories) {
    if (!keys.insert(c.value).second) continue;  // the first category with a key wins, as in SymbolFor
    Value v{c.value, !c.label.empty() ? c.label : c.value.empty() ? "(empty)" : c.value, {}};
    s = rules_of(c.symbol, &v.rules);
    if (!s.ok()) return s;
    values.push_back(std::move(v));
  }
  std::map<std::string, StyleRule> other;
  s = rules_of(style.symbol, &other);
  if (!s.ok()) return s;

  const json key = json::array({"to-string", json::array({"get", style.field})});
  json all_keys = json::array();
  for (const Value& v : values) all_keys.push_back(v.key);
  auto with_guard = [](const std::string& guard, json term) {
    if (guard.empty()) return term.dump();
    return json::array({"all", json::parse(guard), std::move(term)}).dump();
  };

  FeatureStyle split = base;
  split.rules.clear();
  std::set<std::string> ids;
  for (const StyleRule& r : base.rules) ids.insert(r.id);
  auto unique = [&ids](const std::string& want) {
    std::string id = want;
    for (int n = 2; !ids.insert(id).second; ++n) id = want + " (" + std::to_string(n) + ")";
    return id;
  };
  for (const StyleRule& r : base.rules) {
    const auto o = other.find(r.facet);
    const bool varies = o != other.end() && !values.empty() &&
                        std::any_of(values.begin(), values.end(), [&](const Value& v) {
                          const auto it = v.rules.find(r.facet);
                          return it != v.rules.end() && it->second.paint != o->second.paint;
                        });
    if (!varies) {
      split.rules.push_back(r);
      continue;
    }
    // Other first, then the categories last to first, so the first category is drawn on top.
    StyleRule rest = r;
    rest.paint = o->second.paint;
    rest.id = unique(r.id + ": Other");
    rest.legend = "Other";
    rest.filter = with_guard(r.filter, json::array({"match", key, all_keys, false, true}));
    split.rules.push_back(std::move(rest));
    for (auto v = values.rbegin(); v != values.rend(); ++v) {
      StyleRule one = r;
      one.paint = v->rules.at(r.facet).paint;
      one.id = unique(r.id + ": " + v->title);
      one.legend = v->title;
      one.filter = with_guard(r.filter, json::array({"==", key, v->key}));
      split.rules.push_back(std::move(one));
    }
  }
  *out = std::move(split);
  return Status::Ok();
}

std::vector<RuleLegendEntry> RuleLegend(const FeatureStyle& style) {
  std::vector<RuleLegendEntry> out;
  if (style.renderer != FeatureRenderer::kRules) return out;
  std::map<std::string, size_t> by_title;
  for (size_t i = style.rules.size(); i-- > 0;) {
    const StyleRule& r = style.rules[i];
    if (!r.shown) continue;
    if (r.legend.empty()) {
      out.push_back({r.id, {i}});
      continue;
    }
    const auto it = by_title.find(r.legend);
    if (it != by_title.end()) {
      std::vector<size_t>& rules = out[it->second].rules;
      rules.insert(rules.begin(), i);
      continue;
    }
    by_title[r.legend] = out.size();
    out.push_back({r.legend, {i}});
  }
  return out;
}

namespace {

/// Adds to `out` the field names `j` reads through ["get", name] or ["has", name].
void ExpressionFields(const json& j, std::set<std::string>* out) {
  if (!j.is_array()) return;
  if (j.size() >= 2 && j[0].is_string() && (j[0] == "get" || j[0] == "has") && j[1].is_string())
    out->insert(j[1].get<std::string>());
  for (const json& e : j) ExpressionFields(e, out);
}

/// Adds to `out` the field names a `{token}` template string reads.
void TemplateFields(const json& j, std::set<std::string>* out) {
  if (!j.is_string()) return;
  const std::string s = j.get<std::string>();
  for (size_t open = s.find('{'); open != std::string::npos; open = s.find('{', open + 1)) {
    const size_t close = s.find('}', open + 1);
    if (close == std::string::npos) break;
    out->insert(s.substr(open + 1, close - open - 1));
  }
}

/// The fields a rule reads in its filter, paint, layout and extra members.
std::set<std::string> RuleFields(const StyleRule& rule) {
  std::set<std::string> fields;
  for (const std::string* text : {&rule.filter, &rule.paint, &rule.layout, &rule.extra}) {
    if (text->empty()) continue;
    const json j = json::parse(*text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) continue;
    if (j.is_object()) {
      for (auto it = j.begin(); it != j.end(); ++it) {
        ExpressionFields(it.value(), &fields);
        if (it.key() == "text-field" || it.key() == "icon-image" || it.key() == "fill-pattern")
          TemplateFields(it.value(), &fields);
      }
    } else {
      ExpressionFields(j, &fields);
    }
  }
  return fields;
}

/// `"a"`, `"a" and "b"`, `"a", "b", "c" and 2 more`.
std::string QuotedList(const std::vector<std::string>& names) {
  const size_t shown = std::min<size_t>(names.size(), 3);
  std::string text;
  for (size_t i = 0; i < shown; ++i) {
    if (i > 0) text += i + 1 == shown && names.size() == shown ? " and " : ", ";
    text += "\"" + names[i] + "\"";
  }
  if (names.size() > shown) text += " and " + std::to_string(names.size() - shown) + " more";
  return text;
}

}  // namespace

std::vector<std::string> StyleBreaksFromSchemaChange(const FeatureStyle& style, const FeatureSchema& old,
                                                     const SchemaChange& change) {
  // Each old field's fate: its new definition, or none when deleted.
  std::map<std::string, const FieldDef*> fate;
  for (size_t j = 0; j < old.fields.size(); ++j) fate[old.fields[j].name] = nullptr;
  for (size_t i = 0; i < change.source.size() && i < change.schema.fields.size(); ++i) {
    const int src = change.source[i];
    if (src >= 0 && static_cast<size_t>(src) < old.fields.size())
      fate[old.fields[static_cast<size_t>(src)].name] = &change.schema.fields[i];
  }
  auto what_happens = [&fate](const std::string& name) -> std::string {
    const auto it = fate.find(name);
    if (it == fate.end()) return std::string();
    if (it->second == nullptr) return "is deleted";
    if (it->second->name != name) return "is renamed to \"" + it->second->name + "\"";
    return std::string();
  };

  std::vector<std::string> out;
  if (style.renderer == FeatureRenderer::kRules) {
    std::map<std::string, std::vector<std::string>> rules_by_field;  // old field, rule ids
    for (const StyleRule& rule : style.rules)
      for (const std::string& field : RuleFields(rule))
        if (!what_happens(field).empty()) rules_by_field[field].push_back(rule.id);
    for (const auto& [field, rules] : rules_by_field) {
      const bool one = rules.size() == 1;
      out.push_back(std::string(one ? "Style rule " : "Style rules ") + QuotedList(rules) +
                    (one ? " uses" : " use") + " the field \"" + field + "\", which " + what_happens(field) +
                    (one ? "; the rule will not find it until it is edited." : "; the rules will not find it until they are edited."));
    }
    return out;
  }
  if (style.renderer != FeatureRenderer::kSingle && !style.field.empty()) {
    const auto it = fate.find(style.field);
    const FieldDef* def = it == fate.end() ? nullptr : it->second;
    const bool numeric = def != nullptr && (def->type == FieldType::kInteger || def->type == FieldType::kReal);
    const bool usable = def != nullptr && def->type != FieldType::kBlob &&
                        (style.renderer != FeatureRenderer::kGraduated || numeric);
    if (!usable)
      out.push_back(std::string("The ") + ToString(style.renderer) + " style is based on \"" + style.field +
                    "\", which " + (def == nullptr ? "is deleted" : def->type == FieldType::kBlob ? "becomes a blob" : "is no longer a number") +
                    "; the layer will draw with a single symbol.");
  }
  if (!style.label.field.empty()) {
    const auto it = fate.find(style.label.field);
    if (it != fate.end() && (it->second == nullptr || it->second->type == FieldType::kBlob))
      out.push_back("The labels show \"" + style.label.field + "\", which " +
                    (it->second == nullptr ? "is deleted" : "becomes a blob") + "; the layer will lose its labels.");
  }
  return out;
}

Status ExportFeatureStyleJson(const FeatureStyle& style, std::string* out) {
  if (out == nullptr) return Bad("null out");
  FeatureStyle plain = style;
  plain.overrides.clear();
  std::string text;
  Status s = WriteFeatureStyleJson(plain, &text);
  if (!s.ok()) return s;
  try {
    json root = json::parse(text);
    bool draws_text = false;
    for (json& l : root["layers"]) {
      draws_text = draws_text || l.value("layout", json::object()).contains("text-field");
      json meta = l.value("metadata", json::object());
      for (const char* section : {"paint", "layout"}) {
        const auto sec = l.find(section);
        if (sec == l.end() || !sec->is_object()) continue;
        json moved = json::object();
        for (auto it = sec->begin(); it != sec->end(); ++it)
          if (it.key().rfind("fv-", 0) == 0) moved[it.key()] = it.value();
        if (moved.empty()) continue;
        for (auto it = moved.begin(); it != moved.end(); ++it) sec->erase(it.key());
        if (sec->empty()) l.erase(section);
        meta[std::string("fv:") + section] = std::move(moved);
      }
      if (!meta.empty()) l["metadata"] = std::move(meta);
    }
    // MapLibre refuses a text layer without a glyph source; the layer itself draws with its own font.
    if (draws_text) root["glyphs"] = kExportGlyphs;
    *out = root.dump(1, '\t');
  } catch (const std::exception& e) {
    return Status::Error(kInternal, std::string("feature style: ") + e.what());
  }
  return Status::Ok();
}

Status ImportFeatureStyleJson(const std::string& text, FeatureStyle* out) {
  const json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) return Bad("not a JSON object");
  const auto version = root.find("version");
  const auto layers = root.find("layers");
  if (version == root.end() || *version != 8 || layers == root.end() || !layers->is_array())
    return Bad("not a MapLibre style (version 8 with a layers array)");
  return ReadFeatureStyleJson(text, out);
}

Status WriteFeatureLayerInfoJson(const FeatureLayerInfo& info, std::string* style_json) {
  if (style_json == nullptr) return Bad("null out");
  try {
    json root = json::parse(*style_json, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) return Bad("not a JSON object");
    json meta = root.value("metadata", json::object());
    if (!meta.is_object()) return Bad("metadata is not an object");
    root.erase("name");
    for (const char* key : {"fv:description", "fv:credits", "fv:min-scale", "fv:max-scale", "fv:map-tip-field",
                            "fv:hyperlink-field", "fv:selection-color", "fv:selection-width", "fv:definition-query"})
      meta.erase(key);
    if (!info.name.empty()) root["name"] = info.name;
    if (!info.description.empty()) meta["fv:description"] = info.description;
    if (!info.credits.empty()) meta["fv:credits"] = info.credits;
    if (info.min_scale > 0) meta["fv:min-scale"] = info.min_scale;
    if (info.max_scale > 0) meta["fv:max-scale"] = info.max_scale;
    if (!info.map_tip_field.empty()) meta["fv:map-tip-field"] = info.map_tip_field;
    if (!info.hyperlink_field.empty()) meta["fv:hyperlink-field"] = info.hyperlink_field;
    if (info.has_selection_style) {
      meta["fv:selection-color"] = ColorText(info.selection.color);
      meta["fv:selection-width"] = info.selection.width_px;
    }
    if (!info.definition_query.empty()) {
      json query = json::parse(info.definition_query, nullptr, /*allow_exceptions=*/false);
      if (query.is_discarded()) return Bad("the definition query is not JSON");
      meta["fv:definition-query"] = query;
    }
    root["metadata"] = meta;
    *style_json = root.dump(1, '\t');
  } catch (const std::exception& e) {
    return Status::Error(kInternal, std::string("feature layer info: ") + e.what());
  }
  return Status::Ok();
}

Status ReadFeatureLayerInfoJson(const std::string& style_json, FeatureLayerInfo* out) {
  if (out == nullptr) return Bad("null out");
  try {
    const json root = json::parse(style_json, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) return Bad("not a JSON object");
    const json meta = root.value("metadata", json::object());
    if (!meta.is_object()) return Bad("metadata is not an object");
    auto text = [](const json& j, const char* key) {
      const auto it = j.find(key);
      return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    auto number = [](const json& j, const char* key) {
      const auto it = j.find(key);
      return it != j.end() && it->is_number() ? it->get<double>() : 0.0;
    };
    FeatureLayerInfo info;
    info.name = text(root, "name");
    info.description = text(meta, "fv:description");
    info.credits = text(meta, "fv:credits");
    info.min_scale = number(meta, "fv:min-scale");
    info.max_scale = number(meta, "fv:max-scale");
    info.map_tip_field = text(meta, "fv:map-tip-field");
    info.hyperlink_field = text(meta, "fv:hyperlink-field");
    const auto color = meta.find("fv:selection-color");
    if (color != meta.end() && ColorFrom(*color, &info.selection.color)) {
      info.has_selection_style = true;
      const double width = number(meta, "fv:selection-width");
      if (width > 0) info.selection.width_px = width;
    }
    const auto query = meta.find("fv:definition-query");
    if (query != meta.end() && !query->is_null()) info.definition_query = query->dump();
    *out = std::move(info);
  } catch (const std::exception& e) {
    return Status::Error(kInternal, std::string("feature layer info: ") + e.what());
  }
  return Status::Ok();
}

}  // namespace fv
