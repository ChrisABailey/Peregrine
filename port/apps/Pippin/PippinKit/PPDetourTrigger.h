// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file PPDetourTrigger.h
/// When an off-route rider is offered a detour (re-route plan RR-D6): off
/// route has held `hold_s` and the cross-track error is not shrinking. Also
/// keeps what a detour plan needs from the ride: the last on-route along
/// distance and the direction of travel. Pure C++ so the mac tests it.

#pragma once

#include <cmath>
#include <deque>
#include <limits>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/nav/guidance.h"
#include "fvkit/nav/position.h"

namespace pippin {

/// Thresholds for `DetourTrigger`.
struct DetourTriggerSettings {
  /// Seconds after `kOffRoute` before the offer can appear.
  double hold_s = 10.0;
  /// The cross-track error is compared with its value this many seconds ago.
  double trend_s = 5.0;
  /// A cross-track error this much smaller than that counts as heading back.
  double closing_m = 2.0;
  /// The guidance's off-route distance: along is kept only within it.
  double on_line_m = 25.0;
  /// A derived heading needs this much movement since the last one.
  double heading_step_m = 3.0;
};

/// Watches guidance state fix by fix and says when to offer a detour.
/// Fixes without a time never make the offer ready. Not thread-safe.
class DetourTrigger {
 public:
  explicit DetourTrigger(DetourTriggerSettings settings = DetourTriggerSettings{})
      : settings_(settings) {}

  /// Feeds one fix after `fv::nav::Guidance::OnFix`, with the state it left
  /// and the events it returned. Returns true when `ready()` changed.
  bool OnFix(const fv::PositionFix& fix, const fv::nav::GuidanceState& state,
             const std::vector<fv::nav::GuidanceEvent>& events) {
    if (!fix.has_position) return false;
    const fv::GeoPoint here{fix.lat, fix.lon};
    UpdateHeading(fix, here);
    position_ = here;
    has_position_ = true;

    const bool was_ready = ready_;
    for (const fv::nav::GuidanceEvent& e : events) {
      if (e.type == fv::nav::GuidanceEventType::kOffRoute) {
        off_ = true;
        off_since_s_ = fix.time_s;
        has_off_time_ = fix.has_time;
        samples_.clear();
      } else if (e.type == fv::nav::GuidanceEventType::kRejoined) {
        off_ = false;
        ready_ = false;
        samples_.clear();
      }
    }
    if (state.valid && state.on_route && state.cross_track_m <= settings_.on_line_m) {
      last_along_m_ = state.along_m;
      has_line_along_ = true;
    } else if (state.valid && !has_line_along_) {
      // No fix on the line yet (the app was opened off route): the nearest
      // point of the line is the best guess at how far the rider got.
      last_along_m_ = state.along_m;
    }
    if (off_ && !ready_ && fix.has_time && has_off_time_ && state.valid) {
      samples_.push_back({fix.time_s, state.cross_track_m});
      // Front becomes the newest sample at least `trend_s` old.
      while (samples_.size() > 1 && samples_[1].time_s <= fix.time_s - settings_.trend_s) {
        samples_.pop_front();
      }
      const Sample& ref = samples_.front();
      const bool held = fix.time_s - off_since_s_ >= settings_.hold_s;
      const bool aged = fix.time_s - ref.time_s >= settings_.trend_s;
      if (held && aged && state.cross_track_m > ref.cross_m - settings_.closing_m) {
        ready_ = true;
      }
    }
    return ready_ != was_ready;
  }

  /// Forgets the ride: a new route, a detour applied, or GPS mode turned off.
  void Reset() {
    off_ = false;
    ready_ = false;
    samples_.clear();
    last_along_m_ = 0.0;
    has_line_along_ = false;
    heading_deg_ = std::numeric_limits<double>::quiet_NaN();
    has_heading_from_ = false;
    has_position_ = false;
  }

  /// The offer should be on screen. Stays true until rejoin or `Reset`.
  bool ready() const { return ready_; }
  /// Along the guided line at the last fix on it, metres; before any fix on
  /// it, the projection of the latest fix.
  double last_along_m() const { return last_along_m_; }
  /// Direction of travel, degrees true; NaN until known.
  double heading_deg() const { return heading_deg_; }
  bool has_position() const { return has_position_; }
  const fv::GeoPoint& position() const { return position_; }

 private:
  struct Sample {
    double time_s;
    double cross_m;
  };

  /// The receiver's course when it has one, otherwise the bearing from the
  /// position `heading_step_m` or more behind.
  void UpdateHeading(const fv::PositionFix& fix, const fv::GeoPoint& here) {
    if (fix.has_true_heading) {
      heading_deg_ = fix.true_heading_deg;
      heading_from_ = here;
      has_heading_from_ = true;
      return;
    }
    if (!has_heading_from_) {
      heading_from_ = here;
      has_heading_from_ = true;
      return;
    }
    constexpr double kDeg = M_PI / 180.0;
    constexpr double kEarthM = 6371008.8;
    const double north = (here.lat - heading_from_.lat) * kDeg * kEarthM;
    const double east = (here.lon - heading_from_.lon) * kDeg * kEarthM *
                        std::cos(0.5 * (here.lat + heading_from_.lat) * kDeg);
    if (std::hypot(north, east) < settings_.heading_step_m) return;
    heading_deg_ = std::fmod(std::atan2(east, north) / kDeg + 360.0, 360.0);
    heading_from_ = here;
  }

  DetourTriggerSettings settings_;
  bool off_ = false;
  bool ready_ = false;
  double off_since_s_ = 0.0;
  bool has_off_time_ = false;
  std::deque<Sample> samples_;
  double last_along_m_ = 0.0;
  bool has_line_along_ = false;
  double heading_deg_ = std::numeric_limits<double>::quiet_NaN();
  fv::GeoPoint heading_from_;
  bool has_heading_from_ = false;
  fv::GeoPoint position_;
  bool has_position_ = false;
};

}  // namespace pippin
