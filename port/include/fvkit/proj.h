// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/** @file
 * fvkit/proj.h — the map projection: centre + scale + surface size to
 * degrees per pixel (via the ported MapScaleUtil, so dpp matches FalconView)
 * and geo<->surface transforms, with an optional clockwise rotation about the
 * surface centre.
 *
 * Surface coords are doubles: x right, y down, origin at the surface's
 * top-left pixel centre (contracts D4).
 *
 * Rotation turns the chart clockwise on the screen, the sense the nav camera
 * reports (270 for a course of 090). Rotation 0 runs the unrotated arithmetic
 * in its original order, so its doubles are bit-identical to an unrotated
 * build; every pinned golden depends on that. Multiples of 90 take cos/sin
 * from a table so cardinal turns are exact.
 *
 * All five FalconView display projections are implemented. Equal Arc is
 * tested first in every transform, ahead of any other arithmetic.
 *
 * Mercator is spherical, on a sphere of radius WGS84_a_METERS, with the
 * standard parallel at the viewport centre and a hard limit of
 * kMercatorMaxLat: a point beyond it reports kNotProjectable in both
 * directions. The centre latitude used for the projection is not the
 * requested one when the requested one would put the top (or bottom) edge
 * past that limit; Center() still reports what the caller asked for.
 *
 * Lambert is spherical too, with two standard parallels derived from the
 * viewport rather than set by the caller: centre latitude plus and minus a
 * third of the surface height in degrees. A pan or zoom therefore re-derives
 * the cone. Within one pixel of latitude of the equator the cone constant
 * goes to zero, and the Mercator equations are used instead, as Windows
 * does. It is the first projection where north is not up away from the
 * centre meridian; LocalScaleAt reports that angle.
 *
 * Azimuthal Equidistant and Orthographic are the two azimuthal projections,
 * both spherical and both centred on the viewport centre. They are the first
 * projections whose image of the earth does not fill the plane: Azimuthal
 * Equidistant maps the whole sphere into a disc of radius pi * R, with the
 * antipode as its rim, and Orthographic shows one hemisphere as a disc of
 * radius R. A surface pixel outside the disc, or a point on the far
 * hemisphere in Orthographic, reports kNotProjectable. They are also the
 * first projections that can put a pole, or both poles, inside the surface,
 * which VmapBounds answers with a full circle of longitude; PoleOnSurface
 * asks that question directly.
 */

#pragma once

#include "fvkit/geo.h"

namespace fv {

// Physical pixel pitch of the REFERENCE display, in millimetres. Used only to
// define "100%" for imagery that carries a ground resolution instead of a
// cartographic scale: at this pitch one source pixel maps to one screen pixel.
// An Apple Cinema Display is ~4 px/mm; change this for a different panel.
constexpr double kNativeDisplayMmPerPixel = 0.25;

/// Latitude beyond which Mercator is not drawn (Windows MERCATOR_MAX_LAT).
constexpr double kMercatorMaxLat = 80.0;

/// The 2D display projections FalconView ships, in Windows ProjectionEnum order.
enum class ProjectionType {
  kEqualArc,
  kMercator,
  kLambert,
  kAzimuthalEquidistant,
  kOrthographic,
};

class MapProjection {
 public:
  Status SetSurfaceSize(int width, int height);
  Status SetCenter(const GeoPoint& center);   // lon normalized to (-180,180]
  Status SetScale(double scale_denominator);  // 1:N equal-arc (dpp via MapScaleUtil)

  // Explicit-resolution mode (tile pyramids need latitude-independent,
  // exactly reproducible levels): sets dpp directly, bypassing MapScaleUtil.
  // Mutually exclusive with SetScale — whichever was called last wins;
  // Scale() reports 0 in resolution mode.
  Status SetResolution(double dpp_lat, double dpp_lon);

  // Physical-display mode: choose dpp so a feature 1:scale_denominator large
  // is drawn at THAT SAME scale on a device whose pixel pitch is
  // mm_per_pixel, with square ground cells at the center latitude. So at
  // 1:1,000,000 on a 0.25 mm/px screen, 1 cm of screen spans ~10 km of
  // ground. Unlike SetScale (which reproduces each map's baked native pixel
  // density, and only corrects aspect below 1:10M), this gives correct
  // physical scale AND correct aspect at every scale — it drives dpp from
  // real ground distances via the ported MapScaleUtil::ResolutionToDegrees.
  //
  // mm_per_pixel is the zoom knob: larger = more ground per pixel = zoomed
  // out. Mutually exclusive with SetScale/SetResolution; last call wins.
  // Scale() reports scale_denominator in this mode.
  Status SetPhysicalScale(double scale_denominator, double mm_per_pixel);
  double MmPerPixel() const { return mm_per_pixel_; }

  // Turns the chart clockwise on the screen about the surface centre. Any
  // finite value is accepted and wrapped into [0, 360); Rotation() reports
  // the wrapped value. Independent of the dpp modes above — rotation changes
  // where a degree lands, never how big it is.
  Status SetRotation(double degrees);
  double Rotation() const { return rot_deg_; }

  /// Selects the display projection. All five are implemented.
  Status SetProjectionType(ProjectionType type);
  ProjectionType Type() const { return type_; }

  /// True when geo->surface is affine (Equal Arc at any rotation). Code that
  /// relies on straight lines staying straight asks this, not the type.
  bool IsAffine() const { return type_ == ProjectionType::kEqualArc; }

  bool Ready() const { return ready_; }
  PixelSize SurfaceSize() const { return {width_, height_}; }
  GeoPoint Center() const { return center_; }
  double Scale() const { return scale_; }
  double DegPerPixelLat() const { return dpp_lat_; }
  double DegPerPixelLon() const { return dpp_lon_; }

  // Geographic bounds of the surface (lat clamped to +/-90; the rect
  // crosses the antimeridian when the viewport does).
  //
  // Under rotation this is the axis-aligned box of the ROTATED viewport —
  // the four turned corners, up to sqrt(2) larger at 45 degrees. That growth
  // is the real cost of a turned chart: it is what every source is queried
  // with, so a turned frame reads more data than a straight one.
  GeoRect VmapBounds() const;

  /// Unwrapped longitude extent of the viewport box: west <= Center().lon <=
  /// east, and east - west may exceed 360 when the view wraps the world.
  /// VmapBounds cannot say that; the engine needs it to draw a frame more
  /// than once. Unported projections return kUnsupported.
  Status VmapLonRange(double* west, double* east) const;

  /// The one point whose image is an arc rather than a point: the antipode of
  /// an Azimuthal Equidistant centre, which the whole rim of the disc is.
  /// kNotFound for every other projection. A caller that bounds a projected
  /// region by the image of its boundary — the engine's raster path does —
  /// must widen to the whole surface when the region contains this point,
  /// because an interior singularity is not covered by the boundary's image.
  Status SingularPoint(GeoPoint* p) const;

  /// Whether each pole falls inside the surface. Only the azimuthal pair and
  /// Lambert can hold one; the cylindrical projections never do. Callers that
  /// walk a line of constant latitude need this, because a line that encloses
  /// a pole has no left and right on the surface.
  Status PoleOnSurface(bool* north, bool* south) const;

  // Geo <-> surface in the current projection. Longitude deltas are taken the
  // short way around relative to the center, so viewports spanning the
  // antimeridian work without special-casing by callers. Mercator reports
  // kNotProjectable beyond kMercatorMaxLat in either direction.
  Status GeoToSurface(const GeoPoint& p, double* sx, double* sy) const;
  Status SurfaceToGeo(double sx, double sy, GeoPoint* p) const;

  /// GeoToSurface without the short-way unwrap: p.lon is taken as given, so
  /// a longitude 200 degrees east of the centre lands 200 degrees east. For
  /// callers that already work in an unwrapped frame wider than 360 degrees.
  Status GeoToSurfaceUnwrapped(const GeoPoint& p, double* sx,
                               double* sy) const;

  /// Ground metres per surface pixel at one point, and the angle from grid
  /// north to true north there. DegPerPixelLat/Lon are the centre scale
  /// (contracts D6); this is the scale where the caller is working.
  struct LocalScale {
    double m_per_px_x = 0;
    double m_per_px_y = 0;
    /// Angle from true north to grid north, positive clockwise on the
    /// surface. Zero for Equal Arc and Mercator, n * delta-lon for Lambert.
    /// The chart rotation is not included: this is a projection property.
    double convergence_deg = 0;
  };
  Status LocalScaleAt(const GeoPoint& p, LocalScale* out) const;

 private:
  Status Update();  // recompute dpp when center+scale are known

  /// Per-viewport constants of the non-affine projectors, named after their
  /// Windows members. Recomputed by Update() whenever centre, scale or surface
  /// changes; Equal Arc reads none of them.
  struct ProjectionConstants {
    double center_lat_for_calculations = 0;  // centre after validate_center
    double mosaic_m_per_px_lat = 0;          // ground metres per surface pixel
    double mosaic_m_per_px_lon = 0;
    double lambert_n = 0;                    // cone constant
    double lambert_F = 0;
    double lambert_rho_0 = 0;                // metres, at the centre latitude
    bool lambert_uses_mercator = false;      // |centre lat| < one pixel
    double std_parallel = 0;                 // Mercator, degrees
    double cos_std_parallel = 1;             // cos of the above, precomputed
    double mercator_y0 = 0;                  // projection-plane y of the centre
    double sin_center_lat = 0;               // azimuthal, at the centre above
    double cos_center_lat = 1;
  };
  Status UpdateProjectionConstants();
  Status UpdateMercatorConstants();
  Status UpdateLambertConstants();
  Status UpdateAzimuthalConstants();

  /// LocalScaleAt for the two azimuthal projections, the only ones whose
  /// scale depends on the direction it is measured in.
  Status AzimuthalLocalScaleAt(const GeoPoint& p, LocalScale* out) const;

  /// Mercator forward/inverse about the surface centre, in surface-pixel
  /// offsets, with no rotation and no kMercatorMaxLat check. The centre used
  /// is pc_.center_lat_for_calculations, so the pair is self-consistent while
  /// UpdateMercatorConstants is still iterating towards it.
  void MercatorForwardRaw(double lat, double dlon, double* dx,
                          double* dy) const;
  void MercatorInverseRaw(double dx, double dy, double* lat,
                          double* dlon) const;

  /// Lambert forward/inverse about the surface centre, in surface-pixel
  /// offsets, with no rotation. The inverse returns false where the Windows
  /// projector reports FAILURE (a point on the cone apex, or a cone constant
  /// of zero) or where the arithmetic leaves the real line.
  void LambertForwardRaw(double lat, double dlon, double* dx,
                         double* dy) const;
  bool LambertInverseRaw(double dx, double dy, double* lat,
                         double* dlon) const;

  /// Azimuthal Equidistant forward/inverse about the surface centre, in
  /// surface-pixel offsets, with no rotation. The forward returns false at
  /// the antipode of the centre, whose image is the whole rim rather than a
  /// point, and the inverse outside the disc of radius pi * R, which is where
  /// the sphere runs out.
  bool AzEqForwardRaw(double lat, double dlon, double* dx, double* dy) const;
  bool AzEqInverseRaw(double dx, double dy, double* lat, double* dlon) const;

  /// Orthographic forward/inverse about the surface centre, in surface-pixel
  /// offsets, with no rotation. The forward returns false on the far
  /// hemisphere and the inverse outside the disc of radius R; both still
  /// write the plane point, so a caller drawing a clipped edge can use it.
  bool OrthoForwardRaw(double lat, double dlon, double* dx, double* dy) const;
  bool OrthoInverseRaw(double dx, double dy, double* lat, double* dlon) const;

  /// Point `c_rad` of arc from the projection centre along azimuth `az_rad`,
  /// as latitude and degrees east of the centre. Draws the limb of the
  /// azimuthal projections, which is where their bounds are decided.
  void PointAtAzimuth(double c_rad, double az_rad, double* lat,
                      double* dlon) const;

  /// Extremes of latitude and of unwrapped longitude over the surface
  /// boundary, plus whether the near pole falls on the surface. Backs both
  /// VmapBounds and VmapLonRange for the projections whose boundary is not
  /// the box of its corners.
  struct BoundarySweep {
    double lat_min = 0, lat_max = 0;
    double dlon_min = 0, dlon_max = 0;  // degrees east of the centre
    bool pole_on_surface = false;  // Lambert: the pole its cone closes on
    double pole_lat = 0;
    bool north_pole_on_surface = false;
    bool south_pole_on_surface = false;
    /// The surface reaches every meridian: a pole is on it, or (Azimuthal
    /// Equidistant) it extends past the rim, where all meridians meet.
    bool all_longitudes = false;
    bool valid = false;
  };
  BoundarySweep SweepProjectedBoundary() const;

  /// Surface-pixel half-extents of the viewport box relative to its centre,
  /// widened to the box of the turned corners under rotation.
  void ViewportOffsets(double* x_min, double* x_max, double* y_min,
                       double* y_max) const;

  // Surface offset (dx right, dy down, relative to the surface centre) turned
  // clockwise by the current rotation; RotateInverse undoes it. Both are the
  // identity when rotation is 0 and are not called on that path at all.
  void RotateOffset(double dx, double dy, double* rx, double* ry) const;
  void RotateOffsetInverse(double dx, double dy, double* rx, double* ry) const;

  // How dpp is derived. kScale = MapScaleUtil native density; kResolution =
  // caller-supplied dpp; kPhysical = physical-display scale (dpp from ground
  // metres/pixel). Last setter wins.
  enum class Mode { kScale, kResolution, kPhysical };

  int width_ = 0, height_ = 0;
  GeoPoint center_;
  double scale_ = 0;
  double dpp_lat_ = 0, dpp_lon_ = 0;
  double mm_per_pixel_ = 0;  // kPhysical only
  double rot_deg_ = 0;       // clockwise, [0, 360)
  double rot_cos_ = 1, rot_sin_ = 0;
  bool have_center_ = false;
  Mode mode_ = Mode::kScale;
  ProjectionType type_ = ProjectionType::kEqualArc;
  ProjectionConstants pc_;
  bool ready_ = false;
};

// Unwraps lon into the +/-180-degree window around ref (helper shared with
// the engine's per-frame overlap math).
double UnwrapLonNear(double lon, double ref);

}  // namespace fv
