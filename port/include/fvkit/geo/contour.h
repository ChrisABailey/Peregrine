// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/geo/contour.h — geographic contours (draw plan G1).
//
// A CONTOUR is a geographic shape that reduces to a connected run of
// lat/lon points: a great-circle line, a rhumb line, a straight-in-this
// -projection line, a circle, an ellipse, an arc, a stored polyline. This is
// the left-hand side of the overlay drawing pipeline:
//
//   IGeoContour -> BuildGeoPath -> [ G3: GeoDraw ] -> ICanvas
//
// Ported from fvw_core/FvMappingGraphics/GeographicContourIterator.{h,cpp},
// with IDrawingToolsProjection replaced by fv::MapProjection. The algorithms
// are preserved (bit-faithful rule): the great-circle rotation math, the
// degrees-per-pixel step size, the polar step reduction, the Mercator rhumb
// walk and its parametric clipper. What changed is the shape of the seam, not
// the arithmetic.
//
// WHY THE PULL ITERATOR SURVIVED THE PORT. It is O(1) in memory over a line
// that may densify to thousands of points, and it composes — BuildGeoPath
// wraps ANY contour, so a new shape costs one class and reaches the canvas
// for free.
//
// WHY THE CLIP IS THE WHOLE POINT. A contour does NOT densify a geodesic and
// let the rasterizer discard it. It finds where the shape enters the viewport
// first and steps from there, so a New-York-to-Tokyo great circle drawn on a
// harbour-scale map costs a search, not forty thousand points. That is what
// the bounds-flag machinery in the .cpp buys, and it is the reason this is
// worth porting rather than rewriting from a formula.
//
// PURE GEOMETRY (draw plan R4): degrees in, degrees out. Nothing here knows
// about a canvas, a style, a symbol or a product.

#ifndef FVKIT_GEO_CONTOUR_H_
#define FVKIT_GEO_CONTOUR_H_

#include <memory>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/proj.h"

// NOTE: nothing from fv_geo_tool appears in this header, deliberately.
// RhumbLineContour needs its Mercator and GreatCircleContour needs its bounds
// helpers, but geo_tool.h carries PI / TRUE / boolean_t as macros, and an
// overlay that just wants to draw a line should not have to eat them. Hence
// the pimpl on RhumbLineContour.

namespace fv {

// How the space between two geographic points is filled in.
enum class LineKind {
  // Straight in whatever projection is currently in use: the two endpoints
  // are projected and joined. FalconView's UTIL_LINE_TYPE_SIMPLE.
  kSimple = 0,
  // Constant bearing (loxodrome). UTIL_LINE_TYPE_RHUMB.
  kRhumb,
  // Shortest path over the sphere. UTIL_LINE_TYPE_GREAT.
  kGreatCircle,
};

// ---------------------------------------------------------------------------
// The seam
// ---------------------------------------------------------------------------

class IGeoContour {
 public:
  virtual ~IGeoContour() = default;

  // Rewinds. MUST be called before the first NextPoint — several contours do
  // their entry search here, not in the constructor, exactly as the original
  // does, so that re-walking a contour costs the search again and nothing
  // else.
  virtual void MoveFirst() = 0;

  // Yields the next point, or false at the end of the contour. A contour that
  // misses the viewport entirely returns false on the first call.
  virtual bool NextPoint(GeoPoint* p) = 0;

  // Does the point JUST RETURNED start a new run? (G3)
  //
  // A flat point stream has nowhere to say "these two are not connected", and
  // before G3 it did not: PolylineContour got the intent right — a leg that
  // clipped away must break the run — and then handed BuildGeoPath a stream in
  // which the break was invisible, so a polyline whose middle leg was off
  // screen came back as ONE sub-path drawn end to end (the ledger's own §2d
  // entry, pinned as-it-behaved). This is the signal it was missing, asked
  // AFTER NextPoint and about the point that call produced.
  //
  // Default false: every contour that is a single connected run — which is all
  // of them but PolylineContour — says nothing and is unchanged.
  virtual bool AtBreak() const { return false; }
};

using GeoContourPtr = std::unique_ptr<IGeoContour>;

// ---------------------------------------------------------------------------
// Two-point lines
// ---------------------------------------------------------------------------

// Straight in the current projection: two points and nothing between them.
// When clipped it borrows the rhumb clipper for its endpoints, which is what
// the original does — the two agree closely at any scale where the difference
// would be visible, and it avoids a second clipper.
class SimpleGeoLine : public IGeoContour {
 public:
  SimpleGeoLine(const MapProjection& proj, const GeoPoint& a, const GeoPoint& b,
                bool clip);

  void MoveFirst() override;
  bool NextPoint(GeoPoint* p) override;

 private:
  GeoPoint a_, b_;
  int index_ = -1;
  bool clipped_away_ = false;
};

// The shortest path over a sphere of radius 6378 km.
//
// The walk is angular: P1 and P2 are rotated into a frame where both lie in
// the x'-y' plane, and an intermediate point is one angle step further round.
// The step comes from the projection's degrees-per-pixel (about a 20-pixel
// chord), so the point count tracks the SCREEN and not the line's length —
// which is what makes a 10,000 km arc affordable.
class GreatCircleContour : public IGeoContour {
 public:
  GreatCircleContour(const MapProjection& proj, const GeoPoint& a,
                     const GeoPoint& b, bool clip);

  void MoveFirst() override;
  bool NextPoint(GeoPoint* p) override;

 private:
  // Angles of the rotated frame; see the .cpp for the derivation, which is
  // the original's verbatim.
  bool ComputeAngles();
  bool ComputeDeltaAngle(const MapProjection& proj);
  double ComputeStartingAngle() const;
  bool IntermediateGeo(double beta, GeoPoint* out) const;
  void ForwardConversion(double x, double y, double z, double* xo, double* yo,
                         double* zo) const;
  void ReverseConversion(double x, double y, double* xo, double* yo,
                         double* zo) const;
  bool AngleAt(double lat, double lon, double* angle, double* z_out) const;

  GeoPoint a_, b_;
  GeoRect bounds_;
  GeoPoint center_;
  bool clip_ = true;

  // A line of longitude is a great circle whose two endpoints already ARE the
  // whole visible arc, so it is emitted as two points. It doubles as the
  // fallback when the rotation math cannot be set up.
  bool degenerate_ = false;
  int degenerate_count_ = 0;
  bool at_end_ = false;

  GeoPoint current_;
  double angle_ = 0.0;
  double angle_p1_ = 0.0;
  double beta_ = 0.0;
  double delta_angle_ = 0.0;
  double rotate_x_ = 0.0, rotate_y_ = 0.0;
  double cos_rx_ = 1.0, cos_ry_ = 1.0, sin_rx_ = 0.0, sin_ry_ = 0.0;
};

// Constant bearing. Straight in a Mercator projection, so the walk is a
// parametric line in Mercator space sampled back to lat/lon, and the clip is
// a parametric (Liang-Barsky) clip in the same space.
//
// Clipping can yield TWO sub-lines when the viewport wraps the world, which
// is why the clipper carries up to 4 points.
class RhumbLineContour : public IGeoContour {
 public:
  RhumbLineContour(const MapProjection& proj, const GeoPoint& a,
                   const GeoPoint& b, bool clip);
  ~RhumbLineContour() override;

  void MoveFirst() override;
  bool NextPoint(GeoPoint* p) override;

  // The clipped endpoints, exposed because SimpleGeoLine borrows them (and
  // because they are the cheapest thing to assert in a test). `count` is the
  // number of sub-LINES, so points[0..2*count-1] are meaningful.
  void ClippedPoints(GeoPoint points[4], int* count) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// Closed shapes
// ---------------------------------------------------------------------------

// FalconView's DEFAULT_NUM_POINTS_IN_GEOCIRCLE.
constexpr int kDefaultCirclePoints = 80;

// A circle of constant GROUND radius: N points at equal bearings, each
// `radius_m` away along a great circle. The first point is repeated at the
// end to close the ring.
//
// Not clipped — a circle is bounded by construction, and N is small.
class GeoCircleContour : public IGeoContour {
 public:
  GeoCircleContour(const GeoPoint& center, double radius_m,
                   int num_points = kDefaultCirclePoints);

  void MoveFirst() override;
  bool NextPoint(GeoPoint* p) override;

 private:
  GeoPoint center_;
  double radius_m_ = 0.0;
  int num_points_ = kDefaultCirclePoints;
  int index_ = 0;
};

// The same with two radii and a rotation. `rotation_deg` turns the ellipse
// clockwise from north, the bearing convention the radii are stated in.
class GeoEllipseContour : public IGeoContour {
 public:
  GeoEllipseContour(const GeoPoint& center, double vert_radius_m,
                    double horz_radius_m, double rotation_deg,
                    int num_points = kDefaultCirclePoints);

  void MoveFirst() override;
  bool NextPoint(GeoPoint* p) override;

 private:
  GeoPoint center_;
  double vert_r2_ = 0.0, horz_r2_ = 0.0;
  double rotation_deg_ = 0.0;
  int num_points_ = kDefaultCirclePoints;
  int index_ = 0;
};

// NEW in G1 (FalconView spells this several places and none of them is
// reusable): a circular arc from `start_bearing_deg` clockwise through
// `sweep_deg`. The point spacing matches a full circle of the same radius, so
// a 90-degree arc gets a quarter of the points rather than the same number
// crammed in, and an arc is NOT closed.
//
// A negative sweep runs counter-clockwise. |sweep| is clamped to 360.
class GeoArcContour : public IGeoContour {
 public:
  GeoArcContour(const GeoPoint& center, double radius_m,
                double start_bearing_deg, double sweep_deg,
                int points_per_circle = kDefaultCirclePoints);

  void MoveFirst() override;
  bool NextPoint(GeoPoint* p) override;

 private:
  GeoPoint center_;
  double radius_m_ = 0.0;
  double start_deg_ = 0.0;
  double sweep_deg_ = 0.0;
  int steps_ = 1;
  int index_ = 0;
};

// ---------------------------------------------------------------------------
// Stored geometry
// ---------------------------------------------------------------------------

// A run of geographic points with ONE LineKind for every leg, so a route, a
// coastline or a hand-drawn shape goes through the same pipe as a two-point
// line. Legs are expanded lazily through the per-leg contour, so a great
// -circle polyline is densified leg by leg and never materialised whole.
//
// The contour needs the projection because its legs do; a kSimple polyline
// still needs one and simply does not consult it.
//
// CEILING, stated rather than hidden (draw plan §6.4): this is O(1) per point
// but the caller pays per leg, and there is no simplification or scene
// retention for overlay geometry the way there is for charts. A hundred
// thousand legs will be slow.
class PolylineContour : public IGeoContour {
 public:
  PolylineContour(const MapProjection& proj, std::vector<GeoPoint> points,
                  LineKind kind, bool clip, bool closed = false);
  ~PolylineContour() override;

  void MoveFirst() override;
  bool NextPoint(GeoPoint* p) override;
  bool AtBreak() const override { return at_break_; }

 private:
  bool AdvanceLeg();

  const MapProjection& proj_;
  std::vector<GeoPoint> points_;
  LineKind kind_ = LineKind::kSimple;
  bool clip_ = true;
  bool closed_ = false;

  size_t leg_ = 0;
  GeoContourPtr current_;
  bool emitted_any_in_leg_ = false;
  // A leg's first point is the previous leg's last, so it is dropped to keep
  // the run connected — unless the previous leg emitted nothing (it was
  // clipped away), in which case the run is broken and the point is kept.
  bool drop_first_ = false;
  // Set when a leg was entered with the run broken, cleared by the emit that
  // carries the signal out. This is what AtBreak() reports.
  bool pending_break_ = false;
  bool at_break_ = false;
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

// The one call a caller normally wants for a two-point line.
GeoContourPtr MakeGeoLine(const MapProjection& proj, const GeoPoint& a,
                          const GeoPoint& b, LineKind kind, bool clip = true);

// ---------------------------------------------------------------------------
// Projection: contour -> surface sub-paths
// ---------------------------------------------------------------------------
//
// Replaces CGeographicContourLineSegments. It yields SUB-PATHS rather than the
// original's (x1,y1,x2,y2) integer segments, because everything downstream
// already speaks sub-paths — PlaceAlongPath, ClipPolyline, ICanvas::DrawLines
// — and because a break is naturally the start of a new sub-path instead of a
// flag riding alongside each segment.
//
// A sub-path is broken when a point fails to project, and when consecutive
// contour points step more than 180 degrees of longitude. That second rule is
// the ANTIMERIDIAN SEAM: a contour densifies at ~20-pixel chords, so a step
// that large can only be the projection unwrapping to the far side of the
// centre, and joining those two points would draw a spurious line clean across
// the surface.
//
// DECLARED DEVIATION. FalconView's geoline_to_surface returns a SECOND,
// wrapped segment in that case, so a world-spanning map shows the line running
// off one edge and back in the other. fv::MapProjection has no world-wrap
// duplication (it takes longitude the short way round relative to the centre),
// so we break the path and draw neither stub. Revisit if a world view needs it.
//
// Sub-paths of fewer than 2 points are dropped: they cannot be stroked and
// every consumer would have to check.
std::vector<std::vector<SurfacePoint>> BuildGeoPath(const MapProjection& proj,
                                                    IGeoContour& contour);

// Same, appending to `out` instead of allocating a fresh vector. For a caller
// drawing many lines per frame.
void BuildGeoPathInto(const MapProjection& proj, IGeoContour& contour,
                      std::vector<std::vector<SurfacePoint>>* out);

// Convenience for the common case: make a two-point line and project it.
std::vector<std::vector<SurfacePoint>> BuildGeoLinePath(
    const MapProjection& proj, const GeoPoint& a, const GeoPoint& b,
    LineKind kind, bool clip = true);

}  // namespace fv

#endif  // FVKIT_GEO_CONTOUR_H_
