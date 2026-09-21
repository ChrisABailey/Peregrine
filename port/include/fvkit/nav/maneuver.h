// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/nav/maneuver.h — the turn list a route implies (guidance plan GD1).
//
// FalconView has no maneuvers to port and there is no third-party router to
// ask, so this derives them: a route is a drawn line plus a list of legs, each
// leg a run of arcs sharing a road name, and a maneuver is a leg boundary that
// is actually a turn.
//
// TWO THINGS DECIDE WHAT A MANEUVER IS.
//
// 1. A LEG BOUNDARY IS A CANDIDATE, NOT A TURN. Road names change mid-block
//    all the time — a street becomes an avenue at a county line, a lane
//    becomes a track where the surface changes — and none of that is
//    something to tell a rider. A boundary whose turn angle is inside
//    `straight_deg` is dropped. The rider is told about corners, not about
//    the cartography.
//
// 2. THE ANGLE IS MEASURED OVER A WINDOW, NOT OVER THE TWO ADJACENT POINTS.
//    OSM shape points around a bend can be a metre apart, so the bearing into
//    and out of a junction taken from the immediate neighbours is mostly
//    receiver-grade noise about the road's curvature. The bearings are taken
//    from `angle_window_m` back and forward instead, clamped so the window
//    never reaches past the neighbouring candidates — a window that ran
//    through the next corner would average the two into one wrong angle.
//
// Angles are SIGNED and in degrees, positive to the right, in (-180, +180].
// Distances are metres along the route from its start (contract D2/D4), and
// they are the PORT'S metre — the WGS-84 mean-radius great circle that
// port/Routing measures arc lengths in. Guidance counts these distances down
// against its own projection onto the same line, so a second definition of a
// metre here would show up as a countdown that never reaches zero.
//
// THE INPUT IS NOT `fv::routing::Route`, deliberately. fvkit does not link
// port/Routing (stated in both modules' CMakeLists) so this takes the shape a
// route has — the line and the leg boundaries on it — and `port/RouteKit`
// fills it from a real route. Same D6 seam as nav/road_snap.h, and it is also
// what lets the tests here build a junction by hand.

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
  // departing or arriving there, not turning.
  double end_margin_m = 20.0;
};

// One instruction.
struct Maneuver {
  GeoPoint at;                 // the junction itself
  double distance_m = 0.0;     // along the route from its start
  double turn_deg = 0.0;       // signed, right positive; 0 on depart/arrive
  ManeuverType type = ManeuverType::kStraight;
  std::string road;            // the road being JOINED ("" when unnamed)
  std::string klass;           // that road's OSM highway value
  uint32_t geometry_index = 0; // where `at` is in RouteShape::geometry
};

// A route as this file needs it: the drawn line, and where each named run
// starts on it.
struct RouteShape {
  struct Leg {
    uint32_t geometry_begin = 0;  // index into `geometry`
    std::string name;
    std::string klass;
  };

  std::vector<GeoPoint> geometry;
  std::vector<Leg> legs;  // ordered, `geometry_begin` non-decreasing, [0] == 0
};

// The turn list, in travel order, always beginning with kDepart and ending
// with kArrive when the shape has a line at all.
//
// A shape with fewer than two points yields nothing: there is no route to
// give instructions about, and a single point has no direction to depart in.
std::vector<Maneuver> BuildManeuvers(const RouteShape& shape,
                                     const ManeuverSettings& settings = {});

// The instruction's short name, for a test failure or a log line. Not for a
// user — the phone's banner draws a glyph and a road name (GD3), and any
// wording a rider reads is localizable and belongs in the app.
const char* ManeuverTypeName(ManeuverType type);

}  // namespace nav
}  // namespace fv

#endif  // FVKIT_NAV_MANEUVER_H_
