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
#include <limits>

#include "fv_map_enums.h"
#include "fv_map_scale_util.h"
#include "geo_tool.h"  // fv_geo_tool: GEO_geo_to_distance, WGS84_a_METERS

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

/// Ground metres per surface pixel at `lat`, as Windows'
/// Projector::calc_meters_per_pixel derives it: the geodesic length of one
/// pixel of latitude on the centre meridian, taken on the equator side of the
/// centre. Windows measures a pixel of the UNZOOMED dpp and then scales by the
/// zoom percent; the port has no zoom stage, so it measures the effective dpp
/// directly. Not printing, so the longitude value is the latitude value.
double MosaicMetersPerPixel(double center_lat, double center_lon,
                            double dpp_lat) {
  double top_lat, bottom_lat;
  if (center_lat >= 0) {
    top_lat = center_lat;
    bottom_lat = center_lat - dpp_lat;
  } else {
    top_lat = center_lat + dpp_lat;
    bottom_lat = center_lat;
  }
  double m_p_p = 0, dummy = 0;
  GEO_geo_to_distance(bottom_lat, center_lon, top_lat, center_lon, &m_p_p,
                      &dummy);
  return m_p_p;
}

/// Mercator projection-plane northing, in metres, for `lat` at `cos_std`.
double MercatorY(double lat, double cos_std) {
  return WGS84_a_METERS * std::log(std::tan(M_PI / 4 + (lat / 2) * M_PI / 180.0)) *
         cos_std;
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

Status MapProjection::SetProjectionType(ProjectionType type) {
  switch (type) {
    case ProjectionType::kEqualArc:
    case ProjectionType::kMercator:
    case ProjectionType::kLambert:
    case ProjectionType::kAzimuthalEquidistant:
    case ProjectionType::kOrthographic:
      break;
    default:
      return Status::Error(kUnsupported, "projection not implemented");
  }
  type_ = type;
  return Update();
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
    return ready_ ? UpdateProjectionConstants() : Status::Ok();
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
    return ready_ ? UpdateProjectionConstants() : Status::Ok();
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
  return UpdateProjectionConstants();
}

/** Derives the non-affine projectors' constants from the current centre and
 *  dpp. Equal Arc uses none of them, so its path leaves them zeroed. */
Status MapProjection::UpdateProjectionConstants() {
  pc_ = ProjectionConstants{};
  if (type_ == ProjectionType::kEqualArc) return Status::Ok();
  if (type_ == ProjectionType::kMercator) return UpdateMercatorConstants();
  if (type_ == ProjectionType::kLambert) return UpdateLambertConstants();
  if (type_ == ProjectionType::kAzimuthalEquidistant ||
      type_ == ProjectionType::kOrthographic)
    return UpdateAzimuthalConstants();
  return Status::Error(kUnsupported, "projection not implemented");
}

/** Ports MercatorProj::validate_center plus its half of
 *  initialize_projection_specific_parameters: the standard parallel is the
 *  centre latitude, and the centre is walked towards the equator, one pixel of
 *  latitude at a time, until the view's near edge is inside kMercatorMaxLat.
 *  Each step re-derives the metres per pixel at the new centre, as Windows
 *  does, because the standard parallel moved with it.
 *
 *  Windows tests the unrotated top-left and bottom-right pixels, so this does
 *  too: under rotation a corner can still fall beyond the limit, and it
 *  reports kNotProjectable rather than moving the centre further. */
Status MapProjection::UpdateMercatorConstants() {
  // A centre at the pole has no Mercator northing at all; Windows keeps it one
  // pixel clear of MAX_LAT_DEG before the loop below can use it.
  double calc_lat = center_.lat;
  if (calc_lat >= 90.0) calc_lat = 90.0 - dpp_lat_;
  if (calc_lat <= -90.0) calc_lat = -90.0 + dpp_lat_;

  pc_.std_parallel = calc_lat;
  pc_.cos_std_parallel = std::cos(calc_lat * M_PI / 180.0);
  pc_.center_lat_for_calculations = calc_lat;
  pc_.mosaic_m_per_px_lat =
      MosaicMetersPerPixel(calc_lat, center_.lon, dpp_lat_);
  pc_.mosaic_m_per_px_lon = pc_.mosaic_m_per_px_lat;
  pc_.mercator_y0 = MercatorY(calc_lat, pc_.cos_std_parallel);
  if (pc_.mosaic_m_per_px_lat <= 0)
    return Status::Error(kInternal, "meters-per-pixel failed");

  // The unrotated edge the limit applies to, as a pixel offset from the
  // surface centre: the top row in the north, the bottom row in the south.
  const double edge_dy = center_.lat >= 0.0 ? -(height_ - 1) / 2.0
                                            : (height_ - 1) / 2.0;
  const double delta_lat = dpp_lat_;
  const double limit = center_.lat >= 0.0 ? kMercatorMaxLat : -kMercatorMaxLat;

  // Windows iterates without a bound. A tiny dpp under a centre near the pole
  // would spin for millions of steps, so the walk stops after one screen's
  // worth of pixels and the remaining distance is closed in one jump; the
  // result is the same centre to within a pixel of latitude.
  const int max_steps = height_ > 0 ? height_ + 2 : 2;
  for (int step = 0; step < max_steps; ++step) {
    double edge_lat = 0, unused_dlon = 0;
    MercatorInverseRaw(0.0, edge_dy, &edge_lat, &unused_dlon);
    const bool outside = center_.lat >= 0.0 ? edge_lat > limit
                                            : edge_lat < limit;
    if (!outside) return Status::Ok();
    calc_lat += center_.lat >= 0.0 ? -delta_lat : delta_lat;
    pc_.std_parallel = calc_lat;
    pc_.cos_std_parallel = std::cos(calc_lat * M_PI / 180.0);
    pc_.center_lat_for_calculations = calc_lat;
    pc_.mosaic_m_per_px_lat =
        MosaicMetersPerPixel(calc_lat, center_.lon, dpp_lat_);
    pc_.mosaic_m_per_px_lon = pc_.mosaic_m_per_px_lat;
    pc_.mercator_y0 = MercatorY(calc_lat, pc_.cos_std_parallel);
  }

  // Still outside after a full screen of steps: place the edge exactly on the
  // limit with the constants in hand, then re-derive them at that centre.
  const double edge_y = MercatorY(limit, pc_.cos_std_parallel);
  calc_lat = 0;
  {
    // edge_dy = (y0 - edge_y) / m_per_px  =>  y0 = edge_y + edge_dy * m_per_px
    const double y0 = edge_y + edge_dy * pc_.mosaic_m_per_px_lat;
    const double t = std::exp(y0 / (WGS84_a_METERS * pc_.cos_std_parallel));
    calc_lat = (2 * std::atan(t) - M_PI / 2) * 180.0 / M_PI;
  }
  pc_.std_parallel = calc_lat;
  pc_.cos_std_parallel = std::cos(calc_lat * M_PI / 180.0);
  pc_.center_lat_for_calculations = calc_lat;
  pc_.mosaic_m_per_px_lat =
      MosaicMetersPerPixel(calc_lat, center_.lon, dpp_lat_);
  pc_.mosaic_m_per_px_lon = pc_.mosaic_m_per_px_lat;
  pc_.mercator_y0 = MercatorY(calc_lat, pc_.cos_std_parallel);
  return Status::Ok();
}

/** MercatorProj::geo_to_xy, in surface-pixel offsets from the surface centre.
 *  `dlon` is degrees east of the projection centre, already unwrapped by the
 *  caller if it needs to be. */
void MapProjection::MercatorForwardRaw(double lat, double dlon, double* dx,
                                       double* dy) const {
  double xx = WGS84_a_METERS * (dlon * M_PI / 180.0) * pc_.cos_std_parallel;
  xx /= pc_.mosaic_m_per_px_lon;
  const double yy = MercatorY(lat, pc_.cos_std_parallel);
  *dx = xx;
  *dy = (pc_.mercator_y0 - yy) / pc_.mosaic_m_per_px_lat;
}

/** MercatorProj::xy_to_geo, from surface-pixel offsets. `dlon` comes back as
 *  degrees east of the projection centre, not normalized. */
void MapProjection::MercatorInverseRaw(double dx, double dy, double* lat,
                                       double* dlon) const {
  const double xx = dx * pc_.mosaic_m_per_px_lon;
  *dlon = ((xx / pc_.cos_std_parallel) / WGS84_a_METERS) * 180.0 / M_PI;
  const double yy = dy * -pc_.mosaic_m_per_px_lat + pc_.mercator_y0;
  *lat = (0.5 * M_PI -
          2 * std::atan(std::exp((-yy / pc_.cos_std_parallel) /
                                 WGS84_a_METERS))) *
         180.0 / M_PI;
}

/** Ports LambertProj::validate_center and
 *  initialize_projection_specific_parameters: the centre snaps to the pole
 *  when the pole is on the surface, the two standard parallels are the centre
 *  latitude plus and minus a third of the surface height in degrees, and the
 *  cone constant and its two companions follow from them.
 *
 *  Within one pixel of latitude of the equator the cone constant goes to zero
 *  and the Mercator equations take over. That fallback is the Lambert file's
 *  own, not MercatorProj's: no standard-parallel scaling and no centre
 *  northing, which is what cos_std_parallel = 1 and mercator_y0 = 0 give the
 *  shared raw pair.
 *
 *  Windows' WORLD_OVERVIEW rule (a second parallel at +/-30, and a
 *  metres-per-pixel override) is not ported: WORLD_OVERVIEW is a sentinel
 *  scale denominator the port has no equivalent of, and in Windows it is
 *  unreachable anyway — validate_center zeroes the centre latitude first,
 *  which sends every WORLD_OVERVIEW view down the Mercator fallback before
 *  the parallels are read.
 */
Status MapProjection::UpdateLambertConstants() {
  double calc_lat = center_.lat;
  const double degrees_center_to_pole = 90.0 - std::fabs(center_.lat);
  if (degrees_center_to_pole >= 0.0) {
    // Windows compares the rounded pixel distance against the integer half
    // height, so a pole exactly half a surface away stays where it is.
    const double pixels_center_to_pole = degrees_center_to_pole / dpp_lat_;
    if (static_cast<int>(pixels_center_to_pole + 0.5) < height_ / 2)
      calc_lat = center_.lat >= 0.0 ? 89.999999 : -89.999999;
  }
  pc_.center_lat_for_calculations = calc_lat;
  pc_.mosaic_m_per_px_lat =
      MosaicMetersPerPixel(calc_lat, center_.lon, dpp_lat_);
  pc_.mosaic_m_per_px_lon = pc_.mosaic_m_per_px_lat;
  if (pc_.mosaic_m_per_px_lat <= 0)
    return Status::Error(kInternal, "meters-per-pixel failed");

  const double third = (height_ * dpp_lat_) / 3.0;
  double phi1 = calc_lat + third;  // northernmost
  double phi2 = calc_lat - third;
  // A parallel at a pole has no cone; reflect the other one about the centre.
  if (phi1 >= 90.0) {
    phi1 = 89.999999;
    phi2 = 2 * calc_lat - 90.0;
    if (phi2 > phi1) phi2 = phi1;
  }
  if (phi2 <= -90.0) {
    phi2 = -89.999999;
    phi1 = 2 * calc_lat + 90.0;
    if (phi1 < phi2) phi1 = phi2;
  }

  const double limit = std::fabs(dpp_lat_);
  if (calc_lat < limit && calc_lat > -limit) {
    pc_.lambert_uses_mercator = true;
    pc_.cos_std_parallel = 1.0;
    pc_.mercator_y0 = 0.0;
    return Status::Ok();
  }
  pc_.lambert_uses_mercator = false;

  double n = 0;
  if (phi1 == phi2) {  // one standard parallel, the tangent cone
    n = std::sin(phi1 * M_PI / 180.0);
  } else {
    double tf = std::cos(phi1 * M_PI / 180.0);
    double tf2 = std::cos(phi2 * M_PI / 180.0);
    if (tf2 == 0.0) return Status::Error(kInternal, "standard parallel at a pole");
    n = std::log(tf / tf2);
    tf = std::tan(0.25 * M_PI + 0.5 * (phi1 * M_PI / 180.0));
    if (tf == 0.0) return Status::Error(kInternal, "standard parallel at a pole");
    tf2 = std::tan(0.25 * M_PI + 0.5 * (phi2 * M_PI / 180.0));
    if (tf2 == 0.0) return Status::Error(kInternal, "standard parallel at a pole");
    tf2 /= tf;
    tf2 = std::log(tf2);
    if (tf2 == 0.0) return Status::Error(kInternal, "standard parallels coincide");
    n /= tf2;
  }
  if (n == 0.0) return Status::Error(kInternal, "cone constant is zero");

  pc_.lambert_n = n;
  pc_.lambert_F = std::cos(phi1 * M_PI / 180.0) *
                  std::pow(std::tan(0.25 * M_PI + 0.5 * (phi1 * M_PI / 180.0)),
                           n) / n;
  pc_.lambert_rho_0 =
      WGS84_a_METERS * pc_.lambert_F /
      std::pow(std::tan(0.25 * M_PI + 0.5 * (calc_lat * M_PI / 180.0)), n);
  return Status::Ok();
}

/** LambertProj::geo_to_xy, in surface-pixel offsets from the surface centre.
 *  `dlon` is degrees east of the projection centre, already unwrapped by the
 *  caller if it needs to be. */
void MapProjection::LambertForwardRaw(double lat, double dlon, double* dx,
                                      double* dy) const {
  if (pc_.lambert_uses_mercator) {
    MercatorForwardRaw(lat, dlon, dx, dy);
    return;
  }
  double rho;
  if ((pc_.lambert_n > 0.0 && lat == 90.0) ||
      (pc_.lambert_n < 0.0 && lat == -90.0)) {
    rho = 0.0;  // the pole the cone closes on is the apex
  } else {
    const double tan_value = std::tan(0.25 * M_PI + 0.5 * (lat * M_PI / 180.0));
    const double power = std::pow(tan_value, pc_.lambert_n);
    rho = WGS84_a_METERS * pc_.lambert_F / power;
  }
  const double theta = pc_.lambert_n * (dlon * M_PI / 180.0);
  *dx = rho * std::sin(theta) / pc_.mosaic_m_per_px_lon;
  *dy = (rho * std::cos(theta) - pc_.lambert_rho_0) / pc_.mosaic_m_per_px_lat;
}

/** LambertProj::xy_to_geo, from surface-pixel offsets. `dlon` comes back as
 *  degrees east of the projection centre, not normalized. False where Windows
 *  returns FAILURE, or where the result is not finite — the far pole is one
 *  such point, and the port reports it rather than passing an infinity on. */
bool MapProjection::LambertInverseRaw(double dx, double dy, double* lat,
                                      double* dlon) const {
  if (pc_.lambert_uses_mercator) {
    MercatorInverseRaw(dx, dy, lat, dlon);
    return std::isfinite(*lat) && std::isfinite(*dlon);
  }
  const double xx = dx * pc_.mosaic_m_per_px_lon;
  const double yy = -dy * pc_.mosaic_m_per_px_lat;
  double theta;
  double rho = std::sqrt(xx * xx + (pc_.lambert_rho_0 - yy) *
                                       (pc_.lambert_rho_0 - yy));
  if (pc_.lambert_n < 0.0) {
    theta = std::atan2(-xx, yy - pc_.lambert_rho_0);
    rho = -std::fabs(rho);
  } else {
    theta = std::atan2(xx, pc_.lambert_rho_0 - yy);
    rho = std::fabs(rho);
  }
  if (rho == 0.0 || pc_.lambert_n == 0.0) return false;
  *lat = (2 * std::atan(std::pow(WGS84_a_METERS * pc_.lambert_F / rho,
                                 1 / pc_.lambert_n)) -
          M_PI / 2) *
         180.0 / M_PI;
  *dlon = (theta / pc_.lambert_n) * 180.0 / M_PI;
  return std::isfinite(*lat) && std::isfinite(*dlon);
}

/** Sets the azimuthal pair's projection centre and the sine and cosine every
 *  one of their transforms goes through, plus the metres per pixel the base
 *  Projector derives — neither AzimEquiProj nor OrthoProj overrides
 *  calc_meters_per_pixel, so both take a pixel of latitude at the centre.
 *
 *  AzimEquiProj::validate_center moves a centre within 1e-6 of the equator to
 *  0.5 degrees north. That is NOT ported. The spherical equations are well
 *  defined at a centre latitude of zero in both directions, and Windows makes
 *  the fudge invisible by handing the moved centre back to the caller, which
 *  re-requests it; the port's convention is that Center() reports what was
 *  asked for (contracts D4), so the same fudge would leave the map half a
 *  degree off its own reported centre.
 */
Status MapProjection::UpdateAzimuthalConstants() {
  const double calc_lat = center_.lat;
  pc_.center_lat_for_calculations = calc_lat;
  pc_.sin_center_lat = std::sin(calc_lat * M_PI / 180.0);
  pc_.cos_center_lat = std::cos(calc_lat * M_PI / 180.0);
  pc_.mosaic_m_per_px_lat =
      MosaicMetersPerPixel(calc_lat, center_.lon, dpp_lat_);
  pc_.mosaic_m_per_px_lon = pc_.mosaic_m_per_px_lat;
  if (pc_.mosaic_m_per_px_lat <= 0)
    return Status::Error(kInternal, "meters-per-pixel failed");
  return Status::Ok();
}

/** AzimEquiProj::geo_to_xy, in surface-pixel offsets from the surface centre.
 *
 *  False at the antipode of the centre, which has no single image: the whole
 *  rim of the disc is that one point. Windows' own arithmetic lands it back on
 *  the centre — the clamp makes cos_c exactly -1, sin(acos(-1)) is 1.2e-16,
 *  which fails the DBL_EPSILON test, so the scale factor stays at pi while
 *  both direction terms vanish. The arithmetic is kept; the caller is told
 *  instead of being handed the middle of the map (D7). */
bool MapProjection::AzEqForwardRaw(double lat, double dlon, double* dx,
                                   double* dy) const {
  const double lat_rad = lat * M_PI / 180.0;
  const double dlon_rad = dlon * M_PI / 180.0;
  double cos_c = pc_.sin_center_lat * std::sin(lat_rad) +
                 pc_.cos_center_lat * std::cos(lat_rad) * std::cos(dlon_rad);
  if (cos_c > 1.0)
    cos_c = 1.0;
  else if (cos_c < -1.0)
    cos_c = -1.0;
  const double c = std::acos(cos_c);
  const double sin_c = std::sin(c);
  double k = c;
  if (std::fabs(sin_c) > std::numeric_limits<double>::epsilon()) k /= sin_c;
  double xx = WGS84_a_METERS * k * std::cos(lat_rad) * std::sin(dlon_rad);
  xx /= pc_.mosaic_m_per_px_lon;
  double yy = -WGS84_a_METERS * k *
              (pc_.cos_center_lat * std::sin(lat_rad) -
               pc_.sin_center_lat * std::cos(lat_rad) * std::cos(dlon_rad));
  yy /= pc_.mosaic_m_per_px_lat;
  *dx = xx;
  *dy = yy;
  return !(c > 0.0 &&
           std::fabs(sin_c) <= std::numeric_limits<double>::epsilon());
}

/** AzimEquiProj::xy_to_geo, from surface-pixel offsets. `dlon` comes back as
 *  degrees east of the projection centre, not normalized.
 *
 *  False beyond the rim, where Windows keeps going: an arc of more than 180
 *  degrees wraps the arithmetic back over the near hemisphere and draws the
 *  earth a second time outside the disc (D7). */
bool MapProjection::AzEqInverseRaw(double dx, double dy, double* lat,
                                   double* dlon) const {
  const double xx = dx * pc_.mosaic_m_per_px_lon;
  const double yy = -dy * pc_.mosaic_m_per_px_lat;
  const double rho = std::sqrt(xx * xx + yy * yy);
  // Windows compares rho, in metres, against its degrees-per-pixel member.
  // It is a rho-is-zero test whose threshold happens to be six orders of
  // magnitude below a pixel; kept as it stands.
  if (rho <= dpp_lat_) {
    *lat = pc_.center_lat_for_calculations;
    *dlon = 0.0;
    return true;
  }
  const double c = rho / WGS84_a_METERS;
  if (c > M_PI) return false;
  double sin_lat = std::cos(c) * pc_.sin_center_lat +
                   (yy * std::sin(c) * pc_.cos_center_lat / rho);
  sin_lat = std::max(-1.0, std::min(1.0, sin_lat));
  *lat = std::asin(sin_lat) * 180.0 / M_PI;
  if (pc_.center_lat_for_calculations > 89.999999)
    *dlon = std::atan2(xx, -yy) * 180.0 / M_PI;
  else if (pc_.center_lat_for_calculations < -89.999999)
    *dlon = std::atan2(xx, yy) * 180.0 / M_PI;
  else
    *dlon = std::atan2(xx * std::sin(c),
                       rho * pc_.cos_center_lat * std::cos(c) -
                           yy * pc_.sin_center_lat * std::sin(c)) *
            180.0 / M_PI;
  return std::isfinite(*lat) && std::isfinite(*dlon);
}

/** OrthoProj::geo_to_xy, in surface-pixel offsets from the surface centre.
 *  False on the far hemisphere (Windows' NONVISIBLE_RESULT), where the point
 *  is hidden behind the globe but still has a plane position — the point is
 *  written either way, so a caller walking an edge can use it.
 *
 *  Windows divides both axes by the LATITUDE metres per pixel. The port does
 *  the same; the two are equal here anyway, and the pair must stay isotropic
 *  for the disc to be a circle. */
bool MapProjection::OrthoForwardRaw(double lat, double dlon, double* dx,
                                    double* dy) const {
  const double lat_rad = lat * M_PI / 180.0;
  const double dlon_rad = dlon * M_PI / 180.0;
  const double cos_c = pc_.sin_center_lat * std::sin(lat_rad) +
                       pc_.cos_center_lat * std::cos(lat_rad) *
                           std::cos(dlon_rad);
  double xx = WGS84_a_METERS * std::cos(lat_rad) * std::sin(dlon_rad);
  xx /= pc_.mosaic_m_per_px_lat;
  double yy = -WGS84_a_METERS *
              (pc_.cos_center_lat * std::sin(lat_rad) -
               pc_.sin_center_lat * std::cos(lat_rad) * std::cos(dlon_rad));
  yy /= pc_.mosaic_m_per_px_lat;
  *dx = xx;
  *dy = yy;
  return cos_c >= 0.0;
}

/** OrthoProj::xy_to_geo, from surface-pixel offsets. `dlon` comes back as
 *  degrees east of the projection centre, not normalized. False outside the
 *  disc of radius R, which is Windows' INVALID_POINT, and on the one point of
 *  the limb where its longitude denominator vanishes. */
bool MapProjection::OrthoInverseRaw(double dx, double dy, double* lat,
                                    double* dlon) const {
  const double xx = dx * pc_.mosaic_m_per_px_lat;
  const double yy = -dy * pc_.mosaic_m_per_px_lat;
  const double rho = std::sqrt(xx * xx + yy * yy);
  if (rho == 0.0) {
    *lat = pc_.center_lat_for_calculations;
    *dlon = 0.0;
    return true;
  }
  const double tf = rho / WGS84_a_METERS;
  if (std::fabs(tf) > 1.0) return false;
  const double c = std::asin(tf);
  double sin_lat = std::cos(c) * pc_.sin_center_lat +
                   (yy * std::sin(c) * pc_.cos_center_lat / rho);
  sin_lat = std::max(-1.0, std::min(1.0, sin_lat));
  *lat = std::asin(sin_lat) * 180.0 / M_PI;
  const double den = rho * pc_.cos_center_lat * std::cos(c) -
                     yy * pc_.sin_center_lat * std::sin(c);
  if (den == 0.0) return false;
  *dlon = std::atan2(xx * std::sin(c), den) * 180.0 / M_PI;
  return std::isfinite(*lat) && std::isfinite(*dlon);
}

/** The point `c_rad` of arc from the projection centre along `az_rad`,
 *  measured clockwise from north, as latitude and degrees east of the
 *  centre. */
void MapProjection::PointAtAzimuth(double c_rad, double az_rad, double* lat,
                                   double* dlon) const {
  double sin_lat = pc_.sin_center_lat * std::cos(c_rad) +
                   pc_.cos_center_lat * std::sin(c_rad) * std::cos(az_rad);
  sin_lat = std::max(-1.0, std::min(1.0, sin_lat));
  *lat = std::asin(sin_lat) * 180.0 / M_PI;
  *dlon = std::atan2(std::sin(az_rad) * std::sin(c_rad) * pc_.cos_center_lat,
                     std::cos(c_rad) - pc_.sin_center_lat * sin_lat) *
          180.0 / M_PI;
}

/** Walks the surface boundary and takes the extremes of latitude and of
 *  unwrapped longitude, tests whether a pole is on the surface, and — for the
 *  azimuthal pair — walks whatever part of the limb the surface holds.
 *
 *  Windows samples six points per projection: three along each of the top and
 *  bottom edges, with longitude from the two corners of the pole-ward edge.
 *  That is enough only when north is up and the whole surface is on the
 *  globe; a turned chart puts the extremes anywhere on the boundary, and an
 *  azimuthal view can run off the earth entirely. The whole boundary is
 *  walked instead. At rotation 0 the walk includes Windows' six points, so it
 *  reports the same box wherever Windows' answer was right.
 */
MapProjection::BoundarySweep MapProjection::SweepProjectedBoundary() const {
  BoundarySweep s;
  const bool azimuthal = type_ == ProjectionType::kAzimuthalEquidistant ||
                         type_ == ProjectionType::kOrthographic;

  // An unrotated plane offset, turned and placed, against the surface.
  auto on_surface = [&](double px, double py) {
    if (!std::isfinite(px) || !std::isfinite(py)) return false;
    if (rot_deg_ != 0.0) RotateOffset(px, py, &px, &py);
    px += (width_ - 1) / 2.0;
    py += (height_ - 1) / 2.0;
    return px >= 0 && px < width_ && py >= 0 && py < height_;
  };
  auto forward = [&](double lat, double dlon, double* px, double* py) {
    if (type_ == ProjectionType::kOrthographic)
      return OrthoForwardRaw(lat, dlon, px, py);
    if (type_ == ProjectionType::kAzimuthalEquidistant)
      return AzEqForwardRaw(lat, dlon, px, py);
    LambertForwardRaw(lat, dlon, px, py);
    return true;
  };
  auto inverse = [&](double dx, double dy, double* lat, double* dlon) {
    if (type_ == ProjectionType::kOrthographic)
      return OrthoInverseRaw(dx, dy, lat, dlon);
    if (type_ == ProjectionType::kAzimuthalEquidistant)
      return AzEqInverseRaw(dx, dy, lat, dlon);
    return LambertInverseRaw(dx, dy, lat, dlon);
  };
  auto add = [&](double lat, double dlon) {
    if (!s.valid) {
      s.lat_min = s.lat_max = lat;
      s.dlon_min = s.dlon_max = dlon;
      s.valid = true;
      return;
    }
    s.lat_min = std::min(s.lat_min, lat);
    s.lat_max = std::max(s.lat_max, lat);
    s.dlon_min = std::min(s.dlon_min, dlon);
    s.dlon_max = std::max(s.dlon_max, dlon);
  };

  s.pole_lat = pc_.center_lat_for_calculations >= 0.0 ? 89.999999 : -89.999999;
  if (azimuthal) {
    // Either pole, or both: an azimuthal view centred near the equator and
    // wide enough holds the two of them, and the equations reach 90 exactly.
    for (int i = 0; i < 2; ++i) {
      const double lat = i == 0 ? 90.0 : -90.0;
      double px = 0, py = 0;
      if (!forward(lat, 0.0, &px, &py) || !on_surface(px, py)) continue;
      (i == 0 ? s.north_pole_on_surface : s.south_pole_on_surface) = true;
      add(lat, 0.0);
    }
    // The projection centre is on the globe and on the surface by
    // construction, so the sweep is never empty.
    add(pc_.center_lat_for_calculations, 0.0);
  } else if (!pc_.lambert_uses_mercator) {
    double px = 0, py = 0;
    LambertForwardRaw(s.pole_lat, 0.0, &px, &py);
    s.pole_on_surface = on_surface(px, py);
    if (s.pole_on_surface) {
      (s.pole_lat >= 0 ? s.north_pole_on_surface : s.south_pole_on_surface) =
          true;
    }
  }
  if (s.north_pole_on_surface || s.south_pole_on_surface)
    s.all_longitudes = true;  // every meridian reaches a pole
  if (azimuthal) s.pole_on_surface = s.all_longitudes;

  constexpr int kStepsPerEdge = 64;
  const double x0 = -(width_ - 1) / 2.0, x1 = (width_ - 1) / 2.0;
  const double y0 = -(height_ - 1) / 2.0, y1 = (height_ - 1) / 2.0;
  bool edge_off_globe = false;
  for (int edge = 0; edge < 4; ++edge) {
    for (int i = 0; i <= kStepsPerEdge; ++i) {
      const double t = static_cast<double>(i) / kStepsPerEdge;
      double dx = 0, dy = 0;
      switch (edge) {
        case 0: dx = x0 + (x1 - x0) * t; dy = y0; break;  // top
        case 1: dx = x0 + (x1 - x0) * t; dy = y1; break;  // bottom
        case 2: dx = x0; dy = y0 + (y1 - y0) * t; break;  // left
        default: dx = x1; dy = y0 + (y1 - y0) * t; break; // right
      }
      if (rot_deg_ != 0.0) RotateOffsetInverse(dx, dy, &dx, &dy);
      double lat = 0, dlon = 0;
      if (!inverse(dx, dy, &lat, &dlon)) {
        edge_off_globe = true;
        continue;
      }
      add(lat, dlon);
    }
  }

  // The surface runs past the edge of the earth, so the extremes are on the
  // limb rather than on the boundary. The centre is inside the surface, so a
  // boundary point beyond the limb means the limb itself is crossed.
  if (azimuthal && edge_off_globe) {
    if (type_ == ProjectionType::kAzimuthalEquidistant) {
      // The rim IS the antipode: one point that every meridian passes
      // through, so crossing it puts the whole circle of longitude in view.
      s.all_longitudes = true;
      s.pole_on_surface = true;
      add(-pc_.center_lat_for_calculations, 0.0);
    } else {
      constexpr int kLimbSteps = 360;
      for (int i = 0; i < kLimbSteps; ++i) {
        double lat = 0, dlon = 0, px = 0, py = 0;
        PointAtAzimuth(M_PI / 2, 2 * M_PI * i / kLimbSteps, &lat, &dlon);
        OrthoForwardRaw(lat, dlon, &px, &py);  // on the limb either way
        if (on_surface(px, py)) add(lat, dlon);
      }
    }
  }
  return s;
}

void MapProjection::ViewportOffsets(double* x_min_out, double* x_max_out,
                                    double* y_min_out,
                                    double* y_max_out) const {
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
  *x_min_out = x_min;
  *x_max_out = x_max;
  *y_min_out = y_min;
  *y_max_out = y_max;
}

GeoRect MapProjection::VmapBounds() const {
  if (!ready_) return GeoRect{};
  if (type_ == ProjectionType::kLambert) {
    const BoundarySweep s = SweepProjectedBoundary();
    if (!s.valid) return GeoRect{};
    GeoRect r;
    // A pole on the surface is reached from every longitude, so the box is
    // the whole circle and the pole itself is one latitude bound.
    if (s.pole_on_surface) {
      r.ll.lon = -180.0;
      r.ur.lon = 180.0;
      if (s.pole_lat >= 0) {
        r.ur.lat = s.pole_lat;
        r.ll.lat = std::max(s.lat_min, -90.0);
      } else {
        r.ll.lat = s.pole_lat;
        r.ur.lat = std::min(s.lat_max, 90.0);
      }
      return r;
    }
    r.ll.lat = std::max(s.lat_min, -90.0);
    r.ur.lat = std::min(s.lat_max, 90.0);
    const double west = center_.lon + s.dlon_min;
    const double east = center_.lon + s.dlon_max;
    if (east - west >= 360.0) {
      r.ll.lon = -180.0;
      r.ur.lon = 180.0;
    } else {
      r.ll.lon = NormalizeLon(west);
      r.ur.lon = NormalizeLon(east);
    }
    return r;
  }
  if (type_ == ProjectionType::kAzimuthalEquidistant ||
      type_ == ProjectionType::kOrthographic) {
    const BoundarySweep s = SweepProjectedBoundary();
    if (!s.valid) return GeoRect{};
    GeoRect r;
    // A pole on the surface is the latitude bound on its side, whatever the
    // boundary samples reached.
    r.ll.lat = std::max(s.south_pole_on_surface ? -90.0 : s.lat_min, -90.0);
    r.ur.lat = std::min(s.north_pole_on_surface ? 90.0 : s.lat_max, 90.0);
    if (s.all_longitudes || s.dlon_max - s.dlon_min >= 360.0) {
      r.ll.lon = -180.0;
      r.ur.lon = 180.0;
    } else {
      r.ll.lon = NormalizeLon(center_.lon + s.dlon_min);
      r.ur.lon = NormalizeLon(center_.lon + s.dlon_max);
    }
    return r;
  }
  if (type_ != ProjectionType::kEqualArc &&
      type_ != ProjectionType::kMercator)
    return GeoRect{};
  double x_min, x_max, y_min, y_max;
  ViewportOffsets(&x_min, &x_max, &y_min, &y_max);
  if (type_ == ProjectionType::kMercator) {
    // Mercator is separable and monotone on both axes, so the extremes of the
    // pixel box are the extremes of the geographic box.
    GeoRect m;
    double lat = 0, dlon = 0, ignored = 0;
    MercatorInverseRaw(0, y_max, &lat, &ignored);
    m.ll.lat = std::max(lat, -90.0);
    MercatorInverseRaw(0, y_min, &lat, &ignored);
    m.ur.lat = std::min(lat, 90.0);
    MercatorInverseRaw(x_min, 0, &ignored, &dlon);
    const double west = center_.lon + dlon;
    MercatorInverseRaw(x_max, 0, &ignored, &dlon);
    const double east = center_.lon + dlon;
    if (east - west >= 360.0) {
      m.ll.lon = -180.0;
      m.ur.lon = 180.0;
    } else {
      m.ll.lon = NormalizeLon(west);
      m.ur.lon = NormalizeLon(east);
    }
    return m;
  }
  GeoRect r;
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

Status MapProjection::VmapLonRange(double* west, double* east) const {
  if (west == nullptr || east == nullptr)
    return Status::Error(kInvalidArg, "west/east is null");
  if (!ready_) return Status::Error(kInvalidArg, "projection not configured");
  if (type_ == ProjectionType::kLambert) {
    const BoundarySweep s = SweepProjectedBoundary();
    if (!s.valid) return Status::Error(kNotProjectable, "surface is off the cone");
    if (s.pole_on_surface) {
      *west = center_.lon - 180.0;
      *east = center_.lon + 180.0;
      return Status::Ok();
    }
    *west = center_.lon + s.dlon_min;
    *east = center_.lon + s.dlon_max;
    return Status::Ok();
  }
  if (type_ == ProjectionType::kAzimuthalEquidistant ||
      type_ == ProjectionType::kOrthographic) {
    const BoundarySweep s = SweepProjectedBoundary();
    if (!s.valid) return Status::Error(kNotProjectable, "surface is off the earth");
    if (s.all_longitudes) {
      *west = center_.lon - 180.0;
      *east = center_.lon + 180.0;
      return Status::Ok();
    }
    *west = center_.lon + s.dlon_min;
    *east = center_.lon + s.dlon_max;
    return Status::Ok();
  }
  if (type_ != ProjectionType::kEqualArc &&
      type_ != ProjectionType::kMercator)
    return Status::Error(kUnsupported, "projection not implemented");
  double x_min, x_max, y_min, y_max;
  ViewportOffsets(&x_min, &x_max, &y_min, &y_max);
  if (type_ == ProjectionType::kMercator) {
    double ignored = 0, dlon = 0;
    MercatorInverseRaw(x_min, 0, &ignored, &dlon);
    *west = center_.lon + dlon;
    MercatorInverseRaw(x_max, 0, &ignored, &dlon);
    *east = center_.lon + dlon;
    return Status::Ok();
  }
  *west = center_.lon + x_min * dpp_lon_;
  *east = center_.lon + x_max * dpp_lon_;
  return Status::Ok();
}

Status MapProjection::SingularPoint(GeoPoint* p) const {
  if (p == nullptr) return Status::Error(kInvalidArg, "p is null");
  if (!ready_) return Status::Error(kInvalidArg, "projection not configured");
  if (type_ != ProjectionType::kAzimuthalEquidistant)
    return Status::Error(kNotFound, "projection has no singular point");
  p->lat = -pc_.center_lat_for_calculations;
  p->lon = NormalizeLon(center_.lon + 180.0);
  return Status::Ok();
}

/** Whether each pole is inside the surface.
 *
 *  Equal Arc answers from its own bounds — a wide enough viewport runs past
 *  the pole even though the projection has no pole singularity — and Mercator
 *  never does, since it stops at kMercatorMaxLat. The rest ask the boundary
 *  sweep. */
Status MapProjection::PoleOnSurface(bool* north, bool* south) const {
  if (north == nullptr || south == nullptr)
    return Status::Error(kInvalidArg, "north/south is null");
  if (!ready_) return Status::Error(kInvalidArg, "projection not configured");
  *north = false;
  *south = false;
  if (type_ == ProjectionType::kMercator) return Status::Ok();
  if (type_ == ProjectionType::kEqualArc) {
    double x_min, x_max, y_min, y_max;
    ViewportOffsets(&x_min, &x_max, &y_min, &y_max);
    *north = center_.lat - y_min * dpp_lat_ >= 90.0;
    *south = center_.lat - y_max * dpp_lat_ <= -90.0;
    return Status::Ok();
  }
  const BoundarySweep s = SweepProjectedBoundary();
  *north = s.north_pole_on_surface;
  *south = s.south_pole_on_surface;
  return Status::Ok();
}

Status MapProjection::GeoToSurface(const GeoPoint& p, double* sx,
                                   double* sy) const {
  if (sx == nullptr || sy == nullptr)
    return Status::Error(kInvalidArg, "sx/sy is null");
  if (!ready_) return Status::Error(kInvalidArg, "projection not configured");
  if (type_ == ProjectionType::kMercator) {
    if (p.lat < -kMercatorMaxLat || p.lat > kMercatorMaxLat)
      return Status::Error(kNotProjectable, "latitude outside Mercator range");
    double mdx = 0, mdy = 0;
    MercatorForwardRaw(p.lat, UnwrapLonNear(p.lon, center_.lon) - center_.lon,
                       &mdx, &mdy);
    if (rot_deg_ != 0.0) RotateOffset(mdx, mdy, &mdx, &mdy);
    *sx = (width_ - 1) / 2.0 + mdx;
    *sy = (height_ - 1) / 2.0 + mdy;
    return Status::Ok();
  }
  if (type_ == ProjectionType::kLambert) {
    double ldx = 0, ldy = 0;
    LambertForwardRaw(p.lat, UnwrapLonNear(p.lon, center_.lon) - center_.lon,
                      &ldx, &ldy);
    if (!std::isfinite(ldx) || !std::isfinite(ldy))
      return Status::Error(kNotProjectable, "point is off the Lambert cone");
    if (rot_deg_ != 0.0) RotateOffset(ldx, ldy, &ldx, &ldy);
    *sx = (width_ - 1) / 2.0 + ldx;
    *sy = (height_ - 1) / 2.0 + ldy;
    return Status::Ok();
  }
  if (type_ == ProjectionType::kAzimuthalEquidistant ||
      type_ == ProjectionType::kOrthographic) {
    double adx = 0, ady = 0;
    const bool visible =
        type_ == ProjectionType::kOrthographic
            ? OrthoForwardRaw(p.lat, UnwrapLonNear(p.lon, center_.lon) - center_.lon, &adx, &ady)
            : AzEqForwardRaw(p.lat, UnwrapLonNear(p.lon, center_.lon) - center_.lon, &adx, &ady);
    if (!visible || !std::isfinite(adx) || !std::isfinite(ady))
      return Status::Error(kNotProjectable,
                           type_ == ProjectionType::kOrthographic
                               ? "point is on the far hemisphere"
                               : "point is the antipode of the centre");
    if (rot_deg_ != 0.0) RotateOffset(adx, ady, &adx, &ady);
    *sx = (width_ - 1) / 2.0 + adx;
    *sy = (height_ - 1) / 2.0 + ady;
    return Status::Ok();
  }
  if (type_ != ProjectionType::kEqualArc)
    return Status::Error(kUnsupported, "projection not implemented");
  double dlon = UnwrapLonNear(p.lon, center_.lon) - center_.lon;
  double dx = dlon / dpp_lon_;
  double dy = (center_.lat - p.lat) / dpp_lat_;
  if (rot_deg_ != 0.0) RotateOffset(dx, dy, &dx, &dy);
  *sx = (width_ - 1) / 2.0 + dx;
  *sy = (height_ - 1) / 2.0 + dy;
  return Status::Ok();
}

Status MapProjection::GeoToSurfaceUnwrapped(const GeoPoint& p, double* sx,
                                            double* sy) const {
  if (sx == nullptr || sy == nullptr)
    return Status::Error(kInvalidArg, "sx/sy is null");
  if (!ready_) return Status::Error(kInvalidArg, "projection not configured");
  if (type_ == ProjectionType::kMercator) {
    if (p.lat < -kMercatorMaxLat || p.lat > kMercatorMaxLat)
      return Status::Error(kNotProjectable, "latitude outside Mercator range");
    double mdx = 0, mdy = 0;
    MercatorForwardRaw(p.lat, p.lon - center_.lon, &mdx, &mdy);
    if (rot_deg_ != 0.0) RotateOffset(mdx, mdy, &mdx, &mdy);
    *sx = (width_ - 1) / 2.0 + mdx;
    *sy = (height_ - 1) / 2.0 + mdy;
    return Status::Ok();
  }
  if (type_ == ProjectionType::kLambert) {
    double ldx = 0, ldy = 0;
    LambertForwardRaw(p.lat, p.lon - center_.lon, &ldx, &ldy);
    if (!std::isfinite(ldx) || !std::isfinite(ldy))
      return Status::Error(kNotProjectable, "point is off the Lambert cone");
    if (rot_deg_ != 0.0) RotateOffset(ldx, ldy, &ldx, &ldy);
    *sx = (width_ - 1) / 2.0 + ldx;
    *sy = (height_ - 1) / 2.0 + ldy;
    return Status::Ok();
  }
  if (type_ == ProjectionType::kAzimuthalEquidistant ||
      type_ == ProjectionType::kOrthographic) {
    double adx = 0, ady = 0;
    const bool visible =
        type_ == ProjectionType::kOrthographic
            ? OrthoForwardRaw(p.lat, p.lon - center_.lon, &adx, &ady)
            : AzEqForwardRaw(p.lat, p.lon - center_.lon, &adx, &ady);
    if (!visible || !std::isfinite(adx) || !std::isfinite(ady))
      return Status::Error(kNotProjectable,
                           type_ == ProjectionType::kOrthographic
                               ? "point is on the far hemisphere"
                               : "point is the antipode of the centre");
    if (rot_deg_ != 0.0) RotateOffset(adx, ady, &adx, &ady);
    *sx = (width_ - 1) / 2.0 + adx;
    *sy = (height_ - 1) / 2.0 + ady;
    return Status::Ok();
  }
  if (type_ != ProjectionType::kEqualArc)
    return Status::Error(kUnsupported, "projection not implemented");
  double dx = (p.lon - center_.lon) / dpp_lon_;
  double dy = (center_.lat - p.lat) / dpp_lat_;
  if (rot_deg_ != 0.0) RotateOffset(dx, dy, &dx, &dy);
  *sx = (width_ - 1) / 2.0 + dx;
  *sy = (height_ - 1) / 2.0 + dy;
  return Status::Ok();
}

Status MapProjection::SurfaceToGeo(double sx, double sy, GeoPoint* p) const {
  if (p == nullptr) return Status::Error(kInvalidArg, "p is null");
  if (!ready_) return Status::Error(kInvalidArg, "projection not configured");
  if (type_ == ProjectionType::kMercator) {
    double mdx = sx - (width_ - 1) / 2.0;
    double mdy = sy - (height_ - 1) / 2.0;
    if (rot_deg_ != 0.0) RotateOffsetInverse(mdx, mdy, &mdx, &mdy);
    double lat = 0, dlon = 0;
    MercatorInverseRaw(mdx, mdy, &lat, &dlon);
    if (!std::isfinite(lat) || lat < -kMercatorMaxLat || lat > kMercatorMaxLat)
      return Status::Error(kNotProjectable, "pixel outside Mercator range");
    p->lat = lat;
    p->lon = NormalizeLon(center_.lon + dlon);
    return Status::Ok();
  }
  if (type_ == ProjectionType::kLambert) {
    double ldx = sx - (width_ - 1) / 2.0;
    double ldy = sy - (height_ - 1) / 2.0;
    if (rot_deg_ != 0.0) RotateOffsetInverse(ldx, ldy, &ldx, &ldy);
    double lat = 0, dlon = 0;
    if (!LambertInverseRaw(ldx, ldy, &lat, &dlon))
      return Status::Error(kNotProjectable, "pixel is off the Lambert cone");
    p->lat = lat;
    p->lon = NormalizeLon(center_.lon + dlon);
    return Status::Ok();
  }
  if (type_ == ProjectionType::kAzimuthalEquidistant ||
      type_ == ProjectionType::kOrthographic) {
    double adx = sx - (width_ - 1) / 2.0;
    double ady = sy - (height_ - 1) / 2.0;
    if (rot_deg_ != 0.0) RotateOffsetInverse(adx, ady, &adx, &ady);
    double lat = 0, dlon = 0;
    const bool ok = type_ == ProjectionType::kOrthographic
                        ? OrthoInverseRaw(adx, ady, &lat, &dlon)
                        : AzEqInverseRaw(adx, ady, &lat, &dlon);
    if (!ok) return Status::Error(kNotProjectable, "pixel is off the earth");
    p->lat = lat;
    p->lon = NormalizeLon(center_.lon + dlon);
    return Status::Ok();
  }
  if (type_ != ProjectionType::kEqualArc)
    return Status::Error(kUnsupported, "projection not implemented");
  double dx = sx - (width_ - 1) / 2.0;
  double dy = sy - (height_ - 1) / 2.0;
  if (rot_deg_ != 0.0) RotateOffsetInverse(dx, dy, &dx, &dy);
  p->lat = center_.lat - dy * dpp_lat_;
  p->lon = NormalizeLon(center_.lon + dx * dpp_lon_);
  return Status::Ok();
}

/** Ground metres per surface pixel at one point, plus the convergence.
 *
 *  Equal Arc takes the true ground metres per degree at the point; the two
 *  conformal projections take the centre's metres per pixel and scale it by
 *  the point scale factor, so both axes report the same number. Convergence
 *  is Windows' n * delta-lon, but SIGNED: LambertProj::get_convergence takes
 *  the delta through GEO_delta_lon, which drops the sign, so it cannot say
 *  which side of the centre meridian the point is on. A caller turning a
 *  symbol needs that, and no Windows caller is being matched here.
 */
Status MapProjection::LocalScaleAt(const GeoPoint& p, LocalScale* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  if (!ready_) return Status::Error(kInvalidArg, "projection not configured");
  *out = LocalScale{};
  const double lat_rad = p.lat * M_PI / 180.0;

  if (type_ == ProjectionType::kEqualArc) {
    double m_lat = 0, m_lon = 0;
    MetersPerDegree(p.lat, &m_lat, &m_lon);
    out->m_per_px_x = dpp_lon_ * m_lon;
    out->m_per_px_y = dpp_lat_ * m_lat;
    return Status::Ok();
  }

  if (type_ == ProjectionType::kMercator) {
    if (p.lat < -kMercatorMaxLat || p.lat > kMercatorMaxLat)
      return Status::Error(kNotProjectable, "latitude outside Mercator range");
    const double ground_per_plane = std::cos(lat_rad) / pc_.cos_std_parallel;
    out->m_per_px_x = pc_.mosaic_m_per_px_lon * ground_per_plane;
    out->m_per_px_y = pc_.mosaic_m_per_px_lat * ground_per_plane;
    return Status::Ok();
  }

  if (type_ == ProjectionType::kAzimuthalEquidistant ||
      type_ == ProjectionType::kOrthographic)
    return AzimuthalLocalScaleAt(p, out);

  if (type_ != ProjectionType::kLambert)
    return Status::Error(kUnsupported, "projection not implemented");

  const double dlon = UnwrapLonNear(p.lon, center_.lon) - center_.lon;
  if (pc_.lambert_uses_mercator) {
    const double ground_per_plane = std::cos(lat_rad);
    out->m_per_px_x = pc_.mosaic_m_per_px_lon * ground_per_plane;
    out->m_per_px_y = pc_.mosaic_m_per_px_lat * ground_per_plane;
    return Status::Ok();  // the fallback cone is flat: no convergence
  }
  // A parallel of the cone has plane radius rho and ground radius
  // R cos(lat) / n, so ground metres per plane metre is R cos(lat) / (n rho).
  const double rho =
      WGS84_a_METERS * pc_.lambert_F /
      std::pow(std::tan(0.25 * M_PI + 0.5 * lat_rad), pc_.lambert_n);
  const double denom = std::fabs(pc_.lambert_n * rho);
  if (!std::isfinite(rho) || denom == 0.0)
    return Status::Error(kNotProjectable, "point is on the Lambert apex");
  const double ground_per_plane = WGS84_a_METERS * std::cos(lat_rad) / denom;
  out->m_per_px_x = pc_.mosaic_m_per_px_lon * ground_per_plane;
  out->m_per_px_y = pc_.mosaic_m_per_px_lat * ground_per_plane;
  out->convergence_deg = pc_.lambert_n * dlon;
  return Status::Ok();
}

/** LocalScaleAt for the azimuthal pair.
 *
 *  Neither is conformal, so the scale depends on the direction it is measured
 *  in: both have a radial and a transverse principal scale about the
 *  projection centre, and the reported x and y are those two resolved onto the
 *  plane axes. Rotation is not applied — this is a projection property, like
 *  the convergence beside it.
 *
 *  The convergence is exact rather than Windows' delta-lon * sin(lat)
 *  approximation. A great circle through the centre is a straight line
 *  through the plane origin in either projection, so the angle between grid
 *  north and true north at a point is the difference between the plane
 *  bearing of the line back to the centre and the spherical azimuth of the
 *  same direction.
 */
Status MapProjection::AzimuthalLocalScaleAt(const GeoPoint& p,
                                            LocalScale* out) const {
  const double lat_rad = p.lat * M_PI / 180.0;
  const double dlon = UnwrapLonNear(p.lon, center_.lon) - center_.lon;
  const double dlon_rad = dlon * M_PI / 180.0;
  double cos_c = pc_.sin_center_lat * std::sin(lat_rad) +
                 pc_.cos_center_lat * std::cos(lat_rad) * std::cos(dlon_rad);
  cos_c = std::max(-1.0, std::min(1.0, cos_c));
  const double c = std::acos(cos_c);

  // Ground metres per plane metre along and across the radius from the centre.
  double radial = 1.0, transverse = 1.0;
  if (type_ == ProjectionType::kOrthographic) {
    if (cos_c <= 0.0)
      return Status::Error(kNotProjectable,
                           "point is on the far hemisphere or the limb");
    radial = 1.0 / cos_c;  // the plane foreshortens the radius by cos(c)
  } else {
    transverse = c > 0.0 ? std::sin(c) / c : 1.0;
  }

  double px = 0, py = 0;
  if (type_ == ProjectionType::kOrthographic)
    OrthoForwardRaw(p.lat, dlon, &px, &py);
  else if (!AzEqForwardRaw(p.lat, dlon, &px, &py))
    return Status::Error(kNotProjectable, "point is the antipode of the centre");
  const double r = std::hypot(px, py);
  if (r == 0.0) {  // at the centre both principal scales are 1
    out->m_per_px_x = pc_.mosaic_m_per_px_lon;
    out->m_per_px_y = pc_.mosaic_m_per_px_lat;
    return Status::Ok();
  }
  const double ux = px / r, uy = py / r;  // the radial direction in the plane
  out->m_per_px_x = pc_.mosaic_m_per_px_lon *
                    std::hypot(ux * radial, uy * transverse);
  out->m_per_px_y = pc_.mosaic_m_per_px_lat *
                    std::hypot(uy * radial, ux * transverse);

  // Where a step due north lands on the plane. Neither projection is
  // conformal, so this cannot be read off the angle between the meridian and
  // the line back to the centre: the two principal scales turn it as well.
  // North is written in the radial/transverse frame at the point, each
  // component divided by its own scale, and read back as a plane bearing.
  const double back = -dlon_rad;  // centre longitude minus the point's
  const double az_to_centre =
      std::atan2(pc_.cos_center_lat * std::sin(back),
                 std::cos(lat_rad) * pc_.sin_center_lat -
                     std::sin(lat_rad) * pc_.cos_center_lat * std::cos(back));
  const double theta = -(az_to_centre + M_PI);  // radial-outward to north
  const double vx = std::cos(theta) / radial * ux +
                    std::sin(theta) / transverse * -uy;
  const double vy = std::cos(theta) / radial * uy +
                    std::sin(theta) / transverse * ux;
  double conv = -std::atan2(vx, -vy) * 180.0 / M_PI;
  while (conv > 180.0) conv -= 360.0;
  while (conv <= -180.0) conv += 360.0;
  out->convergence_deg = conv;
  return Status::Ok();
}

}  // namespace fv
