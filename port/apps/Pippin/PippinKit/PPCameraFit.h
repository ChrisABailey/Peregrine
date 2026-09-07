// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPCameraFit.h — the zoom-out rule, as arithmetic.
//
// On entering GPS mode with a route not fully in view, widen the scale until
// the current position and the route's start are both visible. Once, on
// entry, not per frame: a fit recomputed every frame is a map that breathes
// in and out under a rider.
//
// Pure C++ so the mac tests it. The alternative was a method on
// `PPViewport`, which is Objective-C and therefore testable only on a phone,
// for arithmetic that has no phone in it.
//
// Three decisions a reader would otherwise reverse-engineer from the
// constants:
//
// 1. It widens and never narrows, so entering GPS mode cannot throw away a
//    zoom the rider chose. "Both already in view" and "no route at all" then
//    give the same answer, the current scale, which is why neither is a
//    branch.
//
// 2. It fits a disc, not a box. A bounding box is the obvious reading and the
//    wrong shape, because the chart turns: a box that fits north-up does not
//    fit at 45 degrees, where a turned viewport's box grows to (w+h)/√2 on
//    both axes. A disc of radius |ship - start| about the ship does not care
//    how the chart is turned. The price is that a start due north is fitted
//    as though it might be diagonal, which is one zoom level at worst.
//
// 3. The ship is not at the centre of the screen, and where it sits is the
//    whole of the arithmetic rather than a margin bolted on. MM2's discrete
//    track-up branch calls `DeltaXyTrackUp(W, H, angle, 0.0, 1/3)`, putting
//    the centre a third of the height ahead of the ship, so the ship is drawn
//    at (W/2, 5H/6): five sixths of the screen in front and one sixth behind.
//
//    A route's start is normally behind the rider, which is the tight
//    direction, so the radius that fits is the smallest clearance to any edge
//    — `min(W/2, H/6)`, the sixth rather than the half on a portrait phone.
//    Allowing 0.4 of the smaller side instead puts the start about 10% off
//    the bottom of the screen. The constants below name MM2's fraction, so a
//    change to the anchor moves this in step.

#pragma once

#include <algorithm>
#include <cmath>

#include "fvkit/geo.h"

namespace pippin {

// The surface the fit is against, in the units `PPViewport` holds: rendered
// pixels and the physical pitch of one.
struct CameraFitSurface {
  int pixel_width = 0;
  int pixel_height = 0;
  double mm_per_pixel = 0.0;
};

// The WGS-84 mean radius, the same metre `port/Routing` measures arc lengths
// in and `road_snap.h` projects onto segments with.
constexpr double kFitEarthRadiusM = 6371008.8;

// MM2's `anchor_frac_y` in the discrete track-up branch: the map centre is
// this fraction of the height ahead of the ship (`DeltaXyTrackUp`).
constexpr double kTrackUpAheadFraction = 1.0 / 3.0;

// A position fitted exactly to the edge is drawn half off it, because a
// waypoint is a diamond around the point rather than the point itself. A
// tenth is about two diamonds' worth at this pitch.
constexpr double kFitEdgeMargin = 1.1;

// Metres between two positions on a tangent plane through `a`. Exact to well
// under a metre over the few kilometres a bundled pack spans, which is all a
// choice of scale needs: this is a view fit, not a geodesic.
inline double FitDistanceMeters(const fv::GeoPoint& a, const fv::GeoPoint& b) {
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  constexpr double kMetersPerDegLat = kFitEarthRadiusM * kDegToRad;
  const double cos_lat = std::cos(a.lat * kDegToRad);
  double dlon = b.lon - a.lon;
  while (dlon > 180.0) dlon -= 360.0;
  while (dlon < -180.0) dlon += 360.0;
  const double dx = dlon * kMetersPerDegLat * (cos_lat > 1e-6 ? cos_lat : 1e-6);
  const double dy = (b.lat - a.lat) * kMetersPerDegLat;
  return std::sqrt(dx * dx + dy * dy);
}

// The scale denominator to enter GPS mode at, given the ship and the one other
// position that has to stay in view. The caller passes the route's start
// rather than the whole route, because a rider halfway round is not asking to
// see the end.
//
// Returns `current_denominator` unchanged when there is nothing to do: the two
// are already in view, the surface is not measured yet, or an input is not a
// number. It never returns a smaller denominator than it was given.
inline double ScaleToShow(const fv::GeoPoint& ship, const fv::GeoPoint& other,
                          const CameraFitSurface& surface,
                          double current_denominator) {
  if (!(current_denominator > 0.0) || !std::isfinite(current_denominator)) {
    return current_denominator;
  }
  if (surface.pixel_width <= 0 || surface.pixel_height <= 0 ||
      !(surface.mm_per_pixel > 0.0)) {
    return current_denominator;
  }
  if (!std::isfinite(ship.lat) || !std::isfinite(ship.lon) ||
      !std::isfinite(other.lat) || !std::isfinite(other.lon)) {
    return current_denominator;
  }

  const double radius_m = FitDistanceMeters(ship, other);
  if (!std::isfinite(radius_m) || radius_m <= 0.0) return current_denominator;

  // How much screen the ship has around it at its narrowest, in pixels: half
  // the width to either side, and a sixth of the height behind.
  const double half_width_px = 0.5 * surface.pixel_width;
  const double behind_px = (0.5 - kTrackUpAheadFraction) * surface.pixel_height;
  const double clearance_px = std::min(half_width_px, behind_px);
  const double clearance_m = clearance_px * surface.mm_per_pixel / 1000.0;
  if (!(clearance_m > 0.0)) return current_denominator;

  // A denominator is ground metres per screen metre, so this is the whole
  // conversion: the ground the disc needs over the screen it must land in.
  const double needed = (radius_m * kFitEdgeMargin) / clearance_m;
  if (!std::isfinite(needed)) return current_denominator;
  return std::max(current_denominator, needed);
}

// ---------------------------------------------------------------------------
// Framing a search result
// ---------------------------------------------------------------------------

// The scale that puts `bounds` on the screen, given that the map is about to
// be centred on it and pushed `top_bias` of the height up so a sheet can sit
// over the bottom half.
//
// Four differences from `ScaleToShow` above, all of them the requirement
// rather than an inconsistency.
//
// 1. It narrows as well as widening, because this is the answer to "show me
//    that". `ScaleToShow` may never throw away a zoom the rider chose, since
//    it fires on a button press about something else.
//
// 2. A degenerate box keeps the current scale. A point's honest bounds has
//    `ll == ur`, and no scale fits a dimensionless thing; fitting one would
//    be a division by zero wearing a zoom limit's clothes.
//
// 3. `floor_denominator` is a floor, not a limit. A forty-metre cul-de-sac
//    fitted to a phone is arithmetically 1:300 and useless to look at. The
//    pack's `search.frame_min_scale` is the scale a rider reads a street at.
//    Fitting out is unbounded here; the viewport's zoom limits are the
//    backstop, as for every other camera move.
//
// 4. It fits the diagonal as a disc, for decision 2 above: the chart turns
//    and a disc does not care. The price is that a road running due east is
//    fitted as though it might run north-east.
inline double ScaleToFitBounds(const fv::GeoRect& bounds,
                               const CameraFitSurface& surface,
                               double current_denominator,
                               double floor_denominator, double top_bias) {
  if (!(current_denominator > 0.0) || !std::isfinite(current_denominator)) {
    return current_denominator;
  }
  if (surface.pixel_width <= 0 || surface.pixel_height <= 0 ||
      !(surface.mm_per_pixel > 0.0)) {
    return current_denominator;
  }
  const fv::GeoPoint ll = bounds.ll;
  const fv::GeoPoint ur = bounds.ur;
  if (!std::isfinite(ll.lat) || !std::isfinite(ll.lon) ||
      !std::isfinite(ur.lat) || !std::isfinite(ur.lon)) {
    return current_denominator;
  }

  // Half the diagonal: the radius of the disc holding the box whichever way
  // the chart is turned. Zero for a point.
  const double radius_m = 0.5 * FitDistanceMeters(ll, ur);
  if (!std::isfinite(radius_m) || radius_m <= 0.0) return current_denominator;

  // The result is centred, so the clearance is half the screen, less whatever
  // the bias pushes it off centre, which costs height only.
  const double bias = std::isfinite(top_bias) ? std::fabs(top_bias) : 0.0;
  const double half_width_px = 0.5 * surface.pixel_width;
  const double half_height_px =
      std::max(0.0, 0.5 - std::min(bias, 0.45)) * surface.pixel_height;
  const double clearance_px = std::min(half_width_px, half_height_px);
  const double clearance_m = clearance_px * surface.mm_per_pixel / 1000.0;
  if (!(clearance_m > 0.0)) return current_denominator;

  const double needed = (radius_m * kFitEdgeMargin) / clearance_m;
  if (!std::isfinite(needed) || !(needed > 0.0)) return current_denominator;
  if (floor_denominator > 0.0 && std::isfinite(floor_denominator)) {
    return std::max(needed, floor_denominator);
  }
  return needed;
}

}  // namespace pippin
