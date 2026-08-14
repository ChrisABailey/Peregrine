// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvgraph — build a routable road graph from a raw OSM extract, and query it.
//
//   fvgraph build -o roads.fvroad south-carolina.osm.pbf [more.osm ...]
//   fvgraph info  roads.fvroad
//   fvgraph route roads.fvroad 32.60 -80.13 32.63 -79.99 [--distance] [--walk]
//                 [--dijkstra] [--geojson out.json]
//
// The build step is the offline half of O4 (the fvpack pattern): it reads the
// RAW extract, not the MVT pyramid, because vector tiles carry no topology.

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "fv_road_graph.h"
#include "fv_route_rules.h"
#include "fv_router.h"

#ifndef FV_ROUTE_RULES_FILE
#define FV_ROUTE_RULES_FILE ""
#endif

namespace {

using fv::Status;
using fv::routing::RoadGraph;

int Usage() {
  std::fprintf(stderr,
               "usage:\n"
               "  fvgraph build -o OUT.fvroad INPUT.osm[.pbf] [INPUT ...]\n"
               "                [--drive-only] [--cycle-only] [--no-oneway]\n"
               "                [--ignore-access] [--ignore-turns] [--no-ferries]\n"
               "  fvgraph info  GRAPH.fvroad\n"
               "  fvgraph route GRAPH.fvroad LAT1 LON1 LAT2 LON2\n"
               "                [--via LAT LON ...] [--u-turns]\n"
               "                [--distance] [--walk] [--cycle] [--dijkstra]\n"
               "                [--ignore-turns] [--private-penalty X] [--snap M]\n"
               "                [--avoid-toll] [--avoid-ferry]\n"
               "                [--toll-penalty X] [--ferry-penalty X]\n"
               "                [--rules FILE] [--profile NAME] [--geojson OUT.json]\n"
               "  fvgraph profiles [RULES.json]\n"
               "\n"
               "  --profile takes its weights from the JSON rule file, which is\n"
               "  reread whenever it changes — see rules/route-weights.json.\n"
               "  --via adds an ordered stop between the two ends; repeat it for\n"
               "  more. The route goes THROUGH each stop rather than restarting\n"
               "  at it, so signage at a stop binds and it does not turn round\n"
               "  there unless there is no other way out. --u-turns allows the\n"
               "  turn-round back.\n"
               "  --avoid-toll / --avoid-ferry refuse those arcs outright, which\n"
               "  can leave a destination unreachable; --toll-penalty X and\n"
               "  --ferry-penalty X merely price them up (1.0 = no preference).\n"
               "  Any of the four overrides what --profile asked for, whatever\n"
               "  order they are written in.\n");
  return 2;
}

int Fail(const Status& s) {
  std::fprintf(stderr, "fvgraph: %s (code %d)\n", s.message.c_str(), s.code);
  return 1;
}

int Build(int argc, char** argv) {
  std::string out_path;
  std::vector<std::string> inputs;
  fv::routing::RoadGraphBuildOptions options;

  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-o" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (a == "--drive-only") {
      options.include_non_driveable = false;
    } else if (a == "--no-oneway") {
      options.honor_oneway = false;
    } else if (a == "--ignore-access") {
      options.honor_access = false;
    } else if (a == "--cycle-only") {
      options.cycle_only = true;
    } else if (a == "--ignore-turns") {
      options.honor_turn_restrictions = false;
    } else if (a == "--no-ferries") {
      options.include_ferries = false;
    } else if (!a.empty() && a[0] == '-') {
      return Usage();
    } else {
      inputs.push_back(a);
    }
  }
  if (out_path.empty() || inputs.empty()) return Usage();

  RoadGraph graph;
  fv::routing::RoadGraphBuildStats stats;
  Status s = fv::routing::BuildRoadGraph(inputs, options, &graph, &stats);
  if (!s.ok()) return Fail(s);
  s = graph.Save(out_path);
  if (!s.ok()) return Fail(s);

  std::printf("built %s\n", out_path.c_str());
  std::printf("  ways seen/kept   %lld / %lld (%lld dropped: barred to every mode)\n",
              static_cast<long long>(stats.ways_seen),
              static_cast<long long>(stats.ways_kept),
              static_cast<long long>(stats.ways_dropped_access));
  std::printf("  nodes needed/res %lld / %lld\n", static_cast<long long>(stats.nodes_needed),
              static_cast<long long>(stats.nodes_resolved));
  std::printf("  edges            %lld (%lld dropped, ran off the extract)\n",
              static_cast<long long>(stats.edges),
              static_cast<long long>(stats.edges_dropped_unresolved));
  std::printf("  turn restr seen  %lld (%lld turns applied, %lld via-way skipped,\n"
              "                    %lld unresolved, %lld excepted)\n",
              static_cast<long long>(stats.restrictions_seen),
              static_cast<long long>(stats.restrictions_applied),
              static_cast<long long>(stats.restrictions_via_way),
              static_cast<long long>(stats.restrictions_unresolved),
              static_cast<long long>(stats.restrictions_excepted));
  // Per-mode, because a graph whose driveable network has vanished looks
  // exactly like a healthy one in the totals above until a route fails.
  std::printf("  access barred    %lld car / %lld bike / %lld foot arcs\n",
              static_cast<long long>(stats.arcs_no_motor_vehicle),
              static_cast<long long>(stats.arcs_no_bicycle),
              static_cast<long long>(stats.arcs_no_foot));
  std::printf("  access private   %lld car / %lld bike / %lld foot arcs (passable, penalised)\n",
              static_cast<long long>(stats.arcs_private_motor_vehicle),
              static_cast<long long>(stats.arcs_private_bicycle),
              static_cast<long long>(stats.arcs_private_foot));
  std::printf("  ferry / toll     %lld ferry ways (%lld timed by `duration`), "
              "%lld ferry arcs, %lld tolled arcs\n",
              static_cast<long long>(stats.ways_ferry),
              static_cast<long long>(stats.ferries_timed),
              static_cast<long long>(stats.arcs_ferry),
              static_cast<long long>(stats.arcs_toll));
  std::printf("  graph            %u nodes, %u arcs, %u shape points\n", graph.node_count(),
              graph.arc_count(), graph.geometry_count());
  return 0;
}

int Info(int argc, char** argv) {
  if (argc != 1) return Usage();
  RoadGraph graph;
  Status s = RoadGraph::Load(argv[0], &graph);
  if (!s.ok()) return Fail(s);
  const fv::GeoRect b = graph.bounds();
  std::printf("%s\n", argv[0]);
  std::printf("  nodes %u  arcs %u  shape points %u\n", graph.node_count(), graph.arc_count(),
              graph.geometry_count());
  std::printf("  bounds %.6f,%.6f .. %.6f,%.6f\n", b.ll.lat, b.ll.lon, b.ur.lat, b.ur.lon);

  double total_km = 0.0;
  int oneway = 0;
  for (uint32_t a = 0; a < graph.arc_count(); ++a) {
    const fv::routing::RoadArc& arc = graph.arc(a);
    total_km += arc.length_m / 2000.0;  // each edge is stored as two arcs
    if (arc.forward() != arc.backward()) ++oneway;
  }
  std::printf("  centreline %.1f km, %d one-way arcs\n", total_km, oneway);

  // The same per-mode picture `build` prints, but off the finished file — the
  // graph in the app is this one, not the build log somebody kept.
  int barred[3] = {0, 0, 0}, priv[3] = {0, 0, 0}, usable_car = 0;
  int ferry_arcs = 0, toll_arcs = 0;
  double ferry_km = 0.0;
  for (uint32_t a = 0; a < graph.arc_count(); ++a) {
    const fv::routing::RoadArc& arc = graph.arc(a);
    if (arc.is_ferry()) {
      ++ferry_arcs;
      ferry_km += arc.length_m / 2000.0;  // two arcs per edge, as above
    }
    if (arc.tolled()) ++toll_arcs;
    if (!arc.motor_vehicle_allowed()) ++barred[0];
    if (!arc.bicycle_allowed()) ++barred[1];
    if (!arc.foot_allowed()) ++barred[2];
    if (arc.motor_vehicle_private()) ++priv[0];
    if (arc.bicycle_private()) ++priv[1];
    if (arc.foot_private()) ++priv[2];
    if (fv::routing::IsDriveable(arc.klass) && arc.motor_vehicle_allowed()) ++usable_car;
  }
  std::printf("  access barred    %d car / %d bike / %d foot arcs\n", barred[0], barred[1],
              barred[2]);
  std::printf("  access private   %d car / %d bike / %d foot arcs\n", priv[0], priv[1], priv[2]);
  std::printf("  driveable arcs a car may use: %d\n", usable_car);
  std::printf("  ferry / toll     %d ferry arcs (%.1f km of crossing), %d tolled arcs\n",
              ferry_arcs, ferry_km, toll_arcs);

  uint32_t only_turns = 0;
  for (uint32_t i = 0; i < graph.restriction_count(); ++i) {
    if (graph.restriction(i).kind == fv::routing::TurnRestrictionKind::kOnly) ++only_turns;
  }
  std::printf("  %u turn restrictions (%u only_*)\n", graph.restriction_count(), only_turns);

  // Connected components, ignoring direction. A extract clipped mid-island
  // legitimately has many; a graph whose *largest* component is a small
  // fraction of it is a build that failed to node something.
  std::vector<uint32_t> parent(graph.node_count());
  for (uint32_t i = 0; i < parent.size(); ++i) parent[i] = i;
  std::function<uint32_t(uint32_t)> find = [&](uint32_t x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };
  for (uint32_t u = 0; u < graph.node_count(); ++u) {
    for (uint32_t a = graph.arc_begin(u); a < graph.arc_end(u); ++a) {
      const uint32_t ru = find(u), rv = find(graph.arc(a).target);
      if (ru != rv) parent[ru] = rv;
    }
  }
  std::vector<uint32_t> size(graph.node_count(), 0);
  for (uint32_t i = 0; i < graph.node_count(); ++i) ++size[find(i)];
  uint32_t components = 0, largest = 0;
  for (uint32_t i = 0; i < graph.node_count(); ++i) {
    if (size[i] == 0) continue;
    ++components;
    largest = std::max(largest, size[i]);
  }
  std::printf("  %u connected components, largest holds %u nodes (%.0f%%)\n", components, largest,
              graph.node_count() ? 100.0 * largest / graph.node_count() : 0.0);
  return 0;
}

void WriteGeoJson(const char* path, const fv::routing::Route& route) {
  std::FILE* f = std::fopen(path, "wb");
  if (f == nullptr) {
    std::fprintf(stderr, "fvgraph: cannot write %s\n", path);
    return;
  }
  std::fprintf(f,
               "{\"type\":\"Feature\",\"properties\":{\"length_m\":%.1f,\"seconds\":%.1f},"
               "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[",
               route.length_m, route.seconds);
  for (size_t i = 0; i < route.geometry.size(); ++i) {
    std::fprintf(f, "%s[%.7f,%.7f]", i ? "," : "", route.geometry[i].lon, route.geometry[i].lat);
  }
  std::fprintf(f, "]}}\n");
  std::fclose(f);
}

int Route(int argc, char** argv) {
  if (argc < 5) return Usage();
  const std::string graph_path = argv[0];
  fv::GeoPoint from{std::atof(argv[1]), std::atof(argv[2])};
  fv::GeoPoint to{std::atof(argv[3]), std::atof(argv[4])};

  fv::routing::RouteOptions options;
  const char* geojson = nullptr;
  std::string rules_path = FV_ROUTE_RULES_FILE;
  std::string profile;
  // The stops in request order. `to` is appended last, so --via may appear
  // anywhere in the argument list and still land between the two ends.
  std::vector<fv::GeoPoint> vias;
  // Held back rather than written straight onto `options`: SelectProfile runs
  // after this loop and would overwrite them with the profile's defaults, so
  // "--profile car --avoid-ferry" would silently take a ferry. Applied below,
  // after the profile, where the query's own answer belongs. A separate flag
  // rather than a sentinel value, so `--toll-penalty 0` and an unparsable
  // number are REFUSED instead of silently reading as "not given".
  double toll = 0.0, ferry = 0.0;
  bool have_toll = false, have_ferry = false;
  // atof gives 0 for garbage, which is not a legal penalty either way.
  auto penalty = [](const char* text, double* out) {
    *out = std::atof(text);
    return *out > 0.0;
  };
  for (int i = 5; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--rules" && i + 1 < argc) {
      rules_path = argv[++i];
    } else if (a == "--via" && i + 2 < argc) {
      const double lat = std::atof(argv[i + 1]);
      const double lon = std::atof(argv[i + 2]);
      i += 2;
      vias.push_back(fv::GeoPoint{lat, lon});
    } else if (a == "--u-turns") {
      options.allow_u_turn_at_stops = true;
    } else if (a == "--profile" && i + 1 < argc) {
      profile = argv[++i];
    } else if (a == "--distance") {
      options.metric = fv::routing::RouteMetric::kDistance;
    } else if (a == "--walk") {
      options.driving = false;
    } else if (a == "--cycle") {
      options.cycle_only = true;
    } else if (a == "--dijkstra") {
      options.bidirectional = false;
    } else if (a == "--ignore-turns") {
      options.honor_turn_restrictions = false;
    } else if (a == "--private-penalty" && i + 1 < argc) {
      options.private_penalty = std::atof(argv[++i]);
    } else if (a == "--avoid-toll") {
      toll = fv::routing::kAvoidExcluded;
      have_toll = true;
    } else if (a == "--avoid-ferry") {
      ferry = fv::routing::kAvoidExcluded;
      have_ferry = true;
    } else if (a == "--toll-penalty" && i + 1 < argc) {
      if (!penalty(argv[++i], &toll)) return Usage();
      have_toll = true;
    } else if (a == "--ferry-penalty" && i + 1 < argc) {
      if (!penalty(argv[++i], &ferry)) return Usage();
      have_ferry = true;
    } else if (a == "--snap" && i + 1 < argc) {
      options.snap_meters = std::atof(argv[++i]);
    } else if (a == "--geojson" && i + 1 < argc) {
      geojson = argv[++i];
    } else {
      return Usage();
    }
  }

  if (!profile.empty()) {
    // A one-shot CLI cannot show a reload, but it loads the rules exactly the
    // way a long-running application does — through the watched file, so a
    // missing or broken rule file falls back to the builtin weights with a
    // warning rather than failing the query.
    fv::routing::RouteRulesFile rules_file(rules_path);
    const std::shared_ptr<const fv::routing::RouteRules> rules = rules_file.rules();
    if (!rules_file.last_error().empty())
      std::fprintf(stderr, "fvgraph: %s — using built-in weights\n",
                   rules_file.last_error().c_str());
    const Status ps = fv::routing::SelectProfile(rules, profile, &options);
    if (!ps.ok()) return Fail(ps);
    std::printf("profile '%s' from %s\n", profile.c_str(), rules->origin().c_str());
  }

  // After the profile, deliberately — see the declaration.
  if (have_toll) options.toll_penalty = toll;
  if (have_ferry) options.ferry_penalty = ferry;

  RoadGraph graph;
  Status s = RoadGraph::Load(graph_path, &graph);
  if (!s.ok()) return Fail(s);

  fv::routing::Router router(graph);
  fv::routing::Route route;
  if (vias.empty()) {
    s = router.Route(from, to, options, &route);
  } else {
    std::vector<fv::GeoPoint> stops;
    stops.push_back(from);
    stops.insert(stops.end(), vias.begin(), vias.end());
    stops.push_back(to);
    s = router.RouteVia(stops, options, &route);
  }
  if (!s.ok()) return Fail(s);
  if (!route.found) {
    if (route.unreachable_leg != fv::routing::Route::kNoLeg) {
      std::printf("no route: stop %u and stop %u are not connected on this network\n",
                  route.unreachable_leg, route.unreachable_leg + 1);
    } else {
      std::printf("no route: the two points are not connected on this network\n");
    }
    return 1;
  }

  std::printf("%.2f km, %.1f min (%lld nodes expanded)\n", route.length_m / 1000.0,
              route.seconds / 60.0, static_cast<long long>(route.nodes_expanded));
  std::printf("snapped %.0f m from the start, %.0f m from the end\n", route.start_offset_m,
              route.end_offset_m);
  if (!vias.empty()) {
    std::printf("through %zu stops", route.stop_nodes.size());
    // Worth saying out loud: the route doubled back at a stop because the
    // stop had no other exit, which usually means it was dropped up a
    // driveway rather than on the road that was meant.
    for (uint32_t stop : route.u_turn_stops) {
      std::printf(", turned round at stop %u (no other way out)", stop);
    }
    std::printf("\n");
  }
  for (const fv::routing::RouteLeg& leg : route.legs) {
    if (leg.length_m < 30.0) continue;  // don't narrate every slip lane
    std::printf("  %6.2f km  %-16s %s\n", leg.length_m / 1000.0, leg.klass.c_str(),
                leg.name.empty() ? "(unnamed)" : leg.name.c_str());
  }
  if (geojson != nullptr) WriteGeoJson(geojson, route);
  return 0;
}

// Prints what a rule file defines — the fastest way to check that an edit
// took, and that a weight says what its author thought it said.
int Profiles(int argc, char** argv) {
  if (argc > 1) return Usage();
  const std::string path = argc == 1 ? argv[0] : std::string(FV_ROUTE_RULES_FILE);
  fv::routing::RouteRulesFile rules_file(path);
  const std::shared_ptr<const fv::routing::RouteRules> rules = rules_file.rules();
  if (!rules_file.last_error().empty())
    std::fprintf(stderr, "fvgraph: %s\n", rules_file.last_error().c_str());
  std::printf("%s\n", rules->origin().c_str());
  for (const fv::routing::RouteProfile& p : rules->profiles()) {
    const char* mode = p.mode == fv::routing::TravelMode::kBicycle   ? "bicycle"
                       : p.mode == fv::routing::TravelMode::kFoot    ? "foot"
                                                                     : "motor_vehicle";
    std::printf("\n%s%s\n  %s\n", p.name.c_str(),
                p.name == rules->default_profile_name() ? "  (default)" : "",
                p.description.c_str());
    if (p.speed_source == fv::routing::SpeedSource::kFixed) {
      std::printf("  mode %s, %.1f km/h flat, metric %s, turn signage %s, private x%.2f\n",
                  mode, p.fixed_kph,
                  p.metric == fv::routing::RouteMetric::kDistance ? "distance" : "time",
                  p.turn_restrictions ? "obeyed" : "ignored", p.private_penalty);
    } else {
      std::printf("  mode %s, posted speeds, metric %s, turn signage %s, private x%.2f\n",
                  mode, p.metric == fv::routing::RouteMetric::kDistance ? "distance" : "time",
                  p.turn_restrictions ? "obeyed" : "ignored", p.private_penalty);
    }
    // "excluded" rather than a number: the two are different answers, and a
    // reader checking an edit took needs to see which one landed.
    auto avoidance = [](double v) {
      if (v == fv::routing::kAvoidExcluded) return std::string("excluded");
      char buf[32];
      std::snprintf(buf, sizeof(buf), "x%.2f", v);
      return std::string(buf);
    };
    std::printf("  toll %s, ferry %s\n", avoidance(p.toll_penalty).c_str(),
                avoidance(p.ferry_penalty).c_str());
    std::printf("  classes:");
    for (size_t i = 0; i < static_cast<size_t>(fv::routing::RoadClass::kCount); ++i) {
      const auto klass = static_cast<fv::routing::RoadClass>(i);
      if (!p.allows(klass)) continue;
      std::printf(" %s=%.2f", fv::routing::RoadClassName(klass), p.weight(klass));
    }
    std::printf("\n");
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return Usage();
  const std::string cmd = argv[1];
  if (cmd == "build") return Build(argc - 2, argv + 2);
  if (cmd == "info") return Info(argc - 2, argv + 2);
  if (cmd == "route") return Route(argc - 2, argv + 2);
  if (cmd == "profiles") return Profiles(argc - 2, argv + 2);
  return Usage();
}
