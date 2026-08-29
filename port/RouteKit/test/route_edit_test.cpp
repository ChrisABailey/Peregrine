// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// `RouteEditSession` — route.py's editor, in C++ and therefore in both shells.
//
// These tests are the transcription's receipt. Almost every one of them pins a
// behaviour that already worked in Python and that a rewrite is free to lose
// quietly: that a click is not a drag, that a whole drag is ONE undo entry,
// that Escape unwinds in a particular order, that a cancelled drag leaves no
// history behind it. None of that is visible in a diff.
//
// The exception is the snap, which is new here and is the reason the session
// exists at all: a waypoint placed over something exact takes that thing's
// coordinate. Two of the tests below are about the one way that can go wrong.

#include "fv_route_edit.h"

#include <gtest/gtest.h>

#include <memory>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/point_overlay.h"

namespace {

using fv::GeoPoint;
using fv::MapProjection;
using fv::PixelPoint;
using fv::RouteEditSession;
using fv::RouteOverlay;
using fv::RouteWaypoint;

MapProjection KiawahProj() {
  MapProjection p;
  p.SetSurfaceSize(800, 600);
  p.SetCenter({32.5987, -80.1130});
  p.SetScale(50000.0);
  return p;
}

std::vector<RouteWaypoint> TwoWaypoints() {
  return {{"A", {32.6044007, -80.1083007}}, {"B", {32.5930000, -80.1180000}}};
}

// A frame. The editor answers about what was DRAWN and through the projection
// that drew it, so a session that has never seen a frame is a session with no
// pixels to reason about -- which is exactly what `has_projection()` reports
// and what several of these tests lean on.
// Labels are off throughout: they are on by default for a route, and a label
// costs a TTF on the filesystem (`RouteOverlay.LabelsAreOnByDefaultAndThatCostsAFont`
// pins that price). None of these tests is about text.
void DrawOnce(RouteOverlay& ov, const MapProjection& proj) {
  ov.SetShowLabels(false);
  fv::CpuCanvas canvas(proj.SurfaceSize().width, proj.SurfaceSize().height);
  ASSERT_TRUE(ov.OnDraw(proj, canvas).ok());
}

// Where a waypoint's marker lands, in surface pixels.
PixelPoint PixelOf(const MapProjection& proj, const GeoPoint& g) {
  double x = 0, y = 0;
  EXPECT_TRUE(proj.GeoToSurface(g, &x, &y).ok());
  return PixelPoint{static_cast<int>(x + 0.5), static_cast<int>(y + 0.5)};
}

fv::MouseEvent At(PixelPoint p) {
  fv::MouseEvent e;
  e.x = p.x;
  e.y = p.y;
  return e;
}

fv::MouseEvent At(int x, int y) { return At(PixelPoint{x, y}); }

fv::KeyEvent KeyOf(int key) {
  fv::KeyEvent e;
  e.key = key;
  return e;
}

const RouteWaypoint* Find(const RouteOverlay& ov, const std::string& label) {
  for (const RouteWaypoint& w : ov.waypoints()) {
    if (w.label == label) return &w;
  }
  return nullptr;
}

// --- selection and the drag threshold --------------------------------------

TEST(RouteEdit, AClickOnAWaypointSelectsIt) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  const MapProjection proj = KiawahProj();
  DrawOnce(ov, proj);

  EXPECT_TRUE(ov.OnMouseDown(At(PixelOf(proj, ov.waypoints()[1].position))));
  EXPECT_EQ("B", ov.selected());
}

TEST(RouteEdit, AClickOnOpenMapIsDeclinedSoThePickSessionCanHaveIt) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  const MapProjection proj = KiawahProj();
  DrawOnce(ov, proj);

  // Not handled -> the stack keeps routing it down. An editor that swallowed
  // every click could not be used to pick anything.
  EXPECT_FALSE(ov.OnMouseDown(At(5, 5)));
  EXPECT_TRUE(ov.selected().empty());
}

TEST(RouteEdit, AStillHandIsAClickAndNotAnEdit) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  const MapProjection proj = KiawahProj();
  DrawOnce(ov, proj);
  ov.set_dirty(false);

  const PixelPoint at = PixelOf(proj, ov.waypoints()[0].position);
  ASSERT_TRUE(ov.OnMouseDown(At(at)));
  // Two pixels of tremor, under the 3-pixel threshold.
  ov.OnMouseMove(At(at.x + 1, at.y + 1));
  ov.OnMouseUp(At(at.x + 1, at.y + 1));

  EXPECT_EQ("A", ov.selected());
  // Selecting costs no undo entry and does not dirty the document: the user
  // has to MEAN an edit.
  EXPECT_FALSE(ov.edit().CanUndo());
  EXPECT_FALSE(ov.is_dirty());
  EXPECT_EQ(32.6044007, Find(ov, "A")->position.lat);
}

TEST(RouteEdit, AWholeDragIsOneUndoEntry) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  const MapProjection proj = KiawahProj();
  DrawOnce(ov, proj);
  const GeoPoint before = Find(ov, "A")->position;

  const PixelPoint at = PixelOf(proj, before);
  ASSERT_TRUE(ov.OnMouseDown(At(at)));
  for (int step = 10; step <= 60; step += 10) {
    ov.OnMouseMove(At(at.x + step, at.y + step));
  }
  ov.OnMouseUp(At(at.x + 60, at.y + 60));

  EXPECT_NE(before.lat, Find(ov, "A")->position.lat);
  EXPECT_TRUE(ov.is_dirty());
  ASSERT_TRUE(ov.edit().CanUndo());
  // ONE snapshot for six moves: the snapshot is taken at the first movement,
  // not at every one, so the drag undoes as the single thing the user did.
  ov.Undo();
  EXPECT_EQ(before.lat, Find(ov, "A")->position.lat);
  EXPECT_FALSE(ov.edit().CanUndo());
}

TEST(RouteEdit, EscapeMidDragRestoresThePositionAndLeavesNoHistory) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  const MapProjection proj = KiawahProj();
  DrawOnce(ov, proj);
  ov.set_dirty(false);
  const GeoPoint before = Find(ov, "A")->position;

  const PixelPoint at = PixelOf(proj, before);
  ASSERT_TRUE(ov.OnMouseDown(At(at)));
  ov.OnMouseMove(At(at.x + 40, at.y + 40));
  ASSERT_NE(before.lat, Find(ov, "A")->position.lat);

  EXPECT_TRUE(ov.OnKeyDown(KeyOf(fv::Key::kEscape)));
  EXPECT_EQ(before.lat, Find(ov, "A")->position.lat);
  EXPECT_EQ(before.lon, Find(ov, "A")->position.lon);
  // The undo entry the first move pushed is SPENT putting it back, so a
  // cancelled drag leaves no trace in the history at all...
  EXPECT_FALSE(ov.edit().CanUndo());
  // ...nor in the dirty flag, which was clean before the gesture.
  EXPECT_FALSE(ov.is_dirty());
}

TEST(RouteEdit, ReleasingEditFocusCancelsAGestureInFlight) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  const MapProjection proj = KiawahProj();
  DrawOnce(ov, proj);
  const GeoPoint before = Find(ov, "A")->position;

  const PixelPoint at = PixelOf(proj, before);
  ASSERT_TRUE(ov.OnMouseDown(At(at)));
  ov.OnMouseMove(At(at.x + 40, at.y + 40));
  ov.edit().SetAdding(true);

  ov.ReleaseEditFocus();
  EXPECT_EQ(before.lat, Find(ov, "A")->position.lat);
  EXPECT_FALSE(ov.edit().dragging());
  // Or the next entry into the mode starts armed for a click the user made a
  // minute ago.
  EXPECT_FALSE(ov.edit().adding());
  // And nothing routes to an overlay that is not being edited.
  EXPECT_FALSE(ov.OnMouseDown(At(at)));
  EXPECT_FALSE(ov.OnKeyDown(KeyOf('A')));
}

// --- adding, deleting, history ---------------------------------------------

TEST(RouteEdit, AddIsTwoGesturesAndInsertsAfterTheSelection) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  const MapProjection proj = KiawahProj();
  DrawOnce(ov, proj);
  ov.SetSelected("A");

  // The key ARMS; a click on open map before that is not an insert.
  ASSERT_FALSE(ov.OnMouseDown(At(700, 500)));
  EXPECT_EQ(2u, ov.waypoints().size());

  EXPECT_TRUE(ov.OnKeyDown(KeyOf('A')));
  EXPECT_TRUE(ov.edit().adding());
  EXPECT_TRUE(ov.OnMouseDown(At(700, 500)));

  ASSERT_EQ(3u, ov.waypoints().size());
  EXPECT_EQ("A", ov.waypoints()[0].label);
  EXPECT_EQ("WP3", ov.waypoints()[1].label);  // after the selected one
  EXPECT_EQ("B", ov.waypoints()[2].label);
  // Selected, so the NEXT add lands after it and a route is built by clicking
  // along it.
  EXPECT_EQ("WP3", ov.selected());
  // The mode is spent by the click, not sticky.
  EXPECT_FALSE(ov.edit().adding());
}

TEST(RouteEdit, LabelsStayUniqueBecauseSelectionIsByLabel) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints({{"WP1", {32.60, -80.10}}, {"WP2", {32.59, -80.11}}});
  // Two waypoints called WP2 would delete as a pair, so the obvious "size + 1"
  // is not enough on its own.
  EXPECT_EQ("WP3", ov.edit().NewLabel());
  ov.SetWaypoints({{"WP1", {32.60, -80.10}}, {"WP3", {32.59, -80.11}}});
  EXPECT_EQ("WP4", ov.edit().NewLabel());
}

TEST(RouteEdit, DeleteRemovesTheSelectionAndUndoBringsItBack) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  ov.SetSelected("A");

  EXPECT_TRUE(ov.OnKeyDown(KeyOf(fv::Key::kDelete)));
  ASSERT_EQ(1u, ov.waypoints().size());
  EXPECT_EQ("B", ov.waypoints()[0].label);
  // The selection went with it: a selected label that is not in the document
  // is a handle onto nothing.
  EXPECT_TRUE(ov.selected().empty());

  ASSERT_TRUE(ov.CanUndo());
  ov.Undo();
  EXPECT_EQ(2u, ov.waypoints().size());
  ASSERT_TRUE(ov.CanRedo());
  ov.Redo();
  EXPECT_EQ(1u, ov.waypoints().size());
}

TEST(RouteEdit, DeletingSomethingThatIsNotThereChangesNothing) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  ov.set_dirty(false);
  EXPECT_FALSE(ov.edit().Delete("nope"));
  EXPECT_EQ(2u, ov.waypoints().size());
  // Above all it does not push an undo entry for a no-op.
  EXPECT_FALSE(ov.CanUndo());
  EXPECT_FALSE(ov.is_dirty());
}

TEST(RouteEdit, ANewEditThrowsAwayTheRedoBranch) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  ov.SetSelected("A");
  ASSERT_TRUE(ov.edit().Delete("A"));
  ov.Undo();
  ASSERT_TRUE(ov.CanRedo());

  ASSERT_TRUE(ov.edit().Delete("B"));
  EXPECT_FALSE(ov.CanRedo());
}

TEST(RouteEdit, OpeningADocumentClearsTheHistory) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  ASSERT_TRUE(ov.edit().Delete("A"));
  ASSERT_TRUE(ov.CanUndo());
  // An undo that reached back past a File > Open would restore waypoints into
  // a document they were never in.
  ASSERT_TRUE(ov.FileOpen(FV_ROUTE_FIXTURE_FILE).ok());
  EXPECT_FALSE(ov.CanUndo());
  EXPECT_FALSE(ov.CanRedo());
}

// --- the keys --------------------------------------------------------------

TEST(RouteEdit, EscapeUnwindsOneThingAtATime) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  ov.SetSelected("A");
  ov.edit().SetAdding(true);

  // The mode first: it is the one that changes what a click will do.
  EXPECT_TRUE(ov.OnKeyDown(KeyOf(fv::Key::kEscape)));
  EXPECT_FALSE(ov.edit().adding());
  EXPECT_EQ("A", ov.selected());

  // Then the selection.
  EXPECT_TRUE(ov.OnKeyDown(KeyOf(fv::Key::kEscape)));
  EXPECT_TRUE(ov.selected().empty());

  // With nothing left to cancel it DECLINES, so the app can quit on it.
  EXPECT_FALSE(ov.OnKeyDown(KeyOf(fv::Key::kEscape)));
}

TEST(RouteEdit, GCyclesTheLegGeometryAndComesBackRound) {
  RouteOverlay ov("Route1");
  EXPECT_EQ(fv::LineKind::kGreatCircle, ov.leg_kind());
  EXPECT_TRUE(ov.OnKeyDown(KeyOf('G')));
  EXPECT_EQ(fv::LineKind::kRhumb, ov.leg_kind());
  EXPECT_TRUE(ov.OnKeyDown(KeyOf('G')));
  EXPECT_EQ(fv::LineKind::kSimple, ov.leg_kind());
  EXPECT_TRUE(ov.OnKeyDown(KeyOf('G')));
  EXPECT_EQ(fv::LineKind::kGreatCircle, ov.leg_kind());
}

TEST(RouteEdit, CtrlZUndoesAndCtrlShiftZRedoes) {
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  ASSERT_TRUE(ov.edit().Delete("A"));

  fv::KeyEvent undo = KeyOf('Z');
  undo.ctrl = true;
  EXPECT_TRUE(ov.OnKeyDown(undo));
  EXPECT_EQ(2u, ov.waypoints().size());

  fv::KeyEvent redo = undo;
  redo.shift = true;
  EXPECT_TRUE(ov.OnKeyDown(redo));
  EXPECT_EQ(1u, ov.waypoints().size());
}

// --- the snap: the reason this session exists ------------------------------

// A point set with one marker on it, in a stack with the route. This is the
// whole apparatus a snap needs -- `SnapCandidates` walks the MANAGER, so a
// route that is not in one snaps to nothing, which is correct and is its own
// test below.
struct SnapFixture {
  fv::OverlayManager manager;
  std::shared_ptr<fv::PointOverlay> points{new fv::PointOverlay("Points")};
  std::shared_ptr<RouteOverlay> route{new RouteOverlay("Route1")};
  MapProjection proj = KiawahProj();

  // Somewhere on Kiawah with a coordinate nobody could hit by un-projecting a
  // pixel: ten decimals is far finer than a pixel is wide at 1:50,000.
  static GeoPoint MarkerPosition() { return {32.6009876543, -80.1102468013}; }

  SnapFixture() {
    fv::MapPoint p;
    p.name = "Ruddy Turnstone";
    p.position = MarkerPosition();
    points->AddPoint(p);
    // Points below, route above: the route is what the user is working on.
    EXPECT_TRUE(manager.Add(points).ok());
    EXPECT_TRUE(manager.Add(route).ok());
    route->SetManager(&manager);
    route->SetShowLabels(false);
    route->SetWaypoints(TwoWaypoints());
    Draw();
  }

  void Draw() {
    fv::CpuCanvas canvas(proj.SurfaceSize().width, proj.SurfaceSize().height);
    EXPECT_TRUE(manager.DrawAll(proj, canvas).ok());
  }
};

TEST(RouteEditSnap, ADraggedWaypointTakesThePointsExactCoordinate) {
  SnapFixture f;
  const PixelPoint marker = PixelOf(f.proj, SnapFixture::MarkerPosition());
  const PixelPoint from = PixelOf(f.proj, f.route->waypoints()[0].position);

  ASSERT_TRUE(f.route->OnMouseDown(At(from)));
  f.route->OnMouseMove(At(marker));
  f.route->OnMouseUp(At(marker));

  // Not "near the marker" -- AT it. The whole worth of a snap is that the
  // coordinate is the surveyed one and not the un-projection of a pixel, and a
  // test that allowed a metre of slack would pass with no snap at all.
  const GeoPoint got = Find(*f.route, "A")->position;
  EXPECT_DOUBLE_EQ(SnapFixture::MarkerPosition().lat, got.lat);
  EXPECT_DOUBLE_EQ(SnapFixture::MarkerPosition().lon, got.lon);
}

TEST(RouteEditSnap, AResolvedPixelSaysWhatItSnappedTo) {
  SnapFixture f;
  const PixelPoint marker = PixelOf(f.proj, SnapFixture::MarkerPosition());
  const fv::EditPosition p = f.route->edit().ResolvePixel(marker);
  ASSERT_TRUE(p.valid);
  EXPECT_TRUE(p.snapped);
  // A shell puts this on a button ("Use Ruddy Turnstone") or in a status bar.
  // A snap that happened silently is indistinguishable from a drag that missed.
  EXPECT_NE(std::string::npos, p.snapped_to.find("Ruddy Turnstone"));
}

TEST(RouteEditSnap, AWaypointDoesNotSnapToTheWaypointBeingDragged) {
  // THE TEST THIS WHOLE FEATURE TURNS ON. RouteOverlay::SnapToPoint answers
  // out of what it DREW, and the waypoint under the cursor is drawn there --
  // distance ~0, first in a nearest-first list, every single frame. Without
  // the overlay excluding itself the marker snaps to itself and never moves.
  SnapFixture f;
  const GeoPoint before = f.route->waypoints()[0].position;
  const PixelPoint from = PixelOf(f.proj, before);

  ASSERT_TRUE(f.route->OnMouseDown(At(from)));
  for (int step = 10; step <= 50; step += 10) {
    f.route->OnMouseMove(At(from.x + step, from.y));
    f.Draw();  // redraw between moves, exactly as a shell does
  }
  f.route->OnMouseUp(At(from.x + 50, from.y));

  EXPECT_NE(before.lon, Find(*f.route, "A")->position.lon);
}

TEST(RouteEditSnap, ANeighbouringWaypointOfTheSameRouteIsNotASnapTarget) {
  // The same rule seen from the other side, and it is right on its own terms:
  // two waypoints of ONE route collapsing onto each other is not an edit
  // anybody asked for. Snapping to ANOTHER overlay's answer is untouched --
  // that is the test above.
  SnapFixture f;
  const GeoPoint b = f.route->waypoints()[1].position;
  const PixelPoint onto = PixelOf(f.proj, b);
  const PixelPoint from = PixelOf(f.proj, f.route->waypoints()[0].position);

  ASSERT_TRUE(f.route->OnMouseDown(At(from)));
  f.route->OnMouseMove(At(onto));
  f.route->OnMouseUp(At(onto));

  const GeoPoint got = Find(*f.route, "A")->position;
  EXPECT_NE(b.lat, got.lat);  // the un-projected pixel, not B's coordinate
}

TEST(RouteEditSnap, AnAddedWaypointSnapsToo) {
  // Placing and moving are the same act, so they go through the same function.
  SnapFixture f;
  const PixelPoint marker = PixelOf(f.proj, SnapFixture::MarkerPosition());
  ASSERT_TRUE(f.route->OnKeyDown(KeyOf('A')));
  ASSERT_TRUE(f.route->OnMouseDown(At(marker)));

  const RouteWaypoint* added = Find(*f.route, f.route->selected());
  ASSERT_NE(nullptr, added);
  EXPECT_DOUBLE_EQ(SnapFixture::MarkerPosition().lat, added->position.lat);
}

TEST(RouteEditSnap, ZeroToleranceSwitchesSnappingOff) {
  SnapFixture f;
  f.route->edit().SetSnapTolerancePx(0.0);
  const PixelPoint marker = PixelOf(f.proj, SnapFixture::MarkerPosition());
  const fv::EditPosition p = f.route->edit().ResolvePixel(marker);
  ASSERT_TRUE(p.valid);
  EXPECT_FALSE(p.snapped);
}

TEST(RouteEditSnap, WithNoManagerTheDragStillWorksUnsnapped) {
  // A plain script, or a test: the gesture works, it is just uncaptured and
  // unsnapped. Legal, and route.py has always treated it so.
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  const MapProjection proj = KiawahProj();
  DrawOnce(ov, proj);
  const GeoPoint before = Find(ov, "A")->position;

  const PixelPoint at = PixelOf(proj, before);
  ASSERT_TRUE(ov.OnMouseDown(At(at)));
  ov.OnMouseMove(At(at.x + 50, at.y + 50));
  ov.OnMouseUp(At(at.x + 50, at.y + 50));
  EXPECT_NE(before.lat, Find(ov, "A")->position.lat);
}

TEST(RouteEditSnap, AnOverlayThatHasNeverDrawnResolvesNothing) {
  // Same rule the hit test follows, and for the same reason: a coordinate
  // cannot come from a pixel on a frame that was never rendered.
  RouteOverlay ov("Route1");
  ov.SetWaypoints(TwoWaypoints());
  EXPECT_FALSE(ov.has_projection());
  EXPECT_FALSE(ov.edit().ResolvePixel(PixelPoint{100, 100}).valid);
}

}  // namespace
