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
-  FILE NAME:          overlap.c
-  LIBRARY NAME:       geo_tool
-
-  DESCRIPTION:
-
-      These functions return a 1 if the two regions intersect and they return
-  a 0 if they have no intersection. These functions assume that the are
-  passed valid inputs. 
-
-  PUBLIC FUNCTIONS:
-
-      GEO_intersect_radians
-      GEO_intersect_degrees
-      GEO_intersect_minutes
-      GEO_intersect
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
-       $Log: overlap.cpp $
//Revision 1.1  1995/04/23  17:00:08  vinny
//Initial revision
//
 * Revision 1.1  1994/10/26  09:16:12  gue
 * Initial revision
 * 
 * Revision 1.1  1994/02/28  07:25:03  gue
 * Initial revision
 * 
-------------------------------------------------------------------*/

/*------------------------------------------------------------------
-                            Includes
-------------------------------------------------------------------*/

#include "stdafx.h"

#include "geo_tool.h"

/*------------------------------------------------------------------
-  FUNCTION NAME:      GEO_intersect_radians
-  PROGRAMMER:         Vincent Sollicito
-  DATE:               January 1994
-
-  PURPOSE:
-
-      Determine if region A and region B intersect.  All coordinates are
-  in radians.
-
-  PARAMETERS:
-
-      ll_A:        coordinate of lower left corner of region A
-
-      ur_A:        coordinate of upper right corner of region A
-
-      ll_B:        coordinate of lower left corner of region B
-
-      ur_B:        coordinate of upper right corner of region B
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

boolean_t GEO_intersect_radians(r_geo_t ll_A, r_geo_t ur_A, 
   r_geo_t ll_B, r_geo_t ur_B)
{
   return (GEO_intersect(ll_A.lat, ll_A.lon, ur_A.lat, ur_A.lon,
      ll_B.lat, ll_B.lon, ur_B.lat, ur_B.lon));
}

/*------------------------------------------------------------------
-  FUNCTION NAME:      GEO_intersect_degrees
-  PROGRAMMER:         Vincent Sollicito
-  DATE:               January 1994
-
-  PURPOSE:
-
-      Determine if region A and region B intersect.  All coordinates are
-  in degrees.
-
-  PARAMETERS:
-
-      ll_A:        coordinate of lower left corner of region A
-
-      ur_A:        coordinate of upper right corner of region A
-
-      ll_B:        coordinate of lower left corner of region B
-
-      ur_B:        coordinate of upper right corner of region B
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

boolean_t GEO_intersect_degrees(const d_geo_t& ll_A, const d_geo_t& ur_A, 
   const d_geo_t& ll_B, const d_geo_t& ur_B)
{
   return (GEO_intersect(ll_A.lat, ll_A.lon, ur_A.lat, ur_A.lon,
      ll_B.lat, ll_B.lon, ur_B.lat, ur_B.lon));
}

/*------------------------------------------------------------------
-  FUNCTION NAME:      GEO_intersect_minutes
-  PROGRAMMER:         Vincent Sollicito
-  DATE:               January 1994
-
-  PURPOSE:
-
-      Determine if region A and region B intersect.  All coordinates are
-  in minutes.
-
-  PARAMETERS:
-
-      ll_A:        coordinate of lower left corner of region A
-
-      ur_A:        coordinate of upper right corner of region A
-
-      ll_B:        coordinate of lower left corner of region B
-
-      ur_B:        coordinate of upper right corner of region B
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

boolean_t GEO_intersect_minutes(m_geo_t ll_A, m_geo_t ur_A, 
   m_geo_t ll_B, m_geo_t ur_B)
{
   return (GEO_intersect(ll_A.lat, ll_A.lon, ur_A.lat, ur_A.lon,
      ll_B.lat, ll_B.lon, ur_B.lat, ur_B.lon));
}

/*------------------------------------------------------------------
-  FUNCTION NAME:      GEO_intersect
-  PROGRAMMER:         Vincent Sollicito
-  DATE:               January 1994
-
-  PURPOSE:
-
-      Determine if region A and region B intersect.
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

boolean_t GEO_intersect(double ll_A_lat, double ll_A_lon,
   double ur_A_lat, double ur_A_lon,
   double ll_B_lat, double ll_B_lon,
   double ur_B_lat, double ur_B_lon)
{
   if (ur_A_lat <= ll_B_lat)   /* region A completely below B */
      return FALSE;

   if (ll_A_lat >= ur_B_lat)   /* region A completely above B */
      return FALSE;

   /* if either region goes around the world, intersection exists */
   if (ll_A_lon == ur_A_lon || ll_B_lon == ur_B_lon)
      return TRUE;

   /* if right edge of region B is in region A */
   if (GEO_lon_in_range(ll_A_lon, ur_A_lon, ur_B_lon) == TRUE) 
      return TRUE;

   /* if left edge of region B is in region A */
   if (GEO_lon_in_range(ll_A_lon, ur_A_lon, ll_B_lon) == TRUE) 
      return TRUE;

   /* if right edge of region A is in region B */ 
   if (GEO_lon_in_range(ll_B_lon, ur_B_lon, ur_A_lon) == TRUE) 
      return TRUE;

   /* if left edge of region A is in region B */
   if (GEO_lon_in_range(ll_B_lon, ur_B_lon, ll_A_lon) == TRUE)
      return TRUE;

   return FALSE;
}

// This function returns TRUE if the circular region A intersects the
// rectangular region B.  This function assumes that it is passed
// valid inputs.  The radius is in meters.
boolean_t GEO_intersect_degrees(const d_geo_t& center_A, double radius_A, 
   boolean_t great_circle_not_rhumb_line, const d_geo_t& ll_B, const d_geo_t& ur_B)
{
   d_geo_t p000;
   d_geo_t p090;
   d_geo_t p180;
   d_geo_t p270;

   // Get the lat-lon at 0, 90, 180, and 270.
   if (GEO_calc_end_point(center_A, radius_A, 000.0, p000, 
      great_circle_not_rhumb_line) == SUCCESS &&
      GEO_calc_end_point(center_A, radius_A, 090.0, p090, 
      great_circle_not_rhumb_line) == SUCCESS &&
      GEO_calc_end_point(center_A, radius_A, 180.0, p180, 
      great_circle_not_rhumb_line) == SUCCESS &&
      GEO_calc_end_point(center_A, radius_A, 270.0, p270, 
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
      double radius_in_deg_lat = 360.0 * radius_A / WGS84_b_METERS;

      // If the circle bounds the north pole then you must use the north
      // pole as max_lat.
      if (center_A.lat > (90.0 - radius_in_deg_lat))
         max_lat = 90.0;
      else
         max_lat = p000.lat;

      // If the circle bounds the south pole then you must use the south
      // pole as min_lat.
      if (center_A.lat < (radius_in_deg_lat - 90.0))
         min_lat = -90.0;
      else
         min_lat = p180.lat;

      // Use the max-lat, min-lat, western lon, and eastern lon to define the
      // geo-bounds for the circle, and perform the quick reject test.
      if (!GEO_intersect(min_lat, p270.lon, max_lat, p090.lon,
         ll_B.lat, ll_B.lon, ur_B.lat, ur_B.lon))
         return FALSE;

      // A more precise accept could be added, but this is probably close
      // enough for now.
      return TRUE;
   }

   // Invalid Inputs
   ASSERT(0);

   return FALSE;  // in case of bogus inputs, reject
}

// This function returns TRUE if the circular region A intersects the
// rectangular region B.  This function assumes that it is passed
// valid inputs.  The radius is in meters.
boolean_t GEO_intersect(degrees_t center_lat_A, degrees_t center_lon_A,
   double radius_A, boolean_t great_circle_not_rhumb_line, 
   degrees_t ll_B_lat, degrees_t ll_B_lon,
   degrees_t ur_B_lat, degrees_t ur_B_lon)
{
   d_geo_t center_A = {center_lat_A, center_lon_A};
   d_geo_t ll_B = {ll_B_lat, ll_B_lon};
   d_geo_t ur_B = {ur_B_lat, ur_B_lon};

   return GEO_intersect_degrees(center_A, radius_A, 
      great_circle_not_rhumb_line, ll_B, ur_B);
}
