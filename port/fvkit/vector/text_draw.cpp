// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Extracted verbatim from vector/renderer.cpp's anonymous namespace (G3).
// See text_draw.h for why. No edit but the namespace and the includes.

#include "fvkit/vector/text_draw.h"

#include <algorithm>
#include <cmath>

#include "fvkit/vector/renderer.h"  // kMaxLabelPx

namespace fv {

// The size this label actually draws at, in pixels.
//
// Three ways in, one way out. A kMeters label states a ground size and is
// converted with this frame's metres per pixel. A kPixels label is either left
// alone (ref_scale 0, the default: constant on screen) or reinterpreted as
// "that size AT ref_scale" and scaled by how far the current scale is from it —
// which is the same ground sizing expressed in the unit the products author in.
// See VectorRenderer::SetLabelReferenceScale.
double LabelPixelSize(const LabelStyle& lb, double scale_denominator,
                      double meters_per_pixel, double ref_scale) {
  double px = lb.style.size;
  if (lb.size_unit == LabelSizeUnit::kMeters) {
    if (!(lb.ground_size_m > 0.0) || !(meters_per_pixel > 0.0)) return 0.0;
    px = lb.ground_size_m / meters_per_pixel;
  } else if (ref_scale > 0.0 && scale_denominator > 0.0) {
    px *= ref_scale / scale_denominator;
  } else {
    return px;  // untouched, so a pinned golden is untouched
  }
  return (std::min)(px, kMaxLabelPx);
}

// Where an along-path run sits across its line. See text_draw.h for why the
// cap height is a constant fraction of the em and not a font metric.
double AlongPathAnchorShift(const LabelStyle& lb, double drawn_size_px) {
  if (lb.along_anchor != LabelAlongAnchor::kCenter) return 0.0;
  // Half the cap height, and negative: the placer's positive offset is left of
  // travel (up on screen), and centring lowers the baseline.
  return -0.5 * kCapHeightEm * drawn_size_px;
}

// --- halo (T2) --------------------------------------------------------------
//
// The offsets one halo pass draws the string at. Windows FalconView drew FOUR —
// right, left, down, up — and at one pixel that is a closed ring: a glyph's own
// coverage bridges the diagonal, so nothing shows through the corners. At two
// or more it is not closed, the corners open and the outline reads as a plus
// sign around each letter, so the four diagonals are added past r == 1. They go
// on the SAME circle of radius r (r/sqrt2 each way) rather than at the square's
// corner, which would put them r*sqrt2 out and fringe the halo.
//
// This is a dilation by stamping, and it is imprecise on purpose: it is exactly
// as good as the original, needs nothing from ICanvas, and costs 4-8 draws of a
// string the backend has already shaped. A real halo dilates glyph COVERAGE in
// the rasterizer, which only CpuCanvas could do and which every other backend
// would then have to grow.
int HaloOffsets(double width_px, double dx[kMaxHaloOffsets],
                double dy[kMaxHaloOffsets]) {
  // At least one whole pixel: a sub-pixel halo is a request the integer text
  // origins cannot honour, and rounding it to zero would drop the halo the
  // style asked for.
  const double r = (std::max)(1.0, std::floor(width_px + 0.5));
  dx[0] =  r; dy[0] =  0.0;
  dx[1] = -r; dy[1] =  0.0;
  dx[2] =  0.0; dy[2] =  r;
  dx[3] =  0.0; dy[3] = -r;
  if (r < 2.0) return 4;
  const double d = r * 0.7071067811865476;
  dx[4] =  d; dy[4] =  d;
  dx[5] = -d; dy[5] =  d;
  dx[6] =  d; dy[6] = -d;
  dx[7] = -d; dy[7] = -d;
  return 8;
}

// The halo width for a label that has been resized. `halo_width` is authored in
// pixels against the authored text size, so a label that grew (kMeters, or a
// label reference scale) must grow its outline with it or a 40 px name wears a
// one-pixel thread. The ratio is the same one LabelPixelSize applied.
double HaloPixels(const LabelStyle& lb, double drawn_size_px) {
  if (!(lb.halo_width > 0.0)) return 0.0;
  if (lb.style.size > 0.0 && drawn_size_px > 0.0)
    return lb.halo_width * (drawn_size_px / lb.style.size);
  return lb.halo_width;
}

// Per-glyph advances, measured through the canvas because the canvas owns the
// font. Taken as DIFFERENCES OF PREFIX WIDTHS rather than per-character widths:
// GetTextExtent returns whole pixels, so summing rounded characters would drift
// by up to half a pixel per glyph, while prefix differences put every glyph
// within a pixel of where the upright renderer would have put it.
bool GlyphAdvances(ICanvas* canvas, const std::string& text,
                   const TextStyle& ts, std::vector<double>* out) {
  out->clear();
  out->reserve(text.size());
  int prev = 0;
  for (size_t i = 1; i <= text.size(); ++i) {
    PixelSize ext;
    if (!canvas->GetTextExtent(text.substr(0, i), ts, &ext).ok()) return false;
    out->push_back(static_cast<double>(ext.width - prev));
    prev = ext.width;
  }
  return !out->empty() && prev > 0;
}

}  // namespace fv
