// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_layers.h — the layers panel's view-model: the overlay stack as rows
/// the user can show, hide, fade, reorder and select
/// (port/feature-overlay-plan.md §6, FO3).
///
/// Rows are read from the stack on every call, so the model never goes stale;
/// a shell re-reads them when `Desk::LayersGeneration()` changes. Overlays of
/// hidden types (no display name) and ones the user may not control are left
/// out. Call only where the overlay stack may be changed.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "fvkit/overlay/manager.h"

namespace fv {
namespace desk {

/// One row, top of the stack first.
struct LayerRow {
  const Overlay* overlay = nullptr;  ///< identity only
  std::string name;
  std::string type_name;  ///< the type's display name
  std::string icon;       ///< the type's symbolic icon
  bool visible = true;
  bool has_opacity = false;  ///< the overlay draws with `opacity`
  int opacity = 100;
  bool current = false;
  bool dirty = false;
  bool top_most = false;  ///< pinned above the rest; cannot be moved below them
  std::string source;     ///< the file it comes from (`SplitSourceSpec`); empty without one
};

class LayersModel {
 public:
  explicit LayersModel(OverlayManager& stack) : stack_(stack) {}

  std::vector<LayerRow> Rows() const;

  /// kInvalidArg for a row past the end.
  Status SetVisible(size_t row, bool visible);
  /// Clamped to 0..100. kUnsupported for an overlay without opacity.
  Status SetOpacity(size_t row, int percent);
  /// Moves row `from` to position `to` in the rows (0 = top). Moving across
  /// the top-most set is refused by the stack.
  Status Move(size_t from, size_t to);
  /// Makes the row's overlay current, as a click in the panel does.
  Status Select(size_t row);

 private:
  /// The overlays behind `Rows()`, top first.
  std::vector<std::shared_ptr<Overlay>> Listed() const;
  Status At(size_t row, std::shared_ptr<Overlay>* out) const;

  OverlayManager& stack_;
};

}  // namespace desk
}  // namespace fv
