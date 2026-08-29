// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_map_enums.h — portable mirror of map enums from
// fvw_core/CustomInterfaces/CommonMap.idl. Values are part of the COM ABI on
// Windows and of persisted data; never renumber.

#pragma once

// From fvw_core/MapSeriesStringConverter/Interfaces (imports CommonMap.idl)
enum MapSeriesStringFormatEnum {
  FORMAT_PRODUCT_NAME_SCALE_SERIES = 0,
  FORMAT_SERIES_SCALE = 1,
  FORMAT_SCALE_SERIES = 2,
  FORMAT_SCALE = 3,
  FORMAT_SCALE_NO_UNITS = 4
};

enum MapScaleUnitsEnum {
  MAP_SCALE_DENOMINATOR = 0,
  MAP_SCALE_NM = 1,
  MAP_SCALE_MILE = 2,
  MAP_SCALE_KILOMETER = 3,
  MAP_SCALE_METERS = 4,
  MAP_SCALE_YARDS = 5,
  MAP_SCALE_FEET = 6,
  MAP_SCALE_INCHES = 7,
  MAP_SCALE_ARC_DEGREES = 8,
  MAP_SCALE_ARC_MINUTES = 9,
  MAP_SCALE_ARC_SECONDS = 10,
  MAP_SCALE_WORLD = 11
};
