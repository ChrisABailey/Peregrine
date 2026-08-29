// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// LabelPlacer / MeasureLabelInk — see fvkit/canvas/label_placer.h.

#include "fvkit/canvas/label_placer.h"

#include <cmath>

namespace fv {

namespace {

bool Intersects(const PixelRect& a, const PixelRect& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
         b.y < a.y + a.height;
}

bool Contains(const PixelRect& outer, const PixelRect& inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

PixelRect Grow(const PixelRect& r, int px) {
  PixelRect g = r;
  g.x -= px;
  g.y -= px;
  g.width += 2 * px;
  g.height += 2 * px;
  return g;
}

}  // namespace

LabelInk MeasureLabelInk(ICanvas& canvas, double x, double y,
                         const std::string& text, const TextStyle& resolved,
                         const LabelStyle& style) {
  LabelInk ink;
  ink.origin.x = static_cast<int>(std::lround(x)) + style.dx;
  ink.origin.y = static_cast<int>(std::lround(y)) + style.dy;

  PixelSize ext{0, 0};
  if (!canvas.GetTextExtent(text, resolved, &ext).ok() || ext.width <= 0 ||
      ext.height <= 0)
    return ink;  // measured stays false; origin unadjusted, box empty

  // Alignment moves the anchor; this is the same arithmetic (and the same
  // order) DrawLabelAtPixel performs, which is the point of sharing it.
  switch (style.halign) {
    case LabelHAlign::kLeft: break;
    case LabelHAlign::kCenter: ink.origin.x -= ext.width / 2; break;
    case LabelHAlign::kRight: ink.origin.x -= ext.width; break;
  }
  switch (style.valign) {
    case LabelVAlign::kBaseline:
    case LabelVAlign::kBottom: break;
    case LabelVAlign::kCenter: ink.origin.y += ext.height / 2; break;
    case LabelVAlign::kTop: ink.origin.y += ext.height; break;
  }

  ink.box.x = ink.origin.x;
  ink.box.y = ink.origin.y - ext.height;  // text draws upward from its baseline
  ink.box.width = ext.width;
  ink.box.height = ext.height;
  ink.measured = true;
  return ink;
}

void LabelPlacer::Reserve(const PixelRect& r) { reserved_.push_back(r); }

void LabelPlacer::Clear() {
  boxes_.clear();
  reserved_.clear();
  placed_ = 0;
  rejected_ = 0;
}

PixelRect LabelPlacer::EffectiveBounds(const ICanvas& canvas) const {
  PixelRect b = bounds_;
  if (b.width <= 0 || b.height <= 0) {
    const PixelSize s = canvas.Size();
    b.x = 0;
    b.y = 0;
    b.width = s.width;
    b.height = s.height;
  }
  if (margin_ > 0) {
    b.x += margin_;
    b.y += margin_;
    b.width -= 2 * margin_;
    b.height -= 2 * margin_;
  }
  return b;
}

bool LabelPlacer::Fits(const PixelRect& box, const PixelRect& bounds) const {
  if (bounds.width > 0 && bounds.height > 0 && !Contains(bounds, box))
    return false;
  const PixelRect probe = Grow(box, padding_);
  for (const PixelRect& r : boxes_)
    if (Intersects(probe, r)) return false;
  for (const PixelRect& r : reserved_)
    if (Intersects(probe, r)) return false;
  return true;
}

bool LabelPlacer::Place(ICanvas& canvas, double x, double y,
                        const std::string& text, const TextStyle& resolved,
                        const LabelStyle& style, LabelInk* out_ink) {
  const LabelInk ink = MeasureLabelInk(canvas, x, y, text, resolved, style);
  if (out_ink) *out_ink = ink;

  // Unmeasurable: draw it, record nothing. See the header.
  if (!ink.measured) {
    ++placed_;
    return true;
  }

  if (!Fits(ink.box, EffectiveBounds(canvas))) {
    ++rejected_;
    return false;
  }
  boxes_.push_back(ink.box);
  ++placed_;
  return true;
}

bool LabelPlacer::PlaceFirstFit(ICanvas& canvas,
                                const std::vector<SurfacePoint>& anchors,
                                const std::string& text,
                                const TextStyle& resolved,
                                const LabelStyle& style, LabelInk* out_ink,
                                size_t* out_index) {
  const PixelRect bounds = EffectiveBounds(canvas);
  for (size_t i = 0; i < anchors.size(); ++i) {
    const LabelInk ink =
        MeasureLabelInk(canvas, anchors[i].x, anchors[i].y, text, resolved,
                        style);
    if (!ink.measured) {
      // No metrics at all: the first anchor is as good as any, and refusing
      // would drop every label on this canvas.
      if (out_ink) *out_ink = ink;
      if (out_index) *out_index = i;
      ++placed_;
      return true;
    }
    if (!Fits(ink.box, bounds)) continue;
    boxes_.push_back(ink.box);
    ++placed_;
    if (out_ink) *out_ink = ink;
    if (out_index) *out_index = i;
    return true;
  }
  ++rejected_;
  return false;
}

}  // namespace fv
