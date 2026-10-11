// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_feature_form.h"

#include "fvkit/feature/value_text.h"

namespace fv {
namespace desk {

namespace {

FormControl ControlFor(const FieldDef& f) {
  if (f.type == FieldType::kBlob) return FormControl::kReadOnly;
  if (f.role == FieldRole::kEnumeration && !f.choices.empty()) return FormControl::kChoice;
  switch (f.type) {
    case FieldType::kInteger: return FormControl::kInteger;
    case FieldType::kReal: return FormControl::kNumber;
    case FieldType::kBoolean: return FormControl::kCheckBox;
    case FieldType::kDate: return f.range_end.empty() ? FormControl::kDate : FormControl::kDateRange;
    case FieldType::kDateTime: return f.range_end.empty() ? FormControl::kDateTime : FormControl::kDateRange;
    default: break;
  }
  switch (f.role) {
    case FieldRole::kPhone: return FormControl::kPhone;
    case FieldRole::kUrl: return FormControl::kUrl;
    case FieldRole::kEmail: return FormControl::kEmail;
    case FieldRole::kMultilineText: return FormControl::kMultiline;
    default: return FormControl::kText;
  }
}

const FieldValue& ValueAt(const Feature& f, int i) {
  static const FieldValue kNull;
  return i >= 0 && static_cast<size_t>(i) < f.values.size() ? f.values[i] : kNull;
}

}  // namespace

Status FeatureFormModel::Open(std::shared_ptr<IEditableFeatureLayer> layer, int64_t id, std::string title,
                              const Overlay* overlay, std::shared_ptr<FeatureFormModel>* out) {
  if (!layer) return Status::Error(kInvalidArg, "no feature layer");
  std::shared_ptr<FeatureFormModel> m(new FeatureFormModel());
  Status s = layer->Get(id, &m->feature_);
  if (!s.ok()) return s;
  m->layer_ = std::move(layer);
  m->overlay_ = overlay;
  m->id_ = id;
  m->title_ = std::move(title);
  m->Load();
  *out = std::move(m);
  return Status::Ok();
}

void FeatureFormModel::Load() {
  const FeatureSchema& schema = layer_->Schema();
  // A range's end field is shown by its start's control, not on its own.
  std::vector<bool> is_end(schema.fields.size(), false);
  for (const FieldDef& f : schema.fields) {
    const int end = f.range_end.empty() ? -1 : schema.Find(f.range_end);
    if (end >= 0) is_end[end] = true;
  }
  fields_.clear();
  for (size_t i = 0; i < schema.fields.size(); ++i) {
    if (is_end[i]) continue;
    const FieldDef& def = schema.fields[i];
    FormField c;
    c.field = static_cast<int>(i);
    c.control = ControlFor(def);
    c.title = def.DisplayTitle();
    c.description = def.description;
    c.unit = def.unit;
    c.required = def.required;
    for (const FieldChoice& choice : def.choices) c.choices.push_back(choice.label.empty() ? choice.value : choice.label);
    c.text = c.control == FormControl::kReadOnly
                 ? (IsNull(ValueAt(feature_, c.field)) ? "" : "(binary data)")
                 : FormatFieldValue(def, ValueAt(feature_, c.field));
    if (c.control == FormControl::kDateRange) {
      c.end_field = schema.Find(def.range_end);
      c.end_text = FormatFieldValue(schema.fields[c.end_field], ValueAt(feature_, c.end_field));
    }
    fields_.push_back(std::move(c));
  }
  loaded_ = fields_;
}

bool FeatureFormModel::dirty() const {
  for (size_t i = 0; i < fields_.size(); ++i)
    if (fields_[i].text != loaded_[i].text || fields_[i].end_text != loaded_[i].end_text) return true;
  return false;
}

Status FeatureFormModel::SetText(size_t control, std::string text) {
  if (control >= fields_.size()) return Status::Error(kInvalidArg, "no form control " + std::to_string(control));
  if (fields_[control].control == FormControl::kReadOnly)
    return Status::Error(kInvalidArg, "\"" + fields_[control].title + "\" cannot be edited");
  fields_[control].text = std::move(text);
  return Status::Ok();
}

Status FeatureFormModel::SetEndText(size_t control, std::string text) {
  if (control >= fields_.size() || fields_[control].control != FormControl::kDateRange)
    return Status::Error(kInvalidArg, "form control " + std::to_string(control) + " is not a date range");
  fields_[control].end_text = std::move(text);
  return Status::Ok();
}

Status FeatureFormModel::Collect(std::vector<FieldValue>* values) {
  const FeatureSchema& schema = layer_->Schema();
  *values = feature_.values;
  values->resize(schema.fields.size());
  auto take = [&](FormField& c, int field, const std::string& text) {
    if (!c.error.empty()) return;
    const FieldDef& def = schema.fields[field];
    FieldValue v;
    Status s = ParseFieldValue(def, text, &v);
    if (s.ok()) s = ValidateValue(def, v);
    if (s.ok()) (*values)[field] = std::move(v);
    else c.error = s.message;
  };
  for (FormField& c : fields_) {
    c.error.clear();
    if (c.control == FormControl::kReadOnly) continue;
    take(c, c.field, c.text);
    if (c.control != FormControl::kDateRange) continue;
    take(c, c.end_field, c.end_text);
    const auto* from = std::get_if<std::string>(&(*values)[c.field]);
    const auto* to = std::get_if<std::string>(&(*values)[c.end_field]);
    // ISO 8601 text of one type orders as the dates do.
    if (c.error.empty() && from && to && *to < *from) c.error = "\"" + c.title + "\" ends before it starts";
  }
  for (const FormField& c : fields_)
    if (!c.error.empty()) return Status::Error(kInvalidArg, c.error);
  return Status::Ok();
}

Status FeatureFormModel::Validate() {
  std::vector<FieldValue> values;
  return Collect(&values);
}

Status FeatureFormModel::Apply() {
  std::vector<FieldValue> values;
  Status s = Collect(&values);
  if (!s.ok()) return s;
  Feature next = feature_;
  next.values = std::move(values);
  s = layer_->Update(next);
  if (!s.ok()) return s;
  return Revert();
}

Status FeatureFormModel::Revert() {
  Status s = layer_->Get(id_, &feature_);
  if (!s.ok()) return s;
  Load();
  return Status::Ok();
}

std::string FeatureFormModel::ActionUrl(size_t control) const {
  if (control >= fields_.size()) return "";
  return FieldActionUrl(layer_->Schema().fields[fields_[control].field], fields_[control].text);
}

}  // namespace desk
}  // namespace fv
