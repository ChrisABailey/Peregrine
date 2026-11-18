// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_moving_map_editor.h — the moving map tool palette for desktop shells.
///
/// The `OverlayEditor` for `fv.movingmap`. Course Up and Centre on Every Fix
/// are window settings; Snap to Road, Listen and Feed… act on the edited map,
/// which is the edited moving map or else the topmost one.
#pragma once

#include <vector>

#include "fvkit/app/editor.h"
#include "fvkit/app/type_registry.h"

namespace fv {
namespace desk {

class Desk;

class MovingMapEditor : public app::OverlayEditor {
 public:
  /// `desk` must outlive the editor.
  explicit MovingMapEditor(Desk& desk) : desk_(desk) {}

  Status Activate() override { return Status::Ok(); }
  Status Deactivate() override { return Status::Ok(); }
  /// Course Up, Centre on Every Fix, Snap to Road, Listen, Feed…, Ownship…
  /// and, with no icon so it stays off the toolbar, Auto Centre.
  std::vector<app::MenuNode> Tools() const override;

 private:
  Desk& desk_;
};

/// Gives the registered `fv.movingmap` type a `MovingMapEditor` over `desk`.
/// kNotFound when the type is not registered.
Status RegisterMovingMapEditor(app::OverlayTypeRegistry& registry, Desk& desk);

}  // namespace desk
}  // namespace fv
