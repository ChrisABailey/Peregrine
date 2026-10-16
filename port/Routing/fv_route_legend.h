// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The router's cost of a way described by raw OSM tags, for a map legend: a
/// factor relative to the profile's own speed, the terms behind it, the
/// profile's tier word for it, and a sentence comparing it with the profile's
/// baseline street. The tags go through the graph builder's way reader and the
/// router's ArcCost, so the legend cannot disagree with a route.

#pragma once

#include <string>
#include <vector>

#include "fv_osm_reader.h"
#include "fv_route_rules.h"

namespace fv {
namespace routing {

/// Raw-tag key standing for membership of a signed `route=bicycle` relation:
/// "local", "regional" or "national".
constexpr const char* kRouteNetworkPseudoTag = "peregrine:route_network";

/// One multiplier that applied, e.g. {"secondary", 2.5} or {"painted lane", 0.6}.
struct CostTerm {
  std::string label;
  double factor = 1.0;
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
/// avoid this way, compared with the profile's `legend_baseline`, and what an
/// unsignalled crossing of it costs.
std::string LegendSentence(const RouteProfile& profile, const LegendCost& cost,
                           const std::string& app_name);

/// The terms as one line, e.g. "secondary 2.5 × painted lane 0.6 = 1.5".
std::string LegendTermsLine(const LegendCost& cost);

}  // namespace routing
}  // namespace fv
