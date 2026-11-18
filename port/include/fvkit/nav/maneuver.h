// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file fvkit/nav/maneuver.h
 * The turn list a route implies.
 *
 * Candidates are leg boundaries (a change of road name or class) and graph
 * junctions inside a leg. A leg boundary is announced when it turns at least
 * `straight_deg`; a junction only when the rider could go another way there
 * (`BuildManeuvers`). Turn angles are measured over `angle_window_m` either
 * side of the point, not over the adjacent shape points, and the window stops
 * at the neighbouring leg boundaries.
 *
 * Angles are signed degrees, right positive, in (-180, +180]. Distances are
 * metres along the route on the WGS-84 mean-radius sphere, the metre
 * port/Routing measures arcs in, so Guidance's countdown reaches zero.
 *
 * The input is `RouteShape`, not `routing::Route`: fvkit does not link
 * port/Routing, and RouteKit fills the shape from a routed route.
 */

#ifndef FVKIT_NAV_MANEUVER_H_
#define FVKIT_NAV_MANEUVER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace nav {

// What the rider is told to do. The bands between the lefts and rights are
// ManeuverSettings'; `kDepart` and `kArrive` are the ends of the route and
// carry no angle.
enum class ManeuverType {
  kDepart,
  kStraight,  // a boundary kept for its name, never produced by BuildManeuvers
  kSlightLeft,
  kLeft,
  kSharpLeft,
  kSlightRight,
  kRight,
  kSharpRight,
  kUTurn,
  kArrive,
};

// The angle bands, and the window the angle is measured over. All four
// thresholds are magnitudes in degrees and must be increasing.
struct ManeuverSettings {
  // Below this a leg boundary is not a turn at all (rule 1 above).
  double straight_deg = 20.0;
  // Below this it is a slight turn, below `sharp_deg` an ordinary one.
  double slight_deg = 45.0;
  double sharp_deg = 110.0;
  // At or beyond this the route doubles back: a u-turn, which has no side.
  double u_turn_deg = 160.0;

  // How far either side of the junction the bearings are taken over (rule 2).
  // 15 m is longer than the shape-point spacing of a surveyed bend and shorter
  // than a residential block.
  double angle_window_m = 15.0;

  // A corner closer than this to either end of the route is dropped. Both ends
  // of a route are a SNAP onto a road, often mid-block, and the first or last
  // few metres are then the stub joining the rider's own position to it: on
  // the Kiawah fixture the last named way is reached half a metre before the
  // destination, which is a "turn left" nobody can act on. The rider is
  // departing or arriving there, not turning. kDepart names the last road
  // joined inside the margin, since that is the road the rider sets off on.
  double end_margin_m = 20.0;
};

/// One instruction.
struct Maneuver {
  GeoPoint at;                 // the junction itself
  double distance_m = 0.0;     // along the route from its start
  double turn_deg = 0.0;       // signed, right positive; 0 on depart/arrive
  ManeuverType type = ManeuverType::kStraight;
  std::string road;            // the road being JOINED ("" when unnamed)
  std::string klass;           // that road's OSM highway value
  uint32_t geometry_index = 0; // where `at` is in RouteShape::geometry
  /// True when the road joined has the name of the road left (both unnamed
  /// counts), so the instruction reads "left to stay on NAME".
  bool continues = false;
};

// A route as this file needs it: the drawn line, and where each named run
// starts on it.
struct RouteShape {
  struct Leg {
    uint32_t geometry_begin = 0;  // index into `geometry`
    std::string name;
    std::string klass;
  };

  /// A graph junction on the line and the other ways a rider could leave it.
  struct Junction {
    uint32_t geometry_index = 0;  // index into `geometry`
    /// True bearings, degrees, of each other usable exit, measured about
    /// `angle_window_m` out along it. The route's own exit and the way back
    /// along the route are not listed.
    std::vector<double> exit_bearings_deg;
  };

  std::vector<GeoPoint> geometry;
  std::vector<Leg> legs;  // ordered, `geometry_begin` non-decreasing, [0] == 0
  /// Ordered by `geometry_index`. Empty when the caller has no graph, and
  /// then only leg boundaries are candidates.
  std::vector<Junction> junctions;
};

/**
 * The turn list, in travel order, beginning with kDepart and ending with
 * kArrive. A shape with fewer than two points yields nothing.
 *
 * A junction inside a leg is announced when the route turns `slight_deg` or
 * more and another exit turns less than `sharp_deg`, or turns `straight_deg`
 * or more and another exit is straighter than the route's. A junction within
 * `angle_window_m` of an announced leg boundary merges into it; of two such
 * junctions the larger turn is kept.
 */
std::vector<Maneuver> BuildManeuvers(const RouteShape& shape,
                                     const ManeuverSettings& settings = {});

// The instruction's short name, for a test failure or a log line. Not for a
// user — the phone's banner draws a glyph and a road name (GD3), and any
// wording a rider reads is localizable and belongs in the app.
const char* ManeuverTypeName(ManeuverType type);

}  // namespace nav
}  // namespace fv

#endif  // FVKIT_NAV_MANEUVER_H_
