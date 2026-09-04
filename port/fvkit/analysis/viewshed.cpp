// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Transcribed from fvw_core/Intervisibility/Viewshed.cs and GeoPoint.cs.
// Read fvkit/analysis/viewshed.h first; this file keeps the C# names in
// comments so the two can be diffed line for line.

#include "fvkit/analysis/viewshed.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fv {
namespace analysis {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kMetersPerNm = 1852.0;

// GeoPoint.equatorialRadiusMeters. NOT WGS84's 6 378 137 — this is the value
// every intervisibility answer FalconView has ever given was computed with,
// and the drop is quadratic in distance, so the difference is not zero.
constexpr double kEquatorialRadiusM = 6378135.0;

// "No slope yet." GeoPoint.cs uses double.MinValue, which multiplied by a
// distance overflows to negative infinity — so the comparison that matters
// (`losRise < elevationChange`) is taken on -inf either way. Spelling it -inf
// says what it is instead of relying on the overflow.
const double kNoSlope = -std::numeric_limits<double>::infinity();

struct LatLon {
  double lat = 0.0;
  double lon = 0.0;
};

// GeoPoint.radiansToMeters / metersToRadians: one nautical mile per minute of
// arc, 1852 m to the nautical mile. This is the ONLY distance model in the
// algorithm; see the header on why it is not geo_tool's ellipsoid.
double RadiansToMeters(double r) { return r * kRadToDeg * 60.0 * kMetersPerNm; }
double MetersToRadians(double m) { return (m / kMetersPerNm / 60.0) * kDegToRad; }

// GeoPoint.getElevationDrop: how much the curvature of the earth lowers a
// point at this distance. Franklin chapter 3.1.1. No refraction term.
double ElevationDrop(double distance_m) {
  return distance_m * distance_m / (2.0 * kEquatorialRadiusM);
}

// GeoPoint.getDistanceRadians: the haversine, on the sphere.
double DistanceMeters(double lat1, double lon1, double lat2, double lon2) {
  const double a_lat = lat1 * kDegToRad;
  const double a_lon = lon1 * kDegToRad;
  const double b_lat = lat2 * kDegToRad;
  const double b_lon = lon2 * kDegToRad;
  const double s_lat = std::sin((a_lat - b_lat) / 2.0);
  const double s_lon = std::sin((a_lon - b_lon) / 2.0);
  const double d = 2.0 * std::asin(std::sqrt(
      s_lat * s_lat + std::cos(a_lat) * std::cos(b_lat) * s_lon * s_lon));
  return RadiansToMeters(d);
}

// Viewshed.calculateCoordinate, verbatim including its sign convention.
//
// Aviation Formulary V1.42's rhumb-line step, which takes WEST longitude as
// POSITIVE — the opposite of the usual mathematical convention and of every
// other file in this tree. The negation on the way in and the way out is the
// whole of the adaptation, and removing it moves every post east of the
// observer. Inputs and outputs are DEGREES; `d` and `tc` are radians.
LatLon StepRhumb(double lat1_deg, double lon1_deg, double d, double tc) {
  double lat1 = lat1_deg * kDegToRad;
  double lon1 = -(lon1_deg * kDegToRad);  // the flip

  // sqrt(1e-19), the tolerance the formulary asks for to avoid 0/0 on an
  // east-west course.
  const double precision = std::pow(10.0, -19.0);

  const double lat = lat1 + d * std::cos(tc);
  const double d_phi = std::log(std::tan(lat / 2.0 + kPi / 4.0) /
                                std::tan(lat1 / 2.0 + kPi / 4.0));
  double q;
  if (std::fabs(lat - lat1) < std::sqrt(precision)) {
    q = std::cos(lat1);
  } else {
    q = (lat - lat1) / d_phi;
  }
  const double d_lon = -d * std::sin(tc) / q;
  const double lon = std::fmod(lon1 + d_lon + kPi, 2.0 * kPi) - kPi;

  LatLon out;
  out.lat = lat * kRadToDeg;
  out.lon = -(lon * kRadToDeg);  // and back
  return out;
}

// Viewshed.angleIntersects, verbatim. Degrees.
bool AngleIntersects(double angle, double lower, double upper) {
  if (lower < 0.0) lower += 360.0;
  if (upper > 360.0) upper -= 360.0;

  if (lower < upper) {
    // not crossing the 0-degree boundary
    if (angle >= lower && angle <= upper) return true;
  } else {
    if (!(angle <= lower && angle >= upper)) return true;
  }
  return false;
}

// One lattice post. GeoPoint.cs, minus the debug accessors.
//
// The three visible heights and the three slopes are computed TOGETHER on
// every post, which is what makes `method` a choice of which column to write
// out rather than a choice of what to compute.
struct Post {
  double lat = 0.0;
  double lon = 0.0;
  double elev_m = 0.0;
  bool has_elev = false;

  double vh_min = 0.0;
  double vh_max = 0.0;
  double vh_interp = 0.0;

  double los_min = kNoSlope;
  double los_max = kNoSlope;
  double los_interp = kNoSlope;
};

// A no-op progress sink, so the walk has no null test in its inner loop.
class NoProgress : public IViewshedProgress {
 public:
  bool OnProgress(int) override { return true; }
};

class Walk {
 public:
  Walk(IElevationSource& src, const ViewshedRequest& req,
       IViewshedProgress* progress)
      : src_(src), req_(req), progress_(progress ? *progress : none_) {}

  Status Run(ViewshedResult* out);

 private:
  // Viewshed.makePoint: the coordinate, and the elevation under it. A post
  // outside the sector is NOT read — that is what the crop buys — and comes
  // back with no elevation, which is how it reaches the output as NaN.
  Post MakePost(double lat, double lon, bool included) const {
    Post p;
    p.lat = lat;
    p.lon = lon;
    if (included) {
      float e = 0.0f;
      if (src_.GetElevation(GeoPoint{lat, lon}, &e).ok() && !std::isnan(e)) {
        p.elev_m = e;
        p.has_elev = true;
      }
    }
    return p;
  }

  // GeoPoint.setLineOfSight*From: the slope of the ray from the observer's
  // eye to this post's own ground, which is what a post that BLOCKS the view
  // hands on to the posts behind it.
  double SlopeFromCenter(const Post& at, double distance) const {
    if (distance == 0.0) return kNoSlope;
    const double effective = at.elev_m - ElevationDrop(distance);
    return (effective - (center_.elev_m + req_.observer_height_m)) / distance;
  }

  // Viewshed.determineVisibility{Max,Min,Interpolated} — one body, since the
  // three differ only in which pair of fields they touch.
  //
  // A post with NO elevation passes its parent's slope through UNCHANGED and
  // takes a NaN height: `losRise < elevationChange` is false against a NaN,
  // so the branch that would re-derive a slope from the missing ground is
  // never taken. That is the right answer for a hole in the coverage — the
  // ray carries on as if nothing were there — and it is what the C# does too,
  // by way of a sentinel elevation instead of a NaN.
  //
  // `distance` is the post's range from the observer, hoisted out by the
  // caller. The C# recomputes it inside each of the three methods and again
  // inside setLineOfSight*From — four haversines per post where one will do,
  // and at 170 ns a post that is most of the running time. Hoisting it is
  // common-subexpression elimination and nothing else: the same value reaches
  // the same arithmetic in the same order, checked byte-for-byte over a
  // million posts of real DTED across all three methods and a sector crop.
  void DetermineVisibility(Post& at, double distance, double los, double* vh,
                           double* los_out) const {
    const double effective = at.elev_m - ElevationDrop(distance);
    const double elevation_change =
        effective - (center_.elev_m + req_.observer_height_m);
    const double los_rise = los * distance;
    if (los_rise < elevation_change) {
      *vh = 0.0;  // the ground here is above the ray: visible, and it blocks
      *los_out = SlopeFromCenter(at, distance);
    } else {
      *vh = los_rise - elevation_change;  // how high to reach the ray
      *los_out = los;                     // nothing new blocks; inherit
    }
  }

  Post& At(int row, int col) {
    return grid_[static_cast<size_t>(row) * span_ + col];
  }
  const Post& At(int row, int col) const {
    return grid_[static_cast<size_t>(row) * span_ + col];
  }

  // Viewshed.setupPoint, the four-index (one parent) overload: the cardinals
  // and the corners, each of which sits directly outward of one inner post.
  void SetupPoint(const LatLon& at, bool included, int row, int col,
                  int parent_row, int parent_col) {
    Post p = MakePost(at.lat, at.lon, included);
    const double d = DistanceMeters(p.lat, p.lon, center_.lat, center_.lon);
    const Post& parent = At(parent_row, parent_col);
    DetermineVisibility(p, d, parent.los_max, &p.vh_max, &p.los_max);
    DetermineVisibility(p, d, parent.los_min, &p.vh_min, &p.los_min);
    DetermineVisibility(p, d, parent.los_interp, &p.vh_interp, &p.los_interp);
    At(row, col) = p;
  }

  // Viewshed.setupPoint, the six-index (two parents) overload: the wall
  // posts, which lie between two inner posts rather than behind one.
  //
  // This is where the three methods stop agreeing: max takes the more
  // optimistic parent slope, min the more pessimistic, and interpolated a
  // weighted step between them.
  void SetupWallPoint(const LatLon& at, bool included, int row, int col,
                      int p1_row, int p1_col, int p2_row, int p2_col) {
    Post p = MakePost(at.lat, at.lon, included);
    const double d = DistanceMeters(p.lat, p.lon, center_.lat, center_.lon);
    const Post& a = At(p1_row, p1_col);
    const Post& b = At(p2_row, p2_col);

    DetermineVisibility(p, d, std::max(a.los_max, b.los_max), &p.vh_max, &p.los_max);
    DetermineVisibility(p, d, std::min(a.los_min, b.los_min), &p.vh_min, &p.los_min);

    // The interpolation weight: how far past the nearer parent the ray from
    // the centre actually crosses this ring. `center_index_` is both the row
    // and the column of the observer, which is why the C# can pass `centerX`
    // where it means `centerY` in the second branch below without it showing.
    const int c = center_index_;
    double theta = 0.0;
    const Post* base_post = &a;
    const Post* non_base = &b;

    if (p1_col == p2_col) {
      // the two parents share a column
      const double width_height_ratio =
          static_cast<double>(row - c) / static_cast<double>(col - c);
      const double width_prime =
          std::fabs(width_height_ratio * static_cast<double>(p1_col - c));
      double use_difference = std::fabs(static_cast<double>(p1_row - c));
      if (std::fabs(static_cast<double>(p2_row - c)) >
          std::fabs(static_cast<double>(p1_row - c))) {
        base_post = &b;
        non_base = &a;
        use_difference = std::fabs(static_cast<double>(p2_row - c));
      }
      theta = width_prime - use_difference;
    } else if (p1_row == p2_row) {
      // the two parents share a row
      const double height_width_ratio =
          static_cast<double>(col - c) / static_cast<double>(row - c);
      const double height_prime =
          std::fabs(height_width_ratio * static_cast<double>(p1_row - c));
      double use_difference = std::fabs(static_cast<double>(p1_col - c));
      if (std::fabs(static_cast<double>(p2_col - c)) >
          std::fabs(static_cast<double>(p1_col - c))) {
        base_post = &b;
        non_base = &a;
        use_difference = std::fabs(static_cast<double>(p2_col - c));
      }
      theta = height_prime - use_difference;
    }

    const double los_difference = base_post->los_interp - non_base->los_interp;
    const double los = base_post->los_interp + los_difference * theta;
    DetermineVisibility(p, d, los, &p.vh_interp, &p.los_interp);

    At(row, col) = p;
  }

  // Viewshed.Continue: report only when the whole percent MOVES, and stop
  // when the caller says so.
  bool Continue(double complete, double to_do) {
    const int percent = static_cast<int>(100.0 * complete / to_do);
    if (percent > last_progress_) {
      last_progress_ = percent;
      if (!progress_.OnProgress(percent)) {
        cancelled_ = true;
        return false;
      }
    }
    return !cancelled_;
  }

  // Viewshed.includeDirection: the cardinals and corners, with 45 degrees of
  // slop so the posts feeding the sector's own edge are computed.
  bool IncludeDirection(double angle) const {
    if (!cropped_) return true;
    const double upper = req_.sector.bearing_deg + req_.sector.angle_deg / 2.0 + 45.0;
    const double lower = req_.sector.bearing_deg - req_.sector.angle_deg / 2.0 - 45.0;
    return AngleIntersects(angle, lower, upper);
  }

  // Viewshed.includeOctant: the walls, at 22.5 degrees. Four tests rather
  // than one because either range may contain an endpoint of the other.
  bool IncludeOctant(double octant_angle) const {
    if (!cropped_) return true;
    const double upper = req_.sector.bearing_deg + req_.sector.angle_deg / 2.0;
    const double lower = req_.sector.bearing_deg - req_.sector.angle_deg / 2.0;
    const double upper_octant = octant_angle + 22.5;
    const double lower_octant = octant_angle - 22.5;
    return AngleIntersects(upper_octant, lower, upper) ||
           AngleIntersects(lower_octant, lower, upper) ||
           AngleIntersects(lower, lower_octant, upper_octant) ||
           AngleIntersects(upper, lower_octant, upper_octant);
  }

  IElevationSource& src_;
  const ViewshedRequest& req_;
  NoProgress none_;
  IViewshedProgress& progress_;

  std::vector<Post> grid_;
  Post center_;
  int span_ = 0;
  int center_index_ = 0;
  int last_progress_ = -1;
  bool cancelled_ = false;
  bool cropped_ = false;
};

Status Walk::Run(ViewshedResult* out) {
  cropped_ = req_.has_sector && req_.sector.angle_deg < 270.0 &&
             req_.sector.angle_deg > 0.0;

  // The centre must have ground to stand on. FalconView asks the same
  // question up front, as IsElevationDataAvailable, and refuses to queue the
  // work without it.
  center_ = MakePost(req_.observer.lat, req_.observer.lon, true);
  if (!center_.has_elev) {
    return Status::Error(kOutOfCoverage,
                         "ComputeViewshed: no elevation at the observer");
  }
  center_.vh_min = 0.0;
  center_.vh_max = 0.0;
  center_.vh_interp = 0.0;
  // The centre's own slopes stay kNoSlope, which is what makes the first ring
  // unconditionally visible and gives it a slope derived from the observer.

  const double distance_rad = MetersToRadians(req_.range_m);
  double step_rad = req_.step_deg * kDegToRad;
  int steps = static_cast<int>(distance_rad / step_rad);
  span_ = steps * 2 + 1;

  // The post budget. Over it the SPAN shrinks and the STEP widens: the range
  // asked for is always the range delivered, more coarsely.
  const size_t wanted = static_cast<size_t>(span_) * static_cast<size_t>(span_);
  if (req_.max_posts > 0 && req_.max_posts < wanted) {
    span_ = static_cast<int>(std::sqrt(static_cast<double>(req_.max_posts)));
    if (span_ % 2 == 0) span_ += 1;  // there must be a centre post
    steps = span_ / 2;
    if (steps > 0) {
      step_rad = distance_rad / steps;
    } else {
      span_ = 1;
    }
    out->step_was_widened = true;
  }

  out->span = span_;
  out->step_deg = step_rad * kRadToDeg;
  center_index_ = span_ / 2;

  grid_.assign(static_cast<size_t>(span_) * static_cast<size_t>(span_), Post{});
  At(center_index_, center_index_) = center_;

  const double to_do = static_cast<double>(span_) * static_cast<double>(span_);
  const int c = center_index_;

  const double tc_north = 0.0;
  const double tc_south = kPi;
  const double tc_west = 3.0 * kPi / 2.0;
  const double tc_east = kPi / 2.0;

  for (int box_radius = 1; box_radius <= steps; ++box_radius) {
    const double ring = static_cast<double>(2 * box_radius - 1);
    if (!Continue(ring * ring, to_do)) break;

    const double step_distance = box_radius * step_rad;

    // --- the four cardinals -------------------------------------------
    const LatLon north =
        StepRhumb(req_.observer.lat, req_.observer.lon, step_distance, tc_north);
    SetupPoint(north, IncludeDirection(0.0), c - box_radius, c,
               c - box_radius + 1, c);

    const LatLon south =
        StepRhumb(req_.observer.lat, req_.observer.lon, step_distance, tc_south);
    SetupPoint(south, IncludeDirection(180.0), c + box_radius, c,
               c + box_radius - 1, c);

    const LatLon west =
        StepRhumb(req_.observer.lat, req_.observer.lon, step_distance, tc_west);
    SetupPoint(west, IncludeDirection(270.0), c, c - box_radius, c,
               c - box_radius + 1);

    const LatLon east =
        StepRhumb(req_.observer.lat, req_.observer.lon, step_distance, tc_east);
    SetupPoint(east, IncludeDirection(90.0), c, c + box_radius, c,
               c + box_radius - 1);

    // --- the four corners ---------------------------------------------
    // Reached from the WEST and EAST posts going north or south, and not
    // from north/south going east or west: the C# note is that the formulas
    // carry less error along a meridian than across one.
    const LatLon north_west =
        StepRhumb(west.lat, west.lon, step_distance, tc_north);
    SetupPoint(north_west, IncludeDirection(315.0), c - box_radius,
               c - box_radius, c - box_radius + 1, c - box_radius + 1);

    const LatLon north_east =
        StepRhumb(east.lat, east.lon, step_distance, tc_north);
    SetupPoint(north_east, IncludeDirection(45.0), c - box_radius,
               c + box_radius, c - box_radius + 1, c + box_radius - 1);

    const LatLon south_west =
        StepRhumb(west.lat, west.lon, step_distance, tc_south);
    SetupPoint(south_west, IncludeDirection(225.0), c + box_radius,
               c - box_radius, c + box_radius - 1, c - box_radius + 1);

    const LatLon south_east =
        StepRhumb(east.lat, east.lon, step_distance, tc_south);
    SetupPoint(south_east, IncludeDirection(135.0), c + box_radius,
               c + box_radius, c + box_radius - 1, c + box_radius - 1);

    // --- the eight wall octants ---------------------------------------
    // Everything between a cardinal and a corner, one step at a time out
    // from the cardinal. box_radius == 1 has no walls.
    struct WallIndices {
      int row, col, p1_row, p1_col, p2_row, p2_col;
    };
    auto walk_wall = [&](double octant, const LatLon& from, double tc,
                         WallIndices (*ix)(int c, int box_radius, int wall_step)) {
      const bool included = IncludeOctant(octant);
      for (int wall_step = 1; wall_step < box_radius; ++wall_step) {
        if (!Continue(ring * ring + 8.0 * wall_step, to_do)) return;
        const LatLon at =
            StepRhumb(from.lat, from.lon, wall_step * step_rad, tc);
        const WallIndices w = ix(c, box_radius, wall_step);
        SetupWallPoint(at, included, w.row, w.col, w.p1_row, w.p1_col,
                       w.p2_row, w.p2_col);
      }
    };

    walk_wall(292.5, west, tc_north, [](int c, int b, int s) {
      return WallIndices{c - s, c - b, c - s, c - b + 1, c - s + 1, c - b + 1};
    });
    if (cancelled_) break;
    walk_wall(247.5, west, tc_south, [](int c, int b, int s) {
      return WallIndices{c + s, c - b, c + s, c - b + 1, c + s - 1, c - b + 1};
    });
    if (cancelled_) break;
    walk_wall(67.5, east, tc_north, [](int c, int b, int s) {
      return WallIndices{c - s, c + b, c - s, c + b - 1, c - s + 1, c + b - 1};
    });
    if (cancelled_) break;
    walk_wall(112.5, east, tc_south, [](int c, int b, int s) {
      return WallIndices{c + s, c + b, c + s, c + b - 1, c + s - 1, c + b - 1};
    });
    if (cancelled_) break;
    walk_wall(337.5, north, tc_west, [](int c, int b, int s) {
      return WallIndices{c - b, c - s, c - b + 1, c - s, c - b + 1, c - s + 1};
    });
    if (cancelled_) break;
    walk_wall(22.5, north, tc_east, [](int c, int b, int s) {
      return WallIndices{c - b, c + s, c - b + 1, c + s, c - b + 1, c + s - 1};
    });
    if (cancelled_) break;
    walk_wall(202.5, south, tc_west, [](int c, int b, int s) {
      return WallIndices{c + b, c - s, c + b - 1, c - s, c + b - 1, c - s + 1};
    });
    if (cancelled_) break;
    walk_wall(157.5, south, tc_east, [](int c, int b, int s) {
      return WallIndices{c + b, c + s, c + b - 1, c + s, c + b - 1, c + s - 1};
    });
    if (cancelled_) break;
  }

  if (cancelled_) {
    return Status::Error(kInterrupted, "ComputeViewshed: cancelled");
  }

  // --- write it out ---------------------------------------------------
  out->visible_height_m.assign(
      static_cast<size_t>(span_) * static_cast<size_t>(span_),
      std::numeric_limits<float>::quiet_NaN());
  out->no_data_count = 0;
  for (int row = 0; row < span_; ++row) {
    for (int col = 0; col < span_; ++col) {
      const Post& p = At(row, col);
      float v = std::numeric_limits<float>::quiet_NaN();
      if (p.has_elev) {
        switch (req_.method) {
          case HeightMethod::kMin:
            v = static_cast<float>(p.vh_min);
            break;
          case HeightMethod::kMax:
            v = static_cast<float>(p.vh_max);
            break;
          case HeightMethod::kInterpolated:
            v = static_cast<float>(p.vh_interp);
            break;
        }
      }
      if (std::isnan(v)) ++out->no_data_count;
      out->visible_height_m[static_cast<size_t>(row) * span_ + col] = v;
    }
  }

  const Post& nw = At(0, 0);
  const Post& se = At(span_ - 1, span_ - 1);
  out->bounds.ur = GeoPoint{nw.lat, se.lon};   // north, east
  out->bounds.ll = GeoPoint{se.lat, nw.lon};   // south, west
  return Status::Ok();
}

}  // namespace

double ViewshedStepFromPostSpacing(IElevationSource& src, const GeoPoint& at) {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  if (!src.PostSpacing(at, &lat_deg, &lon_deg)) return 0.0;

  // The lattice is square in GROUND distance and its step is quoted in
  // degrees of latitude, so a longitude spacing has to be converted before
  // the two can be compared. The COARSER wins: a lattice finer than the data
  // in either axis is inventing the terrain it then occludes with.
  //
  // (The profile takes the FINER of the two, for the opposite reason — it
  // walks one line and wants the most detail the data honestly has.)
  const double lon_as_lat = lon_deg * std::cos(at.lat * kDegToRad);
  return std::max(lat_deg, lon_as_lat);
}

Status ComputeViewshed(IElevationSource& src, const ViewshedRequest& req,
                       ViewshedResult* out, IViewshedProgress* progress) {
  if (out == nullptr) {
    return Status::Error(kInvalidArg, "ComputeViewshed: null out");
  }
  *out = ViewshedResult{};
  if (!(req.range_m > 0.0)) {
    return Status::Error(kInvalidArg, "ComputeViewshed: range_m must be > 0");
  }
  if (!(req.step_deg > 0.0)) {
    return Status::Error(kInvalidArg, "ComputeViewshed: step_deg must be > 0");
  }

  Walk walk(src, req, progress);
  const Status s = walk.Run(out);
  if (!s.ok()) *out = ViewshedResult{};
  return s;
}

}  // namespace analysis
}  // namespace fv
