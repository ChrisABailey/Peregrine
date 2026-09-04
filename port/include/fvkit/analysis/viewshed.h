// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/analysis/viewshed.h — what can be seen from a point on the earth,
// given the terrain in the way.
//
// AN3 of port/analysis-plan.md. Transcribed from
// fvw_core/Intervisibility/{Viewshed,GeoPoint}.cs — the one piece of
// FalconView written in C#, reached through COM by the Range & Bearing
// overlay's CTerrainMaskObj, which is only a container for the array this
// returns.
//
// THE ALGORITHM IS XDRAW, from Franklin's "Geometric Algorithms for Siting of
// Air Defense Missile Batteries" (1994). A square lattice centred on the
// observer is grown one ring at a time; each new post inherits a LINE OF
// SIGHT SLOPE from the one or two already-computed posts between it and the
// centre, and stores back either "visible" or the height something would have
// to be at that post to be seen from the observer. That stored height, not a
// visible/not-visible flag, is what the mask actually draws — it is why a
// viewshed can answer "how high must an aircraft fly to be seen here?" as
// well as "can I see the ground here?".
//
// The ring order is not decorative: a post's parents must already hold their
// slopes, so each ring walks the four cardinals, then the four corners, then
// the eight wall octants, and only the walls take two parents.
//
// What was kept, and why each one matters:
//
//   * THREE HEIGHT METHODS, COMPUTED TOGETHER. `kMin` inherits the more
//     pessimistic of a wall post's two parent slopes, `kMax` the more
//     optimistic, and `kInterpolated` a blend weighted by where the ray
//     really crosses the ring. FalconView always asks for kInterpolated
//     (INTERPOLATION_HEIGHT_CALCULATION = 2 in RangeBearing/main.cpp); the
//     other two cost nothing once the ring walk exists and they BRACKET the
//     answer, which is the invariant the tests pin.
//
//   * CURVATURE AS A PARABOLIC DROP, d^2 / 2R, with R = 6 378 135 m. Not
//     WGS84's 6 378 137, and no refraction term at all — FalconView models
//     none, and slipping a 4/3-earth in here would silently change every
//     answer this overlay has ever given.
//
//   * THE SPHERICAL DISTANCE, not geo_tool's ellipsoidal one. The drop and
//     the slope are computed over the same distance function, and mixing two
//     of them would put a metre of disagreement into every ray.
//
//   * THE SECTOR CROP AT TWO GRANULARITIES — the four cardinals and four
//     corners are tested with 45 degrees of slop, the wall octants with 22.5
//     — so that the posts feeding the sector's own boundary are computed. A
//     crop of 270 degrees or more is treated as no crop at all.
//
//   * PROGRESS AND CANCEL, fired only when the integer percent MOVES. This
//     is the one computation in the overlay slow enough to need it.
//
// What was dropped: the COM-per-post elevation lookup (the C# file's own
// header comment lists it as a defect), SAFEARRAY, the c:\intervis.txt debug
// dump, the Registry.LocalMachine read of MaxDTEDPoints (a request field
// here), and MapScaleUtil::GetDegreesPerPixel as the only way to choose a
// step (the caller decides; a helper below derives one from the source).
//
// THE TRAP, recorded so nobody "fixes" it: the rhumb-line step
// (`calculateCoordinate`) is Aviation Formulary V1.42's, which takes WEST
// longitude as positive. The port negates on the way in and out exactly as
// the C# does. Change it and every post east of the observer moves.

#ifndef FVKIT_ANALYSIS_VIEWSHED_H_
#define FVKIT_ANALYSIS_VIEWSHED_H_

#include <cstddef>
#include <vector>

#include "fvkit/formats/source.h"
#include "fvkit/geo.h"

namespace fv {
namespace analysis {

// Which of the three visible-height answers the result carries. All three are
// computed whatever this says; it selects the one written out.
enum class HeightMethod {
  kMin,           // err toward smaller heights (more of the ground "visible")
  kMax,           // err toward larger heights
  kInterpolated,  // between the two; what FalconView always asks for
};

// A wedge to crop the viewshed to: `angle_deg` wide, centred on
// `bearing_deg` true. 270 degrees or more is no crop at all, as upstream.
struct ViewshedSector {
  double bearing_deg = 0.0;
  double angle_deg = 0.0;
};

struct ViewshedRequest {
  GeoPoint observer;
  double observer_height_m = 0.0;  // above the ground AT the observer

  // Radius of the lattice, in metres along a great circle.
  double range_m = 0.0;

  // Spacing between posts, in degrees of latitude. FalconView took this from
  // the map scale (GetDegreesPerPixel at the observer's latitude); nothing
  // about the algorithm requires that, so the caller decides. See
  // ViewshedStepFromPostSpacing for the answer a terrain source gives.
  double step_deg = 0.0;

  HeightMethod method = HeightMethod::kInterpolated;

  // Cap on the TOTAL number of posts (span * span). Over it the span shrinks
  // and the STEP WIDENS — the range is never cut, which is upstream's own
  // choice and the right one: a user who asked to see 50 nm out gets 50 nm,
  // more coarsely. FalconView read this from the registry as MaxDTEDPoints.
  //
  // The default is a million posts, about 80 MB of working set. This is the
  // guard behind FalconView's "Out of Memory. Try reducing the range of the
  // intervisibility object."
  size_t max_posts = 1000000;

  bool has_sector = false;
  ViewshedSector sector;
};

struct ViewshedResult {
  // span * span values, ROW-MAJOR, row 0 the NORTHERNMOST and column 0 the
  // WESTERNMOST — so index 0 is the north-west corner and `bounds` names the
  // same two corners.
  //
  // 0 means "visible from the observer". A positive value is the height in
  // metres something would have to reach AT that post to be seen. NaN means
  // the post has no answer: its elevation was unknown, or it was cropped out
  // of the sector.
  std::vector<float> visible_height_m;

  int span = 0;   // always odd; the observer is at (span/2, span/2)
  GeoRect bounds; // ll = (south, west), ur = (north, east), from the corner posts

  double step_deg = 0.0;  // after any widening for max_posts
  bool step_was_widened = false;
  size_t no_data_count = 0;

  bool Valid() const {
    return span > 0 && visible_height_m.size() ==
                           static_cast<size_t>(span) * static_cast<size_t>(span);
  }
  float At(int row, int col) const {
    return visible_height_m[static_cast<size_t>(row) * span + col];
  }
};

// Called as the rings are walked, only when the integer percent CHANGES.
// Return false to cancel; ComputeViewshed then fails with kInterrupted and
// `out` is left empty rather than half-filled.
struct IViewshedProgress {
  virtual ~IViewshedProgress() = default;
  virtual bool OnProgress(int percent) = 0;
};

// The step a terrain source's own posts justify at `at`, in degrees of
// latitude — the COARSER of its two axes, because a lattice finer than the
// data in either direction is inventing terrain to occlude with. Returns 0
// when the source does not know its spacing, which is a legal answer: the
// caller must then choose a step some other way.
double ViewshedStepFromPostSpacing(IElevationSource& src, const GeoPoint& at);

// Computes the viewshed. `progress` may be null.
//
// Fails with kInvalidArg on an unusable request (a null out, a range or step
// that is not positive), kOutOfCoverage when the OBSERVER'S OWN post has no
// elevation — there is nothing to stand on, which is the check FalconView
// spells as IsElevationDataAvailable before it will even queue the work — and
// kInterrupted when `progress` cancelled.
Status ComputeViewshed(IElevationSource& src, const ViewshedRequest& req,
                       ViewshedResult* out,
                       IViewshedProgress* progress = nullptr);

}  // namespace analysis
}  // namespace fv

#endif  // FVKIT_ANALYSIS_VIEWSHED_H_
