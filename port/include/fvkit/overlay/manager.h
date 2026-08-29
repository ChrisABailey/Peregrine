// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/overlay/manager.h — FvKit L4 overlay stack (mirrors
// IFvOverlayManager's open/close/stack-order/draw/route roles; pure calls,
// the app owns the loop). Draw order is bottom-up (index 0 first = deepest);
// event routing is top-down until an overlay reports handled.
//
// A2 (fvkit-app-plan-COMPLETE.md §3c) grew this class IN PLACE rather than adding a
// second stack type: the plan's `stack.h` is this file. What A2 added, all of
// it inert until a shell asks for it, so a pyfvw user who only ever calls
// Add/DrawAll/Route* sees the pre-A2 stack unchanged (rule R7):
//
//   * StackObserver — added/removed/order/current/dirty/file-spec.
//   * a CURRENT overlay, explicit and broadcast.
//   * reorder verbs: MoveAbove/MoveBelow/MoveToBottom/Reorder.
//   * insertion by the TYPE's display order rather than blindly on top.
//   * declutter (only the current overlay draws and routes).
//   * mouse capture, so a gesture cannot be stolen mid-flight.
//   * three-phase event routing (direct-routing pre-pass, declutter, top-down).
//
// LAYERING: this header still includes nothing from fvkit/app. The stack needs
// two things from the app layer -- a type's display order / top-most flag, and
// the Persistence dirty broadcast -- and both are reached through an incomplete
// type here and a complete one in manager.cpp only. `fvkit/overlay` therefore
// still compiles and binds on its own, which is the property A1 went to some
// trouble to establish.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fvkit/overlay/overlay.h"

namespace fv {

namespace app {
class OverlayTypeRegistry;
struct OverlayTypeDesc;
}  // namespace app

// ~ OverlayStackChangedObserver_Interface. The overlay-list UI, title bars and
// the session layer all hang off this. Every method has an empty default, so an
// observer implements only what it redraws.
//
// An observer must not add to or remove from the stack while it is being
// called; the notification walks a copy of the observer list, so DETACHING
// yourself from inside a callback is safe, but mutating the stack is not.
class StackObserver {
 public:
  virtual ~StackObserver() = default;

  virtual void OverlayAdded(Overlay&) {}
  // Fired after removal from the stack and before the shared_ptr drops, so the
  // overlay is still alive and readable here -- that is the whole reason the
  // notification is not in the destructor.
  virtual void OverlayRemoved(Overlay&) {}
  virtual void OverlayOrderChanged() {}
  // FalconView had this implicit (m_current is public state nobody announced).
  // Either pointer may be null: null `was` = nothing was current.
  virtual void CurrentChanged(Overlay* now, Overlay* was) {}
  // Forwarded from app::Persistence, which the stack hooks on Add. Both fire
  // on a CHANGE only -- see capabilities.h.
  virtual void OverlayDirtyChanged(Overlay&) {}
  virtual void OverlayFileSpecChanged(Overlay&) {}
};

class OverlayManager {
 public:
  OverlayManager();
  ~OverlayManager();
  OverlayManager(const OverlayManager&) = delete;
  OverlayManager& operator=(const OverlayManager&) = delete;

  // ---------------------------------------------------------------------
  // Type information (A2). Optional: with no registry every overlay has
  // display order 0 and is not top-most, which makes Add an append and DrawAll
  // one pass -- exactly the pre-A2 stack. The registry is NOT owned and must
  // outlive the manager.
  // ---------------------------------------------------------------------
  void SetTypeRegistry(const app::OverlayTypeRegistry* registry) {
    registry_ = registry;
  }
  const app::OverlayTypeRegistry* type_registry() const { return registry_; }

  // ---------------------------------------------------------------------
  // Membership
  // ---------------------------------------------------------------------

  // Inserts by the type's `default_display_order` (~ AddOverlayToStack), NOT
  // blindly on top. FalconView's rule, kept verbatim including its bias: the
  // overlay lands directly above the TOPMOST overlay whose display order is
  // <= its own, and at the bottom if there is no such overlay. Equal orders
  // therefore stack newest-on-top, and an overlay the user has dragged down
  // does not drag later arrivals down with it.
  //
  // A top-most-flagged overlay is placed among the other top-most ones and
  // always above every overlay that is not flagged.
  Status Add(std::shared_ptr<Overlay> overlay);

  // Fires OverlayRemoved, drops mouse capture and the current overlay if they
  // named it, and detaches the Persistence hook.
  Status Remove(const std::shared_ptr<Overlay>& overlay);

  const std::vector<std::shared_ptr<Overlay>>& Overlays() const {
    return stack_;
  }
  bool Contains(const std::shared_ptr<Overlay>& overlay) const;

  // ---------------------------------------------------------------------
  // Order. Index 0 is the BOTTOM of the stack throughout.
  // ---------------------------------------------------------------------
  Status MoveToTop(const std::shared_ptr<Overlay>& overlay);
  Status MoveToBottom(const std::shared_ptr<Overlay>& overlay);
  // `move` ends up directly above / below `anchor`. Both must be in the stack
  // and must not be the same overlay.
  Status MoveAbove(const std::shared_ptr<Overlay>& move,
                   const std::shared_ptr<Overlay>& anchor);
  Status MoveBelow(const std::shared_ptr<Overlay>& move,
                   const std::shared_ptr<Overlay>& anchor);
  // ~ reorder_overlay_list: a TOTAL permutation, bottom-first. Anything that
  // is not a permutation of exactly the current contents is rejected whole and
  // the stack is left untouched -- a reorder dialog that has gone stale must
  // not half-apply.
  Status Reorder(const std::vector<std::shared_ptr<Overlay>>& full_order);

  // ---------------------------------------------------------------------
  // Current overlay
  // ---------------------------------------------------------------------

  // The overlay the user is working with: the target of declutter, of the
  // editor-follow dance (A4) and of Save/Close in the session flows (A3).
  Overlay* current() const { return current_.get(); }
  const std::shared_ptr<Overlay>& current_ptr() const { return current_; }
  // A null argument clears it. Fires CurrentChanged only on an actual change.
  Status MakeCurrent(const std::shared_ptr<Overlay>& overlay);

  // ---------------------------------------------------------------------
  // Queries by type / document (A3 and A4 dispatch through these)
  // ---------------------------------------------------------------------

  // TOPMOST overlay of the type, so it is the one an editor adopts. Null if
  // there is none.
  std::shared_ptr<Overlay> FirstOfType(const std::string& type_id) const;
  // Every overlay of the type, TOP-DOWN -- so OfType(t).front() == FirstOfType(t).
  std::vector<std::shared_ptr<Overlay>> OfType(const std::string& type_id) const;
  // The open-dedup lookup (plan §3g): the key is (type, file spec). An empty
  // `type_id` matches any type. The spec is compared exactly; canonicalising a
  // path is the shell's job, because the rule differs per platform.
  std::shared_ptr<Overlay> FindByFileSpec(const std::string& type_id,
                                          const std::string& file_spec) const;

  // ---------------------------------------------------------------------
  // Declutter (~ show_other_overlays(FALSE))
  // ---------------------------------------------------------------------

  // On: only the current overlay draws and routes. It is not a per-overlay
  // visibility change -- nothing is hidden, the rest of the stack is simply
  // skipped, and turning it off restores exactly what was there.
  void SetDeclutter(bool current_only) { declutter_ = current_only; }
  bool declutter() const { return declutter_; }

  // ---------------------------------------------------------------------
  // Mouse capture
  // ---------------------------------------------------------------------

  // Replaces FalconView's m_drag/drag()/drop()/cancel_drag() triad with the
  // mechanism every modern toolkit uses, keeping the guarantee those calls
  // existed for: once an overlay captures, every mouse event goes to it alone
  // until it releases. Typically called from the overlay's own OnMouseDown.
  // The overlay must be in the stack.
  Status CaptureMouse(Overlay* overlay);
  void ReleaseMouse() { capture_ = nullptr; }
  Overlay* mouse_capture() const { return capture_; }

  // ---------------------------------------------------------------------
  // Observers
  // ---------------------------------------------------------------------
  void AddObserver(StackObserver* observer);
  void RemoveObserver(StackObserver* observer);

  // ---------------------------------------------------------------------
  // Draw and route
  // ---------------------------------------------------------------------

  // The overlays that are ON SCREEN, in DRAW order: bottom-up, the top-most
  // band last, honouring visibility and declutter. This is exactly the walk
  // DrawAll performs -- and therefore, reversed, the walk a hit test must
  // perform, because the pick layer's whole contract is that a tap agrees with
  // what the user can see (A5). A2 kept this rule inside DrawAll; a second
  // implementation of "who is on screen and in what order" would drift from
  // the first the day someone changed the top-most band.
  std::vector<Overlay*> DrawOrder() const;

  // Bottom-up over visible overlays, then the top-most-flagged ones (also
  // bottom-up among themselves) so a crosshair or HUD lands over everything
  // even if the user has reordered it downwards. Stops at the first failure.
  // Under declutter, only the current overlay draws.
  //
  // NOTE: OverlayTypeDesc::default_opacity is NOT applied -- ICanvas has no
  // layer alpha to apply it with. See the ledger's product-gap list.
  Status DrawAll(const MapProjection& proj, ICanvas& canvas);

  // Three phases per event, in order (~ C_ovl_mgr::select, minus the
  // dynamic_casts and the re-entry flag):
  //
  //   0. mouse capture — if an overlay holds it, the event goes to it and to
  //      nobody else, whatever the rest of this list says.
  //   1. direct-routing pre-pass — visible overlays whose
  //      RoutingOverrides::WantsDirectRouting() is true, top-down. A mid-
  //      gesture or selection-locked overlay must see the mouse first even
  //      when it is not topmost. If none handles it, routing continues
  //      normally; the pre-pass ignores declutter, as the original does.
  //   2. declutter — if on, only the current overlay is offered the event.
  //   3. top-down over visible overlays until one reports handled.
  //
  // KeyDown differs in one place, and only for capture: the capturing overlay
  // sees the key FIRST (Escape is the conventional gesture cancel) but does
  // not swallow it, so an unhandled key still routes normally.
  bool RouteMouseMove(const MouseEvent& e);
  bool RouteMouseDown(const MouseEvent& e);
  bool RouteMouseUp(const MouseEvent& e);
  bool RouteDoubleClick(const MouseEvent& e);
  bool RouteMouseWheel(const MouseEvent& e, double delta);
  bool RouteKeyDown(const KeyEvent& e);

 private:
  class PersistenceBridge;  // manager.cpp: forwards app::Persistence changes

  template <typename Fn>
  bool RouteTopDown(Fn&& fn);
  template <typename Fn>
  bool RouteEvent(Fn&& fn, std::vector<Overlay*>* already);
  template <typename Fn>
  bool RouteMouseEvent(Fn&& fn);

  const app::OverlayTypeDesc* DescFor(const Overlay& o) const;
  int DisplayOrderOf(const Overlay& o) const;
  bool IsTopMost(const Overlay& o) const;
  Overlay* TopWorkingOverlay() const;
  std::shared_ptr<Overlay> FindShared(const Overlay* raw) const;

  // Notification walks a COPY, so an observer may detach itself from inside a
  // callback (the overlay-list row that is being destroyed does exactly that).
  template <typename Fn>
  void Notify(Fn&& fn);
  void SetCurrentInternal(std::shared_ptr<Overlay> now);

  std::vector<std::shared_ptr<Overlay>> stack_;
  std::vector<StackObserver*> observers_;
  std::shared_ptr<Overlay> current_;
  Overlay* capture_ = nullptr;
  const app::OverlayTypeRegistry* registry_ = nullptr;
  std::unique_ptr<PersistenceBridge> bridge_;
  bool declutter_ = false;
};

}  // namespace fv
