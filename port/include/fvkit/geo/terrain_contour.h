// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/geo/terrain_contour.h — elevation posts in, contour polylines out.
// The tracing half of FalconView's Contour Lines overlay
// (Applications/FalconView/Contour/ContourLists.cpp); the overlay itself is
// fvkit/overlay/contour_overlay.h. Plan: port/contour-plan.md, step C1.
//
// NOT TO BE CONFUSED WITH fvkit/geo/contour.h, which is next door and is
// about something else entirely: an `IGeoContour` there is a geographic CURVE
// a user draws (a great circle, an arc, an ellipse). This header is about
// TERRAIN contours — lines of constant elevation traced through a grid of
// posts. The word is overloaded in the product and in the literature, and the
// two files never refer to each other.
//
// WHAT CAME ACROSS FROM ContourLists.cpp, and it is the geometry only:
// a crossing sits where a cell edge's two posts bracket the level, at the
// LINEARLY INTERPOLATED position between them. That is FalconView's formula
// and every vertex here lands where its vertex landed.
//
// WHAT DID NOT: the assembly. The original allocates a heap `CContourPoint`
// per crossing, keys it by the two cell ids that share its edge, files it in a
// `multimap<CellID,·>` per level, and then rebuilds polylines by searching the
// eight neighbouring cells of every point, pushing forks on a std::list stack
// and scanning that stack to notice a loop closing. It is 400 lines, it leans
// on unsigned wraparound (`CellID(row - 1, ...)` at row 0), and its saddle
// behaviour is whatever the multimap's iteration order happens to be. This is
// marching squares with the standard 16-case table, a mean-value saddle rule,
// and a join pass over edge ids: same crossings, linear rather than
// log-linear, and deterministic in a way the original was not.
//
// THREE DIFFERENCES THAT ARE VISIBLE ON A MAP, all of them deliberate:
//
//   * VOIDS. A DTED void arrives here as NaN (contracts D4). A cell with any
//     NaN corner emits nothing. FalconView converted a missing post to
//     -32767 metres and traced through it, which hangs a fan of contours off
//     every hole in the coverage at every level between the terrain and the
//     bottom of the sea floor.
//   * SADDLES are resolved by the cell's mean, so the two branches separate
//     the same way every time the same tile is traced.
//   * A CLOSED RING KNOWS IT IS ONE (`ContourLine::closed`), which the
//     labeller and the smoother both need and which the original could only
//     discover by comparing endpoints after the fact.

#ifndef FVKIT_GEO_TERRAIN_CONTOUR_H_
#define FVKIT_GEO_TERRAIN_CONTOUR_H_

#include <vector>

#include "fvkit/formats/source.h"
#include "fvkit/geo.h"

namespace fv {

// A rectangular block of elevation posts, metres above MSL, NaN for a void.
//
// ROW 0 IS THE SOUTH ROW and column 0 the west one, so a post's index grows
// with its latitude — the opposite of the D4 pixel convention, and the same
// flip FalconView performed by hand in `load_single_tile` before tracing.
// Doing it here means the tracer never reasons about a flip and a caller
// never has to remember one.
//
// The corner posts sit ON the bounds: post (0,0) is at bounds.ll and post
// (height-1, width-1) at bounds.ur. Two tiles that share an edge must
// therefore share that edge's posts, which is why the overlay samples one
// post of overlap (see contour_overlay.h).
// The bounds must NOT cross the antimeridian: LonStep() would come out
// negative and every column would run backwards. SampleElevationGrid refuses
// one, and the overlay's tile lattice is aligned so that tiles abut +/-180
// rather than straddle it.
struct ElevationGrid {
  GeoRect bounds;
  int width = 0;   // posts west to east
  int height = 0;  // posts south to north
  std::vector<float> meters;  // width*height, row-major, row 0 = south

  bool Valid() const {
    return width >= 2 && height >= 2 &&
           meters.size() == static_cast<size_t>(width) * height;
  }
  float At(int row, int col) const {
    return meters[static_cast<size_t>(row) * width + col];
  }
  double LatStep() const {
    return (bounds.ur.lat - bounds.ll.lat) / (height - 1);
  }
  double LonStep() const {
    return (bounds.ur.lon - bounds.ll.lon) / (width - 1);
  }
};

// Fills a grid by asking `src` for every post. A post the source cannot
// answer (out of coverage, read error, or a void) becomes NaN — an elevation
// source with a hole in it is a normal thing to draw over, not a failure.
// Fails only on an unusable request (w/h < 2, a null out).
//
// `covered`, when given, is filled with one byte per post: 1 where the source
// ANSWERED (the post is inside its coverage, even if the value it gave is a
// void NaN) and 0 where it refused. The tracer does not care — a NaN kills
// its cell either way — but the TA mask does, and the difference is the whole
// of its no-data colour: a hole INSIDE the terrain under an aircraft is worth
// shouting about, and ground the source simply does not reach is not.
Status SampleElevationGrid(IElevationSource& src, const GeoRect& bounds,
                           int width, int height, ElevationGrid* out,
                           std::vector<unsigned char>* covered = nullptr);

// One traced line at one level.
//
// `level_index` is the integer multiple of the interval, which is what makes
// "is this a major contour?" the exact test `level_index % divisions == 0`.
// FalconView threw the index away, kept millimetres in an int, and had to ask
// `((level + err/2) % major) > err` with a 5% fudge to get it back.
struct ContourLine {
  double level_m = 0.0;
  int level_index = 0;
  bool closed = false;  // last point == first point
  std::vector<GeoPoint> points;
};

// Every contour at every multiple of `interval_m` crossing the grid.
//
// Ordering is deterministic: ascending level, and within a level the order the
// chains were started in a row-major sweep. A caller may rely on it (the tests
// do); nothing about the picture depends on it, since contours at one level
// never touch.
//
// A post whose elevation is EXACTLY the level counts as below it. That is the
// whole of the degeneracy handling and it is enough: with a strict `>` test
// every corner is on exactly one side, so no cell is ambiguous, the crossing
// on an edge that ends at such a post lands exactly on the post, and the
// zero-length segment that can produce is dropped at the join.
std::vector<ContourLine> TraceElevationContours(const ElevationGrid& grid,
                                                double interval_m);

// The same tracer at an explicit, NAMED set of levels rather than at every
// multiple of an interval — FalconView's `CContourLists::TraceClearanceContours`,
// which is the terrain-avoidance mask's half of ContourLists.cpp and is the
// function above with three altitudes hard-coded into it (warning, caution,
// OK) instead of a ladder. Plan: port/tamask-plan.md TA2.
//
// `level_index` on each returned line is the INDEX INTO `levels_m` as the
// caller wrote it, not a multiple of anything, so a caller with three named
// bands gets 0, 1 and 2 back and can colour by band without comparing
// doubles. The list need not be sorted and the crossing rule is identical: a
// post exactly on a level counts as below it.
std::vector<ContourLine> TraceElevationContoursAtLevels(
    const ElevationGrid& grid, const std::vector<double>& levels_m);

}  // namespace fv

#endif  // FVKIT_GEO_TERRAIN_CONTOUR_H_
