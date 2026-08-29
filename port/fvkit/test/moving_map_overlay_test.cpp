// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// MM4: the ownship overlay — MM1's feed, MM2's camera and MM3's slew held in
// the order they belong in, plus the symbol that says where the ship is
// pointing.
//
// WHAT IS WORTH PINNING HERE, as against what merely passes. The three layers
// under this one already have their own suites, so this file does not re-test
// the apron geometry or the easing: it tests the WIRING those tests cannot see
// — that the apron is recomputed from the DRAWN position and not the new one
// (MM2's load-bearing ordering, which is structural here), that every queued
// fix reaches the heading resolver while only the last reaches the camera, and
// that a mode change forces a recentre instead of waiting on a stale apron.
//
// And it pins the one thing a golden could never state: WHICH WAY THE SHIP IS
// POINTING. A symbol that draws the same picture at every heading passes any
// hash you like, so the rotation gets a directional assertion over the ink, per
// the ledger's rule about asymmetric behaviour.

#include "fvkit/overlay/moving_map_overlay.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/app/type_registry.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/nav/scripted_source.h"
#include "fvkit/symbol/builtin.h"

namespace {

using fv::CameraModes;
using fv::MovingMapOverlay;
using fv::MovingMapTick;
using fv::PositionFix;
using fv::SlewSettings;

// Kiawah Island at harbour scale — the same water every other nav test sails.
fv::MapProjection Proj() {
  fv::MapProjection p;
  p.SetSurfaceSize(800, 600);
  p.SetCenter({32.60, -80.08});
  p.SetScale(100000.0);
  return p;
}

PositionFix Fix(double lat, double lon, double heading_deg = -1.0) {
  PositionFix f;
  f.SetPosition(lat, lon);
  if (heading_deg >= 0.0) {
    f.true_heading_deg = heading_deg;
    f.has_true_heading = true;
  }
  return f;
}

// A jump, not a slew: every test below that is about the CAMERA wants the
// map where the camera said, this tick, with no easing in the way.
SlewSettings Jump() {
  SlewSettings s;
  s.duration_s = 0.0;
  return s;
}

// The ink's bounding box, and its extremes. Background is transparent, so a
// pixel with any alpha at all is ink.
struct InkBox {
  int min_x = 1 << 30, min_y = 1 << 30, max_x = -1, max_y = -1;
  bool empty() const { return max_x < 0; }
  int width() const { return max_x - min_x + 1; }
  int height() const { return max_y - min_y + 1; }
};

InkBox Ink(const fv::CpuCanvas& canvas) {
  InkBox b;
  const fv::PixelBuffer& buf = canvas.Buffer();
  for (int y = 0; y < buf.Height(); ++y) {
    const unsigned char* row = buf.Row(y);
    for (int x = 0; x < buf.Width(); ++x) {
      if (row[x * 4 + 3] == 0) continue;
      b.min_x = std::min(b.min_x, x);
      b.max_x = std::max(b.max_x, x);
      b.min_y = std::min(b.min_y, y);
      b.max_y = std::max(b.max_y, y);
    }
  }
  return b;
}

// --- the symbol ------------------------------------------------------------

TEST(OwnshipSymbol, TheLibraryAnswersToTheOwnshipIdAndListsIt) {
  fv::BuiltinSymbolLibrary lib;
  const fv::VectorSymbol* s = lib.Symbol(fv::builtin_symbol::kOwnship);
  ASSERT_NE(s, nullptr);
  EXPECT_FALSE(s->primitives.empty());

  bool listed = false;
  for (const char* const* p = fv::builtin_symbol::kAll; *p != nullptr; ++p)
    if (std::string(*p) == fv::builtin_symbol::kOwnship) listed = true;
  EXPECT_TRUE(listed) << "an id the library answers to must be enumerable";
}

// The whole point of an ownship is that it says which end is the front. A
// symmetric silhouette would pass a bounds check and a golden hash and still
// be useless, so this asserts the ASYMMETRY directly.
TEST(OwnshipSymbol, TheNoseIsOnTheAxisAndTheWingsAreForwardOfTheTail) {
  fv::BuiltinSymbolLibrary lib;
  const fv::VectorSymbol* s = lib.Symbol(fv::builtin_symbol::kOwnship);
  ASSERT_NE(s, nullptr);
  ASSERT_EQ(s->primitives.size(), 1u) << "one closed ring, not three strokes";
  const std::vector<fv::SymbolPoint>& pts = s->primitives[0].points;
  ASSERT_GE(pts.size(), 8u);

  // Symbol space is y UP, so the nose is the largest y — and it is unique and
  // on the axis, which is what makes the point read as a point.
  size_t nose = 0;
  for (size_t i = 1; i < pts.size(); ++i)
    if (pts[i].y > pts[nose].y) nose = i;
  EXPECT_NEAR(pts[nose].x, 0.0, 1e-9);
  for (size_t i = 0; i < pts.size(); ++i)
    if (i != nose) EXPECT_LT(pts[i].y, pts[nose].y);

  // The widest span (the wings) sits forward of the aftmost vertex (the
  // tailplane), which is the asymmetry that survives being drawn at 12 px.
  size_t wing = 0, tail = 0;
  for (size_t i = 1; i < pts.size(); ++i) {
    if (std::fabs(pts[i].x) > std::fabs(pts[wing].x)) wing = i;
    if (pts[i].y < pts[tail].y) tail = i;
  }
  EXPECT_GT(pts[wing].y, pts[tail].y);
  // ...and aft of the nose, or the "aircraft" is a triangle.
  EXPECT_LT(pts[wing].y, pts[nose].y);
}

// --- the drawing -----------------------------------------------------------

TEST(MovingMapOverlay, TheShipIsDrawnAtTheFixAndRotatedToTheHeading) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  ovl.SetShowEdge(false);  // one stamp, so the ink is the silhouette itself
  ovl.SetSizePx(40.0);
  // Auto-centring off: this test is about the SYMBOL, and a camera that moved
  // the map under it would change where the ink lands.
  CameraModes modes;
  modes.auto_center = false;
  ovl.SetModes(modes);

  // Due north. The nose is the topmost ink and it is over the ship.
  ovl.PushFix(Fix(32.60, -80.08, 0.0));
  ovl.Tick(proj, 0.1);
  fv::CpuCanvas north(800, 600);
  north.Clear(fv::FvColor{0, 0, 0, 0});
  ASSERT_TRUE(ovl.OnDraw(proj, north).ok());
  const InkBox nb = Ink(north);
  ASSERT_FALSE(nb.empty());
  EXPECT_NEAR((nb.min_x + nb.max_x) / 2.0, ovl.drawn_x(), 2.0);
  EXPECT_NEAR((nb.min_y + nb.max_y) / 2.0, ovl.drawn_y(), 2.0);
  // Taller than it is wide: the aircraft's length is up the screen.
  EXPECT_GT(nb.height(), nb.width());

  // Due east. The same silhouette, turned a quarter: now wider than tall.
  MovingMapOverlay east_ovl;
  east_ovl.SetSlewSettings(Jump());
  east_ovl.SetShowEdge(false);
  east_ovl.SetSizePx(40.0);
  east_ovl.SetModes(modes);
  east_ovl.PushFix(Fix(32.60, -80.08, 90.0));
  east_ovl.Tick(proj, 0.1);
  fv::CpuCanvas east(800, 600);
  east.Clear(fv::FvColor{0, 0, 0, 0});
  ASSERT_TRUE(east_ovl.OnDraw(proj, east).ok());
  const InkBox eb = Ink(east);
  ASSERT_FALSE(eb.empty());
  EXPECT_GT(eb.width(), eb.height());
  EXPECT_DOUBLE_EQ(east_ovl.screen_angle_deg(), 90.0);

  // ...AND IT POINTS EAST, not west. `PointSymbolStyle::rotation_deg` turns a
  // symbol COUNTER-clockwise on screen (FalconView's sense, preserved), so a
  // compass heading has to be negated at this seam — and a symmetric-looking
  // test ("wider than tall") passes either way, which is exactly how a ship
  // flying backwards ships. The nose is the SINGLE most extreme point along
  // the heading, so the ink reaches further east of the ship than west.
  const double cx = east_ovl.drawn_x();
  EXPECT_GT(eb.max_x - cx, cx - eb.min_x);
  // The same statement for north, where the nose is UP the screen (-y).
  const double cy = ovl.drawn_y();
  EXPECT_GT(cy - nb.min_y, nb.max_y - cy);
}

// The drawn angle IS the camera's `point_angle`, and it ADDS the rotation.
// Getting this backwards is not cosmetic: it drew a ship steaming east as
// pointing south, and it survived every other assertion in this file.
//
// The 30 degrees is put on the PROJECTION and not through SetMapRotation,
// which is PR3's doing: a tick adopts the rotation from the projection it is
// handed, so a rotation the chart does not actually have no longer survives
// one. Same sum, now stated over a chart that is really turned.
TEST(MovingMapOverlay, TheDrawnAngleIsPointAngleAndAddsTheMapRotation) {
  fv::MapProjection proj = Proj();
  ASSERT_TRUE(proj.SetRotation(30.0).ok());
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  CameraModes modes;
  modes.auto_center = false;
  ovl.SetModes(modes);
  ovl.PushFix(Fix(32.60, -80.08, 45.0));
  ovl.Tick(proj, 0.1);
  EXPECT_DOUBLE_EQ(ovl.screen_angle_deg(), 75.0);

  // Convergence is the third term of the same sum.
  ovl.SetConvergence(10.0);
  EXPECT_DOUBLE_EQ(ovl.screen_angle_deg(), 85.0);

  // Normalized, so a caller feeding it straight into a symbol rotation never
  // has to wrap it: 350 + 30 is 20, not 380.
  ovl.SetConvergence(0.0);
  ovl.PushFix(Fix(32.60, -80.08, 350.0));
  ovl.Tick(proj, 0.1);
  EXPECT_DOUBLE_EQ(ovl.screen_angle_deg(), 20.0);
}

// Track-up means exactly this and nothing else: the ship points UP. It is the
// case that fixes the sign of the rotation term, because it is the one place
// the camera's answer and the drawn angle have to cancel.
TEST(MovingMapOverlay, TrackUpDrawsTheShipUpTheScreenWhenTheShellCanRotate) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  CameraModes modes;
  modes.auto_rotate = true;
  ovl.SetModes(modes);

  // Due east, on a shell that applies what the tick returns.
  ovl.PushFix(Fix(32.60, -80.10, 90.0));
  const MovingMapTick t = ovl.Tick(proj, 0.1);
  ASSERT_TRUE(t.target.rotation_changed);
  EXPECT_DOUBLE_EQ(t.target.rotation_deg, 270.0);
  EXPECT_DOUBLE_EQ(ovl.map_rotation_deg(), 270.0);
  EXPECT_DOUBLE_EQ(ovl.screen_angle_deg(), 0.0) << "track up IS up";
}

// ...and a shell that CANNOT rotate must say so, or it gets a ship pointing at
// a chart orientation that never happened. No shell in the port can rotate as
// of MM4 (MapProjection carries no rotation), so this is the live case.
TEST(MovingMapOverlay, AShellThatCannotRotateDrawsTheTrueScreenBearing) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  ovl.SetRotationSupported(false);
  CameraModes modes;
  modes.auto_rotate = true;
  ovl.SetModes(modes);

  ovl.PushFix(Fix(32.60, -80.10, 90.0));
  const MovingMapTick t = ovl.Tick(proj, 0.1);
  // The camera still ANSWERS a rotation — it is answering the question it was
  // asked — but the overlay does not adopt it, so the ship is drawn east.
  EXPECT_DOUBLE_EQ(ovl.map_rotation_deg(), 0.0);
  EXPECT_DOUBLE_EQ(ovl.screen_angle_deg(), 90.0);
  (void)t;

  // Turning support back on and off again does not leave a rotation behind.
  ovl.SetRotationSupported(true);
  ovl.PushFix(Fix(32.60, -80.09, 90.0));
  ovl.Tick(proj, 0.1);
  ASSERT_DOUBLE_EQ(ovl.map_rotation_deg(), 270.0);
  ovl.SetRotationSupported(false);
  EXPECT_DOUBLE_EQ(ovl.map_rotation_deg(), 0.0);
  EXPECT_DOUBLE_EQ(ovl.screen_angle_deg(), 90.0);
}

// PR3: THE PROJECTION IS THE PLACE THE APPLIED ROTATION IS TRUE, and the
// overlay reads it back rather than remembering what it asked for. Until the
// projection could turn, `map_rotation_deg_` was a belief about the shell, and
// PR2 flagged the drift it invites: the ship points correctly only while the
// overlay's copy and the projection's agree, and nothing asserted it.
//
// The case that shows it is a chart turned by something OTHER than the
// camera — a shell with its own "turn the chart" gesture, or one that lagged
// a frame. Auto-rotate is off here, so before this change the overlay would
// have gone on believing in a north-up chart and drawn an eastbound ship at
// 090 over a chart turned 30, which is 30 degrees of compass error.
TEST(MovingMapOverlay, TheOverlayFollowsAChartTurnedBehindItsBack) {
  fv::MapProjection proj = Proj();
  ASSERT_TRUE(proj.SetRotation(30.0).ok());
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());  // auto_rotate stays OFF

  ovl.PushFix(Fix(32.60, -80.10, 90.0));
  ovl.Tick(proj, 0.1);
  EXPECT_DOUBLE_EQ(ovl.map_rotation_deg(), 30.0);
  EXPECT_DOUBLE_EQ(ovl.screen_angle_deg(), 120.0)
      << "heading + the rotation the chart REALLY has";

  // Straightening the chart is followed the same way, and back to 090.
  ASSERT_TRUE(proj.SetRotation(0.0).ok());
  ovl.PushFix(Fix(32.60, -80.09, 90.0));
  ovl.Tick(proj, 0.1);
  EXPECT_DOUBLE_EQ(ovl.map_rotation_deg(), 0.0);
  EXPECT_DOUBLE_EQ(ovl.screen_angle_deg(), 90.0);

  // And the term arrives WRAPPED, which is the second thing adoption buys:
  // SetRotation normalizes into [0, 360) while a slew's arithmetic does not,
  // and MM2's point_angle takes one 360 subtraction off the sum — enough only
  // while both terms are in range. -30 reaches the overlay as 330.
  ASSERT_TRUE(proj.SetRotation(-30.0).ok());
  ovl.PushFix(Fix(32.60, -80.08, 90.0));
  ovl.Tick(proj, 0.1);
  EXPECT_DOUBLE_EQ(ovl.map_rotation_deg(), 330.0);
  EXPECT_DOUBLE_EQ(ovl.screen_angle_deg(), 60.0);
}

// A shell that says it cannot rotate is still pinned at 0 — adoption is
// gated on the same flag, or a projection someone else turned would put a
// rotation back into an overlay that was told the map cannot turn.
TEST(MovingMapOverlay, AShellThatCannotRotateAdoptsNothing) {
  fv::MapProjection proj = Proj();
  ASSERT_TRUE(proj.SetRotation(30.0).ok());
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  ovl.SetRotationSupported(false);

  ovl.PushFix(Fix(32.60, -80.10, 90.0));
  ovl.Tick(proj, 0.1);
  EXPECT_DOUBLE_EQ(ovl.map_rotation_deg(), 0.0);
  EXPECT_DOUBLE_EQ(ovl.screen_angle_deg(), 90.0);
}

TEST(MovingMapOverlay, NothingIsDrawnBeforeTheFirstFixAndTheApronStaysEmpty) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  fv::CpuCanvas canvas(800, 600);
  canvas.Clear(fv::FvColor{0, 0, 0, 0});
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_TRUE(Ink(canvas).empty());
  EXPECT_FALSE(ovl.has_drawn());
  // An EMPTY apron is what makes the first fix recentre (MM2).
  EXPECT_TRUE(ovl.camera().apron().empty());
}

// --- the wiring ------------------------------------------------------------

TEST(MovingMapOverlay, TheFirstFixRecentresBecauseNothingHasBeenDrawnYet) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());

  // A ship well away from the map centre, and no draw has happened, so there
  // is no apron to be inside.
  ovl.PushFix(Fix(32.62, -80.02, 45.0));
  const MovingMapTick t = ovl.Tick(proj, 0.1);
  EXPECT_TRUE(t.new_fix);
  EXPECT_TRUE(t.target.changed);
  EXPECT_TRUE(t.slew.changed) << "duration 0 is the FalconView jump";
  // The jump landed on the camera's answer this very tick.
  EXPECT_NEAR(t.slew.center.lat, t.target.center.lat, 1e-12);
  EXPECT_NEAR(t.slew.center.lon, t.target.center.lon, 1e-12);
}

// MM2's ordering, and the reason the camera lives on the overlay: the apron is
// built from where the ship was DRAWN and tested against where it has just
// moved to. Rebuilding it around the new position would ask "may the ship be
// here?" of a box drawn around the ship being here, and the map would never
// move again.
TEST(MovingMapOverlay, AShipThatStaysInsideItsDrawnApronDoesNotMoveTheMap) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());

  ovl.PushFix(Fix(32.60, -80.08, 0.0));
  ovl.Tick(proj, 0.1);

  fv::CpuCanvas canvas(800, 600);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  ASSERT_FALSE(ovl.camera().apron().empty());
  ASSERT_TRUE(ovl.has_drawn());

  // A metre or two north: still comfortably inside the apron just computed.
  ovl.PushFix(Fix(32.6001, -80.08, 0.0));
  const MovingMapTick t = ovl.Tick(proj, 0.1);
  EXPECT_TRUE(t.new_fix);
  EXPECT_FALSE(t.target.changed);
  EXPECT_FALSE(t.slew.changed);
}

TEST(MovingMapOverlay, AShipThatLeavesTheApronMovesTheMap) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());

  ovl.PushFix(Fix(32.60, -80.08, 0.0));
  ovl.Tick(proj, 0.1);
  fv::CpuCanvas canvas(800, 600);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  ASSERT_FALSE(ovl.camera().apron().empty());

  ovl.PushFix(Fix(32.75, -80.08, 0.0));  // far north, out of the box
  const MovingMapTick t = ovl.Tick(proj, 0.1);
  EXPECT_TRUE(t.target.changed);
}

// The toggle must not wait on an apron computed while it was off — that is
// FalconView's own force_update, and without it turning auto-centring on
// appears to do nothing.
TEST(MovingMapOverlay, ChangingAModeForcesTheNextTickToRecentre) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());

  ovl.PushFix(Fix(32.60, -80.08, 0.0));
  ovl.Tick(proj, 0.1);
  fv::CpuCanvas canvas(800, 600);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());

  // Auto-centring off: the camera never moves the map, forced or not.
  ovl.SetAutoCenter(false);
  ovl.PushFix(Fix(32.6001, -80.08, 0.0));
  ASSERT_FALSE(ovl.Tick(proj, 0.1).target.changed);

  // Back on. The ship has not moved out of the apron left over from the draw
  // above, so without the force this tick would do nothing at all — which is
  // the map sitting still immediately after the user asked for the opposite.
  ovl.SetAutoCenter(true);
  ovl.PushFix(Fix(32.6002, -80.08, 0.0));
  const MovingMapTick t = ovl.Tick(proj, 0.1);
  EXPECT_TRUE(t.target.changed) << "a mode change forces a recentre";

  // ...and the force is spent, not sticky.
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  ovl.PushFix(Fix(32.6003, -80.08, 0.0));
  EXPECT_FALSE(ovl.Tick(proj, 0.1).target.changed);
}

TEST(MovingMapOverlay, ForceRecenterIsTheSameLeverWithoutAModeChange) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  ovl.PushFix(Fix(32.60, -80.08, 0.0));
  ovl.Tick(proj, 0.1);
  fv::CpuCanvas canvas(800, 600);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());

  ovl.ForceRecenter();
  ovl.PushFix(Fix(32.6001, -80.08, 0.0));
  EXPECT_TRUE(ovl.Tick(proj, 0.1).target.changed);
}

// The resolver's whole state is a history of distinct positions, so a shell
// that ticks slowly must not make the derived heading depend on its tick rate.
// The camera, by contrast, wants the present.
TEST(MovingMapOverlay, EveryQueuedFixReachesTheResolverAndOnlyTheLastTheCamera) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  CameraModes modes;
  modes.auto_center = false;
  ovl.SetModes(modes);

  // Four distinct positions arrive between two ticks, and none reports a
  // heading, so the resolver has to derive one.
  ovl.PushFix(Fix(32.600, -80.08));
  ovl.PushFix(Fix(32.601, -80.08));
  ovl.PushFix(Fix(32.602, -80.08));
  ovl.PushFix(Fix(32.603, -80.08));
  const MovingMapTick t = ovl.Tick(proj, 0.1);

  EXPECT_TRUE(t.new_fix);
  EXPECT_EQ(ovl.heading_resolver().history_size(), 4u)
      << "every fix enters the history";
  EXPECT_EQ(ovl.last_fix().lat, 32.603) << "the camera sees the present";
  EXPECT_TRUE(t.heading.known);
  EXPECT_FALSE(t.heading.reported);
  EXPECT_NEAR(t.heading.degrees, 0.0, 1e-9) << "northbound";
}

TEST(MovingMapOverlay, ATickWithNoFixReportsTheHeadingItAlreadyHad) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  ovl.PushFix(Fix(32.60, -80.08, 123.0));
  ASSERT_TRUE(ovl.Tick(proj, 0.1).new_fix);

  const MovingMapTick t = ovl.Tick(proj, 0.1);
  EXPECT_FALSE(t.new_fix);
  EXPECT_TRUE(t.heading.known);
  EXPECT_DOUBLE_EQ(t.heading.degrees, 123.0);
  EXPECT_FALSE(t.slew.changed) << "nothing arrived, nothing to redraw";
}

// MM3 through the overlay: a non-zero duration arrives over several frames and
// lands on the camera's answer, rather than teleporting to it.
TEST(MovingMapOverlay, ANonZeroDurationArrivesOverSeveralFrames) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  SlewSettings s;
  s.duration_s = 0.4;
  s.easing = fv::SlewEasing::kLinear;
  ovl.SetSlewSettings(s);

  ovl.PushFix(Fix(32.62, -80.02, 45.0));
  const MovingMapTick first = ovl.Tick(proj, 0.1);
  ASSERT_TRUE(first.target.changed);
  EXPECT_TRUE(first.slew.active);
  EXPECT_GT(std::fabs(first.slew.center.lat - first.target.center.lat), 1e-9)
      << "a quarter of the way there, not all of it";

  for (int i = 0; i < 10 && ovl.slew().active(); ++i) ovl.Tick(proj, 0.1);
  EXPECT_FALSE(ovl.slew().active());
  EXPECT_NEAR(ovl.slew().center().lat, first.target.center.lat, 1e-9);
  EXPECT_NEAR(ovl.slew().center().lon, first.target.center.lon, 1e-9);
}

// --- the source ------------------------------------------------------------

TEST(MovingMapOverlay, AScriptedSourceDrivesItThroughTheQueue) {
  fv::MapProjection proj = Proj();
  auto source = std::make_shared<fv::ScriptedSource>();
  double now = 0.0;
  source->SetClock([&now] { return now; });
  source->SetTrack(fv::BuildScriptedTrack(
      {{32.600, -80.10}, {32.600, -80.05}}, 20.0));
  ASSERT_FALSE(source->track().empty());

  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  ovl.SetSource(source);
  ASSERT_TRUE(ovl.Start().ok());
  EXPECT_TRUE(ovl.running());

  // The first fix is emitted from inside Start() (MM1), so it is already
  // queued and the overlay has not had to know anything about polling.
  MovingMapTick t = ovl.Tick(proj, 0.1);
  EXPECT_TRUE(t.new_fix);
  EXPECT_TRUE(ovl.has_fix());

  now += 5.0;
  source->Poll();
  t = ovl.Tick(proj, 0.1);
  EXPECT_TRUE(t.new_fix);
  EXPECT_GT(ovl.last_fix().lon, -80.10) << "the ship is going east";
  EXPECT_TRUE(t.heading.reported) << "BuildScriptedTrack stamps a course";

  ovl.Stop();
  EXPECT_FALSE(ovl.running());
}

// The queue's listener captures the overlay. A source outliving it must not be
// able to deliver into freed memory — the detach is in the destructor, and
// this is the test that would crash under ASan if it were not.
TEST(MovingMapOverlay, ASourceThatOutlivesTheOverlayIsDetached) {
  auto source = std::make_shared<fv::ScriptedSource>();
  {
    MovingMapOverlay ovl;
    ovl.SetSource(source);
  }
  PositionFix f = Fix(32.60, -80.08, 0.0);
  source->SetTrack({fv::ScriptedFix{0.0, f}});
  ASSERT_TRUE(source->Start().ok());  // emits at t=0 into a listener or none
  source->Poll();
  SUCCEED();
}

TEST(MovingMapOverlay, ReplacingTheSourceForgetsTheOldFeedsHistory) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  ovl.PushFix(Fix(32.600, -80.08));
  ovl.PushFix(Fix(32.601, -80.08));
  ovl.Tick(proj, 0.1);
  ASSERT_EQ(ovl.heading_resolver().history_size(), 2u);

  ovl.SetSource(std::make_shared<fv::ScriptedSource>());
  EXPECT_EQ(ovl.heading_resolver().history_size(), 0u);
  EXPECT_FALSE(ovl.has_fix());
}

// --- snap to road (MM5) ----------------------------------------------------

// A network of one road: it runs due east through the projection's centre,
// and every fix below is a metre count off it.
class OneRoad : public fv::IRoadNetwork {
 public:
  void QueryNear(const fv::GeoPoint& p, double radius_m,
                 std::vector<fv::RoadCandidate>* out) const override {
    fv::SegmentProjection sp;
    const fv::GeoPoint a{32.60, -80.12};
    const fv::GeoPoint b{32.60, -80.04};
    if (!fv::ProjectOntoSegment(p, a, b, &sp)) return;
    if (sp.distance_m > radius_m) return;
    fv::RoadCandidate c;
    c.arc = 7;
    c.from_node = 1;
    c.to_node = 2;
    c.point = sp.point;
    c.distance_m = sp.distance_m;
    c.bearing_deg = sp.bearing_deg;
    c.length_m = sp.length_m;
    c.along_m = sp.along_m;
    c.name = "Test Road";
    out->push_back(c);
  }
};

// 10 m north of the road, in degrees of latitude.
constexpr double kTenMetresLat = 10.0 / (6371008.8 * 3.14159265358979323846 / 180.0);

TEST(MovingMapOverlay, DrawsTheShipOnTheRoadRatherThanWhereTheReceiverSaidItWas) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  EXPECT_FALSE(ovl.snapping());

  ovl.SetRoadNetwork(std::make_shared<OneRoad>());
  EXPECT_TRUE(ovl.snapping());
  ovl.PushFix(Fix(32.60 + kTenMetresLat, -80.08));

  const MovingMapTick t = ovl.Tick(proj, 0.1);
  ASSERT_TRUE(t.new_fix);
  ASSERT_TRUE(t.snap.snapped);
  EXPECT_TRUE(t.snap_applied);
  EXPECT_EQ(t.snap.road_name, "Test Road");
  EXPECT_NEAR(t.snap.offset_m, 10.0, 0.5);

  // The ship the overlay carries -- and therefore draws, and builds its apron
  // from -- is on the road.
  EXPECT_NEAR(ovl.last_fix().lat, 32.60, 1e-9);
  // ... and the raw fix is still there to be drawn beside it.
  EXPECT_NEAR(t.snap.raw.lat, 32.60 + kTenMetresLat, 1e-12);
}

TEST(MovingMapOverlay, TakesTheRoadsBearingAsTheHeadingOfAMovingShip) {
  // Three fixes tracking east along the road, scattered 20 m north, 2 m north
  // and 12 m north of it. The receiver reports no course, so the heading is
  // whatever the resolver can make of the positions it is given — and WHICH
  // positions those are is the whole of MM5's ordering.
  auto push = [](MovingMapOverlay& o) {
    const double lons[] = {-80.0810, -80.0805, -80.0800};
    const double norths[] = {2.0, 0.2, 1.2};
    for (int i = 0; i < 3; ++i) {
      PositionFix f = Fix(32.60 + norths[i] * kTenMetresLat, lons[i]);
      f.speed_mps = 10.0;
      f.has_speed = true;
      o.PushFix(f);
    }
  };

  // The control: no road network, so the heading is derived from the scatter
  // and wanders well off the line of the road actually being driven.
  fv::MapProjection proj = Proj();
  MovingMapOverlay raw;
  raw.SetSlewSettings(Jump());
  push(raw);
  const MovingMapTick rt = raw.Tick(proj, 0.1);
  ASSERT_TRUE(rt.heading.known);
  EXPECT_FALSE(rt.heading.reported);
  EXPECT_GT(std::fabs(rt.heading.degrees - 90.0), 5.0);

  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  ovl.SetRoadNetwork(std::make_shared<OneRoad>());
  push(ovl);
  const MovingMapTick t = ovl.Tick(proj, 0.1);
  ASSERT_TRUE(t.snap_applied);
  // A two-way road with no heading at all cannot say WHICH WAY along itself
  // the ship is going, so the first fixes are snapped without a bearing and
  // the resolver derives one from the snapped positions — which are on the
  // road. By the third fix that derived heading is the answer to the
  // direction question, and the road's own bearing takes over as a REPORTED
  // heading, which heading.h already prefers over any derivation.
  EXPECT_TRUE(t.heading.reported);
  EXPECT_NEAR(t.heading.degrees, 90.0, 0.5);
}

TEST(MovingMapOverlay, KeepsTheRawFixWhenTheSnapIsNotSureEnough) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  ovl.SetRoadNetwork(std::make_shared<OneRoad>());
  ovl.SetSnapMinConfidence(0.99);  // nothing short of dead-on will do

  ovl.PushFix(Fix(32.60 + kTenMetresLat, -80.08));
  const MovingMapTick t = ovl.Tick(proj, 0.1);
  ASSERT_TRUE(t.snap.snapped);      // the snapper still answered ...
  EXPECT_FALSE(t.snap_applied);     // ... and the overlay declined it
  EXPECT_NEAR(ovl.last_fix().lat, 32.60 + kTenMetresLat, 1e-12);
}

TEST(MovingMapOverlay, ForgetsTheRoadWhenTheFeedIsReplaced) {
  fv::MapProjection proj = Proj();
  MovingMapOverlay ovl;
  ovl.SetSlewSettings(Jump());
  ovl.SetRoadNetwork(std::make_shared<OneRoad>());
  ovl.PushFix(Fix(32.60 + kTenMetresLat, -80.08));
  ovl.Tick(proj, 0.1);
  ASSERT_NE(ovl.last_snap().arc, fv::kNoRoadArc);

  ovl.SetSource(std::make_shared<fv::ScriptedSource>());
  EXPECT_EQ(ovl.last_snap().arc, fv::kNoRoadArc);
}

// --- the registry ----------------------------------------------------------

TEST(MovingMapOverlay, ItIsRegisteredAsAStaticBuiltinType) {
  fv::app::OverlayTypeRegistry registry;
  ASSERT_TRUE(fv::app::RegisterBuiltinOverlayTypes(registry).ok());
  const fv::app::OverlayTypeDesc* desc =
      registry.Find(MovingMapOverlay::kTypeId);
  ASSERT_NE(desc, nullptr);
  EXPECT_TRUE(registry.IsStatic(MovingMapOverlay::kTypeId));
  EXPECT_FALSE(desc->restore_at_startup)
      << "an overlay that came back by itself would start asking a receiver "
         "for fixes on every run";
  std::shared_ptr<fv::Overlay> made = desc->factory();
  ASSERT_NE(made, nullptr);
  EXPECT_NE(dynamic_cast<MovingMapOverlay*>(made.get()), nullptr);
}

}  // namespace
