// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_route_planner.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace fv {

bool IsBicycleRequest(const std::string& profile, bool cycle_only) {
  if (profile.empty()) return cycle_only;
  std::string lower = profile;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(::tolower(c)); });
  return lower.find("bike") != std::string::npos ||
         lower.find("cycle") != std::string::npos;
}

std::string RoadDropped(const RoutePlan& before, const RoutePlan& after, double min_drop_m) {
  if (before.roads.empty() || after.roads.empty()) return std::string();
  std::map<std::string, double> now;
  for (const routing::RouteLeg& leg : after.roads) now[leg.name] += leg.length_m;
  std::vector<std::pair<std::string, double>> was;  // travel order, for the tie rule
  for (const routing::RouteLeg& leg : before.roads) {
    if (leg.name.empty()) continue;
    auto it = std::find_if(was.begin(), was.end(),
                           [&](const auto& w) { return w.first == leg.name; });
    if (it == was.end()) was.emplace_back(leg.name, leg.length_m);
    else it->second += leg.length_m;
  }
  std::string best;
  double best_drop = 0.0;
  for (const auto& [name, meters] : was) {
    const auto it = now.find(name);
    const double kept = it == now.end() ? 0.0 : it->second;
    const double drop = meters - kept;
    if (drop < min_drop_m || kept > 0.5 * meters || drop <= best_drop) continue;
    best = name;
    best_drop = drop;
  }
  return best;
}

routing::BeachUse ToBeachUse(RouteBeach v) {
  switch (v) {
    case RouteBeach::kToSaveTime: return routing::BeachUse::kToSaveTime;
    case RouteBeach::kWheneverPossible: return routing::BeachUse::kWheneverPossible;
    case RouteBeach::kNever: break;
  }
  return routing::BeachUse::kNever;
}

namespace {

/// The kBeach runs of `route`, merged across leg boundaries: the router splits
/// legs on name as well as class, and two differently named beaches that meet
/// are one stretch of sand.
std::vector<RouteBeachStretch> BeachStretchesOf(const routing::Route& route) {
  static const std::string kBeachClass = routing::RoadClassName(routing::RoadClass::kBeach);
  std::vector<RouteBeachStretch> out;
  const uint32_t last = route.geometry.empty()
                            ? 0u
                            : static_cast<uint32_t>(route.geometry.size()) - 1u;
  double at_m = 0.0, at_s = 0.0;
  bool open = false;
  for (size_t i = 0; i < route.legs.size(); ++i) {
    const routing::RouteLeg& leg = route.legs[i];
    const uint32_t end = i + 1 < route.legs.size() ? route.legs[i + 1].geometry_begin : last;
    if (leg.klass == kBeachClass) {
      if (!open) {
        RouteBeachStretch b;
        b.geometry_begin = leg.geometry_begin;
        b.start_m = at_m;
        b.enter_s = at_s;
        out.push_back(b);
        open = true;
      }
      RouteBeachStretch& b = out.back();
      b.geometry_end = end;
      b.length_m += leg.length_m;
      b.exit_s = at_s + leg.seconds;
    } else {
      open = false;
    }
    at_m += leg.length_m;
    at_s += leg.seconds;
  }
  return out;
}

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
// The radius nav::Guidance measures along-route metres in.
constexpr double kEarthRadiusM = 6371008.8;

/// Great-circle metres, the formula nav::Guidance uses for `along_m`.
double GreatCircleMeters(const GeoPoint& a, const GeoPoint& b) {
  const double phi1 = a.lat * kDegToRad;
  const double phi2 = b.lat * kDegToRad;
  const double s1 = std::sin((b.lat - a.lat) * kDegToRad * 0.5);
  const double s2 = std::sin(NormalizeLon(b.lon - a.lon) * kDegToRad * 0.5);
  double h = s1 * s1 + std::cos(phi1) * std::cos(phi2) * s2 * s2;
  if (h > 1.0) h = 1.0;
  return 2.0 * kEarthRadiusM * std::asin(std::sqrt(h));
}

double LineMeters(const std::vector<GeoPoint>& line) {
  double m = 0.0;
  for (size_t i = 1; i < line.size(); ++i) m += GreatCircleMeters(line[i - 1], line[i]);
  return m;
}

/// Metres from `p` to segment `a`-`b`, on a local flat projection about `p`.
double SegmentMeters(const GeoPoint& p, const GeoPoint& a, const GeoPoint& b) {
  const double kx = kEarthRadiusM * kDegToRad * std::cos(p.lat * kDegToRad);
  const double ky = kEarthRadiusM * kDegToRad;
  const double ax = NormalizeLon(a.lon - p.lon) * kx, ay = (a.lat - p.lat) * ky;
  const double bx = NormalizeLon(b.lon - p.lon) * kx, by = (b.lat - p.lat) * ky;
  const double dx = bx - ax, dy = by - ay;
  const double len2 = dx * dx + dy * dy;
  double t = len2 > 0.0 ? -(ax * dx + ay * dy) / len2 : 0.0;
  t = std::max(0.0, std::min(1.0, t));
  return std::hypot(ax + t * dx, ay + t * dy);
}

/// Metres along `path` (legs joined) to its first vertex within `within_m`
/// of `line`; the whole length when it never comes that close.
double MetersUntilNear(const std::vector<std::vector<GeoPoint>>& path,
                       const std::vector<std::vector<GeoPoint>>& line, double within_m) {
  double at = 0.0;
  const GeoPoint* prev = nullptr;
  for (const std::vector<GeoPoint>& leg : path) {
    for (const GeoPoint& p : leg) {
      if (prev) at += GreatCircleMeters(*prev, p);
      prev = &p;
      for (const std::vector<GeoPoint>& l : line)
        for (size_t i = 1; i < l.size(); ++i)
          if (SegmentMeters(p, l[i - 1], l[i]) <= within_m) return at;
    }
  }
  return at;
}

}  // namespace

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

std::shared_ptr<const routing::RouteRules> RoutePlanner::rules() const {
  if (rules_path_.empty()) return routing::RouteRules::Builtin();
  if (!rules_) rules_.reset(new routing::RouteRulesFile(rules_path_));
  return rules_->rules();
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
    if (!in.overrides.empty()) {
      auto adjusted = std::make_shared<routing::RouteProfile>();
      s = routing::ApplyOverrides(*out->profile, in.overrides, adjusted.get());
      if (!s.ok()) return s;
      routing::ApplyProfile(std::move(adjusted), out);
    }
  } else if (!in.overrides.empty()) {
    return Status::Error(kInvalidArg, "profile overrides need a profile");
  }

  // O5e: the penalties are the only profile-backed settings the router reads
  // from the options rather than the profile, which is exactly what lets a
  // journey say "this profile, but no ferries today".
  if (in.avoid_tolls) out->toll_penalty = routing::kAvoidExcluded;
  if (in.avoid_ferries) out->ferry_penalty = routing::kAvoidExcluded;
  out->beach = in.beach;
  out->hill_penalty = in.avoid_hills.value_or(false) ? in.avoid_hills_penalty : 1.0;
  out->start_heading_deg = in.start_heading_deg;
  out->start_u_turn_seconds = in.start_u_turn_seconds;
  return Status::Ok();
}

DetourPlan RoutePlanner::PlanDetour(const DetourRequest& request,
                                    const std::vector<GeoPoint>& waypoints,
                                    const RoutePlan& route,
                                    const RoutePlanOptions& options) const {
  DetourPlan out;
  if (waypoints.size() < 2 || route.legs.empty()) return out;

  // Stop i (i >= 1) ends leg i - 1 of the original line.
  const size_t stop_count = std::min(waypoints.size(), route.legs.size() + 1);
  double stop_along = 0.0;
  for (size_t i = 1; i + 1 < stop_count; ++i) {
    stop_along += LineMeters(route.legs[i - 1]);
    if (stop_along > request.last_along_m) out.stops_ahead.push_back(waypoints[i]);
  }
  out.stops_ahead.push_back(waypoints.back());

  double total_m = 0.0;
  for (const std::vector<GeoPoint>& leg : route.legs) total_m += LineMeters(leg);
  out.remaining_m = total_m - std::max(0.0, std::min(request.last_along_m, total_m));

  std::vector<GeoPoint> stops{request.position};
  stops.insert(stops.end(), out.stops_ahead.begin(), out.stops_ahead.end());

  RoutePlanOptions ahead = options;
  ahead.start_heading_deg = request.heading_deg;
  ahead.start_u_turn_seconds = request.u_turn_seconds;
  out.reroute = Plan(stops, ahead);
  out.reroute_starts_with_u_turn = out.reroute.found && out.reroute.start_u_turn;
  if (out.reroute.found) out.extra_m = out.reroute.length_m - out.remaining_m;

  // The best plan that sets off back the way the rider came. Without a
  // heading there is no "back", and it is the reroute again.
  if (std::isfinite(request.heading_deg)) {
    RoutePlanOptions back = ahead;
    back.start_heading_deg = std::fmod(request.heading_deg + 180.0, 360.0);
    back.start_u_turn_seconds = request.turn_around_force_seconds;
    RoutePlan turn = Plan(stops, back);
    // A plan that still goes on (a one-way, a dead end ahead of nothing) is
    // not a way to turn round.
    if (turn.found && !turn.start_u_turn) {
      out.back_m = MetersUntilNear(turn.legs, route.legs, request.rejoin_m);
      out.turn_around = std::move(turn);
    }
  }

  const bool can_turn = out.turn_around.found;
  if (!out.reroute.found) {
    out.pick = can_turn ? DetourPick::kTurnAround : DetourPick::kNone;
  } else if (can_turn && (out.reroute_starts_with_u_turn ||
                          out.turn_around.length_m + request.turn_around_margin_m <
                              out.reroute.length_m)) {
    out.pick = DetourPick::kTurnAround;
  } else {
    out.pick = DetourPick::kReroute;
  }
  return out;
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
  RoutePlan plan = PlanOnce(stops, in);
  if (!in.tide.enabled || plan.beach.empty()) return plan;

  std::vector<nav::BeachStretchTiming> timings;
  for (const RouteBeachStretch& b : plan.beach) timings.push_back({b.enter_s, b.exit_s});
  const nav::BeachTideLimits& limits =
      plan.is_foot && in.tide.foot_limits ? *in.tide.foot_limits : in.tide.limits;
  plan.beach_verdicts = nav::BeachTideVerdicts(timings, in.tide.table, in.tide.depart_s, limits);

  for (const nav::BeachStretchVerdict& v : plan.beach_verdicts) {
    const bool unknown = v.verdict == nav::BeachVerdict::kUnknown;
    if (v.verdict != nav::BeachVerdict::kPoor && !(unknown && !in.tide.keep_unknown))
      continue;
    RoutePlanOptions without = in;
    without.beach = routing::BeachUse::kNever;
    without.tide.enabled = false;
    RoutePlan dry = PlanOnce(stops, without);
    if (v.verdict == nav::BeachVerdict::kUnknown)
      dry.beach_dropped = BeachDropped::kNoTable;
    else if (v.enter_height_m > limits.rideable_below_m)
      dry.beach_dropped = BeachDropped::kHigh;
    else
      dry.beach_dropped = BeachDropped::kRising;
    dry.beach_dropped_verdict = v;
    return dry;
  }
  return plan;
}

RoutePlan RoutePlanner::PlanOnce(const std::vector<GeoPoint>& stops,
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
    // A profile the rule file does not define, or a bad override. Reported
    // once and NOT retried per pair: every pair would fail identically.
    plan.status = s.message;
    plan.error = s;
    return plan;
  }
  plan.is_foot = options.profile && options.profile->mode == routing::TravelMode::kFoot;

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
    plan.start_u_turn = through.start_u_turn;
    plan.has_elevation = through.has_elevation;
    plan.climb_m = through.climb_m;
    plan.descent_m = through.descent_m;
    plan.elevation = through.profile;
    plan.tiers = through.tiers;
    // The turn list is measured along `through.geometry`, and `legs` joined
    // end to end is that same line: the cuts are indices into it, and the
    // duplicate vertices a joiner drops are zero-length segments that no
    // cumulative distance counted in the first place.
    plan.maneuvers = ManeuversOf(through, *graph_, options);
    plan.roads = through.legs;
    plan.beach = BeachStretchesOf(through);
    std::string turned;
    if (through.has_elevation) {
      char hills[96];
      std::snprintf(hills, sizeof(hills), ", up %.0f m, down %.0f m%s", through.climb_m,
                    through.descent_m, in.avoid_hills.value_or(false) ? " (avoiding hills)" : "");
      turned = hills;
    }
    if (!through.u_turn_stops.empty()) {
      turned += " (turned round at " +
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
  pairs.is_foot = plan.is_foot;
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
