// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/overlay/moving_map_overlay.h"

#include <cmath>
#include <utility>
#include <vector>

#include "fvkit/canvas/geo_draw.h"

namespace fv {

const char MovingMapOverlay::kTypeId[] = "fv.movingmap";

MovingMapOverlay::MovingMapOverlay(std::string name)
    : Overlay(std::move(name)) {
  body_.SetColor(color_);
  edge_.SetColor(FvColor{0, 0, 0, 255});
}

MovingMapOverlay::~MovingMapOverlay() {
  // The queue's listener captures `this`. A source that outlives the overlay
  // would deliver into freed memory, so detach on the way out — the source is
  // shared and may well be held by the shell as well.
  if (source_) source_->SetListener(nullptr);
}

void MovingMapOverlay::SetSource(std::shared_ptr<IPositionSource> source) {
  if (source_) source_->SetListener(nullptr);
  source_ = std::move(source);
  queue_.Clear();
  heading_.Reset();
  // MM5: and the road, for the same reason the heading history goes. The new
  // feed's first fix must not inherit "the road I am on" from a ship that may
  // be on the other side of the world.
  snapper_.Reset();
  has_fix_ = false;
  if (source_) source_->SetListener(queue_.Listener());
}

void MovingMapOverlay::SetRoadNetwork(std::shared_ptr<const IRoadNetwork> network) {
  snapper_.SetNetwork(std::move(network));
}

void MovingMapOverlay::SetSnapMinConfidence(double c) {
  snap_min_confidence_ = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);
}

Status MovingMapOverlay::Start() {
  if (!source_) return Status::Error(kNotFound, "no position source");
  // heading.h: a restarted source must not derive its first heading from where
  // the last run ended.
  heading_.Reset();
  snapper_.Reset();
  queue_.Clear();
  return source_->Start();
}

void MovingMapOverlay::Stop() {
  if (source_) source_->Stop();
}

void MovingMapOverlay::SetModes(const CameraModes& modes) {
  camera_.SetModes(modes);
  force_ = true;
}

void MovingMapOverlay::SetAutoCenter(bool on) {
  CameraModes m = camera_.modes();
  m.auto_center = on;
  SetModes(m);
}

void MovingMapOverlay::SetAutoRotate(bool on) {
  CameraModes m = camera_.modes();
  m.auto_rotate = on;
  SetModes(m);
}

void MovingMapOverlay::SetContinuous(bool on) {
  CameraModes m = camera_.modes();
  m.continuous = on;
  SetModes(m);
}

void MovingMapOverlay::SetRotationSupported(bool on) {
  rotation_supported_ = on;
  if (!on) {
    // The rotation the shell was never going to apply must not be left behind
    // on the overlay: it is the term the ownship is drawn through.
    map_rotation_deg_ = 0.0;
    slew_.Reset(slew_.center(), 0.0);
  }
  force_ = true;
}

void MovingMapOverlay::ResetMap(const GeoPoint& center, double rotation_deg) {
  slew_.Reset(center, rotation_deg);
  map_rotation_deg_ = rotation_deg;
}

void MovingMapOverlay::SetColor(FvColor c) {
  color_ = c;
  body_.SetColor(c);
}

double MovingMapOverlay::screen_angle_deg() const {
  // THIS IS `point_angle`, AND THERE IS ONLY ONE DEFINITION OF IT. MM2 ported
  // `heading + map_rotation + convergence` verbatim out of `set_new_map` and
  // uses it to choose which of nine boxes the ship is placed in — a
  // SCREEN-space derivation — so the same sum is the angle the ship is drawn
  // at, and the two cannot be allowed to disagree about the sense of the
  // rotation.
  //
  // It ADDS. The first version of this subtracted, reasoning that a chart
  // turned clockwise ought to take the rotation back out of a bearing; the
  // original says otherwise, and the case that settles it is track-up itself.
  // With the course east (090) the camera answers rotation 270, because
  // `rotation -= point_angle` is exactly "turn the chart until this sum comes
  // to zero" — and zero is up. Subtracting gave 090 - 270 = 180 and drew a
  // ship steaming east as pointing south.
  return NormalizeHeadingDeg(heading_.current().degrees + convergence_deg_ +
                             map_rotation_deg_);
}

MovingMapTick MovingMapOverlay::Tick(const MapProjection& proj, double dt_s) {
  MovingMapTick out;

  // WHERE THE MAP IS TURNED TO IS A QUESTION WITH AN ANSWER (PR3), so ask it
  // rather than remembering what we asked for. Until the projection could
  // rotate, `map_rotation_deg_` was a belief about the shell — the ship points
  // correctly only while the two agree, and nothing said so out loud. A shell
  // that applies every tick faithfully finds this to be the identity; one that
  // lags a frame, or turns the map by some route of its own, is corrected here
  // instead of drawing a ship at an angle nobody can account for.
  //
  // It also brings the term into [0, 360): `SetRotation` wraps and the slew
  // does not, and MM2's `point_angle` takes ONE 360 subtraction on the sum of
  // a heading and a rotation, which is enough only while both are in range.
  if (proj.Ready() && rotation_supported_) map_rotation_deg_ = proj.Rotation();

  // The resolver derives its heading in SCREEN space, so it needs the scale
  // the map is currently at — FalconView does exactly this from
  // `handle_mapscale_changes`, and doing it per tick means a zoom between two
  // fixes cannot leave a stale ratio behind.
  if (proj.Ready())
    heading_.SetDegPerPixel(proj.DegPerPixelLat(), proj.DegPerPixelLon());

  // Every fix reaches the resolver; only the last one reaches the camera.
  std::vector<PositionFix> fixes;
  queue_.Drain(&fixes);
  for (const PositionFix& raw : fixes) {
    // MM5 SITS HERE, BEFORE THE RESOLVER, AND THE ORDER IS THE POINT. What the
    // resolver keeps is a history of positions; feeding it the raw ones and
    // snapping afterwards would derive every heading from the scatter the snap
    // exists to remove. Snapping first also means a snapped fix's road bearing
    // arrives as a REPORTED heading, which the resolver already prefers over
    // anything it can derive — no new rule, just the existing one applying.
    //
    // The heading handed to the snapper is the PREVIOUS fix's, because that is
    // the one that exists: the resolver has not seen this fix yet. road_snap.h
    // says why being one fix stale is right rather than merely tolerable.
    const ResolvedHeading prior = heading_.current();
    const SnappedFix snap = snapper_.Snap(raw, prior.degrees, prior.known);
    const bool apply = snap.snapped && snap.confidence >= snap_min_confidence_;
    const PositionFix f = apply ? snap.Applied() : raw;

    out.heading = heading_.Update(f);
    out.snap = snap;
    out.snap_applied = apply;
    if (f.has_position) {
      last_fix_ = f;
      has_fix_ = true;
      out.new_fix = true;
      out.fix = f;
    }
  }
  if (!out.new_fix) out.heading = heading_.current();

  // The slew has to know where the map is before it can animate away from it.
  // A shell is not obliged to say so (`ResetMap`), so the first tick with a
  // ready projection adopts the map's own centre — which is also what makes an
  // overlay dropped into a running application behave.
  if (proj.Ready() && !slew_.started())
    slew_.Reset(proj.Center(), map_rotation_deg_);

  if (proj.Ready() && out.new_fix) {
    out.target = camera_.Update(proj, last_fix_.position(), out.heading.degrees,
                                map_rotation_deg_, convergence_deg_, force_);
    force_ = false;
    slew_.Retarget(proj, out.target);
  }

  out.slew = slew_.Advance(dt_s);
  // The overlay asked for it and the shell applies it, so the next camera
  // update must be told the same number or it would compute its placement
  // against a rotation that has already been left behind. A shell that moved
  // the map behind our back says so with ResetMap.
  // The rotation the slew reports IS the map's rotation from now on -- but
  // only where the shell can actually apply it (see SetRotationSupported).
  if (slew_.started() && rotation_supported_)
    map_rotation_deg_ = out.slew.rotation_deg;
  return out;
}

Status MovingMapOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");
  if (!has_fix_) {
    // No ship yet: no apron either, and an EMPTY apron is what makes the first
    // fix recentre (MM2). Clearing it here rather than leaving the last one is
    // the difference between "the feed restarted and the map followed" and
    // "the map sat still because the ship was inside a box drawn around where
    // it used to be".
    camera_.ClearApron();
    has_drawn_ = false;
    return Status::Ok();
  }

  const PixelSize surf = proj.SurfaceSize();
  double sx = 0.0, sy = 0.0;
  Status s = proj.GeoToSurface(last_fix_.position(), &sx, &sy);
  if (!s.ok()) return s;

  // THE APRON IS RECOMPUTED HERE, from where the ship is DRAWN, and this is
  // the ordering MM2's header spends a paragraph on: `Update` then tests the
  // NEXT fix against this box. Recomputing it inside `Update` would ask "may
  // the ship be here?" of a box built around the ship being here.
  //
  // It is recomputed even when the ship is off the canvas: the surface
  // coordinate is defined everywhere, an apron built round an off-screen ship
  // contains nothing the ship can be inside, and the recentre that follows is
  // the wanted behaviour.
  drawn_x_ = sx;
  drawn_y_ = sy;
  has_drawn_ = true;
  camera_.RecomputeApron(surf.width, surf.height, static_cast<int>(std::lround(sx)),
                         static_cast<int>(std::lround(sy)));

  if (show_apron_) {
    const ApronRect& a = camera_.apron();
    if (!a.empty()) {
      Pen pen;
      pen.color = FvColor{120, 120, 120, 255};
      pen.width = 1;
      pen.dash = {4, 4};
      // right/bottom are EXCLUSIVE (CRect), so the drawn edge is one pixel in.
      const std::vector<PixelPoint> box = {
          {a.left, a.top},        {a.right - 1, a.top},
          {a.right - 1, a.bottom - 1}, {a.left, a.bottom - 1},
          {a.left, a.top}};
      canvas.DrawLines(box, pen);
    }
  }

  GeoDraw draw(proj, &canvas);
  draw.SetSymbolDpiScale(dpi_scale_);

  // NEGATED, and it is a unit conversion rather than a fix — the same one
  // `S52StyleEngine` makes at its own seam and for the same reason. A heading
  // is a COMPASS BEARING (clockwise from north); `PointSymbolStyle::
  // rotation_deg` is `CCGMSymbol::DrawSymbol`'s angle, `x' = x cos r -
  // y sin r` over a y-up symbol space drawn into a y-down device, which turns
  // a symbol COUNTER-clockwise on screen. The shared renderer keeps
  // FalconView's sense under the bit-faithful rule, so the product that
  // authors in bearings converts. Without this the ship flies EAST while
  // pointing WEST — which a golden hash cannot see, and a look at the render
  // can.
  const double angle = -screen_angle_deg();
  const double scale = size_px_ / kBuiltinSymbolNominalPx;

  // G4: the highlight goes on the OUTERMOST stamp and on that one only (the
  // rule PointOverlay's marker established). The outermost stamp is the edge
  // when there is one and the body when there is not.
  bool highlight_pending = highlighted_;
  auto next_state = [&]() {
    const RenderState st =
        highlight_pending ? RenderState::kHighlighted : RenderState::kNormal;
    highlight_pending = false;
    return st;
  };

  if (show_edge_) {
    const double edge_px = size_px_ + 2.0;
    draw.SetState(next_state());
    draw.SetSymbols(&edge_);
    s = draw.DrawSymbolAtPixel(
        sx, sy, symbol_id_,
        PointSymbolStyle{true, symbol_id_, angle,
                         edge_px / kBuiltinSymbolNominalPx});
    if (!s.ok()) return s;
  }

  draw.SetState(next_state());
  draw.SetSymbols(&body_);
  return draw.DrawSymbolAtPixel(
      sx, sy, symbol_id_, PointSymbolStyle{true, symbol_id_, angle, scale});
}

}  // namespace fv
