// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * PPPerspective.h — the 2.5D camera: a tilted view of the flat map.
 *
 * Header-only C++ with no platform dependency, tested on the mac like
 * `PPBaseCoverage.h`. A flat map seen from a tilted pinhole camera is a
 * homography of the top-down picture, so the shell keeps drawing top-down and
 * the compositor applies `FlatToScreen()` as a 3x3 projective transform.
 *
 * "Flat" coordinates are the surface pixels of today's untilted viewport
 * (`fv::MapProjection`, rotation included), so course-up costs nothing extra.
 * The tilt is pinned at the anchor, where the ship sits in follow: the anchor
 * maps to itself and the horizontal scale there is 1, so the ground under the
 * ship keeps the detail it has at pitch 0. At pitch 0 both transforms are
 * exactly the identity.
 */

#ifndef PIPPIN_PPPERSPECTIVE_H_
#define PIPPIN_PPPERSPECTIVE_H_

#include <algorithm>
#include <cmath>

namespace pippin {

/// Highest pitch the camera accepts, in degrees from straight down. Past
/// this the top-down picture needed to fill the screen grows faster than the
/// CPU raster can afford.
constexpr double kMaxPitchDeg = 45.0;

/// The vertical field of view used when none is given, in degrees.
constexpr double kDefaultFovDeg = 40.0;

/// A 3x3 projective transform on 2D points, row-major, normalised so that
/// m[8] == 1 whenever that is possible.
struct Homography {
  double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

  /// Maps (x, y). False when the point maps to infinity or behind the camera
  /// (w <= 0); the outputs are then left unchanged.
  bool Apply(double x, double y, double* ox, double* oy) const {
    const double w = m[6] * x + m[7] * y + m[8];
    if (!(w > 0.0) || !std::isfinite(w)) return false;
    *ox = (m[0] * x + m[1] * y + m[2]) / w;
    *oy = (m[3] * x + m[4] * y + m[5]) / w;
    return std::isfinite(*ox) && std::isfinite(*oy);
  }

  /// The inverse transform, by the adjugate. A singular matrix yields the
  /// identity and `ok` false.
  Homography Inverse(bool* ok = nullptr) const {
    const double* a = m;
    double r[9] = {
        a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8],
        a[1] * a[5] - a[2] * a[4], a[5] * a[6] - a[3] * a[8],
        a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5],
        a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7],
        a[0] * a[4] - a[1] * a[3]};
    const double det = a[0] * r[0] + a[1] * r[3] + a[2] * r[6];
    Homography out;
    if (det == 0.0 || !std::isfinite(det)) {
      if (ok) *ok = false;
      return out;
    }
    // The adjugate is the inverse up to scale, and a homography is only
    // defined up to scale, so normalising stands in for dividing by det. Its
    // sign is kept so w stays positive in front of the camera.
    const double n = r[8] != 0.0 ? std::fabs(r[8]) : std::fabs(det);
    for (int i = 0; i < 9; ++i) out.m[i] = r[i] / n;
    if (det < 0.0) for (double& v : out.m) v = -v;
    if (ok) *ok = true;
    return out;
  }
};

/// What the camera is given: the screen in pixels, the tilt, and the anchor.
struct PerspectiveParams {
  int width = 0;   ///< screen surface, pixels
  int height = 0;
  double pitch_deg = 0.0;  ///< 0 is straight down; clamped to [0, kMaxPitchDeg]
  double fov_deg = kDefaultFovDeg;  ///< vertical field of view
  double anchor_x = 0.0;  ///< screen pixel that keeps its flat position and scale
  double anchor_y = 0.0;
};

/// An axis-aligned pixel rectangle in flat coordinates: pixel centres x0 ..
/// x0 + width - 1 and y0 .. y0 + height - 1.
struct PixelRect {
  int x0 = 0;
  int y0 = 0;
  int width = 0;
  int height = 0;
};

/// The tilted camera for one screen. Cheap to build; rebuild on any change.
class Perspective {
 public:
  Perspective() = default;

  /// Builds the camera. An empty screen or a non-finite input gives the flat
  /// identity camera.
  explicit Perspective(const PerspectiveParams& in) : params_(in) {
    PerspectiveParams& p = params_;
    const double pitch = std::isfinite(p.pitch_deg) ? p.pitch_deg : 0.0;
    p.pitch_deg = std::clamp(pitch, 0.0, kMaxPitchDeg);
    if (!(p.fov_deg > 1.0 && p.fov_deg < 120.0)) p.fov_deg = kDefaultFovDeg;
    if (p.width <= 0 || p.height <= 0 || !std::isfinite(p.anchor_x) ||
        !std::isfinite(p.anchor_y) || p.pitch_deg == 0.0) {
      p.pitch_deg = 0.0;
      return;
    }

    const double kPi = 3.14159265358979323846;
    const double cx = (p.width - 1) / 2.0;
    const double cy = (p.height - 1) / 2.0;
    focal_ = (p.height / 2.0) / std::tan(p.fov_deg * kPi / 360.0);
    sin_ = std::sin(p.pitch_deg * kPi / 180.0);
    cos_ = std::cos(p.pitch_deg * kPi / 180.0);
    cy_ = cy;
    const double f = focal_, s = sin_, c = cos_;

    // Screen -> ground, with the camera at unit height: a ray through screen
    // pixel (x, y) meets the ground at (X, Y)/W for
    //   [X Y W] = A [x y 1],
    // Y pointing away from the camera. Then ground -> flat scales by the
    // anchor's denominator and puts the anchor back where it was.
    const double da = Denominator(p.anchor_y);
    const double A[9] = {1, 0, -cx, 0, -c, f * s + cy * c, 0, s, f * c - cy * s};
    const double B[9] = {da, 0, cx, 0, -da, p.anchor_y + f * s - (p.anchor_y - cy) * c,
                         0, 0, 1};
    Homography to_flat;
    for (int r = 0; r < 3; ++r)
      for (int k = 0; k < 3; ++k)
        to_flat.m[3 * r + k] = B[3 * r] * A[k] + B[3 * r + 1] * A[3 + k] +
                               B[3 * r + 2] * A[6 + k];
    const double n = to_flat.m[8];
    for (double& v : to_flat.m) v /= n;
    bool ok = false;
    Homography to_screen = to_flat.Inverse(&ok);
    if (!ok) {
      params_.pitch_deg = 0.0;
      return;
    }
    screen_to_flat_ = to_flat;
    flat_to_screen_ = to_screen;
  }

  const PerspectiveParams& params() const { return params_; }
  double pitch_deg() const { return params_.pitch_deg; }
  bool IsFlat() const { return params_.pitch_deg == 0.0; }

  /// The transform the compositor applies to top-down layers.
  const Homography& flat_to_screen() const { return flat_to_screen_; }
  const Homography& screen_to_flat() const { return screen_to_flat_; }

  bool FlatToScreen(double fx, double fy, double* sx, double* sy) const {
    return flat_to_screen_.Apply(fx, fy, sx, sy);
  }
  bool ScreenToFlat(double sx, double sy, double* fx, double* fy) const {
    return screen_to_flat_.Apply(sx, sy, fx, fy);
  }

  /// The screen's four outer corners on the ground, in flat pixels, in the
  /// order top-left, top-right, bottom-right, bottom-left. A convex quad (a
  /// trapezoid when tilted); false when a corner is past the horizon.
  bool GroundQuad(double xs[4], double ys[4]) const {
    const double r = params_.width - 0.5, b = params_.height - 0.5;
    const double sx[4] = {-0.5, r, r, -0.5};
    const double sy[4] = {-0.5, -0.5, b, b};
    for (int i = 0; i < 4; ++i)
      if (!ScreenToFlat(sx[i], sy[i], &xs[i], &ys[i])) return false;
    return true;
  }

  /// The smallest whole-pixel flat rectangle whose pixels cover the ground
  /// quad, grown by `pad_px` on every side. This is the top-down picture to
  /// draw. Width and height are zero when the quad is not finite.
  PixelRect RenderBounds(int pad_px = 1) const {
    PixelRect out;
    double xs[4], ys[4];
    if (params_.width <= 0 || params_.height <= 0 || !GroundQuad(xs, ys))
      return out;
    const double x_lo = *std::min_element(xs, xs + 4);
    const double x_hi = *std::max_element(xs, xs + 4);
    const double y_lo = *std::min_element(ys, ys + 4);
    const double y_hi = *std::max_element(ys, ys + 4);
    // A pixel centre at i covers [i - 0.5, i + 0.5].
    out.x0 = (int)std::floor(x_lo + 0.5) - pad_px;
    out.y0 = (int)std::floor(y_lo + 0.5) - pad_px;
    out.width = (int)std::ceil(x_hi - 0.5) + pad_px - out.x0 + 1;
    out.height = (int)std::ceil(y_hi - 0.5) + pad_px - out.y0 + 1;
    return out;
  }

  /// The flat rectangle the base map is drawn into for this camera:
  /// `RenderBounds(pad_px)` united with the screen's own surface, so the
  /// screen sits inside it at whole-pixel offsets as it does in a flat band.
  /// Empty when the camera is flat or the quad is not finite.
  PixelRect BandRect(int pad_px) const {
    if (IsFlat()) return PixelRect{};
    PixelRect r = RenderBounds(pad_px);
    if (r.width <= 0 || r.height <= 0) return PixelRect{};
    const int x1 = std::max(r.x0 + r.width, params_.width);
    const int y1 = std::max(r.y0 + r.height, params_.height);
    r.x0 = std::min(r.x0, 0);
    r.y0 = std::min(r.y0, 0);
    r.width = x1 - r.x0;
    r.height = y1 - r.y0;
    return r;
  }

  /// The on-screen size of ground at screen row `sy`, relative to its size at
  /// the anchor: 1 at the anchor, less than 1 farther away. Sizes billboards.
  /// Zero at and above the horizon.
  double DepthScale(double sy) const {
    if (IsFlat()) return 1.0;
    const double d = Denominator(sy);
    const double da = Denominator(params_.anchor_y);
    return d > 0.0 ? d / da : 0.0;
  }

  /// The screen row of the horizon, which may be above the screen (negative).
  /// False when the camera is flat and there is none.
  bool HorizonY(double* y) const {
    if (IsFlat()) return false;
    *y = cy_ - focal_ * cos_ / sin_;
    return true;
  }

  /// The haze over the far ground: a gradient in the map's background colour
  /// from `top_alpha` at the top of the screen to clear at row `end_y`.
  struct Fade {
    double end_y = 0.0;      ///< screen row where the fade reaches zero
    double top_alpha = 0.0;  ///< opacity at row 0, in [0, 1]
  };

  /// The fade over the last screen height of ground depth, measured in flat
  /// pixels back from the far edge along the anchor's column. Capped at a
  /// third of the screen, since at low pitch the whole view is less than two
  /// screens deep; its strength follows the foreshortening at the top row.
  /// No fade when flat.
  Fade FarFade() const {
    Fade out;
    if (IsFlat()) return out;
    const double ax = params_.anchor_x;
    double fx = 0.0, far_y = 0.0, sx = 0.0, sy = 0.0;
    if (!ScreenToFlat(ax, -0.5, &fx, &far_y)) return out;
    if (!FlatToScreen(fx, far_y + params_.height, &sx, &sy)) return out;
    out.end_y = std::clamp(sy, 0.0, params_.height / 3.0);
    out.top_alpha = std::clamp(2.0 * (1.0 - DepthScale(0.0)), 0.0, 1.0);
    return out;
  }

 private:
  // The camera-frame depth of the ground point under screen row y, up to the
  // camera height; positive in front of the camera.
  double Denominator(double y) const { return (y - cy_) * sin_ + focal_ * cos_; }

  PerspectiveParams params_;
  Homography flat_to_screen_;
  Homography screen_to_flat_;
  double focal_ = 0.0;
  double sin_ = 0.0;
  double cos_ = 1.0;
  double cy_ = 0.0;
};

}  // namespace pippin

#endif  // PIPPIN_PPPERSPECTIVE_H_
