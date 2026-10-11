// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// RouteKit/fv_route_editor.h — the route tool palette for desktop shells.
///
/// The `OverlayEditor` the EditorManager activates for `fv.route`; the C++
/// form of route.py's `RouteEditor`. It holds no editing state: every tool
/// acts on the edited route's `RouteEditSession`, which also takes the
/// gestures and keys.
#pragma once

#include <functional>
#include <vector>

#include "fvkit/app/editor.h"
#include "fvkit/app/type_registry.h"

namespace fv {

class RouteOverlay;

class RouteEditor : public app::OverlayEditor {
 public:
  /// `edited` answers the route being edited, or null. `open_profile`, when
  /// set, opens the route elevation profile and enables its tool.
  explicit RouteEditor(std::function<RouteOverlay*()> edited,
                       std::function<void()> open_profile = nullptr);

  Status Activate() override;
  Status Deactivate() override;
  /// Add, delete, leg type, follow roads (car or bicycle), straight legs,
  /// undo, redo and the elevation profile, each with an icon and enabled
  /// only when it can act.
  std::vector<app::MenuNode> Tools() const override;

  bool active() const { return active_; }

 private:
  std::function<RouteOverlay*()> edited_;
  std::function<void()> open_profile_;
  bool active_ = false;
};

/// Gives the registered `fv.route` type a `RouteEditor` over the route
/// `editors` is editing; `open_profile` is handed to each editor. kNotFound
/// when `fv.route` is not registered.
Status RegisterRouteEditor(app::OverlayTypeRegistry& registry, app::EditorManager& editors,
                           std::function<void()> open_profile = nullptr);

}  // namespace fv
