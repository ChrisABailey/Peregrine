// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/feature_filter.h — a MapLibre filter expression evaluated
/// over a feature, as a layer's definition query and a selection query use it
/// (port/layer-manager-plan.md §4). A feature reaches the expression as the
/// renderer sees it (`FeatureToVector`): each non-null field as the text of
/// `FeatureCategoryKey`, so a filter keeps the same features on the map and in
/// a search or table.
#pragma once

#include <string>

#include "fvkit/feature/layer.h"
#include "fvkit/style/expression.h"

namespace fv {

class FeatureFilter {
 public:
  /// Parses `json`, a MapLibre expression that yields a boolean; "" (or only
  /// white space) is the empty filter, which accepts every feature. The
  /// parser's error for anything else.
  static Status Parse(const std::string& json, FeatureFilter* out);

  bool empty() const { return expr_.empty(); }
  /// The JSON the filter was parsed from; "" for the empty filter.
  const std::string& text() const { return text_; }

  /// True when the expression yields `true` for `f`; anything else (false,
  /// null, a non-boolean) rejects it.
  bool Accepts(const Feature& f, const FeatureSchema& schema) const;

 private:
  StyleExpression expr_;
  std::string text_;
};

}  // namespace fv
