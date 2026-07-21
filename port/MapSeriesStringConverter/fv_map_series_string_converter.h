// Copyright (c) 1994-2009 Georgia Tech Research Corporation, Atlanta, GA
// This file is part of FalconView(tm).

// FalconView(tm) is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// FalconView(tm) is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public License
// along with FalconView(tm).  If not, see <http://www.gnu.org/licenses/>.

// FalconView(tm) is a trademark of Georgia Tech Research Corporation.

// fv_map_series_string_converter.h — portable extraction of
// CMapSeriesStringConverter (fvw_core/MapSeriesStringConverter). The COM
// wrapper (BSTR/VARIANT_BOOL surface) stays Windows-only; this API uses
// std::wstring/bool. Internals were std::wstring already.

#pragma once

#include <string>

#include "fv_compat.h"
#include "fv_map_enums.h"

namespace fv {

class MapSeriesStringConverter {
 public:
  // Formats a (product, scale, units, series) tuple per `format`.
  // e.g. FORMAT_PRODUCT_NAME_SCALE_SERIES -> L"CADRG 1:5 M (GNC)".
  HRESULT ToString(const std::wstring& productName, double dScale,
                   MapScaleUnitsEnum eScaleUnits, const std::wstring& series,
                   bool bIsSoftScale, MapSeriesStringFormatEnum Format,
                   std::wstring* pMapSeriesString);

  // Parses a map-series string back into its components. Only the DTED
  // special cases and FORMAT_SCALE_SERIES are supported;
  // FORMAT_PRODUCT_NAME_SCALE_SERIES / FORMAT_SERIES_SCALE return E_NOTIMPL
  // (matching the COM original, which threw).
  HRESULT ToMapSeries(const std::wstring& mapSeriesString,
                      MapSeriesStringFormatEnum Format,
                      std::wstring* pProductName, double* pScale,
                      MapScaleUnitsEnum* pScaleUnits, std::wstring* pSeries,
                      bool* pIsSoftScale);

  // The public static-style helpers below are used directly by other
  // FalconView code on Windows; kept accessible for parity.
  static std::wstring ToString(double dScale, MapScaleUnitsEnum ScaleUnitsEnum,
                               bool bSimplifyWithUnits = true);
  static std::wstring ToString(MapScaleUnitsEnum ScaleUnitsEnum);
  static std::wstring ToString(long lNum);
  static std::wstring ToString(double dNum, long lNumDecimals);
  static MapScaleUnitsEnum ToUnits(const std::wstring& strUnits);
  static bool ToMapScale(std::wstring strScale, double* pScale,
                         MapScaleUnitsEnum* pScaleUnitsEnum);
};

}  // namespace fv
