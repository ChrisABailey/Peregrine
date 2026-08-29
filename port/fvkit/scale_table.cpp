// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ScaleDenominatorFor — see fvkit/scale_table.h.

#include "fvkit/scale_table.h"

#include "fv_map_enums.h"
#include "fv_map_scale_util.h"
#include "fvkit/proj.h"

namespace fv {

double ScaleDenominatorFor(const MapProjection& proj) {
  if (!proj.Ready()) return 0.0;

  // kScale and kPhysical both report the denominator the caller asked for.
  const double declared = proj.Scale();
  if (declared > 0.0) return declared;

  // kResolution: invert the nominal dpp relation (exact; see the header).
  const double dpp = proj.DegPerPixelLat();
  if (!(dpp > 0.0)) return 0.0;

  MapScaleUtil util;
  double scale = 0.0;
  if (FAILED(util.ResolutionToScale(dpp, MAP_SCALE_ARC_DEGREES, &scale)))
    return 0.0;
  return scale;
}

}  // namespace fv
