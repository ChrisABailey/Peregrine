// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RouteKit/fv_route_edit.h — the route EDITOR, in C++ at last.
//
// P5 moved route.py's document, plan, picture and pick into `fv::RouteOverlay`
// and said in as many words what it was leaving behind: "WHAT IS DELIBERATELY
// NOT HERE, and it is most of route.py's line count: the editor." That left
// the port with the drawing written twice and the EDITING written once, in the
// language the phone cannot call — so PythonView could drag a waypoint and
// Pippin could only replace the whole list.
//
// This is that editor, transcribed from `port/apps/route.py` rather than
// redesigned: the same gestures, the same undo shape, the same Escape order,
// the same labels. What changes is who can reach it.
//
// WHY A SEPARATE OBJECT AND NOT MORE METHODS ON THE OVERLAY. The overlay is
// what is on the map; this is what the user is DOING to it, and the two have
// different lifetimes — a drag, an armed mode and an undo stack are all
// discarded when edit focus leaves, while the route stays on the screen. It is
// also what keeps `RouteOverlay` readable for the shells that never edit
// (Pippin's ride view draws a route it does not touch).
//
// TWO WAYS IN, AND BOTH ARE THE SAME EDIT. A desktop shell routes raw events
// through `OnMouseDown/Move/Up` and `OnKeyDown` — the Overlay SPI, which the
// stack already dispatches, complete with mouse capture. A phone has its own
// recognizers and no key at all, so it drives `BeginDrag/DragTo/EndDrag` and
// the named commands directly. Neither is a wrapper over the other: they are
// the same functions entered at different depths, which is the whole point of
// moving this down here.
//
// SNAPPING IS PART OF PLACING A POINT, not a feature bolted beside it. Every
// position this editor produces — a click that adds, a drag that moves —
// comes from `ResolvePixel`, which asks `fv::app::SnapCandidates` (P19) before
// it falls back to un-projecting the pixel. So a waypoint dropped over a
// `.fvpoints` marker takes that marker's SURVEYED coordinate, and a route that
// starts where another one ended starts there exactly. The two things worth
// not re-deriving are in `ResolvePixel`'s own comment.

#pragma once

#include <string>
#include <vector>

#include "fv_route_doc.h"
#include "fv_route_overlay.h"
#include "fv_route_planner.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/geo.h"
#include "fvkit/overlay/overlay.h"

namespace fv {

// What a pixel means, once the snapper has had its say.
//
// Returned rather than just a GeoPoint because a shell wants to SAY so: P19's
// button reads "Use Ruddy Turnstone" and its ring changes colour, and a
// desktop status bar can do the same with one string. A snap that happens
// silently is indistinguishable from a drag that missed.
struct EditPosition {
  GeoPoint position;
  // False when there is no projection yet (nothing has drawn this overlay) or
  // the pixel is off it. A caller that gets this holds its position — it does
  // not guess a coordinate.
  bool valid = false;
  bool snapped = false;
  // The candidate's own description ("Points: Ruddy Turnstone"), empty when
  // nothing was snapped to.
  std::string snapped_to;
};

class RouteEditSession {
 public:
  // Borrowed, and it must outlive the session. In practice the overlay OWNS
  // its session (`RouteOverlay::edit()`), so this is a back-reference and not
  // a lifetime anybody has to think about.
  explicit RouteEditSession(RouteOverlay& overlay);

  RouteEditSession(const RouteEditSession&) = delete;
  RouteEditSession& operator=(const RouteEditSession&) = delete;

  RouteOverlay& overlay() { return overlay_; }
  const RouteOverlay& overlay() const { return overlay_; }

  // --- tool state ---------------------------------------------------------

  // Armed by "a" and spent by the next click. WHY ADD IS TWO GESTURES AND NOT
  // ONE, route.py's reason unchanged: a click has to keep meaning "select" —
  // an editor where every click on open map appends a point cannot be used to
  // pick anything — so the key arms the mode and the click supplies the
  // position.
  bool adding() const { return adding_; }
  void SetAdding(bool on) { adding_ = on; }

  // What a press hit-tests with, in surface pixels. 8 is route.py's number and
  // is ADDED to the marker's own drawn half-width by `HitTestPoint`, so a
  // 14-pixel diamond is grabbable from about 15 pixels out.
  double pick_tolerance_px() const { return pick_tolerance_px_; }
  void SetPickTolerancePx(double px) { pick_tolerance_px_ = px > 0 ? px : 0; }

  // How far a placed point looks for something exact to land on. 8 by default
  // — the same reach as a pick, because a snap the user cannot predict is
  // worse than no snap — and 0 switches snapping off entirely, which is the
  // convention Pippin's `[pick] snap_tolerance` already uses.
  //
  // THE CORE READS NO SETTINGS FILE. A shell that knows its device says the
  // number; a shell that says nothing gets the mouse-pointer default.
  double snap_tolerance_px() const { return snap_tolerance_px_; }
  void SetSnapTolerancePx(double px) { snap_tolerance_px_ = px > 0 ? px : 0; }

  // --- edit focus (the overlay's EditTarget half) -------------------------

  // Starts TRUE, and that is deliberate: an overlay driven without an
  // `EditorManager` — a plain script, or a test — never hears either call, and
  // must still be editable.
  bool has_edit_focus() const { return has_edit_focus_; }
  void EnterEditFocus() { has_edit_focus_ = true; }
  // Leaving edit must also leave any half-finished gesture, or the next entry
  // starts armed for a click the user made a minute ago. A drag in flight is
  // the same thing one step further along, so it is CANCELLED rather than
  // abandoned mid-move.
  void ReleaseEditFocus();

  // --- history ------------------------------------------------------------
  //
  // A stack of whole waypoint lists. They are a handful of small structs; a
  // command pattern over them would be more code than the thing it is undoing.

  bool CanUndo() const { return !undo_.empty(); }
  void Undo();
  bool CanRedo() const { return !redo_.empty(); }
  void Redo();
  // What a new or newly opened document gets. Called by the overlay's
  // Persistence flows — an undo that reached back past a File > Open would
  // restore waypoints into a document they were never in.
  void ClearHistory();

  // --- the edits themselves ----------------------------------------------
  //
  // Every one of these is callable from a menu item, a key or a gesture, and
  // every one that MUTATES goes through `Record()`.

  void Select(std::string label) { overlay_.SetSelected(std::move(label)); }
  bool Delete(const std::string& label);
  // Inserts AFTER the selected waypoint (at the end when nothing is selected),
  // selects it and returns its new label. Empty only if the position is not
  // usable.
  std::string AddAt(const GeoPoint& position);
  bool MoveTo(const std::string& label, const GeoPoint& position);

  // "r" and "b". The bicycle one exists as its own function because choosing
  // between the rule file's `bicycle` profile and the pre-O5c boolean is a
  // decision, not an argument — see `BicycleOptions`.
  bool FollowRoads();
  bool FollowRoadsByBicycle();
  // The options "b" would use: the rule file's own bicycle profile when it
  // defines one, so a tuned weight applies; the flat boolean otherwise, which
  // is the same ride priced more crudely.
  RoutePlanOptions BicycleOptions() const;

  // "g" — steps the uncalculated legs through the LineKind cycle and returns
  // the one now in force. A computed route is unaffected: road geometry IS the
  // road and there is nothing to interpolate.
  LineKind CycleLegKind();

  // --- placing a point ----------------------------------------------------

  // The position a pixel means. Snapped to an exact coordinate when one is
  // within `snap_tolerance_px()`, un-projected otherwise.
  EditPosition ResolvePixel(PixelPoint p) const;

  // --- dragging, for a shell with its own recognizers ---------------------
  //
  // The desktop enters these through OnMouseDown/Move/Up below; a phone calls
  // them from a pan recognizer. `BeginDrag` takes the mouse capture when the
  // overlay has a manager, so a cursor dragged over another overlay — or off
  // the top waypoint entirely — keeps feeding this one until the button is up.

  bool dragging() const { return drag_.active; }
  const std::string& drag_label() const { return drag_.label; }

  bool BeginDrag(const std::string& label, PixelPoint at);
  bool DragTo(PixelPoint at);
  bool EndDrag(PixelPoint at);
  // Escape mid-drag: the waypoint goes back where it was and the undo entry
  // the first move pushed is SPENT putting it there, so a cancelled drag
  // leaves no trace in the history at all.
  bool CancelDrag();

  // --- the Overlay input SPI, forwarded by RouteOverlay -------------------

  bool OnMouseDown(const MouseEvent& e);
  bool OnMouseMove(const MouseEvent& e);
  bool OnMouseUp(const MouseEvent& e);
  bool OnKeyDown(const KeyEvent& e);

  // --- naming -------------------------------------------------------------

  // A short unique label ("WP4"). Names have to stay unique because selection
  // and deletion are BY LABEL — two waypoints called WP3 would delete as one.
  std::string NewLabel() const;
  // Where a new point goes: after the selected one, else at the end.
  size_t InsertIndex() const;

 private:
  // Snapshot for undo, and mark the document dirty. Every mutation goes
  // through here, which is also the only place `dirty` is set — an overlay
  // that dirties itself in six places forgets in the seventh.
  void Record();

  RouteOverlay& overlay_;

  bool adding_ = false;
  bool has_edit_focus_ = true;
  double pick_tolerance_px_ = 8.0;
  double snap_tolerance_px_ = 8.0;

  std::vector<std::vector<RouteWaypoint>> undo_;
  std::vector<std::vector<RouteWaypoint>> redo_;

  // The drag in flight. `origin` and `was_dirty` are what a cancel restores;
  // `moved` is what separates a click from an edit.
  struct Drag {
    bool active = false;
    std::string label;
    GeoPoint origin;
    PixelPoint press{0, 0};
    bool moved = false;
    bool was_dirty = false;
  };
  Drag drag_;
};

}  // namespace fv
