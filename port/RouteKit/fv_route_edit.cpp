// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_route_edit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "fvkit/app/pick.h"
#include "fvkit/overlay/manager.h"

namespace fv {

namespace {

// How far the cursor must travel before a press becomes a drag, in surface
// pixels, Manhattan. route.py's 3, and the unit matters more than the number:
// below this a press is still a CLICK, so selecting something costs no undo
// entry and does not dirty the document. The user has to mean it.
constexpr int kDragThresholdPx = 3;

// route.py's LEG_KINDS, in its order. Great circle -> rhumb -> straight in the
// projection -> round again: three answers to "what is between these two
// points", and watching them switch over one route is the quickest way to know
// the geodesy is actually running.
constexpr LineKind kLegKindCycle[] = {LineKind::kGreatCircle, LineKind::kRhumb,
                                      LineKind::kSimple};

}  // namespace

RouteEditSession::RouteEditSession(RouteOverlay& overlay) : overlay_(overlay) {}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

void RouteEditSession::Record() {
  undo_.push_back(overlay_.waypoints());
  redo_.clear();
  overlay_.set_dirty(true);
}

void RouteEditSession::ClearHistory() {
  undo_.clear();
  redo_.clear();
}

void RouteEditSession::Undo() {
  if (undo_.empty()) return;
  redo_.push_back(overlay_.waypoints());
  std::vector<RouteWaypoint> prev = std::move(undo_.back());
  undo_.pop_back();
  // SetWaypoints drops the plan and keeps the selection only when its label
  // survived, which is exactly what an undo wants: the road was computed for
  // waypoints that are no longer there.
  overlay_.SetWaypoints(std::move(prev));
}

void RouteEditSession::Redo() {
  if (redo_.empty()) return;
  undo_.push_back(overlay_.waypoints());
  std::vector<RouteWaypoint> next = std::move(redo_.back());
  redo_.pop_back();
  overlay_.SetWaypoints(std::move(next));
}

void RouteEditSession::ReleaseEditFocus() {
  CancelDrag();
  has_edit_focus_ = false;
  adding_ = false;
}

// ---------------------------------------------------------------------------
// The edits
// ---------------------------------------------------------------------------

size_t RouteEditSession::InsertIndex() const {
  const std::vector<RouteWaypoint>& wps = overlay_.waypoints();
  for (size_t i = 0; i < wps.size(); ++i) {
    if (wps[i].label == overlay_.selected()) return i + 1;
  }
  return wps.size();
}

std::string RouteEditSession::NewLabel() const {
  const std::vector<RouteWaypoint>& wps = overlay_.waypoints();
  size_t n = wps.size() + 1;
  for (;;) {
    const std::string candidate = "WP" + std::to_string(n);
    bool used = false;
    for (const RouteWaypoint& w : wps) {
      if (w.label == candidate) { used = true; break; }
    }
    if (!used) return candidate;
    ++n;
  }
}

bool RouteEditSession::Delete(const std::string& label) {
  const std::vector<RouteWaypoint>& wps = overlay_.waypoints();
  bool found = false;
  for (const RouteWaypoint& w : wps) {
    if (w.label == label) { found = true; break; }
  }
  if (!found) return false;

  Record();
  std::vector<RouteWaypoint> kept;
  kept.reserve(wps.size() - 1);
  for (const RouteWaypoint& w : wps) {
    if (w.label != label) kept.push_back(w);
  }
  overlay_.SetWaypoints(std::move(kept));
  return true;
}

std::string RouteEditSession::AddAt(const GeoPoint& position) {
  const std::string label = NewLabel();
  Record();
  std::vector<RouteWaypoint> wps = overlay_.waypoints();
  wps.insert(wps.begin() + static_cast<long>(InsertIndex()),
             RouteWaypoint{label, position});
  overlay_.SetWaypoints(std::move(wps));
  // Selected, so the NEXT add lands after this one and a route is built by
  // clicking along it rather than by re-selecting between every point.
  overlay_.SetSelected(label);
  return label;
}

bool RouteEditSession::MoveTo(const std::string& label,
                              const GeoPoint& position) {
  std::vector<RouteWaypoint> wps = overlay_.waypoints();
  bool found = false;
  for (RouteWaypoint& w : wps) {
    if (w.label == label) {
      w.position = position;
      found = true;
      break;
    }
  }
  if (!found) return false;
  // SetWaypoints drops the plan, and dropping it is the honest thing: a
  // followed road was computed for waypoints that have now moved, so leaving
  // it on screen would show a road line joining somewhere the route is not.
  overlay_.SetWaypoints(std::move(wps));
  return true;
}

// ---------------------------------------------------------------------------
// The plan
// ---------------------------------------------------------------------------

RoutePlanOptions RouteEditSession::BicycleOptions() const {
  RoutePlanOptions options;
  options.cycle_only = true;
  // The rule file's own bicycle profile when it defines one, so "b" follows
  // tuned weights; the pre-O5c boolean otherwise, which is the same ride
  // priced more crudely. route.py's `_bike_profile`, moved.
  const RoutePlanner* planner = overlay_.planner();
  if (planner != nullptr) {
    for (const std::string& name : planner->profile_names()) {
      if (name == "bicycle") {
        options.profile = name;
        break;
      }
    }
  }
  return options;
}

bool RouteEditSession::FollowRoads() { return overlay_.FollowRoads(); }

bool RouteEditSession::FollowRoadsByBicycle() {
  return overlay_.FollowRoads(BicycleOptions());
}

LineKind RouteEditSession::CycleLegKind() {
  const LineKind now = overlay_.leg_kind();
  const size_t n = sizeof(kLegKindCycle) / sizeof(kLegKindCycle[0]);
  size_t i = 0;
  for (; i < n; ++i) {
    if (kLegKindCycle[i] == now) break;
  }
  // An unknown value (nobody can produce one today) restarts the cycle rather
  // than running off the end.
  const LineKind next = kLegKindCycle[(i + 1) % n];
  overlay_.SetLegKind(next);
  return next;
}

// ---------------------------------------------------------------------------
// Placing a point
// ---------------------------------------------------------------------------

EditPosition RouteEditSession::ResolvePixel(PixelPoint p) const {
  EditPosition out;
  if (!overlay_.has_projection()) return out;
  const MapProjection& proj = overlay_.last_projection();

  // THE SNAP COMES FIRST, because it is the exact answer and the un-projection
  // is the approximate one. A pixel un-projects to wherever that pixel's
  // centre happens to land — the noise a snap exists to remove.
  OverlayManager* manager = overlay_.manager();
  if (manager != nullptr && snap_tolerance_px_ > 0.0) {
    const std::vector<app::SnapToItem> candidates =
        app::SnapCandidates(*manager, proj, p, snap_tolerance_px_);
    for (const app::SnapToItem& c : candidates) {
      // THE OVERLAY EXCLUDES ITSELF, and without this line no drag can move.
      // `RouteOverlay::SnapToPoint` answers out of what it DREW, and the
      // waypoint being dragged is drawn under the cursor — distance ~0, first
      // in a nearest-first list, every frame. The waypoint would snap to
      // itself and sit still.
      //
      // Excluding the whole overlay rather than only the dragged label is also
      // the right rule on its own terms: two waypoints of ONE route collapsing
      // onto each other is not an edit anybody asked for. Snapping to another
      // route's waypoints — P19's stated payoff, starting a route where the
      // last one ended — is untouched, and so is snapping to a point set.
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

// ---------------------------------------------------------------------------
// Dragging
// ---------------------------------------------------------------------------

bool RouteEditSession::BeginDrag(const std::string& label, PixelPoint at) {
  const RouteWaypoint* found = nullptr;
  for (const RouteWaypoint& w : overlay_.waypoints()) {
    if (w.label == label) { found = &w; break; }
  }
  if (found == nullptr) return false;

  drag_ = Drag{};
  drag_.active = true;
  drag_.label = label;
  drag_.origin = found->position;
  drag_.press = at;
  drag_.moved = false;
  drag_.was_dirty = overlay_.is_dirty();

  // Capture is phase 0 of the manager's routing: once the press is taken every
  // move belongs to this overlay, whatever is drawn on top of it and wherever
  // the cursor wanders. A null manager is legal (a plain script, or a test) —
  // the gesture still works, it is just interruptible.
  OverlayManager* manager = overlay_.manager();
  if (manager != nullptr) manager->CaptureMouse(&overlay_);
  return true;
}

bool RouteEditSession::DragTo(PixelPoint at) {
  if (!drag_.active) return false;
  if (!drag_.moved) {
    if (std::abs(at.x - drag_.press.x) + std::abs(at.y - drag_.press.y) <
        kDragThresholdPx) {
      return true;  // a still hand: still just a click
    }
    // The first real movement is what makes this an EDIT. One undo snapshot
    // per drag, taken HERE rather than at the press, so the whole drag undoes
    // as the single thing the user did.
    Record();
    drag_.moved = true;
  }
  const EditPosition p = ResolvePixel(at);
  if (!p.valid) return true;  // off the projection: hold position
  MoveTo(drag_.label, p.position);
  return true;
}

bool RouteEditSession::EndDrag(PixelPoint at) {
  if (!drag_.active) return false;
  if (drag_.moved) DragTo(at);  // commit the release position
  drag_ = Drag{};
  OverlayManager* manager = overlay_.manager();
  if (manager != nullptr) manager->ReleaseMouse();
  return true;
}

bool RouteEditSession::CancelDrag() {
  if (!drag_.active) return false;
  const Drag d = drag_;
  drag_ = Drag{};
  OverlayManager* manager = overlay_.manager();
  if (manager != nullptr) manager->ReleaseMouse();

  if (!d.moved) return true;  // nothing happened; nothing to undo
  if (!undo_.empty()) {
    // The snapshot the first move pushed IS the pre-drag route, so spending it
    // restores the position and the history in one step.
    std::vector<RouteWaypoint> before = std::move(undo_.back());
    undo_.pop_back();
    overlay_.SetWaypoints(std::move(before));
  } else {
    MoveTo(d.label, d.origin);
  }
  if (overlay_.is_dirty() && !d.was_dirty) overlay_.set_dirty(false);
  return true;
}

// ---------------------------------------------------------------------------
// The Overlay input SPI
// ---------------------------------------------------------------------------

bool RouteEditSession::OnMouseDown(const MouseEvent& e) {
  // Not the overlay being edited: a click on it is somebody else's business
  // (the pick session's, usually). Declining is what lets two routes be open
  // at once without both eating the same click.
  if (!has_edit_focus_) return false;

  const PixelPoint at{e.x, e.y};
  if (adding_) {
    const EditPosition p = ResolvePixel(at);
    adding_ = false;  // spent either way; an unusable pixel is not a retry
    if (!p.valid) return true;
    AddAt(p.position);
    return true;
  }

  // ONE hit test, shared with the hover and the context menu.
  std::vector<app::HitItem> hits;
  if (overlay_.has_projection()) {
    overlay_.HitTestPoint(overlay_.last_projection(), at, pick_tolerance_px_,
                          hits);
  }
  if (hits.empty()) return false;

  const app::HitItem* nearest = &hits.front();
  for (const app::HitItem& h : hits) {
    if (h.distance_px < nearest->distance_px) nearest = &h;
  }
  const std::string label = overlay_.LabelForFeature(nearest->feature);
  if (label.empty()) return false;

  overlay_.SetSelected(label);
  // A press on a waypoint is the START of a drag, not yet a move.
  BeginDrag(label, at);
  return true;
}

bool RouteEditSession::OnMouseMove(const MouseEvent& e) {
  return DragTo(PixelPoint{e.x, e.y});
}

bool RouteEditSession::OnMouseUp(const MouseEvent& e) {
  return EndDrag(PixelPoint{e.x, e.y});
}

bool RouteEditSession::OnKeyDown(const KeyEvent& e) {
  if (!has_edit_focus_) return false;

  if (e.key == Key::kEscape) {
    // Escape backs out of one thing at a time — the mode first, since that is
    // the one that changes what a click will do. A drag in flight outranks
    // even that: the manager gives the CAPTURING overlay the key first
    // precisely so a gesture can be cancelled.
    if (CancelDrag()) return true;
    if (adding_) {
      adding_ = false;
      return true;
    }
    if (overlay_.has_plan() || !overlay_.status().empty()) {
      overlay_.ClearRoads();
      return true;
    }
    if (!overlay_.selected().empty()) {
      overlay_.SetSelected(std::string());
      return true;
    }
    return false;  // nothing to cancel: let the app quit
  }
  if (e.key == 'A') {
    adding_ = !adding_;
    return true;
  }
  if (e.key == 'R') {
    FollowRoads();
    return true;
  }
  if (e.key == 'G') {
    CycleLegKind();
    return true;
  }
  if (e.key == 'B') {
    FollowRoadsByBicycle();
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
  if (overlay_.selected().empty()) return false;
  if (e.key == Key::kDelete || e.key == Key::kBackspace || e.key == 'D') {
    Delete(overlay_.selected());
    return true;
  }
  return false;
}

}  // namespace fv
