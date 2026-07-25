// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// MapProjection — see fvkit/proj.h. dpp comes from the ported MapScaleUtil
// (GetDegreesPerPixel at the center latitude), so values match FalconView's
// equal-arc display, quirks included.

#include "fvkit/proj.h"

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
  r.ll.lat = std::max(center_.lat - (height_ / 2.0) * dpp_lat_, -90.0);
  r.ur.lat = std::min(center_.lat + (height_ / 2.0) * dpp_lat_, 90.0);
  double west = center_.lon - (width_ / 2.0) * dpp_lon_;
  double east = center_.lon + (width_ / 2.0) * dpp_lon_;
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
  *sx = (width_ - 1) / 2.0 + dlon / dpp_lon_;
  *sy = (height_ - 1) / 2.0 + (center_.lat - p.lat) / dpp_lat_;
  return Status::Ok();
}

Status MapProjection::SurfaceToGeo(double sx, double sy, GeoPoint* p) const {
  if (p == nullptr) return Status::Error(kInvalidArg, "p is null");
  if (!ready_) return Status::Error(kInvalidArg, "projection not configured");
  p->lat = center_.lat - (sy - (height_ - 1) / 2.0) * dpp_lat_;
  p->lon = NormalizeLon(center_.lon + (sx - (width_ - 1) / 2.0) * dpp_lon_);
  return Status::Ok();
}

}  // namespace fv
