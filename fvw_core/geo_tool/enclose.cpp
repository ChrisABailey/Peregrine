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



/*------------------------------------------------------------------
-  FILE NAME:           enclose.c
-  LIBRARY NAME:        geo_tool
-
-  DESCRIPTION:
-
-     These functions return TRUE if the region A encloses region B, and
-  they return FALSE if not.  These functions assume that they are
-  passed valid inputs. 
-
-  PUBLIC FUNCTIONS:
-
-      GEO_enclose_degrees
-      GEO_enclose
-
-  PRIVATE FUNCTIONS: NONE
-
-  STATIC FUNCTIONS: NONE
-
-  PUBLIC VARIABLES: NONE
-
-  PRIVATE VARIABLES: NONE
-
-  REVISION HISTORY:
-       $Log: enclose.cpp $
//Revision 1.1  1995/04/23  16:59:55  vinny
//Initial revision
//
 * Revision 1.1  1994/10/26  09:15:39  gue
 * Initial revision
 * 
 * Revision 1.1  1994/02/28  07:22:48  gue
 * Initial revision
 * 
-------------------------------------------------------------------*/

/*------------------------------------------------------------------
-                            Includes
-------------------------------------------------------------------*/

#include "stdafx.h"

#include "geo_tool.h"


/*------------------------------------------------------------------
-  FUNCTION NAME:        GEO_enclose_degrees
-  PROGRAMMER:           Vincent Sollicito
-  DATE:                 January 1994
-
-  PURPOSE:
-
-      Determine if region A encloses region B.  All coordinates are in degrees.
-
-  PARAMETERS:
-
-      ll_A:        coordinate for lower left corner of region A
-
-      ur_A:        coordinate for upper right corner of region A
-
-      ll_B:        coordinate for lower left corner of region B
-
-      ur_B:        coordinate for upper right corner of region B
-
-  RETURN VALUES:
-
-      TRUE
-      FALSE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      geo_tool.h
-      common.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

boolean_t GEO_enclose_degrees(const d_geo_t& ll_A, const d_geo_t& ur_A, 
   const d_geo_t& ll_B, const d_geo_t& ur_B)
{
   return (GEO_enclose(ll_A.lat, ll_A.lon, ur_A.lat, ur_A.lon,
      ll_B.lat, ll_B.lon, ur_B.lat, ur_B.lon));
}

/*------------------------------------------------------------------
-  FUNCTION NAME:        GEO_enclose
-  PROGRAMMER:           Vincent Sollicito
-  DATE:                 January 1994
-
-  PURPOSE:
-
-      Determine if region A encloses region B.
-
-  PARAMETERS:
-
-      ll_A_lat:    latitude of lower left corner of region A
-
-      ll_A_lon:    longitude of lower left corner of region A
-
-      ur_A_lat:    latitude of upper right corner of region A
-
-      ur_A_lon:    longitude of upper right corner of region A
-
-      ll_B_lat:    latitude of lower left corner of region B
-
-      ll_B_lon:    longitude of lower left corner of region B
-
-      ur_B_lat:    latitude of upper right corner of region B
-
-      ur_B_lon:    longitude of upper right corner of region B
-
-  RETURN VALUES:
-
-      TRUE
-      FALSE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      geo_tool.h
-      common.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

boolean_t GEO_enclose(double ll_A_lat, double ll_A_lon,
   double ur_A_lat, double ur_A_lon,
   double ll_B_lat, double ll_B_lon,
   double ur_B_lat, double ur_B_lon)
{
   /* region B completely below A */
   if (ur_B_lat <= ll_A_lat)
      return FALSE;

   /* region B completely above A */
   if (ll_B_lat >= ur_A_lat)
      return FALSE;

   /* if northern edge of B goes outside of A */
   if (ur_B_lat > ur_A_lat)
      return FALSE;

   /* if southern edge of B goes outside of A */
   if (ll_B_lat < ll_A_lat)
      return FALSE;

   /* if right edge of region B is outside of region A */
   if (GEO_lon_in_range(ll_A_lon, ur_A_lon, ur_B_lon) == FALSE) 
      return FALSE;

   /* if left edge of region B is outside of region A */
   if (GEO_lon_in_range(ll_A_lon, ur_A_lon, ll_B_lon) == FALSE) 
      return FALSE;

   return TRUE;
}

// This function returns TRUE if the circular region A encloses the
// rectangular region B.  This function assumes that it is passed
// valid inputs.  The radius is in meters.
boolean_t GEO_enclose_degrees(const d_geo_t& center_A, double radius_A,
   boolean_t great_circle_not_rhumb_line,
   const d_geo_t& ll_B, const d_geo_t& ur_B)
{
   double range, bearing;

   // get the range to the lower left corner of the map
   if (GEO_calc_range_and_bearing(center_A, ll_B, range, bearing,
      great_circle_not_rhumb_line) != SUCCESS)
   {
      ASSERT(0);
      return FALSE;
   }

   // if this point is outside of the circle, the circle does not bound B
   if (range >= radius_A)
      return FALSE;

   // get the range to the upper right corner of the map
   if (GEO_calc_range_and_bearing(center_A, ur_B, range, bearing,
      great_circle_not_rhumb_line) != SUCCESS)
   {
      ASSERT(0);
      return FALSE;
   }

   // if this point is outside of the circle, the circle does not bound B
   if (range >= radius_A)
      return FALSE;

   // need another point for the lower right and upper left corners
   d_geo_t point = {ll_B.lat, ur_B.lon};

   // get the range to the upper right corner of the map
   if (GEO_calc_range_and_bearing(center_A, point, range, bearing,
      great_circle_not_rhumb_line) != SUCCESS)
   {
      ASSERT(0);
      return FALSE;
   }

   // if this point is outside of the circle, the circle does not bound B
   if (range >= radius_A)
      return FALSE;

   point.lat = ur_B.lat;
   point.lon = ll_B.lon;

   // get the range to the upper right corner of the map
   if (GEO_calc_range_and_bearing(center_A, point, range, bearing,
      great_circle_not_rhumb_line) != SUCCESS)
   {
      ASSERT(0);
      return FALSE;
   }

   // if this point is outside of the circle, the circle does not bound B
   if (range >= radius_A)
      return FALSE;

   return TRUE;
}

// This functions return TRUE if the rectangular region A encloses the
// circular region B.  This function assumes that it is passed valid
// inputs.  The radius is in meters.
boolean_t GEO_enclose_degrees(const d_geo_t& ll_A, const d_geo_t& ur_A,
   const d_geo_t& center_B, double radius_B, boolean_t great_circle_not_rhumb_line)
{
   d_geo_t p000;
   d_geo_t p090;
   d_geo_t p180;
   d_geo_t p270;

   // Get the lat-lon at 0, 90, 180, and 270.
   if (GEO_calc_end_point(center_B, radius_B, 000.0, p000, 
      great_circle_not_rhumb_line) == SUCCESS &&
      GEO_calc_end_point(center_B, radius_B, 090.0, p090, 
      great_circle_not_rhumb_line) == SUCCESS &&
      GEO_calc_end_point(center_B, radius_B, 180.0, p180, 
      great_circle_not_rhumb_line) == SUCCESS &&
      GEO_calc_end_point(center_B, radius_B, 270.0, p270, 
      great_circle_not_rhumb_line) == SUCCESS)
   {
      degrees_t min_lat, max_lat;

      // This is computing the number of degrees of latitude crossed by
      // a radius_A length line at a due north course.  By using the
      // semi-minor axis radius, the value calculated here is guaranteed
      // to be larger than the precise value.  Since the semi-major axis
      // is less than 1% bigger than the semi-minor axis, this value will
      // be less than 1% larger than the true value, but a lot simpler
      // to calculate.
      double radius_in_deg_lat = 360.0 * radius_B / WGS84_b_METERS;

      // If the circle bounds the north pole then you must use the north
      // pole as max_lat.
      if (center_B.lat > (90.0 - radius_in_deg_lat))
         max_lat = 90.0;
      else
         max_lat = p000.lat;

      // If the circle bounds the south pole then you must use the south
      // pole as min_lat.
      if (center_B.lat < (radius_in_deg_lat - 90.0))
         min_lat = -90.0;
      else
         min_lat = p180.lat;

      // Use the max-lat, min-lat, western lon, and eastern lon to define the
      // geo-bounds for the circle, and perform the enclosure test.  The region
      // A encloses the circle B if and only if it encloses the geo-bounds on
      // B.
      return GEO_enclose(ll_A.lat, ll_A.lon, ur_A.lat, ur_A.lon,
         min_lat, p270.lon, max_lat, p090.lon);
   }

   // Invalid Inputs
   ASSERT(0);

   return FALSE;  // in case of bogus inputs, say that A does not enclose B
}

// This functions return TRUE if the rectangular region A encloses the
// circular region B.  This function assumes that it is passed valid
// inputs.  The radius is in meters.
boolean_t GEO_enclose(degrees_t ll_A_lat, degrees_t ll_A_lon, 
   degrees_t ur_A_lat, degrees_t ur_A_lon,
   degrees_t center_lat_B, degrees_t center_lon_B,
   double radius_B, boolean_t great_circle_not_rhumb_line)
{
   d_geo_t ll_A = {ll_A_lat, ll_A_lon};
   d_geo_t ur_A = {ur_A_lat, ur_A_lon};
   d_geo_t center_B = {center_lat_B, center_lat_B};

   return GEO_enclose_degrees(ll_A, ur_A, center_B, radius_B, 
      great_circle_not_rhumb_line);
}
