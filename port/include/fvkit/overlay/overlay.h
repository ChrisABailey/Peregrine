// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

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

#include <cstdint>
#include <memory>
#include <string>

#include "fvkit/canvas/canvas.h"
#include "fvkit/proj.h"

namespace fv {

// The app layer's optional capabilities (fvkit/app/capabilities.h, A1).
// Forward-declared, never included: capabilities.h includes THIS header (a
// HitItem names an Overlay), and the accessors below only return pointers, so
// an incomplete type is all they need. This keeps L4 free of any app-layer
// dependency -- fvkit/overlay still builds and binds on its own.
namespace app {
class Persistence;
class HitTest;
class SnapTo;
class ContextMenu;
class RoutingOverrides;
class EditTarget;
class SearchProvider;
class Properties;
}  // namespace app

struct MouseEvent {
  int x = 0, y = 0;   // surface pixels
  int button = 0;     // 0 left, 1 middle, 2 right
  bool shift = false, ctrl = false;
};

// ---------------------------------------------------------------------------
// Keyboard (2026-08-04, found by Chris hooking a route overlay to tkinter)
// ---------------------------------------------------------------------------
//
// `Key` values ARE Win32 virtual-key codes, and are never renumbered — the
// same rule fv_map_enums.h states for the COM ABI enums, for the same reason:
// this is a value crossing a seam whose native peer is the Windows product,
// and a Windows UI shell gets these free from WM_KEYDOWN's wParam. Two
// properties fall out of the choice and are worth knowing:
//
//   * letters and digits are their ASCII UPPERCASE code points, so
//     `e.key == 'A'` and `e.key == '7'` are legitimate comparisons;
//   * kReturn/kSpace/kBackspace/kTab coincide with their ASCII controls.
//
// A shell that cannot map one of its own keys sends key = 0 (kNone) and
// leaves `text` to carry it — that is not an error, just a key nobody named.
namespace Key {
enum : int {
  kNone = 0x00,
  kBackspace = 0x08,
  kTab = 0x09,
  kReturn = 0x0D,
  kEscape = 0x1B,
  kSpace = 0x20,
  kPageUp = 0x21,
  kPageDown = 0x22,
  kEnd = 0x23,
  kHome = 0x24,
  kLeft = 0x25,
  kUp = 0x26,
  kRight = 0x27,
  kDown = 0x28,
  kInsert = 0x2D,
  kDelete = 0x2E,
  // 0x30-0x39 digits, 0x41-0x5A letters: their own ASCII values.
  kF1 = 0x70,  // F(n) == kF1 + (n - 1), through F24 at 0x87
  kF12 = 0x7B,
};
}  // namespace Key

// One key press, as a UI shell reports it.
//
// `key` and `text` answer DIFFERENT questions and both are needed: `key` is
// the physical intent ("the Left arrow", "the D key"), stable across keyboard
// layouts, and is what a shortcut compares against; `text` is the character
// the layout actually produced, and is what an overlay accepting typed input
// should insert. A non-printing key leaves `text` 0; a key with no portable
// name leaves `key` 0.
struct KeyEvent {
  int key = 0;        // a Key value; 0 = this shell had no name for it
  uint32_t text = 0;  // Unicode code point produced, 0 if none
  bool shift = false, ctrl = false, alt = false, meta = false;
};

class Overlay {
 public:
  explicit Overlay(std::string name) : name_(std::move(name)) {}
  virtual ~Overlay() = default;
  Overlay(const Overlay&) = delete;
  Overlay& operator=(const Overlay&) = delete;

  const std::string& Name() const { return name_; }
  // A FILE overlay renames itself to its document when one is opened (A6), the
  // way FalconView shows the route's file name in the overlay list rather than
  // the name it was created under. Nothing else changes a name.
  void SetName(std::string name) { name_ = std::move(name); }
  bool IsVisible() const { return visible_; }
  void SetVisible(bool v) { visible_ = v; }

  // The registered overlay type this instance came from (app::TypeId, i.e. a
  // string -- "fv.grid"). Empty for an overlay created outside the app layer,
  // which is legal: an ad-hoc pyfvw overlay has no type. Stamped at creation
  // by the session layer, the way InternalInitialize(guid) did it, so the
  // stack can answer FirstOfType/OfType (A2).
  const std::string& type_id() const { return type_id_; }
  void set_type_id(std::string id) { type_id_ = std::move(id); }

  // Optional capabilities (R2). A capable overlay overrides the one it
  // implements and returns `this`; everything else keeps the nullptr default
  // and is skipped at the call site. This replaces FalconView's dynamic_cast
  // discovery -- same optionality, one class hierarchy, and safe across a
  // pybind11 trampoline, which a cross-cast is not.
  virtual app::Persistence* AsPersistence() { return nullptr; }
  virtual app::HitTest* AsHitTest() { return nullptr; }
  virtual app::SnapTo* AsSnapTo() { return nullptr; }
  virtual app::ContextMenu* AsContextMenu() { return nullptr; }
  virtual app::RoutingOverrides* AsRoutingOverrides() { return nullptr; }
  virtual app::EditTarget* AsEditTarget() { return nullptr; }
  // "Where is X" (fvkit/app/search.h, S1). The SEVENTH accessor, and the one
  // that is not about the cursor: every other capability here answers a
  // question about a pixel, and this one answers a question about the world.
  // An overlay implements it to be findable by name or by area whether or not
  // it is on screen, or even visible.
  virtual app::SearchProvider* AsSearch() { return nullptr; }
  // "What can the user change about this overlay" (fvkit/app/properties.h).
  // The EIGHTH accessor, and the one that replaces a whole class of Windows
  // code rather than a call site: an overlay that implements it declares its
  // settable properties, and every shell builds its property page from that
  // declaration instead of porting an MFC CPropertyPage per overlay.
  virtual app::Properties* AsProperties() { return nullptr; }

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
  virtual bool OnKeyDown(const KeyEvent& e) { (void)e; return false; }

 private:
  std::string name_;
  std::string type_id_;
  bool visible_ = true;
};

}  // namespace fv
