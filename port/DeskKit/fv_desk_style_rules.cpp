// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_style_rules.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "fvkit/overlay/feature_overlay.h"

namespace fv {
namespace desk {

namespace {

using json = nlohmann::json;

Status Bad(const std::string& what) { return Status::Error(kInvalidArg, what); }

/// The paint property holding a type's main colour.
const char* ColorKey(const std::string& type) {
  if (type == "fill") return "fill-color";
  if (type == "line") return "line-color";
  if (type == "circle") return "circle-color";
  if (type == "symbol") return "text-color";
  return nullptr;
}

/// A new rule's paint and layout: the generated renderer's default symbol
/// and label colours. A symbol rule labels by `label_field` when there is one.
void DefaultStyle(const std::string& type, const std::string& label_field, std::string* paint, std::string* layout) {
  json p = json::object();
  json l = json::object();
  if (type == "fill") {
    p = {{"fill-color", "#1e5ac8"}, {"fill-opacity", 0.27}, {"fill-outline-color", "#1e5ac8"}};
  } else if (type == "line") {
    p = {{"line-color", "#1e5ac8"}, {"line-width", 2}};
  } else if (type == "circle") {
    p = {{"circle-radius", 4.5}, {"circle-color", "#dc3c28"}, {"circle-stroke-color", "#ffffff"},
         {"circle-stroke-width", 1}};
  } else if (type == "symbol") {
    p = {{"text-color", "#141414"}, {"text-halo-color", "#ffffff"}, {"text-halo-width", 1}};
    if (!label_field.empty()) l = {{"text-field", json::array({"get", label_field})}, {"text-size", 12}};
  }
  *paint = p.dump();
  *layout = l.dump();
}

/// Turns a circle rule into a symbol rule drawing the matching Maki shape in
/// the circle's colour and opacity at about the circle's size, with no label.
void CircleToIcon(StyleRule* rule) {
  const json old = json::parse(rule->paint, nullptr, /*allow_exceptions=*/false);
  const auto member = [&old](const char* key) { return old.is_object() && old.contains(key) ? old[key] : json(); };
  const bool square = member("fv-circle-shape") == "square";
  // The engine's default radius stands in for one that varies.
  const json radius = member("circle-radius");
  const double r = radius.is_number() ? radius.get<double>() : 5.0;
  // At icon-size 1 the Maki circle is 14 px across and the square 12 px.
  const double size = std::round(2 * r / (square ? 12.0 : 14.0) * 100) / 100;
  json paint = {{"icon-color", member("circle-color").is_null() ? json("#000000") : member("circle-color")}};
  if (!member("circle-opacity").is_null()) paint["icon-opacity"] = member("circle-opacity");
  const json layout = {{"icon-image", square ? "square" : "circle"}, {"icon-size", size}};
  rule->paint = paint.dump();
  rule->layout = layout.dump();
  rule->type = "symbol";
  rule->facet.clear();
}

bool IsRuleType(const std::string& type) {
  const std::vector<std::string>& types = RuleTypes();
  return std::find(types.begin(), types.end(), type) != types.end();
}

/// The list index of row `row` (rows are top-drawn first, the list first-drawn first).
size_t IndexOf(const FeatureStyle& style, size_t row) { return style.rules.size() - 1 - row; }

bool Taken(const FeatureStyle& style, const std::string& id, size_t except = SIZE_MAX) {
  for (size_t i = 0; i < style.rules.size(); ++i)
    if (i != except && style.rules[i].id == id) return true;
  return false;
}

/// `base`, or `base N` for the first N from 2 that no rule is named yet.
std::string UniqueId(const FeatureStyle& style, const std::string& base) {
  std::string id = base;
  for (int n = 2; Taken(style, id); ++n) id = base + " " + std::to_string(n);
  return id;
}

bool TestsGeometry(const json& term) {
  if (!term.is_array()) return false;
  if (term.size() == 1 && term[0] == "geometry-type") return true;
  for (const json& t : term)
    if (TestsGeometry(t)) return true;
  return false;
}

}  // namespace

GeometryType LayerGeometry(const FeatureOverlay& overlay) {
  return overlay.layer() ? overlay.layer()->Schema().geometry_type : GeometryType::kGeometry;
}

void SplitRuleFilter(const std::string& filter, std::string* guard, std::string* rest) {
  guard->clear();
  *rest = filter;
  const json f = json::parse(filter, nullptr, /*allow_exceptions=*/false);
  if (f.is_discarded() || !f.is_array() || !TestsGeometry(f)) return;
  if (f.empty() || f[0] != "all") {
    *guard = f.dump();
    rest->clear();
    return;
  }
  json guards = json::array({"all"});
  json others = json::array({"all"});
  for (size_t i = 1; i < f.size(); ++i) (TestsGeometry(f[i]) ? guards : others).push_back(f[i]);
  // One term stands alone; several stay wrapped in `all`.
  auto text = [](const json& all) {
    if (all.size() == 1) return std::string();
    return all.size() == 2 ? all[1].dump() : all.dump();
  };
  *guard = text(guards);
  *rest = text(others);
}

std::string JoinRuleFilter(const std::string& guard, const std::string& rest) {
  if (guard.empty()) return rest;
  if (rest.empty()) return guard;
  const json g = json::parse(guard, nullptr, /*allow_exceptions=*/false);
  const json r = json::parse(rest, nullptr, /*allow_exceptions=*/false);
  if (g.is_discarded() || r.is_discarded()) return rest;
  json all = json::array({"all"});
  // A guard of several terms was written as one `all`; its terms join the outer one.
  if (g.is_array() && !g.empty() && g[0] == "all") {
    for (size_t i = 1; i < g.size(); ++i) all.push_back(g[i]);
  } else {
    all.push_back(g);
  }
  all.push_back(r);
  return all.dump();
}

const std::vector<std::string>& RuleTypes() {
  static const std::vector<std::string> kTypes = {"fill", "line", "circle", "symbol"};
  return kTypes;
}

std::vector<RuleRow> StyleRulesModel::Rows(const FeatureOverlay& overlay) {
  std::vector<RuleRow> rows;
  FeatureStyle rules;
  if (!overlay.layer() || !FeatureStyleToRules(overlay.style(), &rules, LayerGeometry(overlay)).ok()) return rows;
  for (auto it = rules.rules.rbegin(); it != rules.rules.rend(); ++it) {
    RuleRow r;
    r.id = it->id;
    r.type = it->type;
    r.filter = it->filter;
    r.legend = it->legend;
    r.shown = it->shown;
    rows.push_back(std::move(r));
  }
  return rows;
}

Status StyleRulesModel::RulesOf(const FeatureOverlay& overlay, size_t row, FeatureStyle* out) {
  if (!overlay.layer()) return Status::Error(kNotFound, "\"" + overlay.Name() + "\" has no features");
  Status s = FeatureStyleToRules(overlay.style(), out, LayerGeometry(overlay));
  if (!s.ok()) return s;
  if (row != SIZE_MAX && row >= out->rules.size()) return Bad("no rule row " + std::to_string(row));
  return Status::Ok();
}

Status StyleRulesModel::Replace(const std::shared_ptr<FeatureOverlay>& overlay, const FeatureStyle& style,
                                const std::string& label) {
  if (!overlay || !overlay->layer()) return Status::Error(kNotFound, "the layer has no features to style");
  return Commit(overlay, style, label);
}

Status StyleRulesModel::Commit(const std::shared_ptr<FeatureOverlay>& overlay, FeatureStyle after,
                               const std::string& label) {
  // Reading the style back runs the engine's checks, which writing does not.
  std::string text;
  FeatureStyle checked;
  Status s = WriteFeatureStyleJson(after, &text);
  if (s.ok()) s = ReadFeatureStyleJson(text, &checked);
  if (!s.ok()) return Status::Error(kInvalidArg, s.message);
  const FeatureStyle before = overlay->style();
  if (after == before) return Status::Ok();
  FeatureStyle unchanged;
  if (FeatureStyleToRules(before, &unchanged, LayerGeometry(*overlay)).ok() && after == unchanged) return Status::Ok();

  const std::weak_ptr<FeatureOverlay> weak = overlay;
  const std::function<void(FeatureOverlay&)> changed = changed_;
  auto apply = [weak, changed](const FeatureStyle& style) {
    const std::shared_ptr<FeatureOverlay> o = weak.lock();
    if (!o) return Status::Error(kNotFound, "the layer has closed");
    o->SetStyle(style);
    if (app::Persistence* p = o->AsPersistence()) p->set_dirty(true);
    if (changed) changed(*o);
    return Status::Ok();
  };
  s = apply(after);
  if (!s.ok()) return s;
  app::UndoEntry e;
  e.label = label;
  e.owner = static_cast<const Overlay*>(overlay.get());
  e.undo = [apply, before] { return apply(before); };
  e.redo = [apply, after] { return apply(after); };
  undo_.Push(std::move(e));
  return Status::Ok();
}

Status StyleRulesModel::Add(const std::shared_ptr<FeatureOverlay>& overlay, const std::string& id,
                            const std::string& type) {
  if (!overlay) return Status::Error(kNotFound, "no layer");
  FeatureStyle style;
  Status s = RulesOf(*overlay, SIZE_MAX, &style);
  if (!s.ok()) return s;
  if (id.empty()) return Bad("a rule needs a name");
  if (Taken(style, id)) return Bad("a rule named \"" + id + "\" already exists");
  if (!IsRuleType(type)) return Bad("no rule type \"" + type + "\"");
  StyleRule rule;
  rule.id = id;
  rule.type = type;
  DefaultStyle(type, style.label.field, &rule.paint, &rule.layout);
  style.rules.push_back(std::move(rule));
  return Commit(overlay, std::move(style), "Add Rule");
}

Status StyleRulesModel::Edit(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, const std::string& label,
                             const std::function<Status(FeatureStyle& style, size_t at)>& change) {
  if (!overlay) return Status::Error(kNotFound, "no layer");
  FeatureStyle style;
  Status s = RulesOf(*overlay, row, &style);
  if (s.ok()) s = change(style, IndexOf(style, row));
  return s.ok() ? Commit(overlay, std::move(style), label) : s;
}

Status StyleRulesModel::Duplicate(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, std::string* copy_id) {
  return Edit(overlay, row, "Duplicate Rule", [copy_id](FeatureStyle& style, size_t at) {
    StyleRule copy = style.rules[at];
    copy.id = UniqueId(style, copy.id + " copy");
    if (copy_id != nullptr) *copy_id = copy.id;
    style.rules.insert(style.rules.begin() + static_cast<std::ptrdiff_t>(at + 1), std::move(copy));
    return Status::Ok();
  });
}

Status StyleRulesModel::Rename(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, const std::string& id) {
  return Edit(overlay, row, "Rename Rule", [&id](FeatureStyle& style, size_t at) {
    if (id.empty()) return Bad("a rule needs a name");
    if (Taken(style, id, at)) return Bad("a rule named \"" + id + "\" already exists");
    style.rules[at].id = id;
    return Status::Ok();
  });
}

Status StyleRulesModel::SetType(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, const std::string& type) {
  return Edit(overlay, row, "Change Rule Type", [&type](FeatureStyle& style, size_t at) {
    if (!IsRuleType(type)) return Bad("no rule type \"" + type + "\"");
    StyleRule& rule = style.rules[at];
    if (rule.type == type) return Status::Ok();
    // Paint and layout properties are named by type, so only the main colour carries across.
    json color;
    if (const char* from = ColorKey(rule.type)) {
      const json old = json::parse(rule.paint, nullptr, /*allow_exceptions=*/false);
      if (old.is_object() && old.contains(from)) color = old[from];
    }
    if (rule.type == "circle" && type == "symbol") {
      CircleToIcon(&rule);
      return Status::Ok();
    }
    DefaultStyle(type, style.label.field, &rule.paint, &rule.layout);
    if (!color.is_null()) {
      json paint = json::parse(rule.paint);
      paint[ColorKey(type)] = color;
      rule.paint = paint.dump();
    }
    rule.type = type;
    rule.facet.clear();
    return Status::Ok();
  });
}

Status StyleRulesModel::SetFilter(const std::shared_ptr<FeatureOverlay>& overlay, size_t row,
                                  const std::string& filter) {
  return Edit(overlay, row, "Edit Rule Filter", [&filter](FeatureStyle& style, size_t at) {
    style.rules[at].filter = filter;
    return Status::Ok();
  });
}

Status StyleRulesModel::SetShown(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, bool shown) {
  return Edit(overlay, row, shown ? "Show Rule" : "Hide Rule", [shown](FeatureStyle& style, size_t at) {
    style.rules[at].shown = shown;
    return Status::Ok();
  });
}

Status StyleRulesModel::SetStyle(const std::shared_ptr<FeatureOverlay>& overlay, size_t row,
                                 const StyleRule& from) {
  return Edit(overlay, row, "Edit Rule Style", [&from](FeatureStyle& style, size_t at) {
    StyleRule& rule = style.rules[at];
    rule.paint = from.paint;
    rule.layout = from.layout;
    rule.legend = from.legend;
    rule.min_zoom = from.min_zoom;
    rule.max_zoom = from.max_zoom;
    return Status::Ok();
  });
}

Status StyleRulesModel::AddCasing(const std::shared_ptr<FeatureOverlay>& overlay, size_t row,
                                  std::string* casing_id) {
  return Edit(overlay, row, "Add Casing", [casing_id](FeatureStyle& style, size_t at) {
    StyleRule casing = style.rules[at];
    if (casing.type != "line") return Bad("only a line rule takes a casing");
    casing.id = UniqueId(style, casing.id + " casing");
    casing.legend.clear();
    casing.facet.clear();
    json paint = json::parse(casing.paint, nullptr, /*allow_exceptions=*/false);
    if (!paint.is_object()) paint = json::object();
    paint["line-color"] = "#505050";
    json& width = paint["line-width"];
    if (width.is_null()) {
      width = 3;  // the engine's default width of 1, plus 2
    } else if (width.is_number()) {
      width = width.get<double>() + 2;
    } else if (width.is_array() && width.size() >= 5 && width[0] == "interpolate") {
      for (size_t i = 4; i < width.size(); i += 2)
        if (width[i].is_number()) width[i] = width[i].get<double>() + 2;
    }
    // Any other width expression is copied unchanged; the casing is then edited by hand.
    casing.paint = paint.dump();
    if (casing_id != nullptr) *casing_id = casing.id;
    style.rules.insert(style.rules.begin() + static_cast<std::ptrdiff_t>(at), std::move(casing));
    return Status::Ok();
  });
}

Status StyleRulesModel::Move(const std::shared_ptr<FeatureOverlay>& overlay, size_t from, size_t to) {
  return Edit(overlay, from, "Move Rule", [from, to](FeatureStyle& style, size_t) {
    if (to >= style.rules.size()) return Bad("no rule row " + std::to_string(to));
    std::vector<StyleRule> rows(style.rules.rbegin(), style.rules.rend());
    StyleRule moved = std::move(rows[from]);
    rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(from));
    rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    style.rules.assign(rows.rbegin(), rows.rend());
    return Status::Ok();
  });
}

Status StyleRulesModel::Delete(const std::shared_ptr<FeatureOverlay>& overlay, size_t row) {
  return Edit(overlay, row, "Delete Rule", [](FeatureStyle& style, size_t at) {
    style.rules.erase(style.rules.begin() + static_cast<std::ptrdiff_t>(at));
    return Status::Ok();
  });
}

}  // namespace desk
}  // namespace fv
