// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_layer_style.h — the layer style dialog's view-model: a draft of a
/// feature overlay's `FeatureStyle` with its renderer, classes, symbols and
/// legend, applied to the overlay or reverted (port/feature-overlay-plan.md
/// §3–4).
///
/// Opened on a feature it edits that feature's override instead: one entry,
/// the feature's symbol, with no renderer controls.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/raster.h"
#include "fvkit/feature/style.h"

namespace fv {
class FeatureOverlay;
class Overlay;

namespace desk {

/// A symbol property the dialog edits, in dialog order.
enum class SymbolProperty : int {
  kMarkerShape = 0,
  kMarkerSize = 1,
  kMarkerFill = 2,
  kMarkerOutline = 3,
  kMarkerOutlineWidth = 4,
  kLineColor = 5,
  kLineWidth = 6,
  kLineDash = 7,
  kCasingColor = 8,
  kCasingWidth = 9,
  kAreaFill = 10,
  kAreaOutline = 11,
  kAreaOutlineWidth = 12,
};

enum class SymbolPropertyKind : int {
  kColor = 0,   ///< a colour well with opacity
  kNumber = 1,  ///< pixels, 0–64
  kChoice = 2,  ///< a pop-up over `choices`
};

struct SymbolPropertyInfo {
  SymbolProperty id = SymbolProperty::kMarkerShape;
  SymbolPropertyKind kind = SymbolPropertyKind::kColor;
  std::string title;  ///< "Fill", "Width"
  std::string group;  ///< "Point", "Line", "Area"
  std::vector<std::string> choices;
};

/// The properties a layer of `geometry` shows: points the marker, lines the
/// line and casing, areas the fill and outline; any geometry all of them.
std::vector<SymbolPropertyInfo> SymbolPropertiesFor(GeometryType geometry);

FvColor GetSymbolColor(const FeatureSymbol& s, SymbolProperty p);
double GetSymbolNumber(const FeatureSymbol& s, SymbolProperty p);
/// The index into the property's choices.
int GetSymbolChoice(const FeatureSymbol& s, SymbolProperty p);
/// kInvalidArg for a property of another kind, a number outside 0–64 or a
/// choice out of range.
Status SetSymbolColor(FeatureSymbol* s, SymbolProperty p, FvColor c);
Status SetSymbolNumber(FeatureSymbol* s, SymbolProperty p, double v);
Status SetSymbolChoice(FeatureSymbol* s, SymbolProperty p, int choice);

/// A field the renderer can classify by.
struct StyleFieldChoice {
  std::string name;
  std::string title;
};

class LayerStyleModel {
 public:
  /// Drafts `overlay`'s style. `feature` 0 edits the layer style; otherwise
  /// that feature's override, starting from the symbol it is drawn with.
  static Status Open(const std::shared_ptr<FeatureOverlay>& overlay, int64_t feature, std::string title,
                     std::shared_ptr<LayerStyleModel>* out);

  const std::string& title() const { return title_; }
  const Overlay* overlay() const;
  int64_t feature() const { return feature_; }
  GeometryType geometry() const { return geometry_; }
  const FeatureStyle& style() const { return draft_; }
  bool dirty() const { return draft_ != applied_; }

  // MARK: Renderer
  FeatureRenderer renderer() const { return draft_.renderer; }
  /// Switching to categorised or graduated picks the first usable field
  /// when the current one is not, and classifies. kNotFound when the layer
  /// has no usable field; kUnsupported on a feature override.
  Status SetRenderer(FeatureRenderer renderer);
  /// Fields the current renderer can use: any but blobs for categorised,
  /// integer and real for graduated; empty for single.
  std::vector<StyleFieldChoice> CandidateFields() const;
  const std::string& field() const { return draft_.field; }
  /// Picks the field and classifies by it.
  Status SetField(const std::string& name);
  /// Rebuilds the classes from the data: one per value, or `graduated()`'s
  /// class count. Each starts from the Other symbol.
  Status Classify();
  const GraduatedOptions& graduated() const { return graduated_; }
  void SetGraduated(const GraduatedOptions& options) { graduated_ = options; }

  // MARK: Entries
  /// The rows the dialog and legend show: one per category or class, then
  /// "Other"; the single symbol alone; or the feature's one symbol.
  size_t EntryCount() const;
  bool EntryIsOther(size_t i) const;
  /// Legend text: a category's label or value, a class's range, "Other",
  /// "All features", or the feature's title.
  std::string EntryLabel(size_t i) const;
  /// The category value or class lower bound as text; "" for other rows.
  std::string EntryValue(size_t i) const;
  /// A default symbol past the end.
  const FeatureSymbol& EntrySymbol(size_t i) const;
  Status SetEntrySymbol(size_t i, const FeatureSymbol& symbol);
  Status SetEntryLabel(size_t i, const std::string& label);
  /// A category's value (unique, non-empty) or a class's lower bound (a
  /// number, distinct from the others; the classes are re-sorted).
  Status SetEntryValue(size_t i, const std::string& text);
  /// Adds a category or class after the last, copying its symbol.
  Status AddEntry();
  /// Removes a category or class. kInvalidArg for "Other".
  Status RemoveEntry(size_t i);
  /// Draws entry `i`'s swatch on white, `width` x `height` pixels.
  Status RenderSwatch(size_t i, int width, int height, double scale, PixelBuffer* out) const;

  // MARK: Feature override
  bool has_override() const;
  /// Drops the feature's override; the entry then shows the renderer's symbol.
  Status ClearOverride();

  // MARK: Labels
  const FeatureLabel& label() const { return draft_.label; }
  /// Fields a label can show: any but blobs.
  std::vector<StyleFieldChoice> LabelFields() const;
  /// Any field but a blob, or "" for no labels.
  Status SetLabel(const FeatureLabel& label);

  /// Writes the draft to the overlay. kNotFound when the overlay has closed.
  Status Apply();
  /// Back to the overlay's current style.
  Status Revert();

 private:
  LayerStyleModel() = default;
  /// The draft symbol entry `i` edits; null for none.
  FeatureSymbol* MutableEntry(size_t i);
  const FeatureSchema* Schema() const;

  std::weak_ptr<FeatureOverlay> overlay_;
  const Overlay* overlay_id_ = nullptr;
  std::string title_;
  int64_t feature_ = 0;
  Feature feature_data_;  ///< for the renderer's symbol when no override is set
  GeometryType geometry_ = GeometryType::kGeometry;
  FeatureStyle draft_;
  FeatureStyle applied_;
  GraduatedOptions graduated_;
};

}  // namespace desk
}  // namespace fv
