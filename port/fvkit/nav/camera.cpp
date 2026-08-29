// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/camera.h"

#include <cmath>

namespace fv {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kDegToRad = kPi / 180.0;

// The original's `(int)(x + 0.5)`. C casts truncate TOWARD ZERO, so this is
// not lround and must not become one.
//
// THE CLAMP IS THE PORT'S, and it is the one place the original's code could
// not simply be copied. In domain — `point_angle` in [0, 360) — each branch
// below bounds its own ratio to [0, 1], so the index is in [0, 2] and the
// clamp is the identity; the original says so itself, with an ASSERT on
// exactly that range in each branch. OUT of domain it is not: the once-only
// 360 wrap in `Update` can present an angle of 450, where `tan` is ~1.6e16
// and the original's cast of -1.6e16 to `int` is undefined behaviour. There
// is no bit-faithful answer to reproduce at an input whose behaviour is
// undefined, so the port takes the range the original asserts.
int BoxIndex(double v) {
  if (!(v > 0.0)) return 0;  // also catches NaN
  if (v >= 3.0) return 2;
  return static_cast<int>(v);
}

// The same guard for the continuous placement, which keeps the index real.
// Its in-domain range is [0, 2.5] — the literals 0 and 2, or 1 +/- r + 0.5
// with r in [0, 1] — so clamping there is likewise the identity, and it
// keeps `OffsetPixel`'s cast defined at an out-of-domain angle.
constexpr double kMaxContinuousIndex = 2.5;
double BoxIndexContinuous(double v) {
  if (!(v > 0.0)) return 0.0;
  if (v > kMaxContinuousIndex) return kMaxContinuousIndex;
  return v;
}

// `new_center_x = x + (int)(delta + 0.5)` / `(int)(delta - 0.5)` — round half
// AWAY from zero, which is not what lround does at a negative half.
int OffsetPixel(int base, double delta) {
  return base + (delta >= 0.0 ? static_cast<int>(delta + 0.5)
                              : static_cast<int>(delta - 0.5));
}

}  // namespace

ApronRect IntersectApron(const ApronRect& a, const ApronRect& b) {
  ApronRect r;
  r.left = a.left > b.left ? a.left : b.left;
  r.top = a.top > b.top ? a.top : b.top;
  r.right = a.right < b.right ? a.right : b.right;
  r.bottom = a.bottom < b.bottom ? a.bottom : b.bottom;
  // CRect::operator&= zeroes the whole rect when they do not meet.
  if (r.right <= r.left || r.bottom <= r.top) return ApronRect{};
  return r;
}

ApronRect ComputeApron(const CameraModes& modes, int window_width,
                       int window_height, int ship_x, int ship_y) {
  if (!modes.auto_center) return ApronRect{};
  // `SetRectEmpty` — continuous centring recentres on every fix, so it wants
  // an apron the ship can never be inside rather than no apron test at all.
  if (modes.continuous) return ApronRect{};

  if (modes.auto_rotate) {
    // Track-up: a fixed box, because the ship is anchored and it is the CHART
    // that moves. NOTE `ul_x = 2 * (W/5)` and not `2*W/5` — the original
    // multiplies the already-truncated box width, so the box sits up to 2 px
    // left of the two-fifths mark. Preserved; it is a pixel of apron.
    const int box_width = window_width / 5;
    const int box_height = 2 * window_height / 5;
    const int ul_x = 2 * box_width;
    const int ul_y = window_height / 2;
    return ApronRect{ul_x, ul_y, ul_x + box_width + 1, ul_y + box_height + 1};
  }

  // North-up. Start with the outer box, W/10..9W/10 x H/10..9H/10.
  int box_width = 4 * window_width / 5;
  int box_height = 4 * window_height / 5;
  int ul_x = (window_width - box_width) / 2;
  int ul_y = (window_height - box_height) / 2;
  ApronRect rect{ul_x, ul_y, ul_x + box_width + 1, ul_y + box_height + 1};

  // The inner box is a function of where the ship IS, and it is only applied
  // when the ship is inside the outer box — a ship already outside is about
  // to be recentred anyway, and its thirds would build a box around a place
  // it is leaving.
  if (rect.Contains(ship_x, ship_y)) {
    // Middle third: a narrow box, so a ship near the centre may drift only a
    // little before the map moves. Outer thirds: a box twice as wide, so a
    // ship that is already off to one side is left alone.
    int index = 3 * ship_x / window_width;
    if (index == 1) {
      box_width = window_width / 4;
      ul_x = 3 * window_width / 8;
    } else {
      box_width = window_width / 2;
      ul_x = index * window_width / 4;
    }

    index = 3 * ship_y / window_height;
    if (index == 1) {
      box_height = window_height / 4;
      ul_y = 3 * window_height / 8;
    } else {
      box_height = window_height / 2;
      ul_y = index * window_height / 4;
    }

    const ApronRect inner{ul_x, ul_y, ul_x + box_width + 1,
                          ul_y + box_height + 1};
    rect = IntersectApron(rect, inner);
  }
  return rect;
}

// THE TAUTOLOGY, once, for both placements. The original guards the whole
// derivation with `if (point_angle != 90.0 || point_angle != 270.0)`, which
// is true for every possible value — no number is both 90 and 270 — so the
// else branch that hard-codes (i=0,j=1) for due east and (i=2,j=1) for due
// west is unreachable. It is also unnecessary: at exactly 90 the live branch
// takes the `< 180` case, `tan(pi/2)` is ~1.6e16 rather than infinite, and
// j comes out 1 with f = -1, which multiplies a delta_y of exactly 0. Both
// paths therefore give the same answer, which is why the bug has never shown.
// Ported as the live branch alone, with the equivalence pinned by a test
// rather than by carrying dead code across.

void DeltaXyDiscrete(int window_width, int window_height, double point_angle,
                     double* delta_x, double* delta_y) {
  int i = 0;
  int j = 0;
  int f = 1;

  const double point_angle_rad = point_angle * kDegToRad;
  const double w_to_h =
      static_cast<double>(window_width) / static_cast<double>(window_height);
  double mid_angle;

  if (point_angle < 90.0) {
    // 0 <= tan < infinity. Ahead is up-and-right, so the ship goes bottom-left.
    mid_angle = std::atan(w_to_h);
    if (point_angle_rad <= mid_angle) {
      i = BoxIndex(1.0 - std::tan(point_angle_rad) / w_to_h + 0.5);
      j = 2;
    } else {
      i = 0;
      j = BoxIndex(1.0 + w_to_h / std::tan(point_angle_rad) + 0.5);
    }
  } else if (point_angle < 180.0) {
    mid_angle = kPi - std::atan(w_to_h);
    if (point_angle_rad <= mid_angle) {
      i = 0;
      j = BoxIndex(1.0 - w_to_h / std::tan(point_angle_rad) + 0.5);
      f = -1;
    } else {
      i = BoxIndex(1.0 + std::tan(point_angle_rad) / w_to_h + 0.5);
      j = 0;
    }
  } else if (point_angle < 270.0) {
    mid_angle = kPi + std::atan(w_to_h);
    if (point_angle_rad <= mid_angle) {
      i = BoxIndex(1.0 + std::tan(point_angle_rad) / w_to_h + 0.5);
      j = 0;
    } else {
      i = 2;
      j = BoxIndex(1.0 - w_to_h / std::tan(point_angle_rad) + 0.5);
      f = -1;
    }
  } else {
    // 270 <= point_angle < 360 — AND every angle at or above 360, which the
    // once-only wrap in Update can produce. See the note there.
    mid_angle = kTwoPi - std::atan(w_to_h);
    if (point_angle_rad <= mid_angle) {
      i = 2;
      j = BoxIndex(1.0 + w_to_h / std::tan(point_angle_rad) + 0.5);
      f = -1;
    } else {
      i = BoxIndex(1.0 - std::tan(point_angle_rad) / w_to_h + 0.5);
      j = 2;
    }
  }

  // The ship lands at the centre of box (i, j); the offset from it to the
  // window centre is the map centre's offset from the ship.
  if (delta_x != nullptr) {
    *delta_x = static_cast<double>(window_width) *
               (0.5 - static_cast<double>(2 * i + 1) / 6.0);
  }
  if (delta_y != nullptr) {
    *delta_y = f * static_cast<double>(window_height) *
               (0.5 - static_cast<double>(2 * j + 1) / 6.0);
  }
}

void DeltaXyContinuous(int window_width, int window_height, double point_angle,
                       double* delta_x, double* delta_y) {
  // The discrete function with `i`/`j` left real. NOTE the `+ 0.5` stays:
  // it is the rounding term of the discrete formula and here it is simply a
  // constant offset of half a box. Preserved — removing it would move every
  // continuous placement by W/6 or H/6 and is a symbology decision, not a
  // port defect.
  double i = 0.0;
  double j = 0.0;
  int f = 1;

  const double point_angle_rad = point_angle * kDegToRad;
  const double w_to_h =
      static_cast<double>(window_width) / static_cast<double>(window_height);
  double mid_angle;

  if (point_angle < 90.0) {
    mid_angle = std::atan(w_to_h);
    if (point_angle_rad <= mid_angle) {
      i = BoxIndexContinuous(1.0 - std::tan(point_angle_rad) / w_to_h + 0.5);
      j = 2.0;
    } else {
      i = 0.0;
      j = BoxIndexContinuous(1.0 + w_to_h / std::tan(point_angle_rad) + 0.5);
    }
  } else if (point_angle < 180.0) {
    mid_angle = kPi - std::atan(w_to_h);
    if (point_angle_rad <= mid_angle) {
      i = 0.0;
      j = BoxIndexContinuous(1.0 - w_to_h / std::tan(point_angle_rad) + 0.5);
      f = -1;
    } else {
      i = BoxIndexContinuous(1.0 + std::tan(point_angle_rad) / w_to_h + 0.5);
      j = 0.0;
    }
  } else if (point_angle < 270.0) {
    mid_angle = kPi + std::atan(w_to_h);
    if (point_angle_rad <= mid_angle) {
      i = BoxIndexContinuous(1.0 + std::tan(point_angle_rad) / w_to_h + 0.5);
      j = 0.0;
    } else {
      i = 2.0;
      j = BoxIndexContinuous(1.0 - w_to_h / std::tan(point_angle_rad) + 0.5);
      f = -1;
    }
  } else {
    mid_angle = kTwoPi - std::atan(w_to_h);
    if (point_angle_rad <= mid_angle) {
      i = 2.0;
      j = BoxIndexContinuous(1.0 + w_to_h / std::tan(point_angle_rad) + 0.5);
      f = -1;
    } else {
      i = BoxIndexContinuous(1.0 - std::tan(point_angle_rad) / w_to_h + 0.5);
      j = 2.0;
    }
  }

  if (delta_x != nullptr) {
    *delta_x = static_cast<double>(window_width) * (0.5 - (2.0 * i + 1.0) / 6.0);
  }
  if (delta_y != nullptr) {
    *delta_y =
        f * static_cast<double>(window_height) * (0.5 - (2.0 * j + 1.0) / 6.0);
  }
}

void DeltaXyTrackUp(int window_width, int window_height, double point_angle,
                    double anchor_frac_x, double anchor_frac_y,
                    double* delta_x, double* delta_y) {
  const double d_x = anchor_frac_x * static_cast<double>(window_width);
  const double d_y = anchor_frac_y * static_cast<double>(window_height);
  const double rad = point_angle * kDegToRad;

  // This LOOKS like a rotation matrix with a sign error in the second row —
  // a rotation is (x cos + y sin, -x sin + y cos) and the second row here is
  // (+x sin, -y cos) — and it is not one. It is the sum of two correct unit
  // vectors in a Y-DOWN surface: AHEAD along the course is (sin, -cos), and
  // RIGHT of the course is (cos, sin), so `d_x * right + d_y * ahead` is
  // exactly what is written. The determinant is -1 against a y-up rotation
  // for the same reason the surface's y axis points down. Worth stating
  // because it reads as a bug twice: once in the original, and once again in
  // any port that "fixes" it and puts the ownship above the centre while it
  // is heading north.
  if (delta_x != nullptr) *delta_x = d_x * std::cos(rad) + d_y * std::sin(rad);
  if (delta_y != nullptr) *delta_y = d_x * std::sin(rad) - d_y * std::cos(rad);
}

void MovingMapCamera::RecomputeApron(int window_width, int window_height,
                                     int ship_x, int ship_y) {
  // Verbatim: `auto_center_bounding_box_calc` returns with the rect UNTOUCHED
  // when auto-centring is off, keeping whatever it last held. Nothing reads
  // it in that state (Update leaves before the apron test), and preserving
  // the staleness means turning auto-centring back on does not silently
  // change where the apron was.
  if (!modes_.auto_center) return;
  apron_ = ComputeApron(modes_, window_width, window_height, ship_x, ship_y);
}

CameraTarget MovingMapCamera::Update(const MapProjection& proj,
                                     const GeoPoint& ship, double heading_deg,
                                     double map_rotation_deg,
                                     double convergence_deg, bool force) {
  CameraTarget target;
  target.rotation_deg = map_rotation_deg;

  // `map_update`'s trivial rejections.
  if (!modes_.auto_center) return target;
  if (!proj.Ready()) return target;
  // GEO_valid_degrees — a fix with no position must not move the map.
  if (!(ship.lat >= -90.0 && ship.lat <= 90.0 && ship.lon >= -180.0 &&
        ship.lon <= 180.0)) {
    return target;
  }

  double sx = 0.0;
  double sy = 0.0;
  if (!proj.GeoToSurface(ship, &sx, &sy).ok()) return target;
  // FalconView's geo_to_surface answers in whole pixels and the apron test is
  // an integer rect, so the rounding has to happen before the test and not
  // inside it.
  const int x = static_cast<int>(std::lround(sx));
  const int y = static_cast<int>(std::lround(sy));

  const PixelSize size = proj.SurfaceSize();
  // `map->geo_in_surface(lat, lon)`. On an equal-arc projection the geographic
  // test and the pixel test are the same test.
  const bool in_view = x >= 0 && x < size.width && y >= 0 && y < size.height;
  const bool in_apron = apron_.Contains(x, y);

  if (in_view && in_apron && !modes_.continuous && !force) return target;

  // --- set_new_map ---------------------------------------------------------

  // The world-scale escape: at 1:80M and wider there is no "ahead" worth
  // showing, so the ship simply goes in the middle.
  if (world_escape_scale_ > 0.0 && proj.Scale() > 0.0 &&
      proj.Scale() >= world_escape_scale_) {
    target.changed = true;
    target.world_escape = true;
    target.center = ship;
    return target;
  }

  // QUIRK, PRESERVED, and smaller than it looks: the wrap is applied ONCE,
  // not in a loop. A heading in [0, 360) plus a rotation in [0, 360) is under
  // 720, so one subtraction is always enough for the two terms the port
  // actually supplies — this is a latent hazard rather than a live defect,
  // which is why nobody has ever seen it. What can still exceed it is the
  // third term: `convergence_deg` is unbounded here, and a large or negative
  // one leaves `point_angle` outside [0, 360), where the placement's four
  // branches are no longer the four quadrants and `tan` has taken its
  // periodicity somewhere the formula did not intend. Left as FalconView has
  // it — a caller handing this a convergence of 400 degrees has a worse
  // problem — and pinned, with the box index clamped so an out-of-domain
  // angle cannot become an undefined cast. See `BoxIndex`.
  double point_angle = heading_deg + map_rotation_deg + convergence_deg;
  if (point_angle >= 360.0) point_angle -= 360.0;

  double delta_x = 0.0;
  double delta_y = 0.0;
  double rotation = map_rotation_deg;

  if (modes_.auto_rotate) {
    // Turn the map until the course points up the screen.
    rotation -= point_angle;
    if (rotation < 0.0) rotation += 360.0;
    // "Near zero is zero" — a chart drawn at 0.05 degrees costs a rotated
    // render and looks identical to an unrotated one.
    if (rotation < 0.1) rotation = 0.0;

    if (modes_.continuous) {
      DeltaXyTrackUp(size.width, size.height, point_angle, anchor_frac_x_,
                     anchor_frac_y_, &delta_x, &delta_y);
    } else {
      // Verbatim, INCLUDING the fact that the discrete branch ignores the
      // settable anchor: the original's own comment says the preset is used
      // "until the apron calculation is reworked to support locations other
      // than (0.5*W, 5/6*H)" — the track-up apron above really is built
      // around this exact place, so honouring a different fraction here would
      // put the ship outside its own apron and recentre on every fix.
      DeltaXyTrackUp(size.width, size.height, point_angle, 0.0, 0.333333,
                     &delta_x, &delta_y);
    }
  } else if (modes_.continuous) {
    DeltaXyContinuous(size.width, size.height, point_angle, &delta_x, &delta_y);
  } else {
    DeltaXyDiscrete(size.width, size.height, point_angle, &delta_x, &delta_y);
  }

  const int new_center_x = OffsetPixel(x, delta_x);
  const int new_center_y = OffsetPixel(y, delta_y);
  GeoPoint center;
  if (!proj.SurfaceToGeo(new_center_x, new_center_y, &center).ok()) {
    return target;
  }

  target.changed = true;
  target.center = center;
  target.delta_x = delta_x;
  target.delta_y = delta_y;
  target.rotation_deg = rotation;
  target.rotation_changed = rotation != map_rotation_deg;
  return target;
}

}  // namespace fv
