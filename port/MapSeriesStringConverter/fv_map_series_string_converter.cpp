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

// fv_map_series_string_converter.cpp — portable extraction of
// CMapSeriesStringConverter. Logic preserved verbatim, with wide CRT calls
// (swprintf_s/swscanf_s/_itow_s/_wcsicmp) mapped to their standard
// equivalents. On MSVC's wide printf/scanf family %s means wchar_t*; the
// standard requires %ls — spelled %ls here.
//
// NOTE (preserved quirks):
//  * ToMapSeries leaves all output parameters untouched (except
//    *pIsSoftScale = false) for FORMAT_SCALE / FORMAT_SCALE_NO_UNITS and for
//    any unrecognized series string, and still returns S_OK — callers see
//    whatever was in the out-params before the call, as on Windows.
//  * ToMapSeries(FORMAT_SCALE_SERIES) parses "prefix (series)" with
//    substr(0, i-1): a '(' at position 0 yields substr(0, npos) == the whole
//    string being parsed as a scale.
//  * ToUnits returns MAP_SCALE_NM for unknown unit strings (after a debug
//    assert on Windows).
//  * ToMapScale truncates resolutions to 7 decimal places via
//    floor(x * 1e7) / 1e7.

#include "fv_map_series_string_converter.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cwchar>

namespace fv {

HRESULT MapSeriesStringConverter::ToString(const std::wstring& productName,
                                           double dScale,
                                           MapScaleUnitsEnum eScaleUnits,
                                           const std::wstring& series,
                                           bool bIsSoftScale,
                                           MapSeriesStringFormatEnum Format,
                                           std::wstring* pMapSeriesString) {
  std::wstring ret;

  const std::wstring& strProductName = productName;
  const std::wstring& strSeries = series;

  // DTED is a special case of map scale.  It shows as "DTED Level 1"
  // which does not name the resolution as other map scale strings do
  //
  // While the hardcode in the JMPS code seems no longer present, some
  // references remain and the interfaces still imply the specific
  // format for the DTED scale strings.

  // e.g, 1:5 M
  if (Format == FORMAT_SCALE) {
    ret = ToString(dScale, eScaleUnits);
  }

  else if (Format == FORMAT_SCALE_NO_UNITS) {
    ret = ToString(dScale, eScaleUnits, false);
  }

  // DTED special case handling required
  else if (wcscasecmp(strProductName.c_str(), L"DTED") == 0) {
    if (Format == FORMAT_PRODUCT_NAME_SCALE_SERIES)
      ret = L"DTED " + strSeries;
    else
      ret = strSeries;
  }

  // e.g, "CADRG 1:5 M (GNC)
  else if (Format == FORMAT_PRODUCT_NAME_SCALE_SERIES) {
    if (bIsSoftScale) {
      ret = strProductName;

      if (strSeries.size())
        ret += L" (" + strSeries + L")";
      else
        ret += L" " + ToString(dScale, eScaleUnits);
    } else {
      ret = strProductName + L" " + ToString(dScale, eScaleUnits);

      if (strSeries.size()) ret += L" (" + strSeries + L")";
    }
  }

  // e.g, GNC 1:5 M
  else if (Format == FORMAT_SERIES_SCALE) {
    if (strSeries.size()) ret = strSeries + L" ";

    ret += ToString(dScale, eScaleUnits);
  }

  // e.g, 1:5 M (GNC)
  else if (Format == FORMAT_SCALE_SERIES) {
    ret += ToString(dScale, eScaleUnits);

    if (strSeries.size()) ret += L" (" + strSeries + L")";
  }

  *pMapSeriesString = ret;

  return S_OK;
}

HRESULT MapSeriesStringConverter::ToMapSeries(
    const std::wstring& mapSeriesString, MapSeriesStringFormatEnum Format,
    std::wstring* pProductName, double* pScale, MapScaleUnitsEnum* pScaleUnits,
    std::wstring* pSeries, bool* pIsSoftScale) {
  *pIsSoftScale = false;

  const std::wstring& strSeries = mapSeriesString;

  // DTED is a special case of map scale.  It shows as "DTED Level 1"
  // which does not name the resolution as other map scale strings do
  if (strSeries == L"Level 0" || strSeries == L"DTED Level 0") {
    *pProductName = L"DTED";
    *pSeries = L"Level 0";
    *pScale = 30.0;
    *pScaleUnits = MAP_SCALE_ARC_SECONDS;
  } else if (strSeries == L"Level 1" || strSeries == L"DTED Level 1") {
    *pProductName = L"DTED";
    *pSeries = L"Level 1";
    *pScale = 3.0;
    *pScaleUnits = MAP_SCALE_ARC_SECONDS;
  } else if (strSeries == L"Level 2" || strSeries == L"DTED Level 2") {
    *pProductName = L"DTED";
    *pSeries = L"Level 2";
    *pScale = 1.0;
    *pScaleUnits = MAP_SCALE_ARC_SECONDS;
  } else if (strSeries == L"Level 3" || strSeries == L"DTED Level 3") {
    *pProductName = L"DTED";
    *pSeries = L"Level 3";
    *pScale = 0.4;
    *pScaleUnits = MAP_SCALE_ARC_SECONDS;
  }

  // e.g, CADRG 1:5 M (GNC)
  else if (Format == FORMAT_PRODUCT_NAME_SCALE_SERIES) {
    // This conversion is not supported
    return E_NOTIMPL;
  } else if (Format == FORMAT_SERIES_SCALE) {
    // This conversion is not supported
    return E_NOTIMPL;
  }

  // e.g, 1:5 M (GNC)
  else if (Format == FORMAT_SCALE_SERIES) {
    *pProductName = L"";

    size_t i = strSeries.find('(');
    size_t j = strSeries.find(')');
    if (i != std::wstring::npos) {
      ToMapScale(strSeries.substr(0, i - 1), pScale, pScaleUnits);

      if (j != std::wstring::npos)
        *pSeries = strSeries.substr(i + 1, j - i - 1);

    } else {
      ToMapScale(strSeries, pScale, pScaleUnits);
      *pSeries = L"";
    }
  }

  return S_OK;
}

// convert a scale to a string
std::wstring MapSeriesStringConverter::ToString(double dScale,
                                                MapScaleUnitsEnum ScaleUnitsEnum,
                                                bool bSimplifyWithUnits) {
  std::wstring string;

  if (ScaleUnitsEnum == MAP_SCALE_WORLD) {
    string = L"World";
  } else if (ScaleUnitsEnum == MAP_SCALE_DENOMINATOR && bSimplifyWithUnits) {
    long lScale = static_cast<long>(dScale);

    if (lScale <= 0)
      string = L"Invalid Scale";

    else if (lScale >= 1000000 && (lScale % 1000000) == 0)
      string = L"1:" + ToString(lScale / 1000000) + L" M";

    else if (lScale >= 1000 && (lScale % 1000 == 0))
      string = L"1:" + ToString(lScale / 1000) + L" K";

    else {
      std::wstring tmp = ToString(lScale);

      // add commas
      if (tmp.size()) {
        std::wstring strScale;
        int count = 0;
        for (int i = tmp.size() - 1; i >= 0; --i) {
          strScale = tmp[i] + strScale;
          count++;

          if ((count % 3) == 0 && i != 0) strScale = L"," + strScale;
        }

        string = L"1:" + strScale;
      }
    }
  } else if (ScaleUnitsEnum == MAP_SCALE_DENOMINATOR) {
    string = L"1:" + ToString(static_cast<long>(dScale));
  } else {
    // If the scale contains a decimal, then return a string containing
    // three decimal places
    if (fabs(floor(dScale) - dScale) > DBL_EPSILON)
      string = ToString(dScale, 3) + L" " + ToString(ScaleUnitsEnum);

    // Otherwise, return the string with no decimal places
    else
      string = ToString(dScale, 0) + L" " + ToString(ScaleUnitsEnum);
  }

  return string;
}

// convert units to a string
std::wstring MapSeriesStringConverter::ToString(
    MapScaleUnitsEnum ScaleUnitsEnum) {
  std::wstring string;

  switch (ScaleUnitsEnum) {
    case MAP_SCALE_NM: string = L"NM"; break;
    case MAP_SCALE_MILE: string = L"mile"; break;
    case MAP_SCALE_KILOMETER: string = L"km"; break;
    case MAP_SCALE_METERS: string = L"meter"; break;
    case MAP_SCALE_YARDS: string = L"yard"; break;
    case MAP_SCALE_FEET: string = L"foot"; break;
    case MAP_SCALE_INCHES: string = L"inch"; break;
    case MAP_SCALE_ARC_DEGREES: string = L"arc deg"; break;
    case MAP_SCALE_ARC_MINUTES: string = L"arc min"; break;
    case MAP_SCALE_ARC_SECONDS: string = L"arc sec"; break;
    default:
      string = L"Invalid Units";
  };

  return string;
}

MapScaleUnitsEnum MapSeriesStringConverter::ToUnits(
    const std::wstring& strUnits) {
  if (strUnits == L"NM")
    return MAP_SCALE_NM;
  else if (strUnits == L"mile")
    return MAP_SCALE_MILE;
  else if (strUnits == L"km")
    return MAP_SCALE_KILOMETER;
  else if (strUnits == L"meter")
    return MAP_SCALE_METERS;
  else if (strUnits == L"yard")
    return MAP_SCALE_YARDS;
  else if (strUnits == L"foot")
    return MAP_SCALE_FEET;
  else if (strUnits == L"inch")
    return MAP_SCALE_INCHES;
  else if (strUnits == L"arc deg")
    return MAP_SCALE_ARC_DEGREES;
  else if (strUnits == L"arc min")
    return MAP_SCALE_ARC_MINUTES;
  else if (strUnits == L"arc sec")
    return MAP_SCALE_ARC_SECONDS;

  // unknown units string (debug assert on Windows; NM fallback preserved)
  return MAP_SCALE_NM;
}

// convert long to a string
std::wstring MapSeriesStringConverter::ToString(long lNum) {
  wchar_t str[33];  // max num of chars _itow copies is 33 (MSDN docs)
  swprintf(str, 33, L"%ld", lNum);
  return std::wstring(str);
}

// convert double to a string with specified number of decimals
std::wstring MapSeriesStringConverter::ToString(double dNum,
                                                long lNumDecimals) {
  static const size_t STR_BUF_SIZE = 33, FMT_BUF_SIZE = 10;
  wchar_t str[STR_BUF_SIZE];
  wchar_t format_str[FMT_BUF_SIZE];
  swprintf(format_str, FMT_BUF_SIZE, L"%%0.%ldf", lNumDecimals);
  swprintf(str, STR_BUF_SIZE, format_str, dNum);
  return std::wstring(str);
}

bool MapSeriesStringConverter::ToMapScale(std::wstring strScale, double* pScale,
                                          MapScaleUnitsEnum* pScaleUnitsEnum) {
  *pScale = 0;
  *pScaleUnitsEnum = MAP_SCALE_DENOMINATOR;

  // need to get rid of any commas in the string (e.g "1:17,000" -> "1:17000")
  std::wstring tmp = strScale;
  strScale = L"";
  for (size_t i = 0; i < tmp.size(); ++i) {
    if (tmp[i] != ',') strScale += tmp[i];
  }

  if (strScale.size() == 0) return false;

  // World is a special case
  if (strScale == L"World") {
    *pScale = 1;
    *pScaleUnitsEnum = MAP_SCALE_WORLD;

    return true;
  } else {
    int count;
    int one;
    double denominator;
    wchar_t M_or_K;

    // see if this is a chart scale, e.g., 1:10 M, 1:10 K, or 1:10
    count = swscanf(strScale.c_str(), L"%d:%lf %lc", &one, &denominator,
                    &M_or_K);
    if (count >= 2) {
      // always 1:denominator, where denominator is between 1 and 999
      if (one == 1 && 0 < denominator) {
        *pScaleUnitsEnum = MAP_SCALE_DENOMINATOR;

        if (count == 2)
          *pScale = denominator;
        else if (M_or_K == L'M')
          *pScale = denominator * 1000000;
        else if (M_or_K == L'K')
          *pScale = denominator * 1000;

        return true;
      }
    }
    // see if this is a resolution string
    else if (strScale.size() < 80) {
      double resolution;
      static const size_t UNIT_STRING_BUF_SIZE = 80;
      wchar_t unit_stringA[UNIT_STRING_BUF_SIZE];
      wchar_t unit_stringB[UNIT_STRING_BUF_SIZE];

      // MSVC's wide %s means wchar_t*; standard swscanf needs %ls
      int count = swscanf(strScale.c_str(), L"%lf %79ls %79ls", &resolution,
                          unit_stringA, unit_stringB);
      if (count == 2) {
        *pScale = floor(static_cast<double>(resolution) * 10000000) /
                  10000000.0;
        *pScaleUnitsEnum = ToUnits(unit_stringA);

        return true;
      } else if (count == 3) {
        *pScale = floor(static_cast<double>(resolution) * 10000000) /
                  10000000.0;
        *pScaleUnitsEnum = ToUnits(std::wstring(unit_stringA) + L" " +
                                   std::wstring(unit_stringB));

        return true;
      }
    }
  }

  return false;
}

}  // namespace fv
