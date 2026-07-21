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

// *** RpfZoneScales.h ***

// Determining the lat/lon coords from an RPF file name is a rather complex process.
// I ported this code over from the "classic" FalconView files: catalog\catalog.cpp, rpf\info.cpp
// (Curt Dixon, 1/2003)

#pragma once

#include "common.h"
#include "mapscales.h"
#include <vector>

//#define ATLASSERT ASSERT

//typedef double DEGREES;

extern const int CADRG_FRAME_WIDTH_IN_PIX;		  // frame width in pixels
extern const int CADRG_FRAME_HEIGHT_IN_PIX;		  // frame height in pixels
extern const int NUM_CADRG_SCALES;
extern const double WORLD_DEG;


const int NUM_NON_POLAR_ARC_ZONES_IN_HEMISPHERE = 8;
const int NUM_ARC_ZONES_IN_HEMISPHERE = 9;
const int CADRG_POLAR_ZONE_INDEX = 8;

#ifndef DEGREES
#define DEGREES double
#endif

// To get the max lat :
// max_lat = (90.0 / ns_pixel_constant) * 1536

class CArcZone
{
public:
	bool	m_bUpperHemisphere;
	int		m_iZoneIndex;		// 0 -> NUM_NON_POLAR_ARC_ZONES_IN_HEMISPHERE

	DEGREES GetEquatorwardBoundary(void) const
	{
		return (m_bUpperHemisphere ? m_ZoneBoundaries[m_iZoneIndex] : -m_ZoneBoundaries[m_iZoneIndex]);
	}
	DEGREES GetPolewardBoundary(void) const
	{
		return (m_bUpperHemisphere ? m_ZoneBoundaries[m_iZoneIndex + 1] : -m_ZoneBoundaries[m_iZoneIndex + 1]);
	}

	DEGREES GetPolewardExtent(double scale, MapScaleUnitsEnum eScaleUnits);
	DEGREES GetEquatorwardExtent(double scale_denom, MapScaleUnitsEnum eScaleUnits);

   BOOL SetByLatitude(DEGREES latitude);

	BOOL SetFromFileName(std::wstring &strFileName);

	BOOL SetByLetterID(char letter_id);
	char GetLetterID();

	BOOL CalculateDegreesPerPixel(double scale_denominator, MapScaleUnitsEnum eScaleUnits, DEGREES *dpp_lat, DEGREES *dpp_lon);

	BOOL CalculateNumRowsCols(double scale, MapScaleUnitsEnum eUnits, int *rows, int *cols);

	double calc_n_s_pix_const(double scale, MapScaleUnitsEnum eScaleUnits);
	double calc_e_w_pix_const(double scale, MapScaleUnitsEnum eScaleUnits);
	double calc_polar_pix_const(double scale_denominator);

	double get_equatorward_zone_boundary();
	double get_actual_equatorward_zone_extent(double scale, MapScaleUnitsEnum eScaleUnits);
	double calc_lat_frame_rows(int scale, MapScaleUnitsEnum eScaleUnits);

private:

	static const DEGREES m_ZoneBoundaries[NUM_ARC_ZONES_IN_HEMISPHERE + 1];
};

