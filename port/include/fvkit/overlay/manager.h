// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/overlay/manager.h — FvKit L4 overlay stack (mirrors
// IFvOverlayManager's open/close/stack-order/draw/route roles; pure calls,
// the app owns the loop). Draw order is bottom-up (index 0 first = deepest);
// event routing is top-down until an overlay reports handled.

#pragma once

#include <memory>
#include <vector>

#include "fvkit/overlay/overlay.h"

namespace fv {

class OverlayManager {
 public:
  OverlayManager() = default;
  OverlayManager(const OverlayManager&) = delete;
  OverlayManager& operator=(const OverlayManager&) = delete;

  // Appends on top of the stack.
  Status Add(std::shared_ptr<Overlay> overlay);
  Status Remove(const std::shared_ptr<Overlay>& overlay);
  Status MoveToTop(const std::shared_ptr<Overlay>& overlay);
  const std::vector<std::shared_ptr<Overlay>>& Overlays() const {
    return stack_;
  }

  // Draws visible overlays bottom-up. Stops at the first failure.
  Status DrawAll(const MapProjection& proj, ICanvas& canvas);

  // Route top-down through visible overlays; true if any handled it.
  bool RouteMouseMove(const MouseEvent& e);
  bool RouteMouseDown(const MouseEvent& e);
  bool RouteMouseUp(const MouseEvent& e);
  bool RouteDoubleClick(const MouseEvent& e);
  bool RouteMouseWheel(const MouseEvent& e, double delta);
  bool RouteKeyDown(const KeyEvent& e);

 private:
  template <typename Fn>
  bool RouteTopDown(Fn&& fn);

  std::vector<std::shared_ptr<Overlay>> stack_;
};

}  // namespace fv
