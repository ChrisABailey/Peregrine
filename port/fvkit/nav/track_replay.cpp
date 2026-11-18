// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/track_replay.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "fvkit/nav/gpx.h"
#include "fvkit/nav/nmea.h"

namespace fv {

Status ReadTrackFixes(const std::string& path, std::vector<PositionFix>* out) {
  out->clear();
  std::string ext = std::filesystem::path(path).extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (ext != ".gpx") return ReadNmeaLog(path, out);
  GpxDocument doc;
  const Status s = ReadGpxFile(path, &doc);
  if (!s.ok()) return s;
  *out = FlattenGpxFixes(doc);
  return Status::Ok();
}

std::vector<PositionFix> ReplayableFixes(std::vector<PositionFix> fixes) {
  fixes.erase(std::remove_if(fixes.begin(), fixes.end(),
                             [](const PositionFix& f) { return !f.has_position; }),
              fixes.end());
  const bool any_time = std::any_of(fixes.begin(), fixes.end(),
                                    [](const PositionFix& f) { return f.has_time; });
  if (!any_time) {
    for (size_t i = 0; i < fixes.size(); ++i) {
      fixes[i].time_s = static_cast<double>(i);
      fixes[i].has_time = true;
    }
    return fixes;
  }
  std::vector<PositionFix> out;
  out.reserve(fixes.size());
  for (const PositionFix& f : fixes)
    if (f.has_time && (out.empty() || f.time_s > out.back().time_s)) out.push_back(f);
  return out;
}

TrackState TrackStateAt(const std::vector<PositionFix>& track, double t_s) {
  TrackState state;
  if (track.empty()) return state;
  // `seg` is the later fix of the segment the heading comes from: the
  // bracketing one inside the track, the first or last one outside it.
  size_t seg = 1;
  if (t_s < track.front().time_s) {
    state.fix = track.front();
    state.phase = ReplayPhase::kBefore;
  } else if (t_s >= track.back().time_s) {
    state.fix = track.back();
    state.phase = t_s > track.back().time_s ? ReplayPhase::kAfter : ReplayPhase::kDuring;
    state.past = track.size();
    seg = track.size() - 1;
  } else {
    const auto later = std::upper_bound(
        track.begin(), track.end(), t_s,
        [](double t, const PositionFix& f) { return t < f.time_s; });
    seg = static_cast<size_t>(later - track.begin());
    const PositionFix& a = track[seg - 1];
    const PositionFix& b = track[seg];
    const double u = (t_s - a.time_s) / (b.time_s - a.time_s);
    PositionFix f = a;
    f.SetPosition(a.lat + u * (b.lat - a.lat), a.lon + u * (b.lon - a.lon));
    f.time_s = t_s;
    if (a.has_altitude && b.has_altitude)
      f.altitude_msl_m = a.altitude_msl_m + u * (b.altitude_msl_m - a.altitude_msl_m);
    if (a.has_speed && b.has_speed) f.speed_mps = a.speed_mps + u * (b.speed_mps - a.speed_mps);
    state.fix = f;
    state.past = seg;
  }

  if (track.size() < 2) {
    state.segment_from = state.segment_to = track.front().position();
  } else {
    state.segment_from = track[seg - 1].position();
    state.segment_to = track[seg].position();
  }
  if (state.fix.has_true_heading) {
    state.heading.degrees = state.fix.true_heading_deg;
    state.heading.known = true;
    state.heading.reported = true;
  } else {
    state.heading.known =
        ScreenBearingDeg(state.segment_from, state.segment_to, 0.0, 0.0, &state.heading.degrees);
  }
  return state;
}

}  // namespace fv
