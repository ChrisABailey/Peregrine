// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// MapProjection — see fvkit/proj.h. dpp comes from the ported MapScaleUtil
// (GetDegreesPerPixel at the center latitude), so values match FalconView's
// equal-arc display, quirks included.

#include "fvkit/proj.h"

#include <algorithm>
#include <cmath>

#include "fv_map_enums.h"
#include "fv_map_scale_util.h"

namespace fv {
namespace {

// Metres of ground per degree of latitude / longitude at geodetic latitude
// `lat_deg` on WGS84, from the standard truncated series (sub-metre accurate).
// Used ONLY by the physical-display path: it needs a correct, numerically
// stable aspect ratio, and MapScaleUtil's Vincenty-based ResolutionToDegrees
// is unstable for the ~1 km east-west lines it samples (it reports a lon/lat
// ratio near 2.1 at mid-latitudes where the true value is ~1/cos(lat)). The
// bit-faithful SetScale path still uses MapScaleUtil unchanged.
void MetersPerDegree(double lat_deg, double* m_per_deg_lat,
                     double* m_per_deg_lon) {
  const double lat = lat_deg * M_PI / 180.0;
  *m_per_deg_lat = 111132.92 - 559.82 * std::cos(2 * lat) +
                   1.175 * std::cos(4 * lat) - 0.0023 * std::cos(6 * lat);
  *m_per_deg_lon = 111412.84 * std::cos(lat) - 93.5 * std::cos(3 * lat) +
                   0.118 * std::cos(5 * lat);
}

}  // namespace

double UnwrapLonNear(double lon, double ref) {
  while (lon - ref > 180.0) lon -= 360.0;
  while (lon - ref < -180.0) lon += 360.0;
  return lon;
}

Status MapProjection::SetSurfaceSize(int width, int height) {
  if (width <= 0 || height <= 0)
    return Status::Error(kInvalidArg, "surface size must be positive");
  width_ = width;
  height_ = height;
  return Update();
}

Status MapProjection::SetCenter(const GeoPoint& center) {
  if (center.lat < -90.0 || center.lat > 90.0)
    return Status::Error(kInvalidArg, "center lat out of range");
  center_ = center;
  center_.Normalize();
  have_center_ = true;
  return Update();
}

Status MapProjection::SetScale(double scale_denominator) {
  if (scale_denominator <= 0)
    return Status::Error(kInvalidArg, "scale must be positive");
  scale_ = scale_denominator;
  mode_ = Mode::kScale;
  return Update();
}

Status MapProjection::SetResolution(double dpp_lat, double dpp_lon) {
  if (dpp_lat <= 0 || dpp_lon <= 0)
    return Status::Error(kInvalidArg, "dpp must be positive");
  dpp_lat_ = dpp_lat;
  dpp_lon_ = dpp_lon;
  scale_ = 0;  // Scale() reports 0 in resolution mode
  mode_ = Mode::kResolution;
  return Update();
}

Status MapProjection::SetPhysicalScale(double scale_denominator,
                                       double mm_per_pixel) {
  if (scale_denominator <= 0)
    return Status::Error(kInvalidArg, "scale must be positive");
  if (mm_per_pixel <= 0)
    return Status::Error(kInvalidArg, "mm_per_pixel must be positive");
  scale_ = scale_denominator;
  mm_per_pixel_ = mm_per_pixel;
  mode_ = Mode::kPhysical;
  return Update();
}

Status MapProjection::SetRotation(double degrees) {
  if (!std::isfinite(degrees))
    return Status::Error(kInvalidArg, "rotation must be finite");
  double d = std::fmod(degrees, 360.0);
  if (d < 0) d += 360.0;
  if (d == 360.0) d = 0.0;  // fmod of a tiny negative can round up to 360
  rot_deg_ = d;
  // Cardinal turns come off a table: cos(pi/2) is 6.1e-17, which is small
  // enough to ignore in a pixel and large enough to show up in a viewport's
  // bounds and in the "0 is the identity" family of tests.
  if (d == 0.0) {
    rot_cos_ = 1;
    rot_sin_ = 0;
  } else if (d == 90.0) {
    rot_cos_ = 0;
    rot_sin_ = 1;
  } else if (d == 180.0) {
    rot_cos_ = -1;
    rot_sin_ = 0;
  } else if (d == 270.0) {
    rot_cos_ = 0;
    rot_sin_ = -1;
  } else {
    const double rad = d * M_PI / 180.0;
    rot_cos_ = std::cos(rad);
    rot_sin_ = std::sin(rad);
  }
  return Status::Ok();  // rotation feeds no dpp; nothing to recompute
}

// Screen axes are x right, y down, so a CLOCKWISE turn on the screen is the
// positive-angle matrix: (1,0) -> (0,1) at 90 degrees, i.e. east goes down.
// Both take their inputs BY VALUE and write only at the end, so the callers
// below can rotate a pair in place.
void MapProjection::RotateOffset(double dx, double dy, double* rx,
                                 double* ry) const {
  const double x = dx * rot_cos_ - dy * rot_sin_;
  const double y = dx * rot_sin_ + dy * rot_cos_;
  *rx = x;
  *ry = y;
}

void MapProjection::RotateOffsetInverse(double dx, double dy, double* rx,
                                        double* ry) const {
  const double x = dx * rot_cos_ + dy * rot_sin_;
  const double y = -dx * rot_sin_ + dy * rot_cos_;
  *rx = x;
  *ry = y;
}

Status MapProjection::Update() {
  ready_ = false;
  if (mode_ == Mode::kResolution) {
    if (!have_center_ || width_ <= 0) return Status::Ok();
    ready_ = dpp_lat_ > 0 && dpp_lon_ > 0;
    return Status::Ok();
  }
  if (!have_center_ || width_ <= 0) return Status::Ok();

  if (mode_ == Mode::kPhysical) {
    if (scale_ <= 0 || mm_per_pixel_ <= 0) return Status::Ok();
    // Ground metres a single screen pixel spans: a screen millimetre covers
    // (scale_) millimetres of ground, so mm_per_pixel * scale_ mm = that many
    // metres / 1000. Convert metres/pixel to degrees/pixel with the true
    // ground metres per degree at the center latitude, so lat and lon pixels
    // cover equal ground (square cells = correct aspect at every latitude).
    const double ground_m_per_pixel = mm_per_pixel_ * scale_ / 1000.0;
    double m_lat = 0, m_lon = 0;
    MetersPerDegree(center_.lat, &m_lat, &m_lon);
    if (m_lat <= 0 || m_lon <= 0)
      return Status::Error(kInternal, "meters-per-degree failed");
    dpp_lat_ = ground_m_per_pixel / m_lat;
    dpp_lon_ = ground_m_per_pixel / m_lon;
    ready_ = dpp_lat_ > 0 && dpp_lon_ > 0;
    return Status::Ok();
  }

  // Mode::kScale
  if (scale_ <= 0) return Status::Ok();
  MapScaleUtil util;
  HRESULT hr = util.GetDegreesPerPixel(center_.lat, scale_,
                                       MAP_SCALE_DENOMINATOR, &dpp_lat_,
                                       &dpp_lon_);
  if (hr != 0 || dpp_lat_ <= 0 || dpp_lon_ <= 0)
    return Status::Error(kInternal, "GetDegreesPerPixel failed");
  ready_ = true;
  return Status::Ok();
}

GeoRect MapProjection::VmapBounds() const {
  if (!ready_) return GeoRect{};
  GeoRect r;
  // Half-extents of the viewport in surface pixels. Rotated, they become the
  // four turned corners and the box is their min/max — the pixel-space AABB
  // maps to the geographic AABB because the surface->geo map is an
  // axis-aligned scaling. At rotation 0 the loop is skipped entirely and the
  // arithmetic below is what it always was, term for term.
  double hx = width_ / 2.0, hy = height_ / 2.0;
  double x_min = -hx, x_max = hx, y_min = -hy, y_max = hy;
  if (rot_deg_ != 0.0) {
    const double cx[4] = {-hx, hx, hx, -hx};
    const double cy[4] = {-hy, -hy, hy, hy};
    for (int i = 0; i < 4; ++i) {
      double rx = 0, ry = 0;
      RotateOffset(cx[i], cy[i], &rx, &ry);
      if (i == 0) {
        x_min = x_max = rx;
        y_min = y_max = ry;
      } else {
        x_min = std::min(x_min, rx);
        x_max = std::max(x_max, rx);
        y_min = std::min(y_min, ry);
        y_max = std::max(y_max, ry);
      }
    }
  }
  // y is DOWN, so the largest y is the southern edge.
  r.ll.lat = std::max(center_.lat - y_max * dpp_lat_, -90.0);
  r.ur.lat = std::min(center_.lat - y_min * dpp_lat_, 90.0);
  double west = center_.lon + x_min * dpp_lon_;
  double east = center_.lon + x_max * dpp_lon_;
  if (east - west >= 360.0) {  // whole-world viewport
    r.ll.lon = -180.0;
    r.ur.lon = 180.0;
  } else {
    r.ll.lon = NormalizeLon(west);
    r.ur.lon = NormalizeLon(east);
  }
  return r;
}

Status MapProjection::GeoToSurface(const GeoPoint& p, double* sx,
                                   double* sy) const {
  if (sx == nullptr || sy == nullptr)
    return Status::Error(kInvalidArg, "sx/sy is null");
  if (!ready_) return Status::Error(kInvalidArg, "projection not configured");
  double dlon = UnwrapLonNear(p.lon, center_.lon) - center_.lon;
  double dx = dlon / dpp_lon_;
  double dy = (center_.lat - p.lat) / dpp_lat_;
  if (rot_deg_ != 0.0) RotateOffset(dx, dy, &dx, &dy);
  *sx = (width_ - 1) / 2.0 + dx;
  *sy = (height_ - 1) / 2.0 + dy;
  return Status::Ok();
}

Status MapProjection::SurfaceToGeo(double sx, double sy, GeoPoint* p) const {
  if (p == nullptr) return Status::Error(kInvalidArg, "p is null");
  if (!ready_) return Status::Error(kInvalidArg, "projection not configured");
  double dx = sx - (width_ - 1) / 2.0;
  double dy = sy - (height_ - 1) / 2.0;
  if (rot_deg_ != 0.0) RotateOffsetInverse(dx, dy, &dx, &dy);
  p->lat = center_.lat - dy * dpp_lat_;
  p->lon = NormalizeLon(center_.lon + dx * dpp_lon_);
  return Status::Ok();
}

}  // namespace fv
