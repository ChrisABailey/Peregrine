// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The router's cost of a way described by raw OSM tags, for a map legend: a
/// factor relative to the profile's own speed, the terms behind it, the
/// setting a rider would adjust for it, the profile's tier word for it, and a
/// sentence comparing it with factor 1.0, the shortest way. The tags go
/// through the graph builder's way reader and the router's ArcCost, so the
/// legend cannot disagree with a route.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "fv_osm_reader.h"
#include "fv_route_rules.h"

namespace fv {
namespace routing {

/// Raw-tag key standing for membership of a signed `route=bicycle` relation:
/// "local", "regional" or "national".
constexpr const char* kRouteNetworkPseudoTag = "peregrine:route_network";

/// What a profile setting is to the map key.
enum class CostTermKind {
  kClass,      ///< a road class weight (`classes.*`): the key's tier slider
  kModifier,   ///< a lane, route, unpaved surface or golf way: the key's relative slider
  kCondition,  ///< private, toll, ferry or beach: names a row, no slider in the key
  kFixed,      ///< pace, posted speed or paved surface: part of the road, not a row
};

/// One multiplier that applied, e.g. {"secondary", 2.5} or {"painted lane", 0.6}.
struct CostTerm {
  std::string label;
  /// Infinite when the setting excludes the way.
  double factor = 1.0;
  /// The rule file's key path, e.g. "classes.tertiary" or "bike_lane_factor.lane".
  std::string setting;
  CostTermKind kind = CostTermKind::kFixed;
};

/// The cost of riding one metre of a way, relative to one metre at the
/// profile's own speed (or one metre, on a distance profile).
struct LegendCost {
  double factor = 1.0;
  /// The profile may not use the way at all; `factor` and `terms` are empty.
  bool excluded = false;
  /// Every multiplier other than 1.0, in the order ArcCost applies them.
  /// Their product is `factor`.
  std::vector<CostTerm> terms;
  /// The setting the row's label names: the way's one modifier or condition,
  /// or its class when it has none, even when its value is 1.0. `setting` is
  /// empty when the way carries two or more, or when the tags are not a way.
  CostTerm setting;
  /// The key offers no slider: the profile's `key_locks` name the setting,
  /// or the way is excluded by its access tags or a non-class rule-file
  /// penalty. A class the profile merely excludes is not locked.
  bool locked = false;
  /// Why the row is locked, for the key; empty when not locked or the tags
  /// are not a way.
  std::string lock_reason;
  /// Seconds of cost for crossing this road where there is no signal; 0 when
  /// the road is not busier than a minor street or the profile prices nothing.
  double crossing_seconds = 0.0;
};

/// Costs the way `osm_tags` describes under `profile`, in the way's forward
/// direction. The tags are read as the graph builder reads them, with default
/// build options; `peregrine:route_network` sets the signed-route network.
LegendCost CostOfTags(const RouteProfile& profile, const std::vector<OsmTag>& osm_tags);

/// The profile's `legend_tiers` word for `cost`, or "Never" when excluded.
std::string LegendTierWord(const RouteProfile& profile, const LegendCost& cost);

/// One or two sentences a rider reads: how far `app_name` will go to use or
/// avoid this way compared with the shortest way (factor 1.0), and what an
/// unsignalled crossing of it costs. A modifier row compares the same road
/// with and without the modifier. A profile whose speed varies says "as long".
std::string LegendSentence(const RouteProfile& profile, const LegendCost& cost,
                           const std::string& app_name);

/// The terms as one line, e.g. "secondary 2.5 × painted lane 0.6 = 1.5".
std::string LegendTermsLine(const LegendCost& cost);

/// One position of a key slider.
struct LegendStop {
  const char* word;
  /// The setting value the stop stands for; kClassExcluded for Never.
  double value;
};

/// The kind of slider `setting` gets in the key: kClass for `classes.*`,
/// kModifier for a lane, route, unpaved surface or golf setting, kCondition
/// for private, toll, ferry and beach, kFixed for anything else.
CostTermKind SettingKind(const std::string& setting);

/// A slider's stops, best first: the tier scale for kClass (Most preferred
/// 0.6 … Last resort 8, Never), the relative scale for kModifier (Much better
/// 0.45 … Much worse 2.5). Empty for the kinds the key gives no slider.
const std::vector<LegendStop>& LegendStops(CostTermKind kind);

/// The stop nearest `value` (by ratio) on `kind`'s slider; Never only for an
/// excluded class. A non-positive penalty reads as 1.0. -1 when `kind` has
/// no slider or the value has no stop (an excluded modifier).
int LegendStopOf(CostTermKind kind, double value);

/// A rider's slider positions: setting id → stop index.
using LegendStopChoices = std::map<std::string, int>;

/// Turns slider positions into ApplyOverrides input for `shipped`. A setting
/// left on the stop holding its shipped value gets no override, so the
/// profile keeps that exact value rather than the stop's stand-in. A setting
/// `shipped` locks in `key_locks`, or an excluded golf way, is skipped and
/// named in `ignored`, so a rule file that later locks one does not void the
/// rest. A class `shipped` excludes is adjusted like any other. An unknown
/// or slider-less setting or a stop out of range is an error and leaves `out`
/// untouched.
Status OverridesFromStops(const RouteProfile& shipped, const LegendStopChoices& stops,
                          ProfileOverrides* out, std::vector<std::string>* ignored = nullptr);

/// A modifier specimen moved onto one road class.
struct LegendOnRoad {
  /// The key's name for the class, e.g. "Busy road".
  std::string label;
  /// The `highway` value, e.g. "secondary".
  std::string highway;
  LegendCost cost;
};

/// For a modifier row, the specimen's tags costed on a quiet street, a
/// moderate road, a busy road and an arterial (residential, tertiary,
/// secondary, primary), for the key's "On the map this becomes" line. Empty
/// when the row's setting is not a modifier, is a golf way (a path, never a
/// road), or the tags carry no `highway`.
std::vector<LegendOnRoad> ModifierOnRoads(const RouteProfile& profile,
                                          const std::vector<OsmTag>& osm_tags);

}  // namespace routing
}  // namespace fv
