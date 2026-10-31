// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_attribute_table.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "fvkit/feature/value_text.h"

namespace fv {
namespace desk {

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/// <0, 0, >0 ignoring ASCII case.
int CompareText(const std::string& a, const std::string& b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    const int ca = std::tolower(static_cast<unsigned char>(a[i]));
    const int cb = std::tolower(static_cast<unsigned char>(b[i]));
    if (ca != cb) return ca < cb ? -1 : 1;
  }
  return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

}  // namespace

Status AttributeTableModel::Open(std::shared_ptr<IFeatureLayer> layer, std::string title, const Overlay* overlay,
                                 std::shared_ptr<AttributeTableModel>* out) {
  if (!layer) return Status::Error(kInvalidArg, "no feature layer");
  std::shared_ptr<AttributeTableModel> m(new AttributeTableModel());
  m->layer_ = std::move(layer);
  m->title_ = std::move(title);
  m->overlay_ = overlay;
  Status s = m->Reload();
  if (!s.ok()) return s;
  *out = std::move(m);
  return Status::Ok();
}

Status AttributeTableModel::Reload() {
  std::vector<Feature> features;
  Status s = layer_->All(&features);
  if (!s.ok()) return s;
  if (keep_) features.erase(std::remove_if(features.begin(), features.end(), [this](const Feature& f) { return !keep_(f); }),
                            features.end());
  const FeatureSchema& schema = layer_->Schema();
  columns_.clear();
  for (size_t i = 0; i < schema.fields.size(); ++i) {
    const FieldDef& f = schema.fields[i];
    if (f.type == FieldType::kBlob) continue;
    TableColumn c;
    c.field = static_cast<int>(i);
    c.title = f.unit.empty() ? f.DisplayTitle() : f.DisplayTitle() + " (" + f.unit + ")";
    c.numeric = (f.type == FieldType::kInteger || f.type == FieldType::kReal) && f.role != FieldRole::kEnumeration;
    columns_.push_back(std::move(c));
  }
  // The layer's fields may have changed; a sort column that is gone sorts by id.
  if (sort_column_ >= static_cast<int>(columns_.size())) sort_column_ = -1;
  rows_.clear();
  rows_.reserve(features.size());
  for (const Feature& f : features) {
    Row r;
    r.id = f.id;
    for (const TableColumn& c : columns_) {
      const FieldValue& v = static_cast<size_t>(c.field) < f.values.size() ? f.values[c.field] : FieldValue{};
      r.cells.push_back(FormatFieldValue(schema.fields[c.field], v));
      double x = std::nan("");
      if (const auto* i = std::get_if<int64_t>(&v)) x = static_cast<double>(*i);
      else if (const auto* d = std::get_if<double>(&v)) x = *d;
      r.numbers.push_back(x);
    }
    rows_.push_back(std::move(r));
  }
  if (sort_column_ >= static_cast<int>(columns_.size())) sort_column_ = -1;
  Arrange();
  return Status::Ok();
}

const std::string& AttributeTableModel::Cell(size_t row, size_t column) const {
  static const std::string kEmpty;
  if (row >= shown_.size()) return kEmpty;
  const Row& r = rows_[shown_[row]];
  return column < r.cells.size() ? r.cells[column] : kEmpty;
}

int64_t AttributeTableModel::FeatureAt(size_t row) const { return row < shown_.size() ? rows_[shown_[row]].id : 0; }

int AttributeTableModel::RowOf(int64_t id) const {
  for (size_t i = 0; i < shown_.size(); ++i)
    if (rows_[shown_[i]].id == id) return static_cast<int>(i);
  return -1;
}

void AttributeTableModel::Sort(int column, bool ascending) {
  sort_column_ = column >= 0 && column < static_cast<int>(columns_.size()) ? column : -1;
  ascending_ = ascending;
  Arrange();
}

void AttributeTableModel::SetFilter(std::string text) {
  filter_ = std::move(text);
  Arrange();
}

void AttributeTableModel::Arrange() {
  shown_.clear();
  const std::string needle = Lower(filter_);
  for (size_t i = 0; i < rows_.size(); ++i) {
    const Row& r = rows_[i];
    const bool keep = needle.empty() || std::any_of(r.cells.begin(), r.cells.end(), [&](const std::string& c) {
                        return Lower(c).find(needle) != std::string::npos;
                      });
    if (keep) shown_.push_back(i);
  }
  const int col = sort_column_;
  const bool numeric = col >= 0 && columns_[col].numeric;
  auto less = [&](size_t ia, size_t ib) {
    const Row& a = rows_[ia];
    const Row& b = rows_[ib];
    if (col < 0) return ascending_ ? a.id < b.id : a.id > b.id;
    const bool a_empty = numeric ? std::isnan(a.numbers[col]) : a.cells[col].empty();
    const bool b_empty = numeric ? std::isnan(b.numbers[col]) : b.cells[col].empty();
    if (a_empty != b_empty) return b_empty;  // empty last in either direction
    int c = 0;
    if (!a_empty) {
      c = numeric ? (a.numbers[col] < b.numbers[col] ? -1 : a.numbers[col] > b.numbers[col] ? 1 : 0)
                  : CompareText(a.cells[col], b.cells[col]);
      if (!ascending_) c = -c;
    }
    return c != 0 ? c < 0 : a.id < b.id;
  };
  std::sort(shown_.begin(), shown_.end(), less);
}

}  // namespace desk
}  // namespace fv
