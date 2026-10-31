// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_layer_properties.h — the Layer Properties dialog's tabs and the
/// view-models of its General, Display and Selection tabs
/// (port/layer-manager-plan.md §4): each drafts what its tab shows, applies it
/// to the overlay or reverts it.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fvkit/feature/schema.h"
#include "fvkit/feature/style.h"
#include "fvkit/overlay/overlay.h"

namespace fv {
class FeatureOverlay;

namespace desk {

/// The Properties dialog's tabs. Values are stable; the dialog order is
/// `Desk::PropertiesTabs()`.
enum class PropertiesTab : int {
  kSource = 0,           ///< read-only: the layer and its source
  kGeneral = 1,          ///< name, description, credits, scale range
  kSymbology = 2,        ///< the layer style's renderer, classes and symbols
  kLabels = 3,           ///< the layer style's labels
  kFields = 4,           ///< the layer's fields
  kDisplay = 5,          ///< opacity, map tip field, hyperlink field
  kSelection = 6,        ///< the selection halo
  kDefinitionQuery = 7,  ///< the features the layer shows
};

/// "Source", "General", "Symbology", "Labels", "Fields", "Display",
/// "Selection", "Definition Query".
std::string PropertiesTabTitle(PropertiesTab tab);

/// Reads a scale as typed: "1:50,000", "1:50000", "50,000" or "50000", the
/// form `FormatScale` writes; "" is 0, no limit. kInvalidArg for anything else
/// or a denominator below 1.
Status ParseScale(const std::string& text, double* scale_denom);

/// What the General tab edits.
struct LayerGeneral {
  std::string name;
  std::string description;
  std::string credits;
  double min_scale = 0;  ///< zoomed furthest out (`Overlay::min_scale`); 0 = no limit
  double max_scale = 0;  ///< zoomed furthest in (`Overlay::max_scale`); 0 = no limit

  bool operator==(const LayerGeneral& o) const {
    return name == o.name && description == o.description && credits == o.credits &&
           min_scale == o.min_scale && max_scale == o.max_scale;
  }
  bool operator!=(const LayerGeneral& o) const { return !(*this == o); }
};

/// The General tab: a draft of an overlay's name, description, credits and
/// scale range.
class LayerGeneralModel {
 public:
  /// Drafts `overlay`'s current settings. kInvalidArg for null.
  static Status Open(const std::shared_ptr<Overlay>& overlay, std::shared_ptr<LayerGeneralModel>* out);

  const Overlay* overlay() const { return overlay_id_; }
  const LayerGeneral& general() const { return draft_; }
  bool dirty() const { return draft_ != applied_; }

  /// Replaces the draft. kInvalidArg for an empty name, a negative scale, or
  /// a minimum scale not smaller than the maximum (the layer would never
  /// draw); the draft is then unchanged.
  Status Set(const LayerGeneral& general);
  /// Writes the draft to the overlay. kNotFound when the overlay has closed.
  Status Apply();
  /// Back to the overlay's current settings. kNotFound when it has closed.
  Status Revert();

 private:
  LayerGeneralModel() = default;

  std::weak_ptr<Overlay> overlay_;
  const Overlay* overlay_id_ = nullptr;
  LayerGeneral draft_;
  LayerGeneral applied_;
};

/// What the Display tab edits.
struct LayerDisplay {
  int opacity = 100;            ///< percent
  std::string map_tip_field;    ///< "" shows the feature's title
  std::string hyperlink_field;  ///< "" for none

  bool operator==(const LayerDisplay& o) const {
    return opacity == o.opacity && map_tip_field == o.map_tip_field && hyperlink_field == o.hyperlink_field;
  }
  bool operator!=(const LayerDisplay& o) const { return !(*this == o); }
};

/// The Display tab: an overlay's opacity and, for a feature layer, its map
/// tip and hyperlink fields.
class LayerDisplayModel {
 public:
  /// Drafts `overlay`'s display settings. kInvalidArg for null; kUnsupported
  /// for an overlay with neither opacity nor features.
  static Status Open(const std::shared_ptr<Overlay>& overlay, std::shared_ptr<LayerDisplayModel>* out);

  const Overlay* overlay() const { return overlay_id_; }
  bool has_opacity() const { return has_opacity_; }
  /// The fields a map tip or hyperlink can name (all but blobs); empty for a
  /// layer without features, which shows no field choices.
  const std::vector<FieldDef>& fields() const { return fields_; }
  const LayerDisplay& display() const { return draft_; }
  bool dirty() const { return draft_ != applied_; }

  /// Replaces the draft. kInvalidArg for an opacity outside 0–100 or a field
  /// the layer lacks; the draft is then unchanged.
  Status Set(const LayerDisplay& display);
  /// Writes the draft to the overlay. kNotFound when it has closed.
  Status Apply();
  Status Revert();

 private:
  LayerDisplayModel() = default;

  std::weak_ptr<Overlay> overlay_;
  const Overlay* overlay_id_ = nullptr;
  bool has_opacity_ = false;
  std::vector<FieldDef> fields_;
  LayerDisplay draft_;
  LayerDisplay applied_;
};

/// What the Selection tab edits.
struct LayerSelection {
  bool use_default = true;  ///< draw with the default halo; `style` is then ignored
  FeatureSelectionStyle style;

  bool operator==(const LayerSelection& o) const {
    return use_default == o.use_default && (use_default || style == o.style);
  }
  bool operator!=(const LayerSelection& o) const { return !(*this == o); }
};

/// The Selection tab: how a feature layer draws its selected features.
class LayerSelectionModel {
 public:
  /// Drafts `overlay`'s selection style. kInvalidArg for null.
  static Status Open(const std::shared_ptr<FeatureOverlay>& overlay, std::shared_ptr<LayerSelectionModel>* out);

  const Overlay* overlay() const { return overlay_id_; }
  const LayerSelection& selection() const { return draft_; }
  bool dirty() const { return draft_ != applied_; }
  /// How many features are selected now.
  size_t selected_count() const;

  /// Replaces the draft. kInvalidArg for a width outside 0.5–40 pixels.
  Status Set(const LayerSelection& selection);
  /// Writes the draft to the overlay. kNotFound when it has closed.
  Status Apply();
  Status Revert();

 private:
  LayerSelectionModel() = default;

  std::weak_ptr<FeatureOverlay> overlay_;
  const Overlay* overlay_id_ = nullptr;
  LayerSelection draft_;
  LayerSelection applied_;
};

}  // namespace desk
}  // namespace fv
