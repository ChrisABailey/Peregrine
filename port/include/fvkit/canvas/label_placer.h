// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/canvas/label_placer.h — labels that do not land on top of each other.
//
// SHARED OVERLAY TOOLKIT (2 of 3, with scale_table.h and app/properties.h).
// Extracted from grid_map/label.cpp during the graticule port. If you are
// porting an overlay that writes text on the map — a point name, a contour
// value, a zone id, a route leg's distance — place it through this rather than
// calling DrawLabel directly, and the labels of every overlay that does the
// same will stop colliding.
//
// WHAT WAS MISSING. T2 gave labels a halo, E8 gave them alignment, and
// text_draw.h lays glyphs along a path — but NOTHING in fvkit ever asked
// whether another label was already there. FalconView asked: `GridLabel::
// overlap` keeps an array of the latitude labels' rectangles and drops any
// longitude label that intersects one. That is the whole idea, and it is not
// grid-specific in any way.
//
// TWO RULES, AND THEY ARE THE WHOLE CONTRACT.
//
//   1. CALL ORDER IS PRIORITY. The first label to ask for a box gets it; a
//      later one that overlaps is refused. So an overlay places what matters
//      most first, exactly as FalconView drew latitude labels before longitude
//      ones. There is no scoring, no second pass, and no way for a late label
//      to evict an early one -- that keeps placement stable frame to frame,
//      which is what stops text flickering while the user pans.
//
//   2. A PLACER IS PER-FRAME. Clear() at the top of a draw, or make a fresh
//      one. It holds pixel boxes, and a pixel box means nothing once the
//      camera has moved.
//
// A REFUSED LABEL IS NOT DRAWN AT ALL. It is not shrunk, moved or faded --
// give Place several candidate anchors (PlaceFirstFit) if you want a fallback
// position, and accept the drop if none of them fit. Half a map of unreadable
// overlapping text is the failure this exists to prevent.

#ifndef FVKIT_CANVAS_LABEL_PLACER_H_
#define FVKIT_CANVAS_LABEL_PLACER_H_

#include <cstddef>
#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"
#include "fvkit/geo.h"
#include "fvkit/vector/style.h"  // LabelStyle, LabelHAlign, LabelVAlign

namespace fv {

// Where a label's ink actually lands.
struct LabelInk {
  // The baseline-left point DrawTextString is finally called with: the
  // caller's (x, y), plus LabelStyle::dx/dy, then adjusted for halign/valign.
  PixelPoint origin;
  // The rectangle that ink occupies. Text draws from its baseline, so the box
  // runs UPWARD from origin.y -- box.y == origin.y - height. Same
  // approximation the pick index has always made: a descender hangs slightly
  // below the box, because ICanvas exposes no ascent/descent split.
  PixelRect box;
  // False => ICanvas::GetTextExtent failed or returned an empty extent. `box`
  // is then empty and `origin` has had NO alignment applied -- which is
  // exactly what DrawLabelAtPixel does in the same situation, and why this
  // struct reports the failure instead of hiding it.
  bool measured = false;
};

// The box GeoDraw::DrawLabelAtPixel will put this label's ink in.
//
// `resolved` is the TextStyle AFTER size resolution -- LabelStyle::size_unit
// kMeters has already become a pixel size. GeoDraw owns that conversion
// because it needs the projection; a caller placing an ordinary pixel-sized
// overlay label just passes `style.style`.
//
// This function exists so the placer and the draw can never disagree about
// where a label is: DrawLabelAtPixel calls it too.
LabelInk MeasureLabelInk(ICanvas& canvas, double x, double y,
                         const std::string& text, const TextStyle& resolved,
                         const LabelStyle& style);

class LabelPlacer {
 public:
  LabelPlacer() = default;

  // Minimum clear gap between two placed labels, in pixels. The box is grown
  // by this on every side before the intersection test, so 0 lets two labels
  // touch and 2 keeps a visible lane between them. Default 2.
  void SetPadding(int px) { padding_ = px < 0 ? 0 : px; }
  int padding() const { return padding_; }

  // Labels must lie wholly inside this rectangle. Unset (the default, width
  // or height 0) means "the canvas", read from the canvas passed to Place --
  // which is what an overlay wants and saves it from plumbing the size.
  void SetBounds(const PixelRect& r) { bounds_ = r; }

  // Shrinks the bounds on every side. FalconView used 3 px for its grid
  // labels, so a label at the screen edge is not cut in half by it.
  void SetMargin(int px) { margin_ = px < 0 ? 0 : px; }

  // Blocks a region without a label being there: a legend, a ride bar, the
  // ownship's own symbol. Counted like a placed box for the overlap test but
  // not reported in boxes().
  void Reserve(const PixelRect& r);

  // Forget every box. Call at the top of a frame.
  void Clear();

  // Would a label with this ink box be accepted right now? Does not record.
  bool Fits(const PixelRect& box, const PixelRect& bounds) const;

  // Measure `text`, test it, and on acceptance RECORD it and report where to
  // draw. Returns false when the label was refused -- do not draw it.
  //
  // An UNMEASURABLE label (a canvas with no text metrics) is accepted, drawn
  // and not recorded: refusing every label on such a canvas would be a worse
  // failure than an unchecked one, and the pyfvw Python canvases are the real
  // case. `out_ink->measured` says which happened.
  bool Place(ICanvas& canvas, double x, double y, const std::string& text,
             const TextStyle& resolved, const LabelStyle& style,
             LabelInk* out_ink);

  // The same over several anchors in preference order; the first that fits
  // wins. Returns false when none of them do. `out_index`, when non-null,
  // receives which candidate was used.
  bool PlaceFirstFit(ICanvas& canvas, const std::vector<SurfacePoint>& anchors,
                     const std::string& text, const TextStyle& resolved,
                     const LabelStyle& style, LabelInk* out_ink,
                     size_t* out_index = nullptr);

  // Diagnostics. `rejected` is what a test asserts on: a placer that accepted
  // everything has not been exercised.
  size_t placed_count() const { return placed_; }
  size_t rejected_count() const { return rejected_; }
  // The accepted boxes, in the order they were accepted. Reserved regions are
  // not here.
  const std::vector<PixelRect>& boxes() const { return boxes_; }

 private:
  PixelRect EffectiveBounds(const ICanvas& canvas) const;

  std::vector<PixelRect> boxes_;
  std::vector<PixelRect> reserved_;
  PixelRect bounds_;
  int padding_ = 2;
  int margin_ = 0;
  size_t placed_ = 0;
  size_t rejected_ = 0;
};

}  // namespace fv

#endif  // FVKIT_CANVAS_LABEL_PLACER_H_
