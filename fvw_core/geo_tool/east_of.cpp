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
-  FILE NAME:          east_of.c
-  LIBRARY NAME:       geo_tool
-
-  DESCRIPTION:
-
-      These functions return a boolean evaluation of the statement, "a is east
-  of b." They return 0 if a=b or a is west of b, and a 1 if a is east of b. 
-
-  PUBLIC FUNCTIONS:
-
-      GEO_east_of_radians
-      GEO_east_of_degrees
-      GEO_east_of_minutes
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
-       $Log: east_of.cpp $
//Revision 1.1  1995/04/23  16:59:46  vinny
//Initial revision
//
 * Revision 1.2  1995/02/05  17:28:10  vinny
 * changed east_of() to geo_east_of() so it is visible outside of east_of.c
 * 
 * Revision 1.1  1994/10/26  09:15:31  gue
 * Initial revision
 * 
 * Revision 1.1  1994/02/28  07:22:15  gue
 * Initial revision
 * 
-------------------------------------------------------------------*/

/*------------------------------------------------------------------
-                            Includes
-------------------------------------------------------------------*/

#include "stdafx.h"

#include "common.h"
#include "geo_tool.h"
#include "geo_loc.h"

boolean_t geo_east_of(double a, double b, double around_world, 
   double half_world);

/*------------------------------------------------------------------
-  FUNCTION NAME:       GEO_east_of_radians
-  PROGRAMMER:          Vincent Sollicito
-  DATE:                January 1994
-
-  PURPOSE:
-
-      Determine whether point a is east of point b.
-
-  PARAMETERS:
-
-      a:              longitude in radians.  -PI <= a <= PI
-
-      b:              longitude in radians.  -PI <= b <= PI
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

boolean_t GEO_east_of_radians(radians_t a, radians_t b)
{
   return geo_east_of((double)a, (double)b, WORLD_RAD, HALF_WORLD_RAD);
}

/*------------------------------------------------------------------
-  FUNCTION NAME:      GEO_east_of_degrees
-  PROGRAMMER:         Vincent Sollicito
-  DATE:               January 1994
-
-  PURPOSE:
-
-      Determine whether point a is east of point b.
-
-  PARAMETERS:
-
-      a:              longitude in degrees.  -180.0 <= a <= 180.0
-
-      b:              longitude in degrees.  -180.0 <= b <= 180.0
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
-      common.h
-      geo_tool.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

boolean_t GEO_east_of_degrees(degrees_t a, degrees_t b)
{
   return geo_east_of((double)a, (double)b, WORLD_DEG, HALF_WORLD_DEG);
}

/*------------------------------------------------------------------
-  FUNCTION NAME:      GEO_east_of_minutes
-  PROGRAMMER:         Vincent Sollicito
-  DATE:               January 1994
-
-  PURPOSE:
-
-      Determine whether point a is east of point b.
-
-  PARAMETERS:
-
-      a:              longitude in minutes.  -10800.0 <= a <= 10800.0
-
-      b:              longitude in minutes.  -10800.0 <= b <= 10800.0
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
-      common.h
-      geo_tool.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

boolean_t GEO_east_of_minutes(minutes_t a, minutes_t b)
{
   return geo_east_of((double)a, (double)b, WORLD_MIN, HALF_WORLD_MIN);
}

/*------------------------------------------------------------------
-  FUNCTION NAME:     geo_east_of
-  PROGRAMMER:        Vincent Sollicito
-  DATE:              January 1994
-
-  PURPOSE:
-
-  PARAMETERS:
-
-     a:              a longitude in the same unit as around_world and 
-                     half_world.  -half_world <= b < half_world
-
-     b:              a longitude in the same unit as around_world and 
-                     half_world.  -half_world <= b < half_world
-
-     around_world:
-
-     half_world:    
-
-  RETURN VALUES:
-
-      TRUE
-      FALSE
-
-  PRECONDITIONS:
-
-      half_world > 0.0
-      around_world = 2 x half_world
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      common.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

boolean_t geo_east_of(double a, double b, double around_world, 
   double half_world)
{
   double diff;

   /* (a>=b) and in same hemisphere, 0<=diff<= half_world 
      (a<=b) and in same hemisphere, 0>=diff>=-half_world
      (a>b) and in Opp hemispheres,  0< diff<= around_world 
      (a<b) and in Opp hemispheres,  0> diff>=-around_world */

   diff = a - b;

   if (diff < 0.0) /* convert diff to eastward distance from b to a */
      diff += around_world;

   if (0.0 < diff && diff <= half_world)
      return TRUE;

   return FALSE;
} 
