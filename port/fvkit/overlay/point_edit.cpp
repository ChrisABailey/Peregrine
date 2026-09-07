// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/overlay/point_edit.h"

#include <cmath>

#include "fvkit/app/pick.h"
#include "fvkit/overlay/manager.h"

namespace fv {

namespace {

// The smallest drag threshold, in surface pixels. A marker's own half-width is
// the threshold (see the header), but a document may author a 2-px point, and a
// point nobody can select without moving is worse than one that is hard to
// grab.
constexpr double kMinDragThresholdPx = 3.0;

}  // namespace

PointEditSession::PointEditSession(PointOverlay& overlay) : overlay_(overlay) {}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

void PointEditSession::Record() {
  undo_.push_back(overlay_.points());
  redo_.clear();
  overlay_.set_dirty(true);
}

void PointEditSession::ClearHistory() {
  undo_.clear();
  redo_.clear();
}

// SetPoints clears the selection, so it is carried across by hand and restored
// only when the row it names survived the restore — a selection pointing at a
// point that is no longer there draws nothing and deletes nothing.
void PointEditSession::Restore(std::vector<MapPoint> points) {
  const int64_t was = overlay_.selected();
  overlay_.SetPoints(std::move(points));
  if (was != 0 && overlay_.Find(was) != nullptr) overlay_.SetSelected(was);
}

void PointEditSession::Undo() {
  if (undo_.empty()) return;
  redo_.push_back(overlay_.points());
  std::vector<MapPoint> prev = std::move(undo_.back());
  undo_.pop_back();
  Restore(std::move(prev));
}

void PointEditSession::Redo() {
  if (redo_.empty()) return;
  undo_.push_back(overlay_.points());
  std::vector<MapPoint> next = std::move(redo_.back());
  redo_.pop_back();
  Restore(std::move(next));
}

void PointEditSession::ReleaseEditFocus() {
  CancelDrag();
  has_edit_focus_ = false;
  adding_ = false;
}

// ---------------------------------------------------------------------------
// The edits
// ---------------------------------------------------------------------------

bool PointEditSession::Delete(int64_t id) {
  if (overlay_.Find(id) == nullptr) return false;
  Record();
  overlay_.RemovePoint(id);
  if (overlay_.selected() == id) overlay_.SetSelected(0);
  return true;
}

int64_t PointEditSession::AddAt(MapPoint prototype, const GeoPoint& position) {
  Record();
  prototype.id = 0;  // a prototype is a template, never an existing row
  prototype.position = position;
  const int64_t id = overlay_.AddPoint(std::move(prototype));
  // Selected, so the dialog that follows and the delete key both act on what
  // was just placed.
  if (id != 0) overlay_.SetSelected(id);
  return id;
}

bool PointEditSession::MoveTo(int64_t id, const GeoPoint& position) {
  const MapPoint* found = overlay_.Find(id);
  if (found == nullptr) return false;
  MapPoint moved = *found;
  moved.position = position;
  return overlay_.UpdatePoint(moved);
}

bool PointEditSession::Update(const MapPoint& point) {
  if (overlay_.Find(point.id) == nullptr) return false;
  Record();
  return overlay_.UpdatePoint(point);
}

// ---------------------------------------------------------------------------
// Placing a point
// ---------------------------------------------------------------------------

double PointEditSession::HalfWidthPx(const MapPoint& p) const {
  const double drawn = p.size_px * overlay_.symbol_dpi_scale();
  return drawn > 0.0 ? drawn / 2.0 : 0.0;
}

EditPosition PointEditSession::ResolvePixel(PixelPoint p) const {
  EditPosition out;
  if (!overlay_.has_projection()) return out;
  const MapProjection& proj = overlay_.last_projection();

  // The snap comes first, because it is the exact answer and the un-projection
  // is the approximate one.
  OverlayManager* manager = overlay_.manager();
  if (manager != nullptr && snap_tolerance_px_ > 0.0) {
    const std::vector<app::SnapToItem> candidates =
        app::SnapCandidates(*manager, proj, p, snap_tolerance_px_);
    for (const app::SnapToItem& c : candidates) {
      // The overlay excludes itself, and without this line no drag can move:
      // `SnapToPoint` answers out of what it DREW, so the point under the
      // cursor is its own nearest candidate every frame and would snap to
      // itself. Excluding the whole overlay is also right on its own terms —
      // two points of one document collapsing onto each other is not an edit
      // anybody asked for — and snapping to ANOTHER point set or to a route's
      // waypoints is untouched.
      if (c.overlay == &overlay_) continue;
      out.position = c.point;
      out.valid = true;
      out.snapped = true;
      out.snapped_to = c.description;
      return out;
    }
  }

  GeoPoint g;
  if (!proj.SurfaceToGeo(p.x, p.y, &g).ok()) return out;
  out.position = g;
  out.valid = true;
  return out;
}

int64_t PointEditSession::PointAt(PixelPoint p) const {
  if (!overlay_.has_projection()) return 0;
  std::vector<app::HitItem> hits;
  const_cast<PointOverlay&>(overlay_).HitTestPoint(
      overlay_.last_projection(), p, pick_tolerance_px_, hits);
  if (hits.empty()) return 0;
  const app::HitItem* nearest = &hits.front();
  for (const app::HitItem& h : hits) {
    if (h.distance_px < nearest->distance_px) nearest = &h;
  }
  return nearest->feature;
}

// ---------------------------------------------------------------------------
// Dragging
// ---------------------------------------------------------------------------

bool PointEditSession::BeginDrag(int64_t id, PixelPoint at) {
  const MapPoint* found = overlay_.Find(id);
  if (found == nullptr) return false;

  drag_ = Drag{};
  drag_.active = true;
  drag_.id = id;
  drag_.origin = found->position;
  drag_.press = at;
  drag_.threshold_px = std::max(HalfWidthPx(*found), kMinDragThresholdPx);
  drag_.was_dirty = overlay_.is_dirty();

  // Capture is phase 0 of the manager's routing: once the press is taken every
  // move belongs to this overlay, wherever the cursor wanders. A null manager
  // is legal — the gesture still works, it is just interruptible.
  OverlayManager* manager = overlay_.manager();
  if (manager != nullptr) manager->CaptureMouse(&overlay_);
  return true;
}

bool PointEditSession::DragTo(PixelPoint at) {
  if (!drag_.active) return false;
  if (!drag_.moved) {
    const double dx = at.x - drag_.press.x;
    const double dy = at.y - drag_.press.y;
    // Still inside the marker the press landed on: this is a click that has
    // not made up its mind.
    if (std::sqrt(dx * dx + dy * dy) < drag_.threshold_px) return true;
    // The first movement past the icon is what makes this an EDIT. One undo
    // snapshot per drag, taken here rather than at the press, so the whole
    // drag undoes as the single thing the user did.
    Record();
    drag_.moved = true;
  }
  const EditPosition p = ResolvePixel(at);
  if (!p.valid) return true;  // off the projection: hold position
  MoveTo(drag_.id, p.position);
  return true;
}

bool PointEditSession::EndDrag(PixelPoint at) {
  if (!drag_.active) return false;
  if (drag_.moved) DragTo(at);  // commit the release position
  drag_ = Drag{};
  OverlayManager* manager = overlay_.manager();
  if (manager != nullptr) manager->ReleaseMouse();
  return true;
}

bool PointEditSession::CancelDrag() {
  if (!drag_.active) return false;
  const Drag d = drag_;
  drag_ = Drag{};
  OverlayManager* manager = overlay_.manager();
  if (manager != nullptr) manager->ReleaseMouse();

  if (!d.moved) return true;  // nothing happened; nothing to undo
  if (!undo_.empty()) {
    // The snapshot the first move pushed IS the pre-drag document, so spending
    // it restores the position and the history in one step.
    std::vector<MapPoint> before = std::move(undo_.back());
    undo_.pop_back();
    Restore(std::move(before));
  } else {
    MoveTo(d.id, d.origin);
  }
  if (overlay_.is_dirty() && !d.was_dirty) overlay_.set_dirty(false);
  return true;
}

// ---------------------------------------------------------------------------
// The Overlay input SPI
// ---------------------------------------------------------------------------

bool PointEditSession::OnMouseDown(const MouseEvent& e) {
  // Not the overlay being edited: a click on it is the pick session's
  // business. Declining is what lets several point sets be open at once
  // without all of them eating the same click.
  if (!has_edit_focus_) return false;

  const PixelPoint at{e.x, e.y};
  if (adding_) {
    const EditPosition p = ResolvePixel(at);
    adding_ = false;  // spent either way; an unusable pixel is not a retry
    if (!p.valid) return true;
    AddAt(MapPoint{}, p.position);
    return true;
  }

  const int64_t id = PointAt(at);
  if (id == 0) return false;
  overlay_.SetSelected(id);
  // A press on a point is the start of a drag, not yet a move.
  BeginDrag(id, at);
  return true;
}

bool PointEditSession::OnMouseMove(const MouseEvent& e) {
  return DragTo(PixelPoint{e.x, e.y});
}

bool PointEditSession::OnMouseUp(const MouseEvent& e) {
  return EndDrag(PixelPoint{e.x, e.y});
}

bool PointEditSession::OnKeyDown(const KeyEvent& e) {
  if (!has_edit_focus_) return false;

  if (e.key == Key::kEscape) {
    // Escape backs out of one thing at a time, the mode first since that is
    // what changes the meaning of a click. A drag in flight outranks even
    // that: the manager gives the capturing overlay the key first precisely so
    // a gesture can be cancelled.
    if (CancelDrag()) return true;
    if (adding_) {
      adding_ = false;
      return true;
    }
    if (overlay_.selected() != 0) {
      overlay_.SetSelected(0);
      return true;
    }
    return false;  // nothing to cancel: let the app quit
  }
  if (e.key == 'A') {
    adding_ = !adding_;
    return true;
  }
  if (e.key == 'U' || (e.ctrl && e.key == 'Z')) {
    if (e.shift) {
      Redo();
    } else {
      Undo();
    }
    return true;
  }
  if (overlay_.selected() == 0) return false;
  if (e.key == Key::kDelete || e.key == Key::kBackspace || e.key == 'D') {
    Delete(overlay_.selected());
    return true;
  }
  return false;
}

}  // namespace fv
