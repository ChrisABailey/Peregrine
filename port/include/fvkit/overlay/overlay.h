// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/overlay/overlay.h — FvKit L4 overlay SPI (contracts D1: shared_ptr
// ownership, chosen day-one for pybind trampoline lifetime).
//
// Mirrors FalconViewOverlay.idl's decomposition (IFvOverlay /
// IFvOverlayRenderer / IFvOverlayUIEvents). NOTE (plan deviation,
// documented): those are folded into ONE Overlay base class here instead of
// three interfaces — a single trampoline class is what pybind11 subclassing
// needs, and C++ overlays just override what they use (defaults: draw
// nothing, handle nothing). IFvOverlayPersistence / IFvOverlayEditor /
// IMapView come later with their first consumer.
//
// No exceptions across the SPI (D3): a Python overlay that raises is caught
// at the trampoline and surfaces as a failed Status from DrawAll.

#pragma once

#include <memory>
#include <string>

#include "fvkit/canvas/canvas.h"
#include "fvkit/proj.h"

namespace fv {

struct MouseEvent {
  int x = 0, y = 0;   // surface pixels
  int button = 0;     // 0 left, 1 middle, 2 right
  bool shift = false, ctrl = false;
};

class Overlay {
 public:
  explicit Overlay(std::string name) : name_(std::move(name)) {}
  virtual ~Overlay() = default;
  Overlay(const Overlay&) = delete;
  Overlay& operator=(const Overlay&) = delete;

  const std::string& Name() const { return name_; }
  bool IsVisible() const { return visible_; }
  void SetVisible(bool v) { visible_ = v; }

  // Draw in surface space for the given projection. Default: nothing.
  virtual Status OnDraw(const MapProjection& proj, ICanvas& canvas) {
    (void)proj;
    (void)canvas;
    return Status::Ok();
  }

  // UI events; return true when handled (stops top-down routing).
  virtual bool OnMouseMove(const MouseEvent& e) { (void)e; return false; }
  virtual bool OnMouseDown(const MouseEvent& e) { (void)e; return false; }
  virtual bool OnMouseUp(const MouseEvent& e) { (void)e; return false; }
  virtual bool OnDoubleClick(const MouseEvent& e) { (void)e; return false; }
  virtual bool OnMouseWheel(const MouseEvent& e, double delta) {
    (void)e;
    (void)delta;
    return false;
  }
  virtual bool OnKeyDown(int key) { (void)key; return false; }

 private:
  std::string name_;
  bool visible_ = true;
};

}  // namespace fv
