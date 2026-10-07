// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_view_map_view.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fv {
namespace view {

bool PreviewTransform(const Viewport& frame, const Viewport& live, Affine* out) {
  if (!frame.HasSurface() || !live.HasSurface()) return false;
  // Three frame points one point apart near the live centre's image in the
  // frame: exact for an affine projection, a local fit otherwise.
  PointF origin;
  if (!frame.PointFor(live.Center(), &origin)) origin = frame.SurfaceCenter();
  const double step = 64.0;
  const PointF f0 = origin, f1{origin.x + step, origin.y}, f2{origin.x, origin.y + step};
  PointF l0, l1, l2;
  if (!live.PointFor(frame.GeoAt(f0), &l0) || !live.PointFor(frame.GeoAt(f1), &l1) ||
      !live.PointFor(frame.GeoAt(f2), &l2))
    return false;
  Affine m;
  m.a = (l1.x - l0.x) / step;
  m.b = (l1.y - l0.y) / step;
  m.c = (l2.x - l0.x) / step;
  m.d = (l2.y - l0.y) / step;
  m.tx = l0.x - m.a * f0.x - m.c * f0.y;
  m.ty = l0.y - m.b * f0.x - m.d * f0.y;
  if (!std::isfinite(m.a) || !std::isfinite(m.b) || !std::isfinite(m.c) ||
      !std::isfinite(m.d) || !std::isfinite(m.tx) || !std::isfinite(m.ty))
    return false;
  *out = m;
  return true;
}

MapView::MapView(const Viewport& initial, LadderKind ladder,
                 ProductsAt products_at, double uniform_factor)
    : view_(initial), ladder_(ladder, uniform_factor) {
  SetGroup(ladder, std::move(products_at), uniform_factor);
}

void MapView::SetGroup(LadderKind ladder, ProductsAt products_at,
                       double uniform_factor) {
  ladder_ = ScaleLadder(ladder, uniform_factor);
  products_at_ = std::move(products_at);
  has_product_ = false;
  product_ = LadderProduct{};
  ++generation_;
  if (!products_at_) return;
  const LadderStep s =
      ladder_.Settle(view_.ScaleDenom(), nullptr, products_at_(view_.Center()));
  if (view_.HasSurface()) {
    Apply(s, view_.SurfaceCenter());
  } else {
    last_outcome_ = s.outcome;
    has_product_ = s.has_product;
    product_ = s.product;
    if (s.has_product)
      view_ = view_.WithCamera(view_.Center(), s.display_denom, view_.Rotation());
  }
}

void MapView::Resize(double width_pt, double height_pt, double display_scale,
                     double mm_per_point) {
  SetView(view_.WithSurface(width_pt, height_pt, display_scale, mm_per_point));
}

void MapView::SetViewport(const Viewport& v) { SetView(v); }

void MapView::SetProduct(const LadderProduct& p) {
  if (has_product_ && product_.series_id == p.series_id) return;
  has_product_ = true;
  product_ = p;
  ++generation_;
}

void MapView::SetView(const Viewport& v) {
  if (v == view_) return;
  view_ = v;
  ++generation_;
}

void MapView::Hover(const PointF& p) {
  hovering_ = true;
  hover_ = p;
}

void MapView::HoverExit() { hovering_ = false; }

void MapView::PointerDown(const PointF& p) {
  dragging_ = true;
  drag_grab_ = view_.GeoAt(p);
  Hover(p);
}

void MapView::PointerDrag(const PointF& p) {
  if (!dragging_) return PointerDown(p);
  SetView(view_.WithGeoAt(drag_grab_, p));
  Hover(p);
}

void MapView::PointerUp(const PointF& p) {
  if (dragging_) PointerDrag(p);
  dragging_ = false;
}

void MapView::Scroll(const PointF& at, double dx, double dy, bool precise) {
  Hover(at);
  if (precise) {
    SetView(view_.Panned(dx, dy));
    return;
  }
  if (dy == 0.0 || !std::isfinite(dy)) return;
  const int notches = std::max(1, static_cast<int>(std::lround(std::fabs(dy))));
  for (int i = 0; i < notches; ++i) {
    const LadderStep s = StepAt(dy > 0 ? 1 : -1, at);
    if (s.outcome != LadderOutcome::kStepped) break;
  }
}

void MapView::MagnifyBegin(const PointF& at) {
  magnifying_ = true;
  Hover(at);
}

void MapView::Magnify(const PointF& at, double factor) {
  if (!magnifying_) MagnifyBegin(at);
  SetView(view_.ZoomedBy(factor, at));
}

void MapView::MagnifyEnd(const PointF& at) {
  if (!magnifying_) return;
  magnifying_ = false;
  if (!products_at_) return;
  const LadderStep s = ladder_.Settle(view_.ScaleDenom(),
                                      has_product_ ? &product_ : nullptr,
                                      products_at_(view_.GeoAt(at)));
  Apply(s, at);
}

void MapView::Rotate(const PointF& at, double degrees) {
  SetView(view_.RotatedBy(degrees, at));
}

LadderStep MapView::StepAt(int direction, const PointF& at) {
  Hover(at);
  LadderStep s;
  if (!products_at_) {
    s.outcome = LadderOutcome::kNoProduct;
    s.display_denom = view_.ScaleDenom();
    last_outcome_ = s.outcome;
    return s;
  }
  s = ladder_.Step(view_.ScaleDenom(), has_product_ ? &product_ : nullptr,
                   direction, products_at_(view_.GeoAt(at)));
  Apply(s, at);
  return s;
}

LadderStep MapView::Step(int direction) {
  const PointF at = KeyAnchor();
  const bool was_hovering = hovering_;
  const LadderStep s = StepAt(direction, at);
  hovering_ = was_hovering;
  return s;
}

PointF MapView::KeyAnchor() const {
  return hovering_ ? hover_ : view_.SurfaceCenter();
}

void MapView::Apply(const LadderStep& s, const PointF& anchor) {
  last_outcome_ = s.outcome;
  if (s.outcome == LadderOutcome::kEndOfLadder) return;
  Viewport next = view_.ZoomedTo(s.display_denom, anchor);
  bool changed = s.product_changed || s.has_product != has_product_;
  LadderStep chosen = s;
  // A uniform step into a zoom limit: pick the product for the scale the
  // camera actually took.
  if (ladder_.Kind() == LadderKind::kUniform && products_at_ &&
      next.ScaleDenom() != s.display_denom) {
    chosen = ladder_.Settle(next.ScaleDenom(), has_product_ ? &product_ : nullptr,
                            products_at_(next.GeoAt(anchor)));
    if (!chosen.has_product && has_product_) {
      chosen.has_product = true;
      chosen.product = product_;
    }
    changed = chosen.has_product != has_product_ ||
              (chosen.has_product && chosen.product.series_id != product_.series_id);
  }
  if (chosen.has_product) {
    has_product_ = true;
    product_ = chosen.product;
  }
  if (changed) ++generation_;
  SetView(next);
}

}  // namespace view
}  // namespace fv
