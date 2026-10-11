// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/overlay/feature_edit.h — geometry editing for a feature layer
/// (port/feature-overlay-plan.md §5): add points, draw lines and areas, add
/// and remove holes, move features, drag, insert and delete vertices, select
/// by click, shift-click and shift-drag box, snap to visible vertices.
///
/// Gestures follow `PointEditSession`: capture on press, a drag starts past a
/// small threshold so a click never nudges, Escape backs out one thing at a
/// time, chords go back to the shell. Every edit is one entry on an
/// `app::UndoStack`, the workspace's when the shell gives one.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/app/edit_position.h"
#include "fvkit/app/undo.h"
#include "fvkit/canvas/canvas.h"
#include "fvkit/feature/schema.h"
#include "fvkit/geo.h"
#include "fvkit/overlay/overlay.h"

namespace fv {

class FeatureOverlay;
class MapProjection;

/// What a click on the map does.
enum class FeatureTool {
  kSelect,    ///< select, move features, edit vertices
  kAddPoint,  ///< each click adds a point feature
  kDrawLine,  ///< clicks place vertices; Return finishes
  kDrawArea,  ///< clicks place vertices; Return or a click on the first finishes
  kAddHole,   ///< as kDrawArea, cut from the one selected area
};

/// One vertex of a feature. `part` is -1 for a single geometry, else the
/// index into `Geometry::parts`; `path` is the ring of a polygon (0 is the
/// exterior), 0 otherwise. For a closed ring `index` never names the closing
/// duplicate; editing vertex 0 edits both.
struct VertexRef {
  int64_t feature = 0;
  int part = -1;
  int path = 0;
  int index = 0;

  bool valid() const { return feature != 0; }
  bool operator==(const VertexRef& o) const {
    return feature == o.feature && part == o.part && path == o.path && index == o.index;
  }
  bool operator!=(const VertexRef& o) const { return !(*this == o); }
};

class FeatureEditSession {
 public:
  /// Borrowed; the overlay owns its session.
  explicit FeatureEditSession(FeatureOverlay& overlay);
  ~FeatureEditSession();

  FeatureEditSession(const FeatureEditSession&) = delete;
  FeatureEditSession& operator=(const FeatureEditSession&) = delete;

  // MARK: Tool state

  FeatureTool tool() const { return tool_; }
  /// Changing tool discards an unfinished sketch.
  void SetTool(FeatureTool tool);
  /// Whether the layer's geometry type admits what `tool` makes, and for
  /// kAddHole whether exactly one area is selected.
  bool ToolAvailable(FeatureTool tool) const;

  double pick_tolerance_px() const { return pick_tolerance_px_; }
  void SetPickTolerancePx(double px) { pick_tolerance_px_ = px > 0 ? px : 0; }
  /// 0 switches snapping off.
  double snap_tolerance_px() const { return snap_tolerance_px_; }
  void SetSnapTolerancePx(double px) { snap_tolerance_px_ = px > 0 ? px : 0; }

  // MARK: Edit focus

  /// Starts false: an overlay that is not being edited declines the mouse, so
  /// a feature layer above the one being edited does not take its clicks.
  bool has_edit_focus() const { return has_edit_focus_; }
  void EnterEditFocus() { has_edit_focus_ = true; }
  /// Also abandons any drag or sketch and returns to the select tool.
  void ReleaseEditFocus();

  // MARK: History

  /// Null restores the session's own stack. Not owned; it must outlive its
  /// use, or be replaced first.
  void SetUndoStack(app::UndoStack* stack);
  app::UndoStack& undo_stack() { return stack_ != nullptr ? *stack_ : own_stack_; }
  bool CanUndo() const;
  bool CanRedo() const;
  /// The stack's newest entry, which may belong to another document when the
  /// stack is shared.
  Status Undo();
  Status Redo();
  /// Drops this document's entries; for a newly opened or reloaded layer.
  void ClearHistory();

  // MARK: Selection (held by the overlay)

  /// `add` toggles `id` in the selection; otherwise it becomes the selection.
  void Select(int64_t id, bool add = false);
  void ClearSelection();
  /// Features that meet the surface rectangle, by the last drawn projection.
  std::vector<int64_t> FeaturesInRect(const PixelRect& r) const;

  /// The vertex last pressed, which Delete removes; invalid when none.
  const VertexRef& active_vertex() const { return active_vertex_; }

  // MARK: Edits — each is one undo entry; false when refused

  /// Inserts a feature with `geometry` (conformed to the layer's Multi- type)
  /// and null values, and selects it. Returns its id, or 0.
  int64_t AddFeature(const Geometry& geometry, const std::string& label = "Add Feature");
  bool DeleteFeatures(const std::vector<int64_t>& ids);
  bool DeleteSelected();
  /// Moves every vertex of `ids` by the given degrees.
  bool MoveFeatures(const std::vector<int64_t>& ids, double dlat, double dlon);
  bool MoveVertex(const VertexRef& v, const GeoPoint& to);
  /// Inserts a vertex before `v.index` (1 .. size-1 on a ring or line).
  bool InsertVertex(const VertexRef& v, const GeoPoint& at);
  /// Refused when it would leave a line under 2 vertices or a ring under 3.
  bool DeleteVertex(const VertexRef& v);
  /// Adds `ring` as a hole of the part of `id` whose exterior contains every
  /// vertex of it. Refused for a ring of under 3 vertices or one no exterior holds.
  bool AddHole(int64_t id, const std::vector<GeoPoint>& ring);
  /// Removes the interior ring `v` lies on; refused for an exterior ring.
  bool RemoveHole(const VertexRef& v);
  /// Replaces a feature (the feature form's Apply), as one entry.
  bool UpdateFeature(const Feature& after, const std::string& label = "Edit Feature");
  /// Records a change already written to feature `before.id` (a form's
  /// Apply), as one entry.
  bool RecordEdit(const Feature& before, const std::string& label);

  // MARK: Sketch (the draw tools)

  const std::vector<GeoPoint>& sketch() const { return sketch_; }
  bool sketching() const { return !sketch_.empty(); }
  void AddSketchPoint(const GeoPoint& p) { sketch_.push_back(p); }
  /// Removes the last placed vertex; false when the sketch is empty.
  bool RemoveSketchPoint();
  /// Makes the sketch a feature (or a hole); false when it has too few
  /// vertices. The draw tools stay armed; kAddHole returns to select.
  bool FinishSketch();
  void CancelSketch() { sketch_.clear(); }

  // MARK: Hit testing, by the last drawn projection

  /// The vertex of a selected feature under `p`, or invalid.
  VertexRef VertexAt(PixelPoint p) const;
  /// The segment of a selected line or area under `p`: the returned ref's
  /// index is the insertion index, `*on` the nearest point on the segment.
  VertexRef SegmentAt(PixelPoint p, GeoPoint* on) const;
  /// The topmost feature drawn under `p`, or 0.
  int64_t FeatureAt(PixelPoint p) const;
  /// `p` snapped to a visible vertex within `snap_tolerance_px()` (never one
  /// being dragged), else un-projected.
  EditPosition ResolvePixel(PixelPoint p) const;

  // MARK: Dragging

  bool dragging() const { return drag_.kind != DragKind::kNone && drag_.moved; }
  bool drag_pending() const { return drag_.kind != DragKind::kNone; }
  bool BeginVertexDrag(const VertexRef& v, PixelPoint at);
  bool BeginMoveDrag(PixelPoint at);
  /// A box adds what it meets to the selection.
  bool BeginBoxDrag(PixelPoint at);
  bool DragTo(PixelPoint at);
  bool EndDrag(PixelPoint at);
  /// Puts the document back as it was at the press; leaves no undo entry.
  bool CancelDrag();

  /// True for a vertex or feature that is being dragged; snapping skips them.
  bool IsDragged(int64_t feature, int part, int path, int index) const;

  // MARK: Overlay input SPI, forwarded by FeatureOverlay

  bool OnMouseDown(const MouseEvent& e);
  bool OnMouseMove(const MouseEvent& e);
  bool OnMouseUp(const MouseEvent& e);
  bool OnKeyDown(const KeyEvent& e);
  /// What a press at `p` would do with the current tool: "Add point",
  /// "Select area “Lake”", "Move vertex"; empty when it would pan.
  std::string HintAt(PixelPoint p) const;
  /// True once after a double-click selected a feature to open in a form;
  /// the shell opens it.
  bool TakeOpenRequest();

  /// Vertex handles of the selection, the sketch and the selection box.
  Status DrawChrome(const MapProjection& proj, ICanvas& canvas) const;

 private:
  /// Selects `id` alone and raises the open request.
  void OpenFeature(int64_t id);
  /// True when `p` is on the first vertex of a sketch that a click there closes.
  bool OnSketchStart(PixelPoint p) const;

  enum class DragKind { kNone, kVertex, kMove, kBox };
  struct Drag {
    DragKind kind = DragKind::kNone;
    PixelPoint press{0, 0};
    PixelPoint now{0, 0};
    bool moved = false;
    bool edited = false;         ///< the press itself edited (an inserted vertex)
    VertexRef vertex;            ///< kVertex
    Feature base;                ///< kVertex: the feature the drag moves a vertex of
    GeoPoint press_geo;          ///< kMove
    std::vector<Feature> before; ///< what a cancel restores and the entry undoes
    std::string label;
    bool was_dirty = false;
  };

  /// Records `before` → current state of the same ids as one entry.
  void Commit(const std::string& label, std::vector<Feature> before, std::vector<int64_t> ids);
  /// Records an insert or delete.
  void CommitChange(const std::string& label, std::vector<Feature> before, std::vector<Feature> after);
  bool Snapshot(const std::vector<int64_t>& ids, std::vector<Feature>* out) const;
  bool Replace(const Feature& f);
  void Touched();
  bool HasProjection() const;
  const MapProjection& Projection() const;

  FeatureOverlay& overlay_;
  FeatureTool tool_ = FeatureTool::kSelect;
  bool has_edit_focus_ = false;
  bool open_request_ = false;
  double pick_tolerance_px_ = 6.0;
  double snap_tolerance_px_ = 8.0;

  app::UndoStack own_stack_;
  app::UndoStack* stack_ = nullptr;
  /// Expires with the session, so entries left on a shared stack fail cleanly.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);

  std::vector<GeoPoint> sketch_;
  VertexRef active_vertex_;
  Drag drag_;
};

}  // namespace fv
