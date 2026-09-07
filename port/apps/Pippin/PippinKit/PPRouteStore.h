// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPRouteStore.h — the route document's life in the app.
//
// Binds together the three pieces a route is made of: `fv::RouteDoc`,
// `fv::RoutePlanner` and `fv::RouteOverlay`. Deliberately not Objective-C, so
// that everything about a route that can be tested on the mac is in this
// header and everything that cannot is in `PPMap`/`PPRoute` above it. That is
// what lets "create a route, kill the app, relaunch, it is still there" be a
// gtest rather than something somebody remembers to try in the simulator.
//
// The rule that is not obvious, and the reason this class exists:
// `RouteOverlay::FileOpen` drops the plan. That is correct at its own level,
// since the road geometry is not in the document — it is derived from a graph
// and a rule file, either of which can change — but it makes reloading at
// launch two steps. A shell that only calls `FileOpen` gets the waypoints
// joined by straight great circles, which the user reads as the app having
// lost their route. So `LoadAtLaunch` opens and replans.
//
// Persistence: the route is written on every change, so every mutating call
// here ends in a write, ordered with the mutation because they are the same
// call. A shell saving from its own thread would race the next edit and could
// put an older route on disk than the one on screen. `Clear` deletes the file
// rather than writing an empty one, since an empty document reloads
// indistinguishably from no document.
//
// Deliberately absent: a queue, a thread, a lock and a callback. This class is
// as thread-hostile as `PPMap`, which owns it, and belongs to the same queue.
// Planning is synchronous; see `last_plan_ms()` for what that costs and
// `PPMap.h` for where the asynchrony lives.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fv_road_network.h"
#include "fv_route_doc.h"
#include "fv_route_overlay.h"
#include "fv_route_planner.h"
#include "fvkit/geo.h"
#include "fvkit/nav/road_snap.h"

namespace pippin {

// The route as a UI sees it: a plain value, so it crosses a queue boundary by
// being copied, with nothing behind it another thread could be writing.
//
// `calculated` and `waypoints.size() >= 2` are separate questions: a route
// with two waypoints and no roads under it is a real state, meaning the router
// could not connect them, and it is drawn differently.
struct RouteSnapshot {
  // There is a route at all, meaning at least one waypoint. A route being
  // built is worth persisting: it is what the sheet re-opens pre-filled with.
  bool exists = false;

  std::vector<fv::RouteWaypoint> waypoints;
  std::string profile;

  // Road geometry, as against straight legs between the waypoints. False both
  // when nothing has been planned and when the plan came back with nothing
  // drawable.
  bool calculated = false;

  // Every leg followed a road. False when some pair fell back to a straight
  // line — `straight_legs` says how many.
  bool complete = false;
  int straight_legs = 0;

  bool is_bicycle = false;
  double length_m = 0.0;
  double seconds = 0.0;

  // The planner's own sentence, or empty when nothing has been planned. Shown
  // by the sheet rather than drawn on the map; see `RouteStore`'s constructor
  // on why the overlay's status line is off.
  std::string status;
};

// Where a point is, in words a rider can act on.
//
// A lat/lon is the one thing somebody on a bike cannot check, so the pick
// button and the Location rows name the nearest thing the rider could ride on
// instead ("Flyaway Drive", "cycleway"), or say nothing is in range.
//
// A struct rather than a string because the two places that spell a coordinate
// need different sentences from one answer: the button reads "Use Flyaway
// Drive" and the row reads "Flyaway Drive". Returning either would leave the
// other slicing English apart, so the wording stays in SwiftUI and what
// crosses the boundary is the fact.
struct PlaceDescription {
  // Something the selected profile could travel on was found within the
  // radius. False is not a refusal: the point is still taken and the button
  // stays enabled, because a rider aiming at a beach means it.
  bool usable = false;

  // The road's own name ("Flyaway Drive"), or its class prettified for a
  // human ("cycleway", "living street") when it has none. Empty when
  // `usable` is false.
  std::string name;

  // Whether the name came from the road or from its class. Carried as a fact
  // rather than derived later from whether the string happens to be
  // capitalised. No caller distinguishes the two yet.
  bool named = false;

  // How far the point is from that road, and which road it was. The arc id is
  // the hysteresis key below, exposed so a test can say which road was named
  // without matching on English.
  double distance_m = 0.0;
  uint64_t arc = fv::kNoRoadArc;
};

class RouteStore {
 public:
  // None of the three paths has to exist. A missing graph is reported by the
  // first plan that needs it, a missing rule file leaves the builtin weights
  // in force (`fv::RoutePlanner`'s own contract), and a missing document is
  // what a first launch looks like.
  RouteStore(std::string graph_path, std::string rules_path,
             std::string document_path);

  // The overlay, for the caller to put in its stack. Owned here because its
  // planner is owned here and it borrows one.
  const std::shared_ptr<fv::RouteOverlay>& overlay() const { return overlay_; }

  const std::string& document_path() const { return document_path_; }

  // An empty document path means a route that is not persisted, which is
  // supported: `PPMap` builds its store at construction, because the overlay
  // must be in the stack before anything draws, and learns where `Documents/`
  // is afterwards from the one layer allowed to ask Foundation. A test that
  // does not care about disk uses the same state.
  void set_document_path(std::string path) { document_path_ = std::move(path); }

  // Opens the saved document and replans it; see the header on why that is two
  // steps.
  //
  // A missing document is `Ok` with no route, because a first launch is not an
  // error. A document that will not parse is reported, since a file that
  // exists and cannot be read is worth a line on screen, but the app still
  // comes up with no route, which is why the snapshot is always valid
  // afterwards.
  fv::Status LoadAtLaunch();

  // The whole editing surface: wholesale replacement, which is what a sheet
  // produces. Plans, then writes.
  //
  // `profile` names a rule-file profile ("foot", "bicycle"); empty keeps the
  // document's own. Fewer than two waypoints is not an error — it is a
  // half-built route — and comes back as a snapshot whose `status` says so.
  RouteSnapshot SetWaypoints(std::vector<fv::RouteWaypoint> waypoints,
                             const std::string& profile);

  // Replans what is already there, without touching the document. For a rule
  // file or a graph that has changed under a route the user has not edited.
  RouteSnapshot Replan();

  // No route, and no document on disk either.
  RouteSnapshot Clear();

  RouteSnapshot Snapshot() const;

  // The planned line, every leg joined end to end, or empty when nothing has
  // been planned. The trip computer measures distance-to-go along this road
  // geometry and not along the waypoints, which on Kiawah are three points
  // with a 6 km ride between them.
  //
  // The joint between two legs is the same point twice, and the duplicate is
  // dropped here rather than left for every consumer to notice. A repeated
  // vertex is a zero-length segment, which `ProjectOntoSegment` rejects and a
  // length sum ignores, so it is harmless once checked; this way nobody checks
  // twice.
  std::vector<fv::GeoPoint> RoutePath() const;

  // --- Dragging a waypoint -------------------------------------------------
  //
  // The edit itself is `fv::RouteEditSession`, whose `BeginDrag`/`DragTo`/
  // `EndDrag` are the phone entry points RouteKit already built, snapping
  // included. This class adds the two things a session does not know about,
  // because they belong to the document:
  //
  //   * The replan. `RouteEditSession::MoveTo` calls
  //     `RouteOverlay::SetWaypoints`, which drops the plan, correctly: a road
  //     line computed for a waypoint that has since moved joins somewhere the
  //     route is not. So every position produced here must be re-followed, or
  //     the route falls back to straight legs the moment a point is touched.
  //   * The write, or a route the rider dragged into shape is one the next
  //     launch has not got.
  //
  // The replan is per move and it is measured. Kiawah plans in a few
  // milliseconds, so following the roads live under the finger costs about
  // what an overlay pass costs and shows the rider what they will get. A
  // bigger graph would not, and a stuttering drag is worse than one showing
  // straight legs, so `PlanNow` is timed and the first move to overrun
  // `drag_replan_budget_ms()` turns live following off for the rest of that
  // drag. The release replans regardless, so what is committed is always a
  // followed route.
  //
  // The budget is per drag rather than sticky, because the cost is a property
  // of where the route is: a point dragged across a dense town gets straight
  // legs there and live roads again on the next drag in open country.

  // Which waypoint is under a pixel, or `found` false.
  //
  // Answered out of what was drawn, which is `RouteOverlay::HitTestPoint`'s
  // rule and the reason no projection is passed: the overlay kept the one it
  // last drew with, and a hit using a different one could disagree with the
  // screen the finger is on. An overlay that has never drawn answers nothing.
  struct WaypointHit {
    bool found = false;
    std::string label;
    // From the marker's centre, in surface pixels. `tolerance_px` is a margin
    // beyond the drawn ink and the overlay adds the half-width to it, so this
    // can exceed the tolerance it was asked with.
    double distance_px = 0.0;
  };
  WaypointHit WaypointNear(fv::PixelPoint p, double tolerance_px) const;

  // `[pick] snap_tolerance` reaching the editor, in surface pixels: a waypoint
  // dropped over a `.fvpoints` marker takes that marker's surveyed coordinate
  // instead of the un-projection of the pixel a thumb managed to hit. 0
  // switches snapping off, the same spelling the crosshair uses.
  //
  // Pixels rather than points, because that is what the editor reasons in and
  // what `WaypointNear` is asked with; the shell converts once, where it knows
  // the backing scale. There is deliberately no setter for the editor's pick
  // tolerance: that is read by `RouteEditSession::OnMouseDown`, the desktop's
  // way in, and a phone hit-tests through `WaypointNear` with a thumb-sized
  // number of its own.
  void SetDragSnapTolerancePx(double px);

  bool BeginWaypointDrag(const std::string& label, fv::PixelPoint at);
  // No snapshot: a move in flight has not been committed to disk, and the
  // shell only needs to know the picture changed. What it draws comes from the
  // overlay, which the editor has already moved.
  bool DragWaypointTo(fv::PixelPoint at);
  // Replans and writes. The snapshot is the committed route.
  RouteSnapshot EndWaypointDrag(fv::PixelPoint at);
  // Puts it back and replans, without writing: nothing changed on disk.
  RouteSnapshot CancelWaypointDrag();

  bool dragging_waypoint() const;
  std::string dragged_waypoint() const;

  // How long a move's replan may take before the rest of the drag stops
  // following roads. `pippin.ini` carries it (`routing.drag_replan_budget_ms`);
  // 0 means never follow live, which is the desktop editor's own behaviour.
  void set_drag_replan_budget_ms(double ms) { drag_replan_budget_ms_ = ms; }
  double drag_replan_budget_ms() const { return drag_replan_budget_ms_; }
  // False once a move in this drag has overrun the budget. Exposed so a test
  // can pin the give-up rather than infer it from a shape on a canvas.
  bool drag_follows_roads() const { return drag_follows_roads_; }

  // --- The roads under the ship --------------------------------------------

  // The snapper's road network, built once and shared with the router.
  //
  // Here, in a class about a document, because this is where the graph already
  // is. `fv::RoadSnapper` asks which roads are near a point and
  // `fv::routing::RoadGraphNetwork` answers over a `RoadGraph`, the same file
  // the planner routes with. Loading Kiawah's graph twice would pay twice the
  // memory for identical tarmac, and the two copies could disagree once either
  // was rebuilt. So the planner's graph is handed out and indexed a second way
  // — the router indexes junctions, a snap needs the arc geometry — and both
  // halves of the app look at one set of roads.
  //
  // Null is a supported answer and is what a pack with no `.fvroad` gives:
  // `status` says why, and a moving map with no network does not snap. The
  // index is built at the first call and kept, which on Kiawah is
  // milliseconds; `fv_road_network.h`'s note about a continent-sized graph
  // wanting a windowed index applies unchanged.
  //
  // The filter is `kAll`, the opposite of the default. That default admits
  // what a car may drive, so a car does not snap to the cycleway beside the
  // road. Pippin's rider is on the cycleway — Kiawah's parkway has one for its
  // whole length and the app has no car profile — so the driver's filter would
  // put this ship on the wrong ribbon of tarmac every time. Admitting
  // everything costs the reverse case, a rider on the road snapping to a
  // parallel path metres away, and that is what `snap_min_confidence` is for:
  // two roads that close score within `ambiguity_m` and the overlay declines.
  std::shared_ptr<const fv::routing::RoadGraphNetwork> EnsureRoadNetwork(
      fv::Status* status = nullptr);

  // --- Naming a point ------------------------------------------------------

  // Names the nearest thing within `max_m` that a route priced with `profile`
  // could travel on. An empty `profile` means the document's own, which is
  // what the point editor asks with, having no profile picker.
  //
  // Usable means what the router means, and that is the difficulty. The snap
  // index is built at `kAll` — it has to be, or the ship would never snap to
  // the cycleway it is on — so the nearest candidate is often a footpath the
  // bicycle profile refuses, and naming it would promise a road the next
  // replan routes around. So every candidate goes through
  // `fv::routing::ArcUsable` with the options the planner builds for that
  // profile: same rule file, same poll, same access bits. A `private` arc
  // still counts, since on a gated island the private roads are the street
  // network.
  //
  // No graph is not an error: a pack with no `.fvroad` answers `usable =
  // false`, the button says so, and picking still works. That is the same
  // answer as the open sea, correctly.
  //
  // It remembers the last road it named, which is not an optimisation. At a
  // junction, or on Kiawah's parkway where a cycleway runs beside the road,
  // the two nearest candidates are metres apart and which is nearest changes
  // as the map drifts a pixel, so the label would blink while the rider reads
  // it. The road named last is kept while it stays within `stay_bonus_m` of
  // the new nearest, which is `RoadSnapOptions::stay_bonus_m` applied to a
  // label. `ForgetNamedPlace()` clears it, and a pick that begins fresh should
  // call it, or the road named at the end of the last pick gets a head start
  // somewhere else entirely.
  //
  // `remember` false asks without touching that memory, which is what a sheet
  // asks with: a row naming a stop set ten minutes ago is a lookup of a fixed
  // place, and writing the memory would give the crosshair a head start from
  // another coordinate. Only the moving crosshair remembers, because only it
  // can blink.
  PlaceDescription DescribePoint(const fv::GeoPoint& p, double max_m,
                                 const std::string& profile = std::string(),
                                 bool remember = true);

  void ForgetNamedPlace() { described_arc_ = fv::kNoRoadArc; }

  // The margin the remembered road is kept by, in metres. Settable because the
  // right value differs between an island of cul-de-sacs and a city grid;
  // `pippin.ini` carries it as `routing.pick_stay_bonus_m`, along with the
  // radius `DescribePoint` is called with.
  void set_describe_stay_bonus_m(double m) { describe_stay_bonus_m_ = m; }
  double describe_stay_bonus_m() const { return describe_stay_bonus_m_; }

  // The graph the router is using, or null before one has loaded. Exposed so a
  // caller or a test can see the network above is indexing that graph and not
  // a second copy.
  std::shared_ptr<const fv::routing::RoadGraph> road_graph() const {
    return planner_->graph();
  }

  // The rule file's profiles, for a UI that offers them. Empty when the file
  // is missing or bad, in which case the builtin weights are answering routes
  // and `rules_error()` says why.
  std::vector<std::string> ProfileNames() const;
  std::string rules_error() const { return planner_->rules_error(); }

  // What the last plan cost, in milliseconds. Planning happens on the queue
  // that draws, so this is how long a frame was not being drawn, and it is the
  // measurement that decides whether a plan needs its own queue (`PPMap.h`).
  double last_plan_ms() const { return last_plan_ms_; }

  // The error from the last write, or empty. A failed write is not worth
  // refusing an edit over, since the route is on screen and correct, but it is
  // worth saying because the next launch will not have it.
  const std::string& last_write_error() const { return last_write_error_; }

 private:
  // Plans over the current waypoints, timing it. Split out because
  // `SetWaypoints`, `Replan` and `LoadAtLaunch` all do exactly this and
  // differ only in what they do around it.
  void PlanNow();

  // Writes the document, or removes it when there is nothing to write.
  void Persist();

  std::string document_path_;
  std::unique_ptr<fv::RoutePlanner> planner_;
  std::shared_ptr<fv::RouteOverlay> overlay_;

  double last_plan_ms_ = 0.0;
  std::string last_write_error_;

  // Built on demand by `EnsureRoadNetwork` and rebuilt if the graph under it
  // is ever replaced. The network holds its own reference to the graph, so it
  // stays valid even if the planner drops one.
  std::shared_ptr<fv::routing::RoadGraphNetwork> network_;

  // The arc `DescribePoint` named last, and the margin it is kept by.
  // `kNoRoadArc` means nothing named yet, which every first call of a pick
  // sees.
  uint64_t described_arc_ = fv::kNoRoadArc;
  double describe_stay_bonus_m_ = 10.0;

  // The live-follow budget, and whether this drag is still inside it.
  double drag_replan_budget_ms_ = 30.0;
  bool drag_follows_roads_ = true;
  // The waypoint under the finger, and whether the document has actually
  // changed since the press. `moved` is why a grab-and-let-go writes nothing.
  std::string drag_label_;
  bool drag_moved_ = false;
};

}  // namespace pippin
