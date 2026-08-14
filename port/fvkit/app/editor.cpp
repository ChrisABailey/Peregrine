// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/app/editor.h"

#include <memory>
#include <string>
#include <utility>

#include "fvkit/app/capabilities.h"  // EditTarget
#include "fvkit/app/session.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/manager.h"

namespace fv {
namespace app {

// The stack notifications the mode dance is built on. Kept off EditorManager's
// public surface deliberately: CurrentChanged and OverlayRemoved are things
// that HAPPEN TO this object, not verbs a shell should be able to call.
class EditorManager::Hook : public StackObserver {
 public:
  explicit Hook(EditorManager& owner) : owner_(owner) {}
  void CurrentChanged(Overlay* now, Overlay* /*was*/) override {
    owner_.OnCurrentChanged(now);
  }
  void OverlayRemoved(Overlay& overlay) override {
    owner_.OnOverlayRemoved(overlay);
  }

 private:
  EditorManager& owner_;
};

// Held for the length of one transition. Two jobs in one flag, because they
// are the same fact seen from two sides: while the EditorManager is moving the
// mode it must ignore the stack notifications it is itself causing, and a
// caller that arrives during that is genuinely reentrant.
class EditorManager::Transition {
 public:
  explicit Transition(EditorManager& owner) : owner_(owner) {
    owner_.in_transition_ = true;
  }
  ~Transition() { owner_.in_transition_ = false; }
  Transition(const Transition&) = delete;
  Transition& operator=(const Transition&) = delete;

 private:
  EditorManager& owner_;
};

EditorManager::EditorManager(OverlayTypeRegistry& registry,
                             OverlayManager& manager, AppShell& shell)
    : registry_(registry),
      manager_(manager),
      shell_(shell),
      hook_(new Hook(*this)) {
  manager_.AddObserver(hook_.get());
}

EditorManager::~EditorManager() {
  // Detach first, so nothing routes back into a half-destroyed object, then
  // leave the mode properly: an editor that allocated tool state in Activate
  // gets its Deactivate, and the edited overlay -- which outlives this object,
  // because the manager holding it does -- gets its ReleaseEditFocus. Without
  // a notification: a shell that is tearing this down is not listening.
  manager_.RemoveObserver(hook_.get());
  Transition t(*this);
  ExitMode(/*notify=*/false);
}

FlowResult EditorManager::Fail(const Status& s) {
  last_error_ = s;
  shell_.ReportError(s);
  return FlowResult::kFailed;
}

EditorUiConstraints EditorManager::ActiveConstraints() const {
  return editor_ != nullptr ? editor_->UiConstraints() : EditorUiConstraints();
}

// ---------------------------------------------------------------------------
// Editor instances and focus
// ---------------------------------------------------------------------------

OverlayEditor* EditorManager::EditorFor(const TypeId& id) {
  auto it = editors_.find(id);
  if (it != editors_.end()) return it->second.get();

  const OverlayTypeDesc* desc = registry_.Find(id);
  if (desc == nullptr || !desc->editor_factory) return nullptr;
  std::unique_ptr<OverlayEditor> made = desc->editor_factory();
  if (made == nullptr) return nullptr;
  OverlayEditor* raw = made.get();
  editors_.emplace(id, std::move(made));
  return raw;
}

void EditorManager::ReleaseFocus() {
  if (edited_ == nullptr) return;
  Overlay* losing = edited_;
  // Cleared FIRST: ReleaseEditFocus is overlay code and may do anything,
  // including asking who is being edited, and the answer by then is nobody.
  edited_ = nullptr;
  if (EditTarget* target = losing->AsEditTarget()) target->ReleaseEditFocus();
}

void EditorManager::TakeFocus(Overlay* overlay) {
  if (edited_ == overlay) return;  // already the edited one: no bracket to move
  ReleaseFocus();                  // invariant 3d.4: release BEFORE the switch
  edited_ = overlay;
  if (overlay == nullptr) return;
  if (EditTarget* target = overlay->AsEditTarget()) target->EnterEditFocus();
}

// ---------------------------------------------------------------------------
// Entering and leaving a mode
// ---------------------------------------------------------------------------

void EditorManager::ExitMode(bool notify) {
  if (mode_.empty()) {
    // Nothing to leave. Focus cannot outlive a mode, but say so explicitly
    // rather than relying on it.
    ReleaseFocus();
    return;
  }
  ReleaseFocus();
  if (editor_ != nullptr) {
    const Status s = editor_->Deactivate();
    // A refusal is reported and then ignored: an editor does not get to keep
    // the user in a mode they have left. The alternative is a UI stuck in a
    // tool state with no way out.
    if (!s.ok()) {
      last_error_ = s;
      shell_.ReportError(s);
    }
  }
  mode_.clear();
  editor_ = nullptr;
  if (notify) shell_.OnEditorChanged(mode_, nullptr);
}

FlowResult EditorManager::EnterMode(const TypeId& id,
                                   std::shared_ptr<Overlay> adopt) {
  const OverlayTypeDesc* desc = registry_.Find(id);
  if (desc == nullptr) {
    return Fail(
        Status::Error(kNotFound, "no overlay type '" + id + "' is registered"));
  }
  OverlayEditor* editor = EditorFor(id);
  if (editor == nullptr) {
    return Fail(Status::Error(
        kUnsupported, "overlay type '" + id + "' has no editor to enter"));
  }

  // The outgoing half of the bracket, without a notification: this call has
  // one settled outcome and the shell hears about it once.
  const bool had_mode = !mode_.empty();
  ExitMode(/*notify=*/false);

  const Status s = editor->Activate();
  if (!s.ok()) {
    // Only if there was something to leave. A failed entry from no mode at all
    // is not a transition, and telling the shell edit mode was "left" would
    // have it tear down a palette that was never up.
    if (had_mode) shell_.OnEditorChanged(mode_, nullptr);  // mode_ is empty here
    return Fail(s);
  }
  mode_ = id;
  editor_ = editor;

  const FlowResult r = AdoptForMode(std::move(adopt));
  if (r != FlowResult::kDone) {
    // Invariant 1's fallback. A mode whose overlay the user refused to create
    // is not a state they asked to be in.
    ExitMode(/*notify=*/true);
    return r;
  }
  shell_.OnEditorChanged(mode_, editor_);
  return FlowResult::kDone;
}

FlowResult EditorManager::AdoptForMode(std::shared_ptr<Overlay> adopt) {
  std::shared_ptr<Overlay> target = std::move(adopt);
  // An adopt whose type does not match is a caller error, not a mode change;
  // fall back to the search rather than bracketing focus onto the wrong thing.
  if (target != nullptr && target->type_id() != mode_) target = nullptr;
  if (target == nullptr) target = manager_.FirstOfType(mode_);

  if (target == nullptr) {
    // Nothing of this type is open. Either the editor waits, or invariant 1
    // says to create one.
    if (editor_ == nullptr || !editor_->AutoEnterOnCreate()) {
      return FlowResult::kDone;
    }
    if (session_ == nullptr) {
      // No flow layer to create through, so the mode waits -- the same state
      // AutoEnterOnCreate() == false produces, and not a failure: an
      // EditorManager wired without a session is a legitimate configuration.
      return FlowResult::kDone;
    }
    const OverlayTypeDesc* desc = registry_.Find(mode_);
    const FlowResult r = desc->file.has_value()
                             ? session_->NewFileOverlay(mode_)
                             : session_->ToggleStatic(mode_);
    if (r != FlowResult::kDone) return r;  // cancel or failure: mode falls back
    target = manager_.FirstOfType(mode_);
    if (target == nullptr) {
      return Fail(Status::Error(kInternal,
                                "creating an overlay of type '" + mode_ +
                                    "' left nothing in the stack"));
    }
  }

  const Status s = manager_.MakeCurrent(target);
  if (!s.ok()) return Fail(s);
  TakeFocus(target.get());  // invariant 3d.4: enter AFTER the switch
  return FlowResult::kDone;
}

// ---------------------------------------------------------------------------
// The public verbs
// ---------------------------------------------------------------------------

FlowResult EditorManager::SetMode(const TypeId& id) {
  if (in_transition_) {
    return Fail(Status::Error(
        kInternal, "SetMode('" + id + "') entered while the editor was "
                                      "already switching"));
  }
  Transition t(*this);
  if (id.empty()) {
    ExitMode(/*notify=*/true);
    return FlowResult::kDone;
  }
  if (id == mode_) return FlowResult::kDone;  // ~ set_mode's early return
  return EnterMode(id, nullptr);
}

FlowResult EditorManager::ToggleEditor(const TypeId& id) {
  if (!id.empty() && id == mode_) return SetMode(TypeId());
  return SetMode(id);
}

FlowResult EditorManager::AutoEnterFor(
    const std::shared_ptr<Overlay>& overlay) {
  // Mid-transition this call is redundant BY CONSTRUCTION: the only way to get
  // here during one is invariant 1 creating the very overlay it is about to
  // adopt, and adopting it twice would re-enter the mode inside itself.
  if (in_transition_ || overlay == nullptr) return FlowResult::kDone;

  const TypeId& id = overlay->type_id();
  if (id.empty()) return FlowResult::kDone;
  if (id == mode_) {
    Transition t(*this);
    TakeFocus(overlay.get());
    return FlowResult::kDone;
  }
  OverlayEditor* editor = EditorFor(id);
  // No editor, or one that does not want to be entered on create: the document
  // is made and the mode is left exactly as it was.
  if (editor == nullptr || !editor->AutoEnterOnCreate()) {
    return FlowResult::kDone;
  }
  Transition t(*this);
  return EnterMode(id, overlay);
}

// ---------------------------------------------------------------------------
// The two things that happen TO this object (invariants 3d.2 and 3d.3)
// ---------------------------------------------------------------------------

void EditorManager::OnCurrentChanged(Overlay* now) {
  if (in_transition_) return;  // we caused it
  if (mode_.empty()) return;   // no editor active: nothing chases anything

  Transition t(*this);
  if (now != nullptr && now->type_id() == mode_) {
    // Same mode, different document: only the focus bracket moves. The editor
    // is per type and stays activated -- re-activating it here would discard
    // the tool state on every click in an overlay list.
    TakeFocus(now);
    return;
  }

  // ~ SwitchToEditor: the mode follows the overlay, to that overlay's editor
  // or out of edit mode altogether.
  const TypeId type = (now != nullptr) ? now->type_id() : TypeId();
  const OverlayTypeDesc* desc =
      type.empty() ? nullptr : registry_.Find(type);
  if (desc != nullptr && desc->editor_factory) {
    // Adopt the overlay that BECAME current, not the topmost of its type: the
    // user named this one.
    EnterMode(type, manager_.current_ptr());
    return;
  }
  ExitMode(/*notify=*/true);
}

// NOTE the asymmetry with OnCurrentChanged, which is deliberate: this one does
// NOT skip out on `in_transition_`. A current-overlay change can be something
// this object caused and must then ignore, but a REMOVAL never is -- the edited
// overlay is about to stop existing whoever removed it, and the focus has to be
// dropped either way.
void EditorManager::OnOverlayRemoved(Overlay& overlay) {
  if (&overlay != edited_) return;

  Transition t(*this);
  // The overlay is out of the stack but still alive (manager.h pins that
  // ordering), and this is the last moment it is -- so the release half of the
  // bracket happens here, before anything else takes focus.
  edited_ = nullptr;
  if (EditTarget* target = overlay.AsEditTarget()) target->ReleaseEditFocus();

  // Invariant 3d.3: the next overlay OF THAT TYPE, which is finer than A2's
  // "topmost remaining". Setting current here also pre-empts that fallback,
  // because OverlayManager::Remove only applies it if current is still the
  // overlay it just removed.
  std::shared_ptr<Overlay> next = manager_.FirstOfType(mode_);
  if (next != nullptr) {
    manager_.MakeCurrent(next);
    TakeFocus(next.get());
    return;
  }
  // That was the last one: the mode exits. Current is left to A2's rule, and
  // the CurrentChanged it fires finds no mode to chase.
  ExitMode(/*notify=*/true);
}

}  // namespace app
}  // namespace fv
