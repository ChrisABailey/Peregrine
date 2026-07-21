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

// fv_map_scale_util.h — portable extraction of CMapScaleUtil
// (fvw_core/MapScaleUtil/MapScaleUtil.cpp). The ATL COM wrapper stays
// Windows-only. Methods keep the original HRESULT-shaped returns
// (S_OK / E_INVALIDARG via fv_compat.h) so the Windows wrapper can delegate
// to this class later without translation.

#pragma once

#include "fv_compat.h"
#include "fv_map_enums.h"

namespace fv {

class MapScaleUtil {
 public:
  // Converts a resolution in the given units to a map-scale denominator
  // relative to 1:1M CADRG. Returns E_INVALIDARG for dResolution <= 0.
  HRESULT ResolutionToScale(double dResolution,
                            MapScaleUnitsEnum eResolutionUnits,
                            double* pScale);

  // Degrees of latitude/longitude covered by one pixel at the given latitude
  // and scale. eScaleUnits must not be MAP_SCALE_WORLD.
  HRESULT GetDegreesPerPixel(double latitude, double dScale,
                             MapScaleUnitsEnum eScaleUnits,
                             double* degrees_lat_per_pixel,
                             double* degrees_lon_per_pixel);

  // Degrees per pixel for a world-scale display on a surface of the given
  // pixel dimensions.
  HRESULT GetDegreesPerPixelWorld(long surface_width, long surface_height,
                                  double* degrees_lat_per_pixel,
                                  double* degrees_lon_per_pixel);

 private:
  HRESULT ResolutionToDegreesLatPerPix(double dResolution,
                                       MapScaleUnitsEnum eResolutionUnits,
                                       double* pDegreesLatPerPix);
  // Returns E_INVALIDARG for unknown units (the COM original threw).
  HRESULT ConvertResolutionToMeters(double dResolution,
                                    MapScaleUnitsEnum eResolutionUnits,
                                    double* pMeters);
  bool ResolutionToDegrees(double dCenterLat, double dResolutionMeters,
                           double& dDegreesLatPerPix,
                           double& dDegreesLonPerPix);
  bool CalcRangeAndBearing(double dStartLat, double dStartLon, double dEndLat,
                           double dEndLon, double* dDistance,
                           double* dBearing);
  double GetNominalDegreesLatPerPixel(double dScale);
};

}  // namespace fv
