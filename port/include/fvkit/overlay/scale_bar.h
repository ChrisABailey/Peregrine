// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file fvkit/overlay/scale_bar.h
 * The map scale bar, ported from FalconView's scalebar library
 * (Applications/FalconView/scalebar). A ruler pinned to the left and/or bottom
 * edge of the view, measured on the ground through the projection each frame.
 *
 * Carried over: the division ladder (powers of ten, then doubling, then a
 * fallback to the small unit), the placement offsets, the label format, and
 * the "Both" layout. Dropped: the mosaic-map coordinate conversion (the port
 * has no mosaic surfaces), the registry reads, the MFC property page, and the
 * tooltip/status-bar plumbing, which a shell gets from HitTest.
 */

#ifndef FVKIT_OVERLAY_SCALE_BAR_H_
#define FVKIT_OVERLAY_SCALE_BAR_H_

#include <string>
#include <vector>

#include "fvkit/app/capabilities.h"
#include "fvkit/app/properties.h"
#include "fvkit/overlay/overlay.h"

namespace fv {

/// The unit pairs FalconView offers; the second of each is used when the
/// first would need divisions finer than one unit.
enum class ScaleBarUnits { kNmYards = 0, kNmFeet = 1, kKmMeters = 2 };

/// Which edge rulers are drawn. Values are the "orientation" choice indices.
enum class ScaleBarOrientation { kVertical = 0, kHorizontal = 1, kBoth = 2 };

/// The tick spacing chosen for one ruler.
struct ScaleBarDivisions {
  /// Ticks to draw, including the one at zero. 0 means nothing is drawable.
  int count = 0;
  /// Distance between ticks, in display units.
  double increment = 0.0;
  /// Metres in one display unit.
  double meters_per_unit = 0.0;
  /// Label suffix for the last tick, with its leading space (" NM").
  std::string unit_name;
};

/**
 * Chooses the tick spacing for a ruler spanning `distance_m` on the ground.
 * A transcription of CScaleBarIcon::get_scale_params: the increment falls by
 * powers of ten until at least `min_divisions` fit, then doubles until at most
 * `max_divisions` do; an increment below one unit restarts the search in the
 * small unit. A non-positive or non-finite distance returns count 0.
 */
ScaleBarDivisions ChooseScaleBarDivisions(double distance_m, ScaleBarUnits units,
                                          int min_divisions, int max_divisions);

/// The scale bar overlay. Static, screen-anchored, and settable through
/// app::Properties under the `[scalebar]` settings section.
class ScaleBarOverlay : public Overlay,
                        public app::Properties,
                        public app::HitTest {
 public:
  ScaleBarOverlay();

  static const char kTypeId[];  // "fv.scalebar"

  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  app::Properties* AsProperties() override { return this; }
  const std::vector<app::PropertySpec>& Describe() const override;
  Status GetProperty(const std::string& key,
                     app::PropertyValue* out) const override;
  Status SetProperty(const std::string& key,
                     const app::PropertyValue& value) override;

  /// Hits the label boxes of the last draw, as FalconView did; the ruler
  /// lines themselves are not a target.
  app::HitTest* AsHitTest() override { return this; }
  void HitTestPoint(const MapProjection& proj, PixelPoint p,
                    double tolerance_px,
                    std::vector<app::HitItem>& out) override;

  /// Device pixels per layout pixel. Scales the pen widths, tick length and
  /// font size -- the quantities FalconView's drawing-size preference scaled --
  /// and leaves the edge offsets alone.
  void SetDpiScale(double scale) { dpi_scale_ = scale > 0.0 ? scale : 1.0; }

  /// What the last OnDraw put on the chart, for tests.
  struct DrawStats {
    ScaleBarDivisions horizontal;
    ScaleBarDivisions vertical;
    std::vector<int> horizontal_x;  ///< tick x positions, left to right
    std::vector<int> vertical_y;    ///< tick y positions, bottom to top
    std::vector<std::string> labels;
  };
  const DrawStats& last_draw() const { return stats_; }

 private:
  std::vector<app::PropertyValue> values_;
  double dpi_scale_ = 1.0;
  DrawStats stats_;
  std::vector<PixelRect> label_boxes_;
};

}  // namespace fv

#endif  // FVKIT_OVERLAY_SCALE_BAR_H_
