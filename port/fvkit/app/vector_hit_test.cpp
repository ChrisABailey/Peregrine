// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/app/vector_hit_test.h"

#include <utility>

namespace fv {
namespace app {

VectorHitTest::Key VectorHitTest::KeyOf(const FeatureRef& ref) {
  return Key{ref.source, ref.layer, ref.tile, ref.feature};
}

uint64_t VectorHitTest::IdFor(const FeatureRef& ref) {
  const Key key = KeyOf(ref);
  auto it = ids_.find(key);
  if (it != ids_.end()) return it->second;
  by_id_.push_back(ref);
  const uint64_t id = by_id_.size();  // 1-based; 0 stays "no feature"
  ids_.emplace(key, id);
  return id;
}

FeatureRef VectorHitTest::RefFor(uint64_t id) const {
  if (id == 0 || id > by_id_.size()) return FeatureRef();
  return by_id_[static_cast<size_t>(id - 1)];
}

void VectorHitTest::Reset() {
  ids_.clear();
  by_id_.clear();
  hints_.clear();
}

const HintText& VectorHitTest::HintFor(const FeatureRef& ref) {
  static const HintText kNone;
  if (source_ == nullptr) return kNone;

  const Key key = KeyOf(ref);
  auto it = hints_.find(key);
  if (it != hints_.end()) return it->second;

  FeatureDescription desc;
  if (!source_->Describe(ref, &desc).ok()) {
    // A source that cannot describe (the default) is not an error here -- the
    // hit is still real, it just has no words. Memoised as empty so a hover
    // does not re-ask the product once per mouse move.
    if (hints_.size() >= hint_cache_limit) hints_.clear();
    return hints_.emplace(key, HintText()).first->second;
  }

  HintText hint;
  hint.tool_tip = !desc.title.empty() ? desc.title : desc.layer_name;
  hint.status = hint.tool_tip;
  if (!desc.class_name.empty()) {
    hint.status += hint.status.empty() ? desc.class_name
                                       : " (" + desc.class_name + ")";
  }
  if (!desc.source_note.empty()) {
    hint.status += hint.status.empty() ? desc.source_note
                                       : "  " + desc.source_note;
  }
  if (hints_.size() >= hint_cache_limit) hints_.clear();
  return hints_.emplace(key, std::move(hint)).first->second;
}

void VectorHitTest::HitTestPoint(const MapProjection& proj, PixelPoint p,
                                 double tolerance_px,
                                 std::vector<HitItem>& out) {
  // Unused, and see the header: the index is already in canvas pixels because
  // it was built from the ink that was drawn there.
  (void)proj;
  if (index_ == nullptr) return;

  const std::vector<PickHit> hits =
      index_->HitTest(p.x, p.y, tolerance_px);  // topmost first
  const size_t limit = max_items > 0 ? static_cast<size_t>(max_items)
                                     : hits.size();
  for (size_t i = 0; i < hits.size() && i < limit; ++i) {
    HitItem item;
    item.overlay = &owner_;
    item.feature = IdFor(hits[i].ref);
    item.distance_px = hits[i].distance;
    item.hint = HintFor(hits[i].ref);
    item.cursor = cursor;
    out.push_back(std::move(item));
  }
}

}  // namespace app
}  // namespace fv
