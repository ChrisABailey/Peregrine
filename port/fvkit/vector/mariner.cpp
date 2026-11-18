// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
/// Sounding figures in the mariner's depth unit.

#include "fvkit/vector/mariner.h"

#include <cmath>

namespace fv {

namespace {

constexpr double kMetresPerFoot = 0.3048;
constexpr int kFeetPerFathom = 6;

}  // namespace

SoundingFigure FormatSounding(double depth_m, DepthUnit unit) {
  const double v = std::fabs(depth_m);
  SoundingFigure out;
  switch (unit) {
    case DepthUnit::kMetres: {
      if (v >= 31.0) {
        out.whole = std::to_string(static_cast<long>(std::lround(v)));
        break;
      }
      const long dm = std::lround(v * 10.0);
      out.whole = std::to_string(dm / 10);
      if (dm % 10 != 0) out.fraction = std::to_string(dm % 10);
      break;
    }
    case DepthUnit::kFeet:
      out.whole = std::to_string(std::lround(v / kMetresPerFoot));
      break;
    case DepthUnit::kFathomsFeet: {
      const long ft = std::lround(v / kMetresPerFoot);
      const long fm = ft / kFeetPerFathom;
      if (fm >= 11) {
        out.whole = std::to_string(std::lround(v / (kMetresPerFoot * kFeetPerFathom)));
        break;
      }
      out.whole = std::to_string(fm);
      if (ft % kFeetPerFathom != 0) out.fraction = std::to_string(ft % kFeetPerFathom);
      break;
    }
  }
  return out;
}

}  // namespace fv
