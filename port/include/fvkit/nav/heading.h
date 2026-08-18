// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/nav/heading.h — which way the ship is pointing (nav plan MM1).
//
// Ported from MovingMapOverlay/gps_draw.cpp `C_gps_trail::
// get_current_heading()` (~406): use the fix's own true heading when it is
// valid, otherwise DERIVE one from the last two distinct positions, otherwise
// assume north. The alternative-tracking-center branch of the original is not
// ported (that is the CDI/bullseye feature set, out of scope per the plan).
//
// WHY THE DERIVATION IS IN SCREEN SPACE. The original scales the two deltas
// by the map's degrees-per-pixel before taking the angle, so the arrow points
// the way the track LOOKS on the chart. That matters on an equal-arc map,
// where a degree of longitude and a degree of latitude are different numbers
// of pixels: a true bearing of 045 does not draw at 45 degrees on the screen,
// and an ownship symbol drawn at the true bearing would visibly disagree with
// the line of its own travel. Hand the resolver the projection's
// degrees-per-pixel (SetDegPerPixel) and it reproduces FalconView exactly;
// leave it unset and it uses cos(lat), which is the local-tangent-plane
// bearing — right for geography, and the honest default for a caller that has
// no map.
//
// THE ONE DELIBERATE FIX (a deviation from bit-faithful, and the reason it is
// not one). The original computes
//
//     north_up_angle = RAD_TO_DEG(atan2(delta_x, delta_y));
//     if (delta_y < 0.0)      north_up_angle += 180.0;
//     else if (delta_x < 0.0) north_up_angle += 360.0;
//
// That quadrant fix-up is the correct one for `atan` (range -90..+90), and it
// is applied to `atan2`, which has already resolved the quadrant. The two
// agree on the northern half and on due east/west; where the ship is going
// SOUTH the +180 lands on a quadrant that was already right and the answer
// comes out 180 degrees OPPOSED — a ship tracking southeast (135) is reported
// as 315 and drawn flying backwards. It is not a numeric quirk to preserve
// — it is a sign error that a moving-map user would see the moment they
// turned south — so this port computes the bearing once, correctly, with
// atan2 and no fix-up. Pinned by the eight-compass-point test in
// nav_heading_test.cpp, whose southern half fails against the original
// formula.

#ifndef FVKIT_NAV_HEADING_H_
#define FVKIT_NAV_HEADING_H_

#include <cstddef>
#include <deque>

#include "fvkit/geo.h"
#include "fvkit/nav/position.h"

namespace fv {

// What the resolver decided, and where it came from. `known` false still
// carries degrees == 0.0 (FalconView's "assume north"), so a caller that
// ignores the flag draws exactly what FalconView drew.
struct ResolvedHeading {
  double degrees = 0.0;
  bool known = false;
  // True when the fix reported it; false when it was derived from movement.
  // MM4 wants this: a derived heading is noise at a standstill, and a symbol
  // that spins on the spot is the visible form of that.
  bool reported = false;
};

// Resolves a heading per fix, keeping the small position history the
// derivation needs. THIS IS THE WHOLE OF THE "TRAIL" THE MOVING MAP KEEPS
// (nav plan: no breadcrumbs) — a handful of recent positions, kept only so
// that a stationary ship, whose newest fixes all repeat one point, can still
// look far enough back to find the direction it was last going.
class HeadingResolver {
 public:
  // `history` is how many recent positions to keep. The original walks its
  // whole icon list back to the last distinct point; a moving map that has
  // been sitting still for ten minutes should not therefore claim the
  // heading it had ten minutes ago, so this port bounds the walk.
  explicit HeadingResolver(std::size_t history = 8);

  // The map's degrees per pixel (both positive). Set it from the projection
  // whenever the scale changes — FalconView does exactly this in
  // `handle_mapscale_changes`. Values <= 0 clear it, restoring the cos(lat)
  // default.
  void SetDegPerPixel(double deg_per_pixel_lat, double deg_per_pixel_lon);
  bool has_deg_per_pixel() const { return dpp_lat_ > 0.0 && dpp_lon_ > 0.0; }

  // Feeds a fix and returns the heading to draw it at. A fix with no position
  // does not enter the history; a fix repeating the previous position does
  // not either (it is not a distinct point, and a zero delta has no angle).
  ResolvedHeading Update(const PositionFix& fix);

  // The last value Update returned.
  ResolvedHeading current() const { return current_; }

  // Forgets the history. A source restart must call it, or the first fix of
  // the new run derives its heading from where the last run ended.
  void Reset();

  std::size_t history_size() const { return points_.size(); }

 private:
  double dpp_lat_ = 0.0;
  double dpp_lon_ = 0.0;
  std::size_t capacity_;
  std::deque<GeoPoint> points_;  // oldest first, newest last, all distinct
  ResolvedHeading current_;
};

// The derivation on its own, exposed because it is worth testing and reusing
// without a resolver: the screen-space bearing from `from` to `to`, degrees
// clockwise from north, in [0, 360). `deg_per_pixel_lat`/`_lon` <= 0 select
// the cos(lat) default described at the top of this file. Returns false when
// the two points are identical, which has no bearing at all.
bool ScreenBearingDeg(const GeoPoint& from, const GeoPoint& to,
                      double deg_per_pixel_lat, double deg_per_pixel_lon,
                      double* bearing_deg);

}  // namespace fv

#endif  // FVKIT_NAV_HEADING_H_
