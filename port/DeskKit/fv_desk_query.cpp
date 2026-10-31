// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_query.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

#include <nlohmann/json.hpp>

#include "fvkit/feature/feature_filter.h"
#include "fvkit/feature/style.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/style/expression.h"

namespace fv {
namespace desk {
namespace {

using json = nlohmann::json;

/// The MapLibre operator of a comparison; null for the others.
const char* CompareOperator(QueryOp op) {
  switch (op) {
    case QueryOp::kEqual: return "==";
    case QueryOp::kNotEqual: return "!=";
    case QueryOp::kLess: return "<";
    case QueryOp::kLessEqual: return "<=";
    case QueryOp::kGreater: return ">";
    case QueryOp::kGreaterEqual: return ">=";
    default: return nullptr;
  }
}

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

bool ParseNumber(const std::string& text, double* out) {
  const std::string s = Trim(text);
  if (s.empty()) return false;
  char* end = nullptr;
  *out = std::strtod(s.c_str(), &end);
  return *end == '\0' && std::isfinite(*out);
}

Status Bad(size_t clause, const std::string& why) {
  return Status::Error(kInvalidArg, "clause " + std::to_string(clause + 1) + ": " + why);
}

Status CompileClause(const QueryClause& c, size_t index, const FeatureSchema& schema, json* out) {
  const int at = schema.Find(c.field);
  if (at < 0) return Bad(index, "the layer has no field “" + c.field + "”");
  const FieldDef& field = schema.fields[at];
  const json get = json::array({"get", field.name});
  if (c.op == QueryOp::kIsEmpty) {
    *out = json::array({"!", json::array({"has", field.name})});
    return Status::Ok();
  }
  if (c.op == QueryOp::kIsNotEmpty) {
    *out = json::array({"has", field.name});
    return Status::Ok();
  }
  const std::string value = Trim(c.value);
  if (value.empty()) return Bad(index, "“" + field.DisplayTitle() + " " + QueryOpTitle(c.op) + "” needs a value");
  if (c.op == QueryOp::kContains) {
    *out = json::array({"in", json::array({"downcase", value}), json::array({"downcase", get})});
    return Status::Ok();
  }
  json literal = value;
  if (IsNumeric(field.type) && field.role != FieldRole::kEnumeration) {
    double v = 0;
    if (!ParseNumber(value, &v)) return Bad(index, "“" + value + "” is not a number");
    literal = v;
  } else if (field.type == FieldType::kBoolean) {
    const std::string b = Lower(value);
    if (b == "true" || b == "yes" || b == "1") literal = "true";
    else if (b == "false" || b == "no" || b == "0") literal = "false";
    else return Bad(index, "“" + value + "” is not yes or no");
  }
  *out = json::array({CompareOperator(c.op), get, literal});
  return Status::Ok();
}

/// The field a `["get", name]` reads; false for anything else.
bool GetField(const json& j, std::string* field) {
  if (!j.is_array() || j.size() != 2 || j[0] != "get" || !j[1].is_string()) return false;
  *field = j[1].get<std::string>();
  return true;
}

bool ParseClause(const json& j, QueryClause* out) {
  if (!j.is_array() || j.empty() || !j[0].is_string()) return false;
  const std::string op = j[0].get<std::string>();
  if (op == "has" && j.size() == 2 && j[1].is_string()) {
    *out = QueryClause{j[1].get<std::string>(), QueryOp::kIsNotEmpty, ""};
    return true;
  }
  if (op == "!" && j.size() == 2 && j[1].is_array() && j[1].size() == 2 && j[1][0] == "has" &&
      j[1][1].is_string()) {
    *out = QueryClause{j[1][1].get<std::string>(), QueryOp::kIsEmpty, ""};
    return true;
  }
  if (op == "in" && j.size() == 3) {
    const json& needle = j[1];
    const json& hay = j[2];
    std::string field;
    if (needle.is_array() && needle.size() == 2 && needle[0] == "downcase" && needle[1].is_string() &&
        hay.is_array() && hay.size() == 2 && hay[0] == "downcase" && GetField(hay[1], &field)) {
      *out = QueryClause{field, QueryOp::kContains, needle[1].get<std::string>()};
      return true;
    }
    return false;
  }
  for (int k = 0; k < kQueryOpCount; ++k) {
    const char* name = CompareOperator(static_cast<QueryOp>(k));
    if (name == nullptr || op != name || j.size() != 3) continue;
    std::string field;
    if (!GetField(j[1], &field)) return false;
    std::string value;
    if (j[2].is_number()) value = StyleValue::Number(j[2].get<double>()).ToString();
    else if (j[2].is_string()) value = j[2].get<std::string>();
    else if (j[2].is_boolean()) value = j[2].get<bool>() ? "true" : "false";
    else return false;
    *out = QueryClause{field, static_cast<QueryOp>(k), value};
    return true;
  }
  return false;
}

}  // namespace

std::string QueryOpTitle(QueryOp op) {
  switch (op) {
    case QueryOp::kEqual: return "=";
    case QueryOp::kNotEqual: return "≠";
    case QueryOp::kLess: return "<";
    case QueryOp::kLessEqual: return "≤";
    case QueryOp::kGreater: return ">";
    case QueryOp::kGreaterEqual: return "≥";
    case QueryOp::kContains: return "contains";
    case QueryOp::kIsEmpty: return "is empty";
    case QueryOp::kIsNotEmpty: return "is not empty";
  }
  return std::string();
}

bool QueryOpTakesValue(QueryOp op) { return op != QueryOp::kIsEmpty && op != QueryOp::kIsNotEmpty; }

Status CompileFeatureQuery(const FeatureQuery& query, const FeatureSchema& schema, std::string* out) {
  out->clear();
  if (query.clauses.empty()) return Status::Ok();
  json parts = json::array();
  for (size_t i = 0; i < query.clauses.size(); ++i) {
    json c;
    Status s = CompileClause(query.clauses[i], i, schema, &c);
    if (!s.ok()) return s;
    parts.push_back(std::move(c));
  }
  if (parts.size() == 1) {
    *out = parts[0].dump();
    return Status::Ok();
  }
  parts.insert(parts.begin(), query.match_any ? "any" : "all");
  *out = parts.dump();
  return Status::Ok();
}

Status ParseFeatureQuery(const std::string& text, FeatureQuery* out) {
  if (Trim(text).empty()) {
    *out = FeatureQuery();
    return Status::Ok();
  }
  const json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded()) return Status::Error(kInvalidArg, "the query is not JSON");
  const Status unsupported = Status::Error(kUnsupported, "the query is not a list of clauses");
  FeatureQuery q;
  if (j.is_array() && !j.empty() && (j[0] == "all" || j[0] == "any")) {
    q.match_any = j[0] == "any";
    for (size_t i = 1; i < j.size(); ++i) {
      QueryClause c;
      if (!ParseClause(j[i], &c)) return unsupported;
      q.clauses.push_back(std::move(c));
    }
  } else {
    QueryClause c;
    if (!ParseClause(j, &c)) return unsupported;
    q.clauses.push_back(std::move(c));
  }
  *out = std::move(q);
  return Status::Ok();
}

std::string FeatureQueryText(const FeatureQuery& query) {
  std::string out;
  for (const QueryClause& c : query.clauses) {
    if (!out.empty()) out += query.match_any ? " or " : " and ";
    out += c.field + " " + QueryOpTitle(c.op);
    if (!QueryOpTakesValue(c.op)) continue;
    double v = 0;
    out += ParseNumber(c.value, &v) ? " " + Trim(c.value) : " “" + c.value + "”";
  }
  return out;
}

std::string SelectionMethodTitle(SelectionMethod method) {
  switch (method) {
    case SelectionMethod::kNew: return "Create a new selection";
    case SelectionMethod::kAdd: return "Add to the selection";
    case SelectionMethod::kRemove: return "Remove from the selection";
    case SelectionMethod::kWithin: return "Select from the selection";
  }
  return std::string();
}

Status QueryBuilderModel::Open(const std::shared_ptr<FeatureOverlay>& overlay, const std::string& expression,
                               std::shared_ptr<QueryBuilderModel>* out) {
  if (!overlay || !overlay->layer()) return Status::Error(kNotFound, "the layer has no features");
  std::shared_ptr<QueryBuilderModel> m(new QueryBuilderModel());
  m->overlay_ = overlay;
  m->overlay_id_ = overlay.get();
  m->schema_ = overlay->layer()->Schema();
  for (const FieldDef& f : m->schema_.fields)
    if (f.type != FieldType::kBlob) m->fields_.push_back(f);
  Status s = m->SetExpression(expression);
  if (!s.ok()) return s;
  m->MarkApplied();
  *out = std::move(m);
  return Status::Ok();
}

void QueryBuilderModel::SetMatchAny(bool any) { query_.match_any = any; }

size_t QueryBuilderModel::AddClause() {
  if (custom_) {
    custom_ = false;
    custom_text_.clear();
    query_ = FeatureQuery();
  }
  QueryClause c;
  if (!fields_.empty()) c.field = fields_.front().name;
  query_.clauses.push_back(std::move(c));
  return query_.clauses.size() - 1;
}

Status QueryBuilderModel::RemoveClause(size_t i) {
  if (i >= query_.clauses.size()) return Status::Error(kInvalidArg, "no such clause");
  query_.clauses.erase(query_.clauses.begin() + static_cast<std::ptrdiff_t>(i));
  return Status::Ok();
}

Status QueryBuilderModel::SetClause(size_t i, const QueryClause& clause) {
  if (i >= query_.clauses.size()) return Status::Error(kInvalidArg, "no such clause");
  if (schema_.Find(clause.field) < 0)
    return Status::Error(kInvalidArg, "the layer has no field “" + clause.field + "”");
  query_.clauses[i] = clause;
  return Status::Ok();
}

Status QueryBuilderModel::SetExpression(const std::string& text) {
  FeatureFilter check;
  Status s = FeatureFilter::Parse(text, &check);
  if (!s.ok()) return s;
  FeatureQuery q;
  if (ParseFeatureQuery(text, &q).ok()) {
    query_ = std::move(q);
    custom_ = false;
    custom_text_.clear();
  } else {
    query_ = FeatureQuery();
    custom_ = true;
    custom_text_ = Trim(text);
  }
  return Status::Ok();
}

Status QueryBuilderModel::Expression(std::string* json_text) const {
  if (custom_) {
    *json_text = custom_text_;
    return Status::Ok();
  }
  return CompileFeatureQuery(query_, schema_, json_text);
}

std::string QueryBuilderModel::Text() const { return custom_ ? custom_text_ : FeatureQueryText(query_); }

Status QueryBuilderModel::Matching(bool within_definition_query, std::vector<int64_t>* ids, size_t* tested) const {
  ids->clear();
  const std::shared_ptr<FeatureOverlay> o = overlay_.lock();
  if (!o || !o->layer()) return Status::Error(kNotFound, "the layer has closed");
  std::string text;
  Status s = Expression(&text);
  if (!s.ok()) return s;
  FeatureFilter filter;
  s = FeatureFilter::Parse(text, &filter);
  if (!s.ok()) return s;
  std::vector<Feature> features;
  s = within_definition_query ? o->AllShown(&features) : o->layer()->All(&features);
  if (!s.ok()) return s;
  const FeatureSchema& schema = o->layer()->Schema();
  for (const Feature& f : features)
    if (filter.Accepts(f, schema)) ids->push_back(f.id);
  if (tested != nullptr) *tested = features.size();
  return Status::Ok();
}

Status QueryBuilderModel::SampleValues(const std::string& field, size_t limit, std::vector<std::string>* out) const {
  out->clear();
  const std::shared_ptr<FeatureOverlay> o = overlay_.lock();
  if (!o || !o->layer()) return Status::Error(kNotFound, "the layer has closed");
  const FeatureSchema& schema = o->layer()->Schema();
  const int at = schema.Find(field);
  if (at < 0) return Status::Error(kInvalidArg, "the layer has no field “" + field + "”");
  std::vector<Feature> features;
  Status s = o->layer()->All(&features);
  if (!s.ok()) return s;
  std::set<std::string> seen;
  for (const Feature& f : features) {
    if (static_cast<size_t>(at) >= f.values.size()) continue;
    const std::string key = FeatureCategoryKey(f.values[at]);
    if (!key.empty()) seen.insert(key);
  }
  out->assign(seen.begin(), seen.end());
  if (IsNumeric(schema.fields[at].type)) {
    std::stable_sort(out->begin(), out->end(), [](const std::string& a, const std::string& b) {
      return std::strtod(a.c_str(), nullptr) < std::strtod(b.c_str(), nullptr);
    });
  }
  if (out->size() > limit) out->resize(limit);
  return Status::Ok();
}

bool QueryBuilderModel::dirty() const {
  if (custom_ != applied_custom_) return true;
  return custom_ ? custom_text_ != applied_text_ : query_ != applied_query_;
}

void QueryBuilderModel::MarkApplied() {
  applied_query_ = query_;
  applied_custom_ = custom_;
  applied_text_ = custom_text_;
}

void QueryBuilderModel::Revert() {
  query_ = applied_query_;
  custom_ = applied_custom_;
  custom_text_ = applied_text_;
}

}  // namespace desk
}  // namespace fv
