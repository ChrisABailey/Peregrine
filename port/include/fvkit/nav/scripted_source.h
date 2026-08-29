// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/nav/scripted_source.h — the first position feed (nav plan MM1).
//
// A source that plays a canned track. It is FIRST on purpose (decision with
// Chris, 2026-08-15): the camera, the ownship symbol and the snap-to-road all
// need a moving ship long before there is a receiver to move it, and a
// deterministic ship is the only kind a test can assert against.
//
// TWO PROPERTIES MAKE IT TESTABLE, AND BOTH ARE DELIBERATE.
//
// 1. IT HAS NO THREAD. Start() does not spawn anything; Poll() asks the clock
//    what time it is and emits whatever is due. A shell already has a tick to
//    call it from, and a test can drive a whole flight in one loop with no
//    sleeping and no flake. (A threaded source is still perfectly legal at
//    this seam — that is what FixQueue in position.h is for.)
//
// 2. ITS CLOCK IS INJECTABLE. The default is steady_clock; a test hands it a
//    variable it advances by hand, which is what turns "does the source emit
//    the third fix at t=2.5s?" into an equality rather than a tolerance.
//    `time_scale` is the other half: 60x replays an hour of track in a
//    minute, and a test's fake clock plus a scale of 1 is exact.

#ifndef FVKIT_NAV_SCRIPTED_SOURCE_H_
#define FVKIT_NAV_SCRIPTED_SOURCE_H_

#include <cstddef>
#include <functional>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/nav/position.h"

namespace fv {

// One entry of a script: the fix, and how far into the script it is emitted.
// `t_s` is RELATIVE to the script (0 = the moment of Start); the fix's own
// `time_s` is absolute epoch time, so a replay can be re-based without
// touching the schedule.
struct ScriptedFix {
  double t_s = 0.0;
  PositionFix fix;
};

// Seconds, monotonic, from any origin. Only differences are ever taken.
using MonotonicClock = std::function<double()>;

// The port's steady_clock, in seconds.
double SteadyClockSeconds();

class ScriptedSource : public PositionSourceBase {
 public:
  ScriptedSource() = default;
  explicit ScriptedSource(std::vector<ScriptedFix> track);

  // Replaces the track. Stops a running source: a script swapped underneath a
  // running one would emit from an index into a track that no longer exists.
  void SetTrack(std::vector<ScriptedFix> track);
  const std::vector<ScriptedFix>& track() const { return track_; }

  // Defaults to SteadyClockSeconds. Setting it while running re-bases the
  // origin, so the elapsed time does not jump.
  void SetClock(MonotonicClock clock);

  // Wall seconds -> script seconds. Must be > 0; anything else is ignored.
  void SetTimeScale(double scale);
  double time_scale() const { return time_scale_; }

  // Replays from the top when the last fix has been emitted. The last entry
  // and the next lap's first are the SAME instant, so a track meant to loop
  // should be a closed circuit — otherwise the ship teleports home once a lap.
  void SetLooping(bool looping);
  bool looping() const { return looping_; }

  // kInvalidArg on an empty track. Emits everything due at t = 0 before it
  // returns, so a script beginning at 0 delivers its first fix from inside
  // Start() — which is why SetListener must be called first.
  Status Start() override;
  void Stop() override;

  // Emits every fix whose time has come, in order, and returns how many.
  // Cheap and idempotent between fixes: call it as often as the shell ticks.
  std::size_t Poll();

  // Script seconds since Start (scaled). 0 when not running.
  double script_time_s() const;

  // True when the track has run out and looping is off.
  bool finished() const { return !looping_ && next_index_ >= track_.size(); }

  std::size_t next_index() const { return next_index_; }

  // The script's own span, last t minus first t. 0 for a track of one.
  double duration_s() const;

 private:
  std::vector<ScriptedFix> track_;
  MonotonicClock clock_;
  double time_scale_ = 1.0;
  bool looping_ = false;
  double origin_ = 0.0;       // clock reading at Start
  std::size_t next_index_ = 0;
};

// ---------------------------------------------------------------------------
// Building a track
// ---------------------------------------------------------------------------

// How BuildScriptedTrack samples a path.
struct ScriptedTrackOptions {
  // Seconds between fixes. 1 Hz is what a receiver typically delivers.
  double sample_interval_s = 1.0;

  // Epoch seconds stamped on the first fix; each later one is
  // start_time_s + its t_s. 0 leaves the fixes' `has_time` false, since a
  // 1970 timestamp is worse than none.
  double start_time_s = 0.0;

  // Fill in the fields a receiver would report. Heading is the great-circle
  // bearing of the leg the sample sits on, which is what makes a track built
  // this way exercise HeadingResolver's REPORTED branch; turning it off is
  // how a test exercises the derived one.
  bool set_speed = true;
  bool set_true_heading = true;

  // Constant altitude and quality, when wanted.
  bool set_altitude = false;
  double altitude_msl_m = 0.0;
  bool set_hdop = false;
  double hdop = 1.0;

  // Emit the path's final vertex as a fix of its own even when it does not
  // fall on a sample boundary, so a track always ends where the path does.
  bool include_endpoint = true;
};

// Samples `path` at a constant ground speed and returns the script.
//
// The path is walked with geo_tool's great-circle range/bearing, the same
// geodesy the rest of the port measures with, so a track built from a routed
// polyline drives on the real road: this is the plan's "a helper builds that
// vector from a Route". It takes the POLYLINE and not a `Route` on purpose —
// fvkit does not link port/Routing, and `route.geometry` is exactly this
// argument.
//
// Returns an empty script for a path of fewer than two points, a speed <= 0,
// or an interval <= 0 (there is no track to play in any of those cases).
std::vector<ScriptedFix> BuildScriptedTrack(const std::vector<GeoPoint>& path,
                                            double speed_mps,
                                            const ScriptedTrackOptions& options = {});

// ---------------------------------------------------------------------------
// Replaying a RECORDED track (MM6)
// ---------------------------------------------------------------------------

struct FixScriptOptions {
  // What to use when the fixes carry no usable timestamps at all: a constant
  // interval, so a track with no clock still plays at a plausible rate rather
  // than arriving in one frame.
  double fallback_interval_s = 1.0;

  // Drop a fix that is not strictly later than the one before it. Devices do
  // repeat a timestamp, and a duplicate schedule entry is a stutter.
  bool drop_non_monotonic = true;

  // Cap a gap between consecutive fixes, 0 for none. A ride with a 40-minute
  // coffee stop in the middle of it otherwise replays the coffee stop.
  double max_gap_s = 0.0;
};

// Turns recorded fixes into a script that plays at the speed they were
// recorded at, using each fix's OWN timestamp for its schedule.
//
// This is the seam the two recorded-track readers arrive at — `ReadGpxFile`
// (fvkit/nav/gpx.h) and `ReadNmeaLog` (fvkit/nav/nmea.h) both produce exactly
// this argument — and it is why the moving map has one replay path rather than
// one per file format. The fixes are otherwise passed through UNTOUCHED: their
// speed, altitude and quality are what the device recorded, and the schedule
// is the only thing this function invents.
//
// Returns an empty script for fewer than two fixes.
std::vector<ScriptedFix> BuildScriptedTrackFromFixes(const std::vector<PositionFix>& fixes,
                                                     const FixScriptOptions& options = {});

}  // namespace fv

#endif  // FVKIT_NAV_SCRIPTED_SOURCE_H_
