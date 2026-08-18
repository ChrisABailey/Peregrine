// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/nav/camera_slew.h — the map ARRIVES rather than teleports (nav plan
// MM3).
//
// This one has NO Windows original. FalconView jumps: `map_update` computes a
// new centre and calls `change_map_type` with it, so a discrete recentre moves
// the chart a third of a window between one frame and the next, and a track-up
// turn re-renders at the new rotation immediately. MM2 ported that faithfully.
// MM3 is the layer between MM2's answer and the engine, and its whole job is
// to spend a few tenths of a second getting there.
//
// THE SEAM IS THE SAME SHAPE AS THE CAMERA'S AND FOR THE SAME REASON: the
// slew never touches `MapEngine` either. `Retarget` takes MM2's
// `CameraTarget`, `Advance(dt)` is called once per frame, and what comes back
// is a `SlewState` the shell applies exactly as it applied the target before.
// So every easing case below is a plain unit test with no window, no clock and
// no pump — `dt` is an argument, which is the same trick `ScriptedSource`
// plays with its injectable clock (MM1).
//
// DURATION 0 IS THE FALCONVIEW JUMP, and it is not a special case bolted on:
// with a zero duration `Retarget` simply lands on the target and the next
// `Advance` reports it, so MM2's behaviour stays reachable, stays the thing a
// shell gets by asking for it, and stays testable through this class rather
// than around it.
//
// FOUR DECISIONS WORTH NOT RE-DERIVING.
//
// 1. THE INTERPOLATION IS IN GEO, and the plan's "surface space for short
//    moves, geo for long ones" is a distinction this projection does not
//    have. Equal-arc surface coordinates are an affine function of lat/lon
//    (`proj/map_projection.cpp`), so a lerp in one frame IS a lerp in the
//    other — the only difference is the dpp drift as the centre being
//    interpolated changes latitude under the animation, which is a fraction
//    of a pixel over a third of a window. Geo wins the tie because it is the
//    frame that SURVIVES: the projection is re-centred by this very
//    animation, so surface coordinates captured at retarget mean something
//    different by the next tick, and a start point would have to be
//    re-derived every frame to stay honest. Two consequences are real and
//    both are wanted: the path is a straight line in lat/lon (a rhumb-ish
//    slide, not a great circle — over a third of a window nobody can tell),
//    and longitude is taken THE SHORT WAY around (`UnwrapLonNear`), so a
//    recentre across the antimeridian slides a few pixels instead of
//    sweeping the whole world backwards.
//
// 2. THE RATE CAPS ARE IN PIXELS PER SECOND AND DEGREES PER SECOND, and they
//    EXTEND the duration rather than clipping the motion. A cap that clipped
//    would leave the map short of where the camera said it should be, which
//    is a lie the next fix would have to correct; extending means the map is
//    always going to the right place and only the arrival is later. Pixels,
//    not metres: what a user perceives as "the chart flew past" is screen
//    speed, and the same ground distance is a jump at harbour scale and
//    nothing at all at 1:80M. This is why `Retarget` takes the projection —
//    dpp is the only thing it wants from it.
//
// 3. THE CENTRE AND THE ROTATION SHARE ONE DURATION, the longer of the two
//    the caps ask for. They are one motion; a track-up turn whose rotation
//    finished before its pan would swing the chart round and then slide it,
//    which reads as two events. Rotation goes the SHORT WAY (`ShortestRotationDelta`),
//    so 350 -> 10 is +20 degrees and not -340.
//
// 4. A NEW TARGET RETARGETS IN FLIGHT AND NEVER QUEUES (the plan's interrupt
//    rule). The animation restarts FROM WHERE THE MAP NOW IS, so nothing
//    jumps, and the stale destination is simply forgotten — a queue would
//    make the map visit places the ship left several fixes ago. The cost is
//    stated rather than hidden: restarting also restarts the EASE, so under
//    continuous centring, where a target arrives every fix, an ease-in-out
//    spends every animation in its slow opening and the map lags behind the
//    ship by about one duration. Continuous mode wants a short duration or
//    `kLinear`; discrete recentres, which are seconds apart, are what
//    ease-in-out is for.

#ifndef FVKIT_NAV_CAMERA_SLEW_H_
#define FVKIT_NAV_CAMERA_SLEW_H_

#include "fvkit/geo.h"
#include "fvkit/nav/camera.h"
#include "fvkit/proj.h"

namespace fv {

// How the fraction of the way there is shaped over time.
enum class SlewEasing {
  // Constant speed. Starts and stops abruptly, which is what a stream of
  // continuous-mode retargets wants (see decision 4).
  kLinear,
  // Smoothstep, 3t^2 - 2t^3: zero speed at both ends. The default, and what
  // a discrete recentre should look like.
  kEaseInOut,
};

struct SlewSettings {
  // Seconds for the whole move. 0 (or less) is the FalconView jump, and the
  // rate caps below do NOT resurrect it: they shape a slew, they never create
  // one, so a shell that asks for MM2's behaviour gets exactly MM2.
  double duration_s = 0.35;
  SlewEasing easing = SlewEasing::kEaseInOut;
  // Screen speed cap for the pan, surface pixels per second; <= 0 disables.
  // 1200 px/s crosses a 600-px window in half a second.
  double max_pan_px_per_s = 1200.0;
  // Rotation speed cap, degrees per second; <= 0 disables. 120 deg/s turns a
  // chart through 180 degrees in a second and a half, which is brisk and not
  // violent — the plan's "a 180 turn doesn't spin the chart".
  double max_rotation_deg_per_s = 120.0;
};

// What to apply this tick. `changed` is the only flag a shell needs: false
// means the map is already where it should be and there is nothing to redraw.
struct SlewState {
  GeoPoint center;
  double rotation_deg = 0.0;
  // The values differ from the ones last reported.
  bool changed = false;
  // The animation is still in flight — another `Advance` will move again.
  // A shell drives its frame clock off this.
  bool active = false;
};

// The signed shortest way from `from` to `to`, in (-180, +180]. Exposed
// because it is what the rotation tests pin, and because a shell drawing a
// compass wants the same answer.
double ShortestRotationDelta(double from_deg, double to_deg);

class CameraSlew {
 public:
  void SetSettings(const SlewSettings& settings) { settings_ = settings; }
  const SlewSettings& settings() const { return settings_; }

  // Where the map is NOW. The slew does not know the engine, so a shell says
  // so once at startup and again after anything that moves the map behind the
  // slew's back (a user pan, a jump to a bookmark). Cancels any animation in
  // flight — that is the point of it.
  void Reset(const GeoPoint& center, double rotation_deg);
  bool started() const { return started_; }

  // Aim at MM2's answer. A target with `changed` false is IGNORED, exactly as
  // a shell would ignore it: the camera is saying the ship is still in its
  // apron, which is not a reason to interrupt an animation that is in flight.
  // `proj` supplies degrees-per-pixel for the rate caps and nothing else.
  void Retarget(const MapProjection& proj, const CameraTarget& target);

  // The same thing without a camera, for a shell that has its own reason to
  // move the map smoothly (a jump-to-position, a rotation key).
  void RetargetTo(const MapProjection& proj, const GeoPoint& center,
                  double rotation_deg);

  // One frame. `dt_s` is the elapsed time; <= 0 (or NaN) advances no time but
  // still reports a change that is pending, so `Advance(0.0)` is a legal way
  // to collect a jump.
  SlewState Advance(double dt_s);

  // Land on the target immediately, keeping it as the destination — the
  // "stop animating, I need the map there now" call (a print, a screenshot).
  void Finish();

  const GeoPoint& center() const { return center_; }
  double rotation_deg() const { return rotation_deg_; }
  bool active() const { return active_; }
  // Seconds this animation will take in total, after the caps were applied.
  double duration_s() const { return duration_s_; }

 private:
  void Begin(double pan_px, double rotation_delta_deg);

  SlewSettings settings_;

  // Where the map is, and what this animation is between.
  GeoPoint center_;
  double rotation_deg_ = 0.0;
  GeoPoint start_center_;
  double start_rotation_deg_ = 0.0;
  // The destination's longitude is kept UNWRAPPED relative to the start, so
  // the lerp goes the short way; `center_` is normalized on the way out.
  double goal_lat_ = 0.0;
  double goal_lon_unwrapped_ = 0.0;
  double goal_rotation_deg_ = 0.0;
  // The signed shortest arc from `start_rotation_deg_` to the goal, resolved
  // once at retarget so a wrap through 360 cannot reappear mid-animation.
  double rotation_delta_deg_ = 0.0;

  double elapsed_s_ = 0.0;
  double duration_s_ = 0.0;
  bool active_ = false;
  bool started_ = false;
  // Set whenever `center_`/`rotation_deg_` move, cleared when reported.
  bool dirty_ = false;
};

}  // namespace fv

#endif  // FVKIT_NAV_CAMERA_SLEW_H_
