// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/nav/heading.h"

#include <cmath>

namespace fv {
namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

}  // namespace

bool ScreenBearingDeg(const GeoPoint& from, const GeoPoint& to,
                      double deg_per_pixel_lat, double deg_per_pixel_lon,
                      double* bearing_deg) {
  double delta_lat = to.lat - from.lat;
  double delta_lon = to.lon - from.lon;
  // The original's antimeridian fix-up, unchanged: a step across +/-180 is a
  // small one, not a trip round the world.
  if (delta_lon < -180.0) {
    delta_lon += 360.0;
  } else if (delta_lon > 180.0) {
    delta_lon -= 360.0;
  }

  double dx = 0.0;
  double dy = 0.0;
  if (deg_per_pixel_lat > 0.0 && deg_per_pixel_lon > 0.0) {
    // FalconView's own scaling: degrees into pixels of the map on screen.
    dx = delta_lon / deg_per_pixel_lon;
    dy = delta_lat / deg_per_pixel_lat;
  } else {
    // No map to ask: compress longitude by cos(lat) at the midpoint, which is
    // the local tangent plane and so the true bearing for a short step.
    const double mid_lat = 0.5 * (from.lat + to.lat);
    dx = delta_lon * std::cos(mid_lat * kDegToRad);
    dy = delta_lat;
  }

  if (dx == 0.0 && dy == 0.0) return false;

  // atan2(dx, dy) is already the clockwise-from-north angle in (-180, +180].
  // The original adds a further quadrant correction here, which belongs to
  // atan and reflects the southern half when applied to atan2 — see the
  // header for why that one is fixed rather than preserved.
  const double angle = std::atan2(dx, dy) * kRadToDeg;
  if (bearing_deg != nullptr) *bearing_deg = NormalizeHeadingDeg(angle);
  return true;
}

HeadingResolver::HeadingResolver(std::size_t history)
    : capacity_(history < 2 ? 2 : history) {}

void HeadingResolver::SetDegPerPixel(double deg_per_pixel_lat, double deg_per_pixel_lon) {
  if (deg_per_pixel_lat > 0.0 && deg_per_pixel_lon > 0.0) {
    dpp_lat_ = deg_per_pixel_lat;
    dpp_lon_ = deg_per_pixel_lon;
  } else {
    dpp_lat_ = 0.0;
    dpp_lon_ = 0.0;
  }
}

void HeadingResolver::Reset() {
  points_.clear();
  current_ = ResolvedHeading{};
}

ResolvedHeading HeadingResolver::Update(const PositionFix& fix) {
  // A reported heading wins outright, exactly as the original does — and it
  // wins even when the position is missing, because a VTG sentence carries a
  // course and nothing else.
  if (fix.has_true_heading) {
    if (fix.has_position) {
      const GeoPoint p{fix.lat, fix.lon};
      if (points_.empty() || points_.back().lat != p.lat || points_.back().lon != p.lon) {
        points_.push_back(p);
        while (points_.size() > capacity_) points_.pop_front();
      }
    }
    current_ = ResolvedHeading{NormalizeHeadingDeg(fix.true_heading_deg), true, true};
    return current_;
  }

  if (!fix.has_position) {
    // Nothing to derive from. Hold what we had: a dropped sentence must not
    // swing the ownship symbol back to north.
    return current_;
  }

  const GeoPoint here{fix.lat, fix.lon};

  // Walk back to the most recent DISTINCT position, which is the original's
  // do/while over the icon list, bounded by this resolver's history.
  double bearing = 0.0;
  bool derived = false;
  for (auto it = points_.rbegin(); it != points_.rend(); ++it) {
    if (it->lat == here.lat && it->lon == here.lon) continue;
    derived = ScreenBearingDeg(*it, here, dpp_lat_, dpp_lon_, &bearing);
    break;
  }

  if (points_.empty() || points_.back().lat != here.lat || points_.back().lon != here.lon) {
    points_.push_back(here);
    while (points_.size() > capacity_) points_.pop_front();
  }

  if (derived) {
    current_ = ResolvedHeading{bearing, true, false};
  } else if (!current_.known) {
    // The original's last resort: heading unknown, assume north.
    current_ = ResolvedHeading{0.0, false, false};
  }
  // else: standing still with a heading already resolved — keep it. A ship at
  // a stop light points the way it was going, not north.
  return current_;
}

}  // namespace fv
