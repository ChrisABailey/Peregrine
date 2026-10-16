// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_rule_style.h — the style pages of one style rule: its paint and
/// layout properties by type, each a constant, varying by a field or by zoom,
/// or an expression typed by hand (port/layer-manager-plan.md §5).
///
/// A draft over the rule, applied through `StyleRulesModel::SetStyle`. A
/// property the pages cannot show reads as an expression; a property left
/// unedited is written back exactly as it was read.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/feature/style.h"
#include "fvkit/geo.h"

namespace fv {
class FeatureOverlay;

namespace desk {

/// What a property's constant is.
enum class RulePropertyKind : int {
  kColor = 0,   ///< a CSS colour, "#rrggbb" or "rgba(r, g, b, a)"
  kNumber = 1,  ///< a number in `min`..`max`
  kChoice = 2,  ///< one of `choices`
  kText = 3,    ///< a sprite image name
  kField = 4,   ///< a field name; the constant is `["get", field]`
  kDash = 5,    ///< numbers separated by spaces, in line widths; constant only
  kOffset = 6,  ///< "x y" in ems, +y down; constant only
};

/// How a property gets its value.
enum class RuleValueMode : int {
  kDefault = 0,     ///< absent: the engine's default
  kConstant = 1,
  kByField = 2,     ///< `match` on a field's value, `constant` for the rest
  kByClass = 3,     ///< `step` on a numeric field, `constant` below the first class
  kByZoom = 4,      ///< `interpolate` linearly over zoom
  kExpression = 5,  ///< any expression, as typed
};

/// One property of a rule type's style pages.
struct RuleProperty {
  std::string key;    ///< the MapLibre property name
  bool layout = false;
  std::string page;   ///< "Fill", "Outline", "Line", "Circle", "Stroke", "Icon", "Text"
  std::string title;
  RulePropertyKind kind = RulePropertyKind::kNumber;
  double min = 0;
  double max = 0;
  std::vector<std::string> choices;  ///< kChoice
  /// The modes the property offers, in pop-up order.
  std::vector<RuleValueMode> modes;
  /// The constant a value starts from when it has none: the engine's default.
  std::string initial;
};

/// The properties a rule of `type` shows, in page order; empty for an unknown type.
const std::vector<RuleProperty>& RuleProperties(const std::string& type);

/// One row of a field or zoom table: a field value, a class's lower bound or
/// a zoom level, and the constant there.
struct RuleStop {
  std::string key;
  std::string value;
  bool operator==(const RuleStop& o) const { return key == o.key && value == o.value; }
};

/// A property's value as the pages show it.
struct RulePropertyValue {
  RuleValueMode mode = RuleValueMode::kDefault;
  std::string constant;  ///< kConstant; the other value of kByField, the base of kByClass
  std::string field;     ///< kByField, kByClass
  std::vector<RuleStop> stops;
  std::string expression;  ///< kExpression: compact JSON
};

/// Reads a JSON property value into the form the pages show. A value no
/// other mode fits reads as kExpression with its JSON unchanged.
RulePropertyValue ReadRuleValue(const RuleProperty& property, const std::string& json_text);
/// The JSON for `value`; "" for kDefault. kInvalidArg naming the problem.
Status WriteRuleValue(const RuleProperty& property, const RulePropertyValue& value, std::string* json_text);

class RuleStyleModel {
 public:
  /// A draft over the rule named `rule_id` of `overlay`'s style.
  static Status Open(const std::shared_ptr<FeatureOverlay>& overlay, const std::string& rule_id,
                     std::shared_ptr<RuleStyleModel>* out);

  const FeatureOverlay* overlay() const { return overlay_.get(); }
  /// The rule's name; it follows a rename made through this model only.
  const std::string& rule_id() const { return rule_id_; }
  const std::string& type() const { return rule_.type; }
  std::string Title() const;
  bool dirty() const { return draft_ != rule_ || !edited_.empty(); }

  const std::vector<RuleProperty>& properties() const { return RuleProperties(rule_.type); }
  const RulePropertyValue& value(size_t k) const { return values_[k]; }
  /// Replaces property `k`'s value. kInvalidArg, and no change, for a bad
  /// number or colour or a mode the property lacks. An incomplete table or
  /// expression is kept and refused by `Draft`.
  Status SetValue(size_t k, const RulePropertyValue& value);
  /// Switches property `k` to `mode`, carrying its current constant across:
  /// a field or zoom table starts from it, a constant takes the first value,
  /// and an expression starts from the JSON the old mode writes.
  Status SetMode(size_t k, RuleValueMode mode);
  /// Fills property `k`'s field table with the field's distinct values (at
  /// most 50, sorted), each given the current `constant`. kByField only.
  Status FillFieldValues(size_t k);
  /// kInvalidArg with the engine's message when `expression` cannot be
  /// evaluated as property `k`'s value.
  Status Verify(size_t k, const std::string& expression) const;

  /// Fields a value can vary by (any but blobs); `numeric` keeps integers and reals only.
  std::vector<std::string> Fields(bool numeric) const;

  const std::string& legend() const { return draft_.legend; }
  void SetLegend(const std::string& legend) { draft_.legend = legend; }
  double min_zoom() const { return draft_.min_zoom; }
  double max_zoom() const { return draft_.max_zoom; }
  /// kInvalidArg unless 0 <= min < max <= 24.
  Status SetZoomRange(double min_zoom, double max_zoom);

  /// The rule with the draft's paint, layout, legend and zoom range.
  /// kInvalidArg when a property does not write.
  Status Draft(StyleRule* out) const;
  /// Rereads the rule from the overlay, dropping the draft. kNotFound when it is gone.
  Status Revert();

 private:
  RuleStyleModel() = default;
  void Load(const StyleRule& rule);
  void MarkEdited(size_t k);

  std::shared_ptr<FeatureOverlay> overlay_;
  std::string rule_id_;
  StyleRule rule_;   ///< as last read or applied
  StyleRule draft_;  ///< legend and zoom range; paint and layout come from `values_`
  std::vector<RulePropertyValue> values_;
  std::vector<size_t> edited_;  ///< properties changed since the last read, in edit order
};

}  // namespace desk
}  // namespace fv
