// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_style_rules.h — a feature layer's style rules as rows the layer
/// manager lists under the layer, and the edits made on them
/// (port/layer-manager-plan.md §5).
///
/// Every edit goes straight to the overlay, dirties it and is pushed on the
/// undo stack. A layer drawn by a generated renderer lists the rules
/// `FeatureStyleToRules` makes of it; its first edit turns the style into
/// rules, and undoing that edit turns it back.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/app/undo.h"
#include "fvkit/feature/style.h"

namespace fv {
class FeatureOverlay;

namespace desk {

/// One rule, as listed under its layer.
struct RuleRow {
  std::string id;
  std::string type;    ///< "fill", "line", "circle", "symbol"
  std::string filter;  ///< the filter expression; empty: every feature
  std::string legend;  ///< the legend title; empty: `id`
  bool shown = true;
};

/// The geometry `overlay`'s layer declares; kGeometry (any) without a layer.
/// A generated style lists only the rules this geometry draws.
GeometryType LayerGeometry(const FeatureOverlay& overlay);

/// The rule types a rule can be made with, in menu order.
const std::vector<std::string>& RuleTypes();

/// Splits a rule filter into its geometry guard (the terms that test
/// `["geometry-type"]`, which the generators add) and the rest, the part a
/// query builder edits. Either may come back "". Text that is not JSON is all "rest".
void SplitRuleFilter(const std::string& filter, std::string* guard, std::string* rest);
/// Joins what `SplitRuleFilter` split: `["all", guard terms…, rest]`, or the one that is not "".
std::string JoinRuleFilter(const std::string& guard, const std::string& rest);

class StyleRulesModel {
 public:
  /// `changed` runs after every edit, undo and redo, with the overlay changed.
  StyleRulesModel(app::UndoStack& undo, std::function<void(FeatureOverlay&)> changed)
      : undo_(undo), changed_(std::move(changed)) {}

  /// The overlay's rules, top-drawn first; empty for an overlay without a layer.
  static std::vector<RuleRow> Rows(const FeatureOverlay& overlay);

  /// Adds a rule drawn above the rest, with a default style for `type`.
  /// kInvalidArg for an empty or taken name or an unknown type.
  Status Add(const std::shared_ptr<FeatureOverlay>& overlay, const std::string& id, const std::string& type);
  /// Copies a rule directly above itself as "<id> copy"; `copy_id` receives its name.
  Status Duplicate(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, std::string* copy_id = nullptr);
  Status Rename(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, const std::string& id);
  /// Gives the rule the default paint and layout of `type`, keeping its
  /// colour, filter, scale range, visibility and legend title.
  Status SetType(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, const std::string& type);
  /// kInvalidArg, and no change, for an expression the engine cannot evaluate.
  Status SetFilter(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, const std::string& filter);
  Status SetShown(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, bool shown);
  /// Gives the rule `style`'s paint, layout, legend title and zoom range,
  /// keeping its name, type, filter and visibility.
  Status SetStyle(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, const StyleRule& style);
  /// Copies a line rule directly beneath itself as "<id> casing", drawn grey
  /// and 2 px wider (each stop of a zoom-varying width). kInvalidArg for
  /// another type; `casing_id` receives the copy's name.
  Status AddCasing(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, std::string* casing_id = nullptr);
  /// Moves row `from` to row `to` (0 = top-drawn).
  Status Move(const std::shared_ptr<FeatureOverlay>& overlay, size_t from, size_t to);
  Status Delete(const std::shared_ptr<FeatureOverlay>& overlay, size_t row);
  /// Replaces the whole style, generated or rules, as one undoable `label`.
  /// kInvalidArg, and no change, for a style the engine cannot draw.
  Status Replace(const std::shared_ptr<FeatureOverlay>& overlay, const FeatureStyle& style,
                 const std::string& label);

 private:
  /// The overlay's style as rules, with `row` checked against them.
  static Status RulesOf(const FeatureOverlay& overlay, size_t row, FeatureStyle* out);
  /// Sets `after` on the overlay when it writes, dirties it and pushes `label`.
  Status Commit(const std::shared_ptr<FeatureOverlay>& overlay, FeatureStyle after, const std::string& label);
  /// Applies `change` to rule row `row` (list index `at`) and commits the result as `label`.
  Status Edit(const std::shared_ptr<FeatureOverlay>& overlay, size_t row, const std::string& label,
              const std::function<Status(FeatureStyle& style, size_t at)>& change);

  app::UndoStack& undo_;
  std::function<void(FeatureOverlay&)> changed_;
};

}  // namespace desk
}  // namespace fv
