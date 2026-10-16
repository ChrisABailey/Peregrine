// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_query.h — the query builder behind Select by Attributes and a
/// layer's Definition Query tab (port/layer-manager-plan.md §4).
///
/// A query is a list of clauses (field, operator, value) joined by "and" or
/// "or". It compiles to a MapLibre filter expression, the form
/// `fv::FeatureFilter` evaluates and a layer stores, and an expression of that
/// shape reads back into clauses. Any other expression is kept as typed: the
/// builder shows it as custom text.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/feature/schema.h"

namespace fv {
class FeatureOverlay;
class Overlay;

namespace desk {

/// A clause's comparison, in pop-up order.
enum class QueryOp : int {
  kEqual = 0,
  kNotEqual = 1,
  kLess = 2,
  kLessEqual = 3,
  kGreater = 4,
  kGreaterEqual = 5,
  kContains = 6,    ///< text containing the value, ignoring case
  kIsEmpty = 7,     ///< no value; takes none
  kIsNotEmpty = 8,  ///< any value; takes none
};
constexpr int kQueryOpCount = 9;

/// "=", "≠", "<", "≤", ">", "≥", "contains", "is empty", "is not empty".
std::string QueryOpTitle(QueryOp op);
/// False for the empty tests.
bool QueryOpTakesValue(QueryOp op);

struct QueryClause {
  std::string field;
  QueryOp op = QueryOp::kEqual;
  std::string value;  ///< as typed; numbers for a numeric field, "true"/"false" for a boolean

  bool operator==(const QueryClause& o) const { return field == o.field && op == o.op && value == o.value; }
  bool operator!=(const QueryClause& o) const { return !(*this == o); }
};

struct FeatureQuery {
  bool match_any = false;  ///< false: every clause ("and"); true: any clause ("or")
  std::vector<QueryClause> clauses;

  bool operator==(const FeatureQuery& o) const { return match_any == o.match_any && clauses == o.clauses; }
  bool operator!=(const FeatureQuery& o) const { return !(*this == o); }
};

/// The query as a MapLibre filter expression; "" for no clauses. kInvalidArg
/// names the first clause on a field `schema` lacks, a missing value, or a
/// value a numeric or boolean field cannot hold.
Status CompileFeatureQuery(const FeatureQuery& query, const FeatureSchema& schema, std::string* json);
/// Reads what `CompileFeatureQuery` writes; "" is no clauses. kUnsupported for
/// an expression of any other shape, kInvalidArg for text that is not JSON.
Status ParseFeatureQuery(const std::string& json, FeatureQuery* out);
/// The query as a sentence: `depth > 7 and name contains "ash"`; "" for none.
std::string FeatureQueryText(const FeatureQuery& query);

/// How Select by Attributes combines the matches with the current selection.
enum class SelectionMethod : int {
  kNew = 0,     ///< the matches become the selection
  kAdd = 1,     ///< added to it
  kRemove = 2,  ///< taken out of it
  kWithin = 3,  ///< the selected features that also match
};

/// "Create a new selection", "Add to the selection", ...
std::string SelectionMethodTitle(SelectionMethod method);

/// A query being built over one feature layer.
class QueryBuilderModel {
 public:
  /// A builder over `overlay`'s fields, starting from `expression` (a stored
  /// definition query, or "" for none). An expression not of the builder's
  /// shape opens as custom text. kNotFound without a layer; the parser's
  /// error for an expression that does not parse.
  static Status Open(const std::shared_ptr<FeatureOverlay>& overlay, const std::string& expression,
                     std::shared_ptr<QueryBuilderModel>* out);

  const Overlay* overlay() const { return overlay_id_; }
  /// The fields a clause can test (all but blobs), in schema order.
  const std::vector<FieldDef>& fields() const { return fields_; }

  const FeatureQuery& query() const { return query_; }
  /// True while the draft is an expression typed by hand rather than clauses.
  bool custom() const { return custom_; }
  /// The custom expression; "" when the draft is clauses.
  const std::string& custom_text() const { return custom_text_; }

  void SetMatchAny(bool any);
  /// Appends a clause on the first field; returns its index.
  size_t AddClause();
  Status RemoveClause(size_t i);
  /// Replaces clause `i`; kInvalidArg for a field the layer lacks. Values are
  /// checked when the query compiles.
  Status SetClause(size_t i, const QueryClause& clause);
  /// Replaces the draft with an expression typed by hand: clauses when it has
  /// the builder's shape, else custom text. "" clears it. On a parse error the
  /// draft is unchanged.
  Status SetExpression(const std::string& json);

  /// The draft as a filter expression; "" for none. The compiler's error for
  /// an incomplete clause.
  Status Expression(std::string* json) const;
  /// The draft as a sentence, or the custom expression.
  std::string Text() const;
  /// The features the draft accepts, by id, in layer order, and how many
  /// features were tested. `within_definition_query` tests only the features
  /// the layer's definition query shows.
  Status Matching(bool within_definition_query, std::vector<int64_t>* ids, size_t* tested) const;
  /// Up to `limit` distinct values of `field`, sorted (numbers numerically),
  /// as a clause's value would be typed.
  Status SampleValues(const std::string& field, size_t limit, std::vector<std::string>* out) const;

  /// True when the draft differs from the expression it opened with or last
  /// applied (`MarkApplied`).
  bool dirty() const;
  /// Records the draft as applied.
  void MarkApplied();
  /// Back to the applied expression.
  void Revert();

 private:
  QueryBuilderModel() = default;

  std::weak_ptr<FeatureOverlay> overlay_;
  const Overlay* overlay_id_ = nullptr;
  FeatureSchema schema_;
  std::vector<FieldDef> fields_;
  FeatureQuery query_;
  bool custom_ = false;
  std::string custom_text_;
  FeatureQuery applied_query_;
  bool applied_custom_ = false;
  std::string applied_text_;
};

}  // namespace desk
}  // namespace fv
