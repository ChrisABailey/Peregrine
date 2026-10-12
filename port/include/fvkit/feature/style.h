// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/style.h — how a feature layer is drawn
/// (port/feature-overlay-plan.md §3).
///
/// A style picks one renderer: single (every feature alike), categorised (a
/// symbol per value of one field) or graduated (a symbol per numeric class of
/// one field). Per-feature overrides sit above the renderer. One symbol
/// carries a point, a line and an area facet so a layer of mixed geometry
/// needs one style. The style's JSON form is a MapLibre style subset
/// (`WriteFeatureStyleJson`).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"
#include "fvkit/feature/layer.h"
#include "fvkit/feature/schema.h"

namespace fv {

enum class FeatureMarker : int {
  kCircle = 0,
  kSquare = 1,
};

struct FeatureSymbol {
  // Points.
  FeatureMarker marker = FeatureMarker::kCircle;
  double marker_size_px = 9.0;
  FvColor marker_fill{220, 60, 40, 255};
  FvColor marker_outline{255, 255, 255, 255};
  double marker_outline_px = 1.0;
  // Lines. `dash` is a `line_preset` name; alpha 0 or width 0 turns the casing off.
  FvColor line_color{30, 90, 200, 255};
  double line_width_px = 2.0;
  std::string dash = "solid";
  FvColor casing_color{255, 255, 255, 0};
  double casing_px = 0.0;
  // Areas. The fill carries its own opacity in alpha.
  FvColor area_fill{30, 90, 200, 70};
  FvColor area_outline{30, 90, 200, 255};
  double area_outline_px = 1.0;

  bool operator==(const FeatureSymbol& o) const;
  bool operator!=(const FeatureSymbol& o) const { return !(*this == o); }
};

struct FeatureLabel {
  std::string field;  ///< empty = no labels
  double size_px = 12.0;
  FvColor color{20, 20, 20, 255};
  FvColor halo_color{255, 255, 255, 255};
  double halo_px = 1.0;

  bool operator==(const FeatureLabel& o) const;
};

enum class FeatureRenderer : int {
  kSingle = 0,
  kCategorised = 1,
  kGraduated = 2,
};

/// One value of a categorised field. `value` is `FeatureCategoryKey` of the
/// field value it matches.
struct FeatureCategory {
  std::string value;
  std::string label;  ///< legend text; empty = `value`
  FeatureSymbol symbol;
  bool operator==(const FeatureCategory& o) const {
    return value == o.value && label == o.label && symbol == o.symbol;
  }
};

/// One class of a graduated field: values from `lower` (inclusive) up to the
/// next class's `lower`. The last class is open-ended.
struct FeatureClass {
  double lower = 0.0;
  std::string label;  ///< legend text; empty = generated from the bounds
  FeatureSymbol symbol;
  bool operator==(const FeatureClass& o) const {
    return lower == o.lower && label == o.label && symbol == o.symbol;
  }
};

struct FeatureStyle {
  FeatureRenderer renderer = FeatureRenderer::kSingle;
  /// The single renderer's symbol; for the others, the "Other" symbol: a
  /// value matching no category, a null, or a number below the first class.
  FeatureSymbol symbol;
  std::string field;  ///< categorised and graduated
  std::vector<FeatureCategory> categories;
  std::vector<FeatureClass> classes;  ///< ascending `lower`
  /// Per-feature symbols, keyed by feature id; they win over the renderer.
  std::map<int64_t, FeatureSymbol> overrides;
  FeatureLabel label;

  /// The symbol `f` is drawn with. `field_index` is `field`'s index in the
  /// layer schema (-1 when absent, which draws everything with `symbol`).
  const FeatureSymbol& SymbolFor(const Feature& f, int field_index) const;

  bool operator==(const FeatureStyle& o) const;
  bool operator!=(const FeatureStyle& o) const { return !(*this == o); }
};

/// Follows `style`'s field references across a schema change: a renamed field
/// is renamed in the style; a deleted one turns the renderer back to single
/// (or labels off), as does retyping the graduated field to a non-number or
/// any classified field to a blob.
void RemapStyleFields(FeatureStyle* style, const FeatureSchema& old, const SchemaChange& change);

const char* ToString(FeatureRenderer renderer);  ///< "single", "categorised", "graduated"
bool FeatureRendererFromString(const std::string& name, FeatureRenderer* out);

/// A label's text for one value: text as is, numbers without trailing zeros,
/// booleans as "yes"/"no", null and blobs as "".
std::string FeatureLabelText(const FieldValue& value);

/// The text a categorised style matches a value by: text as is, numbers
/// without trailing zeros, booleans as "true"/"false"; "" for null and blobs.
std::string FeatureCategoryKey(const FieldValue& value);

/// The legend text of graduated class `i`: its label, else "lower – next
/// lower", or "≥ lower" for the last class.
std::string FeatureClassLabel(const FeatureStyle& style, size_t i);

/// What a graduated classification varies from class to class.
enum class GraduatedVary : int {
  kColor = 0,  ///< marker fill, line colour, area fill and outline, from `from` to `to`
  kSize = 1,   ///< marker size and line width, from `min_px` to `max_px`
};

enum class GraduatedMode : int {
  kEqualInterval = 0,
  kQuantile = 1,
};

struct GraduatedOptions {
  int classes = 5;
  GraduatedMode mode = GraduatedMode::kEqualInterval;
  GraduatedVary vary = GraduatedVary::kColor;
  FvColor from{255, 235, 160, 255};
  FvColor to{180, 20, 20, 255};
  double min_px = 1.0;
  double max_px = 8.0;
};

/// One category per distinct value of field `field_index`, each from `base`
/// with its colours from a fixed palette. An enumeration's categories follow
/// its choice order and take the choice labels; other fields sort by value
/// (numbers numerically). Nulls are left to the "Other" symbol.
Status ClassifyCategories(const IFeatureLayer& layer, int field_index, const FeatureSymbol& base,
                          std::vector<FeatureCategory>* out);

/// Classes over the numeric field `field_index`, each from `base` varied as
/// `options` says. Fewer classes than asked when the data has fewer distinct
/// values; kInvalidArg for a field that is not integer or real, kNotFound
/// when no feature has a value.
Status ClassifyGraduated(const IFeatureLayer& layer, int field_index, const FeatureSymbol& base,
                         const GraduatedOptions& options, std::vector<FeatureClass>* out);

/// `style` as a MapLibre style document (version 8), the form a feature
/// layer renders through `MapLibreStyleEngine` and the form it is stored in:
/// a fill, an area outline, a line casing (its width the whole casing), a
/// line, a circle, and point and shape label layers, all on source-layer
/// "features". Categorised paint values are `match` expressions over
/// `["to-string", ["get", field]]`, graduated ones `step` expressions over
/// `["get", field]`, and overrides an outer `match` on `["id"]`. Marker shape
/// and dash preset have no MapLibre property and are written as
/// `fv-circle-shape` and `fv-line-dash`; the renderer, field and class list go
/// in the root `metadata`. A nonzero `highlight_id` adds selection halo layers
/// under the rest for that feature.
Status WriteFeatureStyleJson(const FeatureStyle& style, std::string* out, int64_t highlight_id = 0);
/// As above, with halos under every feature in `highlight_ids`.
Status WriteFeatureStyleJson(const FeatureStyle& style, std::string* out,
                             const std::vector<int64_t>& highlight_ids);
/// Reads what `WriteFeatureStyleJson` writes. Properties it does not find keep
/// their defaults. kInvalidArg for malformed JSON or an expression it cannot
/// evaluate.
Status ReadFeatureStyleJson(const std::string& text, FeatureStyle* out);

/// What a feature layer says about itself, kept with its style so the file
/// carries it: a name set by hand, a description, credits and the scale range
/// it draws in (scale denominators, 0 for no limit; `Overlay::SetScaleRange`).
struct FeatureLayerInfo {
  std::string name;  ///< empty: the layer's own name
  std::string description;
  std::string credits;
  double min_scale = 0;
  double max_scale = 0;
};

/// Adds `info` to the style document in `style_json`: the name as the root
/// `name`, the rest as `fv:description`, `fv:credits`, `fv:min-scale` and
/// `fv:max-scale` in the root `metadata`. Empty texts and zero scales are left
/// out. kInvalidArg when `style_json` is not a JSON object.
Status WriteFeatureLayerInfoJson(const FeatureLayerInfo& info, std::string* style_json);
/// Reads what `WriteFeatureLayerInfoJson` writes; what is absent is empty or 0.
Status ReadFeatureLayerInfoJson(const std::string& style_json, FeatureLayerInfo* out);

}  // namespace fv
