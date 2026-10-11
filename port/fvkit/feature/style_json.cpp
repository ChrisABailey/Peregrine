// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The MapLibre-subset JSON form of `FeatureStyle` (feature-overlay-plan §3).
/// The root `metadata` names the renderer and its classes; each paint value
/// is a constant or an expression that the reader evaluates once per class
/// through the shared `StyleExpression` evaluator.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <set>

#include <nlohmann/json.hpp>

#include "fvkit/app/properties.h"
#include "fvkit/feature/style.h"
#include "fvkit/feature/vector_source.h"
#include "fvkit/style/expression.h"

namespace fv {

namespace {

using json = nlohmann::ordered_json;

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

}  // namespace

Status WriteFeatureStyleJson(const FeatureStyle& style, std::string* out, int64_t highlight_id) {
  std::vector<int64_t> ids;
  if (highlight_id != 0) ids.push_back(highlight_id);
  return WriteFeatureStyleJson(style, out, ids);
}

Status WriteFeatureStyleJson(const FeatureStyle& style, std::string* out,
                             const std::vector<int64_t>& highlight_ids) {
  if (out == nullptr) return Bad("null out");
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
      // The selection halo: each facet widened, in yellow, under everything.
      const char* kHalo = "#ffc800";
      struct HaloDef {
        const char* id;
        const char* type;
        const char* geometry;
        const char* prop;
        std::function<json(const FeatureSymbol&)> get;
      };
      const HaloDef halos[] = {
          {"fv-halo-area", "line", "Polygon", "line-width",
           [](const FeatureSymbol& s) { return json(s.area_outline_px + 4); }},
          {"fv-halo-line", "line", "LineString", "line-width",
           [](const FeatureSymbol& s) { return json(s.line_width_px + 6); }},
          {"fv-halo-circle", "circle", "Point", "circle-radius",
           [](const FeatureSymbol& s) { return json(s.marker_size_px / 2 + 4); }},
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
                         ? json{{"circle-color", kHalo}, {"circle-radius", v}}
                         : json{{"line-color", kHalo}, {"line-width", v}};
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
    const json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) return Bad("not a JSON object");
    FeatureStyle style;
    const json meta = root.value("metadata", json::object());
    if (!meta.is_object()) return Bad("metadata is not an object");
    if (!FeatureRendererFromString(meta.value("fv:renderer", "single"), &style.renderer))
      return Bad("unknown renderer \"" + meta.value("fv:renderer", "") + "\"");
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

}  // namespace fv
