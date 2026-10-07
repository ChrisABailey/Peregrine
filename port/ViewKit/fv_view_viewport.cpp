// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_view_viewport.h"

#include <algorithm>
#include <cmath>

namespace fv {
namespace view {

namespace {

double Clamp(double v, double lo, double hi) {
  return std::max(lo, std::min(hi, v));
}

double WrapDegrees(double d) {
  double r = std::fmod(d, 360.0);
  if (r < 0) r += 360.0;
  return r;
}

}  // namespace

Viewport::Viewport() { Reconfigure(); }

Viewport Viewport::Make(const GeoPoint& center, double scale_denom,
                        double rotation_deg) {
  Viewport v;
  return v.WithCamera(center, scale_denom, rotation_deg);
}

Viewport Viewport::WithSurface(double width_pt, double height_pt,
                               double display_scale,
                               double mm_per_point) const {
  const double s = display_scale > 0 ? display_scale : 1.0;
  const int w = static_cast<int>(std::lround(width_pt * s));
  const int h = static_cast<int>(std::lround(height_pt * s));
  if (w <= 0 || h <= 0) return *this;
  Viewport v = *this;
  v.width_pt_ = width_pt;
  v.height_pt_ = height_pt;
  v.display_scale_ = s;
  v.pixel_width_ = w;
  v.pixel_height_ = h;
  if (mm_per_point > 0) v.mm_per_point_ = mm_per_point;
  v.Reconfigure();
  return v;
}

Viewport Viewport::WithLimits(const ScaleLimits& limits) const {
  Viewport v = *this;
  v.limits_ = limits;
  if (v.limits_.max_denom < v.limits_.min_denom)
    v.limits_.max_denom = v.limits_.min_denom;
  return v.WithCamera(center_, scale_denom_, rotation_deg_);
}

Viewport Viewport::WithCamera(const GeoPoint& center, double scale_denom,
                              double rotation_deg) const {
  Viewport v = *this;
  GeoPoint c = center;
  c.Normalize();
  v.center_ = c;
  if (std::isfinite(scale_denom) && scale_denom > 0)
    v.scale_denom_ = Clamp(scale_denom, limits_.min_denom, limits_.max_denom);
  if (std::isfinite(rotation_deg)) v.rotation_deg_ = WrapDegrees(rotation_deg);
  v.Reconfigure();
  return v;
}

Viewport Viewport::WithProjectionType(ProjectionType type) const {
  Viewport v = *this;
  v.type_ = type;
  v.Reconfigure();
  return v;
}

void Viewport::Reconfigure() {
  proj_.SetProjectionType(type_);
  if (HasSurface()) proj_.SetSurfaceSize(pixel_width_, pixel_height_);
  proj_.SetCenter(center_);
  proj_.SetPhysicalScale(scale_denom_, mm_per_point_ / display_scale_);
  // Rotation last: it is independent of the dpp mode and must not be read as
  // one of the scale calls.
  proj_.SetRotation(rotation_deg_);
}

PointF Viewport::SurfaceCenter() const {
  return PointF{(pixel_width_ - 1) / 2.0 / display_scale_,
                (pixel_height_ - 1) / 2.0 / display_scale_};
}

GeoPoint Viewport::GeoAt(const PointF& p) const {
  GeoPoint g = center_;
  if (!HasSurface()) return g;
  GeoPoint out;
  if (proj_.SurfaceToGeo(p.x * display_scale_, p.y * display_scale_, &out).ok())
    g = out;
  return g;
}

bool Viewport::PointFor(const GeoPoint& g, PointF* out) const {
  if (!HasSurface()) return false;
  double sx = 0, sy = 0;
  if (!proj_.GeoToSurface(g, &sx, &sy).ok()) return false;
  if (!std::isfinite(sx) || !std::isfinite(sy)) return false;
  *out = PointF{sx / display_scale_, sy / display_scale_};
  return true;
}

Viewport Viewport::Panned(double dx_pt, double dy_pt) const {
  if (!HasSurface()) return *this;
  // Ask the projection which position ends up under the centre pixel; this
  // is correct under rotation and at any latitude without a dpp division.
  const PointF c = SurfaceCenter();
  const PointF p{c.x - dx_pt, c.y - dy_pt};
  GeoPoint g;
  if (!proj_.SurfaceToGeo(p.x * display_scale_, p.y * display_scale_, &g).ok())
    return *this;
  return WithCamera(g, scale_denom_, rotation_deg_);
}

Viewport Viewport::WithGeoAt(const GeoPoint& g, const PointF& p) const {
  Viewport v = *this;
  for (int i = 0; i < 8; ++i) {
    PointF q;
    if (!v.PointFor(g, &q)) break;
    const double dx = p.x - q.x, dy = p.y - q.y;
    if (std::fabs(dx) < 1e-9 && std::fabs(dy) < 1e-9) break;
    v = v.Panned(dx, dy);
  }
  return v;
}

Viewport Viewport::ZoomedTo(double scale_denom, const PointF& anchor) const {
  if (!HasSurface() || !(scale_denom > 0) || !std::isfinite(scale_denom))
    return *this;
  const GeoPoint under = GeoAt(anchor);
  // Clamped before the anchor correction, so a zoom into a limit keeps the
  // anchor fixed instead of sliding the map.
  return WithCamera(center_, scale_denom, rotation_deg_).WithGeoAt(under, anchor);
}

Viewport Viewport::ZoomedBy(double factor, const PointF& anchor) const {
  if (!(factor > 0) || !std::isfinite(factor)) return *this;
  return ZoomedTo(scale_denom_ / factor, anchor);
}

Viewport Viewport::RotatedBy(double degrees, const PointF& anchor) const {
  if (!HasSurface() || !std::isfinite(degrees) || degrees == 0.0) return *this;
  const GeoPoint under = GeoAt(anchor);
  return WithCamera(center_, scale_denom_, rotation_deg_ + degrees)
      .WithGeoAt(under, anchor);
}

bool Viewport::operator==(const Viewport& o) const {
  return center_.lat == o.center_.lat && center_.lon == o.center_.lon &&
         scale_denom_ == o.scale_denom_ && rotation_deg_ == o.rotation_deg_ &&
         width_pt_ == o.width_pt_ && height_pt_ == o.height_pt_ &&
         display_scale_ == o.display_scale_ &&
         mm_per_point_ == o.mm_per_point_ &&
         limits_.min_denom == o.limits_.min_denom &&
         limits_.max_denom == o.limits_.max_denom &&
         type_ == o.type_;
}

}  // namespace view
}  // namespace fv
