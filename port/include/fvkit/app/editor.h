// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/app/editor.h — the editor half of the overlay model
// (fvkit-app-plan.md §3d, ~ IFvOverlayEditor + IFvOverlayLimitUserInterface).
//
// A1 SLICE: `EditorUiConstraints` and the `OverlayEditor` interface, both
// exactly as the plan specifies them. They are here rather than in A4 for a
// concrete reason: `OverlayTypeDesc::editor_factory` returns a
// std::unique_ptr<OverlayEditor>, and a unique_ptr cannot be RETURNED without
// a complete type -- so a bare forward declaration would leave that field
// unpopulatable and `WithEditors()` untestable.
//
// A4 adds `EditorManager` -- the mode dance (SetMode/ToggleEditor), the
// editor-follows-current invariants, and the focus bracketing.
//
// One editor instance per overlay TYPE, not per overlay: the editor is the
// tool state ("I am drawing routes"), and the overlay being edited is whichever
// instance is current (EditTarget in capabilities.h is the per-instance half).

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "fvkit/app/shell.h"  // CursorId, MenuNode, FlowResult, AppShell
#include "fvkit/geo.h"        // Status

namespace fv {

// The stack. A reference member only, so a forward declaration keeps
// fvkit/overlay/manager.h out of every translation unit that just wants to
// implement an OverlayEditor.
class OverlayManager;

namespace app {

// The flow layer (fvkit/app/session.h). The dependency is MUTUAL -- the session
// tells the EditorManager an overlay is closing, the EditorManager asks the
// session to create one -- so it is wired after construction on both sides
// rather than through either constructor.
class OverlaySession;

// What the frame must stop offering while this editor is active
// (~ IFvOverlayLimitUserInterface). The shell reads these and greys its own
// controls; the core does not enforce them, because the core owns no controls.
struct EditorUiConstraints {
  bool disable_rotation = false;
  bool disable_projection_change = false;
  bool requires_north_up = false;
  bool disable_overlay_reorder = false;
};

class OverlayEditor {
 public:
  virtual ~OverlayEditor() = default;
  OverlayEditor(const OverlayEditor&) = delete;
  OverlayEditor& operator=(const OverlayEditor&) = delete;

  // Build/tear down tool state. The shell shows and hides the palette around
  // these (AppShell::OnEditorChanged, A3).
  virtual Status Activate() = 0;
  virtual Status Deactivate() = 0;

  virtual CursorId DefaultCursor() const { return CursorId::kCrosshair; }
  virtual EditorUiConstraints UiConstraints() const { return {}; }

  // Whether entering this mode should CREATE an overlay when none of its type
  // exists (~ m_bAutoEnterOverlayEditor). False means the mode simply waits.
  virtual bool AutoEnterOnCreate() const { return true; }

  // Editor-owned tools as data; the shell renders a toolbar or palette from
  // them. See the plan's §6 risk note -- MenuNode may prove too weak for a
  // real drawing editor's colour wells, and the escape is a richer panel
  // description, not a widget leaking into the core.
  virtual std::vector<MenuNode> Tools() const { return {}; }

 protected:
  OverlayEditor() = default;
};

// ---------------------------------------------------------------------------
// EditorManager — the mode dance (fvkit-app-plan.md §3d)
// ---------------------------------------------------------------------------
//
// ~ C_ovl_mgr::set_mode / toggle_editor / SwitchToEditor /
// make_mode_match_current_overlay, which in FalconView are four entangled
// members of a 6k-line class. Here they are one small object whose ENTIRE job
// is the plan's four invariants:
//
//   1. SetMode(t) makes the CURRENT OVERLAY MATCH THE MODE: the topmost overlay
//      of type t becomes current; if none exists and the editor auto-enters,
//      one is created (a file type through the FileNew flow, a static type by
//      toggling it on). If that creation is cancelled or fails, the mode falls
//      back to none -- a mode with nothing to edit is not a state the user
//      asked for.
//   2. Making a different overlay current makes the MODE MATCH THE OVERLAY:
//      switch to that type's editor if it has one, else leave edit mode. This
//      is observed, not called -- the EditorManager attaches a StackObserver,
//      so it follows a MakeCurrent from anywhere, including one the user made
//      by clicking a row in an overlay list.
//   3. Closing the overlay being edited drops current to the next OF THAT TYPE
//      (A2's Remove drops to the topmost remaining, which is the coarser rule
//      this refines), or leaves edit mode when that was the last one.
//   4. Focus is bracketed EXACTLY: ReleaseEditFocus on the overlay that is
//      losing edit before the switch, EnterEditFocus on the one gaining it
//      after, and only for the overlay actually being EDITED -- which is not
//      merely the current one, because current outlives the mode.
//
// The editor INSTANCE is per type and is kept: it is made on the type's first
// entry and reused, so tool state ("I am drawing with the arc tool") survives
// leaving and re-entering the mode. Activate/Deactivate bracket its use.
//
// REENTRANCY, same stance as OverlaySession: the manipulations here fire stack
// notifications that come straight back to this object, so a transition flag
// suppresses the self-inflicted ones. A shell that calls SetMode from inside
// AppShell::OnEditorChanged is a genuine reentrant entry and gets kFailed
// loudly rather than a half-switched mode.
class EditorManager {
 public:
  // None of the three is owned; all three must outlive the manager. The
  // constructor attaches a StackObserver to `manager` and the destructor
  // detaches it.
  EditorManager(OverlayTypeRegistry& registry, OverlayManager& manager,
                AppShell& shell);
  ~EditorManager();

  EditorManager(const EditorManager&) = delete;
  EditorManager& operator=(const EditorManager&) = delete;

  // Optional, and the plan's §3d.1 leans on it: creating the overlay a mode
  // needs is a FLOW (FileNew, or toggling a static type on), and flows live in
  // OverlaySession. WITHOUT a session the auto-enter half simply does not
  // happen -- entering a mode with nothing of its type open leaves the editor
  // active and waiting, exactly as `AutoEnterOnCreate() == false` does. Every
  // other invariant works with no session at all.
  void SetSession(OverlaySession* session) { session_ = session; }
  OverlaySession* session() const { return session_; }

  // ~ set_mode. An EMPTY id leaves edit mode. Entering the mode that is
  // already current is a no-op that reports kDone (FalconView returns early
  // too) -- it does not re-activate the editor or re-run adoption.
  //
  // kCanceled means the user cancelled the CREATION invariant 1 demanded, and
  // the mode has fallen back to none. kFailed means the type is unregistered,
  // has no editor, or its editor refused to Activate.
  FlowResult SetMode(const TypeId& id);

  // ~ toggle_editor, the Tools-menu behaviour: the mode that is already
  // current is left, any other is entered.
  FlowResult ToggleEditor(const TypeId& id);

  // Empty = not editing.
  const TypeId& CurrentMode() const { return mode_; }
  // Null = not editing. Owned here; valid until the type is re-entered, which
  // returns the same instance.
  OverlayEditor* CurrentEditor() const { return editor_; }
  // The overlay that holds edit focus -- the one bracketed by
  // Enter/ReleaseEditFocus. Null when no mode is active, and also when a mode
  // is active but nothing of its type is open (invariant 1's waiting state).
  Overlay* edited() const { return edited_; }

  // What the frame must stop offering while this editor is active. Default-
  // constructed (nothing constrained) when there is no editor.
  EditorUiConstraints ActiveConstraints() const;

  // Called by OverlaySession after it creates a document (~ the create path's
  // m_bAutoEnterOverlayEditor). A no-op mid-transition, because a transition
  // is already adopting an overlay and must not be re-entered; a no-op for a
  // type with no editor or an editor that does not auto-enter; and for the
  // mode that is already current it simply moves the focus bracket.
  FlowResult AutoEnterFor(const std::shared_ptr<Overlay>& overlay);

  // The Status behind the last kFailed; the shell has already been told
  // through AppShell::ReportError.
  const Status& last_error() const { return last_error_; }

 private:
  class Hook;       // editor.cpp: the StackObserver, kept off the public API
  class Transition; // editor.cpp: the reentrancy/self-notification guard

  // `adopt` null => find or create the overlay per invariant 1; non-null =>
  // bracket focus onto exactly THAT overlay and create nothing, which is what
  // "the mode follows the current overlay" needs (a current overlay is not
  // necessarily the topmost of its type).
  // BY VALUE, not by reference, and that is load-bearing: the only caller
  // with an interesting `adopt` passes OverlayManager::current_ptr(), and
  // EnterMode runs overlay code (ReleaseEditFocus) and editor code (Activate)
  // before it reads the argument. A reference would let either of those
  // silently redirect the adoption by changing the current overlay.
  FlowResult EnterMode(const TypeId& id, std::shared_ptr<Overlay> adopt);
  FlowResult AdoptForMode(std::shared_ptr<Overlay> adopt);
  void ExitMode(bool notify);

  OverlayEditor* EditorFor(const TypeId& id);
  void ReleaseFocus();
  void TakeFocus(Overlay* overlay);

  void OnCurrentChanged(Overlay* now);
  void OnOverlayRemoved(Overlay& overlay);

  FlowResult Fail(const Status& s);

  OverlayTypeRegistry& registry_;
  OverlayManager& manager_;
  AppShell& shell_;
  OverlaySession* session_ = nullptr;

  // One per TYPE, made on first entry and kept -- see the class comment.
  std::unordered_map<TypeId, std::unique_ptr<OverlayEditor>> editors_;
  TypeId mode_;
  OverlayEditor* editor_ = nullptr;  // into editors_; null when not editing
  Overlay* edited_ = nullptr;        // weak: cleared by OverlayRemoved

  std::unique_ptr<Hook> hook_;
  Status last_error_ = Status::Ok();
  bool in_transition_ = false;
};

}  // namespace app
}  // namespace fv
