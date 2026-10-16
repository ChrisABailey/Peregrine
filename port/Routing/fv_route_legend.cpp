// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_route_legend.h"

#include <cmath>
#include <cstdio>
#include <memory>

#include "fv_road_graph.h"
#include "fv_router.h"

namespace fv {
namespace routing {
namespace {

/// Length of the synthetic arc a legend row is costed on. The factor is per
/// metre, so any length gives the same answer.
constexpr float kSpecimenMetres = 1000.0f;

/// A number for a person: up to three significant figures, no trailing zeros.
std::string Number(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.3g", v);
  return buf;
}

std::string Spaced(std::string s) {
  for (char& c : s) {
    if (c == '_') c = ' ';
  }
  return s;
}

const char* ProvisionLabel(BikeProvision b) {
  switch (b) {
    case BikeProvision::kSharedLane: return "shared lane";
    case BikeProvision::kLane: return "painted lane";
    case BikeProvision::kTrack: return "protected track";
    case BikeProvision::kNone: break;
  }
  return "";
}

const char* RouteLabel(BikeRoute r) {
  switch (r) {
    case BikeRoute::kLocal: return "local route";
    case BikeRoute::kRegional: return "regional route";
    case BikeRoute::kNational: return "national route";
    case BikeRoute::kNone: break;
  }
  return "";
}

const char* SurfaceLabel(RoadSurface s) {
  switch (s) {
    case RoadSurface::kPaved: return "paved surface";
    case RoadSurface::kCompacted: return "compacted surface";
    case RoadSurface::kRough: return "rough surface";
    case RoadSurface::kUnknown: break;
  }
  return "";
}

BikeRoute NetworkFromPseudoTag(const std::string& v) {
  if (v == "national") return BikeRoute::kNational;
  if (v == "regional") return BikeRoute::kRegional;
  return BikeRoute::kLocal;
}

void AddTerm(std::vector<CostTerm>* terms, std::string label, double factor) {
  if (std::fabs(factor - 1.0) > 1e-9) terms->push_back(CostTerm{std::move(label), factor});
}

/// The multipliers ArcCost applies on the profile path, named. Kept beside
/// CostOfTags, whose factor comes from ArcCost itself; the tests check that
/// their product matches it.
std::vector<CostTerm> TermsOf(const RoadArc& arc, const RouteProfile& p,
                              const RouteOptions& options, double reference) {
  std::vector<CostTerm> terms;
  AddTerm(&terms, Spaced(RoadClassName(arc.klass)), p.weight(arc.klass));
  if (p.metric == RouteMetric::kTime) {
    const double seconds =
        arc.is_ferry() ? arc.travel_seconds() : p.Seconds(arc.length_m, arc.speed_kph, arc.klass);
    const double kph = arc.length_m / seconds * 3.6;
    AddTerm(&terms, "at " + Number(kph) + " km/h", seconds / reference);
  }
  AddTerm(&terms, ProvisionLabel(arc.bike), p.bike_lane_factor[static_cast<size_t>(arc.bike)]);
  AddTerm(&terms, RouteLabel(arc.bike_route),
          p.bike_route_factor[static_cast<size_t>(arc.bike_route)]);
  AddTerm(&terms, SurfaceLabel(arc.surface), p.surface_factor[static_cast<size_t>(arc.surface)]);
  if (arc.speed_posted() && arc.bike != BikeProvision::kTrack)
    AddTerm(&terms, "posted " + std::to_string(arc.speed_kph) + " km/h",
            p.SpeedFactor(arc.speed_kph));
  const bool is_private = p.mode == TravelMode::kBicycle ? arc.bicycle_private()
                          : p.mode == TravelMode::kFoot  ? arc.foot_private()
                                                         : arc.motor_vehicle_private();
  if (is_private && p.private_penalty > 0.0) AddTerm(&terms, "private", p.private_penalty);
  if (arc.golf_cartpath() && options.golf_cartpath_penalty > 0.0)
    AddTerm(&terms, "golf cart path", options.golf_cartpath_penalty);
  const bool golf_path_applies =
      arc.golf_path() && !(p.mode == TravelMode::kBicycle && arc.bicycle_designated());
  if (golf_path_applies && options.golf_path_penalty > 0.0)
    AddTerm(&terms, "golf path", options.golf_path_penalty);
  if (arc.tolled() && options.toll_penalty > 0.0) AddTerm(&terms, "toll", options.toll_penalty);
  if (arc.is_ferry() && options.ferry_penalty > 0.0)
    AddTerm(&terms, "ferry", options.ferry_penalty);
  if (arc.is_beach()) {
    const double beach = BeachFactor(arc, options);
    if (beach > 0.0) AddTerm(&terms, "beach", beach);
  }
  return terms;
}

std::string Verb(TravelMode mode) {
  switch (mode) {
    case TravelMode::kFoot: return "walk";
    case TravelMode::kMotorVehicle: return "drive";
    case TravelMode::kBicycle: break;
  }
  return "ride";
}

std::string Traveller(TravelMode mode) {
  switch (mode) {
    case TravelMode::kFoot: return "a walk";
    case TravelMode::kMotorVehicle: return "a car";
    case TravelMode::kBicycle: break;
  }
  return "a bike";
}

}  // namespace

LegendCost CostOfTags(const RouteProfile& profile, const std::vector<OsmTag>& osm_tags) {
  LegendCost out;
  OsmWay way;
  BikeRoute network = BikeRoute::kNone;
  for (const OsmTag& t : osm_tags) {
    if (t.key == kRouteNetworkPseudoTag) network = NetworkFromPseudoTag(t.value);
    else way.tags.push_back(t);
  }
  RoadArc arc;
  if (!ReadWayTags(way, RoadGraphBuildOptions{}, &arc)) {
    out.excluded = true;
    return out;
  }
  arc.bike_route = network;
  arc.length_m = kSpecimenMetres;

  // Non-owning: the caller's profile outlives this call.
  RouteOptions options;
  ApplyProfile(std::shared_ptr<const RouteProfile>(std::shared_ptr<void>(), &profile), &options);
  if (!ArcUsable(arc, options)) {
    out.excluded = true;
    return out;
  }
  const double reference = profile.metric == RouteMetric::kDistance
                               ? arc.length_m
                               : arc.length_m / (profile.fixed_kph / 3.6);
  out.factor = ArcCost(arc, options) / reference;
  out.terms = TermsOf(arc, profile, options, reference);
  out.crossing_seconds =
      profile.crossing_seconds[static_cast<size_t>(CrossingClassOf(arc.klass))];
  return out;
}

std::string LegendTierWord(const RouteProfile& profile, const LegendCost& cost) {
  if (cost.excluded) return "Never";
  for (const auto& tier : profile.legend_tiers) {
    if (cost.factor <= tier.first * (1.0 + 1e-9)) return tier.second;
  }
  return profile.legend_tiers.empty() ? std::string() : profile.legend_tiers.back().second;
}

std::string LegendSentence(const RouteProfile& profile, const LegendCost& cost,
                           const std::string& app_name) {
  if (cost.excluded) return app_name + " never routes " + Traveller(profile.mode) + " here.";
  const LegendCost base = CostOfTags(profile, profile.legend_baseline);
  std::string text;
  if (!base.excluded && base.factor > 0.0) {
    const double r = cost.factor / base.factor;
    const std::string lead = app_name + " will " + Verb(profile.mode) + " up to ";
    if (std::fabs(r - 1.0) < 0.005) {
      std::string what = "the baseline";
      for (const OsmTag& t : profile.legend_baseline) {
        if (t.key == "highway") what = "a " + Spaced(t.value) + " street";
      }
      text = app_name + " weighs this the same as " + what + ".";
    } else if (r < 1.0) {
      text = lead + std::to_string(std::lround((1.0 / r - 1.0) * 100.0)) +
             "% further to use this.";
    } else if (r < 2.0) {
      text = lead + std::to_string(std::lround((r - 1.0) * 100.0)) + "% further to avoid this.";
    } else {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.1f", r);
      std::string times = buf;
      if (times.size() > 2 && times.compare(times.size() - 2, 2, ".0") == 0)
        times.resize(times.size() - 2);
      text = lead + times + " times as far to avoid this.";
    }
  }
  if (cost.crossing_seconds > 0.0) {
    if (!text.empty()) text += ' ';
    text += "Crossing it without a signal adds " +
            std::to_string(std::lround(cost.crossing_seconds)) + " s.";
  }
  return text;
}

std::string LegendTermsLine(const LegendCost& cost) {
  if (cost.excluded) return "excluded";
  std::string line;
  for (const CostTerm& t : cost.terms) {
    if (!line.empty()) line += " × ";
    line += t.label + " " + Number(t.factor);
  }
  if (line.empty()) return Number(cost.factor);
  return line + " = " + Number(cost.factor);
}

}  // namespace routing
}  // namespace fv
