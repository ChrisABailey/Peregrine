// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPFollowCadence.h — how long the follow slew should take.
//
// Course-up recentres on every fix (`CameraModes::continuous`), which is what
// turns the chart with the rider instead of waiting for an apron. MM3's slew
// header warns what that costs: a target arriving every fix restarts the
// animation every fix, so an ease-in-out spends its life in its own slow
// opening and the map lags by about one duration. Its advice is a short
// duration or `kLinear`, and both need a number, which is not a constant but
// how often the retargets arrive:
//
//   * shorter than the interval, the map lunges then stands still, which
//     reads as a stutter once a second;
//   * longer, the map is still travelling toward fix N when fix N+1 retargets
//     it, so it converges on a place the ship has left and trails
//     permanently;
//   * equal to the interval, the motion is continuous: the map reaches the
//     last reported position exactly as the next arrives, so the chart slides
//     at the rider's own speed and the ship oscillates about its anchor by
//     one fix's worth of travel, a few points on a phone.
//
// So the duration is measured. A phone receiver reports at 1 Hz and the demo
// feed at `demo_time_scale` times its recorded schedule, 4x by default.
//
// What is measured is not the receiver's rate but the interval between the
// ticks that saw a fix. MM4's `Tick` drains its queue in a loop and reports
// one `new_fix` however many arrived, so on a phone whose render loop is
// slower than its feed the retargets do arrive at the slower rate, and a
// duration cut to the nominal 1 Hz would be too short for the map it drives.
// What the slew is asking is how long until the camera is aimed somewhere
// else.
//
// Pure C++ so the mac tests it, like `PPCameraFit.h` and `PPBaseCoverage.h`.

#pragma once

#include <algorithm>
#include <cmath>

namespace pippin {

// The knobs, all of them from the pack (`[movingmap]`) except `smoothing`.
struct FollowCadenceSettings {
  // What the estimate is before anything has been measured, and what it
  // returns to after a gap. One second is a phone receiver's own rate.
  double initial_s = 1.0;
  // The answer is clamped to this band. The floor keeps a burst of fixes from
  // asking for an animation shorter than a frame; the ceiling keeps a feed
  // that has slowed to a crawl from turning the follow into a drift.
  double min_s = 0.2;
  double max_s = 2.0;
  // Longer than this is a gap rather than a slow feed — a tunnel, a pocket, a
  // backgrounded app — and folding one into the average would poison it for a
  // dozen fixes. A gap resets the estimate instead, which is also honest:
  // nothing is known about the cadence of a feed that has just come back.
  double gap_s = 10.0;
  // Weight of the newest interval in the running average, in (0, 1]. 0.4
  // settles within about four fixes and still ignores a single late frame;
  // 1.0 would be the last interval, which is the frame clock's jitter applied
  // directly to the camera.
  double smoothing = 0.4;
};

// A running estimate of how often the camera is being re-aimed.
//
// Deliberately not a filter over the timestamps in the fixes: what matters is
// when the shell saw them, and a receiver's stamps are its own clock. MM6
// makes the same distinction for the scripted source, where the two differ by
// `demo_time_scale`.
class FixCadence {
 public:
  FixCadence() { Reset(); }
  explicit FixCadence(const FollowCadenceSettings& settings)
      : settings_(settings) {
    Reset();
  }

  void SetSettings(const FollowCadenceSettings& settings) {
    settings_ = settings;
    Reset();
  }
  const FollowCadenceSettings& settings() const { return settings_; }

  // Forgets everything measured. Called when the mode goes on: an estimate
  // left from a ride that ended twenty minutes ago describes nothing.
  void Reset() {
    estimate_s_ = settings_.initial_s;
    last_s_ = 0.0;
    has_last_ = false;
  }

  // A tick saw a fix, at this wall time.
  void Observe(double now_s) {
    if (!std::isfinite(now_s)) return;
    if (!has_last_) {
      last_s_ = now_s;
      has_last_ = true;
      return;  // one stamp is not an interval
    }
    const double dt = now_s - last_s_;
    last_s_ = now_s;
    // A clock that went backwards, or two fixes inside one tick. Neither is
    // an interval and neither says anything about the next one.
    if (!(dt > 0.0)) return;
    if (dt > settings_.gap_s) {
      estimate_s_ = settings_.initial_s;
      return;
    }
    const double w = std::min(1.0, std::max(0.0, settings_.smoothing));
    estimate_s_ += w * (dt - estimate_s_);
  }

  // How long the next slew should take. Always inside [min_s, max_s].
  double interval_s() const {
    if (!std::isfinite(estimate_s_)) return settings_.initial_s;
    return std::max(settings_.min_s, std::min(settings_.max_s, estimate_s_));
  }

  // The unclamped running average: what a probe prints, and what a test pins
  // when it wants the filter rather than the band.
  double raw_estimate_s() const { return estimate_s_; }
  bool has_observed() const { return has_last_; }

 private:
  FollowCadenceSettings settings_;
  double estimate_s_ = 1.0;
  double last_s_ = 0.0;
  bool has_last_ = false;
};

}  // namespace pippin
