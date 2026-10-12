// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_layer_properties.h — the Layer Properties dialog's tabs and its
/// General tab's view-model (port/layer-manager-plan.md §4): a layer's name,
/// description, credits and the scale range it draws in, drafted and applied
/// to the overlay or reverted.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fvkit/overlay/overlay.h"

namespace fv {
namespace desk {

/// The Properties dialog's tabs, in dialog order.
enum class PropertiesTab : int {
  kSource = 0,     ///< read-only: the layer and its source
  kGeneral = 1,    ///< name, description, credits, scale range
  kSymbology = 2,  ///< the layer style's renderer, classes and symbols
  kLabels = 3,     ///< the layer style's labels
  kFields = 4,     ///< the layer's fields
};

/// "Source", "General", "Symbology", "Labels", "Fields".
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

}  // namespace desk
}  // namespace fv
