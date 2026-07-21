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
-  FILE NAME:          range.c
-  LIBRARY NAME:       geo_tool
-
-  DESCRIPTION:
-
-     These function test the given Lat-Long pair to see if they are in the
-  range of valid values.
-
-  PUBLIC FUNCTIONS:
-
-      GEO_valid_degrees
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
-       $Log: range.cpp $
//Revision 1.1  1995/04/23  17:00:14  vinny
//Initial revision
//
 * Revision 1.1  1994/10/26  09:16:21  gue
 * Initial revision
 * 
 * Revision 1.1  1994/02/28  07:25:25  gue
 * Initial revision
 * 
-------------------------------------------------------------------*/

/*------------------------------------------------------------------
-                            Includes
-------------------------------------------------------------------*/

#include "stdafx.h"
#include "geo_tool.h"

/*------------------------------------------------------------------
-  FUNCTION NAME:       GEO_valid_degrees
-  PROGRAMMER:          Vincent Sollicito
-  DATE:                January 1994
-
-  PURPOSE:
-
-      Determine if the given lat-long pair are within the range of valid 
-  values.  Values are in degrees.
-
-  PARAMETERS:
-
-      latitude:        latitude to test for
-
-      longitude:       longitude to test for
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

boolean_t GEO_valid_degrees(degrees_t latitude, degrees_t longitude)
{
   if (latitude < MIN_LAT_DEG || 
      latitude > MAX_LAT_DEG ||
      longitude < MIN_LON_DEG ||
      longitude > MAX_LON_DEG)
   {
      return FALSE;
   }

   return TRUE;
}

boolean_t GEO_valid_radians(radians_t latitude, radians_t longitude)
{
   if (latitude < MIN_LAT_RAD || 
      latitude > MAX_LAT_RAD ||
      longitude < MIN_LON_RAD ||
      longitude > MAX_LON_RAD)
   {
      return FALSE;
   }

   return TRUE;
}

boolean_t GEO_valid_minutes(minutes_t latitude, minutes_t longitude)
{
   if (latitude < MIN_LAT_MIN || 
      latitude > MAX_LAT_MIN ||
      longitude < MIN_LON_MIN ||
      longitude > MAX_LON_MIN)
   {
      return FALSE;
   }

   return TRUE;
}

