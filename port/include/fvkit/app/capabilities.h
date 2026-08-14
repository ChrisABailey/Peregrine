// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/app/capabilities.h — the optional interfaces an overlay may implement
// (fvkit-app-plan.md §3b).
//
// FalconView discovers these with `dynamic_cast` at the call site
// (IFvOverlayPersistence, OverlayContextMenu_Interface, ...). The same
// optionality is kept, but discovery is by VIRTUAL ACCESSOR (rule R2):
// `Overlay::AsPersistence()` returns `this` in a capable overlay and nullptr
// otherwise. The reason is not taste -- `dynamic_cast` across a pybind11
// trampoline is unreliable, and the L4 header already committed to a single
// overlay base class for exactly that lifetime/binding reason (D1).
//
// Everything here is behaviour an overlay opts into. Nothing here is required:
// an overlay that overrides none of the accessors is still a legal overlay and
// draws and routes exactly as it did before the app layer existed (R7).
//
// Not in scope, each one more accessor when its consumer arrives (plan §4):
// playback/view-time, vertical view, the tabular editor, OLE drag-drop.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fvkit/app/shell.h"  // CursorId, HintText, MenuNode
#include "fvkit/geo.h"        // GeoPoint, PixelPoint, Status
#include "fvkit/overlay/overlay.h"
#include "fvkit/proj.h"

namespace fv {
namespace app {

class Persistence;

// ---------------------------------------------------------------------------
// Persistence — a file overlay's document state (~ IFvOverlayPersistence)
// ---------------------------------------------------------------------------

// The stack's half of the dirty/file-spec broadcast. A2's OverlayManager
// installs itself here on Add and forwards to StackObserver::
// OverlayDirtyChanged / OverlayFileSpecChanged; until then the hook is simply
// null and the state is still correct, which is what A1 tests against.
class PersistenceObserver {
 public:
  virtual ~PersistenceObserver() = default;
  virtual void OnDirtyChanged(Persistence&) {}
  virtual void OnFileSpecChanged(Persistence&) {}
};

class Persistence {
 public:
  virtual ~Persistence() = default;

  // Full path. Empty until the document has been opened or saved once.
  const std::string& file_spec() const { return file_spec_; }
  // Has unsaved edits -- what makes Close prompt (plan §3g).
  bool is_dirty() const { return dirty_; }
  // Has ever reached disk. A dirty overlay that has NOT been saved goes
  // through the Save As flow rather than a silent Save (~ m_bHasBeenSaved).
  bool has_been_saved() const { return has_been_saved_; }
  bool is_read_only() const { return read_only_; }
  // Which `FileTypeDesc::save_filters` entry this document was last written
  // through; 0 = the type's default format. BEYOND THE PLAN, added in A3 for a
  // reason the plan did not see: Save-after-Save-As has to re-save in the
  // format the user picked, and with nowhere to remember it every plain Save
  // would silently rewrite a document in format 0. The session owns it, the
  // same way it owns has_been_saved.
  int save_format_index() const { return save_format_index_; }

  // Setters fire the observer only on an actual CHANGE -- "dirty changed" is
  // the notification, so a redundant set_dirty(true) must not repaint an
  // overlay list. The session flows (A3) own has_been_saved/read_only.
  void set_dirty(bool v) {
    if (dirty_ == v) return;
    dirty_ = v;
    if (observer_) observer_->OnDirtyChanged(*this);
  }
  void set_file_spec(std::string spec) {
    if (file_spec_ == spec) return;
    file_spec_ = std::move(spec);
    if (observer_) observer_->OnFileSpecChanged(*this);
  }
  void set_has_been_saved(bool v) { has_been_saved_ = v; }
  void set_read_only(bool v) { read_only_ = v; }
  void set_save_format_index(int v) { save_format_index_ = v; }

  void set_observer(PersistenceObserver* o) { observer_ = o; }

  // A fresh, empty document. The overlay picks its own default file spec
  // (FalconView numbered them "Route1", "Route2", ...).
  virtual Status FileNew() = 0;
  virtual Status FileOpen(const std::string& spec) = 0;
  // `format_index` indexes FileTypeDesc::save_filters; 0 = the default format.
  // Same contract as FalconView's nSaveFormat, minus the filter-string encoding.
  virtual Status FileSaveAs(const std::string& spec, int format_index) = 0;

  // Re-read from disk, discarding edits. Offered when the user opens a file
  // that is already open and dirty (plan §1.2).
  virtual bool SupportsRevert() const { return false; }
  virtual Status Revert(const std::string& spec) {
    (void)spec;
    return Status::Error(kUnsupported, "overlay does not support revert");
  }

 private:
  std::string file_spec_;
  bool dirty_ = false;
  bool has_been_saved_ = false;
  bool read_only_ = false;
  int save_format_index_ = 0;
  PersistenceObserver* observer_ = nullptr;
};

// ---------------------------------------------------------------------------
// HitTest — "what is under the cursor" (~ test_select/selected, made explicit)
// ---------------------------------------------------------------------------

// One pickable thing under the point. `hint` and `cursor` are what a HOVER
// would show and what a CLICK would feel like -- the two halves of
// FalconView's test_select, carried on the candidate instead of set as a side
// effect on the view.
struct HitItem {
  Overlay* overlay = nullptr;
  // Overlay-scoped feature id. Deliberately wide enough for the ids
  // fvkit/vector/pick.h's PickIndex already emits, so a vector overlay's
  // HitTestPoint is a thin adapter over the pick index it already builds (A5).
  uint64_t feature = 0;
  double distance_px = 0.0;  // screen distance, for nearest-wins policies
  HintText hint;
  CursorId cursor = CursorId::kDefault;
};

class HitTest {
 public:
  virtual ~HitTest() = default;
  // Everything within `tolerance_px` of `p`, best first. APPENDS to `out` --
  // the pick session aggregates across the whole stack into one vector (A5).
  // Called on every mouse move, so it must be cheap.
  virtual void HitTestPoint(const MapProjection& proj, PixelPoint p,
                            double tolerance_px, std::vector<HitItem>& out) = 0;
};

// ---------------------------------------------------------------------------
// SnapTo — "what point would I snap to here" (~ test_snap_to/do_snap_to)
// ---------------------------------------------------------------------------

// Unlike HitTest, snap-to is collected from ALL overlays and the user is asked
// when more than one answers (FalconView's snptodlg). That is why it is a
// separate capability rather than a flavour of HitItem: the deconfliction rule
// is different (plan §1.6).
struct SnapToItem {
  GeoPoint point;
  std::string description;   // the chooser's row text
  Overlay* overlay = nullptr;
};

class SnapTo {
 public:
  virtual ~SnapTo() = default;
  virtual void SnapToPoint(const MapProjection& proj, PixelPoint p,
                           double tolerance_px,
                           std::vector<SnapToItem>& out) = 0;
};

// ---------------------------------------------------------------------------
// ContextMenu (~ OverlayContextMenu_Interface)
// ---------------------------------------------------------------------------

class ContextMenu {
 public:
  virtual ~ContextMenu() = default;
  // Append this overlay's items for the point. The pick layer walks visible
  // overlays top-down and each one appends its own section (A5).
  virtual void AppendMenuItems(const MapProjection& proj, PixelPoint p,
                               MenuNode& menu) = 0;
};

// ---------------------------------------------------------------------------
// RoutingOverrides (~ OverlayUIEventRoutingOverrides — the selection lock)
// ---------------------------------------------------------------------------

class RoutingOverrides {
 public:
  virtual ~RoutingOverrides() = default;
  // True while this overlay must see the mouse FIRST even though it is not
  // topmost -- mid-gesture, region select. The stack's direct-routing pre-pass
  // (A2) consults it on every event, so it is a plain flag read.
  virtual bool WantsDirectRouting() const = 0;
};

// ---------------------------------------------------------------------------
// EditTarget — the overlay half of the editor contract
// ---------------------------------------------------------------------------

// The editor (A4) is per TYPE; this is per INSTANCE. Focus is bracketed
// exactly: the overlay losing edit gets ReleaseEditFocus before the switch,
// the one gaining it EnterEditFocus after -- and only for the overlay that is
// actually being EDITED, which is not merely the current one (plan §3d.4).
class EditTarget {
 public:
  virtual ~EditTarget() = default;
  virtual void EnterEditFocus() {}
  virtual void ReleaseEditFocus() {}
  virtual bool CanUndo() const { return false; }
  virtual void Undo() {}
  virtual bool CanRedo() const { return false; }
  virtual void Redo() {}
};

}  // namespace app
}  // namespace fv
