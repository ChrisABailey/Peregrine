// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/analysis/profile.h — the terrain profile along a path: what the
// ground does under a line, a polyline, or a route.
//
// AN2 of port/analysis-plan.md.
//
// THIS IS A REPLACEMENT, NOT A TRANSCRIPTION, and the plan says why at
// length. In one paragraph: Elevation_Chart::set_elevation_RB asks the map
// server for an N x N BLOCK of DTED over the two endpoints' bounding box and
// then reads a DIAGONAL out of that block, with four hand-written index
// expressions chosen by quadrant of travel (`data[i*(n+1)]` going NW->SE,
// `data[i*(n-1)]` going NE->SW). That is the line itself only when the line
// runs at 45 degrees, the stride is wrong in two of the four quadrants
// regardless, and it costs N^2 elevation reads to produce N samples — which is
// why the segment count is capped at 500. The multi-point variant is worse:
// it samples the TURNING POINTS ONLY, so a leg over a ridge charts as a
// straight line between its ends.
//
// Walking the path is N reads, correct at every bearing, and identical code
// for a two-point line, a polyline and a route. Nothing observable is lost:
// the chart's axis, its units and its segment count all survive into AN6.
//
// Two decisions of our own:
//
//   * A HOLE IS NOT A FAILURE. Windows pops "No data available." and refuses
//     to draw anything at all if any sample is void, which is the wrong
//     answer for a 300 nm route with one gap in it. A void post here is NaN
//     with `has_data == false`, the profile still returns, and
//     `no_data_count` is what a UI says something about.
//
//   * DO NOT SAMPLE FINER THAN THE POSTS. The same rule ElevationTiling
//     applies for the same reason — a profile sampled at 10 m over 3-arcsecond
//     DTED draws interpolation, not terrain. `clamp_to_post_spacing` is on by
//     default and reports what it did.

#ifndef FVKIT_ANALYSIS_PROFILE_H_
#define FVKIT_ANALYSIS_PROFILE_H_

#include <cstddef>
#include <vector>

#include "fvkit/analysis/path.h"
#include "fvkit/formats/source.h"
#include "fvkit/geo.h"

namespace fv {
namespace analysis {

struct ProfilePoint {
  GeoPoint at;
  double distance_m = 0.0;    // along the path from its first vertex
  float elevation_m = 0.0f;   // NaN when !has_data
  bool has_data = false;
  bool is_vertex = false;     // a turning point of the path, not an interior sample
};

struct ProfileResult {
  std::vector<ProfilePoint> points;

  // Over the samples that HAVE data. All zero when none do.
  double min_m = 0.0;
  double max_m = 0.0;
  double gain_m = 0.0;  // sum of the rises between consecutive KNOWN samples
  double loss_m = 0.0;  // sum of the falls, as a positive number

  double total_distance_m = 0.0;
  size_t no_data_count = 0;

  // The spacing actually used, after any clamp to the source's post spacing.
  // A UI that offered the user a step should say when it did not get it.
  double step_m = 0.0;
  bool step_was_clamped = false;

  bool Empty() const { return points.empty(); }
};

struct ProfileOptions {
  // Exactly one of these drives the sampling. `sample_count` is the Windows
  // "Segments" box (it clamped to [3, 500]; we do not, because the cap was a
  // consequence of the N^2 read pattern that is gone). A count of 0 means
  // "use step_m".
  size_t sample_count = 0;
  double step_m = 0.0;

  // With a step (not a count): keep every turning point as a sample. On by
  // default — a polyline profile that does not show its vertices is a lie
  // about where the legs are. Ignored when sampling by count, where the
  // samples are evenly spaced by definition.
  bool include_vertices = true;

  // Widen the step to the source's native post spacing when it is coarser.
  // See the header note. Applies to STEP sampling only: a sample count is a
  // chart's x-axis and the user picked it, so it is honoured as asked even
  // when it lands finer than the posts (the result still reports the spacing
  // it worked out to).
  bool clamp_to_post_spacing = true;

  // Hard cap on samples, whichever way the sampling was asked for. The step
  // is widened to fit; the path is never truncated. 0 means no cap.
  size_t max_samples = 20000;
};

// Samples `src` along `path`.
//
// Fails (kInvalidArg) only on an unusable request: a null out, an empty path,
// or options that ask for neither a count nor a step. A path the source has
// no coverage for is a successful profile with every point `!has_data` — that
// is a real answer and a chart can draw it.
Status SampleTerrainProfile(IElevationSource& src, const GeoPath& path,
                            const ProfileOptions& opts, ProfileResult* out);

}  // namespace analysis
}  // namespace fv

#endif  // FVKIT_ANALYSIS_PROFILE_H_
