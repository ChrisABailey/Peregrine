// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/app/vector_hit_test.h — the adapter that makes A5 real: a vector
// overlay's HitTest capability over the L4 PickIndex it already builds
// (fvkit-app-plan-COMPLETE.md §3e, "a thin adapter over the pick index").
//
// It is a separate header from pick.h on purpose. pick.h is the aggregation
// and knows nothing about vectors; this is the one file in fv::app that
// includes the vector seam, so an app layer over raster-only or hand-drawn
// overlays never pulls fvkit/vector in.
//
// WHY IT IS THIN. fv::PickIndex is filled by VectorRenderer from the ink it
// EMITTED, in canvas pixels, and is valid until the next Render(). So the
// projection argument is unused here, and that is the point rather than an
// omission: there is nothing to unproject, nothing to keep in sync with the
// view, and a tap therefore agrees with the screen by construction.
//
// THE ID, and the plan was wrong about this. `HitItem::feature` is one
// uint64_t and the plan says it "deliberately fits the ids PickIndex already
// emits" -- but a fv::FeatureRef is FOUR int32s, 128 bits, so it does not fit
// and no packing that keeps all four is possible. The header's own words are
// the ones that hold: the id is an "overlay-scoped feature id". This adapter
// mints a small dense handle per distinct FeatureRef it has reported and
// translates back through RefFor(). The handles are STABLE for the adapter's
// life -- one held across a redraw still names the same feature, which a
// packed encoding of a per-frame index could not promise -- and the table
// grows only with the features a human has actually pointed at.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "fvkit/app/capabilities.h"  // HitItem, HitTest
#include "fvkit/vector/pick.h"       // PickIndex, PickHit
#include "fvkit/vector/vector.h"     // FeatureRef, IVectorSource

namespace fv {
namespace app {

class VectorHitTest : public HitTest {
 public:
  // `owner` is stamped onto every HitItem and must outlive the adapter -- an
  // overlay holding this as a member is the intended shape.
  explicit VectorHitTest(Overlay& owner) : owner_(owner) {}

  // The index the renderer rebuilds in place at the start of every Render, so
  // this is set ONCE (VectorRenderer::pick_index() returns a stable reference)
  // and needs no attention afterwards. Null -- the default -- simply answers
  // nothing, which is what an overlay that has not drawn yet should say.
  void SetIndex(const PickIndex* index) { index_ = index; }
  const PickIndex* index() const { return index_; }

  // Optional. With a source the hint text is the product's own answer to
  // "what did I just click on?" (IVectorSource::Describe); without one, hits
  // are still reported and simply carry no words. Not owned.
  void SetSource(IVectorSource* source) { source_ = source; }
  IVectorSource* source() const { return source_; }

  void HitTestPoint(const MapProjection& proj, PixelPoint p,
                    double tolerance_px, std::vector<HitItem>& out) override;

  // The feature behind an id this adapter reported. An unknown id gives a
  // FeatureRef with layer < 0, i.e. !valid().
  FeatureRef RefFor(uint64_t id) const;
  // Mint or reuse the id for a ref. Public because an overlay that wants to
  // SELECT a feature it found some other way needs the same handle space.
  uint64_t IdFor(const FeatureRef& ref);

  // Drop the id table and the hint cache. For a source that has been closed
  // and reopened, where the same FeatureRef may mean a different row.
  void Reset();

  size_t id_count() const { return ids_.size(); }

  // What a click on a feature would feel like. A whole product paints one
  // cursor, so this is a field rather than a per-feature decision.
  CursorId cursor = CursorId::kHand;
  // How many hits to report per point. The pick index returns the whole stack
  // under the cursor, and a chooser listing forty overlapping contour lines
  // helps nobody; 0 means all of them.
  int max_items = 8;
  // Describe() re-reads a row from the product, and hover calls this on every
  // mouse move, so answers are memoised. Cleared wholesale (not evicted one
  // by one) when it grows past this -- the working set is what the cursor has
  // been over, so a scan-and-forget is the right cheap policy.
  size_t hint_cache_limit = 256;

 private:
  using Key = std::array<int32_t, 4>;
  static Key KeyOf(const FeatureRef& ref);

  const HintText& HintFor(const FeatureRef& ref);

  Overlay& owner_;
  const PickIndex* index_ = nullptr;
  IVectorSource* source_ = nullptr;

  std::map<Key, uint64_t> ids_;      // ref -> handle (1-based; 0 = none)
  std::vector<FeatureRef> by_id_;    // handle-1 -> ref
  std::map<Key, HintText> hints_;
};

}  // namespace app
}  // namespace fv
