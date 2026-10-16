// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/style.h — how a feature layer is drawn
/// (port/feature-overlay-plan.md §3).
///
/// A style picks one renderer: single (every feature alike), categorised (a
/// symbol per value of one field), graduated (a symbol per numeric class of
/// one field) or rules (an ordered list of MapLibre style layers, each with
/// its own filter; port/layer-manager-plan.md §5). Per-feature overrides sit
/// above the renderer. One symbol carries a point, a line and an area facet
/// so a layer of mixed geometry needs one style. The style's JSON form is a
/// MapLibre style document (`WriteFeatureStyleJson`).
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
  kRules = 3,
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

/// One rule of a rules style: a MapLibre style layer drawn over the layer's
/// features. JSON members are held as compact JSON text so any expression the
/// engine evaluates survives a save, including ones no editor shows.
struct StyleRule {
  std::string id;              ///< unique within the style
  std::string type = "line";   ///< "fill", "line", "circle", "symbol"; "background" is kept as read
  std::string filter;          ///< a `FeatureFilter` expression; empty: every feature. No legacy filter syntax
  std::string paint = "{}";    ///< the `paint` object
  std::string layout = "{}";   ///< the `layout` object, without `visibility`
  double min_zoom = 0;         ///< `minzoom`; 0: none
  double max_zoom = 24;        ///< `maxzoom`; 24: none
  bool shown = true;           ///< false writes `"visibility": "none"`
  std::string legend;          ///< `metadata.fv:legend`; empty: the legend shows `id`
  /// Which facet of a per-feature override symbol the rule takes
  /// (`metadata.fv:facet`): "fv-fill", "fv-area-outline", "fv-line-casing",
  /// "fv-line" or "fv-circle". Empty: from `type` (fill, line, circle), none for symbol.
  std::string facet;
  std::string extra = "{}";    ///< every other member of the layer, as read

  bool operator==(const StyleRule& o) const;
  bool operator!=(const StyleRule& o) const { return !(*this == o); }
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
  /// Labels of the generated renderers. Under rules only `label.field` is
  /// read, from the first symbol rule whose `text-field` is `["get", field]`;
  /// it names features (`FeatureOverlay::FeatureTitle`), and rules draw labels.
  FeatureLabel label;
  /// The rules, first drawn first (kRules only).
  std::vector<StyleRule> rules;

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

const char* ToString(FeatureRenderer renderer);  ///< "single", "categorised", "graduated", "rules"
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

/// `style` as a rules style that draws the same: one rule per MapLibre layer
/// `WriteFeatureStyleJson` writes for it ("Areas", "Area outlines", "Line
/// casings", "Lines", "Points", "Point labels", "Labels"), each but the labels
/// with its facet set. Overrides are carried over. A rules style is returned
/// unchanged. A `geometry` other than kGeometry keeps only the rules that
/// geometry draws: points keep "Points" and "Point labels", lines "Line
/// casings", "Lines" and "Labels", areas "Areas", "Area outlines" and "Labels".
Status FeatureStyleToRules(const FeatureStyle& style, FeatureStyle* out,
                           GeometryType geometry = GeometryType::kGeometry);

/// A categorised `style` as rules with one rule per value instead of one
/// `match` rule: each generated rule whose paint varies by category becomes a
/// rule per category, filtered by `["==", ["to-string", ["get", field]],
/// value]` and titled in the legend by the category's label, then an "Other"
/// rule beneath for the rest. Rules that do not vary, and labels, stay one
/// rule. `geometry` as for `FeatureStyleToRules`; kInvalidArg for a style
/// that is not categorised.
Status CategoriesToRules(const FeatureStyle& style, FeatureStyle* out,
                         GeometryType geometry = GeometryType::kGeometry);

/// One legend entry of a rules style: a title and the rules it draws.
struct RuleLegendEntry {
  std::string title;
  std::vector<size_t> rules;  ///< indexes into `FeatureStyle::rules`, first drawn first
};

/// The legend of a rules style, top-drawn entry first: one entry per shown
/// rule, titled by its legend title or else its id. Rules that share a legend
/// title are one entry, placed at the topmost of them. Empty for any other renderer.
std::vector<RuleLegendEntry> RuleLegend(const FeatureStyle& style);

/// One sentence per thing in `style` that `change` breaks: rules whose filter
/// or properties read a field it renames or deletes, and a classified
/// renderer or labels whose field it deletes or leaves unusable. Empty when
/// nothing breaks. Field references `RemapStyleFields` follows are not listed.
std::vector<std::string> StyleBreaksFromSchemaChange(const FeatureStyle& style, const FeatureSchema& old,
                                                     const SchemaChange& change);

/// `style` as a style.json for use outside the layer: MapLibre loads it, and
/// `ReadFeatureStyleJson` reads it back as `style` without its per-feature
/// overrides. Paint and layout properties MapLibre does not define
/// (`fv-circle-shape`, `fv-line-dash`) move to each layer's `metadata` as
/// `fv:paint` and `fv:layout`, where the reader finds them. A style that
/// draws text names MapLibre's demo glyph server as its `glyphs`.
Status ExportFeatureStyleJson(const FeatureStyle& style, std::string* out);
/// Reads a style.json from outside the layer: `ReadFeatureStyleJson`, but
/// kInvalidArg for a document that is not a MapLibre style (no `"version": 8`
/// or no `layers` array) rather than reading it as the default style.
Status ImportFeatureStyleJson(const std::string& text, FeatureStyle* out);

/// How selected features are drawn: a halo of `color` under each facet,
/// `width_px` wider than an area outline or a marker's diameter and
/// `width_px + 2` wider than a line.
struct FeatureSelectionStyle {
  FvColor color{255, 200, 0, 255};
  double width_px = 4.0;

  bool operator==(const FeatureSelectionStyle& o) const {
    return color.r == o.color.r && color.g == o.color.g && color.b == o.color.b && color.a == o.color.a &&
           width_px == o.width_px;
  }
  bool operator!=(const FeatureSelectionStyle& o) const { return !(*this == o); }
};

/// As above, with halos drawn as `selection` says under every feature in
/// `highlight_ids`. Under rules each fill, line, circle and symbol rule has a
/// halo of its own with its filter and zoom range, under every rule; an
/// override's values wrap the rule's paint values in a `match` on `["id"]`,
/// recorded in the rule's `metadata.fv:overridden`.
Status WriteFeatureStyleJson(const FeatureStyle& style, std::string* out,
                             const std::vector<int64_t>& highlight_ids,
                             const FeatureSelectionStyle& selection = FeatureSelectionStyle());
/// Reads what `WriteFeatureStyleJson` writes. Properties it does not find keep
/// their defaults. A document without `metadata.fv:renderer` whose layers are
/// not all the generated ones (`fv-fill`, `fv-line`, …) reads as rules, so a
/// plain MapLibre style.json reads; each layer's `source` and `source-layer`
/// are dropped, since every rule draws the layer's own features. kInvalidArg
/// for malformed JSON, an expression it cannot evaluate, a layer type the
/// engine does not draw, two rules of one id, or a rule in the legacy syntax
/// (a legacy filter or a `{"stops": …}` function); `WriteFeatureStyleJson`
/// refuses the same. A layer's `metadata.fv:paint` and `fv:layout`
/// (`ExportFeatureStyleJson`) are put back in its paint and layout.
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
  std::string map_tip_field;    ///< the field a hover shows; empty: the feature's title
  std::string hyperlink_field;  ///< the field holding a feature's link; empty: none
  bool has_selection_style = false;  ///< false: the default selection halo
  FeatureSelectionStyle selection;
  std::string definition_query;  ///< a `FeatureFilter` expression; empty: every feature
};

/// Adds `info` to the style document in `style_json`: the name as the root
/// `name`, the rest as `fv:description`, `fv:credits`, `fv:min-scale`,
/// `fv:max-scale`, `fv:map-tip-field`, `fv:hyperlink-field`,
/// `fv:selection-color`, `fv:selection-width` and `fv:definition-query` (the
/// expression as JSON) in the root `metadata`. Empty texts, zero scales and a
/// default selection are left out. kInvalidArg when `style_json` is not a JSON
/// object or the definition query is not JSON.
Status WriteFeatureLayerInfoJson(const FeatureLayerInfo& info, std::string* style_json);
/// Reads what `WriteFeatureLayerInfoJson` writes; what is absent is empty or 0.
Status ReadFeatureLayerInfoJson(const std::string& style_json, FeatureLayerInfo* out);

}  // namespace fv
