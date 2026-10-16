// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_layer_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/overlay/feature_overlay.h"

namespace fv {
namespace desk {

namespace {

constexpr double kMaxPx = 64.0;

Status Bad(const std::string& why) { return Status::Error(kInvalidArg, why); }

std::vector<std::string> DashNames() {
  std::vector<std::string> names;
  for (const char* const* n = line_preset::kAll; *n != nullptr; ++n) names.push_back(*n);
  return names;
}

SymbolPropertyInfo Info(SymbolProperty id, SymbolPropertyKind kind, const char* title, const char* group) {
  SymbolPropertyInfo info;
  info.id = id;
  info.kind = kind;
  info.title = title;
  info.group = group;
  if (id == SymbolProperty::kMarkerShape) info.choices = {"Circle", "Square"};
  if (id == SymbolProperty::kLineDash) info.choices = DashNames();
  return info;
}

FvColor* ColorField(FeatureSymbol* s, SymbolProperty p) {
  switch (p) {
    case SymbolProperty::kMarkerFill: return &s->marker_fill;
    case SymbolProperty::kMarkerOutline: return &s->marker_outline;
    case SymbolProperty::kLineColor: return &s->line_color;
    case SymbolProperty::kCasingColor: return &s->casing_color;
    case SymbolProperty::kAreaFill: return &s->area_fill;
    case SymbolProperty::kAreaOutline: return &s->area_outline;
    default: return nullptr;
  }
}

double* NumberField(FeatureSymbol* s, SymbolProperty p) {
  switch (p) {
    case SymbolProperty::kMarkerSize: return &s->marker_size_px;
    case SymbolProperty::kMarkerOutlineWidth: return &s->marker_outline_px;
    case SymbolProperty::kLineWidth: return &s->line_width_px;
    case SymbolProperty::kCasingWidth: return &s->casing_px;
    case SymbolProperty::kAreaOutlineWidth: return &s->area_outline_px;
    default: return nullptr;
  }
}

std::string Number(double d) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.10g", d);
  return buf;
}

std::string Trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool IsPointType(GeometryType g) { return g == GeometryType::kPoint || g == GeometryType::kMultiPoint; }
bool IsLineType(GeometryType g) { return g == GeometryType::kLineString || g == GeometryType::kMultiLineString; }
bool IsAreaType(GeometryType g) { return g == GeometryType::kPolygon || g == GeometryType::kMultiPolygon; }

}  // namespace

std::vector<SymbolPropertyInfo> SymbolPropertiesFor(GeometryType geometry) {
  using K = SymbolPropertyKind;
  using P = SymbolProperty;
  const bool any = !IsPointType(geometry) && !IsLineType(geometry) && !IsAreaType(geometry);
  std::vector<SymbolPropertyInfo> out;
  if (any || IsPointType(geometry)) {
    out.push_back(Info(P::kMarkerShape, K::kChoice, "Shape", "Point"));
    out.push_back(Info(P::kMarkerSize, K::kNumber, "Size", "Point"));
    out.push_back(Info(P::kMarkerFill, K::kColor, "Fill", "Point"));
    out.push_back(Info(P::kMarkerOutline, K::kColor, "Outline", "Point"));
    out.push_back(Info(P::kMarkerOutlineWidth, K::kNumber, "Outline width", "Point"));
  }
  if (any || IsLineType(geometry)) {
    out.push_back(Info(P::kLineColor, K::kColor, "Colour", "Line"));
    out.push_back(Info(P::kLineWidth, K::kNumber, "Width", "Line"));
    out.push_back(Info(P::kLineDash, K::kChoice, "Pattern", "Line"));
    out.push_back(Info(P::kCasingColor, K::kColor, "Casing", "Line"));
    out.push_back(Info(P::kCasingWidth, K::kNumber, "Casing width", "Line"));
  }
  if (any || IsAreaType(geometry)) {
    out.push_back(Info(P::kAreaFill, K::kColor, "Fill", "Area"));
    out.push_back(Info(P::kAreaOutline, K::kColor, "Outline", "Area"));
    out.push_back(Info(P::kAreaOutlineWidth, K::kNumber, "Outline width", "Area"));
  }
  return out;
}

FvColor GetSymbolColor(const FeatureSymbol& s, SymbolProperty p) {
  const FvColor* c = ColorField(const_cast<FeatureSymbol*>(&s), p);
  return c ? *c : FvColor{};
}

double GetSymbolNumber(const FeatureSymbol& s, SymbolProperty p) {
  const double* d = NumberField(const_cast<FeatureSymbol*>(&s), p);
  return d ? *d : 0.0;
}

int GetSymbolChoice(const FeatureSymbol& s, SymbolProperty p) {
  if (p == SymbolProperty::kMarkerShape) return s.marker == FeatureMarker::kSquare ? 1 : 0;
  if (p == SymbolProperty::kLineDash) {
    const std::vector<std::string> names = DashNames();
    const auto it = std::find(names.begin(), names.end(), s.dash);
    return it == names.end() ? 0 : static_cast<int>(it - names.begin());
  }
  return 0;
}

Status SetSymbolColor(FeatureSymbol* s, SymbolProperty p, FvColor c) {
  FvColor* field = ColorField(s, p);
  if (field == nullptr) return Bad("not a colour property");
  *field = c;
  return Status::Ok();
}

Status SetSymbolNumber(FeatureSymbol* s, SymbolProperty p, double v) {
  double* field = NumberField(s, p);
  if (field == nullptr) return Bad("not a number property");
  if (!std::isfinite(v) || v < 0 || v > kMaxPx) return Bad("enter a size from 0 to 64 pixels");
  *field = v;
  return Status::Ok();
}

Status SetSymbolChoice(FeatureSymbol* s, SymbolProperty p, int choice) {
  if (p == SymbolProperty::kMarkerShape) {
    if (choice < 0 || choice > 1) return Bad("no marker shape " + std::to_string(choice));
    s->marker = choice == 1 ? FeatureMarker::kSquare : FeatureMarker::kCircle;
    return Status::Ok();
  }
  if (p == SymbolProperty::kLineDash) {
    const std::vector<std::string> names = DashNames();
    if (choice < 0 || choice >= static_cast<int>(names.size())) return Bad("no line pattern " + std::to_string(choice));
    s->dash = names[choice];
    return Status::Ok();
  }
  return Bad("not a choice property");
}

Status LayerStyleModel::Open(const std::shared_ptr<FeatureOverlay>& overlay, int64_t feature, std::string title,
                             std::shared_ptr<LayerStyleModel>* out) {
  if (out == nullptr) return Bad("LayerStyleModel::Open: null out");
  if (!overlay || !overlay->layer()) return Status::Error(kNotFound, "the overlay has no feature layer to style");
  std::shared_ptr<LayerStyleModel> m(new LayerStyleModel());
  m->overlay_ = overlay;
  m->overlay_id_ = overlay.get();
  m->title_ = std::move(title);
  m->feature_ = feature;
  m->geometry_ = overlay->layer()->Schema().geometry_type;
  if (feature != 0) {
    Status s = overlay->layer()->Get(feature, &m->feature_data_);
    if (!s.ok()) return s;
    // A layer of any geometry styles the feature for its own geometry.
    if (m->geometry_ == GeometryType::kGeometry) m->geometry_ = m->feature_data_.geometry.type;
  }
  m->draft_ = overlay->style();
  m->applied_ = m->draft_;
  *out = std::move(m);
  return Status::Ok();
}

const Overlay* LayerStyleModel::overlay() const { return overlay_id_; }

const FeatureSchema* LayerStyleModel::Schema() const {
  const auto o = overlay_.lock();
  return o && o->layer() ? &o->layer()->Schema() : nullptr;
}

Status LayerStyleModel::SetRenderer(FeatureRenderer renderer) {
  if (feature_ != 0) return Status::Error(kUnsupported, "a feature's own style has no renderer");
  if (renderer == draft_.renderer) return Status::Ok();
  if (renderer == FeatureRenderer::kRules) {
    FeatureStyle rules;
    Status s = FeatureStyleToRules(draft_, &rules, geometry_);
    if (s.ok()) draft_ = std::move(rules);
    return s;
  }
  const FeatureRenderer before = draft_.renderer;
  draft_.renderer = renderer;
  // A rules style has no symbol entries; leaving it starts the generator afresh.
  draft_.rules.clear();
  if (renderer == FeatureRenderer::kSingle) {
    draft_.field.clear();
    draft_.categories.clear();
    draft_.classes.clear();
    return Status::Ok();
  }
  const std::vector<StyleFieldChoice> fields = CandidateFields();
  if (fields.empty()) {
    draft_.renderer = before;
    return Status::Error(kNotFound, renderer == FeatureRenderer::kGraduated
                                        ? "the layer has no number field to grade by"
                                        : "the layer has no field to categorise by");
  }
  const bool usable = std::any_of(fields.begin(), fields.end(),
                                  [&](const StyleFieldChoice& f) { return f.name == draft_.field; });
  if (!usable) draft_.field = fields.front().name;
  draft_.categories.clear();
  draft_.classes.clear();
  return Classify();
}

Status LayerStyleModel::CategoriseIntoRules() {
  if (feature_ != 0 || draft_.renderer != FeatureRenderer::kCategorised)
    return Bad("only a categorised layer style splits into rules");
  FeatureStyle rules;
  Status s = CategoriesToRules(draft_, &rules, geometry_);
  if (s.ok()) draft_ = std::move(rules);
  return s;
}

std::vector<StyleFieldChoice> LayerStyleModel::CandidateFields() const {
  std::vector<StyleFieldChoice> out;
  const FeatureSchema* schema = Schema();
  if (schema == nullptr || draft_.renderer == FeatureRenderer::kSingle) return out;
  for (const FieldDef& f : schema->fields) {
    const bool ok = draft_.renderer == FeatureRenderer::kGraduated
                        ? f.type == FieldType::kInteger || f.type == FieldType::kReal
                        : f.type != FieldType::kBlob;
    if (ok) out.push_back({f.name, f.DisplayTitle()});
  }
  return out;
}

Status LayerStyleModel::SetField(const std::string& name) {
  const std::vector<StyleFieldChoice> fields = CandidateFields();
  if (std::none_of(fields.begin(), fields.end(), [&](const StyleFieldChoice& f) { return f.name == name; }))
    return Bad("\"" + name + "\" cannot be used by this renderer");
  draft_.field = name;
  return Classify();
}

Status LayerStyleModel::Classify() {
  if (draft_.renderer == FeatureRenderer::kSingle) return Status::Ok();
  const auto o = overlay_.lock();
  if (!o || !o->layer()) return Status::Error(kNotFound, "the overlay has closed");
  const int index = o->layer()->Schema().Find(draft_.field);
  if (draft_.renderer == FeatureRenderer::kCategorised)
    return ClassifyCategories(*o->layer(), index, draft_.symbol, &draft_.categories);
  return ClassifyGraduated(*o->layer(), index, draft_.symbol, graduated_, &draft_.classes);
}

size_t LayerStyleModel::EntryCount() const {
  if (feature_ != 0) return 1;
  switch (draft_.renderer) {
    case FeatureRenderer::kCategorised: return draft_.categories.size() + 1;
    case FeatureRenderer::kGraduated: return draft_.classes.size() + 1;
    case FeatureRenderer::kRules: return RuleLegend(draft_).size();
    default: return 1;
  }
}

bool LayerStyleModel::EntryIsOther(size_t i) const {
  return feature_ == 0 &&
         (draft_.renderer == FeatureRenderer::kCategorised || draft_.renderer == FeatureRenderer::kGraduated) &&
         i + 1 == EntryCount();
}

std::string LayerStyleModel::EntryLabel(size_t i) const {
  if (i >= EntryCount()) return "";
  if (feature_ != 0) return "This feature";
  if (draft_.renderer == FeatureRenderer::kSingle) return "All features";
  if (draft_.renderer == FeatureRenderer::kRules) return RuleLegend(draft_)[i].title;
  if (EntryIsOther(i)) return "Other";
  if (draft_.renderer == FeatureRenderer::kCategorised) {
    const FeatureCategory& c = draft_.categories[i];
    if (!c.label.empty()) return c.label;
    return c.value.empty() ? "(empty)" : c.value;
  }
  return FeatureClassLabel(draft_, i);
}

std::string LayerStyleModel::EntryValue(size_t i) const {
  if (feature_ != 0 || i >= EntryCount() || EntryIsOther(i)) return "";
  if (draft_.renderer == FeatureRenderer::kCategorised) return draft_.categories[i].value;
  if (draft_.renderer == FeatureRenderer::kGraduated) return Number(draft_.classes[i].lower);
  return "";
}

const FeatureSymbol& LayerStyleModel::EntrySymbol(size_t i) const {
  static const FeatureSymbol kDefault;
  if (i >= EntryCount()) return kDefault;
  if (feature_ != 0) {
    const FeatureSchema* schema = Schema();
    return draft_.SymbolFor(feature_data_, schema ? schema->Find(draft_.field) : -1);
  }
  if (draft_.renderer == FeatureRenderer::kCategorised && i < draft_.categories.size())
    return draft_.categories[i].symbol;
  if (draft_.renderer == FeatureRenderer::kGraduated && i < draft_.classes.size()) return draft_.classes[i].symbol;
  return draft_.symbol;
}

FeatureSymbol* LayerStyleModel::MutableEntry(size_t i) {
  if (i >= EntryCount()) return nullptr;
  if (feature_ == 0 && draft_.renderer == FeatureRenderer::kRules) return nullptr;
  if (feature_ != 0) {
    const FeatureSymbol current = EntrySymbol(0);
    return &draft_.overrides.emplace(feature_, current).first->second;
  }
  if (draft_.renderer == FeatureRenderer::kCategorised && i < draft_.categories.size())
    return &draft_.categories[i].symbol;
  if (draft_.renderer == FeatureRenderer::kGraduated && i < draft_.classes.size()) return &draft_.classes[i].symbol;
  return &draft_.symbol;
}

Status LayerStyleModel::SetEntrySymbol(size_t i, const FeatureSymbol& symbol) {
  FeatureSymbol* s = MutableEntry(i);
  if (s == nullptr) return Bad("no style entry " + std::to_string(i));
  for (double v : {symbol.marker_size_px, symbol.marker_outline_px, symbol.line_width_px, symbol.casing_px,
                   symbol.area_outline_px})
    if (!std::isfinite(v) || v < 0 || v > kMaxPx) return Bad("enter a size from 0 to 64 pixels");
  *s = symbol;
  return Status::Ok();
}

Status LayerStyleModel::SetEntryLabel(size_t i, const std::string& label) {
  if (feature_ == 0 && draft_.renderer == FeatureRenderer::kRules && i < EntryCount()) {
    const std::string title = Trim(label);
    const std::vector<RuleLegendEntry> legend = RuleLegend(draft_);
    for (size_t r : legend[i].rules) draft_.rules[r].legend = title;
    return Status::Ok();
  }
  if (feature_ != 0 || i >= EntryCount() || EntryIsOther(i) || draft_.renderer == FeatureRenderer::kSingle)
    return Bad("only a category or class has a label");
  if (draft_.renderer == FeatureRenderer::kCategorised) draft_.categories[i].label = Trim(label);
  else draft_.classes[i].label = Trim(label);
  return Status::Ok();
}

Status LayerStyleModel::SetEntryValue(size_t i, const std::string& text) {
  if (feature_ != 0 || i >= EntryCount() || EntryIsOther(i) || draft_.renderer == FeatureRenderer::kSingle ||
      draft_.renderer == FeatureRenderer::kRules)
    return Bad("only a category or class has a value");
  const std::string t = Trim(text);
  if (draft_.renderer == FeatureRenderer::kCategorised) {
    if (t.empty()) return Bad("a category needs a value");
    for (size_t k = 0; k < draft_.categories.size(); ++k)
      if (k != i && draft_.categories[k].value == t) return Bad("\"" + t + "\" already has a category");
    draft_.categories[i].value = t;
    return Status::Ok();
  }
  char* end = nullptr;
  const double v = std::strtod(t.c_str(), &end);
  if (t.empty() || end == t.c_str() || *end != '\0' || !std::isfinite(v)) return Bad("enter a number");
  for (size_t k = 0; k < draft_.classes.size(); ++k)
    if (k != i && draft_.classes[k].lower == v) return Bad("another class already starts at " + t);
  draft_.classes[i].lower = v;
  std::stable_sort(draft_.classes.begin(), draft_.classes.end(),
                   [](const FeatureClass& a, const FeatureClass& b) { return a.lower < b.lower; });
  return Status::Ok();
}

Status LayerStyleModel::AddEntry() {
  if (feature_ != 0 || draft_.renderer == FeatureRenderer::kSingle || draft_.renderer == FeatureRenderer::kRules)
    return Bad("only a categorised or graduated style has classes");
  if (draft_.renderer == FeatureRenderer::kCategorised) {
    FeatureCategory c;
    c.symbol = draft_.categories.empty() ? draft_.symbol : draft_.categories.back().symbol;
    for (int n = static_cast<int>(draft_.categories.size()) + 1;; ++n) {
      const std::string v = "value " + std::to_string(n);
      if (std::none_of(draft_.categories.begin(), draft_.categories.end(),
                       [&](const FeatureCategory& k) { return k.value == v; })) {
        c.value = v;
        break;
      }
    }
    draft_.categories.push_back(std::move(c));
    return Status::Ok();
  }
  FeatureClass c;
  if (draft_.classes.empty()) {
    c.symbol = draft_.symbol;
  } else {
    const FeatureClass& last = draft_.classes.back();
    const double step = draft_.classes.size() > 1 ? last.lower - draft_.classes[draft_.classes.size() - 2].lower : 1.0;
    c.lower = last.lower + (step > 0 ? step : 1.0);
    c.symbol = last.symbol;
  }
  draft_.classes.push_back(std::move(c));
  return Status::Ok();
}

Status LayerStyleModel::RemoveEntry(size_t i) {
  if (feature_ != 0 || i >= EntryCount() || EntryIsOther(i) || draft_.renderer == FeatureRenderer::kSingle ||
      draft_.renderer == FeatureRenderer::kRules)
    return Bad("only a category or class can be removed");
  if (draft_.renderer == FeatureRenderer::kCategorised) draft_.categories.erase(draft_.categories.begin() + i);
  else draft_.classes.erase(draft_.classes.begin() + i);
  return Status::Ok();
}

Status LayerStyleModel::RenderSwatch(size_t i, int width, int height, double scale, PixelBuffer* out) const {
  if (out == nullptr) return Bad("RenderSwatch: null out");
  if (width <= 0 || height <= 0 || width > 1024 || height > 1024) return Bad("swatch size out of range");
  CpuCanvas canvas(width, height);
  canvas.Clear({255, 255, 255, 255});
  Status s;
  if (feature_ == 0 && draft_.renderer == FeatureRenderer::kRules) {
    const std::vector<RuleLegendEntry> legend = RuleLegend(draft_);
    if (i >= legend.size()) return Bad("no style entry " + std::to_string(i));
    std::vector<StyleRule> rules;
    for (size_t r : legend[i].rules) rules.push_back(draft_.rules[r]);
    const auto o = overlay_.lock();
    s = DrawRulesSwatch(canvas, rules, geometry_, scale, o ? o->label_font() : std::string());
  } else {
    s = DrawFeatureSwatch(canvas, EntrySymbol(i), geometry_, scale);
  }
  if (!s.ok()) return s;
  *out = canvas.Buffer();
  return Status::Ok();
}

bool LayerStyleModel::has_override() const { return feature_ != 0 && draft_.overrides.count(feature_) != 0; }

Status LayerStyleModel::ClearOverride() {
  if (feature_ == 0) return Bad("the layer style has no feature override");
  draft_.overrides.erase(feature_);
  return Status::Ok();
}

std::vector<StyleFieldChoice> LayerStyleModel::LabelFields() const {
  std::vector<StyleFieldChoice> out;
  if (const FeatureSchema* schema = Schema())
    for (const FieldDef& f : schema->fields)
      if (f.type != FieldType::kBlob) out.push_back({f.name, f.DisplayTitle()});
  return out;
}

Status LayerStyleModel::SetLabel(const FeatureLabel& label) {
  if (!label.field.empty()) {
    const FeatureSchema* schema = Schema();
    const int index = schema ? schema->Find(label.field) : -1;
    if (index < 0) return Bad("no field \"" + label.field + "\" to label with");
    if (schema->fields[index].type == FieldType::kBlob) return Bad("a blob field cannot be a label");
  }
  for (double v : {label.size_px, label.halo_px})
    if (!std::isfinite(v) || v < 0 || v > kMaxPx) return Bad("enter a size from 0 to 64 pixels");
  draft_.label = label;
  return Status::Ok();
}

Status LayerStyleModel::Apply() {
  const auto o = overlay_.lock();
  if (!o) return Status::Error(kNotFound, "the overlay has closed");
  o->SetStyle(draft_);
  applied_ = draft_;
  return Status::Ok();
}

Status LayerStyleModel::Revert() {
  const auto o = overlay_.lock();
  if (!o) return Status::Error(kNotFound, "the overlay has closed");
  draft_ = o->style();
  applied_ = draft_;
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
