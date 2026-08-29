// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/overlay/grid.h — the lat/lon graticule, ported from FalconView's
// grid_map library (Applications/FalconView/grid_map).
//
// WHAT THIS REPLACED. The first version of this file was a 30-line sample
// written to give the overlay SPI a golden test: it picked a "nice" interval
// off degrees-per-pixel and drew screen-axis-aligned horizontal and vertical
// lines. That is a graticule the way a stick figure is a portrait, and it had
// a bug that follows directly from the shape — a horizontal screen line is
// only a parallel when the chart is north-up, so the sample drew a visibly
// wrong grid under rotation (the ledger's parked "grid draws wrong under map
// rotation"). Drawing real geographic lines through GeoDraw closes that: the
// projection turns them, as it turns everything else that goes through it.
//
// WHAT CAME ACROSS FROM grid_map, and it is the part worth having:
//   * the SCALE-KEYED SPACING TABLE (grid_spacing.h) -- cartographers' numbers,
//     not a formula;
//   * major and minor lines, and the unlabelled TICKS that subdivide them
//     further without adding lines to the picture;
//   * the LABEL FORMAT LADDER -- degrees, then degrees-minutes, then
//     degrees-minutes-seconds, then tenths of a second, chosen from the MINOR
//     spacing (the finest thing on screen) and not the major;
//   * labels anchored where each line ENTERS the viewport, latitude first,
//     longitude labels dropped where they would collide with one.
//
// WHAT DID NOT, and why:
//   * MGRS/UTM and GARS. grid_map is really three overlays sharing a class and
//     a pen; the other two are ~3000 lines of military zone geometry over GDI
//     clip paths, and they are not what this port's audience is looking at a
//     map for. Deliberately dropped rather than deferred (Chris, 2026-08-29).
//   * The LinearGridElement/GridLine/Tickmarks hierarchy. Latitude versus
//     longitude is a flag, not a type: the two virtuals differed only in which
//     pen they picked up. It is one function over a GridAxis here.
//   * The static CLists. GridLine, Tickmarks and GridLabel kept their label,
//     line and point lists -- and every one of GridLabel's font settings -- in
//     file-scope mutable statics shared by every instance, which is why the
//     original had to call reset_label_list()/reset_geo_line_list() by hand
//     from the overlay's draw. They are locals of one draw pass here and the
//     resets are gone with them.
//   * set_valid()/get_valid() caching of the generated line list. OnDraw is
//     stateless, the geometry is tens of lines, and P18's raster cache sits
//     above this now.
//   * PRM_get_registry_int inside the draw call -- the original re-read six
//     registry keys per frame. Settings are properties now (AsProperties), and
//     a shell loads them once from peregrine.ini.

#ifndef FVKIT_OVERLAY_GRID_H_
#define FVKIT_OVERLAY_GRID_H_

#include <string>
#include <vector>

#include "fvkit/app/properties.h"
#include "fvkit/overlay/grid_spacing.h"
#include "fvkit/overlay/overlay.h"

namespace fv {

class GridOverlay : public Overlay, public app::Properties {
 public:
  GridOverlay();

  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  // The property page, declared rather than drawn (fvkit/app/properties.h).
  // Its keys live under the "grid." prefix (see port/peregrine.ini.sample); a
  // shell builds its own dialog from Describe() and persists with
  // LoadFrom/SaveTo, both of which are written once in the base class.
  app::Properties* AsProperties() override { return this; }
  const std::vector<app::PropertySpec>& Describe() const override;
  Status GetProperty(const std::string& key,
                     app::PropertyValue* out) const override;
  Status SetProperty(const std::string& key,
                     const app::PropertyValue& value) override;

  // Kept because pyfvw binds it and it is the one thing a caller sets without
  // caring about the schema. Equivalent to SetProperty("line_color", ...).
  void SetColor(const FvColor& c);

  // --- diagnostics, for tests --------------------------------------------
  // What the last OnDraw actually put on the chart. A graticule's failure mode
  // is drawing the wrong NUMBER of things, and a pixel hash cannot say which.
  struct DrawStats {
    int parallels = 0;       // lines of constant latitude drawn
    int meridians = 0;       // lines of constant longitude drawn
    int major_lines = 0;     // of those, how many were major
    int ticks = 0;           // tick marks drawn
    int labels_placed = 0;
    int labels_rejected = 0;  // refused by the placer: overlap or off-screen
    double scale_denominator = 0.0;  // what the spacing table was keyed on
    GraticuleSpacing lat_spacing;
    GraticuleSpacing lon_spacing;
  };
  const DrawStats& last_draw() const { return stats_; }

 private:
  std::vector<app::PropertyValue> values_;
  DrawStats stats_;
};

// The label a graticule line carries, exposed because it is pure, fiddly and
// worth testing on its own.
//
// `axis` picks the hemisphere letter (N/S or E/W) and the degree field's
// width -- longitude is zero-padded to three digits so a column of labels
// lines up. `minor_spacing_deg` chooses the format: at 1 degree or coarser
// whole degrees, down to 1 minute degrees and minutes, down to 1 second
// degrees-minutes-seconds, and finer than that tenths of a second. Carrying
// the spacing rather than the value is what stops "N 34" appearing twice on a
// map whose lines are 30 seconds apart.
std::string GraticuleLabelText(double degrees, GridAxis axis,
                               double minor_spacing_deg);

}  // namespace fv

#endif  // FVKIT_OVERLAY_GRID_H_
