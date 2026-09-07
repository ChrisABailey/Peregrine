// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/overlay/point_edit.h — the editing gestures for a `.fvpoints` document.
//
// Transcribed from `RouteKit/fv_route_edit.h` rather than redesigned: the same
// undo shape, the same two-gesture add, the same Escape order, the same
// capture-on-press drag. It is here in C++ for the reason the route editor
// moved down — Pippin draws `.fvpoints` and edits it through its own sheet, and
// two shells on their way to two answers for "what does dragging a point mean"
// is the one thing a port cannot afford.
//
// A separate object from the overlay because the two have different lifetimes:
// a drag, an armed mode and an undo stack are discarded when edit focus leaves,
// while the points stay on the screen.
//
// TWO WAYS IN, and both are the same edit. A desktop shell routes raw events
// through the Overlay input SPI, which `PointOverlay` forwards here; a phone
// with its own recognizers calls `BeginDrag`/`DragTo`/`EndDrag` and the named
// commands directly.

#pragma once

#include <cstdint>
#include <vector>

#include "fvkit/app/edit_position.h"
#include "fvkit/geo.h"
#include "fvkit/overlay/overlay.h"
#include "fvkit/overlay/point_overlay.h"

namespace fv {

class PointEditSession {
 public:
  /// Borrowed; the overlay owns its session, so this is a back-reference.
  explicit PointEditSession(PointOverlay& overlay);

  PointEditSession(const PointEditSession&) = delete;
  PointEditSession& operator=(const PointEditSession&) = delete;

  PointOverlay& overlay() { return overlay_; }
  const PointOverlay& overlay() const { return overlay_; }

  // --- tool state ---------------------------------------------------------

  /// Armed by the palette or by "a", and spent by the next click. Add is two
  /// gestures for the route editor's reason: a click has to keep meaning
  /// "select", so the mode is armed separately from the position.
  bool adding() const { return adding_; }
  void SetAdding(bool on) { adding_ = on; }

  /// What a press hit-tests with, in surface pixels, ADDED to the marker's own
  /// drawn half-width by `HitTestPoint`.
  double pick_tolerance_px() const { return pick_tolerance_px_; }
  void SetPickTolerancePx(double px) { pick_tolerance_px_ = px > 0 ? px : 0; }

  /// How far a placed point looks for something exact to land on. 0 switches
  /// snapping off, the convention Pippin's `[pick] snap_tolerance` uses. The
  /// core reads no settings file: a shell that knows its device says the
  /// number.
  double snap_tolerance_px() const { return snap_tolerance_px_; }
  void SetSnapTolerancePx(double px) { snap_tolerance_px_ = px > 0 ? px : 0; }

  // --- edit focus ---------------------------------------------------------

  /// Starts true, so an overlay driven without an EditorManager — a script, or
  /// a test — is still editable.
  bool has_edit_focus() const { return has_edit_focus_; }
  void EnterEditFocus() { has_edit_focus_ = true; }
  /// Leaving edit also leaves any half-finished gesture, or the next entry
  /// starts armed for a click the user made a minute ago.
  void ReleaseEditFocus();

  // --- history ------------------------------------------------------------
  //
  // A stack of whole point lists. They are small structs; a command pattern
  // over them would be more code than the thing it undoes.

  bool CanUndo() const { return !undo_.empty(); }
  void Undo();
  bool CanRedo() const { return !redo_.empty(); }
  void Redo();
  /// What a new or newly opened document gets: an undo reaching back past a
  /// File > Open would restore points into a document they were never in.
  void ClearHistory();

  // --- the edits ----------------------------------------------------------

  void Select(int64_t id) { overlay_.SetSelected(id); }
  bool Delete(int64_t id);

  /// Adds `prototype` at `position` and selects it. Returns the new row id, or
  /// 0 when the overlay refused it.
  ///
  /// A whole prototype row rather than a set of arguments, for the reason
  /// `UpdatePoint` replaces a whole row: what an editing dialog produces is a
  /// complete point, and a per-field API would let a caller write half of one.
  int64_t AddAt(MapPoint prototype, const GeoPoint& position);

  bool MoveTo(int64_t id, const GeoPoint& position);
  /// Replaces the row, recording one undo entry. The dialog's Save.
  bool Update(const MapPoint& point);

  // --- placing a point ----------------------------------------------------

  /// The position a pixel means: snapped to an exact coordinate when one is
  /// within `snap_tolerance_px()`, un-projected otherwise.
  EditPosition ResolvePixel(PixelPoint p) const;

  /// The point whose drawn marker covers `p`, or 0. `pick_tolerance_px()` is
  /// added to the marker's half-width, so a 22-px badge is grabbable from a
  /// few pixels outside it.
  int64_t PointAt(PixelPoint p) const;

  // --- dragging -----------------------------------------------------------
  //
  // A press on a point selects it and arms a PENDING drag; the drag begins on
  // the first move that leaves the marker's own drawn bounds. That threshold
  // is the marker rather than a fixed pixel count because a point is grabbed
  // by its icon: a press-move-release inside the icon is a click, so selecting
  // a point never nudges it, and the gesture scales with `size_px` and the
  // DPI scale without a second number to keep in step.

  bool dragging() const { return drag_.active && drag_.moved; }
  /// True from the press until the release, moved or not.
  bool drag_pending() const { return drag_.active; }
  int64_t drag_id() const { return drag_.id; }

  bool BeginDrag(int64_t id, PixelPoint at);
  bool DragTo(PixelPoint at);
  bool EndDrag(PixelPoint at);
  /// Escape mid-drag: the point goes back where it was and the undo entry the
  /// first move pushed is spent putting it there, so a cancelled drag leaves
  /// no trace in the history.
  bool CancelDrag();

  // --- the Overlay input SPI, forwarded by PointOverlay -------------------

  bool OnMouseDown(const MouseEvent& e);
  bool OnMouseMove(const MouseEvent& e);
  bool OnMouseUp(const MouseEvent& e);
  bool OnKeyDown(const KeyEvent& e);

 private:
  /// Snapshot for undo, and mark the document dirty. Every mutation goes
  /// through here, which is also the only place `dirty` is set.
  void Record();

  /// Puts a whole point list back, keeping the selection when its row
  /// survived. `PointOverlay::SetPoints` clears the selection outright, which
  /// is right for a document swap and wrong for an undo.
  void Restore(std::vector<MapPoint> points);

  /// The marker's drawn half-width in surface pixels, which is both the pick
  /// reach and the drag threshold.
  double HalfWidthPx(const MapPoint& p) const;

  PointOverlay& overlay_;

  bool adding_ = false;
  bool has_edit_focus_ = true;
  double pick_tolerance_px_ = 8.0;
  double snap_tolerance_px_ = 8.0;

  std::vector<std::vector<MapPoint>> undo_;
  std::vector<std::vector<MapPoint>> redo_;

  /// `origin` and `was_dirty` are what a cancel restores; `moved` is what
  /// separates a click from an edit.
  struct Drag {
    bool active = false;
    int64_t id = 0;
    GeoPoint origin;
    PixelPoint press{0, 0};
    double threshold_px = 0.0;
    bool moved = false;
    bool was_dirty = false;
  };
  Drag drag_;
};

}  // namespace fv
