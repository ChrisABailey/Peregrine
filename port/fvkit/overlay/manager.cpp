// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// OverlayManager — see fvkit/overlay/manager.h.
//
// This is the one translation unit where L4 knows the app layer exists: the
// stack needs a type's display order and top-most flag (type_registry.h) and
// the Persistence dirty/file-spec broadcast (capabilities.h). Neither leaks
// into manager.h, so fvkit/overlay still builds against nothing but itself.

#include "fvkit/overlay/manager.h"

#include <algorithm>

#include "fvkit/app/capabilities.h"
#include "fvkit/app/type_registry.h"

namespace fv {

// ---------------------------------------------------------------------------
// The Persistence hook
// ---------------------------------------------------------------------------

// app::Persistence announces a dirty/file-spec change to ONE observer; the
// stack turns that into a StackObserver broadcast naming the OVERLAY, which is
// what a UI row actually holds. It is a separate object rather than
// OverlayManager implementing PersistenceObserver directly so that manager.h
// need not include the app-layer header (see the layering note there).
class OverlayManager::PersistenceBridge : public app::PersistenceObserver {
 public:
  explicit PersistenceBridge(OverlayManager* owner) : owner_(owner) {}

  void OnDirtyChanged(app::Persistence& p) override;
  void OnFileSpecChanged(app::Persistence& p) override;

 private:
  // A linear scan of a stack of tens, run when a document is edited -- cheaper
  // than a second map to keep in step with the stack.
  Overlay* OverlayFor(app::Persistence& p) const {
    for (const auto& o : owner_->stack_) {
      if (o->AsPersistence() == &p) return o.get();
    }
    return nullptr;
  }

  OverlayManager* owner_;
};

OverlayManager::OverlayManager()
    : bridge_(std::make_unique<PersistenceBridge>(this)) {}

OverlayManager::~OverlayManager() {
  // The bridge dies with us; anything still in the stack must stop pointing
  // at it, because an overlay can outlive the manager (shared_ptr, D1).
  for (const auto& o : stack_) {
    if (app::Persistence* p = o->AsPersistence()) p->set_observer(nullptr);
  }
}

// ---------------------------------------------------------------------------
// Type lookups. All three answer for an overlay with no type, or a type no
// registry knows, exactly as the pre-A2 stack behaved: order 0, not top-most.
// ---------------------------------------------------------------------------

const app::OverlayTypeDesc* OverlayManager::DescFor(const Overlay& o) const {
  if (registry_ == nullptr || o.type_id().empty()) return nullptr;
  return registry_->Find(o.type_id());
}

int OverlayManager::DisplayOrderOf(const Overlay& o) const {
  const app::OverlayTypeDesc* d = DescFor(o);
  return d == nullptr ? 0 : d->default_display_order;
}

bool OverlayManager::IsTopMost(const Overlay& o) const {
  const app::OverlayTypeDesc* d = DescFor(o);
  return d != nullptr && d->is_top_most;
}

// ---------------------------------------------------------------------------
// Observers
// ---------------------------------------------------------------------------

void OverlayManager::AddObserver(StackObserver* observer) {
  if (observer == nullptr) return;
  if (std::find(observers_.begin(), observers_.end(), observer) ==
      observers_.end()) {
    observers_.push_back(observer);
  }
}

void OverlayManager::RemoveObserver(StackObserver* observer) {
  observers_.erase(std::remove(observers_.begin(), observers_.end(), observer),
                   observers_.end());
}

template <typename Fn>
void OverlayManager::Notify(Fn&& fn) {
  const std::vector<StackObserver*> snapshot = observers_;
  for (StackObserver* s : snapshot) fn(*s);
}

void OverlayManager::PersistenceBridge::OnDirtyChanged(app::Persistence& p) {
  if (Overlay* o = OverlayFor(p)) {
    owner_->Notify([o](StackObserver& s) { s.OverlayDirtyChanged(*o); });
  }
}

void OverlayManager::PersistenceBridge::OnFileSpecChanged(app::Persistence& p) {
  if (Overlay* o = OverlayFor(p)) {
    owner_->Notify([o](StackObserver& s) { s.OverlayFileSpecChanged(*o); });
  }
}

void OverlayManager::SetCurrentInternal(std::shared_ptr<Overlay> now) {
  if (now == current_) return;
  Overlay* was = current_.get();
  current_ = std::move(now);
  Overlay* is_now = current_.get();
  Notify([is_now, was](StackObserver& s) { s.CurrentChanged(is_now, was); });
}

// ---------------------------------------------------------------------------
// Membership
// ---------------------------------------------------------------------------

bool OverlayManager::Contains(const std::shared_ptr<Overlay>& overlay) const {
  return std::find(stack_.begin(), stack_.end(), overlay) != stack_.end();
}

// The topmost overlay the user actually works in -- the top-most band is a
// crosshair or a HUD and is never current. Null if the stack holds nothing
// else.
Overlay* OverlayManager::TopWorkingOverlay() const {
  for (size_t i = stack_.size(); i-- > 0;) {
    if (!IsTopMost(*stack_[i])) return stack_[i].get();
  }
  return nullptr;
}

std::shared_ptr<Overlay> OverlayManager::FindShared(const Overlay* raw) const {
  for (const auto& o : stack_) {
    if (o.get() == raw) return o;
  }
  return nullptr;
}

Status OverlayManager::Add(std::shared_ptr<Overlay> overlay) {
  if (overlay == nullptr) return Status::Error(kInvalidArg, "null overlay");
  if (Contains(overlay))
    return Status::Error(kInvalidArg, "overlay already added");

  // ~ AddOverlayToStack, with the list reversed (FalconView's head is our
  // back). Walk DOWN from the top for the first overlay this one belongs
  // above; "<=" is what makes an equal display order stack newest-on-top.
  // A top-most overlay compares only against other top-most ones and stops at
  // the first overlay that is not flagged, which puts it above the lot.
  const int order = DisplayOrderOf(*overlay);
  const bool top_most = IsTopMost(*overlay);

  size_t at = 0;  // bottom, if this overlay belongs above nothing
  for (size_t i = stack_.size(); i-- > 0;) {
    const Overlay& other = *stack_[i];
    if (top_most && !IsTopMost(other)) {
      at = i + 1;
      break;
    }
    if (!top_most && IsTopMost(other)) continue;  // never above a top-most one
    if (DisplayOrderOf(other) <= order) {
      at = i + 1;
      break;
    }
  }

  Overlay* added = overlay.get();
  if (app::Persistence* p = added->AsPersistence()) p->set_observer(bridge_.get());
  stack_.insert(stack_.begin() + static_cast<ptrdiff_t>(at),
                std::move(overlay));

  Notify([added](StackObserver& s) { s.OverlayAdded(*added); });

  // FalconView's rule: an overlay that landed on top becomes current -- and a
  // top-most one (crosshair, HUD) never does, because the user does not work
  // in it. ONE DEVIATION: the original compares against the head of the whole
  // list, so an open HUD -- which is always at the head -- stops anything ever
  // becoming current again. "Topmost of the overlays the user works in" is
  // what the rule means, and it is the same rule whenever no HUD is open.
  if (!top_most && TopWorkingOverlay() == added) {
    SetCurrentInternal(FindShared(added));
  }
  return Status::Ok();
}

Status OverlayManager::Remove(const std::shared_ptr<Overlay>& overlay) {
  auto it = std::find(stack_.begin(), stack_.end(), overlay);
  if (it == stack_.end()) return Status::Error(kNotFound, "overlay not in stack");

  // Keep it alive across the notification: the observer is told AFTER the
  // removal so the stack it reads is already correct, and BEFORE the last
  // reference drops so the overlay it is handed is still there.
  std::shared_ptr<Overlay> removed = *it;
  stack_.erase(it);

  if (capture_ == removed.get()) capture_ = nullptr;
  if (app::Persistence* p = removed->AsPersistence()) p->set_observer(nullptr);

  Overlay* raw = removed.get();
  Notify([raw](StackObserver& s) { s.OverlayRemoved(*raw); });

  // Current falls to the topmost remaining overlay that is not top-most-
  // flagged; the editor-follow refinement ("next of the same type") is the
  // EditorManager's, layered on this in A4.
  if (current_ == removed) SetCurrentInternal(FindShared(TopWorkingOverlay()));
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Order
// ---------------------------------------------------------------------------

Status OverlayManager::MoveToTop(const std::shared_ptr<Overlay>& overlay) {
  auto it = std::find(stack_.begin(), stack_.end(), overlay);
  if (it == stack_.end()) return Status::Error(kNotFound, "overlay not in stack");
  if (it + 1 == stack_.end()) return Status::Ok();  // already there, no event
  std::rotate(it, it + 1, stack_.end());
  Notify([](StackObserver& s) { s.OverlayOrderChanged(); });
  return Status::Ok();
}

Status OverlayManager::MoveToBottom(const std::shared_ptr<Overlay>& overlay) {
  auto it = std::find(stack_.begin(), stack_.end(), overlay);
  if (it == stack_.end()) return Status::Error(kNotFound, "overlay not in stack");
  if (it == stack_.begin()) return Status::Ok();
  std::rotate(stack_.begin(), it, it + 1);
  Notify([](StackObserver& s) { s.OverlayOrderChanged(); });
  return Status::Ok();
}

namespace {

// Lift `move` out and re-insert it at the index `anchor` then occupies,
// shifted by `above`. Doing it as erase-then-insert (rather than a rotate)
// keeps the anchor's meaning exact when the two are far apart.
Status MoveRelative(std::vector<std::shared_ptr<Overlay>>& stack,
                    const std::shared_ptr<Overlay>& move,
                    const std::shared_ptr<Overlay>& anchor, bool above,
                    bool* moved) {
  *moved = false;
  auto mit = std::find(stack.begin(), stack.end(), move);
  auto ait = std::find(stack.begin(), stack.end(), anchor);
  if (mit == stack.end() || ait == stack.end())
    return Status::Error(kNotFound, "overlay not in stack");
  if (mit == ait)
    return Status::Error(kInvalidArg, "cannot move an overlay past itself");

  std::shared_ptr<Overlay> held = *mit;
  const size_t from = static_cast<size_t>(mit - stack.begin());
  size_t anchor_at = static_cast<size_t>(ait - stack.begin());
  stack.erase(mit);
  if (from < anchor_at) --anchor_at;  // the anchor slid down one
  const size_t to = above ? anchor_at + 1 : anchor_at;
  stack.insert(stack.begin() + static_cast<ptrdiff_t>(to), std::move(held));
  *moved = to != from;
  return Status::Ok();
}

}  // namespace

Status OverlayManager::MoveAbove(const std::shared_ptr<Overlay>& move,
                                 const std::shared_ptr<Overlay>& anchor) {
  bool moved = false;
  Status s = MoveRelative(stack_, move, anchor, /*above=*/true, &moved);
  if (s.ok() && moved) Notify([](StackObserver& o) { o.OverlayOrderChanged(); });
  return s;
}

Status OverlayManager::MoveBelow(const std::shared_ptr<Overlay>& move,
                                 const std::shared_ptr<Overlay>& anchor) {
  bool moved = false;
  Status s = MoveRelative(stack_, move, anchor, /*above=*/false, &moved);
  if (s.ok() && moved) Notify([](StackObserver& o) { o.OverlayOrderChanged(); });
  return s;
}

Status OverlayManager::Reorder(
    const std::vector<std::shared_ptr<Overlay>>& full_order) {
  if (full_order.size() != stack_.size())
    return Status::Error(kInvalidArg, "reorder is not the whole stack");
  // Every current member exactly once, nothing else. Checked before anything
  // moves, so a stale dialog cannot half-apply.
  std::vector<const Overlay*> seen;
  seen.reserve(full_order.size());
  for (const auto& o : full_order) {
    if (o == nullptr) return Status::Error(kInvalidArg, "null overlay in reorder");
    if (!Contains(o)) return Status::Error(kNotFound, "overlay not in stack");
    if (std::find(seen.begin(), seen.end(), o.get()) != seen.end())
      return Status::Error(kInvalidArg, "overlay listed twice in reorder");
    seen.push_back(o.get());
  }

  if (full_order == stack_) return Status::Ok();
  stack_ = full_order;
  Notify([](StackObserver& s) { s.OverlayOrderChanged(); });
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Current
// ---------------------------------------------------------------------------

Status OverlayManager::MakeCurrent(const std::shared_ptr<Overlay>& overlay) {
  if (overlay != nullptr && !Contains(overlay))
    return Status::Error(kNotFound, "overlay not in stack");
  SetCurrentInternal(overlay);
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::shared_ptr<Overlay> OverlayManager::FirstOfType(
    const std::string& type_id) const {
  for (size_t i = stack_.size(); i-- > 0;) {  // topmost first
    if (stack_[i]->type_id() == type_id) return stack_[i];
  }
  return nullptr;
}

std::vector<std::shared_ptr<Overlay>> OverlayManager::OfType(
    const std::string& type_id) const {
  std::vector<std::shared_ptr<Overlay>> out;
  for (size_t i = stack_.size(); i-- > 0;) {  // top-down, so front() is FirstOfType
    if (stack_[i]->type_id() == type_id) out.push_back(stack_[i]);
  }
  return out;
}

std::shared_ptr<Overlay> OverlayManager::FindByFileSpec(
    const std::string& type_id, const std::string& file_spec) const {
  if (file_spec.empty()) return nullptr;  // "not saved yet" is not an identity
  for (size_t i = stack_.size(); i-- > 0;) {
    const auto& o = stack_[i];
    if (!type_id.empty() && o->type_id() != type_id) continue;
    const app::Persistence* p = o->AsPersistence();
    if (p != nullptr && p->file_spec() == file_spec) return o;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

Status OverlayManager::CaptureMouse(Overlay* overlay) {
  if (overlay == nullptr) return Status::Error(kInvalidArg, "null overlay");
  const bool in_stack =
      std::find_if(stack_.begin(), stack_.end(),
                   [overlay](const std::shared_ptr<Overlay>& o) {
                     return o.get() == overlay;
                   }) != stack_.end();
  if (!in_stack) return Status::Error(kNotFound, "overlay not in stack");
  capture_ = overlay;
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

std::vector<Overlay*> OverlayManager::DrawOrder() const {
  std::vector<Overlay*> out;
  if (declutter_) {
    if (current_ != nullptr && current_->IsVisible()) out.push_back(current_.get());
    return out;
  }
  out.reserve(stack_.size());
  for (const auto& o : stack_) {  // bottom-up, ordinary overlays
    if (o->IsVisible() && !IsTopMost(*o)) out.push_back(o.get());
  }
  for (const auto& o : stack_) {  // then the top-most set, over everything
    if (o->IsVisible() && IsTopMost(*o)) out.push_back(o.get());
  }
  return out;
}

Status OverlayManager::DrawAll(const MapProjection& proj, ICanvas& canvas) {
  for (Overlay* o : DrawOrder()) {
    Status s = o->OnDraw(proj, canvas);
    if (!s.ok()) {
      s.message = "overlay '" + o->Name() + "': " + s.message;
      return s;
    }
  }
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------

template <typename Fn>
bool OverlayManager::RouteTopDown(Fn&& fn) {
  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
    if (!(*it)->IsVisible()) continue;
    if (fn(**it)) return true;
  }
  return false;
}

// Phases 1-3; capture (phase 0) differs between mouse and key and is applied
// by the callers. `already` is the set of overlays that have had this event
// offered to them already, which is at most a couple of pointers.
template <typename Fn>
bool OverlayManager::RouteEvent(Fn&& fn, std::vector<Overlay*>* already) {
  auto offered = [already](const Overlay& o) {
    return std::find(already->begin(), already->end(), &o) != already->end();
  };

  // 1. Direct routing: an overlay mid-gesture or holding a selection lock sees
  //    the event first even though it is not topmost. Ignores declutter, as
  //    C_ovl_mgr::select does -- the lock outranks the display mode.
  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
    Overlay& o = **it;
    if (!o.IsVisible() || offered(o)) continue;
    app::RoutingOverrides* r = o.AsRoutingOverrides();
    if (r == nullptr || !r->WantsDirectRouting()) continue;
    already->push_back(&o);
    if (fn(o)) return true;
  }

  // 2. Declutter: the current overlay is the only one that exists.
  if (declutter_) {
    if (current_ == nullptr || !current_->IsVisible()) return false;
    if (offered(*current_)) return false;
    return fn(*current_);
  }

  // 3. Ordinary top-down.
  return RouteTopDown([&](Overlay& o) { return offered(o) ? false : fn(o); });
}

namespace {

// A capture held by an overlay that has been hidden is stale -- routing skips
// invisible overlays everywhere else, and an exclusive delivery to something
// the user cannot see is the one way this mechanism could strand a gesture.
bool CaptureIsLive(Overlay* capture) {
  return capture != nullptr && capture->IsVisible();
}

}  // namespace

// Phase 0 for the mouse: a capture is EXCLUSIVE, so the phases below never run.
//
// One event, one delivery per overlay. FalconView's two loops DID re-offer an
// unhandled event to a direct-routing overlay (the second walk starts at the
// head and skips nobody); that is a deliberate deviation here, because an
// overlay counting clicks would count a declined one twice, and no behaviour
// depends on seeing it again.
template <typename Fn>
bool OverlayManager::RouteMouseEvent(Fn&& fn) {
  if (CaptureIsLive(capture_)) return fn(*capture_);
  std::vector<Overlay*> already;
  return RouteEvent(fn, &already);
}

bool OverlayManager::RouteMouseMove(const MouseEvent& e) {
  return RouteMouseEvent([&](Overlay& o) { return o.OnMouseMove(e); });
}
bool OverlayManager::RouteMouseDown(const MouseEvent& e) {
  return RouteMouseEvent([&](Overlay& o) { return o.OnMouseDown(e); });
}
bool OverlayManager::RouteMouseUp(const MouseEvent& e) {
  return RouteMouseEvent([&](Overlay& o) { return o.OnMouseUp(e); });
}
bool OverlayManager::RouteDoubleClick(const MouseEvent& e) {
  return RouteMouseEvent([&](Overlay& o) { return o.OnDoubleClick(e); });
}
bool OverlayManager::RouteMouseWheel(const MouseEvent& e, double delta) {
  return RouteMouseEvent([&](Overlay& o) { return o.OnMouseWheel(e, delta); });
}

bool OverlayManager::RouteKeyDown(const KeyEvent& e) {
  auto fn = [&](Overlay& o) { return o.OnKeyDown(e); };
  // Capture gives the key FIRST refusal, not exclusivity: Escape cancels the
  // gesture, but a shortcut the capturing overlay does not want still belongs
  // to the rest of the stack.
  std::vector<Overlay*> already;
  if (CaptureIsLive(capture_)) {
    already.push_back(capture_);
    if (fn(*capture_)) return true;
  }
  return RouteEvent(fn, &already);
}

}  // namespace fv
