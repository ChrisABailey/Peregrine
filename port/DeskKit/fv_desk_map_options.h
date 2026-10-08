// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_map_options.h — the built-in Map ▸ Options pages.
///
/// Each is a `MapOptionsSource` over a small `app::Properties` whose keys are
/// the ones peregrine.ini already documents for the family, so a value set in
/// the dialog and one written by hand are the same setting.
#pragma once

#include <string>
#include <vector>

#include "fv_desk_options.h"

namespace fv {
namespace desk {

/// DTED elevation breakpoints, in feet, from "2500, 5000, …": the whole
/// numbers present (rounded half to even), ascending, the lowest five kept.
/// Unparsable parts are skipped; an empty result means FalconView's defaults.
/// The same reading PythonView gives `dted.elevation_bands_ft`.
std::vector<int> ParseElevationBands(const std::string& text);

/// The canonical spelling of `feet`: "2500,5000,7500".
std::string FormatElevationBands(const std::vector<int>& feet);

/// Elevation: `dted.elevation_bands_ft`, applied with
/// `SetDtedShadedElevationBands`.
MapOptionsSource ElevationMapOptions();

/// Every built-in source, in no particular order (pages follow the map
/// group table).
std::vector<MapOptionsSource> BuiltinMapOptions();

}  // namespace desk
}  // namespace fv
