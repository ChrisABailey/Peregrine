// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv::OsmStyleEngine — the MapLibre style engine under the name the OSM
/// format, DeskKit, pyfvw and Pippin use. The engine is
/// `fvkit/style/maplibre_style.h`; feature layers render through it too.
#pragma once

#include "fvkit/style/maplibre_style.h"

namespace fv {

/// A class rather than an alias so it can be forward-declared.
class OsmStyleEngine : public MapLibreStyleEngine {
 public:
  using MapLibreStyleEngine::MapLibreStyleEngine;
};

using OsmStyleLayerInfo = MapLibreLayerInfo;
using OsmStyleLayerType = MapLibreLayerType;

}  // namespace fv
