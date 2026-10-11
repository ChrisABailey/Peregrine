// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/schema_change.h — carrying a layer's values across a
/// `SchemaChange`: value conversion between field types, and what a change
/// would lose, so the Layer Fields dialog can ask first
/// (port/feature-overlay-plan.md §4).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "fvkit/feature/layer.h"
#include "fvkit/feature/schema.h"

namespace fv {

/// `value`, a value of field `from`, as a value of field `to`. A value of the
/// same storage type is kept; integer and real convert numerically, boolean
/// to a number as 1/0, a date to a datetime at midnight, anything to text as
/// the form shows it (a choice's value, not its label), and text to the
/// other types as the form parses typing. A value that does not convert, or
/// breaks `to`'s choices or bounds, becomes null. `*lost` is set when a
/// non-null value became null or changed (a real rounded to an integer, a
/// datetime cut to its date).
FieldValue ConvertFieldValue(const FieldDef& from, const FieldValue& value, const FieldDef& to,
                             bool* lost);

/// `in` under `change`: values moved to their new positions and converted,
/// new fields at their defaults, a null in a required field replaced by its
/// default. `lost`, when given, is sized to the new fields and counts each
/// field's lost values.
void RemapFeature(const Feature& in, const FeatureSchema& old, const SchemaChange& change,
                  Feature* out, std::vector<size_t>* lost = nullptr);

/// What applying a schema change to a layer would do to its data.
struct SchemaChangeImpact {
  /// Old fields no longer kept that hold at least one value.
  std::vector<std::string> dropped;
  /// Kept fields with values lost converting them, as "name" and count.
  std::vector<std::pair<std::string, size_t>> converted;
  /// Required fields some feature would leave empty: the change fails.
  std::vector<std::string> missing_required;

  bool LosesData() const { return !dropped.empty() || !converted.empty(); }
  /// One sentence per problem, for a confirmation or error message.
  std::string Describe() const;
};

/// Checks `change` against `layer`'s schema and measures it over every
/// feature. kInvalidArg from `ValidateSchemaChange`.
Status AssessSchemaChange(const IFeatureLayer& layer, const SchemaChange& change,
                          SchemaChangeImpact* out);

}  // namespace fv
