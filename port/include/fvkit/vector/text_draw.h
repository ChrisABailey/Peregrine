// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// FvKit label mechanics (draw plan G3) — the halo, the sizing, the advances.
//
// EXTRACTED, NOT WRITTEN, exactly as symbol_draw.h was in G2: all four
// functions were in vector/renderer.cpp's anonymous namespace, where nothing
// but the chart renderer could reach them, and G3's GeoDraw wants every one of
// them. Copying them would fork the halo, which is the thing most likely to
// drift into "the overlay's text looks slightly different from the chart's".
// The acceptance test for the move is the same as G2's: every pinned golden
// byte-identical.

#ifndef FVKIT_VECTOR_TEXT_DRAW_H_
#define FVKIT_VECTOR_TEXT_DRAW_H_

#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"
#include "fvkit/vector/style.h"

namespace fv {

// The offsets one halo pass draws the string at. Windows FalconView drew FOUR
// — right, left, down, up — and at one pixel that is a closed ring: a glyph's
// own coverage bridges the diagonal, so nothing shows through the corners. At
// two or more it is not closed, the corners open and the outline reads as a
// plus sign around each letter, so the four diagonals are added past r == 1.
// They go on the SAME circle of radius r (r/sqrt2 each way) rather than at the
// square's corner, which would put them r*sqrt2 out and fringe the halo.
//
// This is a dilation by stamping, and it is imprecise on purpose: it is
// exactly as good as the original, needs nothing from ICanvas, and costs 4-8
// draws of a string the backend has already shaped. A real halo dilates glyph
// COVERAGE in the rasterizer, which only CpuCanvas could do and which every
// other backend would then have to grow.
constexpr int kMaxHaloOffsets = 8;

int HaloOffsets(double width_px, double dx[kMaxHaloOffsets],
                double dy[kMaxHaloOffsets]);

// The halo width for a label that has been resized. `halo_width` is authored
// in pixels against the authored text size, so a label that grew (kMeters, or
// a label reference scale) must grow its outline with it or a 40 px name wears
// a one-pixel thread.
double HaloPixels(const LabelStyle& lb, double drawn_size_px);

// The pixel size a label is actually drawn at. Returns the authored size
// untouched unless the label asked for ground sizing or the caller set a label
// reference scale — which is what keeps a pinned golden untouched.
double LabelPixelSize(const LabelStyle& lb, double scale_denominator,
                      double meters_per_pixel, double ref_scale);

// Cap height as a fraction of the em, for the one thing that needs it and
// cannot ask: centring an along-path run on the line it names.
//
// MEASURED, not assumed, over the fonts this port actually draws with —
// Arial 0.7163, Helvetica 0.7173, Verdana 0.7271, Geneva 0.7578 (OS/2
// sCapHeight / unitsPerEm). 0.72 is central to that spread, and the whole
// spread is 0.042 em: at a 12 px label the worst font is half a pixel from
// where this puts it, which is smaller than the rounding the canvas does to
// place a glyph at all. A per-font number would need ICanvas to report cap
// height, and every backend — including pyfvw's Python subclasses — would
// have to grow that method to gain half a pixel.
constexpr double kCapHeightEm = 0.72;

// The perpendicular shift, in pixels, that puts `lb`'s along-path run where
// its anchor asks. ADDED to LabelStyle::offset_px by the caller, in the same
// sense (positive = LEFT of travel), so kBaseline returns 0 and changes
// nothing — which is what keeps every pinned golden byte-identical.
//
// kCenter returns a NEGATIVE shift: left of travel is up on screen for a
// left-to-right run, and centring moves the baseline DOWN so the caps
// straddle the line.
double AlongPathAnchorShift(const LabelStyle& lb, double drawn_size_px);

// Per-glyph advances, measured through the canvas because the canvas owns the
// font. Taken as DIFFERENCES OF PREFIX WIDTHS rather than per-character
// widths: GetTextExtent returns whole pixels, so summing rounded characters
// would drift by up to half a pixel per glyph, while prefix differences put
// every glyph within a pixel of where the upright renderer would have put it.
bool GlyphAdvances(ICanvas* canvas, const std::string& text,
                   const TextStyle& ts, std::vector<double>* out);

}  // namespace fv

#endif  // FVKIT_VECTOR_TEXT_DRAW_H_
