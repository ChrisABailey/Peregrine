// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/nav/road_snap.h — putting the ship on the road (nav plan MM5).
//
// It sits between the feed and everything that consumes a fix:
//
//   IPositionSource -> FixQueue -> [RoadSnapper] -> HeadingResolver -> camera
//                                                                   -> symbol
//
// A consumer-grade receiver is good to a handful of metres and a chart is
// drawn to one, so an honest fix wanders off the road it is on, jitters at a
// standstill and reports a heading made of noise. Snapping is the standard
// answer: choose the road the ship is most likely on and report the position
// projected onto it.
//
// FIVE THINGS ARE DECIDED HERE, AND FOUR OF THEM ARE THE PLAN'S.
//
// 1. THE RAW FIX IS NEVER DESTROYED. `SnappedFix` carries it whole, beside the
//    snapped position, and `Applied()` is an explicit call. A snapper that
//    overwrote the fix would leave a shell no way to draw where the receiver
//    actually said the ship was — which is the first thing anybody asks for
//    when a snap looks wrong — and no way to fall back when confidence is low.
//
// 2. EVERY SCORE TERM IS IN METRES, so a setting reads as a sentence. The
//    heading penalty is "how far out of my way I will look to find a road
//    pointing the way I am going"; the hysteresis bonus is "how much closer
//    another road has to be before I will leave the one I am on". Nothing is a
//    dimensionless weight nobody can tune by eye.
//
// 3. HYSTERESIS IS THE POINT, NOT AN OPTIMISATION. Nearest-edge alone flaps:
//    at a junction two arcs meet at the same point, and between two parallel
//    carriageways (or a road and the cycleway beside it — Kiawah is full of
//    those) the nearest one changes with the noise, several times a minute.
//    The previous arc keeps a bonus, and an arc CONNECTED to it keeps a
//    smaller one, so the road being driven wins ties and a genuine turn still
//    switches.
//
// 4. BELOW A WALKING PACE THE SNAP IS HELD. A stationary receiver's fixes
//    scatter over tens of metres and its derived heading is pure noise, so at
//    low speed the arc is not reconsidered at all: the point is re-projected
//    onto the arc it was already on, and the heading term is not applied. A
//    ship stopped at a junction otherwise walks up and down every road that
//    meets there.
//
// 5. THE ROAD NETWORK IS BEHIND AN INTERFACE, and that is not the plan's — it
//    is MM1's rule kept. `port/Routing`'s RoadGraph is the network the port
//    has, but fvkit does not link Routing (a moving map should not oblige an
//    application to carry a router), so this is the D6 adapter seam: fvkit
//    declares what it needs and `fv::routing::RoadGraphNetwork`
//    (port/Routing/fv_road_network.h) supplies it. NO NETWORK IS A SUPPORTED
//    STATE — the feature is simply off, every fix passes through unsnapped,
//    which is what a shell with no `.fvroad` on disk gets.
//
// ONE STATED LIMITATION, IN THE FAMILY OF MM1's SCREEN-VS-TRUE SPLIT. A
// candidate's bearing is a TRUE bearing, and the heading it is compared with
// may be a screen angle (heading.h derives one in screen space, deliberately).
// The two differ by the projection's aspect — about 5 degrees off Charleston,
// 18 at 60 degrees north — and the term is a soft ranker rather than a
// decision, so this is documented rather than reconciled: the distinction that
// actually matters, which WAY along the road, is a 180-degree one and survives
// any aspect. Converting would mean handing the snapper the projection.

#ifndef FVKIT_NAV_ROAD_SNAP_H_
#define FVKIT_NAV_ROAD_SNAP_H_

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/nav/position.h"

namespace fv {

// "no arc" at this seam. A network's own ids are opaque here, so this is the
// one value it may not use.
constexpr uint64_t kNoRoadArc = ~static_cast<uint64_t>(0);

// ---------------------------------------------------------------------------
// The geometry every network implementation needs
// ---------------------------------------------------------------------------

// Where a point falls on one segment of a road.
struct SegmentProjection {
  GeoPoint point;           // the projection, clamped to the segment's ends
  double distance_m = 0.0;  // from the query point to `point`
  double along_m = 0.0;     // from the segment's start to `point`
  double length_m = 0.0;    // the whole segment
  double bearing_deg = 0.0; // start -> end, degrees clockwise from true north
};

// Projects `p` onto the segment [a, b] on a local tangent plane through `p`
// (metres east/north, cos(lat) for the meridians), which is exact to well
// under a millimetre over the tens of metres a snap radius covers. Returns
// false only for a degenerate segment (a == b), which has no bearing;
// `distance_m` is still filled in that case, so a caller may use it.
//
// The metre is the SAME metre `port/Routing` measures arc lengths in (WGS-84
// mean radius, 6371008.8 m). A snapper whose distances disagreed with the
// graph's own would put its radius somewhere other than where it says.
//
// INLINE ON PURPOSE, and it is the one thing in fvkit that is inline for a
// linkage reason rather than a speed one: a network adapter lives in the
// library that owns the roads (port/Routing does), and obliging it to LINK
// fvkit to reach one geometry function would drag the whole map engine —
// SQLite, libpng, every decoder — into a graph-building CLI. The alternative
// was for each adapter to carry its own copy of the projection, and two
// copies of "what a metre is" is exactly what the comment above forbids.
inline bool ProjectOntoSegment(const GeoPoint& p, const GeoPoint& a,
                               const GeoPoint& b, SegmentProjection* out) {
  constexpr double kSnapDegToRad = 3.14159265358979323846 / 180.0;
  constexpr double kSnapMetersPerDegLat = 6371008.8 * kSnapDegToRad;
  if (out == nullptr) return false;

  // The tangent plane is taken at the QUERY point rather than at the segment's
  // midpoint, which is what keeps the returned distance the distance from the
  // fix — the number every threshold in this file is expressed in.
  const double cos_lat = std::cos(p.lat * kSnapDegToRad);
  const double mx = kSnapMetersPerDegLat * (cos_lat > 1e-6 ? cos_lat : 1e-6);
  const double ax = NormalizeLon(a.lon - p.lon) * mx;
  const double ay = (a.lat - p.lat) * kSnapMetersPerDegLat;
  const double bx = NormalizeLon(b.lon - p.lon) * mx;
  const double by = (b.lat - p.lat) * kSnapMetersPerDegLat;
  const double dx = bx - ax, dy = by - ay;
  const double len2 = dx * dx + dy * dy;

  double t = 0.0;
  if (len2 > 0.0) {
    // The query is the origin, so the projection parameter is -A.(B-A)/|B-A|^2,
    // CLAMPED to the segment: a road ends, and a fix past its end belongs to
    // whatever comes next rather than to this one extended.
    t = -(ax * dx + ay * dy) / len2;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
  }
  const double px = ax + t * dx;
  const double py = ay + t * dy;

  out->distance_m = std::sqrt(px * px + py * py);
  out->length_m = std::sqrt(len2);
  out->along_m = t * out->length_m;
  out->point.lat = p.lat + py / kSnapMetersPerDegLat;
  out->point.lon = NormalizeLon(p.lon + px / mx);
  if (len2 <= 0.0) {
    // A degenerate segment (a repeated vertex, and OSM has plenty) has a
    // distance but no direction. A caller walking a polyline takes the bearing
    // from the neighbouring segment, which is why this is false rather than an
    // invented 0.
    out->bearing_deg = 0.0;
    return false;
  }
  out->bearing_deg = NormalizeHeadingDeg(std::atan2(dx, dy) / kSnapDegToRad);
  return true;
}

// ---------------------------------------------------------------------------
// The network seam
// ---------------------------------------------------------------------------

// One road offered to the snapper, already projected. An implementation
// returns at most ONE of these per undirected road: the two directed arcs of a
// two-way street are the same piece of tarmac, and offering both would make
// every road ambiguous with itself.
struct RoadCandidate {
  // The network's own id for this road, opaque here and stable across
  // queries — the snapper's whole memory is this number.
  uint64_t arc = kNoRoadArc;

  // The road's two ends, opaque ids again. They exist for one purpose: two
  // arcs sharing an end are CONNECTED, which is what tells a turn at a
  // junction apart from a jump to the road on the other side of the block.
  uint64_t from_node = 0;
  uint64_t to_node = 0;

  GeoPoint point;            // the query projected onto this road
  double distance_m = 0.0;   // from the query to `point`
  double along_m = 0.0;      // from the road's start to `point`
  double length_m = 0.0;     // the whole road
  double bearing_deg = 0.0;  // direction of travel at `point`, true

  // A one-way road can only be travelled in `bearing_deg`'s direction, so a
  // fix moving the other way along it is being told something. A two-way one
  // is aligned on its AXIS and the direction of travel is chosen from the
  // heading.
  bool one_way = false;

  std::string name;  // "" when the road is unnamed; for a status readout
};

// What the snapper asks the world. Held as shared_ptr per D1: the application
// owns the graph the network reads and the snapper is one of several things
// that may hold it.
class IRoadNetwork {
 public:
  virtual ~IRoadNetwork() = default;

  // Every road whose closest approach to `p` is within `radius_m`, projected.
  // APPENDS to `out` and does not clear it. Order is not defined — the snapper
  // scores them all. An implementation is free to return nothing at all (off
  // the edge of its data), which is not an error.
  virtual void QueryNear(const GeoPoint& p, double radius_m,
                         std::vector<RoadCandidate>* out) const = 0;

  // The network's coverage, for a shell that wants to say "no road data here"
  // rather than "no snap". A default-constructed rect means "unknown".
  virtual GeoRect bounds() const { return GeoRect{}; }
};

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

// Defaults are for a car on a consumer receiver. Every distance is metres.
struct RoadSnapSettings {
  // The search radius comes from the fix's own quality: radius = hdop * scale,
  // floored and capped. HDOP is a unitless multiplier on the receiver's own
  // error, so this is "how many metres of error one unit of HDOP is worth" —
  // 5 m is the usual rule of thumb for a bare GPS receiver.
  double hdop_scale = 5.0;
  double min_radius_m = 15.0;
  double max_radius_m = 60.0;
  // A fix with no HDOP at all (an NMEA GLL, a scripted track). Not a floor and
  // not a cap: the honest "I have no quality number" radius.
  double default_radius_m = 25.0;

  // Full penalty for a road pointing the wrong way; nothing for one pointing
  // exactly along the heading, and half of it at right angles. Read it as: a
  // road at right angles has to be this many metres closer to win.
  double heading_penalty_m = 20.0;

  // The hysteresis pair. `stay` is the bonus the arc already snapped to keeps;
  // `connected` is the smaller one an arc sharing an end with it keeps, which
  // is what makes a turn at a junction cheaper than a jump across the block.
  double stay_bonus_m = 10.0;
  double connected_bonus_m = 5.0;

  // Below this ground speed the snap is HELD (see the header). 1 m/s is a
  // slow walk; a vessel or a car stopped at lights is well under it.
  double hold_speed_mps = 1.0;

  // How much better the best score has to be than the runner-up before the
  // choice reads as certain. Under it, `confidence` is scaled down — this is
  // the number that says "two parallel roads, could be either".
  double ambiguity_m = 10.0;
};

// ---------------------------------------------------------------------------
// The answer
// ---------------------------------------------------------------------------

struct SnappedFix {
  // Exactly what the receiver said. Never modified, always present.
  PositionFix raw;

  bool snapped = false;
  GeoPoint position;  // the snapped position; == raw.position() when !snapped

  uint64_t arc = kNoRoadArc;
  std::string road_name;

  // The direction of travel along the road, true. `has_bearing` is false when
  // the road's direction could not be resolved — a two-way road with no
  // heading to choose from — or when the ship is not moving, because the
  // heading of a stationary ship is not the road's.
  double bearing_deg = 0.0;
  bool has_bearing = false;

  // How far the raw fix was from the road, and how sure the choice is (0..1:
  // 1 is on the road with no other road anywhere near, 0 is at the rim of the
  // search radius or an even split between two candidates).
  double offset_m = 0.0;
  double confidence = 0.0;

  // The arc was held rather than chosen, because the ship is below
  // `hold_speed_mps`.
  bool held = false;

  // How many roads were in range. A debug number, and the one that explains a
  // low confidence: 1 is unambiguous, 5 in a car park is not.
  int candidates = 0;

  // The fix to consume: `raw` with the position replaced by the snapped one
  // and, when `has_bearing`, the true heading replaced by the road's. That
  // second half is deliberate — a road bearing is a far better heading than
  // one derived from two noisy positions, and setting it as REPORTED is what
  // makes HeadingResolver prefer it (heading.h resolves a reported heading
  // first). Returns `raw` unchanged when !snapped.
  PositionFix Applied() const;
};

// ---------------------------------------------------------------------------
// The snapper
// ---------------------------------------------------------------------------

// Feed it every fix, in order. It keeps ONE piece of state — the road the last
// fix was snapped to — which is the whole of the hysteresis and the whole of
// the hold. Not thread-safe, and it does not need to be: it lives on the
// consumer's side of MM1's FixQueue.
class RoadSnapper {
 public:
  RoadSnapper() = default;
  explicit RoadSnapper(std::shared_ptr<const IRoadNetwork> network)
      : network_(std::move(network)) {}

  // Null turns snapping OFF (every fix passes through unsnapped) and forgets
  // the previous road — a new network's ids mean nothing to the old one's.
  void SetNetwork(std::shared_ptr<const IRoadNetwork> network);
  const std::shared_ptr<const IRoadNetwork>& network() const { return network_; }
  bool enabled() const { return network_ != nullptr; }

  void SetSettings(const RoadSnapSettings& s) { settings_ = s; }
  const RoadSnapSettings& settings() const { return settings_; }

  // Snap one fix.
  //
  // `prior_heading_deg` is the heading as of the PREVIOUS fix, and being one
  // fix stale is right rather than merely tolerable: it describes the way the
  // ship was going as it arrived here, which is what says which road it is on.
  // The fix's OWN true heading wins when it reports one. Pass has_prior false
  // when there is no heading at all (the first fix), and the alignment term is
  // simply not applied.
  SnappedFix Snap(const PositionFix& fix, double prior_heading_deg,
                  bool has_prior_heading);

  // The fix's own heading only.
  SnappedFix Snap(const PositionFix& fix) { return Snap(fix, 0.0, false); }

  // Forget the road. A source restart or a feed change must call it, for
  // heading.h's reason: the ship is somewhere else now, and holding onto the
  // last run's arc would drag the first fix of this one onto it.
  void Reset();

  const SnappedFix& last() const { return last_; }

  // The candidates the last Snap considered, scored, best first. Kept because
  // "why did it choose that road?" is otherwise unanswerable, and because a
  // shell that wants to draw them can.
  const std::vector<RoadCandidate>& last_candidates() const { return scratch_; }

 private:
  std::shared_ptr<const IRoadNetwork> network_;
  RoadSnapSettings settings_;

  bool have_prev_ = false;
  uint64_t prev_arc_ = kNoRoadArc;
  uint64_t prev_from_ = 0;
  uint64_t prev_to_ = 0;

  SnappedFix last_;
  std::vector<RoadCandidate> scratch_;  // reused per call
};

}  // namespace fv

#endif  // FVKIT_NAV_ROAD_SNAP_H_
