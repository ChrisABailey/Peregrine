// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// Replaying a recorded track on a clock: reading it, making it replayable,
/// and sampling it at any time.

#ifndef FVKIT_NAV_TRACK_REPLAY_H_
#define FVKIT_NAV_TRACK_REPLAY_H_

#include <cstddef>
#include <string>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/nav/heading.h"
#include "fvkit/nav/position.h"

namespace fv {

/// Reads a GPX track (`.gpx`) or an NMEA log (anything else) into its fixes.
Status ReadTrackFixes(const std::string& path, std::vector<PositionFix>* out);

/// Makes fixes replayable on a clock: keeps those with a position, drops a
/// fix not strictly later than the one before it, and when no fix has a time
/// gives them one a second apart from the epoch.
std::vector<PositionFix> ReplayableFixes(std::vector<PositionFix> fixes);

/// Where a replay clock time falls against a track.
enum class ReplayPhase { kBefore, kDuring, kAfter };

/// A recorded track sampled at one time.
struct TrackState {
  /// Interpolated between the bracketing fixes; the first or last fix outside
  /// the track. `time_s` is the track time the position belongs to. The
  /// heading is set only when the earlier fix reported one.
  PositionFix fix;
  ReplayPhase phase = ReplayPhase::kDuring;
  /// How many fixes are at or before the time: the past part of the trail.
  std::size_t past = 0;
  /// True heading: the earlier fix's reported one, else the bearing of the
  /// segment (the first or last segment outside the track), derived.
  ResolvedHeading heading;
  /// The segment `heading` was taken from; equal points when the track has
  /// only one distinct position.
  GeoPoint segment_from;
  GeoPoint segment_to;
};

/// Samples `track` (positioned, timed, strictly increasing time) at `t_s`.
/// Position, altitude and speed are interpolated linearly. An empty track
/// returns a fix with no position.
TrackState TrackStateAt(const std::vector<PositionFix>& track, double t_s);

/// One clock every replayed track is placed at.
struct ReplayClock {
  double time_s = 0.0;  ///< seconds since 1970-01-01 UTC
  bool playing = false;
  double rate = 1.0;  ///< track seconds per wall-clock second while playing
};

}  // namespace fv

#endif  // FVKIT_NAV_TRACK_REPLAY_H_
