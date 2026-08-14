// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/app/pick.h"

#include <algorithm>
#include <utility>

#include "fvkit/overlay/manager.h"

namespace fv {
namespace app {

const char* ToString(PickPolicy p) {
  switch (p) {
    case PickPolicy::kTopMost:
      return "top-most";
    case PickPolicy::kNearest:
      return "nearest";
    case PickPolicy::kAskWhenAmbiguous:
      return "ask-when-ambiguous";
  }
  return "?";
}

std::string PickRowText(const HitItem& item) {
  const std::string name =
      item.overlay != nullptr ? item.overlay->Name() : std::string();
  const std::string& text = !item.hint.tool_tip.empty() ? item.hint.tool_tip
                                                        : item.hint.status;
  if (name.empty()) return text;
  if (text.empty()) return name;
  return name + ": " + text;
}

namespace {

// Two hits are "the same thing" for hover purposes when they name the same
// feature of the same overlay. Distance is deliberately not part of it: moving
// the cursor two pixels along one road must not re-notify the shell.
bool SameHit(const HitItem& a, const HitItem& b) {
  return a.overlay == b.overlay && a.feature == b.feature &&
         a.cursor == b.cursor && a.hint.tool_tip == b.hint.tool_tip &&
         a.hint.status == b.hint.status;
}

}  // namespace

PickSession::PickSession(OverlayManager& manager, AppShell& shell)
    : manager_(manager), shell_(shell) {}

// ---------------------------------------------------------------------------
// Aggregation
// ---------------------------------------------------------------------------

std::vector<HitItem> PickSession::Gather(const MapProjection& proj,
                                         PixelPoint p,
                                         std::vector<int>* ranks) const {
  std::vector<HitItem> out;
  const std::vector<Overlay*> order = manager_.DrawOrder();
  int rank = 0;
  // DrawOrder is bottom-up; picking walks it topmost-first.
  for (auto it = order.rbegin(); it != order.rend(); ++it, ++rank) {
    HitTest* hit = (*it)->AsHitTest();
    if (hit == nullptr) continue;
    const size_t before = out.size();
    hit->HitTestPoint(proj, p, tolerance_px, out);
    for (size_t i = before; i < out.size(); ++i) {
      // The capability APPENDS, and an overlay that forgot to name itself is
      // a bug the aggregate can simply fix: the walk knows whose turn it is.
      if (out[i].overlay == nullptr) out[i].overlay = *it;
      ranks->push_back(rank);
    }
  }
  return out;
}

std::vector<HitItem> PickSession::HitTestPoint(const MapProjection& proj,
                                               PixelPoint p,
                                               PickPolicy policy) const {
  std::vector<int> ranks;
  std::vector<HitItem> hits = Gather(proj, p, &ranks);
  if (hits.size() < 2) return hits;

  // Sort an index, because the rank lives beside the item rather than on it --
  // HitItem is the capability's type and the rank is this walk's business.
  std::vector<size_t> idx(hits.size());
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;

  const bool nearest_first = policy == PickPolicy::kNearest;
  std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
    if (nearest_first) {
      if (hits[a].distance_px != hits[b].distance_px)
        return hits[a].distance_px < hits[b].distance_px;
      return ranks[a] < ranks[b];
    }
    if (ranks[a] != ranks[b]) return ranks[a] < ranks[b];
    return hits[a].distance_px < hits[b].distance_px;
  });

  std::vector<HitItem> sorted;
  sorted.reserve(hits.size());
  for (size_t i : idx) sorted.push_back(std::move(hits[i]));
  return sorted;
}

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------

void PickSession::UpdateHover(const MapProjection& proj, PixelPoint p,
                              PickPolicy policy) {
  // A hover cannot ask a question; ambiguity ranks as top-most for the cursor.
  if (policy == PickPolicy::kAskWhenAmbiguous) policy = PickPolicy::kTopMost;
  std::vector<HitItem> hits = HitTestPoint(proj, p, policy);
  if (hits.empty()) {
    ClearHover();
    return;
  }
  if (hovered_ && SameHit(*hovered_, hits.front())) {
    // Same feature, same words: the shell already shows the right thing. Keep
    // the fresher distance, which a caller reading hovered() may care about.
    hovered_->distance_px = hits.front().distance_px;
    return;
  }
  hovered_ = std::move(hits.front());
  shell_.SetCursor(hovered_->cursor);
  shell_.ShowHint(hovered_->hint);
}

void PickSession::ClearHover() {
  if (!hovered_) return;
  hovered_.reset();
  shell_.SetCursor(CursorId::kDefault);
  shell_.ShowHint(HintText());
}

// ---------------------------------------------------------------------------
// Click
// ---------------------------------------------------------------------------

std::optional<HitItem> PickSession::ResolveClick(const MapProjection& proj,
                                                 PixelPoint p,
                                                 PickPolicy policy) {
  std::vector<HitItem> hits = HitTestPoint(proj, p, policy);
  if (hits.empty()) return std::nullopt;
  if (policy != PickPolicy::kAskWhenAmbiguous || hits.size() == 1) {
    return std::move(hits.front());
  }

  std::vector<std::string> rows;
  rows.reserve(hits.size());
  for (const HitItem& h : hits) rows.push_back(PickRowText(h));
  const std::optional<int> choice = shell_.ChooseFromList("Select", rows);
  if (!choice) return std::nullopt;
  if (*choice < 0 || static_cast<size_t>(*choice) >= hits.size()) {
    // A shell that answers out of range has a bug, and guessing which row it
    // meant would hide it. Treated as a cancel, which is the safe outcome.
    return std::nullopt;
  }
  return std::move(hits[static_cast<size_t>(*choice)]);
}

// ---------------------------------------------------------------------------
// Snap-to
// ---------------------------------------------------------------------------

std::optional<SnapToItem> PickSession::SnapToPoint(const MapProjection& proj,
                                                   PixelPoint p) {
  std::vector<SnapToItem> items;
  const std::vector<Overlay*> order = manager_.DrawOrder();
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    SnapTo* snap = (*it)->AsSnapTo();
    if (snap == nullptr) continue;
    const size_t before = items.size();
    snap->SnapToPoint(proj, p, tolerance_px, items);
    for (size_t i = before; i < items.size(); ++i) {
      if (items[i].overlay == nullptr) items[i].overlay = *it;
    }
  }

  if (items.empty()) return std::nullopt;
  if (items.size() == 1) return std::move(items.front());

  std::vector<std::string> rows;
  rows.reserve(items.size());
  for (const SnapToItem& s : items) {
    // Unlike a hit, a snap candidate carries its own row text by contract
    // (capabilities.h) -- "the north end of runway 15" is not something the
    // pick layer could compose. The overlay name is only the fallback.
    if (!s.description.empty()) {
      rows.push_back(s.description);
    } else {
      rows.push_back(s.overlay != nullptr ? s.overlay->Name() : "");
    }
  }
  const std::optional<int> choice = shell_.ChooseFromList("Snap to", rows);
  if (!choice) return std::nullopt;
  if (*choice < 0 || static_cast<size_t>(*choice) >= items.size()) {
    return std::nullopt;
  }
  return std::move(items[static_cast<size_t>(*choice)]);
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

MenuNode PickSession::BuildContextMenu(const MapProjection& proj,
                                       PixelPoint p) const {
  MenuNode root;
  const std::vector<Overlay*> order = manager_.DrawOrder();
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    ContextMenu* menu = (*it)->AsContextMenu();
    if (menu == nullptr) continue;
    // Into a scratch node, so an overlay that appends NOTHING costs no
    // separator -- the section break belongs to the contribution, not to the
    // attempt.
    MenuNode section;
    menu->AppendMenuItems(proj, p, section);
    if (section.children.empty()) continue;
    if (!root.children.empty()) root.children.push_back(MenuNode());  // rule
    for (MenuNode& child : section.children) {
      root.children.push_back(std::move(child));
    }
  }
  return root;
}

bool PickSession::ShowContextMenu(const MapProjection& proj, PixelPoint p) {
  MenuNode root = BuildContextMenu(proj, p);
  if (root.children.empty()) return false;
  shell_.ShowContextMenu(p, root);
  return true;
}

}  // namespace app
}  // namespace fv
