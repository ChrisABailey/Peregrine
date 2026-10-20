// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_map_options.h — the built-in Map ▸ Options pages, and the Route
/// page's routing section of Overlay ▸ Options.
///
/// Each is a `MapOptionsSource` over a small `app::Properties` whose keys are
/// the ones peregrine.ini already documents for the family, so a value set in
/// the dialog and one written by hand are the same setting.
#pragma once

#include <functional>
#include <optional>
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

/// A `mariner.*` depth in metres: empty leaves `out` unset; false when
/// `text` is not a non-negative number.
bool ParseMarinerDepth(const std::string& text, std::optional<double>* out);

/// A `mariner.*` switch: 1/on/yes/true or 0/off/no/false, any case; empty
/// leaves `out` unset; false for anything else.
bool ParseMarinerFlag(const std::string& text, std::optional<bool>* out);

/// Elevation: `dted.elevation_bands_ft`, applied with
/// `SetDtedShadedElevationBands`.
MapOptionsSource ElevationMapOptions();

/// DNC: `geosym.data_dir`, `geosym.brightness`, `geosym.contrast`, applied
/// with `SetVectorMapConfig`.
MapOptionsSource DncMapOptions();

/// ENC: `enc.data_dir`, `enc.show_meta_objects` and the `mariner.*` keys,
/// which DNC is drawn with too.
MapOptionsSource EncMapOptions();

/// OpenStreetMap: `osm.style`.
MapOptionsSource OsmMapOptions();

/// The app-wide "Data" page: `geotrans.data_dir`, the GeoTrans datum and
/// ellipsoid tables (ellips.dat, 3_param.dat, 7_param.dat, egm96.grd).
MapOptionsSource DataOptions();

/// Points GeoTrans at `dir` by setting `MSPCCS_DATA`; empty leaves the
/// environment as it is. GeoTrans reads its tables once per process, so a
/// change after the first coordinate conversion takes effect on the next launch.
void SetGeoTransDataDir(const std::string& dir);

/// The Route page's Routing section: `routing.graph`, `routing.rules` and
/// `routing.profile`, which the shell's one planner reads rather than each
/// route. `changed` runs after the new values are in the settings.
MapOptionsSource RoutingOptions(std::function<void()> changed);

/// Every built-in source, in no particular order (map-group pages follow the
/// map group table; app-wide pages come after them).
std::vector<MapOptionsSource> BuiltinMapOptions();

}  // namespace desk
}  // namespace fv
