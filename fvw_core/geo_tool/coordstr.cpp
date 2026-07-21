// Copyright (c) 1994-2012 Georgia Tech Research Corporation, Atlanta, GA
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



// coordstr.cpp

#include "stdafx.h"

#include "geo_tool.h"
#include "geotrans.h"

#define FORMAT_DEGREES 1
#define FORMAT_DEGREES_MINUTES 2
#define FORMAT_DEGREES_MINUTES_SECONDS 3

static
boolean_t overflow(double *flow_from, double *flow_into,
	double overflow_threshold);

// This function converts a latitude or a longitude to a formated string. 

void GEO_degrees_to_string(degrees_t degrees, boolean_t lat_not_lon,
   char *string, int string_len)
{
   char str_lat[GEO_MAX_LAT_LON_STRING+1];
   char str_lon[GEO_MAX_LAT_LON_STRING+1];
   CGeoTrans geotrans;

   if (lat_not_lon)
      geotrans.DLL_lat_lon_to_string(degrees, -84.0, 0.000001, string, string_len);
   else
      geotrans.DLL_lat_lon_to_string(34.0, degrees, 0.000001, string, string_len);

   sscanf_s(string, "%[ NSns0123456789.\260\'\"]"
      "%[ EWew01234567890.\260\'\"]", str_lat, GEO_MAX_LAT_LON_STRING+1, str_lon, GEO_MAX_LAT_LON_STRING+1);

   if (lat_not_lon)
   {
      strncpy_s( string, string_len, str_lat, __max( 0, strlen( str_lat ) - 2 ) );
   }
   else
      strcpy_s(string, string_len, str_lon);
}

static
boolean_t overflow(double *flow_from, double *flow_into,
	double overflow_threshold)
{
   if (*flow_from >= overflow_threshold)
   {
      *flow_from = 0.0;
      *flow_into += 1.0; 
      return TRUE;
   }

   return FALSE;
}