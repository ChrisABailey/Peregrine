// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_route_planner.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace fv {

bool IsBicycleRequest(const std::string& profile, bool cycle_only) {
  if (profile.empty()) return cycle_only;
  std::string lower = profile;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(::tolower(c)); });
  return lower.find("bike") != std::string::npos ||
         lower.find("cycle") != std::string::npos;
}

RoutePlanner::RoutePlanner(std::string graph_path, std::string rules_path)
    : graph_path_(std::move(graph_path)), rules_path_(std::move(rules_path)) {}

void RoutePlanner::SetGraphPath(std::string path) {
  if (path == graph_path_) return;
  graph_path_ = std::move(path);
  router_.reset();
  graph_.reset();
}

void RoutePlanner::SetRulesPath(std::string path) {
  if (path == rules_path_) return;
  rules_path_ = std::move(path);
  rules_.reset();
}

Status RoutePlanner::EnsureGraph() const {
  if (router_) return Status::Ok();
  if (graph_path_.empty()) {
    return Status::Error(kInvalidArg, "no road graph configured");
  }
  auto graph = std::make_shared<routing::RoadGraph>();
  Status s = routing::RoadGraph::Load(graph_path_, graph.get());
  if (!s.ok()) return s;
  graph_ = std::move(graph);
  router_.reset(new routing::Router(*graph_));
  return Status::Ok();
}

std::string RoutePlanner::rules_error() const {
  if (rules_path_.empty()) return std::string();
  if (!rules_) rules_.reset(new routing::RouteRulesFile(rules_path_));
  rules_->rules();  // poll, so the error reflects the file as it is now
  return rules_->last_error();
}

std::vector<std::string> RoutePlanner::profile_names() const {
  if (rules_path_.empty()) return routing::RouteRules::Builtin()->names();
  if (!rules_) rules_.reset(new routing::RouteRulesFile(rules_path_));
  return rules_->rules()->names();
}

Status RoutePlanner::BuildOptions(const RoutePlanOptions& in,
                                  routing::RouteOptions* out) const {
  out->snap_meters = in.snap_meters;
  out->cycle_only = in.cycle_only;
  out->driving = !in.cycle_only;

  if (!in.profile.empty()) {
    std::shared_ptr<const routing::RouteRules> rules;
    if (rules_path_.empty()) {
      rules = routing::RouteRules::Builtin();
    } else {
      if (!rules_) rules_.reset(new routing::RouteRulesFile(rules_path_));
      // The poll happens HERE, so every plan routes on the weights currently
      // on disk — an edit to the rule file lands on the next route with
      // nothing reopened.
      rules = rules_->rules();
    }
    // SelectProfile mirrors the profile's toll and ferry defaults onto the
    // options, so the query's own avoidances have to be applied AFTER it.
    Status s = routing::SelectProfile(rules, in.profile, out);
    if (!s.ok()) return s;
  }

  // O5e: the penalties are the only profile-backed settings the router reads
  // from the options rather than the profile, which is exactly what lets a
  // journey say "this profile, but no ferries today".
  if (in.avoid_tolls) out->toll_penalty = routing::kAvoidExcluded;
  if (in.avoid_ferries) out->ferry_penalty = routing::kAvoidExcluded;
  return Status::Ok();
}

std::string RoutePlanner::StatusLine(const RoutePlanOptions& in, double meters,
                                     double seconds,
                                     const std::string& note) const {
  // A rule file that failed to load still routes — on the last good or builtin
  // weights — so this is a warning ON an answer, not an error instead of one.
  // Saying nothing would be the bug: the user edited a file and would
  // otherwise see a route that ignored the edit. Clamped, because a JSON parse
  // error names the file, the line and the token and this has to fit on one
  // line of map.
  std::string bad = rules_error();
  if (bad.size() > 64) bad = bad.substr(0, 61) + "...";
  const std::string warn = bad.empty() ? std::string() : " [rules: " + bad + "]";
  const std::string what =
      !in.profile.empty() ? in.profile : (in.cycle_only ? "bike" : "roads");

  char buf[128];
  std::snprintf(buf, sizeof(buf), "%s: %.1f km, %.0f min", what.c_str(),
                meters / 1000.0, seconds / 60.0);
  return std::string(buf) + note + warn;
}

RoutePlan RoutePlanner::PlanPerPair(const std::vector<GeoPoint>& stops,
                                    const routing::RouteOptions& options) const {
  RoutePlan plan;
  for (size_t i = 0; i + 1 < stops.size(); ++i) {
    const GeoPoint& a = stops[i];
    const GeoPoint& b = stops[i + 1];
    routing::Route leg;
    const Status s = router_->Route(a, b, options, &leg);
    if (!s.ok()) {
      // Only "this end is nowhere near a road" is a per-leg problem that
      // leaves the other legs worth routing. Anything else is about the
      // REQUEST and is reported once, instead of being hidden as straight
      // lines the user would read as a routing answer.
      if (s.code != kOutOfCoverage) {
        RoutePlan failed;
        failed.error = s;
        failed.status = s.message;
        return failed;
      }
      plan.legs.push_back({a, b});
      ++plan.straight_legs;
      continue;
    }
    if (!leg.found) {
      plan.legs.push_back({a, b});  // honest about what it could not do
      ++plan.straight_legs;
      continue;
    }
    plan.legs.push_back(leg.geometry);
    plan.length_m += leg.length_m;
    plan.seconds += leg.seconds;
  }
  return plan;
}

RoutePlan RoutePlanner::Plan(const std::vector<GeoPoint>& stops,
                             const RoutePlanOptions& in) const {
  RoutePlan plan;
  plan.is_bicycle = IsBicycleRequest(in.profile, in.cycle_only);

  if (stops.size() < 2) {
    plan.status = "a route needs two waypoints";
    plan.error = Status::Error(kInvalidArg, plan.status);
    return plan;
  }
  Status s = EnsureGraph();
  if (!s.ok()) {
    plan.status = s.message;
    plan.error = s;
    return plan;
  }

  routing::RouteOptions options;
  s = BuildOptions(in, &options);
  if (!s.ok()) {
    // A profile the rule file does not define. Reported once and NOT retried
    // per pair: every pair would fail identically.
    plan.status = s.message;
    plan.error = s;
    return plan;
  }

  routing::Route through;
  bool have_through = false;
  s = router_->RouteVia(stops, options, &through);
  if (!s.ok()) {
    if (s.code != kOutOfCoverage) {
      plan.status = s.message;
      plan.error = s;
      return plan;
    }
    // A stop nowhere near a road is the one failure the per-pair fallback can
    // still say something useful about.
  } else {
    have_through = true;
  }

  if (have_through && through.found) {
    // Cut the one line back into per-leg pieces at the stops: the drawing code
    // wants polylines, and a leg is still the unit a user thinks in even when
    // the route through them is single.
    const std::vector<uint32_t>& cuts = through.stop_geometry_index;
    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
      const uint32_t a = cuts[i];
      const uint32_t b = cuts[i + 1];
      if (b <= a || b >= through.geometry.size()) continue;
      plan.legs.emplace_back(through.geometry.begin() + a,
                             through.geometry.begin() + b + 1);
    }
    plan.found = true;
    plan.length_m = through.length_m;
    plan.seconds = through.seconds;
    plan.u_turn_stops = through.u_turn_stops;
    std::string turned;
    if (!through.u_turn_stops.empty()) {
      turned = " (turned round at " +
               std::to_string(through.u_turn_stops.size()) +
               " stop(s) with no other way out)";
    }
    plan.status = StatusLine(in, plan.length_m, plan.seconds, turned);
    return plan;
  }

  // No through route. Say why, then draw what can be drawn.
  std::string note = " (a waypoint is not near a road)";
  if (have_through && through.unreachable_leg != routing::Route::kNoLeg) {
    note = " (stops " + std::to_string(through.unreachable_leg + 1) + " and " +
           std::to_string(through.unreachable_leg + 2) + " are not connected)";
  }
  RoutePlan pairs = PlanPerPair(stops, options);
  pairs.is_bicycle = plan.is_bicycle;
  if (have_through) pairs.unreachable_leg = through.unreachable_leg;
  if (pairs.legs.empty()) {
    // PlanPerPair only comes back empty on a request-level failure, and it has
    // already put the router's own words on `status`.
    return pairs;
  }
  std::string straight;
  if (pairs.straight_legs > 0) {
    straight = " (" + std::to_string(pairs.straight_legs) +
               " leg(s) not on the network)";
  }
  pairs.status = "pairs" + note + " - " +
                 StatusLine(in, pairs.length_m, pairs.seconds, straight);
  // `found` stays false whatever the pairs managed: the route asked for was a
  // route THROUGH the waypoints, and this is not one.
  return pairs;
}

}  // namespace fv
