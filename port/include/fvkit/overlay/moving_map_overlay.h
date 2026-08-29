// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/overlay/moving_map_overlay.h — the ship on the chart (nav plan MM4).
//
// MM1 built the feed, MM2 the camera and MM3 the slew, and each of the three
// is a pure piece with no consumer. This is the object that holds them in the
// order they belong in:
//
//   IPositionSource -> FixQueue -> [RoadSnapper] -> HeadingResolver
//                                                -> MovingMapCamera
//                                                -> CameraSlew -> [shell]
//                          |
//                          +-> the ownship symbol, drawn in OnDraw
//
// The snapper (MM5) is optional and off until a shell hands over a road
// network; when it is on it stands FIRST, so everything downstream — the
// derived heading, the apron, the symbol — is computed from the ship's
// position on the road rather than from the receiver's scatter.
//
// It is `fv.movingmap`, a STATIC overlay type (A1): at most one, toggled
// rather than opened, because a moving map has one ship and no document. It is
// the port's answer to `C_gps_trail` minus everything the plan put out of
// scope (no trail, no CDI, no predictive path).
//
// THE OVERLAY DOES NOT TOUCH THE ENGINE, and that is inherited rather than
// re-decided: MM2's camera answers where the map SHOULD be and MM3's slew
// answers where it should be THIS FRAME, and both were built as pure functions
// so that a shell applies them. `Tick` returns exactly that answer. So this
// class is testable with no window and no pump, exactly as the three layers
// under it are, and a shell that wants the ship drawn but the map still simply
// ignores what `Tick` returns.
//
// WHY THE CAMERA LIVES HERE, of all places. `MovingMapCamera` splits into two
// calls for a reason MM2 documents at length: the apron is built from where
// the ship was DRAWN and tested against where it has just MOVED TO, so
// `RecomputeApron` belongs on the draw and `Update` on the fix. An overlay is
// the only object in the port that is both drawn per frame and fed fixes, so
// putting the camera anywhere else would oblige the shell to keep that
// ordering right. Here it is structural: `OnDraw` recomputes, `Tick` updates.
//
// TWO THINGS ABOUT THE DRAWN HEADING ARE WORTH KNOWING BEFORE THEY SURPRISE
// SOMEBODY.
//
// 1. A REPORTED HEADING IS A TRUE BEARING AND A DERIVED ONE IS A SCREEN
//    ANGLE, and the symbol is drawn at whichever the resolver returned. That
//    asymmetry is the ORIGINAL's (`get_current_heading` returns the fix's own
//    course when it has one and an atan2 over deg-per-pixel-scaled deltas when
//    it does not), and it is preserved rather than reconciled: on an equal-arc
//    map the two differ by the aspect of the projection, so a ship reporting
//    045 draws a few degrees off the line of its own track at high latitude.
//    Reconciling it means converting a reported bearing into screen space,
//    which would be a new behaviour and not a port of one. See the ledger.
//
// 2. THE MAP ROTATION IS ADDED, and it is the camera's own `point_angle`.
//    MM2 ported `heading + map_rotation + convergence` verbatim and places the
//    ship on the SCREEN with it, so `screen_angle_deg()` is that same sum and
//    the two cannot disagree about the sense of a rotation. Track-up is the
//    case that fixes the sign: the camera answers rotation 270 for a course of
//    090, because `rotation -= point_angle` means "turn the chart until this
//    sum is zero", and zero is up the screen.
//
//    WHICH RAISES THE QUESTION THE SHELL HAS TO ANSWER. At MM4 nothing in the
//    port could rotate a chart, so `SetRotationSupported(false)` was how a
//    shell said so, and it pins the term at 0 — without it the overlay
//    counter-rotates the ownship to match a rotation that never happened, and
//    track-up draws a ship steaming east as pointing up an unrotated chart.
//    PR1-PR3 made a turning chart real, so the answer for a shell that draws
//    through `MapProjection::SetRotation` is now yes, and PythonView says yes.
//
//    PR3 ALSO TOOK THE DUPLICATE STATE OUT OF THE LOOP. `map_rotation_deg_`
//    was a BELIEF about what the shell had done with the number we handed it,
//    and PR2 flagged the drift: the ship points correctly only while the
//    overlay's copy and the projection's agree, and nothing asserted it. A
//    projection can now be ASKED, so `Tick` adopts `proj.Rotation()` before it
//    does anything with it. For a shell that applies each tick faithfully that
//    is the identity; for one that lags, or pans the map behind our back, it
//    is a per-frame correction instead of a compass error nobody notices.

#ifndef FVKIT_OVERLAY_MOVING_MAP_OVERLAY_H_
#define FVKIT_OVERLAY_MOVING_MAP_OVERLAY_H_

#include <memory>
#include <string>

#include "fvkit/canvas/canvas.h"
#include "fvkit/geo.h"
#include "fvkit/nav/camera.h"
#include "fvkit/nav/camera_slew.h"
#include "fvkit/nav/heading.h"
#include "fvkit/nav/position.h"
#include "fvkit/nav/road_snap.h"
#include "fvkit/overlay/overlay.h"
#include "fvkit/proj.h"
#include "fvkit/symbol/builtin.h"

namespace fv {

// What one tick decided. `slew` is the thing a shell applies; the rest is
// what a shell DISPLAYS (a status bar wants the fix, a debug view wants the
// camera's own answer before the slew smoothed it).
struct MovingMapTick {
  // A fix was taken off the queue this tick. False means nothing arrived —
  // which is the common case, since a shell ticks faster than a receiver
  // reports.
  bool new_fix = false;
  // The fix the rest of this tick was computed from — the SNAPPED one when
  // snapping applied (see `snap`). `snap.raw` is always what the receiver
  // said.
  PositionFix fix;
  ResolvedHeading heading;

  // MM5. What the snapper made of this fix, and whether the overlay acted on
  // it. `snapped` false with `snap_applied` false is the ordinary state of a
  // shell with no road network — the whole feature off.
  SnappedFix snap;
  bool snap_applied = false;

  // MM2's answer for this fix. `changed` false when the ship is still in its
  // apron; every field is meaningless when `new_fix` is false.
  CameraTarget target;

  // MM3's answer for this frame — the centre and rotation to apply NOW.
  // `changed` is the only flag a shell needs; `active` says another frame is
  // coming, which is what a shell drives its redraw clock off.
  SlewState slew;
};

class MovingMapOverlay : public Overlay {
 public:
  explicit MovingMapOverlay(std::string name = "Moving Map");
  ~MovingMapOverlay() override;

  // The registered type id. No extension: a static type opens no document.
  static const char kTypeId[];  // "fv.movingmap"

  // --- the feed -----------------------------------------------------------

  // Wires `source`'s listener to this overlay's queue, so a source that
  // delivers on its own thread is safe by construction (MM1's threading rule).
  // Replacing a source clears the queue and the heading history: the fixes
  // still in flight belong to the feed that is going away, and a heading
  // derived across the seam would be the bearing from one ship to another.
  // Null detaches.
  void SetSource(std::shared_ptr<IPositionSource> source);
  const std::shared_ptr<IPositionSource>& source() const { return source_; }

  // Start/stop the wired source. Starting resets the heading history for the
  // reason heading.h states — a restarted script begins where it began, not
  // where the last run ended. kNotFound with no source.
  Status Start();
  void Stop();
  bool running() const { return source_ && source_->running(); }

  // Feed one fix directly, bypassing the source. This is the seam a shell with
  // its own receiver uses, and it is what the tests drive.
  void PushFix(const PositionFix& fix) { queue_.Push(fix); }

  FixQueue& queue() { return queue_; }

  // --- snap to road (MM5) -------------------------------------------------

  // Hand the overlay a road network and every fix is put on the road it is
  // most likely on before anything else sees it: the heading resolver, the
  // camera and the drawn symbol all take the snapped position, and a snapped
  // fix that is MOVING also carries the road's bearing as its heading. Null
  // turns it off, which is the default and is what a shell with no `.fvroad`
  // has.
  //
  // The network is `fv::routing::RoadGraphNetwork` in the port
  // (port/Routing/fv_road_network.h) — fvkit does not link the router, so the
  // application is what puts the two together.
  //
  // THE RAW FIX SURVIVES IT: `MovingMapTick::snap.raw` is what the receiver
  // said, so a shell can draw both, and `last_fix()` is what was drawn.
  void SetRoadNetwork(std::shared_ptr<const IRoadNetwork> network);
  bool snapping() const { return snapper_.enabled(); }
  RoadSnapper& snapper() { return snapper_; }
  const RoadSnapper& snapper() const { return snapper_; }

  // How sure the snapper has to be before the overlay uses its answer rather
  // than the raw fix. 0.25 by default: under a quarter the snapper is
  // reporting "it could be any of these roads", and a ship jumped onto a
  // guess is worse than a ship a few metres off the road it is on. Set 0 to
  // take every snap it offers.
  void SetSnapMinConfidence(double c);
  double snap_min_confidence() const { return snap_min_confidence_; }

  // What the last fix snapped to, whether or not it was applied.
  const SnappedFix& last_snap() const { return snapper_.last(); }

  // --- modes --------------------------------------------------------------

  // Setting the modes FORCES the next tick to recentre. Without that, turning
  // auto-centring on would do nothing at all until the ship happened to wander
  // out of an apron computed while it was off — the map would sit still after
  // the user asked for exactly the opposite. This is FalconView's own
  // `force_update`, reached the same way its toggles reach it.
  void SetModes(const CameraModes& modes);
  const CameraModes& modes() const { return camera_.modes(); }
  void SetAutoCenter(bool on);
  void SetAutoRotate(bool on);
  void SetContinuous(bool on);

  // Recentre on the next tick whatever the apron says.
  void ForceRecenter() { force_ = true; }

  MovingMapCamera& camera() { return camera_; }
  const MovingMapCamera& camera() const { return camera_; }
  CameraSlew& slew() { return slew_; }
  const CameraSlew& slew() const { return slew_; }

  void SetSlewSettings(const SlewSettings& s) { slew_.SetSettings(s); }
  const SlewSettings& slew_settings() const { return slew_.settings(); }

  // The map's current clockwise rotation. Since PR3 `Tick` adopts this from
  // the projection it is handed, so on a rotating shell the setter is only
  // the initial condition (`ResetMap` takes it too); a shell that cannot
  // rotate leaves it at 0 and `SetRotationSupported(false)` pins it there.
  void SetMapRotation(double degrees) { map_rotation_deg_ = degrees; }
  double map_rotation_deg() const { return map_rotation_deg_; }

  // CAN THE SHELL ACTUALLY ROTATE THE MAP? Default true, because that is
  // fvkit's contract — `Tick` answers and the shell applies what it answered.
  //
  // It is a question worth asking because as of MM4 **NO shell in the port can
  // say yes**: `MapProjection` has no rotation, `MapEngine` has none, and
  // PythonView therefore applies the slew's centre and drops its rotation on
  // the floor. Left to assume, the overlay believes a rotation it asked for was
  // applied and counter-rotates the ownship to match — so in track-up mode a
  // ship steaming east is drawn pointing up while the chart under it is still
  // north-up, which is a compass error and not a cosmetic one.
  //
  // Told false, the overlay pins the rotation at 0: the camera is asked for a
  // placement on an unrotated chart, the ship is drawn at its true screen
  // bearing, and `auto_rotate` degrades to the track-up ANCHOR alone (the ship
  // held below centre with the map ahead of it), which is a real behaviour and
  // an honest one. `rotation_supported()` is what a shell greys its own
  // track-up control on.
  void SetRotationSupported(bool on);
  bool rotation_supported() const { return rotation_supported_; }

  // Meridian convergence at the ship — MM2's forgotten term, identically zero
  // on every projection the port has, and settable so that it does not have to
  // be rediscovered when one of them isn't.
  void SetConvergence(double degrees) { convergence_deg_ = degrees; }
  double convergence_deg() const { return convergence_deg_; }

  // The map moved behind our back (a user pan, a jump to a bookmark). Cancels
  // any slew in flight and re-bases it, exactly `CameraSlew::Reset`.
  void ResetMap(const GeoPoint& center, double rotation_deg = 0.0);

  // --- the tick -----------------------------------------------------------

  // Drains the queue, resolves a heading, asks the camera and advances the
  // slew by `dt_s` seconds. Call it once per frame from the shell's own tick.
  //
  // EVERY QUEUED FIX GOES THROUGH THE HEADING RESOLVER; ONLY THE LAST ONE
  // REACHES THE CAMERA. The resolver's whole state is a short history of
  // distinct positions, so skipping fixes would make a derived heading depend
  // on how fast the shell happened to be ticking; the camera, by contrast,
  // wants the present, and catching up through history would walk the map over
  // ground the ship has already left.
  MovingMapTick Tick(const MapProjection& proj, double dt_s);

  // --- what has been seen --------------------------------------------------

  bool has_fix() const { return has_fix_; }
  const PositionFix& last_fix() const { return last_fix_; }
  ResolvedHeading heading() const { return heading_.current(); }
  HeadingResolver& heading_resolver() { return heading_; }

  // Where the ship was last DRAWN, in surface pixels, and whether it has been.
  // Exposed because it is the input the apron is built from and therefore the
  // one number that explains why the map did or did not move.
  bool has_drawn() const { return has_drawn_; }
  double drawn_x() const { return drawn_x_; }
  double drawn_y() const { return drawn_y_; }

  // The angle the symbol is stamped at: heading + convergence - map rotation,
  // normalized to [0, 360). Public because it is what the drawing is pinned
  // against and because a shell drawing a compass rose or a heading readout
  // wants the same number the ship is drawn at, not the raw heading.
  double screen_angle_deg() const;

  // --- how the ship is drawn ----------------------------------------------

  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  // Any id the builtin library answers to; the default is the aircraft (G2's
  // `fv.ownship`). A caller whose platform is a boat or a car asks for
  // `fv.north`, which is the generic chevron.
  void SetSymbolId(std::string id) { symbol_id_ = std::move(id); }
  const std::string& symbol_id() const { return symbol_id_; }

  // Full width on screen, in pixels, before symbol_dpi_scale. 24 by default —
  // an ownship is furniture, not one marker in a set.
  void SetSizePx(double px) { size_px_ = px > 0.0 ? px : 1.0; }
  double size_px() const { return size_px_; }

  void SetColor(FvColor c);
  FvColor color() const { return color_; }

  // The black outline stamped one size up under the body, exactly
  // PointOverlay's badge edge and for the same reason: a coloured silhouette
  // over a chart of the same colour is not there at all.
  void SetShowEdge(bool on) { show_edge_ = on; }
  bool show_edge() const { return show_edge_; }

  // G4's render state, for a shell that wants the ship called out (a search
  // result, a selected platform when there is more than one feed).
  void SetHighlighted(bool on) { highlighted_ = on; }
  bool highlighted() const { return highlighted_; }

  // Draws the apron as a dashed rectangle. Off by default and deliberately
  // available: the apron is the hardest part of the moving map to believe, and
  // a picture of it settles an argument that a log line does not.
  void SetShowApron(bool on) { show_apron_ = on; }
  bool show_apron() const { return show_apron_; }

  // The device scale GeoDraw carries for symbology (the ledger's symbol-DPI
  // item). 1.0 = the authored pixel size exactly.
  void SetSymbolDpiScale(double s) { dpi_scale_ = s > 0.0 ? s : 1.0; }
  double symbol_dpi_scale() const { return dpi_scale_; }

 private:
  std::shared_ptr<IPositionSource> source_;
  FixQueue queue_;
  HeadingResolver heading_;
  RoadSnapper snapper_;
  double snap_min_confidence_ = 0.25;
  MovingMapCamera camera_;
  CameraSlew slew_;
  BuiltinSymbolLibrary body_;
  BuiltinSymbolLibrary edge_;

  PositionFix last_fix_;
  bool has_fix_ = false;
  bool force_ = false;
  double map_rotation_deg_ = 0.0;
  double convergence_deg_ = 0.0;

  bool rotation_supported_ = true;
  bool has_drawn_ = false;
  double drawn_x_ = 0.0;
  double drawn_y_ = 0.0;

  std::string symbol_id_ = builtin_symbol::kOwnship;
  double size_px_ = 24.0;
  FvColor color_{230, 30, 30, 255};
  bool show_edge_ = true;
  bool highlighted_ = false;
  bool show_apron_ = false;
  double dpi_scale_ = 1.0;
};

}  // namespace fv

#endif  // FVKIT_OVERLAY_MOVING_MAP_OVERLAY_H_
