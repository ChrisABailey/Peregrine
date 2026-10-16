// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_attribute_table.h — the attribute table's view-model: every
/// feature of a layer as rows of text, sortable by a column and filtered by
/// typed text (port/feature-overlay-plan.md §4).
///
/// Rows are a snapshot taken by `Reload`, so a shell reads cells without
/// touching the layer. Call `Reload` where the layer may be read.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/feature/layer.h"

namespace fv {
class Overlay;

namespace desk {

struct TableColumn {
  int field = -1;     ///< index in the schema
  std::string title;  ///< the field's title, with its unit: "Depth (m)"
  bool numeric = false;
};

class AttributeTableModel {
 public:
  /// Reads every feature of `layer`. `overlay` identifies the layer's overlay
  /// to the Desk and is not dereferenced here.
  static Status Open(std::shared_ptr<IFeatureLayer> layer, std::string title, const Overlay* overlay,
                     std::shared_ptr<AttributeTableModel>* out);

  const std::string& title() const { return title_; }
  const Overlay* overlay() const { return overlay_; }
  const std::vector<TableColumn>& columns() const { return columns_; }

  /// Re-reads the layer, keeping the sort and filter.
  Status Reload();

  /// Rows that pass the filter, in sort order.
  size_t RowCount() const { return shown_.size(); }
  /// "" past the end.
  const std::string& Cell(size_t row, size_t column) const;
  /// 0 past the end.
  int64_t FeatureAt(size_t row) const;
  /// The row showing feature `id`; -1 when it is filtered out or absent.
  int RowOf(int64_t id) const;

  /// Sorts by `column` (-1: feature id). Numbers sort as numbers, text
  /// case-insensitively; empty cells go last either way; ties keep id order.
  void Sort(int column, bool ascending);
  int sort_column() const { return sort_column_; }
  bool ascending() const { return ascending_; }

  /// Keeps rows with a cell containing `text`, ignoring ASCII case. "" keeps all.
  void SetFilter(std::string text);
  const std::string& filter() const { return filter_; }
  /// Leaves out the features `keep` rejects, such as those a layer's
  /// definition query hides; an empty function keeps all. Takes effect at
  /// the next `Reload`.
  void SetFeatureFilter(std::function<bool(const Feature&)> keep) { keep_ = std::move(keep); }

 private:
  struct Row {
    int64_t id = 0;
    std::vector<std::string> cells;
    std::vector<double> numbers;  ///< parallel to `cells`; NaN when not numeric
  };

  AttributeTableModel() = default;
  /// Rebuilds `shown_` from `rows_`, the filter and the sort.
  void Arrange();

  std::shared_ptr<IFeatureLayer> layer_;
  const Overlay* overlay_ = nullptr;
  std::string title_;
  std::vector<TableColumn> columns_;
  std::vector<Row> rows_;
  std::vector<size_t> shown_;  ///< indexes into `rows_`
  int sort_column_ = -1;
  bool ascending_ = true;
  std::string filter_;
  std::function<bool(const Feature&)> keep_;
};

}  // namespace desk
}  // namespace fv
