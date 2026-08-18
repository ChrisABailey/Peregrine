// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/nav/camera.h — where the map goes when the ship moves (nav plan MM2).
//
// Ported from `Applications/FalconView/MovingMapOverlay/gps_draw.cpp`:
// `map_update()` (~1703, the recenter trigger),
// `auto_center_bounding_box_calc()` (~1556, the apron),
// `set_new_map()` (~488, the placement) and its two helpers
// `get_delta_xy_discrete()` (~645) / `get_delta_xy_continuous()` (~763).
// The anchor fractions are `gps.cpp:114`.
//
// THE CAMERA NEVER TOUCHES THE ENGINE. `Update` answers WHERE the map should
// be — a centre, a rotation, and whether either changed — and the shell
// applies it. That is what makes every geometry case below a plain unit test
// with no map window, no view and no message pump, and it is why the
// original's four functions are one pure function here even though in
// FalconView they reach for the active view and invalidate it themselves.
//
// WHAT THE ALGORITHM IS, in one paragraph, because the code reads as
// trigonometry and not as intent. An auto-centring moving map does not
// recentre on every fix — that would slide the chart continuously under a
// ship that is barely moving. Instead the ship is allowed to wander inside an
// APRON, a rectangle somewhere near the middle of the window, and the map is
// only moved when the ship leaves it (or leaves the window entirely, or the
// user asked for continuous centring, or something forced it). When the map
// IS moved, the ship is not put back in the middle: the window is cut into a
// 3x3 grid and the ship is placed in whichever perimeter box leaves the most
// map AHEAD of it, so a ship heading north sits low on the screen and sees
// where it is going. Track-up mode replaces both halves — the map is rotated
// so the course points up the screen and the ship is anchored a third of the
// way below centre, which is the same idea expressed once instead of nine
// times.
//
// THE ANGLE EVERYONE FORGETS is `point_angle` = heading + map rotation +
// MERIDIAN CONVERGENCE. Convergence is the angle between grid north and true
// north at the ship, and it is a parameter here rather than something asked
// of `MapProjection` because on the port's only projection it is identically
// zero: `EqualArcProj::get_convergence` (proj/equalarc.cpp:299) returns 0.0,
// as does the Mercator branch of the Lambert one. Only a genuine conic
// (`LambertProj::get_convergence`, lambert.cpp:685) has a number to give, and
// the port has no conic. So the seam is a `convergence_deg` argument that
// defaults to 0 — adding an accessor to `MapProjection` that could only ever
// return zero would state the opposite of the truth, which is that this term
// is real and this projection simply does not have one.
//
// TWO QUIRKS ARE PRESERVED, each documented at its site below: the
// once-only 360 wrap on `point_angle` (which a heading and a rotation alone
// can never defeat — only a wild convergence can), and the tautological
// `!= 90 || != 270` test
// whose else-branch is unreachable. None of them is observable in the default
// configuration, which is exactly why they have survived; see the ledger.
// ONE thing is deliberately NOT reproduced, because there is nothing there to
// reproduce: the 3x3 box index is clamped to the range the original ASSERTs,
// since out of domain — where the once-only wrap can put it — the original's
// cast to int is undefined behaviour. In domain the clamp is the identity.
// See `BoxIndex` in camera.cpp.

#ifndef FVKIT_NAV_CAMERA_H_
#define FVKIT_NAV_CAMERA_H_

#include "fvkit/geo.h"
#include "fvkit/proj.h"

namespace fv {

// The three toggles, with FalconView's own names in brackets. They are
// independent: `auto_rotate` and `continuous` both change what auto-centring
// DOES, and neither does anything while `auto_center` is off.
struct CameraModes {
  // 'ACEN' — the master switch. Off and the camera never moves the map.
  bool auto_center = true;
  // 'AROT' — track-up: the map is rotated so the course points up the screen.
  bool auto_rotate = false;
  // 'CCEN' — recentre on EVERY fix rather than only on leaving the apron.
  bool continuous = false;
};

// The apron, in surface pixels. Right and bottom are EXCLUSIVE, which is
// `CRect`'s convention and the reason the original writes `ul + size + 1`.
struct ApronRect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  bool empty() const { return right <= left || bottom <= top; }
  // CRect::PtInRect — left/top inclusive, right/bottom exclusive. An empty
  // rect contains nothing, which is what makes continuous mode (whose apron
  // IS empty) recentre on every fix.
  bool Contains(int x, int y) const {
    return x >= left && x < right && y >= top && y < bottom;
  }
};

// CRect::operator&= — the intersection, or an all-zero rect when they miss.
ApronRect IntersectApron(const ApronRect& a, const ApronRect& b);

// What the camera decided. `changed` false means the ship is still inside the
// apron and in view: nothing to apply, and every other field is untouched.
struct CameraTarget {
  bool changed = false;
  // The map centre to move to. Valid only when `changed`.
  GeoPoint center;
  // The map rotation to set, degrees clockwise, [0, 360). Equal to the
  // rotation passed in unless `auto_rotate` produced a new one.
  double rotation_deg = 0.0;
  bool rotation_changed = false;
  // The surface offset from the ship to the new centre, before rounding —
  // the placement's own answer, kept because it is what the tests pin and
  // what a shell would draw when debugging the apron.
  double delta_x = 0.0;
  double delta_y = 0.0;
  // True when the world-scale escape fired: at 1:80M and wider the original
  // skips the apron and the 3x3 grid entirely and simply centres on the ship
  // (`set_new_map`'s first branch). `delta_x`/`delta_y` are 0 and the
  // rotation is unchanged.
  bool world_escape = false;
};

// --- The pure pieces, exposed because they are what the tests pin ---------

// `auto_center_bounding_box_calc`. The ship position is where the ship was
// drawn LAST time, not where it has just moved to — see MovingMapCamera
// below for why that distinction is the original's and is kept.
//
// Track-up: a fixed W/5 x 2H/5 box at (2*(W/5), H/2). North-up: the outer box
// (W/10..9W/10 x H/10..9H/10) intersected with an inner box whose size and
// place come from which third of the window the ship is in — a ship in the
// middle third gets a narrow box (it may drift little before the map moves),
// one out at the edge gets a wide one (it is already going somewhere).
// Returns an empty rect when auto-centring is off or continuous is on.
ApronRect ComputeApron(const CameraModes& modes, int window_width,
                       int window_height, int ship_x, int ship_y);

// `get_delta_xy_discrete` — the 3x3 placement. `point_angle` is degrees
// clockwise; the answer is the offset from the ship to the new map centre,
// in surface pixels. The ship lands at the centre of one of the nine boxes,
// so each delta is one of {-W/3, 0, +W/3} x {-H/3, 0, +H/3}.
void DeltaXyDiscrete(int window_width, int window_height, double point_angle,
                     double* delta_x, double* delta_y);

// `get_delta_xy_continuous` — the same derivation with the box index left as
// a real number instead of rounded to a box, so the ship slides smoothly
// round the perimeter as it turns instead of jumping between nine positions.
// NOTE the two are not the same function with a rounding switch: the
// original's `+ 0.5` (the rounding term in the discrete one) is left in the
// continuous formula, so a continuous placement sits a third of a box away
// from the discrete one it corresponds to. Preserved.
void DeltaXyContinuous(int window_width, int window_height, double point_angle,
                       double* delta_x, double* delta_y);

// The track-up offset (`set_new_map`, the `is_autorotating` branch). The ship
// is anchored `frac_x`/`frac_y` of the window from the centre along the
// COURSE, so it stays put on the screen while the chart turns under it.
void DeltaXyTrackUp(int window_width, int window_height, double point_angle,
                    double anchor_frac_x, double anchor_frac_y,
                    double* delta_x, double* delta_y);

// --- The camera ----------------------------------------------------------

class MovingMapCamera {
 public:
  void SetModes(const CameraModes& modes) { modes_ = modes; }
  const CameraModes& modes() const { return modes_; }

  // The track-up anchor as a fraction of the window from its centre, along
  // the course. FalconView's own numbers (gps.cpp:114) are (0, 1/3), i.e.
  // the ship a third of the window BEHIND the centre — about 83% down a
  // north-up screen. Only consulted in continuous track-up; discrete
  // track-up uses the original's hard-coded (0, 0.333333), which is the same
  // place to six figures and is deliberately not this setting — see the
  // comment in camera.cpp.
  void SetTrackUpAnchor(double frac_x, double frac_y) {
    anchor_frac_x_ = frac_x;
    anchor_frac_y_ = frac_y;
  }
  double anchor_frac_x() const { return anchor_frac_x_; }
  double anchor_frac_y() const { return anchor_frac_y_; }

  // Scales at or wider than this take the world-scale escape: centre on the
  // ship, no apron, no grid. FalconView tests `scale() == WORLD ||
  // scale() == ONE_TO_80M`; the port has a scale denominator instead of a
  // scale enum, so the test is a threshold. <= 0 disables the escape.
  void SetWorldEscapeScale(double scale_denominator) {
    world_escape_scale_ = scale_denominator;
  }
  double world_escape_scale() const { return world_escape_scale_; }

  // THE APRON IS RECOMPUTED WHEN THE MAP IS DRAWN, NOT WHEN A FIX ARRIVES,
  // and that ordering is the original's: `auto_center_bounding_box_calc` runs
  // inside `C_gps_trail::draw` off the ship's DRAWN rectangle, while
  // `map_update` tests the newly arrived position against the apron left over
  // from that draw. It matters because the north-up apron depends on where
  // the ship is: computing it from the new position would ask "may the ship
  // be here?" of a box built around the ship being here, which is always yes.
  // So a shell calls this once per frame after drawing, and `Update` per fix.
  // A camera that is never drawn has an empty apron and therefore recentres
  // on every fix, which is the safe direction.
  void RecomputeApron(int window_width, int window_height, int ship_x,
                      int ship_y);

  // "There is no drawn ship", which is NOT the same call as recomputing over a
  // zero-sized window: `ComputeApron`'s boxes carry the original's `+ 1`
  // exclusive edge, so a 0x0 window yields a 1x1 apron containing the origin
  // rather than an empty one. An overlay whose feed has not delivered yet says
  // so with this, and an empty apron is what makes the first fix recentre.
  void ClearApron() { apron_ = ApronRect{}; }

  const ApronRect& apron() const { return apron_; }

  // The per-fix decision. `proj` supplies the surface size, the scale and the
  // geo<->surface transform; `ship` is where the ship now is; `heading_deg`
  // is what MM1's HeadingResolver resolved; `map_rotation_deg` is the map's
  // current clockwise rotation (the port's MapProjection carries none, so the
  // shell owns it); `convergence_deg` is the header's forgotten term, zero on
  // every projection the port has. `force` is FalconView's `force_update` —
  // the toggles and the tracking-centre keys use it to recentre now.
  CameraTarget Update(const MapProjection& proj, const GeoPoint& ship,
                      double heading_deg, double map_rotation_deg,
                      double convergence_deg = 0.0, bool force = false);

 private:
  CameraModes modes_;
  ApronRect apron_;
  double anchor_frac_x_ = 0.0;
  double anchor_frac_y_ = 0.33333333;
  double world_escape_scale_ = 80000000.0;
};

}  // namespace fv

#endif  // FVKIT_NAV_CAMERA_H_
