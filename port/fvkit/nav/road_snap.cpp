// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/road_snap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fv {
namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// The absolute angle between two bearings, in [0, 180].
double AngleBetweenDeg(double a, double b) {
  double d = std::fabs(NormalizeHeadingDeg(a) - NormalizeHeadingDeg(b));
  if (d > 180.0) d = 360.0 - d;
  return d;
}

}  // namespace

// ---------------------------------------------------------------------------
// SnappedFix
// ---------------------------------------------------------------------------

PositionFix SnappedFix::Applied() const {
  PositionFix out = raw;
  if (!snapped) return out;
  out.SetPosition(position.lat, position.lon);
  if (has_bearing) {
    out.true_heading_deg = bearing_deg;
    out.has_true_heading = true;
  }
  return out;
}

// ---------------------------------------------------------------------------
// RoadSnapper
// ---------------------------------------------------------------------------

void RoadSnapper::SetNetwork(std::shared_ptr<const IRoadNetwork> network) {
  network_ = std::move(network);
  Reset();
}

void RoadSnapper::Reset() {
  have_prev_ = false;
  prev_arc_ = kNoRoadArc;
  prev_from_ = 0;
  prev_to_ = 0;
  last_ = SnappedFix{};
  scratch_.clear();
}

SnappedFix RoadSnapper::Snap(const PositionFix& fix, double prior_heading_deg,
                             bool has_prior_heading) {
  SnappedFix out;
  out.raw = fix;
  out.position = fix.position();
  scratch_.clear();

  if (network_ == nullptr || !fix.has_position) {
    last_ = out;
    return out;
  }

  // Radius from the fix's own quality (see RoadSnapSettings).
  double radius = settings_.default_radius_m;
  if (fix.has_hdop) {
    radius = fix.hdop * settings_.hdop_scale;
    radius = std::max(settings_.min_radius_m,
                      std::min(settings_.max_radius_m, radius));
  }
  if (radius <= 0.0) radius = 1.0;

  network_->QueryNear(fix.position(), radius, &scratch_);
  out.candidates = static_cast<int>(scratch_.size());
  if (scratch_.empty()) {
    // Off the network entirely — a boat, a car park, the edge of the extract.
    // The previous road is forgotten rather than kept: coming back on later,
    // somewhere else, must not inherit a hysteresis bonus from a road that may
    // now be miles away.
    have_prev_ = false;
    last_ = out;
    return out;
  }

  // Whose heading is it? The fix's own when it reports one — a receiver's
  // course over ground beats anything derived — and the caller's otherwise.
  double heading = 0.0;
  bool have_heading = false;
  if (fix.has_true_heading) {
    heading = fix.true_heading_deg;
    have_heading = true;
  } else if (has_prior_heading) {
    heading = prior_heading_deg;
    have_heading = true;
  }

  // A fix with no speed field at all is treated as MOVING: a source that does
  // not report speed (an NMEA GLL, a hand-built fix) must not be permanently
  // frozen onto its first road.
  const bool moving =
      !fix.has_speed || fix.speed_mps >= settings_.hold_speed_mps;
  const bool hold = !moving && have_prev_;

  // Score every candidate. Lower is better and every term is METRES.
  std::vector<std::pair<double, std::size_t>> scored;
  scored.reserve(scratch_.size());
  for (std::size_t i = 0; i < scratch_.size(); ++i) {
    RoadCandidate& c = scratch_[i];

    // Which way along this road is the ship going? A one-way road answers for
    // itself; a two-way one is aligned on its axis, so the direction of travel
    // is whichever end of that axis the heading is nearer — and the candidate
    // is rewritten with it, so a caller reading last_candidates() sees the
    // direction that was assumed rather than the arbitrary one the network
    // stored.
    if (!c.one_way && have_heading && AngleBetweenDeg(heading, c.bearing_deg) > 90.0)
      c.bearing_deg = NormalizeHeadingDeg(c.bearing_deg + 180.0);

    double score = c.distance_m;

    // The alignment term, skipped at a standstill where the heading is noise.
    if (have_heading && moving) {
      const double delta = AngleBetweenDeg(heading, c.bearing_deg) * kDegToRad;
      score += settings_.heading_penalty_m * (1.0 - std::cos(delta)) * 0.5;
    }

    // Hysteresis.
    if (have_prev_) {
      if (c.arc == prev_arc_) {
        score -= settings_.stay_bonus_m;
      } else if (c.from_node == prev_from_ || c.from_node == prev_to_ ||
                 c.to_node == prev_from_ || c.to_node == prev_to_) {
        score -= settings_.connected_bonus_m;
      }
    }

    // THE HOLD IS AN INFINITE STAY BONUS, which is what makes it a special
    // case of hysteresis rather than a branch of its own: the arc still has to
    // be IN RANGE to be held (it is only here at all because QueryNear
    // returned it), and if the ship has drifted off it entirely the ordinary
    // scoring decides.
    if (hold && c.arc == prev_arc_) score = -std::numeric_limits<double>::infinity();

    scored.emplace_back(score, i);
  }

  std::stable_sort(scored.begin(), scored.end(),
                   [](const std::pair<double, std::size_t>& a,
                      const std::pair<double, std::size_t>& b) {
                     return a.first < b.first;
                   });

  // Reorder the candidate list best-first — the sort order IS the answer, so
  // last_candidates()[0] is always the road that was chosen.
  std::vector<RoadCandidate> ordered;
  ordered.reserve(scratch_.size());
  for (const std::pair<double, std::size_t>& s : scored)
    ordered.push_back(scratch_[s.second]);
  scratch_.swap(ordered);

  const RoadCandidate& best = scratch_.front();
  out.snapped = true;
  out.position = best.point;
  out.arc = best.arc;
  out.road_name = best.name;
  out.offset_m = best.distance_m;
  out.held = hold && best.arc == prev_arc_;

  // A bearing is only reported when it means something: the ship must be
  // moving (a stationary ship's heading is not the road's), and the direction
  // along the road must have been resolvable.
  out.has_bearing = moving && (best.one_way || have_heading);
  out.bearing_deg = best.bearing_deg;

  // Confidence. `nearness` is 1 on the road and 0 at the rim of the search
  // radius; `sep` is how clearly the winner won. A HELD snap takes `nearness`
  // alone —
  // holding makes no claim about which road is best, that is what it means.
  const double nearness = Clamp01(1.0 - best.distance_m / radius);
  if (out.held) {
    out.confidence = nearness;
  } else {
    double sep = 1.0;
    if (scored.size() > 1 && settings_.ambiguity_m > 0.0)
      sep = Clamp01((scored[1].first - scored[0].first) / settings_.ambiguity_m);
    out.confidence = Clamp01(nearness * (0.5 + 0.5 * sep));
  }

  have_prev_ = true;
  prev_arc_ = best.arc;
  prev_from_ = best.from_node;
  prev_to_ = best.to_node;

  last_ = out;
  return out;
}

}  // namespace fv
