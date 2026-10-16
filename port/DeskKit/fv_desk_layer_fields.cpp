// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_layer_fields.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <utility>

#include "fvkit/feature/templates.h"
#include "fvkit/overlay/feature_overlay.h"

namespace fv {
namespace desk {

namespace {

Status Bad(const std::string& why) { return Status::Error(kInvalidArg, why); }

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool IsNumeric(FieldType t) { return t == FieldType::kInteger || t == FieldType::kReal; }

std::string Extension(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return std::string();
  return Lower(path.substr(dot + 1));
}

}  // namespace

const std::vector<FieldType>& EditableFieldTypes() {
  static const std::vector<FieldType> kTypes = {FieldType::kText,    FieldType::kInteger, FieldType::kReal,
                                                FieldType::kBoolean, FieldType::kDate,    FieldType::kDateTime};
  return kTypes;
}

const std::vector<FieldRole>& EditableFieldRoles() {
  static const std::vector<FieldRole> kRoles = {FieldRole::kNone,        FieldRole::kPhone,
                                                FieldRole::kUrl,         FieldRole::kEmail,
                                                FieldRole::kEnumeration, FieldRole::kMeasurement,
                                                FieldRole::kMultilineText};
  return kRoles;
}

std::string FieldTypeTitle(FieldType type) {
  switch (type) {
    case FieldType::kText: return "Text";
    case FieldType::kInteger: return "Whole number";
    case FieldType::kReal: return "Decimal number";
    case FieldType::kBoolean: return "Yes/No";
    case FieldType::kDate: return "Date";
    case FieldType::kDateTime: return "Date and time";
    case FieldType::kBlob: return "Binary";
  }
  return "Text";
}

std::string FieldRoleTitle(FieldRole role) {
  switch (role) {
    case FieldRole::kNone: return "None";
    case FieldRole::kPhone: return "Phone";
    case FieldRole::kUrl: return "Web address";
    case FieldRole::kEmail: return "Email";
    case FieldRole::kEnumeration: return "List of choices";
    case FieldRole::kMeasurement: return "Measurement";
    case FieldRole::kMultilineText: return "Multi-line text";
  }
  return "None";
}

std::string FormatChoices(const std::vector<FieldChoice>& choices) {
  std::string text;
  for (const FieldChoice& c : choices) {
    if (!text.empty()) text += "\n";
    text += c.value;
    if (!c.label.empty()) text += " = " + c.label;
  }
  return text;
}

Status ParseChoices(const std::string& text, std::vector<FieldChoice>* out) {
  std::vector<FieldChoice> choices;
  std::set<std::string> seen;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string line = Trim(text.substr(start, end - start));
    start = end + 1;
    if (line.empty()) continue;
    FieldChoice c;
    const size_t eq = line.find('=');
    c.value = Trim(eq == std::string::npos ? line : line.substr(0, eq));
    if (eq != std::string::npos) c.label = Trim(line.substr(eq + 1));
    if (c.value.empty()) return Bad("a choice has no value: \"" + line + "\"");
    if (!seen.insert(c.value).second) return Bad("choice \"" + c.value + "\" appears twice");
    choices.push_back(std::move(c));
  }
  *out = std::move(choices);
  return Status::Ok();
}

// MARK: LayerFieldsModel

Status LayerFieldsModel::Open(const std::shared_ptr<FeatureOverlay>& overlay, std::string title,
                              std::shared_ptr<LayerFieldsModel>* out) {
  if (out == nullptr) return Bad("LayerFieldsModel::Open: null out");
  if (!overlay || !overlay->layer()) return Status::Error(kNotFound, "the overlay has no feature layer");
  std::shared_ptr<LayerFieldsModel> m(new LayerFieldsModel());
  m->overlay_ = overlay;
  m->overlay_id_ = overlay.get();
  m->title_ = std::move(title);
  m->Reset(overlay->layer()->Schema());
  *out = std::move(m);
  return Status::Ok();
}

void LayerFieldsModel::Reset(const FeatureSchema& schema) {
  original_ = schema;
  draft_.schema = schema;
  draft_.source.clear();
  for (size_t i = 0; i < schema.fields.size(); ++i) draft_.source.push_back(static_cast<int>(i));
}

bool LayerFieldsModel::dirty() const {
  if (!(draft_.schema == original_)) return true;
  for (size_t i = 0; i < draft_.source.size(); ++i)
    if (draft_.source[i] != static_cast<int>(i)) return true;
  return false;
}

const FieldDef& LayerFieldsModel::Field(size_t i) const {
  static const FieldDef kNone;
  return i < draft_.schema.fields.size() ? draft_.schema.fields[i] : kNone;
}

std::string LayerFieldsModel::OriginalName(size_t i) const {
  if (i >= draft_.source.size() || draft_.source[i] < 0) return std::string();
  return original_.fields[draft_.source[i]].name;
}

size_t LayerFieldsModel::AddField() {
  FieldDef f;
  f.name = "field";
  for (int n = 1; draft_.schema.Find(f.name) >= 0; ++n) f.name = "field_" + std::to_string(n);
  draft_.schema.fields.push_back(std::move(f));
  draft_.source.push_back(-1);
  return draft_.schema.fields.size() - 1;
}

Status LayerFieldsModel::RemoveField(size_t i) {
  if (i >= draft_.schema.fields.size()) return Bad("no field " + std::to_string(i));
  const std::string name = draft_.schema.fields[i].name;
  draft_.schema.fields.erase(draft_.schema.fields.begin() + static_cast<std::ptrdiff_t>(i));
  draft_.source.erase(draft_.source.begin() + static_cast<std::ptrdiff_t>(i));
  for (FieldDef& f : draft_.schema.fields)
    if (Lower(f.range_end) == Lower(name)) f.range_end.clear();
  return Status::Ok();
}

Status LayerFieldsModel::MoveField(size_t from, size_t to) {
  const size_t n = draft_.schema.fields.size();
  if (from >= n || to >= n) return Bad("no field to move there");
  auto& fields = draft_.schema.fields;
  auto& source = draft_.source;
  FieldDef f = std::move(fields[from]);
  const int src = source[from];
  fields.erase(fields.begin() + static_cast<std::ptrdiff_t>(from));
  source.erase(source.begin() + static_cast<std::ptrdiff_t>(from));
  fields.insert(fields.begin() + static_cast<std::ptrdiff_t>(to), std::move(f));
  source.insert(source.begin() + static_cast<std::ptrdiff_t>(to), src);
  return Status::Ok();
}

Status LayerFieldsModel::SetField(size_t i, const FieldDef& field) {
  if (i >= draft_.schema.fields.size()) return Bad("no field " + std::to_string(i));
  FieldDef f = field;
  f.name = Trim(f.name);
  if (f.name.empty()) return Bad("a field needs a name");
  const int other = draft_.schema.Find(f.name);
  if (other >= 0 && static_cast<size_t>(other) != i) return Bad("another field is named \"" + f.name + "\"");

  const FieldDef& was = draft_.schema.fields[i];
  if (f.type != was.type) {
    if (!IsNumeric(f.type)) {
      f.min.reset();
      f.max.reset();
    }
    bool lost = false;
    f.default_value = ConvertFieldValue(was, f.default_value, f, &lost);
    if (f.type != FieldType::kDate && f.type != FieldType::kDateTime) f.range_end.clear();
  }
  if (f.role != FieldRole::kEnumeration) f.choices.clear();
  if (f.role != FieldRole::kMeasurement) f.unit.clear();

  if (Lower(f.name) != Lower(was.name))
    for (FieldDef& g : draft_.schema.fields)
      if (Lower(g.range_end) == Lower(was.name)) g.range_end = f.name;
  draft_.schema.fields[i] = std::move(f);
  return Status::Ok();
}

Status LayerFieldsModel::Validate() const { return draft_.schema.Validate(); }

Status LayerFieldsModel::Assess(SchemaChangeImpact* out) const {
  const auto o = overlay_.lock();
  if (!o || !o->layer()) return Status::Error(kNotFound, "the layer has closed");
  if (!(o->layer()->Schema() == original_))
    return Status::Error(kInvalidArg, "the layer's fields changed since the dialog opened; revert first");
  return AssessSchemaChange(*o->layer(), draft_, out);
}

std::vector<std::string> LayerFieldsModel::StyleBreaks() const {
  const auto o = overlay_.lock();
  if (!o || !o->layer()) return {};
  return StyleBreaksFromSchemaChange(o->style(), o->layer()->Schema(), draft_);
}

Status LayerFieldsModel::Apply() {
  const auto o = overlay_.lock();
  if (!o || !o->layer()) return Status::Error(kNotFound, "the layer has closed");
  if (!(o->layer()->Schema() == original_))
    return Status::Error(kInvalidArg, "the layer's fields changed since the dialog opened; revert first");
  Status s = o->ChangeSchema(draft_);
  if (!s.ok()) return s;
  Reset(o->layer()->Schema());
  return Status::Ok();
}

Status LayerFieldsModel::Revert() {
  const auto o = overlay_.lock();
  if (!o || !o->layer()) return Status::Error(kNotFound, "the layer has closed");
  Reset(o->layer()->Schema());
  return Status::Ok();
}

// MARK: NewLayerModel

const std::vector<GeometryType>& NewLayerGeometries() {
  static const std::vector<GeometryType> kTypes = {
      GeometryType::kPoint,      GeometryType::kLineString,      GeometryType::kPolygon,
      GeometryType::kMultiPoint, GeometryType::kMultiLineString, GeometryType::kMultiPolygon,
      GeometryType::kGeometry};
  return kTypes;
}

std::string GeometryTitle(GeometryType type) {
  switch (type) {
    case GeometryType::kPoint: return "Point";
    case GeometryType::kLineString: return "Line";
    case GeometryType::kPolygon: return "Area";
    case GeometryType::kMultiPoint: return "Multipoint";
    case GeometryType::kMultiLineString: return "Multiline";
    case GeometryType::kMultiPolygon: return "Multi-area";
    default: return "Any geometry";
  }
}

NewLayerModel::NewLayerModel() { SetTemplate(0); }

size_t NewLayerModel::TemplateCount() const { return FeatureLayerTemplates().size(); }

std::string NewLayerModel::TemplateTitle(size_t i) const {
  const auto& t = FeatureLayerTemplates();
  return i < t.size() ? t[i].title : std::string();
}

Status NewLayerModel::SetTemplate(size_t i) {
  const auto& t = FeatureLayerTemplates();
  if (i >= t.size()) return Bad("no template " + std::to_string(i));
  template_ = i;
  schema_ = t[i].schema;
  // The blank template starts as points, the commonest new layer.
  if (schema_.geometry_type == GeometryType::kGeometry) schema_.geometry_type = GeometryType::kPoint;
  if (!name_by_hand_) name_ = t[i].id == "blank" ? "New Layer" : t[i].title;
  return Status::Ok();
}

Status NewLayerModel::SetGeometry(GeometryType type) {
  const auto& types = NewLayerGeometries();
  if (std::find(types.begin(), types.end(), type) == types.end()) return Bad("not a layer geometry type");
  schema_.geometry_type = type;
  return Status::Ok();
}

void NewLayerModel::SetName(std::string name) {
  name_ = Trim(name);
  name_by_hand_ = true;
}

void NewLayerModel::SetTarget(NewLayerTarget target) {
  target_ = target;
  if (target == NewLayerTarget::kPackage) {
    if (format_ != FeatureOverlay::kFormatGeoPackage) path_.clear();
    format_ = FeatureOverlay::kFormatGeoPackage;
  }
}

Status NewLayerModel::SetFormat(int format) {
  if (format != FeatureOverlay::kFormatGeoPackage && format != FeatureOverlay::kFormatGeoJson)
    return Bad("format is 0 (GeoPackage) or 1 (GeoJSON)");
  if (target_ == NewLayerTarget::kPackage && format != FeatureOverlay::kFormatGeoPackage)
    return Bad("a layer added to a GeoPackage is a GeoPackage table");
  format_ = format;
  if (!path_.empty()) {
    const std::string ext = Extension(path_);
    const std::string want = format == FeatureOverlay::kFormatGeoJson ? "geojson" : "gpkg";
    if (ext == "gpkg" || ext == "geojson" || ext == "json") path_.resize(path_.size() - ext.size() - 1);
    path_ += "." + want;
  }
  return Status::Ok();
}

void NewLayerModel::SetPath(std::string path) {
  path_ = Trim(path);
  if (path_.empty()) return;
  const std::string ext = Extension(path_);
  if (ext == "geojson" || ext == "json") format_ = FeatureOverlay::kFormatGeoJson;
  else if (ext == "gpkg") format_ = FeatureOverlay::kFormatGeoPackage;
  else path_ += format_ == FeatureOverlay::kFormatGeoJson ? ".geojson" : ".gpkg";
}

Status NewLayerModel::Validate() const {
  if (name_.empty()) return Bad("the layer needs a name");
  if (target_ != NewLayerTarget::kUnsaved && path_.empty()) return Bad("choose where to save the layer");
  if (target_ == NewLayerTarget::kPackage) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path_, ec))
      return Status::Error(kNotFound, "choose an existing GeoPackage to add the layer to");
  }
  return schema_.Validate();
}

Status NewLayerModel::Create(std::string* spec) const {
  if (spec == nullptr) return Bad("Create: null spec");
  Status s = Validate();
  if (!s.ok()) return s;
  switch (target_) {
    case NewLayerTarget::kPackage:
      return AddFeatureLayerToPackage(path_, name_, schema_, spec);
    case NewLayerTarget::kUnsaved:
      return Bad("an unsaved layer has no file to create");
    default:
      s = CreateFeatureLayerFile(path_, format_, name_, schema_);
      if (s.ok()) *spec = path_;
      return s;
  }
}

}  // namespace desk
}  // namespace fv
