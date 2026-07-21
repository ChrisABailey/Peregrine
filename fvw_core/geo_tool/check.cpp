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
-  FILE NAME:           check.c
-  LIBRARY NAME:        geo_tool
-
-  DESCRIPTION:
-
-      These functions return 0 if the point p is in the geographic
-  bounds defined by ur and ll, and they return non-zero if it is completely
-  outside the bounds. If the point lies on the bounding rectangle then
-  it is considered inside the bounds and the function returns 0.  The
-  return value contains four bit flags, N, S, E, and W (N is bit 0, 
-  W is bit 3), and they are set to 1 to indicate that the point is north,
-  south, east, or west of the box, respectively.  Bits N and S (0 and 1)
-  are never both 1, and bits E and W (2 and 3) are never both 1.  If E
-  is 1 then the point is east of the lower left longitude by less than
-  180 degrees, and if W is 1 then the point is more than 180 degrees east
-  of the lower left longitude.
-
-  PUBLIC FUNCTIONS:
-
-      GEO_bounds_check_degrees
-
-  PRIVATE FUNCTIONS:
-
-      geo_bounds_check
-
-  STATIC FUNCTIONS: NONE
-
-  PUBLIC VARIABLES: NONE
-
-  PRIVATE VARIABLES: NONE
-
-  REVISION HISTORY:
-       $Log: check.cpp $
//Revision 1.1  1995/04/23  17:00:22  vinny
//Initial revision
//
 * Revision 1.1  1995/02/05  17:28:58  vinny
 * Initial revision
 * 
 * Revision 1.1  1994/10/26  09:15:22  gue
 * Initial revision
 * 
 * Revision 1.1  1994/02/28  07:21:19  gue
 * Initial revision
 * 
-------------------------------------------------------------------*/

/*------------------------------------------------------------------
-                            Includes
-------------------------------------------------------------------*/


#include "stdafx.h"

#include "geo_tool.h"
#include "geo_loc.h"


/*------------------------------------------------------------------
-  FUNCTION NAME:    GEO_bounds_check_degrees
-  PROGRAMMER:       Vincent Sollicito
-  DATE:             January 1994
-
-  PURPOSE:
-
-      Test if a geographic point (given in degrees) lies on or within
-  a bounding rectangle.
-
-  PARAMETERS: (in degrees)
-
-      ll_lat:     latitude of the lower left corner of the bounding rectangle
-
-      ll_lon:     longitude of the lower left corner of the bounding rectangle
-
-      ur_lat:     latitude of the upper right corner of the bounding rectangle
-
-      ur_lon:     longitude of the upper right corner of the bounding rectangle
-
-      p_lat:      latitude of the point to test for
-
-      p_lon:      longitude of the point to test for
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

int GEO_bounds_check_degrees(degrees_t ll_lat, degrees_t ll_lon,
                             degrees_t ur_lat, degrees_t ur_lon,
                             degrees_t p_lat, degrees_t p_lon)
{
   return geo_bounds_check(ll_lat, ll_lon, ur_lat, ur_lon, p_lat, p_lon,
      WORLD_DEG, HALF_WORLD_DEG);
}

int GEO_bounds_check_radians(radians_t ll_lat, radians_t ll_lon,
                             radians_t ur_lat, radians_t ur_lon,
                             radians_t p_lat, radians_t p_lon)
{
   return geo_bounds_check(ll_lat, ll_lon, ur_lat, ur_lon, p_lat, p_lon,
      WORLD_RAD, HALF_WORLD_RAD);
}

 int GEO_bounds_check_minutes(minutes_t ll_lat, minutes_t ll_lon,
                             minutes_t ur_lat, minutes_t ur_lon,
                             minutes_t p_lat, minutes_t p_lon)
 {
    return geo_bounds_check(ll_lat, ll_lon, ur_lat, ur_lon, p_lat, p_lon,
      WORLD_MIN, HALF_WORLD_MIN);
 }

/*------------------------------------------------------------------
-  FUNCTION NAME:    geo_bounds_check
-  PROGRAMMER:       Vincent Sollicito
-  DATE:             January 1994
-
-  PURPOSE:
-
-      Test if a geographic point lies on or within a bounding rectangle.
-  All points are in geo coordinates in the same unit.
-
-  PARAMETERS:
-
-      ll_lat:     latitude of the lower left corner of the bounding rectangle
-
-      ll_lon:     longitude of the lower left corner of the bounding rectangle
-
-      ur_lat:     latitude of the upper right corner of the bounding rectangle
-
-      ur_lon:     longitude of the upper right corner of the bounding rectangle
-
-      p_lat:      latitude of the point to test for
-
-      p_lon:      longitude of the point to test for
-
-      around_world: unit dependent constant
-
-      half_world:   unit dependent constant
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

int geo_bounds_check(double ll_lat, double ll_lon,
   double ur_lat, double ur_lon,
   double p_lat,  double p_lon, double around_world, double half_world)
{
   int flags = 0;

   /* north/south check */
   if (p_lat > ur_lat)
      flags = GEO_NORTH_OF;
   else if (p_lat < ll_lat)
      flags = GEO_SOUTH_OF;

   /* east/west check */
   if (GEO_lon_in_range(ll_lon, ur_lon, p_lon) == FALSE)
   {
      // if point is east of the eastern boundary
      if (geo_east_of(p_lon, ur_lon, around_world, half_world))
      {
         double geo_width;

         // compute the width in units longitude
         geo_width = ur_lon - ll_lon;
         if (geo_width < 0.0)
            geo_width += around_world;

         // if the geo-width is less than half way around the world, a point
         // is east of the region whenever it is east of the eastern edge of
         // the region, ur_lon
         if (geo_width < half_world)
         {
            flags |= GEO_EAST_OF;
            return flags;
         }

         double mid_lon;

         // find the mid-longitude moving east from ur_lon to ll_lon
         mid_lon = ur_lon + (around_world - geo_width) / 2.0;
         if (mid_lon > half_world)
            mid_lon -= around_world;

         // if the point is east of the mid-longitude then it is west of the
         // region, otherwise it is east of the region
         if (geo_east_of(p_lon, mid_lon, around_world, half_world))
            flags |= GEO_WEST_OF;
         else
            flags |= GEO_EAST_OF;
      }
      else
         flags |= GEO_WEST_OF;
   }

   return flags;
}
