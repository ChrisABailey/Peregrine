// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_route_legend.h"

#include <cmath>
#include <cstdio>
#include <limits>
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

// The rule file's key for each value, which is also its setting id's suffix.
const char* ProvisionKey(BikeProvision b) {
  switch (b) {
    case BikeProvision::kSharedLane: return "shared_lane";
    case BikeProvision::kLane: return "lane";
    case BikeProvision::kTrack: return "track";
    case BikeProvision::kNone: break;
  }
  return "";
}

const char* RouteKey(BikeRoute r) {
  switch (r) {
    case BikeRoute::kLocal: return "local";
    case BikeRoute::kRegional: return "regional";
    case BikeRoute::kNational: return "national";
    case BikeRoute::kNone: break;
  }
  return "";
}

const char* SurfaceKey(RoadSurface s) {
  switch (s) {
    case RoadSurface::kPaved: return "paved";
    case RoadSurface::kCompacted: return "compacted";
    case RoadSurface::kRough: return "rough";
    case RoadSurface::kUnknown: break;
  }
  return "";
}

BikeRoute NetworkFromPseudoTag(const std::string& v) {
  if (v == "national") return BikeRoute::kNational;
  if (v == "regional") return BikeRoute::kRegional;
  return BikeRoute::kLocal;
}

/// A setting's value as a multiplier: an exclusion is infinite and a
/// non-positive penalty means "no preference", as ArcCost reads it.
double Multiplier(double v) {
  if (v == kAvoidExcluded) return std::numeric_limits<double>::infinity();
  return v > 0.0 ? v : 1.0;
}

void AddTerm(std::vector<CostTerm>* terms, std::string label, double factor, std::string setting,
             CostTermKind kind) {
  terms->push_back(CostTerm{std::move(label), Multiplier(factor), std::move(setting), kind});
}

/// Every setting ArcCost consults for this arc on the profile path, named, in
/// the order it applies them, including those whose value is 1.0. Kept beside
/// CostOfTags, whose factor comes from ArcCost itself; the tests check that
/// the product of the terms that are not 1.0 matches it.
std::vector<CostTerm> SettingsOf(const RoadArc& arc, const RouteProfile& p,
                                 const RouteOptions& options, double reference) {
  std::vector<CostTerm> terms;
  const std::string klass = RoadClassName(arc.klass);
  AddTerm(&terms, Spaced(klass), p.allows(arc.klass) ? p.weight(arc.klass) : kAvoidExcluded,
          "classes." + klass, CostTermKind::kClass);
  if (p.metric == RouteMetric::kTime) {
    const double seconds =
        arc.is_ferry() ? arc.travel_seconds() : p.Seconds(arc.length_m, arc.speed_kph, arc.klass);
    const double kph = arc.length_m / seconds * 3.6;
    AddTerm(&terms, "at " + Number(kph) + " km/h", seconds / reference, "class_kph." + klass,
            CostTermKind::kFixed);
  }
  if (arc.bike != BikeProvision::kNone)
    AddTerm(&terms, ProvisionLabel(arc.bike), p.bike_lane_factor[static_cast<size_t>(arc.bike)],
            std::string("bike_lane_factor.") + ProvisionKey(arc.bike), CostTermKind::kModifier);
  if (arc.bike_route != BikeRoute::kNone)
    AddTerm(&terms, RouteLabel(arc.bike_route),
            p.bike_route_factor[static_cast<size_t>(arc.bike_route)],
            std::string("bike_route_factor.") + RouteKey(arc.bike_route),
            CostTermKind::kModifier);
  if (arc.surface != RoadSurface::kUnknown)
    AddTerm(&terms, SurfaceLabel(arc.surface), p.surface_factor[static_cast<size_t>(arc.surface)],
            std::string("surface_factor.") + SurfaceKey(arc.surface),
            // Paved is the reference surface; the key offers only the others.
            arc.surface == RoadSurface::kPaved ? CostTermKind::kFixed : CostTermKind::kModifier);
  if (arc.speed_posted() && arc.bike != BikeProvision::kTrack)
    AddTerm(&terms, "posted " + std::to_string(arc.speed_kph) + " km/h",
            p.SpeedFactor(arc.speed_kph), "speed_factor", CostTermKind::kFixed);
  const bool is_private = p.mode == TravelMode::kBicycle ? arc.bicycle_private()
                          : p.mode == TravelMode::kFoot  ? arc.foot_private()
                                                         : arc.motor_vehicle_private();
  if (is_private)
    AddTerm(&terms, "private", p.private_penalty, "private_penalty", CostTermKind::kCondition);
  if (arc.golf_cartpath())
    AddTerm(&terms, "golf cart path", options.golf_cartpath_penalty, "golf_cartpath_penalty",
            CostTermKind::kModifier);
  if (arc.golf_path() && !(p.mode == TravelMode::kBicycle && arc.bicycle_designated()))
    AddTerm(&terms, "golf path", options.golf_path_penalty, "golf_path_penalty",
            CostTermKind::kModifier);
  if (arc.tolled())
    AddTerm(&terms, "toll", options.toll_penalty, "toll_penalty", CostTermKind::kCondition);
  if (arc.is_ferry())
    AddTerm(&terms, "ferry", options.ferry_penalty, "ferry_penalty", CostTermKind::kCondition);
  if (arc.is_beach())
    AddTerm(&terms, "beach", BeachFactor(arc, options), "beach_penalty", CostTermKind::kCondition);
  return terms;
}

/// The setting a legend row's label names: the arc's one modifier or
/// condition, or its class when it carries none. Pace and posted speed belong
/// to the road, not the row. An empty setting when it carries two or more.
CostTerm SubjectOf(const std::vector<CostTerm>& settings) {
  const CostTerm* subject = nullptr;
  for (const CostTerm& t : settings) {
    if (t.kind != CostTermKind::kModifier && t.kind != CostTermKind::kCondition) continue;
    if (subject != nullptr) return CostTerm{"", 1.0, "", CostTermKind::kFixed};
    subject = &t;
  }
  if (subject == nullptr) subject = &settings.front();
  return *subject;
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

/// Who a mode's access tags close a way to, e.g. "bikes".
std::string Travellers(TravelMode mode) {
  switch (mode) {
    case TravelMode::kFoot: return "walkers";
    case TravelMode::kMotorVehicle: return "cars";
    case TravelMode::kBicycle: break;
  }
  return "bikes";
}

/// `arc` is unusable under `profile` only because its class is excluded:
/// with that class allowed it would be usable.
bool ExcludedOnlyByClass(const RoadArc& arc, const RouteProfile& profile) {
  if (profile.allows(arc.klass) || arc.klass >= RoadClass::kCount) return false;
  RouteProfile allowed = profile;
  allowed.class_weight[static_cast<size_t>(arc.klass)] = 1.0;
  RouteOptions options;
  ApplyProfile(std::shared_ptr<const RouteProfile>(std::shared_ptr<void>(), &allowed), &options);
  return ArcUsable(arc, options);
}

/// A ratio with at most two decimals and no trailing zeros, e.g. "1.67".
std::string Times(double r) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", r);
  std::string s = buf;
  while (s.back() == '0') s.pop_back();
  if (s.back() == '.') s.pop_back();
  return s;
}

/// "far" on a distance profile or a fixed speed, "long" where speed varies.
const char* Extent(const RouteProfile& p) {
  return p.metric == RouteMetric::kTime && p.speed_source != SpeedSource::kFixed ? "long"
                                                                                 : "far";
}

/// How a modifier row names the way with the modifier and the same way without it.
struct ModifierWords {
  const char* with;
  const char* without;
};

ModifierWords WordsFor(const std::string& setting) {
  if (setting == "bike_lane_factor.shared_lane")
    return {"a road with shared-lane markings", "the same road without them"};
  if (setting == "bike_lane_factor.lane")
    return {"a road with a painted lane", "the same road without one"};
  if (setting == "bike_lane_factor.track")
    return {"a road with a protected track", "the same road without one"};
  if (setting == "bike_route_factor.local")
    return {"a road on a local bike route", "the same road off it"};
  if (setting == "bike_route_factor.regional")
    return {"a road on a regional bike route", "the same road off it"};
  if (setting == "bike_route_factor.national")
    return {"a road on a national bike route", "the same road off it"};
  if (setting == "surface_factor.compacted")
    return {"a way with a compacted surface", "the same way paved"};
  if (setting == "surface_factor.rough")
    return {"a way with a rough surface", "the same way paved"};
  if (setting == "golf_cartpath_penalty")
    return {"a golf cart path", "the same path off the course"};
  if (setting == "golf_path_penalty") return {"a golf path", "the same path off the course"};
  return {"a way with this", "the same way without it"};
}

/// The classes a modifier row's "On the map this becomes" line shows.
struct RoadOfKey {
  const char* label;
  const char* highway;
};
constexpr RoadOfKey kRoadsOfKey[] = {
    {"Quiet street", "residential"},
    {"Moderate road", "tertiary"},
    {"Busy road", "secondary"},
    {"Arterial", "primary"},
};

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
    out.locked = true;
    return out;
  }
  arc.bike_route = network;
  arc.length_m = kSpecimenMetres;

  // Non-owning: the caller's profile outlives this call.
  RouteOptions options;
  ApplyProfile(std::shared_ptr<const RouteProfile>(std::shared_ptr<void>(), &profile), &options);
  const double reference = profile.metric == RouteMetric::kDistance
                               ? arc.length_m
                               : arc.length_m / (profile.fixed_kph / 3.6);
  const std::vector<CostTerm> settings = SettingsOf(arc, profile, options, reference);
  out.setting = SubjectOf(settings);
  const auto declared = profile.key_locks.find(out.setting.setting);
  if (declared != profile.key_locks.end()) {
    out.locked = true;
    out.lock_reason = declared->second;
  }
  if (!ArcUsable(arc, options)) {
    out.excluded = true;
    if (!out.locked && !ExcludedOnlyByClass(arc, profile)) {
      out.locked = true;
      double value = 0.0;
      const bool by_rules = out.setting.kind != CostTermKind::kClass &&
                            ProfileSetting(profile, out.setting.setting, &value) &&
                            value == kClassExcluded;
      out.lock_reason = by_rules
                            ? "The routing rules exclude this way."
                            : "The map marks this way closed to " + Travellers(profile.mode) + ".";
    }
    return out;
  }
  out.factor = ArcCost(arc, options) / reference;
  for (const CostTerm& t : settings) {
    if (std::fabs(t.factor - 1.0) > 1e-9) out.terms.push_back(t);
  }
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
  const std::string extent = Extent(profile);
  const std::string lead = app_name + " will " + Verb(profile.mode) + " up to ";
  std::string text;
  if (cost.setting.kind == CostTermKind::kModifier) {
    const ModifierWords words = WordsFor(cost.setting.setting);
    const double m = cost.setting.factor;
    if (Times(m) == "1") {
      text = app_name + " treats " + words.with + " the same as " + words.without + ".";
    } else if (m < 1.0) {
      text = lead + Times(1.0 / m) + "× as " + extent + " to use " + words.with + " instead of " +
             words.without + ".";
    } else {
      text = app_name + " counts " + words.with + " as " + Times(m) + "× as " + extent + " as " +
             words.without + ".";
    }
  } else {
    const double f = cost.factor;
    if (Times(f) == "1") {
      text = app_name + " neither seeks this out nor avoids it.";
    } else if (f < 1.0) {
      text = lead + Times(1.0 / f) + "× as " + extent + " as the " +
             (extent == "far" ? "shortest" : "quickest") + " way to stay on this.";
    } else {
      text = app_name + " uses this only when the alternative is more than " + Times(f) +
             "× as " + extent + ".";
    }
  }
  if (cost.crossing_seconds > 0.0) {
    text += " Crossing it without a signal adds " +
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

CostTermKind SettingKind(const std::string& setting) {
  const std::string group = setting.substr(0, setting.find('.'));
  if (group == "classes") return CostTermKind::kClass;
  if (group == "bike_lane_factor" || group == "bike_route_factor" ||
      setting == "surface_factor.compacted" || setting == "surface_factor.rough" ||
      setting == "golf_cartpath_penalty" || setting == "golf_path_penalty")
    return CostTermKind::kModifier;
  if (setting == "private_penalty" || setting == "toll_penalty" || setting == "ferry_penalty" ||
      setting == "beach_penalty")
    return CostTermKind::kCondition;
  return CostTermKind::kFixed;
}

const std::vector<LegendStop>& LegendStops(CostTermKind kind) {
  static const std::vector<LegendStop> kClassStops = {
      {"Most preferred", 0.6}, {"Preferred", 0.9},    {"Acceptable", 1.5},
      {"Avoided", 3.5},        {"Last resort", 8.0}, {"Never", kClassExcluded},
  };
  static const std::vector<LegendStop> kModifierStops = {
      {"Much better", 0.45}, {"Better", 0.6}, {"A little better", 0.9},
      {"No difference", 1.0}, {"Worse", 1.6}, {"Much worse", 2.5},
  };
  static const std::vector<LegendStop> kNone;
  switch (kind) {
    case CostTermKind::kClass: return kClassStops;
    case CostTermKind::kModifier: return kModifierStops;
    case CostTermKind::kCondition:
    case CostTermKind::kFixed: break;
  }
  return kNone;
}

int LegendStopOf(CostTermKind kind, double value) {
  const std::vector<LegendStop>& stops = LegendStops(kind);
  if (stops.empty()) return -1;
  if (value == kClassExcluded) {
    return stops.back().value == kClassExcluded ? static_cast<int>(stops.size()) - 1 : -1;
  }
  const double v = value > 0.0 ? value : 1.0;
  int best = -1;
  double best_distance = 0.0;
  for (size_t i = 0; i < stops.size(); ++i) {
    if (stops[i].value == kClassExcluded) continue;
    const double d = std::fabs(std::log(v / stops[i].value));
    // Strictly nearer: a value midway between two stops takes the better one.
    if (best < 0 || d < best_distance - 1e-12) {
      best = static_cast<int>(i);
      best_distance = d;
    }
  }
  return best;
}

Status OverridesFromStops(const RouteProfile& shipped, const LegendStopChoices& stops,
                          ProfileOverrides* out, std::vector<std::string>* ignored) {
  ProfileOverrides overrides;
  std::vector<std::string> skipped;
  for (const auto& [id, stop] : stops) {
    const CostTermKind kind = SettingKind(id);
    const std::vector<LegendStop>& scale = LegendStops(kind);
    double value = 0.0;
    if (scale.empty() || !ProfileSetting(shipped, id, &value))
      return Status::Error(kInvalidArg, "no slider for setting '" + id + "'");
    if (stop < 0 || stop >= static_cast<int>(scale.size()))
      return Status::Error(kInvalidArg, "stop " + std::to_string(stop) + " out of range for '" +
                                            id + "'");
    // An excluded class is on the slider at Never; an excluded golf way has no stop.
    if (shipped.key_locks.count(id) != 0 || (value == kClassExcluded && kind != CostTermKind::kClass)) {
      skipped.push_back(id);
      continue;
    }
    if (stop == LegendStopOf(kind, value)) continue;
    overrides[id] = scale[static_cast<size_t>(stop)].value;
  }
  *out = std::move(overrides);
  if (ignored != nullptr) *ignored = std::move(skipped);
  return Status::Ok();
}

std::vector<LegendOnRoad> ModifierOnRoads(const RouteProfile& profile,
                                          const std::vector<OsmTag>& osm_tags) {
  std::vector<LegendOnRoad> out;
  const LegendCost specimen = CostOfTags(profile, osm_tags);
  const std::string& setting = specimen.setting.setting;
  if (specimen.setting.kind != CostTermKind::kModifier || setting.rfind("golf_", 0) == 0)
    return out;
  size_t highway = osm_tags.size();
  for (size_t i = 0; i < osm_tags.size(); ++i) {
    if (osm_tags[i].key == "highway") highway = i;
  }
  if (highway == osm_tags.size()) return out;
  std::vector<OsmTag> tags = osm_tags;
  for (const RoadOfKey& road : kRoadsOfKey) {
    tags[highway].value = road.highway;
    out.push_back(LegendOnRoad{road.label, road.highway, CostOfTags(profile, tags)});
  }
  return out;
}

}  // namespace routing
}  // namespace fv
