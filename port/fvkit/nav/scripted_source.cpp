// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/scripted_source.h"

#include <chrono>
#include <utility>

#include "geo_tool.h"  // fv_geo_tool: great-circle range/bearing and end point

namespace fv {

double SteadyClockSeconds() {
  using Clock = std::chrono::steady_clock;
  const auto now = Clock::now().time_since_epoch();
  return std::chrono::duration<double>(now).count();
}

ScriptedSource::ScriptedSource(std::vector<ScriptedFix> track)
    : track_(std::move(track)) {}

void ScriptedSource::SetTrack(std::vector<ScriptedFix> track) {
  Stop();
  track_ = std::move(track);
  next_index_ = 0;
}

void ScriptedSource::SetClock(MonotonicClock clock) {
  const double elapsed = running_ ? (clock_ ? clock_() : SteadyClockSeconds()) - origin_ : 0.0;
  clock_ = std::move(clock);
  if (running_) {
    // Re-base, so swapping clocks mid-run does not jump the script.
    origin_ = (clock_ ? clock_() : SteadyClockSeconds()) - elapsed;
  }
}

void ScriptedSource::SetTimeScale(double scale) {
  if (scale > 0.0) time_scale_ = scale;
}

void ScriptedSource::SetLooping(bool looping) { looping_ = looping; }

double ScriptedSource::duration_s() const {
  if (track_.size() < 2) return 0.0;
  return track_.back().t_s - track_.front().t_s;
}

Status ScriptedSource::Start() {
  if (track_.empty()) {
    return Status::Error(kInvalidArg, "ScriptedSource: no track to play");
  }
  if (running_) return Status::Ok();
  origin_ = clock_ ? clock_() : SteadyClockSeconds();
  next_index_ = 0;
  running_ = true;
  Poll();  // everything due at t = 0, before Start returns
  return Status::Ok();
}

void ScriptedSource::Stop() {
  running_ = false;
  next_index_ = 0;
}

double ScriptedSource::script_time_s() const {
  if (!running_) return 0.0;
  const double now = clock_ ? clock_() : SteadyClockSeconds();
  return (now - origin_) * time_scale_;
}

std::size_t ScriptedSource::Poll() {
  if (!running_ || track_.empty()) return 0;

  const double first_t = track_.front().t_s;
  const double lap = duration_s();
  std::size_t emitted = 0;

  for (;;) {
    if (next_index_ >= track_.size()) {
      if (!looping_ || lap <= 0.0) break;
      // A new lap: push the origin forward by one script duration rather than
      // resetting it to now, so a lap boundary drifts by nothing.
      origin_ += lap / time_scale_;
      next_index_ = 0;
      continue;
    }
    const double due = track_[next_index_].t_s - first_t;
    if (script_time_s() < due) break;
    const ScriptedFix& entry = track_[next_index_];
    ++next_index_;
    ++emitted;
    Emit(entry.fix);
    if (!running_) break;  // a listener stopped us
  }
  return emitted;
}

// ---------------------------------------------------------------------------
// BuildScriptedTrack
// ---------------------------------------------------------------------------

namespace {

// Great-circle distance in metres and initial bearing in degrees.
bool RangeAndBearing(const GeoPoint& a, const GeoPoint& b, double* metres, double* bearing) {
  double d = 0.0;
  double brg = 0.0;
  if (GEO_calc_range_and_bearing(a.lat, a.lon, b.lat, b.lon, &d, &brg, TRUE) != SUCCESS) {
    return false;
  }
  if (metres != nullptr) *metres = d;
  if (bearing != nullptr) *bearing = brg;
  return true;
}

PositionFix MakeFix(const GeoPoint& p, double t_s, double speed_mps, double bearing_deg,
                    const ScriptedTrackOptions& opt) {
  PositionFix fix;
  fix.SetPosition(p.lat, p.lon);
  if (opt.set_speed) {
    fix.speed_mps = speed_mps;
    fix.has_speed = true;
  }
  if (opt.set_true_heading) {
    fix.true_heading_deg = NormalizeHeadingDeg(bearing_deg);
    fix.has_true_heading = true;
  }
  if (opt.set_altitude) {
    fix.altitude_msl_m = opt.altitude_msl_m;
    fix.has_altitude = true;
  }
  if (opt.set_hdop) {
    fix.hdop = opt.hdop;
    fix.has_hdop = true;
  }
  if (opt.start_time_s != 0.0) {
    fix.time_s = opt.start_time_s + t_s;
    fix.has_time = true;
  }
  return fix;
}

}  // namespace

std::vector<ScriptedFix> BuildScriptedTrack(const std::vector<GeoPoint>& path,
                                            double speed_mps,
                                            const ScriptedTrackOptions& options) {
  std::vector<ScriptedFix> track;
  if (path.size() < 2 || speed_mps <= 0.0 || options.sample_interval_s <= 0.0) {
    return track;
  }

  // Measure the legs once: a road polyline is thousands of points and the
  // sampler would otherwise re-measure the whole path per sample.
  struct Leg {
    double length_m = 0.0;
    double bearing_deg = 0.0;
  };
  std::vector<Leg> legs;
  legs.reserve(path.size() - 1);
  double total_m = 0.0;
  for (std::size_t i = 0; i + 1 < path.size(); ++i) {
    Leg leg;
    if (!RangeAndBearing(path[i], path[i + 1], &leg.length_m, &leg.bearing_deg)) {
      leg.length_m = 0.0;
      leg.bearing_deg = 0.0;
    }
    total_m += leg.length_m;
    legs.push_back(leg);
  }
  if (total_m <= 0.0) return track;

  const double total_s = total_m / speed_mps;

  std::size_t leg_index = 0;
  double leg_start_m = 0.0;  // distance along the path at the leg's first point
  for (double t = 0.0; t <= total_s; t += options.sample_interval_s) {
    const double along_m = t * speed_mps;
    while (leg_index + 1 < legs.size() && along_m > leg_start_m + legs[leg_index].length_m) {
      leg_start_m += legs[leg_index].length_m;
      ++leg_index;
    }
    const Leg& leg = legs[leg_index];
    const double into_leg_m = along_m - leg_start_m;

    GeoPoint p = path[leg_index];
    if (into_leg_m > 0.0 && leg.length_m > 0.0) {
      double lat = 0.0;
      double lon = 0.0;
      if (GEO_calc_end_point(path[leg_index].lat, path[leg_index].lon, into_leg_m,
                             leg.bearing_deg, &lat, &lon, TRUE) == SUCCESS) {
        p = GeoPoint{lat, lon};
      }
    }

    ScriptedFix entry;
    entry.t_s = t;
    // Heading is filled in below, once the next sample is known.
    entry.fix = MakeFix(p, t, speed_mps, leg.bearing_deg, options);
    track.push_back(entry);
  }

  if (options.include_endpoint && !track.empty()) {
    const double last_t = track.back().t_s;
    // Only when the sampling did not already land on the end.
    if (total_s - last_t > 1e-9) {
      const Leg& leg = legs.back();
      ScriptedFix entry;
      entry.t_s = total_s;
      entry.fix = MakeFix(path.back(), total_s, speed_mps, leg.bearing_deg, options);
      track.push_back(entry);
    }
  }

  // The reported heading is the bearing to the NEXT SAMPLE, not to the far
  // end of the leg the sample sits on. On a long leg the great circle has
  // turned by the time the ship gets here, so the bearing to the leg's end
  // is not the course being steered at this instant — and a track built with
  // headings must agree with the heading DERIVED from its own positions, or
  // MM1's two branches would disagree by the leg's convergence. The last
  // sample inherits the one before it: there is no next point to steer to.
  if (options.set_true_heading) {
    for (std::size_t i = 0; i + 1 < track.size(); ++i) {
      double bearing = 0.0;
      double metres = 0.0;
      if (RangeAndBearing(track[i].fix.position(), track[i + 1].fix.position(), &metres,
                          &bearing) &&
          metres > 0.0) {
        track[i].fix.true_heading_deg = NormalizeHeadingDeg(bearing);
      }
    }
    if (track.size() > 1) {
      track.back().fix.true_heading_deg = track[track.size() - 2].fix.true_heading_deg;
    }
  }

  return track;
}

// ---------------------------------------------------------------------------
// BuildScriptedTrackFromFixes (MM6)
// ---------------------------------------------------------------------------

std::vector<ScriptedFix> BuildScriptedTrackFromFixes(const std::vector<PositionFix>& fixes,
                                                     const FixScriptOptions& options) {
  std::vector<ScriptedFix> track;
  if (fixes.size() < 2) return track;

  // Does this recording have a clock at all? One fix with a timestamp is not
  // a schedule, so the whole track falls back to a constant interval rather
  // than mixing the two.
  std::size_t stamped = 0;
  for (const PositionFix& fix : fixes) {
    if (fix.has_time) ++stamped;
  }
  const bool use_stamps = stamped >= 2;
  const double interval =
      options.fallback_interval_s > 0.0 ? options.fallback_interval_s : 1.0;

  track.reserve(fixes.size());
  double t = 0.0;                 // script seconds allotted so far
  double previous_stamp = 0.0;    // the last fix's own time
  bool have_previous = false;

  for (const PositionFix& fix : fixes) {
    if (!use_stamps || !fix.has_time) {
      // No clock: one interval per fix. An unstamped fix in an otherwise
      // stamped recording takes an interval too, which keeps it in order
      // instead of dropping it — and that interval is CHARGED AGAINST the
      // recording's own clock (`previous_stamp` moves with it), or the next
      // stamped fix would add its full delta on top and the whole rest of the
      // replay would drift one interval later per dropout.
      if (!track.empty()) t += interval;
      if (have_previous) previous_stamp += interval;
      ScriptedFix entry;
      entry.t_s = t;
      entry.fix = fix;
      track.push_back(entry);
      continue;
    }

    if (have_previous) {
      double delta = fix.time_s - previous_stamp;
      if (options.drop_non_monotonic && delta <= 0.0) continue;
      if (delta < 0.0) delta = 0.0;
      if (options.max_gap_s > 0.0 && delta > options.max_gap_s) delta = options.max_gap_s;
      t += delta;
    }
    previous_stamp = fix.time_s;
    have_previous = true;

    ScriptedFix entry;
    entry.t_s = t;
    entry.fix = fix;
    track.push_back(entry);
  }
  return track;
}

}  // namespace fv
