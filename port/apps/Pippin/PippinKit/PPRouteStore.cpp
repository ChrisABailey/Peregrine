// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "PPRouteStore.h"

#include <chrono>
#include <filesystem>
#include <utility>

// The editor itself. `fv_route_overlay.h` only forward-declares the
// session it owns, so the one call site that drives a drag is also the one
// place that needs the whole class.
#include "fv_route_edit.h"

namespace pippin {
namespace {

using Clock = std::chrono::steady_clock;

}  // namespace

RouteStore::RouteStore(std::string graph_path, std::string rules_path,
                       std::string document_path)
    : document_path_(std::move(document_path)),
      planner_(new fv::RoutePlanner(std::move(graph_path),
                                    std::move(rules_path))),
      overlay_(std::make_shared<fv::RouteOverlay>("Route")) {
  overlay_->SetPlanner(planner_.get());

  // The overlay's status line is off. `RouteOverlay::OnDraw` writes the
  // planner's sentence on the canvas at (10, 20), which is route.py's spot in
  // a desktop window and is underneath the Dynamic Island on a phone. The
  // words are worth keeping, so `RouteSnapshot::status` carries them to
  // SwiftUI to be laid out in the safe area. Drawing them into the map bitmap
  // would also put them in the preview transform, sliding about with the
  // chart under a finger.
  overlay_->SetShowStatus(false);
}

void RouteStore::PlanNow() {
  const Clock::time_point t0 = Clock::now();
  // The document's own profile is the default, so there is nothing to pass
  // here: a route saved as a cycle route replans as one.
  overlay_->FollowRoads();
  last_plan_ms_ =
      std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void RouteStore::Persist() {
  last_write_error_.clear();
  if (document_path_.empty()) return;

  std::error_code ec;
  if (overlay_->waypoints().empty()) {
    // Nothing to save means nothing on disk. An empty document reloads as an
    // empty route, which is the same state as no document at all, and of two
    // spellings of one state a phone should hold the one that is not a file.
    std::filesystem::remove(document_path_, ec);
    // A failed remove is reported, exactly as a failed write is: the same
    // defect with the opposite sign, where the user cleared their route, the
    // screen agrees, and the next launch reads the file back. `remove`
    // returning false with no error is a file that was not there, which is
    // success, because the state asked for is the state on disk.
    if (ec) last_write_error_ = "could not delete " + document_path_ + ": " +
                                ec.message();
    return;
  }

  const fv::Status s = overlay_->FileSaveAs(document_path_, 0);
  if (!s.ok()) last_write_error_ = s.message;
}

fv::Status RouteStore::LoadAtLaunch() {
  if (document_path_.empty()) return fv::Status::Ok();

  std::error_code ec;
  if (!std::filesystem::exists(document_path_, ec)) {
    // A first launch. Not an error, and deliberately not reported as one:
    // there is no user action this could be about.
    return fv::Status::Ok();
  }

  const fv::Status s = overlay_->FileOpen(document_path_);
  if (!s.ok()) {
    // A file that exists and will not read is worth saying out loud — but the
    // overlay is untouched by a failed `Read` (RouteDoc's own contract), so
    // the app still comes up, with no route on it.
    return s;
  }

  // AND REPLAN, which is the half a caller would forget. `FileOpen` drops the
  // plan because the road geometry is not in the document; without this the
  // user who computed a route yesterday relaunches to straight red legs and
  // reads it as the app having lost it.
  PlanNow();
  return fv::Status::Ok();
}

RouteSnapshot RouteStore::SetWaypoints(std::vector<fv::RouteWaypoint> waypoints,
                                       const std::string& profile) {
  // The profile FIRST: `SetWaypoints` drops the plan, and planning has to see
  // the profile this route is now priced with.
  if (!profile.empty() && profile != overlay_->profile()) {
    overlay_->SetProfile(profile);
  }
  overlay_->SetWaypoints(std::move(waypoints));
  PlanNow();
  Persist();
  return Snapshot();
}

RouteSnapshot RouteStore::Replan() {
  PlanNow();
  return Snapshot();
}

RouteSnapshot RouteStore::Clear() {
  overlay_->FileNew();
  Persist();  // with no waypoints left, this is the remove
  last_plan_ms_ = 0.0;
  return Snapshot();
}

RouteSnapshot RouteStore::Snapshot() const {
  RouteSnapshot out;
  out.waypoints = overlay_->waypoints();
  out.exists = !out.waypoints.empty();
  out.profile = overlay_->profile();

  const fv::RoutePlan& plan = overlay_->plan();
  out.calculated = overlay_->has_plan();
  out.complete = plan.found;
  out.straight_legs = plan.straight_legs;
  out.is_bicycle = plan.is_bicycle;
  out.length_m = plan.length_m;
  out.seconds = plan.seconds;
  out.status = plan.status;
  return out;
}

std::vector<fv::GeoPoint> RouteStore::RoutePath() const {
  std::vector<fv::GeoPoint> out;
  if (!overlay_->has_plan()) return out;

  std::size_t total = 0;
  for (const std::vector<fv::GeoPoint>& leg : overlay_->plan().legs) {
    total += leg.size();
  }
  out.reserve(total);

  for (const std::vector<fv::GeoPoint>& leg : overlay_->plan().legs) {
    for (const fv::GeoPoint& p : leg) {
      // The joint between two legs, and any repeated vertex inside one.
      // Compared exactly rather than within a tolerance: these are the same
      // double, copied — the last point of one leg IS the first point of the
      // next — and a tolerance here would start deleting real road geometry
      // where a route doubles back on a cul-de-sac.
      if (!out.empty() && out.back().lat == p.lat && out.back().lon == p.lon) {
        continue;
      }
      out.push_back(p);
    }
  }
  return out;
}

std::vector<std::string> RouteStore::ProfileNames() const {
  return planner_->profile_names();
}

std::shared_ptr<const fv::routing::RoadGraphNetwork> RouteStore::EnsureRoadNetwork(
    fv::Status* status) {
  // The graph is the planner's, loaded by whichever of the two asks first: a
  // rider who routes before pressing GPS has already paid for it, and a rider
  // who does not pays here. Either way there is one graph.
  const fv::Status s = planner_->EnsureGraph();
  if (status != nullptr) *status = s;
  if (!s.ok()) {
    network_.reset();
    return nullptr;
  }
  const std::shared_ptr<const fv::routing::RoadGraph> graph = planner_->graph();
  if (graph == nullptr) {
    if (status != nullptr) {
      *status = fv::Status::Error(fv::kInvalidArg, "no road graph loaded");
    }
    network_.reset();
    return nullptr;
  }
  // Rebuilt only when the graph underneath has actually been replaced — a
  // `SetGraphPath` on the planner. Comparing the pointers is what makes this
  // cheap enough to call on every entry into GPS mode.
  if (network_ != nullptr && network_->graph() == graph) return network_;

  fv::routing::RoadNetworkOptions options;
  options.filter = fv::routing::RoadSnapFilter::kAll;  // see the header
  network_ = std::make_shared<fv::routing::RoadGraphNetwork>(graph, options);
  return network_;
}

// ---------------------------------------------------------------------------
// P11 — where a point is, in words
// ---------------------------------------------------------------------------

namespace {

// `RoadClassName` hands back the OSM tag value, which is how the class is
// spelled in a file: underscored. `living_street` on a button is a tag leaking
// into a sentence, so the underscores become spaces and nothing else changes —
// no capitalisation, because "Cycleway" would read as a road called Cycleway
// beside the real names this sits among.
std::string PrettyClassName(fv::routing::RoadClass klass) {
  const char* raw = fv::routing::RoadClassName(klass);
  if (raw == nullptr) return std::string();
  std::string out(raw);
  for (char& c : out) {
    if (c == '_') c = ' ';
  }
  return out;
}

}  // namespace

PlaceDescription RouteStore::DescribePoint(const fv::GeoPoint& p, double max_m,
                                           const std::string& profile,
                                           bool remember) {
  PlaceDescription out;

  // A pack with no graph, or one that will not load. Not an error here: the
  // caller is labelling a button, and "No usable path" is the truthful label
  // for a phone that has no roads to offer.
  const std::shared_ptr<const fv::routing::RoadGraphNetwork> network =
      EnsureRoadNetwork();
  if (network == nullptr) return out;
  const std::shared_ptr<const fv::routing::RoadGraph> graph = network->graph();
  if (graph == nullptr) return out;

  // The options the NEXT REPLAN would use, built by the planner rather than
  // assembled here — see the header. An empty profile is the document's own,
  // which is what the point editor asks with.
  fv::RoutePlanOptions request;
  request.profile = profile.empty() ? overlay_->profile() : profile;
  fv::routing::RouteOptions options;
  if (!planner_->BuildOptions(request, &options).ok()) {
    // A profile the rule file does not define. The router reports this on the
    // route itself (once, per P5), and a label is not the place to say it a
    // second time — but naming a road the rules cannot price would be a
    // guess, so this answers "nothing usable" instead.
    return out;
  }

  std::vector<fv::RoadCandidate> candidates;
  network->QueryNear(p, max_m, &candidates);

  const fv::RoadCandidate* best = nullptr;
  const fv::RoadCandidate* remembered = nullptr;
  for (const fv::RoadCandidate& c : candidates) {
    if (c.arc > 0xFFFFFFFFull) continue;  // not an index this graph could hold
    if (!fv::routing::ArcUsable(graph->arc(static_cast<uint32_t>(c.arc)),
                                options)) {
      continue;
    }
    if (best == nullptr || c.distance_m < best->distance_m) best = &c;
    if (remember && c.arc == described_arc_) remembered = &c;
  }
  if (best == nullptr) {
    // Nothing in range clears the memory: a rider who drags out over the
    // water and back onto a different street should get that street, not the
    // one they left. The margin is for a wobble, not a journey.
    if (remember) described_arc_ = fv::kNoRoadArc;
    return out;
  }

  // The road named last wins ties and near-ties, so the button does not blink
  // while it is being read.
  const fv::RoadCandidate& chosen =
      (remembered != nullptr &&
       remembered->distance_m <= best->distance_m + describe_stay_bonus_m_)
          ? *remembered
          : *best;

  out.usable = true;
  out.arc = chosen.arc;
  out.distance_m = chosen.distance_m;
  if (!chosen.name.empty()) {
    out.name = chosen.name;
    out.named = true;
  } else {
    out.name = PrettyClassName(
        graph->arc(static_cast<uint32_t>(chosen.arc)).klass);
    // A usable arc of a class with no name at all is not a case this graph
    // has — every routable class has a tag value — but an empty label on a
    // button is worse than the numbers were, so it falls all the way back.
    if (out.name.empty()) out.usable = false;
  }
  if (remember) described_arc_ = out.usable ? out.arc : fv::kNoRoadArc;
  return out;
}


// ---------------------------------------------------------------------------
// Dragging a waypoint
// ---------------------------------------------------------------------------

RouteStore::WaypointHit RouteStore::WaypointNear(fv::PixelPoint p,
                                                 double tolerance_px) const {
  WaypointHit out;
  if (!overlay_->has_projection()) return out;

  std::vector<fv::app::HitItem> hits;
  overlay_->HitTestPoint(overlay_->last_projection(), p,
                         tolerance_px > 0.0 ? tolerance_px : 0.0, hits);
  if (hits.empty()) return out;

  // Nearest wins with no ambiguity question, the same rule the point set's
  // tap follows. Two waypoints close enough to sit under one thumb are a
  // route the rider is about to zoom into, and a dialog asking which they
  // meant is a dialog in the middle of a gesture.
  const fv::app::HitItem* best = &hits.front();
  for (const fv::app::HitItem& h : hits) {
    if (h.distance_px < best->distance_px) best = &h;
  }
  out.found = true;
  // The overlay puts the waypoint's own label in the tool tip; the feature id
  // beside it is a number minted for search, and a label is what every edit in
  // `RouteEditSession` is addressed by.
  out.label = best->hint.tool_tip;
  out.distance_px = best->distance_px;
  return out;
}

void RouteStore::SetDragSnapTolerancePx(double px) {
  overlay_->edit().SetSnapTolerancePx(px);
}

namespace {

// Where a labelled waypoint is right now, or `false` if it is not there.
bool PositionOf(const fv::RouteOverlay& ov, const std::string& label,
                fv::GeoPoint* out) {
  for (const fv::RouteWaypoint& w : ov.waypoints()) {
    if (w.label != label) continue;
    *out = w.position;
    return true;
  }
  return false;
}

}  // namespace

bool RouteStore::BeginWaypointDrag(const std::string& label,
                                   fv::PixelPoint at) {
  if (!overlay_->edit().BeginDrag(label, at)) return false;
  // The halo is the only acknowledgement this gesture can give: a long press
  // that grabs something and looks identical to one that missed is a gesture
  // nobody trusts. G4 already draws a selected waypoint with a halo, so
  // selecting it is the acknowledgement at no cost in new drawing code. It is
  // dropped again on release.
  overlay_->SetSelected(label);
  drag_follows_roads_ = drag_replan_budget_ms_ > 0.0;
  drag_label_ = label;
  drag_moved_ = false;
  return true;
}

bool RouteStore::DragWaypointTo(fv::PixelPoint at) {
  if (!overlay_->edit().dragging()) return false;

  // Whether anything moved is asked of the document, not the gesture.
  // `RouteEditSession` has a click/drag threshold of its own, and a `DragTo`
  // off the projection holds position deliberately, so both arrive here as
  // the waypoint being where it was, which is what a replan cares about.
  fv::GeoPoint before{};
  const bool had = PositionOf(*overlay_, drag_label_, &before);
  if (!overlay_->edit().DragTo(at)) return false;
  fv::GeoPoint now{};
  if (!had || !PositionOf(*overlay_, drag_label_, &now)) return true;
  if (now.lat == before.lat && now.lon == before.lon) return true;
  drag_moved_ = true;

  if (!drag_follows_roads_) return true;
  PlanNow();
  if (last_plan_ms_ > drag_replan_budget_ms_) {
    // One overrun ends live following for this drag, not retried on the next
    // move: a plan too slow once will be too slow three pixels away, and a
    // drag alternating between road geometry and straight legs is worse to
    // watch than one that stays straight.
    drag_follows_roads_ = false;
  }
  return true;
}

RouteSnapshot RouteStore::EndWaypointDrag(fv::PixelPoint at) {
  if (!overlay_->edit().dragging()) return Snapshot();

  // The release position is committed by `EndDrag` itself, which calls back
  // into `DragTo`, so this goes through this class's own move and the
  // did-it-move bookkeeping catches a release that landed where the press
  // did.
  fv::GeoPoint before{};
  const bool had = PositionOf(*overlay_, drag_label_, &before);
  overlay_->edit().EndDrag(at);
  fv::GeoPoint now{};
  if (had && PositionOf(*overlay_, drag_label_, &now) &&
      (now.lat != before.lat || now.lon != before.lon)) {
    drag_moved_ = true;
  }
  overlay_->SetSelected(std::string());

  // A press that grabbed and let go is not an edit. Nothing moved, so nothing
  // is replanned and nothing is written: a document rewritten on every touch
  // has an mtime that lies about when the route last changed.
  if (!drag_moved_) return Snapshot();

  // Otherwise, always, whatever the drag showed. This is the frame that
  // decides what the rider is left looking at and what goes on disk, and it is
  // the one plan in the whole gesture that is not allowed to be skipped for
  // being slow.
  PlanNow();
  Persist();
  return Snapshot();
}

RouteSnapshot RouteStore::CancelWaypointDrag() {
  if (!overlay_->edit().dragging()) return Snapshot();
  overlay_->edit().CancelDrag();
  overlay_->SetSelected(std::string());
  if (!drag_moved_) return Snapshot();
  drag_moved_ = false;

  // The waypoints are back where they started, but the PLAN is not: every
  // `MoveTo` on the way out dropped it, and a cancel that left the route drawn
  // as straight legs would look exactly like a cancel that failed. Nothing is
  // written — the document on disk was never touched.
  PlanNow();
  return Snapshot();
}

bool RouteStore::dragging_waypoint() const {
  return overlay_->edit().dragging();
}

std::string RouteStore::dragged_waypoint() const {
  return overlay_->edit().drag_label();
}

}  // namespace pippin
