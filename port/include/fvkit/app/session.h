// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/app/session.h — the flows (fvkit-app-plan-COMPLETE.md §3g).
//
// `OverlaySession` is the verb layer. It owns nothing: it composes the type
// REGISTRY (what can be opened), the STACK (what is open), the SHELL (what the
// user is asked) and SETTINGS (what survives a restart), and implements the
// operations FalconView scatters across C_ovl_mgr::{open, create, close, save,
// save_as, save_all, toggle_static_overlay, OpenFileOverlay, exit} and
// save_overlay_configuration / RestoreStartupOverlays.
//
// Every user-visible verb returns a FlowResult, never a bool (rule R5), and
// the difference between kCanceled and kFailed is load-bearing: a cancel is
// the user's answer and propagates -- one "Cancel" in one Save prompt aborts
// the rest of CloseAll and with it the application exit. That is FalconView's
// behaviour and it is the reason a flow cannot return a plain Status.
//
// EDITORS ARE OPTIONAL. A4 built the EditorManager and it slots in here as one
// more collaborator, exactly as A3 predicted: a NULLABLE pointer, not a
// constructor reference, because the dependency is mutual (the EditorManager
// asks the session to create the overlay a mode needs) and one of the two must
// be built first. With no EditorManager wired these flows are the A3 flows
// unchanged -- Close does the per-instance EditTarget release itself, and
// nothing enters a mode.
//
// REENTRANCY (plan §6). FalconView guarded C_ovl_mgr::select with a static
// `active` flag because its dialogs pumped the message loop and a second flow
// could start inside the first. Shell calls here are synchronous, but a shell
// that pumps -- every real one does -- can still deliver a click while a Save
// prompt is up. So every public flow takes a guard, and a nested entry returns
// kFailed LOUDLY (with a Status naming both flows) rather than corrupting the
// stack quietly. The private *Impl methods are what the flows call each other
// through, so Close -> Save is not a nested entry.

#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fvkit/app/capabilities.h"
#include "fvkit/app/shell.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/geo.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/settings.h"

namespace fv {
namespace app {

class EditorManager;  // fvkit/app/editor.h (A4)

class OverlaySession {
 public:
  // None of the four is owned; all four must outlive the session.
  OverlaySession(OverlayTypeRegistry& registry, OverlayManager& manager,
                 AppShell& shell, Settings& settings);

  OverlaySession(const OverlaySession&) = delete;
  OverlaySession& operator=(const OverlaySession&) = delete;

  // A4, optional and settable either way round (see the header comment). Wire
  // it in both directions -- `editors->SetSession(this)` too -- or the mode
  // dance can enter a mode and not create the overlay it needs.
  void SetEditorManager(EditorManager* editors) { editors_ = editors; }
  EditorManager* editors() const { return editors_; }

  // -----------------------------------------------------------------------
  // Creating and opening
  // -----------------------------------------------------------------------

  // ~ toggle_static_overlay: exists => close it, does not => create it. Only
  // legal for a STATIC type (no FileTypeDesc); a file type has many instances
  // and nothing to toggle.
  //
  // DECIDED HERE, and it is a decision rather than a transcription: toggling a
  // static overlay on does NOT make it current. A graticule is not a document
  // and there is nothing to be current FOR; what makes an editable overlay
  // current is entering its editor's mode, which is A4's job and does it
  // explicitly (plan §3d.1).
  FlowResult ToggleStatic(const TypeId& id);

  // ~ create + FileNew. A fresh untitled document: the overlay is created,
  // asked to FileNew itself, added and made current. It has NOT been saved, so
  // the first Save goes through the Save As flow.
  // A failed FileNew leaves NOTHING in the stack -- a half-built document must
  // not become something the user has to close.
  //
  // A4: with an EditorManager wired, a new document of a type whose editor
  // auto-enters also ENTERS that editor (~ m_bAutoEnterOverlayEditor). Only on
  // create, not on open -- FalconView draws that line too. An editor that
  // refuses to activate does NOT undo the document: the failure is reported to
  // the shell by the EditorManager and noted in warnings(), and this flow
  // still returns kDone, because the thing the user asked for exists.
  FlowResult NewFileOverlay(const TypeId& id);

  // ~ OpenFileOverlay: ask the shell for files, then open each one.
  // `hint` empty => the chooser is offered the union of every registered file
  // type's open filters and each chosen path is dispatched BY EXTENSION.
  //
  // One file's outcome never stops the others: the aggregate is kFailed if any
  // file failed, else kCanceled if any was cancelled (or the chooser itself
  // was), else kDone.
  FlowResult OpenFileOverlays(const TypeId& hint);

  // Open one named file. `type` may be empty, in which case the extension
  // picks the type (registry order breaks a tie).
  //
  // DEDUP is by (TypeId, file spec) through the stack's FindByFileSpec: a file
  // that is already open is made current rather than opened twice. If that
  // already-open copy is DIRTY and supports revert, the user is asked whether
  // to discard their edits and re-read -- answering no is not a cancel, it
  // simply leaves the dirty copy alone and current (plan §1.2).
  // The spec is compared exactly; canonicalising a path is the shell's job,
  // because the rule differs per platform.
  FlowResult OpenFile(const TypeId& type, const std::string& spec);

  // -----------------------------------------------------------------------
  // Saving
  // -----------------------------------------------------------------------

  // Writes to the document's own file spec, in the format it was last saved
  // through. Falls through to the Save As flow when there is nowhere to write
  // (never saved, no file spec) or writing there is not allowed (read-only).
  // A clean document is kDone without touching the disk.
  FlowResult Save(Overlay& overlay);

  // Always asks for a spec. On success the overlay adopts the new spec, the
  // chosen format index and a clean, saved, writable state.
  FlowResult SaveAs(Overlay& overlay);

  // Every dirty document in the stack, bottom-up. STOPS at the first cancel or
  // failure and returns it: the user who cancelled one Save As did not mean
  // "carry on with the rest".
  FlowResult SaveAll();

  // -----------------------------------------------------------------------
  // Closing
  // -----------------------------------------------------------------------

  // Order, pinned by the plan and by the tests: release edit focus, then
  // prompt if dirty (kSave runs the Save flow, and a FAILED save aborts the
  // close -- the alternative is losing the document), then Remove, which fires
  // the observers and drops the last reference.
  //
  // The edit-focus release is DEFERRED to the EditorManager when one is wired:
  // it releases from its own OverlayRemoved hook, where it can also do the
  // half this flow cannot (drop current to the next overlay of the same type,
  // or leave the mode). Doing both would call ReleaseEditFocus twice.
  //
  // Refuses an overlay whose type is not `user_controllable`: that flag exists
  // to keep the user from closing it. CloseAll and Exit close it anyway.
  FlowResult Close(Overlay& overlay);

  // Top-down over the whole stack, one prompt per dirty document. ANY cancel
  // aborts the rest -- and the overlays already closed stay closed, exactly as
  // FalconView leaves them.
  FlowResult CloseAll();

  // CloseAll, plus the configuration snapshot if `[session] autosave` is on.
  // The snapshot is taken BEFORE anything closes and applied only if the close
  // actually completed, so a cancelled exit does not overwrite the saved
  // session with an empty one.
  FlowResult Exit();

  // -----------------------------------------------------------------------
  // Configuration (~ save_overlay_configuration / RestoreStartupOverlays)
  // -----------------------------------------------------------------------

  // Writes the stack into `settings` under [session.<name>]: one row per
  // overlay bottom-up (type, file spec, visibility), plus the current index
  // and the declutter flag. Overlays with no type_id are skipped and reported
  // in warnings() -- an ad-hoc overlay was made outside the app layer and
  // there is no descriptor to remake it from.
  //
  // IN MEMORY ONLY, and this is the S1 rule rather than an omission:
  // fv::Settings has no Save() because the file is authored by a human and the
  // application never rewrites it. So this populates the live Settings (a
  // shell may serialise it, and a hand-written peregrine.ini restores a
  // session at startup), and persisting a session the user built at RUN time
  // needs a settings writer that does not exist yet. Noted in the ledger.
  Status SaveConfiguration(const std::string& name = "default");

  // Opens what the configuration names, through the same flows -- so an
  // already-open file is deduped rather than doubled and a static type is not
  // added twice. Nothing is closed first.
  //
  // BEST EFFORT, deliberately: a type the configuration names and the registry
  // does not know (a plugin that is gone, a downgrade) is skipped with a line
  // in warnings(), not an aborted restore. Same for a file that no longer
  // opens. The saved ORDER is reapplied only when the restored overlays are
  // exactly the stack -- reordering around overlays the configuration never
  // mentioned would be inventing an answer.
  Status RestoreConfiguration(const std::string& name = "default");

  // ~ RestoreStartupOverlays: every STATIC type flagged `restore_at_startup`
  // that is not already open. This is what that descriptor flag is for.
  Status RestoreStartupOverlays();

  // -----------------------------------------------------------------------

  // The Status behind the last kFailed. The shell has already been told
  // through ReportError; this is for a caller that wants the detail.
  const Status& last_error() const { return last_error_; }

  // Accumulated non-fatal notes from the configuration flows, newest last.
  const std::vector<std::string>& warnings() const { return warnings_; }
  void ClearWarnings() { warnings_.clear(); }

 private:
  class Guard;

  // The unguarded bodies. Flows call each OTHER through these, so Close's
  // internal Save is not a reentrant entry.
  FlowResult CloseImpl(Overlay& overlay, bool force);
  FlowResult SaveImpl(Overlay& overlay);
  FlowResult SaveAsImpl(Overlay& overlay);
  FlowResult OpenFileImpl(const TypeId& type, const std::string& spec);
  FlowResult ToggleStaticImpl(const TypeId& id);
  FlowResult NewFileOverlayImpl(const TypeId& id);
  FlowResult CloseAllImpl();

  // Makes an instance of `desc`, stamped with its type id. Null on a factory
  // that returned nothing, which is a failure the caller reports.
  // The configuration as key/value pairs, WITHOUT writing them. Exit needs the
  // snapshot taken before the stack empties and applied only if the close
  // completed, which a straight write into Settings cannot express.
  Status BuildConfiguration(const std::string& name,
                            std::vector<std::pair<std::string, std::string>>* out);

  std::shared_ptr<Overlay> Instantiate(const OverlayTypeDesc& desc);
  std::shared_ptr<Overlay> Shared(const Overlay& overlay) const;

  FlowResult Fail(const Status& s);
  void Warn(const std::string& text) { warnings_.push_back(text); }

  OverlayTypeRegistry& registry_;
  OverlayManager& manager_;
  AppShell& shell_;
  Settings& settings_;
  EditorManager* editors_ = nullptr;  // A4, optional
  Status last_error_ = Status::Ok();
  std::vector<std::string> warnings_;
  const char* in_flow_ = nullptr;  // the guard's name, null when idle
};

}  // namespace app
}  // namespace fv
