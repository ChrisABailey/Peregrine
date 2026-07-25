// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::PickIndex — hit-testing over what was actually DRAWN (plan §5.3).
//
// The plan's rule, and the reason this is not a source-side spatial query:
// **the pick index is built from drawn geometry, not source geometry**, so a
// tap agrees with what the user can see. A hairline centreline styled 5 px
// wide is 5 px wide to the finger; a point symbol is its glyph box, not its
// anchor pixel; a clipped polyline contributes only its visible runs. Querying
// the source instead would hit features that were styled invisible, missed the
// scale band, or fell outside the canvas.
//
// (Source-side query stays the right answer for the pre-rendered raster iOS
// path, where no scene exists to index — see the plan's note.)
//
// The index is filled by VectorRenderer as it emits primitives and is valid
// until the next Render(). Shapes are in canvas pixels, so a hit test is a
// straight geometric predicate: no projection, no scale, nothing to keep in
// sync with the view.
//
// HitTest returns the WHOLE STACK under the point, topmost first, so a UI can
// say "3 features here" rather than guessing which one was meant.
//
// PERFORMANCE: HitTest is a linear scan with a bounding-box reject per shape.
// A full DNC harbor view indexes ~2k shapes, which a click tests in well under
// a millisecond. If a product ever indexes enough shapes for that to matter, a
// grid goes in here — the API does not change. (Plan phase R3 retires the
// per-render rebuild in favour of the retained TileDisplayList anyway.)

#ifndef FVKIT_VECTOR_PICK_H_
#define FVKIT_VECTOR_PICK_H_

#include <cstddef>
#include <vector>

#include "fvkit/geo.h"            // PixelPoint, PixelRect
#include "fvkit/vector/vector.h"  // FeatureRef

namespace fv {

// One feature under the query point.
struct PickHit {
  FeatureRef ref;
  int priority = 0;      // display priority of the pass that drew it
  double distance = 0.0; // pixels from the point to the drawn ink; 0 = on it
};

class PickIndex {
 public:
  void Clear();

  // A stroked run (already clipped to the canvas). `half_width` is half the
  // pen width in pixels — the ink actually laid down.
  void AddStroke(const FeatureRef& ref, int priority,
                 const std::vector<PixelPoint>& run, double half_width);

  // A filled ring (already clipped). Inside counts as a hit (even-odd against
  // this ring alone, matching CpuCanvas's fill rule for a single ring).
  void AddFill(const FeatureRef& ref, int priority,
               const std::vector<PixelPoint>& ring);

  // A box: point symbols and labels, whose ink is best approximated by the
  // extent the renderer just drew into.
  void AddBox(const FeatureRef& ref, int priority, const PixelRect& box);

  // Features whose ink is within `tolerance` pixels of (x, y), TOPMOST FIRST
  // (descending display priority, then reverse draw order — the same order
  // they were painted, reversed). One entry per feature: when several of a
  // feature's primitives hit, the topmost one is reported with the smallest
  // distance found.
  std::vector<PickHit> HitTest(int x, int y, double tolerance = 3.0) const;

  size_t shape_count() const { return shapes_.size(); }
  bool empty() const { return shapes_.empty(); }

 private:
  enum class Kind { kStroke, kFill, kBox };

  struct Shape {
    Kind kind = Kind::kStroke;
    FeatureRef ref;
    int priority = 0;
    size_t order = 0;      // draw order, for the topmost tie-break
    double half_width = 0.0;
    PixelRect box;         // shape bounds (also the hit shape for kBox)
    std::vector<PixelPoint> pts;
  };

  std::vector<Shape> shapes_;
};

}  // namespace fv

#endif  // FVKIT_VECTOR_PICK_H_
