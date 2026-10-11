// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/schema_change.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "fvkit/feature/value_text.h"

namespace fv {

namespace {

/// The shortest of %.15g and %.17g that reads back as `d`.
std::string ExactNumber(double d) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.15g", d);
  if (std::strtod(buf, nullptr) != d) std::snprintf(buf, sizeof buf, "%.17g", d);
  return buf;
}

/// The text a value converts to: what the form shows, without choice labels,
/// numbers exact.
std::string PlainText(const FieldDef& from, const FieldValue& value) {
  if (const auto* d = std::get_if<double>(&value)) return ExactNumber(*d);
  FieldDef plain = from;
  plain.role = FieldRole::kNone;
  return FormatFieldValue(plain, value);
}

/// `value` in `to`'s storage type, before choices and bounds; null when it
/// does not convert. `*changed` is set when it converted with a loss.
FieldValue ConvertType(const FieldDef& from, const FieldValue& value, const FieldDef& to,
                       bool* changed) {
  if (to.type == FieldType::kBlob || std::holds_alternative<std::vector<uint8_t>>(value)) {
    if (to.type == FieldType::kBlob && std::holds_alternative<std::vector<uint8_t>>(value)) return value;
    return std::monostate{};
  }
  if (from.type == to.type) return value;
  if (const auto* b = std::get_if<bool>(&value)) {
    if (to.type == FieldType::kInteger) return static_cast<int64_t>(*b ? 1 : 0);
    if (to.type == FieldType::kReal) return *b ? 1.0 : 0.0;
  }
  if (const auto* d = std::get_if<double>(&value)) {
    if (to.type == FieldType::kInteger) {
      if (!std::isfinite(*d) || std::fabs(*d) > 9.2e18) return std::monostate{};
      const double r = std::round(*d);
      if (r != *d) *changed = true;
      return static_cast<int64_t>(r);
    }
  }
  if (const auto* i = std::get_if<int64_t>(&value)) {
    if (to.type == FieldType::kReal) return static_cast<double>(*i);
  }
  if (const auto* s = std::get_if<std::string>(&value)) {
    if (from.type == FieldType::kDate && to.type == FieldType::kDateTime) return *s + "T00:00:00";
    if (from.type == FieldType::kDateTime && to.type == FieldType::kDate && s->size() > 10) {
      *changed = true;
      return s->substr(0, 10);
    }
  }
  FieldDef target = to;
  target.role = FieldRole::kNone;
  FieldValue out;
  if (!ParseFieldValue(target, PlainText(from, value), &out).ok()) return std::monostate{};
  return out;
}

}  // namespace

FieldValue ConvertFieldValue(const FieldDef& from, const FieldValue& value, const FieldDef& to,
                             bool* lost) {
  bool dummy = false;
  bool* changed = lost != nullptr ? lost : &dummy;
  *changed = false;
  if (IsNull(value)) return value;
  FieldValue out = ConvertType(from, value, to, changed);
  if (!IsNull(out)) {
    FieldDef unrequired = to;
    unrequired.required = false;
    if (!ValidateValue(unrequired, out).ok()) {
      // An enumeration also takes a choice's label.
      FieldValue parsed;
      if (to.role == FieldRole::kEnumeration && ParseFieldValue(to, PlainText(from, value), &parsed).ok() &&
          ValidateValue(unrequired, parsed).ok())
        out = std::move(parsed);
      else
        out = std::monostate{};
    }
  }
  if (IsNull(out)) *changed = true;
  return out;
}

void RemapFeature(const Feature& in, const FeatureSchema& old, const SchemaChange& change,
                  Feature* out, std::vector<size_t>* lost) {
  const size_t n = change.schema.fields.size();
  Feature f;
  f.id = in.id;
  f.geometry = in.geometry;
  f.values.resize(n);
  if (lost != nullptr) lost->assign(n, 0);
  for (size_t i = 0; i < n; ++i) {
    const FieldDef& to = change.schema.fields[i];
    const int src = i < change.source.size() ? change.source[i] : -1;
    FieldValue v;
    if (src < 0) {
      v = to.default_value;
    } else if (static_cast<size_t>(src) < in.values.size()) {
      bool was_lost = false;
      v = ConvertFieldValue(old.fields[src], in.values[src], to, &was_lost);
      if (was_lost && lost != nullptr) ++(*lost)[i];
    }
    if (IsNull(v) && to.required) v = to.default_value;
    f.values[i] = std::move(v);
  }
  *out = std::move(f);
}

std::string SchemaChangeImpact::Describe() const {
  std::string text;
  auto sentence = [&](const std::string& s) {
    if (!text.empty()) text += " ";
    text += s;
  };
  for (const std::string& name : dropped) sentence("Deleting \"" + name + "\" discards its values.");
  for (const auto& c : converted)
    sentence(std::to_string(c.second) + (c.second == 1 ? " value" : " values") + " of \"" + c.first +
             "\" will not convert and will be cleared or changed.");
  for (const std::string& name : missing_required)
    sentence("\"" + name + "\" is required but some features would have no value; give it a default.");
  return text;
}

Status AssessSchemaChange(const IFeatureLayer& layer, const SchemaChange& change,
                          SchemaChangeImpact* out) {
  const FeatureSchema& old = layer.Schema();
  Status s = ValidateSchemaChange(old, change);
  if (!s.ok()) return s;
  std::vector<Feature> features;
  s = layer.All(&features);
  if (!s.ok()) return s;

  const size_t n = change.schema.fields.size();
  std::vector<bool> kept(old.fields.size(), false);
  for (int src : change.source)
    if (src >= 0) kept[src] = true;
  std::vector<bool> dropped_has_data(old.fields.size(), false);
  std::vector<size_t> lost_total(n, 0);
  std::vector<bool> missing(n, false);
  std::vector<size_t> lost;
  for (const Feature& f : features) {
    for (size_t j = 0; j < old.fields.size() && j < f.values.size(); ++j)
      if (!kept[j] && !IsNull(f.values[j])) dropped_has_data[j] = true;
    Feature mapped;
    RemapFeature(f, old, change, &mapped, &lost);
    for (size_t i = 0; i < n; ++i) {
      lost_total[i] += lost[i];
      if (change.schema.fields[i].required && IsNull(mapped.values[i])) missing[i] = true;
    }
  }

  SchemaChangeImpact impact;
  for (size_t j = 0; j < old.fields.size(); ++j)
    if (dropped_has_data[j]) impact.dropped.push_back(old.fields[j].name);
  for (size_t i = 0; i < n; ++i) {
    if (lost_total[i] > 0) impact.converted.emplace_back(change.schema.fields[i].name, lost_total[i]);
    if (missing[i]) impact.missing_required.push_back(change.schema.fields[i].name);
  }
  if (out != nullptr) *out = std::move(impact);
  return Status::Ok();
}

}  // namespace fv
