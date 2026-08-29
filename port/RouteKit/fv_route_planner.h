// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RouteKit/fv_route_planner.h — waypoints in, drawable legs out (P5).
//
// THIS IS WHY RouteKit EXISTS AS A MODULE. The ledger's invariant, stated
// twice, is that fvkit does not link `port/Routing` — it is what keeps the map
// engine out of `fvgraph` and the router out of every shell that only draws.
// A route overlay needs both, so it lives HERE, beside fvkit and Routing
// rather than under either, exactly as `pippin_core_deps` already links them.
//
// The semantics are route.py's `follow_roads`, moved verbatim rather than
// improved, because the two shells have to agree about what a route IS:
//
//   * ONE route through the ordered waypoints (`RouteVia`, O5d), so a turn
//     restriction at a stop binds and the route does not drive out to a
//     waypoint, turn round and come back unless there is genuinely no other
//     way out;
//   * falling back to routing the consecutive PAIRS independently when the
//     through route cannot be had at all — a waypoint dropped in the sea, two
//     that are not connected. Half a drawn route is more use than none, and it
//     is what this did before O5d; the result SAYS it is the lesser answer;
//   * a pair that will not route at all stays a STRAIGHT leg, which is the
//     honest picture rather than a hole;
//   * `kOutOfCoverage` is the only per-leg failure. Anything else — above all a
//     profile the rule file does not define — is about the REQUEST, would fail
//     identically on every leg, and is reported ONCE instead of hidden as a
//     screenful of straight lines.
//
// The graph is loaded LAZILY and once: a `.fvroad` is a build artifact that
// costs minutes and gigabytes to make and a moment to load, and a route
// overlay that never routes should not pay even the moment.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fv_route_rules.h"
#include "fv_road_graph.h"
#include "fv_router.h"
#include "fvkit/geo.h"

namespace fv {

// What a plan came out as. Every field is meaningful when `found` is false —
// that is the case the status line exists for.
struct RoutePlan {
  // A through route was computed over every waypoint in order. False for the
  // per-pair fallback AND for a total failure; `legs` says which.
  bool found = false;

  // Per-leg polylines, one per consecutive pair of waypoints, in order.
  // Empty only when nothing could be drawn at all. Road geometry is already
  // dense — it IS the road — so these are drawn as SIMPLE lines: there is
  // nothing to interpolate between two points a few metres apart, and asking
  // for a great circle between them would cost the geodesy and return the
  // same line.
  std::vector<std::vector<GeoPoint>> legs;

  double length_m = 0.0;
  double seconds = 0.0;

  // How many legs came back straight because the pair would not route. 0 for a
  // through route by construction.
  int straight_legs = 0;

  // The stops the through route had to turn round at (`Route::u_turn_stops`).
  std::vector<uint32_t> u_turn_stops;

  // Which consecutive pair the through route could not connect, or
  // `routing::Route::kNoLeg`. Reported by the router, not derived here.
  uint32_t unreachable_leg = routing::Route::kNoLeg;

  // Was this priced as a BICYCLE route? The line says so (route.py draws a
  // cycle route dashed and everything else solid), and the answer is matched
  // on the profile NAME rather than on a flag out of the router, because the
  // rule file owns the profiles and a user may well call theirs
  // "bicycle-winter". §2c's standing note applies: deriving the style from the
  // name is right for the two modes the apps have keys for and silent about a
  // third.
  bool is_bicycle = false;

  // The one line of map a route gets to explain itself on. Same wording as
  // route.py's `_road_status_line`, so a user moving between the two shells
  // reads the same sentence.
  std::string status;

  // Set when the plan failed outright — no legs, nothing to draw. `status`
  // carries the same words; this is for a caller that wants the code.
  Status error;
};

// How a plan is asked for. Everything here has a default that routes.
struct RoutePlanOptions {
  // Metres a stop may be from the nearest road before it is out of coverage.
  // route.py's own default, and much wider than the router's 500: a waypoint
  // dropped by hand on a chart is not on a road.
  double snap_meters = 2000.0;

  // The rule-file profile to price with. Empty = the router's default, and
  // then `cycle_only` decides. A profile OVERRIDES cycle_only, in the router
  // and in the bicycle test above.
  std::string profile;

  // The pre-O5c boolean: only classes a bike may ride, at a flat speed, with
  // cycleways and quiet streets preferred.
  bool cycle_only = false;

  // O5e, the two the apps have no UI for yet. Left at the profile's own
  // defaults, which is what `SelectProfile` mirrors onto the options.
  bool avoid_tolls = false;
  bool avoid_ferries = false;
};

// Is `profile` (or `cycle_only` when it is empty) a bicycle request? Public
// because the OVERLAY needs the same answer to style a line it did not plan —
// a route reloaded from disk, drawn before anyone has pressed route.
bool IsBicycleRequest(const std::string& profile, bool cycle_only);

class RoutePlanner {
 public:
  // Neither path has to exist yet. A missing graph is reported by the first
  // `Plan` that needs it (and only then); a missing or invalid rule file is
  // not an error at all — `RouteRulesFile` keeps the builtin weights in force
  // and says why on `rules_error()`, so a bad edit warns on an answer rather
  // than replacing one.
  RoutePlanner(std::string graph_path, std::string rules_path = std::string());

  const std::string& graph_path() const { return graph_path_; }
  const std::string& rules_path() const { return rules_path_; }

  // Replacing either path drops whatever was loaded. Setting the same path
  // again is a no-op, so a shell may call these per frame.
  void SetGraphPath(std::string path);
  void SetRulesPath(std::string path);

  // Loads the graph if it is not loaded. Called by `Plan`; exposed so a shell
  // can pay the cost at a moment of its choosing (a splash screen) rather than
  // on the first press of a button.
  Status EnsureGraph() const;

  bool graph_loaded() const { return graph_ != nullptr; }

  // The loaded graph, or null when nothing has loaded one yet (`EnsureGraph`
  // is what fills it). Handed out as a SHARED pointer, and P7 is why: the
  // moving map's `RoadSnapper` wants the same roads the router has
  // (`fv::routing::RoadGraphNetwork` takes a `shared_ptr<const RoadGraph>`),
  // and a phone loading Kiawah's graph a second time to answer "which road am
  // I on" would pay twice the memory for the identical tarmac. Sharing it is
  // also the only way the two can never disagree about what the roads ARE.
  //
  // It is const because a borrower may index it and must not edit it; the
  // planner still owns its lifetime in every sense that matters, since
  // `SetGraphPath` drops this pointer and any index built over it keeps the
  // old graph alive until its own owner lets go.
  std::shared_ptr<const routing::RoadGraph> graph() const { return graph_; }

  // Empty while the rules in force came from the file. Otherwise why they did
  // not, with the builtin or last-good weights still answering routes.
  std::string rules_error() const;

  // The profiles the rule file defines, for a UI that offers them.
  std::vector<std::string> profile_names() const;

  // Fewer than two waypoints is not an error to shout about — it is what a
  // half-built route looks like — so it comes back as a failed plan with the
  // status line saying so, exactly as route.py does.
  RoutePlan Plan(const std::vector<GeoPoint>& stops,
                 const RoutePlanOptions& options = RoutePlanOptions{}) const;

  // Fills `options` from the plan options plus the rules currently on disk.
  // The rule file is polled by THIS call, so every plan routes on the weights
  // that are there NOW, with nothing reopened and nothing restarted.
  //
  // PUBLIC SINCE P11, and for one reason: something that is not planning needs
  // to ask what THIS plan would consider usable. Pippin's pick button names
  // the nearest road a rider could actually ride on, and "could" has to mean
  // what the next replan means or the button promises a road the router then
  // refuses. Getting the options from the planner rather than rebuilding them
  // is what makes the two answers the same answer — the same rule file, the
  // same profile lookup, the same poll.
  Status BuildOptions(const RoutePlanOptions& in, routing::RouteOptions* out) const;

 private:

  RoutePlan PlanPerPair(const std::vector<GeoPoint>& stops,
                        const routing::RouteOptions& options) const;

  std::string StatusLine(const RoutePlanOptions& in, double meters,
                         double seconds, const std::string& note) const;

  std::string graph_path_;
  std::string rules_path_;

  // Mutable because `Plan` is const: loading the graph is a cache fill, not a
  // change of what this planner IS, and a shell holding a `const RoutePlanner&`
  // should still be able to route.
  mutable std::shared_ptr<routing::RoadGraph> graph_;
  mutable std::unique_ptr<routing::Router> router_;
  mutable std::unique_ptr<routing::RouteRulesFile> rules_;
};

}  // namespace fv
