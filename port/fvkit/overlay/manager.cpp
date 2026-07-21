// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// OverlayManager — see fvkit/overlay/manager.h.

#include "fvkit/overlay/manager.h"

#include <algorithm>

namespace fv {

Status OverlayManager::Add(std::shared_ptr<Overlay> overlay) {
  if (overlay == nullptr) return Status::Error(kInvalidArg, "null overlay");
  if (std::find(stack_.begin(), stack_.end(), overlay) != stack_.end())
    return Status::Error(kInvalidArg, "overlay already added");
  stack_.push_back(std::move(overlay));
  return Status::Ok();
}

Status OverlayManager::Remove(const std::shared_ptr<Overlay>& overlay) {
  auto it = std::find(stack_.begin(), stack_.end(), overlay);
  if (it == stack_.end()) return Status::Error(kNotFound, "overlay not in stack");
  stack_.erase(it);
  return Status::Ok();
}

Status OverlayManager::MoveToTop(const std::shared_ptr<Overlay>& overlay) {
  auto it = std::find(stack_.begin(), stack_.end(), overlay);
  if (it == stack_.end()) return Status::Error(kNotFound, "overlay not in stack");
  std::rotate(it, it + 1, stack_.end());
  return Status::Ok();
}

Status OverlayManager::DrawAll(const MapProjection& proj, ICanvas& canvas) {
  for (const auto& o : stack_) {  // bottom-up
    if (!o->IsVisible()) continue;
    Status s = o->OnDraw(proj, canvas);
    if (!s.ok()) {
      s.message = "overlay '" + o->Name() + "': " + s.message;
      return s;
    }
  }
  return Status::Ok();
}

template <typename Fn>
bool OverlayManager::RouteTopDown(Fn&& fn) {
  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
    if (!(*it)->IsVisible()) continue;
    if (fn(**it)) return true;
  }
  return false;
}

bool OverlayManager::RouteMouseMove(const MouseEvent& e) {
  return RouteTopDown([&](Overlay& o) { return o.OnMouseMove(e); });
}
bool OverlayManager::RouteMouseDown(const MouseEvent& e) {
  return RouteTopDown([&](Overlay& o) { return o.OnMouseDown(e); });
}
bool OverlayManager::RouteMouseUp(const MouseEvent& e) {
  return RouteTopDown([&](Overlay& o) { return o.OnMouseUp(e); });
}
bool OverlayManager::RouteDoubleClick(const MouseEvent& e) {
  return RouteTopDown([&](Overlay& o) { return o.OnDoubleClick(e); });
}
bool OverlayManager::RouteMouseWheel(const MouseEvent& e, double delta) {
  return RouteTopDown([&](Overlay& o) { return o.OnMouseWheel(e, delta); });
}
bool OverlayManager::RouteKeyDown(int key) {
  return RouteTopDown([&](Overlay& o) { return o.OnKeyDown(key); });
}

}  // namespace fv
