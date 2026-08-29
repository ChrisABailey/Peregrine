// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_dted_enums.h — portable mirror of DtedMapServer.idl's DtedDisplayModeEnum.
//
// Values ARE the COM ABI numbering (fvw_core/MapDataServer/MdsInterfaces/
// DtedMapServer.idl) — never renumber. Same policy as fv_map_enums.h. The
// low bit is color(0)/mono(1); the pair index (value>>1) selects
// rendered/elevation/slope, which is how DTEDRenderer::SetDisplayMode maps
// these onto CDtedReader's internal display_mode 0/1/2 + color-mode flag.

#pragma once

namespace fv {

enum DtedDisplayMode {
  kDtedRenderedColor = 0,  // shaded relief, colour
  kDtedRenderedMono = 1,   // shaded relief, grayscale
  kDtedElevationColor = 2,  // elevation colour bands
  kDtedElevationMono = 3,
  kDtedSlopeColor = 4,  // slope shading
  kDtedSlopeMono = 5,
};

}  // namespace fv
