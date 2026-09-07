// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The point editor's gestures.
//
// What is worth pinning: the DRAG THRESHOLD, which is this editor's one
// departure from the route editor's — a press inside a marker is a click
// however long it wobbles, and the boundary is the marker's own drawn
// half-width, so it moves with `size_px` and the DPI scale. Then the things
// that would be silent if they broke: one undo entry per drag, a cancelled
// drag leaving no history at all, and a drop taking a neighbouring set's
// surveyed coordinate rather than the pixel's.

#include "fvkit/overlay/point_edit.h"

#include <gtest/gtest.h>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/point_overlay.h"

namespace {

using fv::MapPoint;
using fv::PixelPoint;
using fv::PointOverlay;

fv::MapProjection HarbourProj() {
  fv::MapProjection p;
  p.SetSurfaceSize(800, 600);
  p.SetCenter({32.74, -79.89});
  p.SetScale(50000.0);
  return p;
}

MapPoint At(int64_t id, double lat, double lon, double size_px = 20.0) {
  MapPoint p;
  p.id = id;
  p.name = "P" + std::to_string(id);
  p.position = {lat, lon};
  p.size_px = size_px;
  return p;
}

// Draws once, which is what gives the overlay the projection its gestures
// resolve pixels against.
void Draw(PointOverlay& ov, const fv::MapProjection& proj) {
  fv::CpuCanvas canvas(proj.SurfaceSize().width, proj.SurfaceSize().height);
  ASSERT_TRUE(ov.OnDraw(proj, canvas).ok());
}

// Where a point lands on the surface.
PixelPoint Pixel(const fv::MapProjection& proj, const fv::GeoPoint& g) {
  double x = 0, y = 0;
  EXPECT_TRUE(proj.GeoToSurface(g, &x, &y).ok());
  return PixelPoint{(int)std::lround(x), (int)std::lround(y)};
}

TEST(PointEdit, PressSelectsAndDoesNotMove) {
  PointOverlay ov;
  ov.SetPoints({At(1, 32.74, -79.89)});
  const fv::MapProjection proj = HarbourProj();
  Draw(ov, proj);

  const fv::GeoPoint before = ov.Find(1)->position;
  const PixelPoint at = Pixel(proj, before);

  ASSERT_TRUE(ov.edit().OnMouseDown(fv::MouseEvent{at.x, at.y, 0, false, false}));
  EXPECT_EQ(ov.selected(), 1);
  EXPECT_TRUE(ov.edit().drag_pending());
  EXPECT_FALSE(ov.edit().dragging());

  // A wobble of four pixels inside a 20-px marker is still a click.
  ov.edit().OnMouseMove(fv::MouseEvent{at.x + 4, at.y + 2, 0, false, false});
  EXPECT_FALSE(ov.edit().dragging());
  ov.edit().OnMouseUp(fv::MouseEvent{at.x + 4, at.y + 2, 0, false, false});

  EXPECT_DOUBLE_EQ(ov.Find(1)->position.lat, before.lat);
  EXPECT_DOUBLE_EQ(ov.Find(1)->position.lon, before.lon);
  EXPECT_FALSE(ov.edit().CanUndo());
}

TEST(PointEdit, DragBeginsOutsideTheMarkerAndCommitsOnce) {
  PointOverlay ov;
  ov.SetPoints({At(1, 32.74, -79.89)});
  const fv::MapProjection proj = HarbourProj();
  Draw(ov, proj);
  const fv::GeoPoint before = ov.Find(1)->position;
  const PixelPoint at = Pixel(proj, before);

  ov.edit().OnMouseDown(fv::MouseEvent{at.x, at.y, 0, false, false});
  // 40 px out is well past the 10-px half-width.
  ov.edit().OnMouseMove(fv::MouseEvent{at.x + 40, at.y, 0, false, false});
  EXPECT_TRUE(ov.edit().dragging());
  ov.edit().OnMouseMove(fv::MouseEvent{at.x + 60, at.y, 0, false, false});
  ov.edit().OnMouseUp(fv::MouseEvent{at.x + 60, at.y, 0, false, false});

  EXPECT_GT(ov.Find(1)->position.lon, before.lon);  // dragged east
  EXPECT_FALSE(ov.edit().dragging());
  // One entry for the whole drag, not one per move.
  EXPECT_TRUE(ov.edit().CanUndo());
  ov.edit().Undo();
  EXPECT_NEAR(ov.Find(1)->position.lon, before.lon, 1e-12);
  EXPECT_FALSE(ov.edit().CanUndo());
  EXPECT_EQ(ov.selected(), 1);  // the row survived, so the selection did
}

TEST(PointEdit, ThresholdFollowsTheMarkerSize) {
  // The same 12-px move is a click on a big marker and a drag on a small one.
  for (const double size_px : {40.0, 8.0}) {
    PointOverlay ov;
    ov.SetPoints({At(1, 32.74, -79.89, size_px)});
    const fv::MapProjection proj = HarbourProj();
    Draw(ov, proj);
    const PixelPoint at = Pixel(proj, ov.Find(1)->position);
    ov.edit().OnMouseDown(fv::MouseEvent{at.x, at.y, 0, false, false});
    ov.edit().OnMouseMove(fv::MouseEvent{at.x + 12, at.y, 0, false, false});
    EXPECT_EQ(ov.edit().dragging(), size_px < 24.0) << "size_px " << size_px;
    ov.edit().CancelDrag();
  }
}

TEST(PointEdit, CancelledDragLeavesNoHistory) {
  PointOverlay ov;
  ov.SetPoints({At(1, 32.74, -79.89)});
  ov.set_dirty(false);
  const fv::MapProjection proj = HarbourProj();
  Draw(ov, proj);
  const fv::GeoPoint before = ov.Find(1)->position;
  const PixelPoint at = Pixel(proj, before);

  ov.edit().OnMouseDown(fv::MouseEvent{at.x, at.y, 0, false, false});
  ov.edit().OnMouseMove(fv::MouseEvent{at.x + 50, at.y + 30, 0, false, false});
  ASSERT_TRUE(ov.edit().dragging());
  ASSERT_TRUE(ov.edit().OnKeyDown(fv::KeyEvent{fv::Key::kEscape, false, false}));

  EXPECT_NEAR(ov.Find(1)->position.lat, before.lat, 1e-12);
  EXPECT_NEAR(ov.Find(1)->position.lon, before.lon, 1e-12);
  EXPECT_FALSE(ov.edit().CanUndo());
  EXPECT_FALSE(ov.is_dirty());
}

TEST(PointEdit, AddIsArmedThenSpent) {
  PointOverlay ov;
  const fv::MapProjection proj = HarbourProj();
  Draw(ov, proj);

  // Un-armed, a click on empty map is not this editor's.
  EXPECT_FALSE(ov.edit().OnMouseDown(fv::MouseEvent{100, 100, 0, false, false}));
  EXPECT_TRUE(ov.points().empty());

  ov.edit().SetAdding(true);
  EXPECT_TRUE(ov.edit().OnMouseDown(fv::MouseEvent{100, 100, 0, false, false}));
  ASSERT_EQ(ov.points().size(), 1u);
  EXPECT_EQ(ov.selected(), ov.points()[0].id);
  EXPECT_FALSE(ov.edit().adding()) << "one click spends the armed mode";

  ov.edit().Undo();
  EXPECT_TRUE(ov.points().empty());
}

TEST(PointEdit, DeleteAndUpdate) {
  PointOverlay ov;
  ov.SetPoints({At(1, 32.74, -79.89), At(2, 32.75, -79.88)});

  MapPoint edited = *ov.Find(2);
  edited.name = "Renamed";
  edited.category = "harbour";
  ASSERT_TRUE(ov.edit().Update(edited));
  EXPECT_EQ(ov.Find(2)->name, "Renamed");
  EXPECT_EQ(ov.Find(2)->category, "harbour");

  ov.edit().Select(2);
  ASSERT_TRUE(ov.edit().Delete(2));
  EXPECT_EQ(ov.points().size(), 1u);
  EXPECT_EQ(ov.selected(), 0);

  ov.edit().Undo();                      // the delete
  EXPECT_EQ(ov.points().size(), 2u);
  EXPECT_EQ(ov.Find(2)->name, "Renamed");
  ov.edit().Undo();                      // the rename
  EXPECT_EQ(ov.Find(2)->name, "P2");
}

TEST(PointEdit, DropSnapsToAnotherSetAndNeverToItself) {
  fv::OverlayManager mgr;
  auto target = std::make_shared<PointOverlay>("Survey");
  target->SetPoints({At(10, 32.7450, -79.8850)});
  auto edited = std::make_shared<PointOverlay>("Working");
  edited->SetPoints({At(1, 32.7400, -79.8900)});
  edited->SetManager(&mgr);
  ASSERT_TRUE(mgr.Add(target).ok());
  ASSERT_TRUE(mgr.Add(edited).ok());

  const fv::MapProjection proj = HarbourProj();
  Draw(*target, proj);
  Draw(*edited, proj);

  const fv::GeoPoint survey = target->Find(10)->position;
  const PixelPoint from = Pixel(proj, edited->Find(1)->position);
  const PixelPoint onto = Pixel(proj, survey);

  edited->edit().OnMouseDown(fv::MouseEvent{from.x, from.y, 0, false, false});
  edited->edit().OnMouseMove(fv::MouseEvent{onto.x, onto.y, 0, false, false});
  edited->edit().OnMouseUp(fv::MouseEvent{onto.x, onto.y, 0, false, false});

  // The exact surveyed coordinate, not the pixel's centre.
  EXPECT_DOUBLE_EQ(edited->Find(1)->position.lat, survey.lat);
  EXPECT_DOUBLE_EQ(edited->Find(1)->position.lon, survey.lon);

  // And the dragged point never snapped to itself on the way: it moved.
  EXPECT_NE(edited->Find(1)->position.lon, -79.8900);
}

TEST(PointEdit, ReleasingEditFocusCancelsTheGestureInFlight) {
  PointOverlay ov;
  ov.SetPoints({At(1, 32.74, -79.89)});
  ov.set_dirty(false);
  const fv::MapProjection proj = HarbourProj();
  Draw(ov, proj);
  const fv::GeoPoint before = ov.Find(1)->position;
  const PixelPoint at = Pixel(proj, before);

  ov.edit().SetAdding(true);
  ov.edit().OnMouseDown(fv::MouseEvent{at.x, at.y, 0, false, false});  // adds
  ov.edit().OnMouseDown(fv::MouseEvent{at.x, at.y, 0, false, false});  // grabs
  ov.edit().OnMouseMove(fv::MouseEvent{at.x + 50, at.y, 0, false, false});
  ASSERT_TRUE(ov.edit().dragging());

  ov.ReleaseEditFocus();
  EXPECT_FALSE(ov.edit().dragging());
  EXPECT_FALSE(ov.edit().adding());
  // Declines input while it does not hold focus.
  EXPECT_FALSE(ov.edit().OnMouseDown(fv::MouseEvent{at.x, at.y, 0, false, false}));
  ov.EnterEditFocus();
  EXPECT_TRUE(ov.edit().has_edit_focus());
}

TEST(PointEdit, DimmingIsNotADocumentChange) {
  PointOverlay ov;
  ov.SetPoints({At(1, 32.74, -79.89)});
  ov.set_dirty(false);
  EXPECT_FALSE(ov.dimmed());
  ov.SetDimmed(true);
  EXPECT_TRUE(ov.dimmed());
  EXPECT_FALSE(ov.is_dirty());
}

TEST(PointEdit, DimmedDrawLaysLessInk) {
  PointOverlay ov;
  ov.SetPoints({At(1, 32.74, -79.89)});
  const fv::MapProjection proj = HarbourProj();

  auto ink = [&](bool dimmed) {
    ov.SetDimmed(dimmed);
    fv::CpuCanvas canvas(800, 600);
    canvas.Clear(fv::FvColor{255, 255, 255, 255});
    EXPECT_TRUE(ov.OnDraw(proj, canvas).ok());
    long sum = 0;
    for (int y = 0; y < 600; ++y) {
      const unsigned char* row = canvas.Buffer().Row(y);
      for (int x = 0; x < 800 * 4; ++x) sum += 255 - row[x];
    }
    return sum;
  };

  const long normal = ink(false);
  const long dim = ink(true);
  EXPECT_GT(normal, 0);
  EXPECT_LT(dim, normal);
  EXPECT_GT(dim, 0) << "dimmed is faded, not hidden";
}

}  // namespace
