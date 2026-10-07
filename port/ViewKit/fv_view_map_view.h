// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_view_map_view.h — the platform-free controller behind a map widget.
///
/// A shell translates its OS events into the calls below (points, top-left
/// origin) and redraws when `Generation()` changes. `MapView` owns the
/// viewport, the zoom ladder of the current map group and the product being
/// drawn. It is not thread-safe: call it from the UI thread and hand
/// `View()` snapshots to the render scheduler.
#pragma once

#include <cstdint>

#include "fv_view_ladder.h"
#include "fv_view_viewport.h"

namespace fv {
namespace view {

/// A 2D affine map, CoreGraphics order: x' = a*x + c*y + tx, y' = b*x + d*y + ty.
struct Affine {
  double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

  PointF Apply(const PointF& p) const {
    return PointF{a * p.x + c * p.y + tx, b * p.x + d * p.y + ty};
  }
};

/// The transform, in points, that places a frame rendered for `frame` where
/// it belongs under `live`, for showing the last frame during a gesture.
/// Exact for Equal Arc; a first-order fit at the live centre for the other
/// projections. False when the views have no surface or no common ground.
bool PreviewTransform(const Viewport& frame, const Viewport& live, Affine* out);

class MapView {
 public:
  /// `products_at` lists what the current map group can draw at a position;
  /// it may be empty, in which case scale steps change nothing.
  MapView(const Viewport& initial, LadderKind ladder, ProductsAt products_at,
          double uniform_factor = 2.0);

  /// Replaces the map group: its ladder and candidate source. The product is
  /// re-chosen at the view centre.
  void SetGroup(LadderKind ladder, ProductsAt products_at,
                double uniform_factor = 2.0);

  /// The widget changed size or moved to a screen with another pitch.
  void Resize(double width_pt, double height_pt, double display_scale,
              double mm_per_point);

  void SetViewport(const Viewport& v);
  /// Draws `p` without changing the camera, as when a saved view names its
  /// series. The ladder takes its next step from there.
  void SetProduct(const LadderProduct& p);

  // MARK: Pointer

  /// The cursor moved (no button). Keyboard steps zoom about this point.
  void Hover(const PointF& p);
  /// The cursor left the widget; keyboard steps then zoom about the centre.
  void HoverExit();
  void PointerDown(const PointF& p);
  /// A drag pans: the position grabbed at PointerDown stays under the pointer.
  void PointerDrag(const PointF& p);
  void PointerUp(const PointF& p);

  /// A scroll event at `at`. Precise (trackpad) scrolling pans by
  /// (`dx`, `dy`) points. Notched (wheel) scrolling takes one ladder step per
  /// notch of `dy`; positive `dy` zooms in.
  void Scroll(const PointF& at, double dx, double dy, bool precise);

  // MARK: Pinch and rotate

  void MagnifyBegin(const PointF& at);
  /// `factor` > 1 zooms in, relative to the previous event of the gesture.
  void Magnify(const PointF& at, double factor);
  /// Ends the pinch and settles on the nearest ladder step.
  void MagnifyEnd(const PointF& at);
  void Rotate(const PointF& at, double degrees);

  // MARK: Steps

  /// One ladder step anchored at `at`; `direction` > 0 zooms in.
  LadderStep StepAt(int direction, const PointF& at);
  /// A keyboard step (Page Up/Down) anchored at the cursor, or the centre
  /// when the cursor is outside the widget.
  LadderStep Step(int direction);

  // MARK: State

  const Viewport& View() const { return view_; }
  bool HasProduct() const { return has_product_; }
  const LadderProduct& Product() const { return product_; }
  /// The outcome of the last step or settle; kEndOfLadder is what the status
  /// bar reports.
  LadderOutcome LastOutcome() const { return last_outcome_; }
  /// Bumped whenever the view or the product changes.
  uint64_t Generation() const { return generation_; }
  bool InGesture() const { return dragging_ || magnifying_; }
  /// Whether the cursor is over the widget, and where (the keyboard anchor).
  bool Hovering() const { return hovering_; }
  const PointF& HoverPoint() const { return hover_; }

 private:
  void SetView(const Viewport& v);
  void Apply(const LadderStep& s, const PointF& anchor);
  PointF KeyAnchor() const;

  Viewport view_;
  ScaleLadder ladder_;
  ProductsAt products_at_;
  bool has_product_ = false;
  LadderProduct product_;
  LadderOutcome last_outcome_ = LadderOutcome::kNoProduct;
  uint64_t generation_ = 0;

  bool hovering_ = false;
  PointF hover_;
  bool dragging_ = false;
  GeoPoint drag_grab_;
  bool magnifying_ = false;
};

}  // namespace view
}  // namespace fv
