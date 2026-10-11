// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/style/expression.h — MapLibre style expressions, parsed once and
/// evaluated per feature.
///
/// The one expression evaluator for every MapLibre-style front end: the OSM
/// style loader (filters, paint and layout values) and the feature layer style
/// JSON. The supported subset is listed in `StyleExpression::Parse`; anything
/// else is a parse failure naming the operator, never a silent skip.
///
/// Deviations from MapLibre, all because the vector seam carries attributes as
/// text: equality and `match` compare a string with a number numerically when
/// the string parses as one, and numeric operators accept such strings.
/// Colours interpolate per straight (not premultiplied) RGBA channel.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"
#include "fvkit/geo.h"

namespace fv {

/// A value an expression reads or produces.
struct StyleValue {
  enum class Type { kNull, kBool, kNumber, kString, kColor, kArray };
  Type type = Type::kNull;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  FvColor color;
  std::vector<StyleValue> array;

  static StyleValue Null() { return StyleValue(); }
  static StyleValue Bool(bool v);
  static StyleValue Number(double v);
  static StyleValue String(std::string v);
  static StyleValue Color(const FvColor& v);

  bool is_null() const { return type == Type::kNull; }
  /// A number, or a string that parses entirely as one.
  bool AsNumber(double* out) const;
  /// A colour, or a string that parses as a CSS colour.
  bool AsColor(FvColor* out) const;
  /// MapLibre `to-string`: numbers in shortest form, colours as rgba(), null as "".
  std::string ToString() const;
};

/// MapLibre's equality: same type and value, plus the string/number leniency
/// described in the file comment.
bool StyleValuesEqual(const StyleValue& a, const StyleValue& b);

/// What an expression reads from the feature being styled.
struct StyleExpressionInput {
  /// Property lookup for `get` and `has`; false when the feature lacks `key`.
  std::function<bool(const std::string& key, StyleValue* out)> get;
  bool has_id = false;
  int64_t id = 0;
  std::string geometry_type;  ///< "Point", "LineString" or "Polygon"; "" = unknown
  double zoom = 0.0;
};

/// A parsed expression. Copies share the compiled tree.
class StyleExpression {
 public:
  /// Parses a JSON value: a literal (number, string, boolean, null) or an
  /// expression array. Supported operators: literal, get, has, id,
  /// geometry-type, zoom; to-string, to-number, to-boolean, to-color, string,
  /// number, boolean; ==, !=, <, <=, >, >=, !, all, any, in; match, case,
  /// coalesce, step, interpolate (linear, exponential); concat, downcase,
  /// upcase; +, -, *, /, %, min, max; rgb, rgba. kUnsupported names an
  /// operator outside that list, kInvalidArg a malformed one.
  static Status Parse(const std::string& json_text, StyleExpression* out);

  /// A constant value as an expression.
  static StyleExpression Constant(const StyleValue& v);

  bool empty() const { return root_ == nullptr; }
  /// True when evaluation reads nothing from the feature or the zoom.
  bool is_constant() const;
  /// True when evaluation reads the zoom.
  bool reads_zoom() const;
  /// The JSON the expression was parsed from.
  const std::string& text() const { return text_; }

  /// Evaluates for one feature. A runtime type error yields null, which a
  /// caller treats as "use the property's default", as MapLibre does.
  StyleValue Evaluate(const StyleExpressionInput& in) const;

  struct Node;

 private:
  std::shared_ptr<const Node> root_;
  std::string text_;
};

/// The CSS colour forms a MapLibre style uses: #rgb, #rgba, #rrggbb,
/// #rrggbbaa, rgb(), rgba(), hsl(), hsla(), and the names white, black and
/// transparent.
bool ParseCssColor(const std::string& text, FvColor* out);

}  // namespace fv
