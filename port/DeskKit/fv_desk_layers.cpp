// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_layers.h"

#include "fvkit/app/capabilities.h"
#include "fvkit/app/source_spec.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/feature_overlay.h"

namespace fv {
namespace desk {

namespace {

const app::OverlayTypeDesc* DescOf(const OverlayManager& stack, const Overlay& o) {
  const app::OverlayTypeRegistry* registry = stack.type_registry();
  return registry == nullptr ? nullptr : registry->Find(o.type_id());
}

}  // namespace

std::vector<std::shared_ptr<Overlay>> LayersModel::Listed() const {
  std::vector<std::shared_ptr<Overlay>> out;
  const auto& all = stack_.Overlays();
  for (auto it = all.rbegin(); it != all.rend(); ++it) {
    const app::OverlayTypeDesc* d = DescOf(stack_, **it);
    // An untyped overlay (made outside the app layer) is listed under its own name.
    if (d != nullptr && (d->display_name.empty() || !d->user_controllable)) continue;
    out.push_back(*it);
  }
  return out;
}

std::vector<LayerRow> LayersModel::Rows() const {
  std::vector<LayerRow> rows;
  for (const std::shared_ptr<Overlay>& o : Listed()) {
    LayerRow r;
    r.overlay = o.get();
    r.name = o->Name();
    if (const app::OverlayTypeDesc* d = DescOf(stack_, *o)) {
      r.type_name = d->display_name;
      r.icon = d->icon;
      r.top_most = d->is_top_most;
    }
    r.visible = o->IsVisible();
    r.has_opacity = o->SupportsOpacity();
    r.opacity = o->opacity();
    r.current = o.get() == stack_.current();
    if (app::Persistence* p = o->AsPersistence()) {
      r.dirty = p->is_dirty();
      r.source = app::SplitSourceSpec(p->file_spec()).path;
    }
    if (const auto* f = dynamic_cast<const FeatureOverlay*>(o.get())) r.rules = StyleRulesModel::Rows(*f);
    rows.push_back(std::move(r));
  }
  return rows;
}

Status LayersModel::At(size_t row, std::shared_ptr<Overlay>* out) const {
  const auto listed = Listed();
  if (row >= listed.size())
    return Status::Error(kInvalidArg, "no layer row " + std::to_string(row));
  *out = listed[row];
  return Status::Ok();
}

Status LayersModel::SetVisible(size_t row, bool visible) {
  std::shared_ptr<Overlay> o;
  Status s = At(row, &o);
  if (!s.ok()) return s;
  o->SetVisible(visible);
  return Status::Ok();
}

Status LayersModel::SetOpacity(size_t row, int percent) {
  std::shared_ptr<Overlay> o;
  Status s = At(row, &o);
  if (!s.ok()) return s;
  if (!o->SupportsOpacity())
    return Status::Error(kUnsupported, "\"" + o->Name() + "\" has no opacity");
  o->SetOpacity(percent);
  return Status::Ok();
}

Status LayersModel::Move(size_t from, size_t to) {
  const auto listed = Listed();
  if (from >= listed.size() || to >= listed.size())
    return Status::Error(kInvalidArg, "no layer row " + std::to_string(from >= listed.size() ? from : to));
  if (from == to) return Status::Ok();
  const std::shared_ptr<Overlay>& move = listed[from];
  const std::shared_ptr<Overlay>& anchor = listed[to];
  const app::OverlayTypeDesc* dm = DescOf(stack_, *move);
  const app::OverlayTypeDesc* da = DescOf(stack_, *anchor);
  const bool move_top = dm != nullptr && dm->is_top_most;
  const bool anchor_top = da != nullptr && da->is_top_most;
  if (move_top != anchor_top)
    return Status::Error(kInvalidArg, "\"" + (move_top ? move : anchor)->Name() +
                                          "\" stays above the other layers");
  // Rows run top first, so a row moving toward 0 goes above its anchor.
  return to < from ? stack_.MoveAbove(move, anchor) : stack_.MoveBelow(move, anchor);
}

Status LayersModel::Select(size_t row) {
  std::shared_ptr<Overlay> o;
  Status s = At(row, &o);
  if (!s.ok()) return s;
  return stack_.MakeCurrent(o);
}

}  // namespace desk
}  // namespace fv
