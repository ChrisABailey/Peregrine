// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_feature_editor.h — the feature layer tool palette for desktop shells.
///
/// The `OverlayEditor` the EditorManager activates for `fv.features`. It holds
/// no editing state: every tool acts on the edited overlay's
/// `FeatureEditSession`, which also takes the gestures and keys.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "fvkit/app/editor.h"
#include "fvkit/app/type_registry.h"

namespace fv {
class FeatureOverlay;

namespace desk {

/// A shell action on the edited layer (attribute table, style, fields) that
/// the palette shows after the editing tools.
struct FeatureLayerTool {
  std::string label;
  std::string icon;
  std::function<bool()> enabled;
  std::function<void()> run;
};

class FeatureEditor : public app::OverlayEditor {
 public:
  /// `edited` answers the feature overlay being edited, or null.
  /// `edit_feature`, when set, opens the feature form on the selection.
  explicit FeatureEditor(std::function<FeatureOverlay*()> edited,
                         std::function<void()> edit_feature = nullptr);
  /// As above, with `layer_tools` listed after the deletions in place of the
  /// single Edit Feature tool.
  FeatureEditor(std::function<FeatureOverlay*()> edited, std::vector<FeatureLayerTool> layer_tools);

  Status Activate() override;
  Status Deactivate() override;
  /// The drawing tools (checked when armed), then the vertex, hole and
  /// feature deletions, the form or the layer tools, and undo and redo; each
  /// enabled only when it can act.
  std::vector<app::MenuNode> Tools() const override;

  bool active() const { return active_; }

 private:
  std::function<FeatureOverlay*()> edited_;
  std::function<void()> edit_feature_;
  std::vector<FeatureLayerTool> layer_tools_;
  bool active_ = false;
};

/// Gives the registered `fv.features` type a `FeatureEditor` over the overlay
/// `editors` is editing. kNotFound when `fv.features` is not registered.
Status RegisterFeatureEditor(app::OverlayTypeRegistry& registry, app::EditorManager& editors,
                             std::function<void()> edit_feature = nullptr);
/// As above, with the shell's layer tools on the palette.
Status RegisterFeatureEditor(app::OverlayTypeRegistry& registry, app::EditorManager& editors,
                             std::vector<FeatureLayerTool> layer_tools);

}  // namespace desk
}  // namespace fv
