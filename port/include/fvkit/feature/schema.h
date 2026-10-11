// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/schema.h — a feature layer's fields, their values, and the
/// feature itself (port/feature-overlay-plan.md §2).
///
/// A field has a storage type (what the file holds) and an optional role
/// (what the value means: a phone number, a URL, one of a list). Forms are
/// generated from both; the file formats store the storage type natively and
/// the rest as metadata.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "fvkit/feature/geometry.h"
#include "fvkit/geo.h"

namespace fv {

/// What the file stores. Dates are ISO 8601 text ("2026-10-10",
/// "2026-10-10T14:30:00Z"), as GeoPackage stores DATE and DATETIME.
enum class FieldType : int {
  kText = 0,
  kInteger = 1,
  kReal = 2,
  kBoolean = 3,
  kDate = 4,
  kDateTime = 5,
  kBlob = 6,
};

/// What a value means, for the form and for actions on it.
enum class FieldRole : int {
  kNone = 0,
  kPhone = 1,
  kUrl = 2,
  kEmail = 3,
  kEnumeration = 4,   ///< one of `FieldDef::choices`
  kMeasurement = 5,   ///< a number in `FieldDef::unit`
  kMultilineText = 6,
};

/// "text", "integer", ... and "", "phone", "url", ... — the spellings the
/// GeoJSON schema member and GeoPackage metadata use.
const char* ToString(FieldType type);
const char* ToString(FieldRole role);
bool FieldTypeFromString(const std::string& name, FieldType* out);
bool FieldRoleFromString(const std::string& name, FieldRole* out);

/// Null, or a value of the field's storage type: bool, int64_t, double, or
/// std::string (text and dates). A blob travels as std::vector<uint8_t>.
using FieldValue =
    std::variant<std::monostate, bool, int64_t, double, std::string, std::vector<uint8_t>>;

inline bool IsNull(const FieldValue& v) { return std::holds_alternative<std::monostate>(v); }

/// One allowed value of an enumeration and the text a form shows for it.
struct FieldChoice {
  std::string value;
  std::string label;  ///< empty = show `value`
  bool operator==(const FieldChoice& o) const { return value == o.value && label == o.label; }
};

struct FieldDef {
  std::string name;  ///< the column name; unique in the schema, case-insensitively
  FieldType type = FieldType::kText;
  FieldRole role = FieldRole::kNone;
  std::string title;        ///< what a form shows; empty = `name`
  std::string description;  ///< help text
  std::string unit;         ///< kMeasurement: "m", "m³/s"
  bool required = false;
  FieldValue default_value;
  std::vector<FieldChoice> choices;  ///< kEnumeration
  std::optional<double> min;         ///< numeric bounds, inclusive
  std::optional<double> max;
  /// For the first date of a range, the name of the field holding its end.
  std::string range_end;

  const std::string& DisplayTitle() const { return title.empty() ? name : title; }
  bool operator==(const FieldDef& o) const;
};

struct FeatureSchema {
  GeometryType geometry_type = GeometryType::kGeometry;  ///< kGeometry = any
  std::vector<FieldDef> fields;

  /// The field's index, or -1. Case-insensitive, as SQLite column names are.
  int Find(const std::string& name) const;
  /// kInvalidArg naming the first problem: an empty or duplicate name, a
  /// `range_end` naming no date field, an enumeration with no choices, min > max.
  Status Validate() const;
  bool operator==(const FeatureSchema& o) const {
    return geometry_type == o.geometry_type && fields == o.fields;
  }
};

/// A new schema for an existing layer and where each new field's values come
/// from: `source[i]` is the index of the old field feeding field `i`, or -1
/// for a new field, which starts at its default. Old fields no entry names
/// are dropped. A field fed by an old one may be renamed, retyped and moved.
struct SchemaChange {
  FeatureSchema schema;
  std::vector<int> source;
};

/// kInvalidArg unless `change.schema` validates, keeps `old`'s geometry type,
/// and has one `source` entry per field, each -1 or a distinct old index.
Status ValidateSchemaChange(const FeatureSchema& old, const SchemaChange& change);

/// Checks one value against its field: storage type, required, choices and
/// bounds. Null passes unless the field is required.
Status ValidateValue(const FieldDef& field, const FieldValue& value);

struct Feature {
  int64_t id = 0;  ///< unique in its layer; 0 = not yet stored
  Geometry geometry;
  std::vector<FieldValue> values;  ///< parallel to the schema's fields
};

}  // namespace fv
