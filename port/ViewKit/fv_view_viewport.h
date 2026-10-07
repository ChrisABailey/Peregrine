// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_view_viewport.h — the camera and surface of one interactive map view.
///
/// A `Viewport` is an immutable value: every gesture returns a new one. It
/// carries the camera (centre, 1:N, rotation), the surface in points and
/// pixels, and the zoom limits, and keeps an `fv::MapProjection` in physical
/// mode (`SetPhysicalScale`) in step with them. Coordinates in this interface
/// are points (the shell's logical units); the projection works in pixels.
#pragma once

#include "fvkit/geo.h"
#include "fvkit/proj.h"

namespace fv {
namespace view {

/// A position on the view's surface, in points from the top-left corner.
struct PointF {
  double x = 0.0;
  double y = 0.0;
};

/// The range of scale denominators the camera may take. Larger is further out.
struct ScaleLimits {
  double min_denom = 1.0e3;
  double max_denom = 5.0e8;
};

class Viewport {
 public:
  Viewport();

  /// A camera with no surface yet; it cannot be panned or zoomed about a
  /// point until `WithSurface` gives it one.
  static Viewport Make(const GeoPoint& center, double scale_denom,
                       double rotation_deg = 0.0);

  /// The same camera over a surface of `width_pt` x `height_pt` points.
  /// `display_scale` is pixels per point; `mm_per_point` the physical size
  /// of one point on the screen. Returns *this unchanged for an empty size.
  Viewport WithSurface(double width_pt, double height_pt, double display_scale,
                       double mm_per_point) const;

  /// The same view under new zoom limits; the scale is re-clamped.
  Viewport WithLimits(const ScaleLimits& limits) const;

  /// A new camera over the same surface. The scale is clamped to the limits,
  /// the rotation wrapped into [0, 360), the centre normalized.
  Viewport WithCamera(const GeoPoint& center, double scale_denom,
                      double rotation_deg) const;

  /// The same camera in another display projection.
  Viewport WithProjectionType(ProjectionType type) const;

  /// The content follows the pointer: the position that was at the surface
  /// centre minus (dx, dy) is moved to the surface centre.
  Viewport Panned(double dx_pt, double dy_pt) const;

  /// The camera moved, at the same scale and rotation, so that `g` shows at
  /// `p`. Iterated, because in physical mode the x scale follows the centre
  /// latitude and one correction leaves a residual. Returns *this when `g`
  /// has no image.
  Viewport WithGeoAt(const GeoPoint& g, const PointF& p) const;

  /// The scale set to `scale_denom` (after clamping) with the position under
  /// `anchor` kept where it is.
  Viewport ZoomedTo(double scale_denom, const PointF& anchor) const;

  /// `factor` > 1 zooms in; the position under `anchor` stays put.
  Viewport ZoomedBy(double factor, const PointF& anchor) const;

  /// Turns the chart clockwise by `degrees` about `anchor`.
  Viewport RotatedBy(double degrees, const PointF& anchor) const;

  /// Which position is under a surface point. The centre when there is no
  /// surface or the point has no geographic image (off an orthographic globe).
  GeoPoint GeoAt(const PointF& p) const;

  /// Where a position lands on the surface, in points. False when it has no
  /// image in the display projection or there is no surface.
  bool PointFor(const GeoPoint& g, PointF* out) const;

  bool HasSurface() const { return pixel_width_ > 0 && pixel_height_ > 0; }
  GeoPoint Center() const { return center_; }
  double ScaleDenom() const { return scale_denom_; }
  double Rotation() const { return rotation_deg_; }
  ProjectionType Type() const { return type_; }
  double WidthPt() const { return width_pt_; }
  double HeightPt() const { return height_pt_; }
  double DisplayScale() const { return display_scale_; }
  double MmPerPoint() const { return mm_per_point_; }
  int PixelWidth() const { return pixel_width_; }
  int PixelHeight() const { return pixel_height_; }
  const ScaleLimits& Limits() const { return limits_; }
  /// The centre of the surface in points (the projection's pixel-centre
  /// convention, not width/2).
  PointF SurfaceCenter() const;

  /// The projection a renderer draws this view with, in pixels.
  const MapProjection& Projection() const { return proj_; }

  /// Same camera, surface and limits.
  bool operator==(const Viewport& o) const;
  bool operator!=(const Viewport& o) const { return !(*this == o); }

 private:
  void Reconfigure();

  GeoPoint center_;
  double scale_denom_ = 1.0e6;
  double rotation_deg_ = 0.0;
  ProjectionType type_ = ProjectionType::kEqualArc;
  double width_pt_ = 0.0, height_pt_ = 0.0;
  double display_scale_ = 1.0;
  double mm_per_point_ = kNativeDisplayMmPerPixel;
  int pixel_width_ = 0, pixel_height_ = 0;
  ScaleLimits limits_;
  MapProjection proj_;
};

}  // namespace view
}  // namespace fv
