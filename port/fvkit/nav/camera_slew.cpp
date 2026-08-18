// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/nav/camera_slew.h"

#include <cmath>

namespace fv {
namespace {

// Rotation is carried in [0, 360) everywhere, which is the range MM2's
// `Update` hands out and the range a shell's map rotation lives in.
double NormalizeDeg360(double deg) {
  if (!std::isfinite(deg)) return 0.0;
  double d = std::fmod(deg, 360.0);
  if (d < 0.0) d += 360.0;
  // fmod of a tiny negative can round to exactly 360 after the add.
  if (d >= 360.0) d = 0.0;
  return d;
}

// Smoothstep. Zero derivative at both ends, which is the whole difference a
// user sees between this and kLinear.
double Ease(SlewEasing easing, double t) {
  if (easing == SlewEasing::kLinear) return t;
  return t * t * (3.0 - 2.0 * t);
}

}  // namespace

double ShortestRotationDelta(double from_deg, double to_deg) {
  double d = std::fmod(to_deg - from_deg, 360.0);
  if (d <= -180.0) d += 360.0;
  if (d > 180.0) d -= 360.0;
  return d;
}

void CameraSlew::Reset(const GeoPoint& center, double rotation_deg) {
  center_ = center;
  center_.Normalize();
  rotation_deg_ = NormalizeDeg360(rotation_deg);
  start_center_ = center_;
  start_rotation_deg_ = rotation_deg_;
  goal_lat_ = center_.lat;
  goal_lon_unwrapped_ = center_.lon;
  goal_rotation_deg_ = rotation_deg_;
  rotation_delta_deg_ = 0.0;
  elapsed_s_ = 0.0;
  duration_s_ = 0.0;
  active_ = false;
  started_ = true;
  // The shell told US where the map is; it does not need telling back.
  dirty_ = false;
}

void CameraSlew::Retarget(const MapProjection& proj,
                          const CameraTarget& target) {
  // A camera that did not move the map is not an interruption. Note this
  // reads `changed` and not `rotation_changed`: MM2 sets the rotation on
  // every answer it gives, and an unchanged one is simply the rotation we
  // are already at.
  if (!target.changed) return;
  RetargetTo(proj, target.center, target.rotation_deg);
}

void CameraSlew::RetargetTo(const MapProjection& proj, const GeoPoint& center,
                            double rotation_deg) {
  GeoPoint goal = center;
  goal.Normalize();
  const double goal_rotation = NormalizeDeg360(rotation_deg);

  // Nobody has said where the map is, so there is nothing to animate FROM.
  // Landing on the target is the only honest answer, and it is also what the
  // first fix of a session should do.
  if (!started_) {
    Reset(goal, goal_rotation);
    dirty_ = true;
    return;
  }

  goal_lat_ = goal.lat;
  // The short way round the world (decision 1 in the header).
  goal_lon_unwrapped_ = UnwrapLonNear(goal.lon, center_.lon);
  goal_rotation_deg_ = goal_rotation;
  rotation_delta_deg_ = ShortestRotationDelta(rotation_deg_, goal_rotation);

  // The pan measured in SCREEN pixels, which is what the cap is about. A
  // projection that is not ready has no dpp to measure with, and an
  // unmeasurable pan is simply uncapped rather than refused — the animation
  // is still perfectly well defined in geo.
  double pan_px = 0.0;
  const double dlat = goal_lat_ - center_.lat;
  const double dlon = goal_lon_unwrapped_ - center_.lon;
  if (proj.Ready() && proj.DegPerPixelLat() > 0.0 &&
      proj.DegPerPixelLon() > 0.0) {
    pan_px = std::hypot(dlon / proj.DegPerPixelLon(),
                        dlat / proj.DegPerPixelLat());
  }

  Begin(pan_px, rotation_delta_deg_);
}

void CameraSlew::Begin(double pan_px, double rotation_delta_deg) {
  const bool moves = pan_px != 0.0 || rotation_delta_deg != 0.0;
  // Retargeting at the place we are already at must not restart the ease —
  // under continuous centring a stationary ship produces exactly this, and an
  // animation between a point and itself would report a change every frame.
  if (!moves) {
    active_ = false;
    return;
  }

  double duration = settings_.duration_s;
  if (!std::isfinite(duration) || duration <= 0.0) {
    // The jump. The caps are deliberately NOT consulted here: a shell that
    // asked for duration 0 asked for MM2, and a cap that turned that back
    // into an animation would take the jump away from the one caller who
    // wants it.
    center_.lat = goal_lat_;
    center_.lon = goal_lon_unwrapped_;
    center_.Normalize();
    rotation_deg_ = NormalizeDeg360(goal_rotation_deg_);
    elapsed_s_ = 0.0;
    duration_s_ = 0.0;
    active_ = false;
    dirty_ = true;
    return;
  }

  // The caps EXTEND, never clip (decision 2), and the centre and the rotation
  // share whichever is longer (decision 3) so the move lands as one thing.
  if (settings_.max_pan_px_per_s > 0.0 && pan_px > 0.0) {
    const double need = pan_px / settings_.max_pan_px_per_s;
    if (need > duration) duration = need;
  }
  if (settings_.max_rotation_deg_per_s > 0.0 && rotation_delta_deg != 0.0) {
    const double need =
        std::fabs(rotation_delta_deg) / settings_.max_rotation_deg_per_s;
    if (need > duration) duration = need;
  }

  // Restart FROM WHERE THE MAP NOW IS (decision 4): the old destination is
  // forgotten whole, and because the start is the current interpolated
  // position rather than the previous animation's start, nothing jumps.
  start_center_ = center_;
  start_rotation_deg_ = rotation_deg_;
  elapsed_s_ = 0.0;
  duration_s_ = duration;
  active_ = true;
}

SlewState CameraSlew::Advance(double dt_s) {
  if (active_ && std::isfinite(dt_s) && dt_s > 0.0) {
    elapsed_s_ += dt_s;
    if (elapsed_s_ >= duration_s_) {
      // Land exactly on the goal rather than on the ease's last step — an
      // arrival short by a rounding is a map that never quite gets there.
      center_.lat = goal_lat_;
      center_.lon = goal_lon_unwrapped_;
      center_.Normalize();
      rotation_deg_ = NormalizeDeg360(goal_rotation_deg_);
      elapsed_s_ = duration_s_;
      active_ = false;
    } else {
      const double e = Ease(settings_.easing, elapsed_s_ / duration_s_);
      center_.lat = start_center_.lat + e * (goal_lat_ - start_center_.lat);
      center_.lon =
          start_center_.lon + e * (goal_lon_unwrapped_ - start_center_.lon);
      center_.Normalize();
      rotation_deg_ =
          NormalizeDeg360(start_rotation_deg_ + e * rotation_delta_deg_);
    }
    dirty_ = true;
  }

  SlewState state;
  state.center = center_;
  state.rotation_deg = rotation_deg_;
  state.changed = dirty_;
  state.active = active_;
  dirty_ = false;
  return state;
}

void CameraSlew::Finish() {
  if (!active_) return;
  center_.lat = goal_lat_;
  center_.lon = goal_lon_unwrapped_;
  center_.Normalize();
  rotation_deg_ = NormalizeDeg360(goal_rotation_deg_);
  elapsed_s_ = duration_s_;
  active_ = false;
  dirty_ = true;
}

}  // namespace fv
