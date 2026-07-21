// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/overlay/grid.h — built-in lat/lon graticule overlay (the plan's
// first native overlay; also the golden-test subject for the SPI).

#pragma once

#include "fvkit/overlay/overlay.h"

namespace fv {

class GridOverlay : public Overlay {
 public:
  GridOverlay() : Overlay("grid") {}

  // Lines every "nice" interval (chosen so lines are >= ~min_spacing_px
  // apart): 30, 10, 5, 1, 0.5, 0.25, 0.1, ... degrees.
  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  void SetColor(const FvColor& c) { color_ = c; }
  void SetMinSpacingPx(int px) { min_spacing_px_ = px; }

 private:
  FvColor color_{255, 255, 255, 96};
  int min_spacing_px_ = 80;
};

}  // namespace fv
