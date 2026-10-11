// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Geometry editing on a feature overlay: draw, vertex edits, holes, moves,
/// selection, snapping and the undo stack.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "fvkit/app/undo.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/feature/layer.h"
#include "fvkit/overlay/feature_edit.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/overlay/manager.h"

namespace {

using fv::Feature;
using fv::FeatureOverlay;
using fv::FeatureTool;
using fv::GeometryType;
using fv::GeoPoint;
using fv::PixelPoint;
using fv::VertexRef;

fv::MapProjection View() {
  fv::MapProjection p;
  p.SetSurfaceSize(400, 400);
  p.SetCenter({32.80, -79.90});
  p.SetScale(50000.0);
  return p;
}

PixelPoint Pixel(const fv::MapProjection& proj, const GeoPoint& g) {
  double x = 0, y = 0;
  EXPECT_TRUE(proj.GeoToSurface(g, &x, &y).ok());
  return PixelPoint{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
}

GeoPoint Geo(const fv::MapProjection& proj, int x, int y) {
  GeoPoint g;
  EXPECT_TRUE(proj.SurfaceToGeo(x, y, &g).ok());
  return g;
}

void Draw(FeatureOverlay& o, const fv::MapProjection& proj) {
  fv::CpuCanvas canvas(400, 400);
  canvas.Clear({255, 255, 255, 255});
  ASSERT_TRUE(o.OnDraw(proj, canvas).ok());
}

fv::MouseEvent At(int x, int y, bool shift = false) { return fv::MouseEvent{x, y, 0, shift, false}; }

void Click(FeatureOverlay& o, int x, int y, bool shift = false) {
  o.edit().OnMouseDown(At(x, y, shift));
  o.edit().OnMouseUp(At(x, y, shift));
}

void Drag(FeatureOverlay& o, PixelPoint from, PixelPoint to, bool shift = false) {
  o.edit().OnMouseDown(At(from.x, from.y, shift));
  o.edit().OnMouseMove(At((from.x + to.x) / 2, (from.y + to.y) / 2, shift));
  o.edit().OnMouseMove(At(to.x, to.y, shift));
  o.edit().OnMouseUp(At(to.x, to.y, shift));
}

bool SamePosition(const fv::Position& a, const fv::Position& b) { return a.lat == b.lat && a.lon == b.lon; }

fv::KeyEvent Key(int key) {
  fv::KeyEvent e;
  e.key = key;
  return e;
}

struct Fixture {
  explicit Fixture(GeometryType type = GeometryType::kGeometry) {
    fv::FeatureSchema schema;
    schema.geometry_type = type;
    layer = std::make_shared<fv::MemoryFeatureLayer>("Edits", schema);
    overlay = std::make_shared<FeatureOverlay>();
    overlay->SetLayer(layer);
    overlay->SetManager(&manager);
    EXPECT_TRUE(manager.Add(overlay).ok());
    overlay->edit().EnterEditFocus();
    Draw(*overlay, proj);
  }
  Feature Get(int64_t id) const {
    Feature f;
    EXPECT_TRUE(layer->Get(id, &f).ok());
    return f;
  }
  /// Clicks out a square area with corners (100,100)–(300,300) and returns its id.
  int64_t DrawSquare() {
    overlay->edit().SetTool(FeatureTool::kDrawArea);
    Click(*overlay, 100, 100);
    Click(*overlay, 300, 100);
    Click(*overlay, 300, 300);
    Click(*overlay, 100, 300);
    overlay->edit().OnKeyDown(Key(fv::Key::kReturn));
    overlay->edit().SetTool(FeatureTool::kSelect);
    return overlay->selected();
  }

  fv::MapProjection proj = View();
  fv::OverlayManager manager;
  std::shared_ptr<fv::MemoryFeatureLayer> layer;
  std::shared_ptr<FeatureOverlay> overlay;
};

TEST(FeatureGeometryEdit, DrawsAnAreaAndALine) {
  Fixture fx;
  const int64_t area = fx.DrawSquare();
  ASSERT_NE(area, 0);
  const Feature a = fx.Get(area);
  ASSERT_EQ(a.geometry.type, GeometryType::kPolygon);
  ASSERT_EQ(a.geometry.paths.size(), 1u);
  EXPECT_EQ(a.geometry.paths[0].size(), 5u);  // closed
  EXPECT_TRUE(fx.overlay->is_dirty());

  fx.overlay->edit().SetTool(FeatureTool::kDrawLine);
  Click(*fx.overlay, 20, 20);
  Click(*fx.overlay, 60, 40);
  Click(*fx.overlay, 90, 20);
  // Backspace takes back the last vertex placed.
  EXPECT_TRUE(fx.overlay->edit().OnKeyDown(Key(fv::Key::kBackspace)));
  EXPECT_EQ(fx.overlay->edit().sketch().size(), 2u);
  EXPECT_TRUE(fx.overlay->edit().OnKeyDown(Key(fv::Key::kReturn)));
  const Feature line = fx.Get(fx.overlay->selected());
  EXPECT_EQ(line.geometry.type, GeometryType::kLineString);
  EXPECT_EQ(line.geometry.paths[0].size(), 2u);
  // The tool stays armed for the next line.
  EXPECT_EQ(fx.overlay->edit().tool(), FeatureTool::kDrawLine);
  EXPECT_EQ(fx.layer->Count(), 2u);
}

TEST(FeatureGeometryEdit, ClickOnFirstVertexClosesAnArea) {
  Fixture fx;
  fx.overlay->edit().SetTool(FeatureTool::kDrawArea);
  Click(*fx.overlay, 100, 100);
  Click(*fx.overlay, 200, 100);
  Click(*fx.overlay, 150, 200);
  Click(*fx.overlay, 102, 101);
  EXPECT_FALSE(fx.overlay->edit().sketching());
  EXPECT_EQ(fx.Get(fx.overlay->selected()).geometry.paths[0].size(), 4u);
}

/// The two presses and releases of a double-click at (x, y).
void DoubleClick(FeatureOverlay& o, int x, int y) {
  Click(o, x, y);
  fv::MouseEvent second = At(x, y);
  second.clicks = 2;
  o.edit().OnMouseDown(second);
  o.edit().OnMouseUp(second);
}

TEST(FeatureGeometryEdit, DoubleClickPlacesTheLastVertexAndFinishes) {
  Fixture fx;
  fx.overlay->edit().SetTool(FeatureTool::kDrawLine);
  Click(*fx.overlay, 20, 20);
  DoubleClick(*fx.overlay, 90, 40);
  EXPECT_FALSE(fx.overlay->edit().sketching());
  const Feature line = fx.Get(fx.overlay->selected());
  EXPECT_EQ(line.geometry.type, GeometryType::kLineString);
  EXPECT_EQ(line.geometry.paths[0].size(), 2u);

  fx.overlay->edit().SetTool(FeatureTool::kDrawArea);
  Click(*fx.overlay, 100, 100);
  Click(*fx.overlay, 200, 100);
  DoubleClick(*fx.overlay, 150, 200);
  EXPECT_FALSE(fx.overlay->edit().sketching());
  EXPECT_EQ(fx.Get(fx.overlay->selected()).geometry.paths[0].size(), 4u);
  EXPECT_EQ(fx.layer->Count(), 2u);
  EXPECT_FALSE(fx.overlay->edit().TakeOpenRequest());
}

TEST(FeatureGeometryEdit, DoubleClickOnAFeatureRequestsItsForm) {
  Fixture fx;
  const int64_t area = fx.DrawSquare();
  fx.overlay->SetSelection({});
  DoubleClick(*fx.overlay, 200, 200);
  EXPECT_TRUE(fx.overlay->edit().TakeOpenRequest());
  EXPECT_FALSE(fx.overlay->edit().TakeOpenRequest());
  EXPECT_EQ(fx.overlay->selection(), std::vector<int64_t>{area});
  // Nothing moved: the geometry is the square as drawn.
  EXPECT_EQ(fx.Get(area).geometry.paths[0].size(), 5u);
  EXPECT_EQ(fx.layer->Count(), 1u);
  // A double-click on empty map asks for nothing.
  DoubleClick(*fx.overlay, 500, 20);
  EXPECT_FALSE(fx.overlay->edit().TakeOpenRequest());
}

TEST(FeatureGeometryEdit, HintSaysWhatAPressWouldDo) {
  Fixture fx;
  auto& edit = fx.overlay->edit();
  const int64_t area = fx.DrawSquare();
  const std::string title = fx.overlay->FeatureTitle(fx.Get(area));
  fx.overlay->SetSelection({});
  EXPECT_EQ(edit.HintAt({200, 200}), "Select area \u201C" + title + "\u201D");
  EXPECT_EQ(edit.HintAt({30, 380}), "");  // empty map pans
  fx.overlay->SetSelected(area);
  EXPECT_EQ(edit.HintAt({300, 300}), "Move vertex");
  EXPECT_EQ(edit.HintAt({200, 100}), "Insert vertex");
  EXPECT_EQ(edit.HintAt({200, 200}).rfind("Move area", 0), 0u);

  edit.SetTool(FeatureTool::kAddPoint);
  EXPECT_EQ(edit.HintAt({50, 50}), "Add point");
  edit.SetTool(FeatureTool::kDrawArea);
  EXPECT_EQ(edit.HintAt({20, 20}), "Start area");
  Click(*fx.overlay, 20, 20);
  Click(*fx.overlay, 80, 20);
  Click(*fx.overlay, 80, 80);
  EXPECT_EQ(edit.HintAt({21, 21}), "Close area");
  EXPECT_EQ(edit.HintAt({50, 90}).rfind("Add vertex", 0), 0u);
  edit.SetTool(FeatureTool::kDrawLine);
  EXPECT_EQ(edit.HintAt({20, 20}), "Start line");

  // The pick hint names the feature for a viewer that is not editing.
  std::vector<fv::app::HitItem> hits;
  fx.overlay->HitTestPoint(fx.proj, {200, 200}, 4, hits);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].hint.tool_tip, title);
  EXPECT_EQ(hits[0].hint.status, fx.overlay->Name() + ": " + title);
  fx.overlay->edit().ReleaseEditFocus();
  EXPECT_EQ(edit.HintAt({200, 200}), "");
}

TEST(FeatureGeometryEdit, ConformsToAMultiLayer) {
  Fixture fx(GeometryType::kMultiPolygon);
  EXPECT_FALSE(fx.overlay->edit().ToolAvailable(FeatureTool::kDrawLine));
  EXPECT_TRUE(fx.overlay->edit().ToolAvailable(FeatureTool::kDrawArea));
  const int64_t id = fx.DrawSquare();
  const Feature f = fx.Get(id);
  ASSERT_EQ(f.geometry.type, GeometryType::kMultiPolygon);
  ASSERT_EQ(f.geometry.parts.size(), 1u);
  EXPECT_EQ(f.geometry.parts[0].paths[0].size(), 5u);
}

TEST(FeatureGeometryEdit, HoleDragAndUndo) {
  Fixture fx;
  const int64_t id = fx.DrawSquare();
  const Feature drawn = fx.Get(id);

  ASSERT_TRUE(fx.overlay->edit().ToolAvailable(FeatureTool::kAddHole));
  fx.overlay->edit().SetTool(FeatureTool::kAddHole);
  Click(*fx.overlay, 150, 150);
  Click(*fx.overlay, 250, 150);
  Click(*fx.overlay, 200, 250);
  fx.overlay->edit().OnKeyDown(Key(fv::Key::kReturn));
  EXPECT_EQ(fx.overlay->edit().tool(), FeatureTool::kSelect);
  const Feature holed = fx.Get(id);
  ASSERT_EQ(holed.geometry.paths.size(), 2u);
  EXPECT_EQ(holed.geometry.paths[1].size(), 4u);

  // Drag the exterior's first corner; the closing vertex follows it.
  Drag(*fx.overlay, PixelPoint{100, 100}, PixelPoint{80, 70});
  const Feature dragged = fx.Get(id);
  const GeoPoint want = Geo(fx.proj, 80, 70);
  EXPECT_NEAR(dragged.geometry.paths[0][0].lat, want.lat, 1e-9);
  EXPECT_NEAR(dragged.geometry.paths[0][0].lon, want.lon, 1e-9);
  EXPECT_TRUE(SamePosition(dragged.geometry.paths[0].front(), dragged.geometry.paths[0].back()));

  fv::app::UndoStack& stack = fx.overlay->edit().undo_stack();
  EXPECT_EQ(stack.UndoLabel(), "Move Vertex");
  ASSERT_TRUE(fx.overlay->edit().Undo().ok());
  EXPECT_EQ(fx.Get(id).geometry, holed.geometry);
  EXPECT_EQ(stack.UndoLabel(), "Add Hole");
  ASSERT_TRUE(fx.overlay->edit().Undo().ok());
  EXPECT_EQ(fx.Get(id).geometry, drawn.geometry);
  ASSERT_TRUE(fx.overlay->edit().Undo().ok());
  EXPECT_EQ(fx.layer->Count(), 0u);
  EXPECT_EQ(fx.overlay->selected(), 0);

  // Redo brings the area back under its own id.
  ASSERT_TRUE(fx.overlay->edit().Redo().ok());
  ASSERT_TRUE(fx.overlay->edit().Redo().ok());
  ASSERT_TRUE(fx.overlay->edit().Redo().ok());
  EXPECT_EQ(fx.Get(id).geometry, dragged.geometry);
}

TEST(FeatureGeometryEdit, HoleOutsideTheAreaIsRefused) {
  Fixture fx;
  const int64_t id = fx.DrawSquare();
  const std::vector<GeoPoint> outside = {Geo(fx.proj, 10, 10), Geo(fx.proj, 50, 10), Geo(fx.proj, 30, 50)};
  EXPECT_FALSE(fx.overlay->edit().AddHole(id, outside));
  EXPECT_EQ(fx.Get(id).geometry.paths.size(), 1u);
}

TEST(FeatureGeometryEdit, SegmentPressInsertsAndDeleteRemovesVertex) {
  Fixture fx;
  const int64_t id = fx.DrawSquare();
  fx.overlay->edit().Select(id);
  // Press on the top edge, drag the new vertex up.
  Drag(*fx.overlay, PixelPoint{200, 100}, PixelPoint{200, 60});
  Feature f = fx.Get(id);
  ASSERT_EQ(f.geometry.paths[0].size(), 6u);
  EXPECT_NEAR(f.geometry.paths[0][1].lat, Geo(fx.proj, 200, 60).lat, 1e-9);
  EXPECT_EQ(fx.overlay->edit().undo_stack().UndoLabel(), "Insert Vertex");

  // The inserted vertex is active; Delete removes it.
  ASSERT_TRUE(fx.overlay->edit().active_vertex().valid());
  EXPECT_TRUE(fx.overlay->edit().OnKeyDown(Key(fv::Key::kDelete)));
  EXPECT_EQ(fx.Get(id).geometry.paths[0].size(), 5u);

  // A ring keeps at least three vertices.
  EXPECT_TRUE(fx.overlay->edit().DeleteVertex(VertexRef{id, -1, 0, 0}));
  EXPECT_FALSE(fx.overlay->edit().DeleteVertex(VertexRef{id, -1, 0, 0}));
  f = fx.Get(id);
  EXPECT_EQ(f.geometry.paths[0].size(), 4u);
  EXPECT_TRUE(SamePosition(f.geometry.paths[0].front(), f.geometry.paths[0].back()));
}

TEST(FeatureGeometryEdit, RemoveHole) {
  Fixture fx;
  const int64_t id = fx.DrawSquare();
  ASSERT_TRUE(fx.overlay->edit().AddHole(
      id, {Geo(fx.proj, 150, 150), Geo(fx.proj, 250, 150), Geo(fx.proj, 200, 250)}));
  EXPECT_FALSE(fx.overlay->edit().RemoveHole(VertexRef{id, -1, 0, 1}));
  EXPECT_TRUE(fx.overlay->edit().RemoveHole(VertexRef{id, -1, 1, 1}));
  EXPECT_EQ(fx.Get(id).geometry.paths.size(), 1u);
}

TEST(FeatureGeometryEdit, MoveDragsTheWholeSelection) {
  Fixture fx;
  const int64_t id = fx.DrawSquare();
  const Feature before = fx.Get(id);
  fx.overlay->edit().ClearSelection();
  // A press inside the area selects it and drags it.
  Drag(*fx.overlay, PixelPoint{200, 200}, PixelPoint{220, 230});
  const Feature after = fx.Get(id);
  const GeoPoint a = Geo(fx.proj, 200, 200), b = Geo(fx.proj, 220, 230);
  EXPECT_NEAR(after.geometry.paths[0][2].lat - before.geometry.paths[0][2].lat, b.lat - a.lat, 1e-9);
  EXPECT_NEAR(after.geometry.paths[0][2].lon - before.geometry.paths[0][2].lon, b.lon - a.lon, 1e-9);
  EXPECT_EQ(fx.overlay->edit().undo_stack().UndoLabel(), "Move Feature");

  // A click without movement selects and leaves no entry.
  const size_t entries = fx.overlay->edit().undo_stack().undo_count();
  Click(*fx.overlay, 210, 230);
  EXPECT_EQ(fx.overlay->edit().undo_stack().undo_count(), entries);
}

TEST(FeatureGeometryEdit, EscapeCancelsADragWithoutTrace) {
  Fixture fx;
  const int64_t id = fx.DrawSquare();
  fx.overlay->set_dirty(false);
  const Feature before = fx.Get(id);
  const size_t entries = fx.overlay->edit().undo_stack().undo_count();
  fx.overlay->edit().OnMouseDown(At(100, 100));
  fx.overlay->edit().OnMouseMove(At(60, 60));
  EXPECT_NE(fx.Get(id).geometry, before.geometry);
  EXPECT_TRUE(fx.overlay->edit().OnKeyDown(Key(fv::Key::kEscape)));
  EXPECT_EQ(fx.Get(id).geometry, before.geometry);
  EXPECT_EQ(fx.overlay->edit().undo_stack().undo_count(), entries);
  EXPECT_FALSE(fx.overlay->is_dirty());
  EXPECT_EQ(fx.manager.mouse_capture(), nullptr);
}

TEST(FeatureGeometryEdit, ShiftClickAndBoxSelect) {
  Fixture fx;
  fx.overlay->edit().SetTool(FeatureTool::kAddPoint);
  Click(*fx.overlay, 50, 50);
  Click(*fx.overlay, 100, 50);
  Click(*fx.overlay, 350, 350);
  fx.overlay->edit().SetTool(FeatureTool::kSelect);
  ASSERT_EQ(fx.layer->Count(), 3u);
  fx.overlay->edit().ClearSelection();

  Click(*fx.overlay, 50, 50);
  Click(*fx.overlay, 100, 50, true);
  EXPECT_EQ(fx.overlay->selection(), (std::vector<int64_t>{1, 2}));
  Click(*fx.overlay, 50, 50, true);  // shift-click toggles
  EXPECT_EQ(fx.overlay->selection(), (std::vector<int64_t>{2}));

  // A shift-drag on empty map boxes in what it meets.
  fx.overlay->edit().ClearSelection();
  Drag(*fx.overlay, PixelPoint{30, 30}, PixelPoint{120, 80}, true);
  EXPECT_EQ(fx.overlay->selection(), (std::vector<int64_t>{1, 2}));
  // A plain press on empty map is declined, so the shell pans.
  EXPECT_FALSE(fx.overlay->edit().OnMouseDown(At(200, 300)));
  // Escape clears the selection, then has nothing left to do.
  EXPECT_TRUE(fx.overlay->edit().OnKeyDown(Key(fv::Key::kEscape)));
  EXPECT_TRUE(fx.overlay->selection().empty());
  EXPECT_FALSE(fx.overlay->edit().OnKeyDown(Key(fv::Key::kEscape)));

  fx.overlay->SetSelection({1, 3});
  EXPECT_TRUE(fx.overlay->edit().OnKeyDown(Key(fv::Key::kDelete)));
  EXPECT_EQ(fx.layer->Count(), 1u);
  EXPECT_EQ(fx.overlay->edit().undo_stack().UndoLabel(), "Delete Features");
  ASSERT_TRUE(fx.overlay->edit().Undo().ok());
  EXPECT_EQ(fx.layer->Count(), 3u);
  EXPECT_EQ(fx.overlay->selection(), (std::vector<int64_t>{1, 3}));
}

TEST(FeatureGeometryEdit, SnapsToAVertexOfAnotherLayerAndNotToTheDraggedOne) {
  Fixture fx;
  auto other_layer = std::make_shared<fv::MemoryFeatureLayer>("Survey", fv::FeatureSchema{});
  Feature mark;
  mark.geometry = fv::Geometry::Point(Geo(fx.proj, 205, 105));
  ASSERT_TRUE(other_layer->Insert(&mark).ok());
  auto other = std::make_shared<FeatureOverlay>();
  other->SetLayer(other_layer);
  ASSERT_TRUE(fx.manager.Add(other).ok());
  Draw(*other, fx.proj);

  // A sketch vertex placed near the mark lands on it exactly.
  fx.overlay->edit().SetTool(FeatureTool::kDrawLine);
  Click(*fx.overlay, 202, 102);
  ASSERT_EQ(fx.overlay->edit().sketch().size(), 1u);
  EXPECT_EQ(fx.overlay->edit().sketch()[0].lat, mark.geometry.paths[0][0].lat);
  EXPECT_EQ(fx.overlay->edit().sketch()[0].lon, mark.geometry.paths[0][0].lon);
  fx.overlay->edit().CancelSketch();
  fx.overlay->edit().SetTool(FeatureTool::kSelect);

  // A dragged vertex moves rather than snapping back to where it was.
  const int64_t id = fx.DrawSquare();
  Drag(*fx.overlay, PixelPoint{300, 300}, PixelPoint{310, 290});
  EXPECT_NEAR(fx.Get(id).geometry.paths[0][2].lat, Geo(fx.proj, 310, 290).lat, 1e-9);
}

TEST(FeatureGeometryEdit, WithoutFocusTheOverlayDeclines) {
  Fixture fx;
  fx.overlay->edit().SetTool(FeatureTool::kAddPoint);
  fx.overlay->ReleaseEditFocus();
  EXPECT_EQ(fx.overlay->edit().tool(), FeatureTool::kSelect);
  EXPECT_FALSE(fx.overlay->edit().OnMouseDown(At(50, 50)));
  EXPECT_FALSE(fx.overlay->edit().OnKeyDown(Key(fv::Key::kEscape)));
}

TEST(FeatureGeometryEdit, SharedStackOutlivesTheOverlay) {
  fv::app::UndoStack stack;
  {
    Fixture fx;
    fx.overlay->edit().SetUndoStack(&stack);
    fx.DrawSquare();
    EXPECT_EQ(stack.undo_count(), 1u);
    fx.overlay->edit().SetUndoStack(nullptr);
    EXPECT_EQ(stack.undo_count(), 0u);
    fx.overlay->edit().SetUndoStack(&stack);
    fx.DrawSquare();
  }
  // The document is gone; its entry fails and is dropped.
  EXPECT_FALSE(stack.Undo().ok());
  EXPECT_EQ(stack.undo_count(), 0u);
}

TEST(FeatureGeometryEdit, SelectionHaloCoversEverySelectedFeature) {
  Fixture fx;
  fx.overlay->edit().SetTool(FeatureTool::kAddPoint);
  Click(*fx.overlay, 100, 100);
  Click(*fx.overlay, 300, 300);
  fx.overlay->edit().SetTool(FeatureTool::kSelect);
  fx.overlay->ReleaseEditFocus();  // no handles, only the style's halo
  auto yellow = [&fx](int x, int y) {
    fv::CpuCanvas canvas(400, 400);
    canvas.Clear({255, 255, 255, 255});
    EXPECT_TRUE(fx.overlay->OnDraw(fx.proj, canvas).ok());
    const unsigned char* p = canvas.Buffer().Row(y) + 4 * x;
    return p[0] > 200 && p[1] > 150 && p[2] < 80;
  };
  // A halo pixel just outside the marker.
  const int off = static_cast<int>(fx.overlay->style().symbol.marker_size_px / 2) + 2;
  fx.overlay->SetSelection({1, 2});
  EXPECT_TRUE(yellow(100 + off, 100));
  EXPECT_TRUE(yellow(300 + off, 300));
  fx.overlay->SetSelection({2});
  EXPECT_FALSE(yellow(100 + off, 100));
}

TEST(UndoStack, PushUndoRedoAndDepth) {
  fv::app::UndoStack stack(2);
  int value = 0;
  auto push = [&](int to) {
    const int from = value;
    value = to;
    stack.Push({"Set " + std::to_string(to), &stack,
                [&value, from] { value = from; return fv::Status::Ok(); },
                [&value, to] { value = to; return fv::Status::Ok(); }});
  };
  push(1);
  push(2);
  push(3);
  EXPECT_EQ(stack.undo_count(), 2u);  // the oldest was dropped
  ASSERT_TRUE(stack.Undo().ok());
  EXPECT_EQ(value, 2);
  EXPECT_EQ(stack.RedoLabel(), "Set 3");
  ASSERT_TRUE(stack.Undo().ok());
  EXPECT_EQ(value, 1);
  EXPECT_FALSE(stack.Undo().ok());
  ASSERT_TRUE(stack.Redo().ok());
  EXPECT_EQ(value, 2);
  push(7);  // a new action discards the redo history
  EXPECT_FALSE(stack.CanRedo());
}

}  // namespace
