// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/schema.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace fv {

namespace {

constexpr const char* kTypeNames[] = {"text", "integer", "real", "boolean",
                                      "date", "datetime", "blob"};
constexpr const char* kRoleNames[] = {"", "phone", "url", "email",
                                      "enumeration", "measurement", "multiline"};

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool IsDateType(FieldType t) { return t == FieldType::kDate || t == FieldType::kDateTime; }

bool IsNumber(const FieldValue& v) {
  return std::holds_alternative<int64_t>(v) || std::holds_alternative<double>(v);
}

double AsNumber(const FieldValue& v) {
  if (const auto* i = std::get_if<int64_t>(&v)) return static_cast<double>(*i);
  return std::get<double>(v);
}

/// The text form of a value, for matching an enumeration's choices.
std::string ChoiceKey(const FieldValue& v) {
  if (const auto* s = std::get_if<std::string>(&v)) return *s;
  if (const auto* i = std::get_if<int64_t>(&v)) return std::to_string(*i);
  if (const auto* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
  return std::string();
}

}  // namespace

const char* ToString(FieldType type) {
  const int i = static_cast<int>(type);
  return i >= 0 && i <= 6 ? kTypeNames[i] : "text";
}

const char* ToString(FieldRole role) {
  const int i = static_cast<int>(role);
  return i >= 0 && i <= 6 ? kRoleNames[i] : "";
}

bool FieldTypeFromString(const std::string& name, FieldType* out) {
  const std::string n = Lower(name);
  for (int i = 0; i <= 6; ++i) {
    if (n == kTypeNames[i]) {
      *out = static_cast<FieldType>(i);
      return true;
    }
  }
  return false;
}

bool FieldRoleFromString(const std::string& name, FieldRole* out) {
  const std::string n = Lower(name);
  for (int i = 0; i <= 6; ++i) {
    if (n == kRoleNames[i]) {
      *out = static_cast<FieldRole>(i);
      return true;
    }
  }
  return false;
}

bool FieldDef::operator==(const FieldDef& o) const {
  return name == o.name && type == o.type && role == o.role && title == o.title &&
         description == o.description && unit == o.unit && required == o.required &&
         default_value == o.default_value && choices == o.choices && min == o.min &&
         max == o.max && range_end == o.range_end;
}

int FeatureSchema::Find(const std::string& name) const {
  const std::string key = Lower(name);
  for (size_t i = 0; i < fields.size(); ++i)
    if (Lower(fields[i].name) == key) return static_cast<int>(i);
  return -1;
}

Status FeatureSchema::Validate() const {
  std::set<std::string> seen;
  for (const FieldDef& f : fields) {
    if (f.name.empty()) return Status::Error(kInvalidArg, "a field has no name");
    if (!seen.insert(Lower(f.name)).second)
      return Status::Error(kInvalidArg, "field \"" + f.name + "\" appears twice");
    if (f.role == FieldRole::kEnumeration && f.choices.empty())
      return Status::Error(kInvalidArg, "field \"" + f.name + "\" is a list with no choices");
    if (f.min && f.max && *f.min > *f.max)
      return Status::Error(kInvalidArg, "field \"" + f.name + "\" has min above max");
    if (!f.range_end.empty()) {
      const int end = Find(f.range_end);
      if (!IsDateType(f.type) || end < 0 || !IsDateType(fields[end].type))
        return Status::Error(kInvalidArg, "field \"" + f.name + "\" ends its range at \"" +
                                              f.range_end + "\", which is not a date field");
    }
  }
  return Status::Ok();
}

Status ValidateValue(const FieldDef& field, const FieldValue& value) {
  const std::string who = "\"" + field.DisplayTitle() + "\"";
  if (IsNull(value)) {
    if (field.required) return Status::Error(kInvalidArg, who + " is required");
    return Status::Ok();
  }
  bool type_ok = false;
  switch (field.type) {
    case FieldType::kText:
    case FieldType::kDate:
    case FieldType::kDateTime: type_ok = std::holds_alternative<std::string>(value); break;
    case FieldType::kInteger: type_ok = std::holds_alternative<int64_t>(value); break;
    case FieldType::kReal: type_ok = IsNumber(value); break;
    case FieldType::kBoolean: type_ok = std::holds_alternative<bool>(value); break;
    case FieldType::kBlob: type_ok = std::holds_alternative<std::vector<uint8_t>>(value); break;
  }
  if (!type_ok) return Status::Error(kInvalidArg, who + " needs a " + ToString(field.type) + " value");
  if (IsNumber(value)) {
    const double x = AsNumber(value);
    if (std::isnan(x)) return Status::Error(kInvalidArg, who + " is not a number");
    if (field.min && x < *field.min) return Status::Error(kInvalidArg, who + " is below its minimum");
    if (field.max && x > *field.max) return Status::Error(kInvalidArg, who + " is above its maximum");
  }
  if (field.role == FieldRole::kEnumeration && !field.choices.empty()) {
    const std::string key = ChoiceKey(value);
    const bool listed = std::any_of(field.choices.begin(), field.choices.end(),
                                    [&](const FieldChoice& c) { return c.value == key; });
    if (!listed) return Status::Error(kInvalidArg, who + " is not one of its choices");
  }
  return Status::Ok();
}

Status ValidateSchemaChange(const FeatureSchema& old, const SchemaChange& change) {
  Status s = change.schema.Validate();
  if (!s.ok()) return s;
  if (change.schema.geometry_type != old.geometry_type)
    return Status::Error(kInvalidArg, "a schema change cannot change the geometry type");
  if (change.source.size() != change.schema.fields.size())
    return Status::Error(kInvalidArg, "a schema change needs one source per field");
  std::set<int> used;
  for (int src : change.source) {
    if (src < -1 || src >= static_cast<int>(old.fields.size()))
      return Status::Error(kInvalidArg, "a schema change names old field " + std::to_string(src) +
                                            ", which does not exist");
    if (src >= 0 && !used.insert(src).second)
      return Status::Error(kInvalidArg, "a schema change feeds two fields from \"" +
                                            old.fields[src].name + "\"");
  }
  return Status::Ok();
}

}  // namespace fv
