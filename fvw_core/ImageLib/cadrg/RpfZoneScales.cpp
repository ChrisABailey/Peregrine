// Copyright (c) 1994-2012 Georgia Tech Research Corporation, Atlanta, GA
// This file is part of FalconView(R).

// FalconView(R) is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// FalconView(R) is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public License
// along with FalconView(R).  If not, see <http://www.gnu.org/licenses/>.

// FalconView(R) is a registered trademark of Georgia Tech Research Corporation.

// *** RpfZoneScales.cpp ***
#include "stdafx.h"
#include "RpfZoneScales.h"

#include <math.h>

// Determining the lat/lon coords from an RPF file name is a rather complex process.
// I ported this code over from the "classic" FalconView files: catalog\catalog.cpp, rpf\info.cpp
// (Curt Dixon, 1/2003)

const int		CADRG_FRAME_WIDTH_IN_PIX = 1536;		  // frame width in pixels
const int		CADRG_FRAME_HEIGHT_IN_PIX = 1536;		  // frame height in pixels
const DEGREES	WORLD_DEG = 360.0;

const int CADRG_SF_WIDTH = 256;				// CADRG subframe width
const int CADRG_FRAME_WIDTH_IN_SF = 6;		// frame width in subframes


// To get the max lat :
// max_lat = (90.0 / ns_pixel_constant) * 1536
	
const DEGREES CArcZone::m_ZoneBoundaries[NUM_ARC_ZONES_IN_HEMISPHERE + 1] = 
{
	0.0, 32.0, 48.0, 56.0, 64.0, 68.0, 72.0, 76.0, 80.0, 90.0
};


DEGREES CArcZone::GetPolewardExtent(double scale, MapScaleUnitsEnum eScaleUnits)
{
   double dLatPixConst = calc_n_s_pix_const(scale, eScaleUnits);

	// determine the actual poleward extent of a zone as described in the CADRG standard

	/*
	*  determine the number of frames needed to reach the nominal zone boundary.
	*  This is the following equation rounded up to the nearest integer:
	*             pixels                frame
	*             ------  *  latitude * ------
	*             degrees               pixels
	*/

   DEGREES abs_nominal_poleward_zone_boundary = fabs(GetPolewardBoundary());

   double pixels_per_degree = dLatPixConst / 90.0;
   
   int number_of_frames = (int) 
     ceil(pixels_per_degree * abs_nominal_poleward_zone_boundary * (1.0 / CADRG_FRAME_HEIGHT_IN_PIX));

   /*
    *  use the number of frame to get the poleward zone extent =
    *
    *                         pixels   degrees
    *      number of frames * ------ * -------
    *                         frame     pixel
    */
	DEGREES poleward_zone_lat = 
		number_of_frames * CADRG_FRAME_HEIGHT_IN_PIX * (1.0 / pixels_per_degree);

	if (!m_bUpperHemisphere)
		poleward_zone_lat = -poleward_zone_lat;

   return poleward_zone_lat;
}


DEGREES CArcZone::GetEquatorwardExtent(double scale, MapScaleUnitsEnum eScaleUnits)
{
   double dLatPixConst = calc_n_s_pix_const(scale, eScaleUnits);

	// determine the actual equatorward extent of a zone as described in the CADRG standard

	/*
	*  determine the number of frames needed to reach the nominal zone boundary.
	*  This is the following equation rounded down to the nearest integer:
	*             pixels                frame
	*             ------  *  latitude * ------
	*             degrees               pixels
	*/

	DEGREES abs_nominal_equatorward_zone_boundary = fabs(GetEquatorwardBoundary());

	double pixels_per_degree = dLatPixConst / 90.0;

	int number_of_frames = (int) 
		floor(pixels_per_degree * abs_nominal_equatorward_zone_boundary * (1.0 / CADRG_FRAME_HEIGHT_IN_PIX));

	/*
	*  use the number of frame to get the poleward zone extent =
	*
	*                         pixels   degrees
	*      number of frames * ------ * -------
	*                         frame     pixel
	*/
	DEGREES	equatorward_zone_lat = 
		number_of_frames * CADRG_FRAME_HEIGHT_IN_PIX * (1.0 / pixels_per_degree);

	if (!m_bUpperHemisphere)
		equatorward_zone_lat = -equatorward_zone_lat;

	return equatorward_zone_lat;	
}

// returns TRUE if the zone index / hemisphere are correctly set from the 
// given latitude, o.w. FALSE
BOOL CArcZone::SetByLatitude(DEGREES latitude)
{
	/*
    *  find the zone for the latitude
    */

   const DEGREES abs_lat = fabs(latitude);
   const bool upper_hemi = latitude >= 0.0 ? true : false;

   /*
    *  NOTE: start at index 1 since the poleward boundary is being checked
    */
   int zone_idx;
   for (zone_idx=1; zone_idx<=NUM_ARC_ZONES_IN_HEMISPHERE; zone_idx++)
   {
      /*
       *  check the poleward zone boundary
       *
       *  make sure to use abs_lat here instead of latitude
       */
      if (abs_lat <= m_ZoneBoundaries[zone_idx])
      {
         //return set(zone_idx, upper_hemi);
			m_iZoneIndex = zone_idx - 1;
			m_bUpperHemisphere = upper_hemi;

			return TRUE;
      }
   }

   // zone not found
   return FALSE;
}

BOOL CArcZone::SetFromFileName(std::wstring &strFileName)
{
	wchar_t zone_char = strFileName[11];	// zone is last char in 8.3 filename
   
   if (isdigit(zone_char))
	{
		m_bUpperHemisphere = true;
		m_iZoneIndex = zone_char - '1';
	}
	else
	{
		m_bUpperHemisphere = false;
		
		// there is no zone 'I'
		if ( (zone_char >= 'a') && (zone_char <= 'h') )
			m_iZoneIndex = zone_char - 'a';
		else if (zone_char == 'j')
			m_iZoneIndex = zone_char - 'a' - 1;
		else
			return FALSE;		// invalid zone code
	}
	return TRUE;
}

BOOL CArcZone::CalculateDegreesPerPixel(double scale, MapScaleUnitsEnum eScaleUnits, DEGREES *dpp_lat, DEGREES *dpp_lon)
{
	if (m_iZoneIndex == CADRG_POLAR_ZONE_INDEX)
	{
		*dpp_lat = *dpp_lon = 360.0/calc_polar_pix_const(scale);
	}
	else
	{
		*dpp_lat = 90.0/calc_n_s_pix_const(scale, eScaleUnits);
		*dpp_lon = 360.0/calc_e_w_pix_const(scale, eScaleUnits);
	}

   return TRUE;
}

BOOL CArcZone::CalculateNumRowsCols(double scale, MapScaleUnitsEnum eScaleUnits, int *rows, int *cols)
{
	if (m_iZoneIndex == CADRG_POLAR_ZONE_INDEX)
	{
		int num_frames = static_cast<int>(ceil(calc_polar_pix_const(scale) * (20.0 / 360.0) / CADRG_SF_WIDTH / CADRG_FRAME_WIDTH_IN_SF));
		
		// spec says to round up to the next odd number of frames
		if (num_frames % 2 == 0) 
			num_frames++;

		*rows = num_frames;
		*cols = num_frames;
	}
	else
	{
		*rows = (int) ceil(calc_lat_frame_rows(static_cast<int>(scale), eScaleUnits));
		*cols = (int) ceil(calc_e_w_pix_const(scale, eScaleUnits) / CADRG_FRAME_WIDTH_IN_PIX);
	}

	return TRUE;
}

char CArcZone::GetLetterID()
{
	if (m_bUpperHemisphere)
       return (m_iZoneIndex + 1) + 48;
   else
   {
      if (m_iZoneIndex == 8)
         return 'J';
      else
         return (m_iZoneIndex + 1) + 64; 
   }

	// invalid zone index
	return '*';
}

BOOL CArcZone::SetByLetterID(char letter_id)
{
	if (letter_id >= 'a' && letter_id <= 'j')
      letter_id = toupper(letter_id);

   if (letter_id >= '1' && letter_id <= '9')
   {
      m_bUpperHemisphere = true;
      m_iZoneIndex = letter_id - 48;
   }
   else if (letter_id >= 'A' && letter_id <= 'J' && letter_id != 'I')
   {
      m_bUpperHemisphere = false;

      if (letter_id == 'J')
         m_iZoneIndex = 9;
      else 
         m_iZoneIndex = letter_id - 64;
   }
   else
      return FALSE;

	// zone index is zero based
	m_iZoneIndex--;

   return TRUE;
}

double CArcZone::calc_n_s_pix_const(double scale, MapScaleUnitsEnum eScaleUnits)
{
   double n_s_pix_const = 0.0;

	// B parameter from MIL-A-89007 Appendix 70, Table 3
   const double B_param = 400384;

   if (eScaleUnits == MAP_SCALE_DENOMINATOR)
   {
      
      // determine the pixel constant in ADRG for the given scale.  This
      // is the B parameter multiplied by a scale factor (1000000/scale) and
      // rounded up to the nearest multiple of 512 pixels
      double ADRG_pix_const = ceil(B_param * (1000000.0/scale) / 512.0) * 512.0;
      
      // The ADRG value represents 360 degrees, whereas the corresponding
      // CADRG value is represents 90 degrees.  The CADRG value is determined
      // by dividing the ADRG value by 4 to represent 90 degrees, dividing
      // by the 150/100micro spatial downsampling ratio, and rounding to the
      // nearest multiple of 256 pixels
      n_s_pix_const = (int)((ADRG_pix_const/4.0) / (150.0/100.0) / 256.0 + 0.5) * 256.0;
   }
   else if (eScaleUnits == MAP_SCALE_METERS)
   {
      // To determine the north-south pixel constant for CIB, the "B" parameter
      // is multiplied by a scale factor (1,000,000*S), where "S" is the scale 
      // corresponding the resolution of the image. S is equal to (100 x 10-6 / GSD). This 
      // value is rounded up to the next highest multiple of 512 pixels
      double S = 0.0001 / scale;
      double ADRG_pix_const = ceil(B_param * (1000000 * S) / 512.0) * 512.0;

      // The CIB pixel constant is calculated by dividing this value by 4 to 
      // represent 90° instead of 360° and rounding to the nearest multiple of 
      // 256 pixels (the size of a subframe)
      n_s_pix_const = (int)((ADRG_pix_const/4.0) / 256.0 + 0.5) * 256.0;
   }
   else
   {
      // invalid units for a CADRG or CIB frame
      ATLASSERT(0);
   }

   return n_s_pix_const;
}

double CArcZone::calc_e_w_pix_const(double scale, MapScaleUnitsEnum eScaleUnits)
{
   double e_w_pix_const = 0.0;

	// A parameters from MIL-A-89007 Appendix 70, Table 3
   static const double A_param[9] = { 
         369664,                    // Zone 1,a
         302592,                    // Zone 2,b
         245760,                    // Zone 3,c
         199168,                    // Zone 4,d
         163328,                    // Zone 5,e
         137216,                    // Zone 6,f
         110080,                    // Zone 7,g
         82432,                     // Zone 8,h
         82432,                     // Zone 9,j
   };

	// check if we are at polar zones, and constrain it
	// to the non-polar zones if necessary
	int zone_idx = m_iZoneIndex;
	if ( zone_idx >= NUM_ARC_ZONES_IN_HEMISPHERE || zone_idx < 0 )
	{
		zone_idx = NUM_ARC_ZONES_IN_HEMISPHERE - 1;
	}

   if (eScaleUnits == MAP_SCALE_DENOMINATOR)
   {
      
      // The ADRG pixel e-w pixel constants is determined by multiplying
      // the A-parameter by a scale factor (1000000/scale) and rounded
      // up to the next highest multiple of 512 pixels

      
      double ADRG_pix_const = ceil(A_param[zone_idx] * (1000000.0/scale) / 512.0) * 512.0;
      
      // The CADRG values are determined by dividing the ADRG value by 
      // the spatial downsampling ratio (150u/100u) and rounding to the
      // nearest multiple of 256 pixels
      e_w_pix_const = (int)(ADRG_pix_const / (150.0/100.0) / 256.0 + 0.5) * 256.0;
   }
   else if (eScaleUnits == MAP_SCALE_METERS)
   {
      // The east-west pixel constant for CIB is calculated similar to n-s pix const, using 
      // the "A" constant which represents the number of pixels required to circle the earth 
      // at the midpoint latitudes of each zone. The calculation for the east-west pixel 
      // constant does not include division by 4 because the longitudinal or east-west pixel 
      // constant encircles the earth (360°) at each midpoint latitude.
      double S = 0.0001 / scale;
	
	  // Need to break the following statement 
	  //
	  //   double ADRG_pix_const = ceil(A_param[zone_idx] * (1000000 * S) / 512.0) * 512.0;
	  //
	  // due to rounding problems. When computing ceil(11820.0) the result was 11821.0


	  double ADRG_pix_const = 1000000.0 * S;
	  ADRG_pix_const *= A_param[zone_idx];
	  ADRG_pix_const /= 512.0;
	  ADRG_pix_const = ceil(ADRG_pix_const);
	  ADRG_pix_const *= 512.0;

      // The CIB pixel constant is calculated by dividing this value by 4 to 
      // represent 90° instead of 360° and rounding to the nearest multiple of 
      // 256 pixels (the size of a subframe)
      e_w_pix_const = (int)((ADRG_pix_const) / 256.0 + 0.5) * 256.0;
   }
   else
   {
      // invalid units for a CADRG or CIB frame
      ATLASSERT(0);
   }

   return e_w_pix_const;
}

double CArcZone::calc_polar_pix_const(double scale_denominator)
{
	// B parameter from MIL-A-89007 Appendix 70, Table 3
   const double B_param = 400384;

	// determine the pixel constant in ADRG for the given scale.  This
   // is the B parameter multiplied by a scale factor (1000000/scale) and
   // rounded up to the nearest multiple of 512 pixels
   double ADRG_pix_const = ceil(B_param * (1000000.0/scale_denominator) / 512.0) * 512.0;

	// The CADRG value is found by multiplying the ADRG pixel constant
	// by the ratio 20 degrees / 360 degrees; dividing by the 
	// 150/100micro spatial downsampling ratio, and rounding to the
   // nearest multiple of 512 pixels, and multiplying by the ration
	// 360 degrees/20 degrees
	double POL_pix_const = floor(ADRG_pix_const * (20.0/360.0) / (150.0/100.0) / 512.0 + 0.5) * 512.0 * (360.0/20.0);

	return POL_pix_const;
}

// compute the latitudinal frame rows for each zone
double CArcZone::calc_lat_frame_rows(int scale, MapScaleUnitsEnum eScaleUnits)
{
	// check if we are at polar zones, and constrain it
	// to the non-polar zones if necessary
	int zone_idx = m_iZoneIndex;
	if ( zone_idx >= NUM_NON_POLAR_ARC_ZONES_IN_HEMISPHERE )
	{
		zone_idx = NUM_NON_POLAR_ARC_ZONES_IN_HEMISPHERE - 1;
	}

   // calculate the number of pixels per degree
   const double pix_per_degree_lat = calc_n_s_pix_const(scale, eScaleUnits) / 90.0;
   
   // calculate the number of frames needed to reach each of the nominal
   // zone boundaries.  This number is the pixels per degree lat multiplied
   // by the nominal zone boundary (in degrees), divided by 1536 (the number of
   // pixel rows in a frame)
   double num_frames_poleward = pix_per_degree_lat * m_ZoneBoundaries[zone_idx+1] / 1536.0;
	double num_frames_equatorward = pix_per_degree_lat * m_ZoneBoundaries[zone_idx] / 1536.0;
   
   // the exact poleward zone extent is calculated by multiplying the number of frames 
   // (rounded up) by 1536 and dividing by the number of pixels in a degree of latitude
   double exact_poleward_zone_extent = ceil(num_frames_poleward) * 1536.0 / pix_per_degree_lat;

   // equatorward zone extent is calculated the same way, except we round the number
   // of frames down rather than up
   double exact_equatorward_zone_extent = floor(num_frames_equatorward) * 1536.0 / pix_per_degree_lat;
   
   // The number of latitudinal frames is the difference (in degrees)
   // between the exact poleward extent and exact equatorward zone extent,
   // multiplied by the number of pixels per degree, and divided by 1536 (the
   // number of pixel rows per frame).
   double lat_frame_rows = (exact_poleward_zone_extent - exact_equatorward_zone_extent) *
         pix_per_degree_lat / 1536.0;

	return lat_frame_rows;
}

double CArcZone::get_equatorward_zone_boundary()
{
	return m_bUpperHemisphere ? m_ZoneBoundaries[m_iZoneIndex] :
		-m_ZoneBoundaries[m_iZoneIndex];
}

double CArcZone::get_actual_equatorward_zone_extent(double scale, MapScaleUnitsEnum eScaleUnits)
{
	double equatorward_zone_lat;

   /*
    *  determine the actual equatorward extent of a zone as described in the 
    *  CADRG standard
    */

   /*
    *  determine the number of frames needed to reach the nominal zone boundary.
    *  This is the following equation rounded down to the nearest integer:
    *             pixels                frame
    *             ------  *  latitude * ------
    *             degrees               pixels
    */

   /*
    *  make sure to use the absolute value here
    */
   const double abs_nominal_equatorward_zone_boundary = 
      fabs(get_equatorward_zone_boundary());

   double pixels_per_degree = calc_n_s_pix_const(scale, eScaleUnits)/90.0;
   
   double pixel_height_per_frame = CADRG_FRAME_HEIGHT_IN_PIX;

   int number_of_frames = (int) 
    floor(pixels_per_degree*abs_nominal_equatorward_zone_boundary*(1.0/pixel_height_per_frame));

   /*
    *  use the number of frame to get the poleward zone extent =
    *
    *                         pixels   degrees
    *      number of frames * ------ * -------
    *                         frame     pixel
    */
    equatorward_zone_lat = 
         number_of_frames * pixel_height_per_frame * (1.0/pixels_per_degree);

   if (!m_bUpperHemisphere)
      equatorward_zone_lat = -(equatorward_zone_lat);

   return equatorward_zone_lat;
}

