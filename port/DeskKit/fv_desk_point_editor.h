// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_point_editor.h — the points tool palette for desktop shells.
///
/// The `OverlayEditor` the EditorManager activates for `fv.points`; the C++
/// form of points.py's `PointEditor`. It holds no editing state: every tool
/// acts on the edited overlay's `PointEditSession`, which also takes the
/// gestures and keys.
#pragma once

#include <functional>
#include <vector>

#include "fvkit/app/editor.h"
#include "fvkit/app/type_registry.h"

namespace fv {
class PointOverlay;

namespace desk {

class PointEditor : public app::OverlayEditor {
 public:
  /// `edited` answers the point set being edited, or null. `edit_point`,
  /// when set, opens the Edit Point form on the selected point.
  explicit PointEditor(std::function<PointOverlay*()> edited, std::function<void()> edit_point = nullptr);

  Status Activate() override;
  Status Deactivate() override;
  /// Add, edit, delete, labels, undo and redo, each with an icon and enabled
  /// only when it can act.
  std::vector<app::MenuNode> Tools() const override;

  bool active() const { return active_; }

 private:
  std::function<PointOverlay*()> edited_;
  std::function<void()> edit_point_;
  bool active_ = false;
};

/// Gives the registered `fv.points` type a `PointEditor` over the point set
/// `editors` is editing; `edit_point` backs its Edit Point tool. kNotFound
/// when `fv.points` is not registered.
Status RegisterPointEditor(app::OverlayTypeRegistry& registry, app::EditorManager& editors,
                           std::function<void()> edit_point = nullptr);

}  // namespace desk
}  // namespace fv
