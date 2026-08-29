// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Ported from fvw_core/FvMappingGraphics/GeographicContourIterator.cpp.
// See fvkit/geo/contour.h for the seam and the reasoning; this file is the
// arithmetic, and it is preserved (bit-faithful rule) rather than rederived.

#include "fvkit/geo/contour.h"

#include <algorithm>
#include <cmath>

#include "geo_tool.h"  // fv_geo_tool: bounds checks, distance, Mercator

namespace fv {
namespace {

// FvMappingGraphicsInclude.h's EARTH_RADIUS, in km. The great-circle math
// treats the earth as a perfect sphere of this radius; the value cancels out
// of every angle it computes, and survives only in the "is this vector really
// on the sphere" sanity checks.
constexpr double kEarthRadiusKm = 6378.0;

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.2831853071795862;
constexpr double kHalfPi = 1.57079632679489661923;

constexpr double kDegToRad = 1.7453292519943295e-2;
constexpr double kRadToDeg = 57.295779513082322;

double Deg(double radians) { return radians * kRadToDeg; }
double Rad(double degrees) { return degrees * kDegToRad; }

// The original steps a great circle in 20-pixel chords and reduces the step
// fivefold in the polar regions, where a constant angular step covers far
// more screen. Both numbers are preserved.
constexpr double kPolarLatitude = 70.0;
constexpr double kPolarStepDivisor = 5.0;

}  // namespace

// ---------------------------------------------------------------------------
// SimpleGeoLine
// ---------------------------------------------------------------------------

SimpleGeoLine::SimpleGeoLine(const MapProjection& proj, const GeoPoint& a,
                             const GeoPoint& b, bool clip)
    : a_(a), b_(b) {
  if (!clip) return;

  // The original borrows the rhumb clipper rather than writing a second one.
  // Note it takes only the FIRST sub-line: a simple line that the clipper
  // split in two (a viewport wrapping the world) is not representable as two
  // endpoints, and the original says so — "wrap around the world case is not
  // handled by this class".
  RhumbLineContour r(proj, a, b, true);
  GeoPoint pts[4];
  int count = 0;
  r.ClippedPoints(pts, &count);
  if (count == 0) {
    clipped_away_ = true;
    return;
  }
  a_ = pts[0];
  b_ = pts[1];
}

void SimpleGeoLine::MoveFirst() { index_ = -1; }

bool SimpleGeoLine::NextPoint(GeoPoint* p) {
  if (clipped_away_) return false;
  ++index_;
  if (index_ == 0) {
    *p = a_;
    return true;
  }
  if (index_ == 1) {
    *p = b_;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// GreatCircleContour
// ---------------------------------------------------------------------------

namespace {

// lat/lon -> a vector on the sphere. The z axis is the axis of rotation, the
// x axis runs to (0N, 0E).
void LatLonToXyz(double lat, double lon, double* x, double* y, double* z) {
  if (lon < 0.0) lon += 360.0;
  const double theta = Rad(lon);
  const double phi = Rad(90.0 - lat);
  const double temp = kEarthRadiusKm * std::sin(phi);
  *x = std::cos(theta) * temp;
  *y = std::sin(theta) * temp;
  *z = kEarthRadiusKm * std::cos(phi);
}

bool XyzToLatLon(double x, double y, double z, double* lat, double* lon) {
  if (z > kEarthRadiusKm || -z > kEarthRadiusKm) return false;
  *lat = 90.0 - Deg(std::acos(z / kEarthRadiusKm));
  const double r = std::sqrt(x * x + y * y);
  if (r > 0.0) {
    *lon = Deg(std::acos(x / r));
    if (y < 0.0) *lon = -(*lon);
  } else {
    *lon = 0.0;  // a pole; longitude is arbitrary
  }
  return true;
}

// The CCW angle of (x, y) about the origin, in [0, 2pi). Written as the
// original's quadrant ladder rather than atan2 because the two disagree at
// the axes on which quadrant they claim, and the crossover search compares
// these angles against each other.
double CcwAngle(double x, double y) {
  if (x == 0.0) return y < 0.0 ? kPi + kHalfPi : kHalfPi;
  if (x > 0.0) {
    if (y < 0.0) return std::atan(y / x) + kTwoPi;  // 4th quadrant
    return std::atan(y / x);                        // 1st quadrant
  }
  return std::atan(y / x) + kPi;  // 2nd and 3rd quadrants
}

// beta (the angle between P1 and P2) and the two rotations that put the
// normal of the P1-P2 plane on the z axis.
bool GreatCircleParameters(double x1, double y1, double z1, double x2,
                           double y2, double z2, double* beta,
                           double* rotate_x, double* rotate_y) {
  double length = std::sqrt(x1 * x1 + y1 * y1 + z1 * z1);
  if (length < (kEarthRadiusKm - 1.0) || length > (kEarthRadiusKm + 1.0))
    return false;
  length = std::sqrt(x2 * x2 + y2 * y2 + z2 * z2);
  if (length < (kEarthRadiusKm - 1.0) || length > (kEarthRadiusKm + 1.0))
    return false;

  // N = P1 x P2
  double nx = y1 * z2 - z1 * y2;
  double ny = z1 * x2 - x1 * z2;
  double nz = x1 * y2 - y1 * x2;
  length = std::sqrt(nx * nx + ny * ny + nz * nz);

  // Antipodal points: infinitely many great circles join them, and the cross
  // product is zero. The original picks the one through (0N, 0E).
  if (length < 0.000001) {
    *beta = kPi;
    length = std::sqrt(z1 * z1 + y1 * y1);
    if (length > 0.0) {
      *rotate_y = (y1 < 0.0) ? 0.0 : kPi;
      if (y1 <= 0.0) {
        *rotate_x = std::asin(z1 / length);
      } else if (z1 < 0.0) {
        *rotate_x = -(kPi + std::asin(z1 / length));
      } else {
        *rotate_x = kPi - std::asin(z1 / length);
      }
      return true;
    }
    // P1 is on the x axis; go through (0N, 90E).
    *rotate_y = 0.0;
    *rotate_x = 0.0;
    return true;
  }

  const double cos_beta =
      (x1 * x2 + y1 * y2 + z1 * z2) / (kEarthRadiusKm * kEarthRadiusKm);
  constexpr double kEps = 1e-15;
  if (cos_beta < 1 + kEps && cos_beta > 1 - kEps) {
    *beta = 0;
  } else if (cos_beta < -1 + kEps && cos_beta > -1 - kEps) {
    *beta = kPi;
  } else {
    *beta = std::acos(cos_beta);
  }

  nx /= length;
  ny /= length;
  nz /= length;

  *rotate_x = std::asin(ny);
  *rotate_y = (nz == 0.0) ? kHalfPi
                          : std::acos(nz / std::sqrt(nx * nx + nz * nz));
  if (nx < 0.0) *rotate_y = -(*rotate_y);
  return true;
}

}  // namespace

GreatCircleContour::GreatCircleContour(const MapProjection& proj,
                                       const GeoPoint& a, const GeoPoint& b,
                                       bool clip)
    : a_(a), b_(b), clip_(clip) {
  const double dlon = std::fabs(a_.lon - b_.lon);
  degenerate_ = (dlon < 1e-6) || (dlon > (360.0 - 1e-6));

  bounds_ = proj.VmapBounds();
  center_ = proj.Center();

  if (!degenerate_) {
    if (!ComputeAngles() || !ComputeDeltaAngle(proj)) {
      // Fall back to a point-to-point line rather than drawing nothing.
      degenerate_ = true;
    }
  } else if (clip_) {
    // A line of longitude is clipped in latitude only, to an apron 50 pixels
    // outside the map. The apron is the original's, and it exists because a
    // line style with an end cap (an arrowhead) inks outside its own endpoint.
    const double dpp_lat = proj.DegPerPixelLat();
    const double lower = std::max(bounds_.ll.lat - 50 * dpp_lat, -90.0);
    const double upper = std::min(bounds_.ur.lat + 50 * dpp_lat, 90.0);

    if ((a_.lat < lower && b_.lat < lower) ||
        (a_.lat > upper && b_.lat > upper)) {
      a_.lat = b_.lat;  // collapses to a point: nothing visible
    } else if (a_.lat < b_.lat) {
      a_.lat = std::max(a_.lat, lower);
      b_.lat = std::min(b_.lat, upper);
    } else {
      a_.lat = std::min(a_.lat, upper);
      b_.lat = std::max(b_.lat, lower);
    }
  }
}

bool GreatCircleContour::ComputeAngles() {
  double x1, y1, z1, x2, y2, z2;
  LatLonToXyz(a_.lat, a_.lon, &x1, &y1, &z1);
  LatLonToXyz(b_.lat, b_.lon, &x2, &y2, &z2);

  if (!GreatCircleParameters(x1, y1, z1, x2, y2, z2, &beta_, &rotate_x_,
                             &rotate_y_))
    return false;

  cos_rx_ = std::cos(rotate_x_);
  cos_ry_ = std::cos(rotate_y_);
  sin_rx_ = std::sin(rotate_x_);
  sin_ry_ = std::sin(rotate_y_);

  // P1 rotated into the x'-y' plane; its angle there is where the walk starts.
  double x, y, z;
  ForwardConversion(x1, y1, z1, &x, &y, &z);
  angle_p1_ = CcwAngle(x, y);
  return true;
}

bool GreatCircleContour::ComputeDeltaAngle(const MapProjection& proj) {
  const double dpp_lat = proj.DegPerPixelLat();
  const double dpp_lon = proj.DegPerPixelLon();

  // Guard from the original: GEO_geo_to_distance returns 0 for very small
  // separations, which would make delta_angle 0 and the walk never terminate.
  if (dpp_lon < 2.0e-006) {
    delta_angle_ = 1.0e-7;
    return true;
  }

  // One pixel's height in km. Stepping toward the pole would run past it at
  // an extreme centre latitude, so the step is taken the other way there.
  double center_lat = center_.lat;
  const double center_lon = center_.lon;
  double end_lat;
  if (center_lat >= 0.0) {
    end_lat = center_lat + dpp_lat;
    if (end_lat > 90.0) {
      end_lat = center_lat;
      center_lat = center_lat - dpp_lat;
    }
  } else {
    end_lat = center_lat - dpp_lat;
    if (end_lat < -90.0) {
      end_lat = center_lat;
      center_lat = center_lat + dpp_lat;
    }
  }

  double pixel_height = 0.0, bearing = 0.0;
  if (GEO_geo_to_distance(center_lat, center_lon, end_lat, center_lon,
                          &pixel_height, &bearing) != SUCCESS)
    return false;
  pixel_height *= 0.001;

  double lon = center_.lon + dpp_lon;
  if (lon > 180.0) lon -= 360.0;

  double pixel_width = 0.0;
  if (GEO_geo_to_distance(center_.lat, center_.lon, center_.lat, lon,
                          &pixel_width, &bearing) != SUCCESS)
    return false;
  pixel_width *= 0.001;

  // Roughly 20-pixel chords.
  delta_angle_ = 10.0 * (pixel_width + pixel_height) / kEarthRadiusKm;
  return delta_angle_ > 0.0;
}

void GreatCircleContour::ForwardConversion(double x, double y, double z,
                                           double* xo, double* yo,
                                           double* zo) const {
  *xo = x * cos_ry_ - z * sin_ry_;
  *yo = -x * sin_rx_ * sin_ry_ + y * cos_rx_ - z * sin_rx_ * cos_ry_;
  *zo = x * cos_rx_ * sin_ry_ + y * sin_rx_ + z * cos_rx_ * cos_ry_;
}

void GreatCircleContour::ReverseConversion(double x, double y, double* xo,
                                           double* yo, double* zo) const {
  *xo = x * cos_ry_ - y * sin_ry_ * sin_rx_;
  *yo = y * cos_rx_;
  *zo = -x * sin_ry_ - y * cos_ry_ * sin_rx_;
}

bool GreatCircleContour::IntermediateGeo(double beta, GeoPoint* out) const {
  const double x = kEarthRadiusKm * std::cos(angle_p1_ + beta);
  const double y = kEarthRadiusKm * std::sin(angle_p1_ + beta);
  double xo, yo, zo;
  ReverseConversion(x, y, &xo, &yo, &zo);
  return XyzToLatLon(xo, yo, zo, &out->lat, &out->lon);
}

bool GreatCircleContour::AngleAt(double lat, double lon, double* angle,
                                 double* z_out) const {
  double x, y, z, xr, yr, zr;
  LatLonToXyz(lat, lon, &x, &y, &z);
  ForwardConversion(x, y, z, &xr, &yr, &zr);
  *angle = CcwAngle(xr, yr);
  // Normalized relative to P1, so the search walks forward along the arc.
  if (*angle < angle_p1_) *angle += kTwoPi;
  *z_out = zr;
  return true;
}

// A first guess at the angle where the arc crosses into the map.
//
// The clip test: the P1-P2 plane splits the globe in two, and in the rotated
// frame a point's z' sign says which half it is in. Two points with opposite
// z' signs must have the great circle between them cross the P1-P2 great
// circle. That is conclusive for a line of longitude (itself a great circle)
// and only one-directional for a line of latitude, which is why the northern
// and southern edges are each split in half below.
//
// Returns -1.0 when the arc provably misses the map.
double GreatCircleContour::ComputeStartingAngle() const {
  double ll_angle, ul_angle, corner_angle;
  double ll_z, ul_z, corner_z;

  AngleAt(bounds_.ll.lat, bounds_.ll.lon, &ll_angle, &ll_z);
  AngleAt(bounds_.ur.lat, bounds_.ll.lon, &ul_angle, &ul_z);

  if (ll_z == 0.0 && ul_z != 0.0) return ll_angle - angle_p1_;
  if (ul_z == 0.0 && ll_z != 0.0) return ul_angle - angle_p1_;

  // Western map boundary crossover.
  if ((ll_z >= 0.0 && ul_z <= 0.0) || (ll_z <= 0.0 && ul_z >= 0.0)) {
    const double ll_norm = ll_angle >= kTwoPi ? ll_angle - kTwoPi : ll_angle;
    const double ul_norm = ul_angle >= kTwoPi ? ul_angle - kTwoPi : ul_angle;
    if (ll_norm < ul_norm) {
      // When angle_p1 falls between the corners it is the only safe choice —
      // ll_angle could be past the end of the line.
      if (ll_norm < angle_p1_ && angle_p1_ < ul_norm) return 0.0;
      return ll_angle - angle_p1_;
    }
    if (ul_norm < angle_p1_ && angle_p1_ < ll_norm) return 0.0;
    return ul_angle - angle_p1_;
  }

  // North pole, then the northern edge in two halves.
  AngleAt(90.0, bounds_.ll.lon, &corner_angle, &corner_z);
  if ((corner_z >= 0.0 && ul_z <= 0.0) || (corner_z <= 0.0 && ul_z >= 0.0)) {
    AngleAt(center_.lat, center_.lon, &corner_angle, &corner_z);
    if (corner_z == 0.0 && ul_z != 0.0) return corner_angle - angle_p1_;
    if ((corner_z >= 0.0 && ul_z <= 0.0) || (corner_z <= 0.0 && ul_z >= 0.0))
      return std::min(corner_angle, ul_angle) - angle_p1_;

    ul_z = corner_z;
    ul_angle = corner_angle;
    AngleAt(bounds_.ur.lat, bounds_.ur.lon, &corner_angle, &corner_z);
    if (corner_z == 0.0 && ul_z != 0.0) return corner_angle - angle_p1_;
    if ((corner_z >= 0.0 && ul_z <= 0.0) || (corner_z <= 0.0 && ul_z >= 0.0))
      return std::min(corner_angle, ul_angle) - angle_p1_;
    return -1.0;
  }

  // South pole, then the southern edge in two halves.
  AngleAt(-90.0, bounds_.ll.lon, &corner_angle, &corner_z);
  if ((corner_z >= 0.0 && ll_z <= 0.0) || (corner_z <= 0.0 && ll_z >= 0.0)) {
    AngleAt(center_.lat, center_.lon, &corner_angle, &corner_z);
    if (corner_z == 0.0 && ll_z != 0.0) return corner_angle - angle_p1_;
    if ((corner_z >= 0.0 && ll_z <= 0.0) || (corner_z <= 0.0 && ll_z >= 0.0))
      return std::min(corner_angle, ll_angle) - angle_p1_;

    ll_z = corner_z;
    ll_angle = corner_angle;
    AngleAt(bounds_.ll.lat, bounds_.ur.lon, &corner_angle, &corner_z);
    if (corner_z == 0.0 && ll_z != 0.0) return corner_angle - angle_p1_;
    if ((corner_z >= 0.0 && ll_z <= 0.0) || (corner_z <= 0.0 && ll_z >= 0.0))
      return std::min(corner_angle, ll_angle) - angle_p1_;
    return -1.0;
  }

  // A line of longitude (handled elsewhere), the equator, or a world
  // overview whose starting point is already on the map.
  return 0.0;
}

void GreatCircleContour::MoveFirst() {
  at_end_ = false;
  degenerate_count_ = 0;
  current_ = a_;

  if (degenerate_) return;

  const bool start_on_map =
      GEO_in_bounds(bounds_.ll.lat, bounds_.ll.lon, bounds_.ur.lat,
                    bounds_.ur.lon, current_.lat, current_.lon) != FALSE;

  if (start_on_map || !clip_) {
    if (delta_angle_ < beta_) {
      angle_ = (current_.lat > kPolarLatitude || current_.lat < -kPolarLatitude)
                   ? delta_angle_ / kPolarStepDivisor
                   : delta_angle_;
    } else {
      angle_ = beta_;  // shorter than one step: a straight line will do
      degenerate_ = true;
    }
    return;
  }

  // --- the entry search ----------------------------------------------------
  //
  // P1 is off the map. Walk forward along the arc until a point lands within
  // one step of the map, halving the step whenever a jump skips a corner.
  // This is what makes an intercontinental arc cheap on a harbour map, and
  // its failure mode is silent (nothing drawn), so it is preserved verbatim.

  // 1/100 of the arc to start, so the search is coarse where it can afford to
  // be, but never coarser than one drawn step.
  double step = beta_ / 100.0;
  if (step < delta_angle_) {
    if (delta_angle_ < beta_ / 2.0) {
      step = delta_angle_;
    } else {
      step = delta_angle_ = beta_ / 2.0;
    }
  }
  // Jumping the map even at this step means the arc does not meet it.
  const double min_step = delta_angle_ / 16.0;

  angle_ = ComputeStartingAngle();
  if (angle_ == -1.0) {
    at_end_ = true;
    return;
  }
  if (angle_ < 0.0) angle_ = 0.0;
  if (angle_ >= beta_) angle_ = beta_ - step;

  int old_flags =
      GEO_bounds_check_degrees(bounds_.ll.lat, bounds_.ll.lon, bounds_.ur.lat,
                               bounds_.ur.lon, current_.lat, current_.lon);

  if (!IntermediateGeo(angle_, &current_)) {
    at_end_ = true;
    return;
  }
  int new_flags =
      GEO_bounds_check_degrees(bounds_.ll.lat, bounds_.ll.lon, bounds_.ur.lat,
                               bounds_.ur.lon, current_.lat, current_.lon);

  int xor_flags = old_flags ^ new_flags;
  if (xor_flags != 0) {
    double geo_width = bounds_.ur.lon - bounds_.ll.lon;
    if (geo_width < 0.0) geo_width += 360.0;

    // One point north of the map and the other south of it, on a wide map:
    // start the search from the first step instead of from the guess.
    if ((xor_flags & (GEO_NORTH_OF | GEO_SOUTH_OF)) && geo_width >= 180.0) {
      angle_ = delta_angle_;
      if (!IntermediateGeo(angle_, &current_)) {
        at_end_ = true;
        return;
      }
      new_flags = GEO_bounds_check_degrees(bounds_.ll.lat, bounds_.ll.lon,
                                           bounds_.ur.lat, bounds_.ur.lon,
                                           current_.lat, current_.lon);
    }
  }

  // Starting east of the map with an endpoint not east of its western edge:
  // the arc ran away from the map.
  if ((new_flags & GEO_EAST_OF) &&
      GEO_east_of_degrees(bounds_.ll.lon, b_.lon) != FALSE) {
    at_end_ = true;
    return;
  }

  // The guess landed ON the map: back off until it is outside again, so the
  // walk below enters from a point the caller can see the line arriving from.
  if (new_flags == 0) {
    if (step > 10.0 * delta_angle_) step = 10.0 * delta_angle_;
    if (angle_ < step) step = angle_;
    do {
      angle_ -= step;
      if (!IntermediateGeo(angle_, &current_)) {
        at_end_ = true;
        return;
      }
      new_flags = GEO_bounds_check_degrees(bounds_.ll.lat, bounds_.ll.lon,
                                           bounds_.ur.lat, bounds_.ur.lon,
                                           current_.lat, current_.lon);
    } while (new_flags == 0 && angle_ > 0.0);
    if (angle_ < 0.0) angle_ = 0.0;
    step = delta_angle_;
  }

  // Walk forward to the first point within one step of a map edge.
  old_flags = new_flags;
  GeoPoint prev = current_;
  while (old_flags != 0 && angle_ < beta_) {
    angle_ += step;
    prev = current_;
    if (angle_ > beta_) angle_ = beta_;

    if (!IntermediateGeo(angle_, &current_)) {
      at_end_ = true;
      return;
    }
    new_flags =
        GEO_bounds_check_degrees(bounds_.ll.lat, bounds_.ll.lon, bounds_.ur.lat,
                                 bounds_.ur.lon, current_.lat, current_.lon);
    xor_flags = old_flags ^ new_flags;

    if (xor_flags != 0) {
      if (new_flags == 0) {
        // At one step or less, the PREVIOUS point is the last one outside,
        // and it is where the drawn line should start.
        if (step <= delta_angle_) {
          current_ = prev;
          if (current_.lat > kPolarLatitude || current_.lat < -kPolarLatitude) {
            angle_ -= step;
            angle_ += delta_angle_ / kPolarStepDivisor;
          }
          break;
        }
        if (step > 10.0 * delta_angle_) step = 10.0 * delta_angle_;
        do {
          angle_ -= step;
          if (!IntermediateGeo(angle_, &current_)) {
            at_end_ = true;
            return;
          }
          new_flags = GEO_bounds_check_degrees(
              bounds_.ll.lat, bounds_.ll.lon, bounds_.ur.lat, bounds_.ur.lon,
              current_.lat, current_.lon);
        } while (new_flags == 0);
        // Small enough to step back on without jumping a corner.
        if (step > delta_angle_) step = delta_angle_;
      } else {
        // Crossed the map's eastern longitude without touching any other
        // edge: this arc passes the map by.
        if (xor_flags == GEO_EAST_OF &&
            (old_flags == GEO_NORTH_OF || old_flags == GEO_SOUTH_OF)) {
          at_end_ = true;
          return;
        }
        // Still outside, but in a new region: keep going.
        if (xor_flags == GEO_NORTH_OF || xor_flags == GEO_SOUTH_OF ||
            xor_flags == GEO_WEST_OF) {
          old_flags = new_flags;
          continue;
        }
        // East to west half a world away is not a crossing.
        if ((old_flags & GEO_EAST_OF) && (new_flags & GEO_WEST_OF)) {
          old_flags = new_flags;
          continue;
        }
        // A corner jumped even at the minimum step: no intersection.
        if (step == min_step) {
          at_end_ = true;
          return;
        }
        angle_ -= step;
        step /= 2.0;
        if (step < min_step) step = min_step;
        current_ = prev;
        new_flags = old_flags;
      }
    }
    old_flags = new_flags;
  }

  if (angle_ == beta_) at_end_ = true;
}

bool GreatCircleContour::NextPoint(GeoPoint* p) {
  if (at_end_) return false;

  if (degenerate_) {
    if (degenerate_count_ == 0) {
      *p = a_;
      degenerate_count_++;
      return true;
    }
    *p = b_;
    at_end_ = true;
    return true;
  }

  if (angle_ < beta_) {
    *p = current_;
    if (!IntermediateGeo(angle_, &current_)) {
      at_end_ = true;
      return true;  // the point already yielded is still good
    }
    angle_ += (current_.lat > kPolarLatitude || current_.lat < -kPolarLatitude)
                  ? delta_angle_ / kPolarStepDivisor
                  : delta_angle_;
    return true;
  }

  *p = b_;
  at_end_ = true;
  return true;
}

// ---------------------------------------------------------------------------
// RhumbLineContour
// ---------------------------------------------------------------------------

struct RhumbLineContour::Impl {
  // Composed, not inherited: the original derives privately from Mercator,
  // which makes every lat_to_y call look like a member and hides that the
  // projection is re-centred per line.
  Mercator merc{0.0};

  GeoPoint start, end;
  GeoPoint ll, ur;
  double west_x = 0.0, east_x = 0.0, south_y = 0.0, north_y = 0.0;
  double dpp_lat = 0.0, dpp_lon = 0.0;

  GeoPoint clipped[4];
  int line_count = 0;

  // Walk state.
  int current_line = -2;
  bool init_next_line = true;
  double current_t = 0.0, delta_t = 1.0;
  double cur_lat = 0.0, cur_lon = 0.0;
  bool changing_lat = false;
  double x1 = 0.0, y1 = 0.0, dx = 0.0, dy = 0.0;
  int num_steps = 0, step = 0;

  bool SetClipBounds(GeoPoint lower_left, GeoPoint upper_right);
  bool WrapAround() const { return east_x <= west_x; }
  double LatAtLon(double lon);
  bool GetClippedPoints(GeoPoint points[4], int* count, bool clip);
  bool GetClippedPoints2(GeoPoint points[4], int* count, bool clip);
  bool ClipLine(int xor_flags, GeoPoint* p1, GeoPoint* p2);
  static bool ClipT(double denom, double num, double* te, double* tl);
};

bool RhumbLineContour::Impl::SetClipBounds(GeoPoint lower_left,
                                           GeoPoint upper_right) {
  if (upper_right.lat <= lower_left.lat) return false;

  // Mercator cannot represent the poles.
  if (upper_right.lat > 89.99) upper_right.lat = 89.99;
  if (lower_left.lat < -89.99) lower_left.lat = -89.99;
  if (upper_right.lat <= lower_left.lat) return false;

  ll = lower_left;
  ur = upper_right;
  west_x = merc.lon_to_x(ll.lon);
  south_y = merc.lat_to_y(ll.lat);
  east_x = merc.lon_to_x(ur.lon);
  north_y = merc.lat_to_y(ur.lat);
  return true;
}

double RhumbLineContour::Impl::LatAtLon(double lon) {
  const double ax = merc.lon_to_x(start.lon);
  const double ay = merc.lat_to_y(start.lat);
  const double bx = merc.lon_to_x(end.lon);
  const double by = merc.lat_to_y(end.lat);
  const double run = bx - ax;
  if (run != 0.0) {
    const double m = (by - ay) / run;
    const double b = ay - m * ax;
    return merc.y_to_lat(m * merc.lon_to_x(lon) + b);
  }
  return start.lat;
}

// Liang-Barsky in Mercator space. `xor_flags` names the edges the segment
// actually crossed, so an edge it never met costs nothing.
bool RhumbLineContour::Impl::ClipLine(int xor_flags, GeoPoint* p1,
                                      GeoPoint* p2) {
  double ax = merc.lon_to_x(p1->lon);
  double ay = merc.lat_to_y(p1->lat);
  double bx = merc.lon_to_x(p2->lon);
  double by = merc.lat_to_y(p2->lat);

  const double run = bx - ax;
  const double rise = by - ay;
  double te = 0.0, tl = 1.0;

  if (xor_flags & GEO_WEST_OF) ClipT(run, west_x - ax, &te, &tl);
  if (xor_flags & GEO_EAST_OF) ClipT(-run, ax - east_x, &te, &tl);
  if (xor_flags & GEO_SOUTH_OF) ClipT(rise, south_y - ay, &te, &tl);
  if (xor_flags & GEO_NORTH_OF) ClipT(-rise, ay - north_y, &te, &tl);

  if (tl < 1.0) {
    bx = ax + tl * run;
    by = ay + tl * rise;
  }
  if (te > 0.0) {
    ax = ax + te * run;
    ay = ay + te * rise;
  }

  p1->lat = merc.y_to_lat(ay);
  p1->lon = merc.x_to_lon(ax);
  p2->lat = merc.y_to_lat(by);
  p2->lon = merc.x_to_lon(bx);
  return true;
}

bool RhumbLineContour::Impl::ClipT(double denom, double num, double* te,
                                   double* tl) {
  // BIT-FAITHFUL: the original computes t in SINGLE precision
  // ((float)num/(float)denom) even though everything around it is double.
  // That is a real loss of precision on a long line, and it is preserved
  // because the clipped endpoints it produces are what the Windows product
  // draws.
  if (denom > 0.0) {
    const double t = static_cast<double>(static_cast<float>(num) /
                                         static_cast<float>(denom));
    if (t > *tl) return false;
    if (t > *te) *te = t;
  } else if (denom < 0.0) {
    const double t = static_cast<double>(static_cast<float>(num) /
                                         static_cast<float>(denom));
    if (t < *te) return false;
    if (t < *tl) *tl = t;
  } else if (num > 0.0) {
    return false;  // parallel to the edge and outside it
  }
  return true;
}

bool RhumbLineContour::Impl::GetClippedPoints(GeoPoint points[4], int* count,
                                              bool clip) {
  const int start_flags = GEO_bounds_check_degrees(
      ll.lat, ll.lon, ur.lat, ur.lon, start.lat, start.lon);
  const int end_flags =
      GEO_bounds_check_degrees(ll.lat, ll.lon, ur.lat, ur.lon, end.lat, end.lon);

  // Both ends off the map the same way: no crossing is possible.
  if (start_flags & end_flags) {
    if (clip) {
      *count = 0;
      return false;
    }
    *count = 1;
    points[0] = start;
    points[1] = end;
    return true;
  }

  // Both ends on the map — but the line may still leave and come back if the
  // viewport wraps the world.
  if ((start_flags | end_flags) == 0) {
    const bool lon_in_range =
        (GEO_east_of_degrees(end.lon, start.lon) != FALSE)
            ? GEO_lon_in_range(start.lon, end.lon, ur.lon) != FALSE
            : GEO_lon_in_range(end.lon, start.lon, ur.lon) != FALSE;

    if (WrapAround() && lon_in_range) {
      *count = 2;
      const bool east = GEO_east_of_degrees(end.lon, start.lon) != FALSE;
      const double first_edge = east ? ur.lon : ll.lon;
      const double second_edge = east ? ll.lon : ur.lon;

      points[0] = start;
      points[1] = {LatAtLon(first_edge), first_edge};
      points[2] = {LatAtLon(second_edge), second_edge};
      points[3] = end;

      // On a line of constant latitude LatAtLon carries round-off that can
      // move the intermediate points a whole surface pixel. Pin them.
      if (start.lat == end.lat) points[1].lat = points[2].lat = end.lat;
      return true;
    }
    *count = 1;
    points[0] = start;
    points[1] = end;
    return true;
  }

  // The viewport wraps: clip against each half separately, which yields one
  // sub-line per half.
  if (WrapAround()) {
    GeoPoint tmp[4];
    int tmp_count = 0;
    *count = 0;

    const double saved_west = west_x;
    west_x = merc.get_min_x();
    if (GetClippedPoints2(tmp, &tmp_count, clip)) {
      *count = 1;
      points[0] = tmp[0];
      points[1] = tmp[1];
    }
    west_x = saved_west;

    const double saved_east = east_x;
    east_x = merc.get_max_x();
    if (GetClippedPoints2(tmp, &tmp_count, clip)) {
      const int i = 2 * (*count);
      (*count)++;
      points[i] = tmp[0];
      points[i + 1] = tmp[1];
    }
    east_x = saved_east;

    return *count != 0;
  }

  points[0] = start;
  points[1] = end;
  if (clip) ClipLine(start_flags ^ end_flags, &points[0], &points[1]);
  *count = 1;
  return true;
}

// The same walk with the wrap branch inverted: called only from inside the
// wrap handling above, where the bounds have already been un-wrapped on one
// side, so a still-wrapping region means there is nothing to clip against.
bool RhumbLineContour::Impl::GetClippedPoints2(GeoPoint points[4], int* count,
                                               bool clip) {
  const int start_flags = GEO_bounds_check_degrees(
      ll.lat, ll.lon, ur.lat, ur.lon, start.lat, start.lon);
  const int end_flags =
      GEO_bounds_check_degrees(ll.lat, ll.lon, ur.lat, ur.lon, end.lat, end.lon);

  if (start_flags & end_flags) {
    if (clip) {
      *count = 0;
      return false;
    }
    *count = 1;
    points[0] = start;
    points[1] = end;
    return true;
  }

  if ((start_flags | end_flags) == 0) {
    const bool lon_in_range =
        (GEO_east_of_degrees(end.lon, start.lon) != FALSE)
            ? GEO_lon_in_range(start.lon, end.lon, ur.lon) != FALSE
            : GEO_lon_in_range(end.lon, start.lon, ur.lon) != FALSE;

    if (WrapAround() && lon_in_range) {
      *count = 2;
      const bool east = GEO_east_of_degrees(end.lon, start.lon) != FALSE;
      const double first_edge = east ? ur.lon : ll.lon;
      const double second_edge = east ? ll.lon : ur.lon;

      points[0] = start;
      points[1] = {LatAtLon(first_edge), first_edge};
      points[2] = {LatAtLon(second_edge), second_edge};
      points[3] = end;
      if (start.lat == end.lat) points[1].lat = points[2].lat = end.lat;
      return true;
    }
    *count = 1;
    points[0] = start;
    points[1] = end;
    return true;
  }

  if (!WrapAround()) {
    points[0] = start;
    points[1] = end;
    if (clip) ClipLine(start_flags ^ end_flags, &points[0], &points[1]);
    *count = 1;
    return true;
  }

  *count = 0;
  return false;
}

RhumbLineContour::RhumbLineContour(const MapProjection& proj, const GeoPoint& a,
                                   const GeoPoint& b, bool clip)
    : impl_(new Impl) {
  impl_->start = a;
  impl_->end = b;
  GEO_fudge_polar_lat_for_rhumb_line(impl_->start.lat);
  GEO_fudge_polar_lat_for_rhumb_line(impl_->end.lat);

  // The Mercator is re-centred on the line's own midpoint, so a line that
  // spans the antimeridian is still a straight segment in x.
  double geo_width;
  double center_lon;
  if (GEO_east_of_degrees(a.lon, b.lon) != FALSE) {
    geo_width = a.lon - b.lon;
    if (geo_width < 0.0) geo_width += 360.0;
    center_lon = b.lon + geo_width / 2.0;
  } else {
    geo_width = b.lon - a.lon;
    if (geo_width < 0.0) geo_width += 360.0;
    center_lon = a.lon + geo_width / 2.0;
  }
  if (center_lon > 180.0) center_lon -= 360.0;
  impl_->merc.set_center_lon(center_lon);

  const GeoRect bounds = proj.VmapBounds();
  impl_->SetClipBounds(bounds.ll, bounds.ur);
  impl_->dpp_lat = proj.DegPerPixelLat();
  impl_->dpp_lon = proj.DegPerPixelLon();

  impl_->GetClippedPoints(impl_->clipped, &impl_->line_count, clip);
}

RhumbLineContour::~RhumbLineContour() = default;

void RhumbLineContour::ClippedPoints(GeoPoint points[4], int* count) const {
  *count = impl_->line_count;
  for (int i = 0; i < 2 * impl_->line_count && i < 4; ++i)
    points[i] = impl_->clipped[i];
}

void RhumbLineContour::MoveFirst() {
  impl_->current_line = -2;
  impl_->init_next_line = true;
}

bool RhumbLineContour::NextPoint(GeoPoint* p) {
  Impl& d = *impl_;

  if (d.init_next_line) {
    for (;;) {
      d.current_line += 2;
      if (d.current_line >= 2 * d.line_count) return false;

      const GeoPoint p1 = d.clipped[d.current_line];
      const GeoPoint p2 = d.clipped[d.current_line + 1];

      // Is any of this sub-line on screen? An off-screen one still has to be
      // walked (a line style's phase depends on it) but at a token 5 steps.
      GeoPoint line_ll{std::min(p1.lat, p2.lat), p1.lon};
      GeoPoint line_ur{std::max(p1.lat, p2.lat), p2.lon};
      if (GEO_east_of_degrees(p1.lon, p2.lon) != FALSE) {
        line_ll.lon = p2.lon;
        line_ur.lon = p1.lon;
      }
      const d_geo_t a_ll{line_ll.lat, line_ll.lon};
      const d_geo_t a_ur{line_ur.lat, line_ur.lon};
      const d_geo_t b_ll{d.ll.lat, d.ll.lon};
      const d_geo_t b_ur{d.ur.lat, d.ur.lon};
      const bool on_screen =
          GEO_intersect_degrees(a_ll, a_ur, b_ll, b_ur) != FALSE;

      d.x1 = d.merc.lon_to_x(p1.lon);
      d.y1 = d.merc.lat_to_y(p1.lat);
      d.dx = d.merc.lon_to_x(p2.lon) - d.x1;
      d.dy = d.merc.lat_to_y(p2.lat) - d.y1;
      if (d.dx == 0.0 && d.dy == 0.0) continue;  // not a line after all

      // A pixel's size in Mercator units, at this line's own mid-latitude.
      const double mid_lat = (p1.lat + p2.lat) / 2.0;
      const double delta_y =
          ((mid_lat + d.dpp_lat) < 89.99)
              ? d.merc.lat_to_y(mid_lat + d.dpp_lat) - d.merc.lat_to_y(mid_lat)
              : d.merc.lat_to_y(mid_lat) - d.merc.lat_to_y(mid_lat - d.dpp_lat);
      const double delta_x = d.merc.lon_to_x(d.merc.get_center_lon() + d.dpp_lon);

      // BIT-FAITHFUL: the cast binds to sqrt, THEN the integer divide by 20 —
      // so this is "length in pixels, truncated, then / 20", not
      // "length / 20 pixels". About one vertex per 20 pixels either way.
      d.num_steps = static_cast<int>(std::sqrt(
                        (d.dx * d.dx + d.dy * d.dy) /
                        (delta_x * delta_x + delta_y * delta_y))) /
                    20;
      if (!on_screen) d.num_steps = 5;
      if (d.num_steps > 1000) d.num_steps = 1000;
      d.delta_t = (d.num_steps > 1) ? 1.0 / static_cast<double>(d.num_steps) : 1.0;

      // On a line of constant latitude, y_to_lat round-off would give the
      // intermediate points a different latitude from the endpoints — enough
      // to move them a surface pixel. Hold the latitude fixed instead.
      d.changing_lat = (p1.lat != p2.lat);

      d.current_t = d.delta_t;
      d.cur_lat = p1.lat;
      d.cur_lon = p1.lon;
      d.step = 1;
      break;
    }
    d.init_next_line = false;
  }

  // Too short to densify: just the two endpoints.
  if (d.num_steps < 2) {
    if (d.step == 1) {
      *p = d.clipped[d.current_line];
      d.step++;
      return true;
    }
    if (d.step == 2) {
      *p = d.clipped[d.current_line + 1];
      d.step++;
      d.init_next_line = true;
      return true;
    }
    return false;
  }

  if (d.step < d.num_steps) {
    p->lat = d.cur_lat;
    p->lon = d.cur_lon;
    d.cur_lon = d.merc.x_to_lon(d.x1 + d.dx * d.current_t);
    if (d.changing_lat) d.cur_lat = d.merc.y_to_lat(d.y1 + d.dy * d.current_t);
    d.current_t += d.delta_t;
    d.step++;
    return true;
  }
  if (d.step == d.num_steps) {
    *p = d.clipped[d.current_line + 1];
    d.init_next_line = true;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Circle, ellipse, arc
// ---------------------------------------------------------------------------

GeoCircleContour::GeoCircleContour(const GeoPoint& center, double radius_m,
                                   int num_points)
    : center_(center),
      radius_m_(radius_m),
      num_points_(num_points > 2 ? num_points : 3) {}

void GeoCircleContour::MoveFirst() { index_ = 0; }

bool GeoCircleContour::NextPoint(GeoPoint* p) {
  // > and not >=: the first point is emitted twice, which closes the ring.
  if (index_ > num_points_) return false;
  const double bearing = index_++ * (360.0 / num_points_);
  GEO_calc_end_point(center_.lat, center_.lon, radius_m_, bearing, &p->lat,
                     &p->lon, TRUE);
  return true;
}

GeoEllipseContour::GeoEllipseContour(const GeoPoint& center,
                                     double vert_radius_m, double horz_radius_m,
                                     double rotation_deg, int num_points)
    : center_(center),
      vert_r2_(vert_radius_m * vert_radius_m),
      horz_r2_(horz_radius_m * horz_radius_m),
      rotation_deg_(rotation_deg),
      num_points_(num_points > 2 ? num_points : 3) {}

void GeoEllipseContour::MoveFirst() { index_ = 0; }

bool GeoEllipseContour::NextPoint(GeoPoint* p) {
  if (index_ > num_points_) return false;
  const double bearing = index_++ * (360.0 / num_points_);
  const double rad = Rad(bearing);
  const double s = std::sin(rad), c = std::cos(rad);
  const double denom = horz_r2_ * c * c + vert_r2_ * s * s;
  double radius = 0.0;
  if (denom > 0.0) radius = std::sqrt(vert_r2_ * horz_r2_ / denom);
  GEO_calc_end_point(center_.lat, center_.lon, radius, bearing + rotation_deg_,
                     &p->lat, &p->lon, TRUE);
  return true;
}

GeoArcContour::GeoArcContour(const GeoPoint& center, double radius_m,
                             double start_bearing_deg, double sweep_deg,
                             int points_per_circle)
    : center_(center),
      radius_m_(radius_m),
      start_deg_(start_bearing_deg),
      sweep_deg_(sweep_deg) {
  if (sweep_deg_ > 360.0) sweep_deg_ = 360.0;
  if (sweep_deg_ < -360.0) sweep_deg_ = -360.0;
  const int per_circle = points_per_circle > 2 ? points_per_circle : 3;
  // Spacing matches a full circle of the same radius, so a short arc is not
  // over-sampled and a long one is not under-sampled.
  const double fraction = std::fabs(sweep_deg_) / 360.0;
  steps_ = static_cast<int>(std::lround(per_circle * fraction));
  if (steps_ < 1) steps_ = 1;
}

void GeoArcContour::MoveFirst() { index_ = 0; }

bool GeoArcContour::NextPoint(GeoPoint* p) {
  // <= steps_: an arc is not closed, so both endpoints are emitted and
  // nothing is repeated.
  if (index_ > steps_) return false;
  const double t = static_cast<double>(index_++) / static_cast<double>(steps_);
  GEO_calc_end_point(center_.lat, center_.lon, radius_m_,
                     start_deg_ + sweep_deg_ * t, &p->lat, &p->lon, TRUE);
  return true;
}

// ---------------------------------------------------------------------------
// PolylineContour
// ---------------------------------------------------------------------------

PolylineContour::PolylineContour(const MapProjection& proj,
                                 std::vector<GeoPoint> points, LineKind kind,
                                 bool clip, bool closed)
    : proj_(proj),
      points_(std::move(points)),
      kind_(kind),
      clip_(clip),
      closed_(closed) {
  if (closed_ && points_.size() >= 3) points_.push_back(points_.front());
}

PolylineContour::~PolylineContour() = default;

void PolylineContour::MoveFirst() {
  leg_ = 0;
  current_.reset();
  drop_first_ = false;
  emitted_any_in_leg_ = false;
  pending_break_ = false;
  at_break_ = false;
}

bool PolylineContour::AdvanceLeg() {
  // A leg that emitted nothing (clipped away) BREAKS the run, so the next
  // leg's first point must be kept — otherwise two disjoint visible stretches
  // would be joined by a line that was never there.
  drop_first_ = emitted_any_in_leg_;
  // ... and the KEEPING is only half of it: the point is also the start of a
  // new sub-path, which is what AtBreak() carries out to BuildGeoPath. `leg_`
  // is the number of legs already started, so 0 here is the first leg, whose
  // first point begins the run rather than breaking it.
  if (leg_ > 0 && !drop_first_) pending_break_ = true;
  emitted_any_in_leg_ = false;

  if (leg_ + 1 >= points_.size()) {
    current_.reset();
    return false;
  }
  current_ = MakeGeoLine(proj_, points_[leg_], points_[leg_ + 1], kind_, clip_);
  ++leg_;
  current_->MoveFirst();
  return true;
}

bool PolylineContour::NextPoint(GeoPoint* p) {
  if (points_.size() < 2) return false;

  for (;;) {
    if (!current_) {
      if (leg_ + 1 >= points_.size()) return false;
      if (!AdvanceLeg()) return false;
    }

    GeoPoint out;
    if (current_->NextPoint(&out)) {
      if (drop_first_) {
        // This leg's first point is the previous leg's last; skip it once.
        drop_first_ = false;
        emitted_any_in_leg_ = true;
        continue;
      }
      emitted_any_in_leg_ = true;
      at_break_ = pending_break_;
      pending_break_ = false;
      *p = out;
      return true;
    }

    if (!AdvanceLeg()) return false;
  }
}

// ---------------------------------------------------------------------------
// Factory and projection
// ---------------------------------------------------------------------------

GeoContourPtr MakeGeoLine(const MapProjection& proj, const GeoPoint& a,
                          const GeoPoint& b, LineKind kind, bool clip) {
  switch (kind) {
    case LineKind::kGreatCircle:
      return GeoContourPtr(new GreatCircleContour(proj, a, b, clip));
    case LineKind::kRhumb:
      return GeoContourPtr(new RhumbLineContour(proj, a, b, clip));
    case LineKind::kSimple:
      break;
  }
  return GeoContourPtr(new SimpleGeoLine(proj, a, b, clip));
}

void BuildGeoPathInto(const MapProjection& proj, IGeoContour& contour,
                      std::vector<std::vector<SurfacePoint>>* out) {
  contour.MoveFirst();

  std::vector<SurfacePoint> run;
  bool have_prev = false;
  double prev_lon = 0.0;

  auto flush = [&]() {
    if (run.size() >= 2) out->push_back(std::move(run));
    run.clear();
  };

  GeoPoint g;
  while (contour.NextPoint(&g)) {
    // The contour's own break (G3): this point does not continue the last one.
    // Asked FIRST, because a break makes the longitude test below meaningless
    // — the two points are not neighbours in the first place.
    if (contour.AtBreak()) {
      flush();
      have_prev = false;
    }
    // The antimeridian seam: a contour steps in ~20-pixel chords, so a jump
    // this large can only be the projection unwrapping to the far side of the
    // centre. Joining the two would draw a line clean across the surface.
    if (have_prev) {
      double dlon = g.lon - prev_lon;
      if (dlon > 180.0 || dlon < -180.0) flush();
    }

    double sx = 0.0, sy = 0.0;
    if (!proj.GeoToSurface(g, &sx, &sy).ok()) {
      flush();
      have_prev = false;
      continue;
    }
    run.push_back(SurfacePoint{sx, sy});
    prev_lon = g.lon;
    have_prev = true;
  }
  flush();
}

std::vector<std::vector<SurfacePoint>> BuildGeoPath(const MapProjection& proj,
                                                    IGeoContour& contour) {
  std::vector<std::vector<SurfacePoint>> out;
  BuildGeoPathInto(proj, contour, &out);
  return out;
}

std::vector<std::vector<SurfacePoint>> BuildGeoLinePath(
    const MapProjection& proj, const GeoPoint& a, const GeoPoint& b,
    LineKind kind, bool clip) {
  GeoContourPtr c = MakeGeoLine(proj, a, b, kind, clip);
  return BuildGeoPath(proj, *c);
}

}  // namespace fv
