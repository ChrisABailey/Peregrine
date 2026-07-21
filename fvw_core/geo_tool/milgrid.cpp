// Copyright (c) 1994-2011 Georgia Tech Research Corporation, Atlanta, GA
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
-  FILE NAME:          milgrid.cpp
-  LIBRARY NAME:       geo_tool
-
-  DESCRIPTION:        Geographical Utilities, define the interface
-                      to GEO2.DLL
-
-  DATE:               18Oct96
 * 
-------------------------------------------------------------------*/
#include "stdafx.h"
#include "geo_tool.h"
#include "geotrans.h"

CGeoTrans geotrans;

int GEO_lat_lon_to_string(degrees_t lat, degrees_t lon, char *geo_string, int geo_string_len)
{
   return geotrans.DLL_lat_lon_to_geo(lat, lon, geo_string, geo_string_len);
}

#ifdef _WIN32
GEO_TOOL_DECLSPEC CString GEO_get_formatted_location_string(degrees_t lat, degrees_t lon)
{
   char lat_lon_str[GEO_MAX_VALUE_LENGTH + 1];
   char datum[GEO_MAX_DATUM_LENGTH + 1];

	GEO_lat_lon_to_string(lat, lon, lat_lon_str, GEO_MAX_VALUE_LENGTH + 1);
   GEO_get_default_datum(datum, GEO_MAX_DATUM_LENGTH + 1);

	// put Location string in edit string
   CString locationStr;
	locationStr.Format("(%s)  %s", datum, lat_lon_str);
	return locationStr;
}
#endif  // _WIN32


int GEO_lat_lon_to_string(degrees_t lat, degrees_t lon, degrees_t dpp,
   char *geo_string, int geo_string_len)
{
   return geotrans.DLL_lat_lon_to_geo(lat, lon, dpp, geo_string, geo_string_len);
}


int GEO_string_to_lat_lon(const char *geo_string, const char *datum,
	degrees_t *lat, degrees_t *lon)
{
	return geotrans.DLL_string_to_lat_lon(geo_string, datum, *lat, *lon);
}

int GEO_datum_valid( const char *datum, long &index_datum )
{
	return geotrans.DLL_datum_valid( datum, index_datum );
}

// Returns the datum for the default display: primary or secondary.
// See GEO_get_default_display().  The datum string will contain 5
// characters, plus 1 for '\0'.
void GEO_get_default_datum(char *datum, int datum_len)
{
   const int LEN = 21;
   char display_type[LEN];

   // get default datum
   GEO_get_default_display(display_type, LEN);
   if (strcmp(display_type, "SECONDARY") == 0) 
      GEO_get_secondary_datum(datum, datum_len);
   else 
      GEO_get_primary_datum(datum, datum_len);
}

void GEO_get_primary_datum( char *value, int value_len )
{
	geotrans.DLL_get_primary_datum(value, value_len);
}


void GEO_get_secondary_datum( char *value, int value_len )
{
	geotrans.DLL_get_secondary_datum(value, value_len);
}


void GEO_get_primary_format( char *value, int value_len )
{
	geotrans.DLL_get_primary_format(value, value_len);
}


void GEO_get_secondary_format( char *value, int value_len )
{
	geotrans.DLL_get_secondary_format(value, value_len);
}


void GEO_get_default_display( char *value, int value_len )
{
	geotrans.DLL_get_default_display(value, value_len);
}


int GEO_set_primary_datum( const char *value )
{
	return geotrans.DLL_set_primary_datum(value);
}


int GEO_set_secondary_datum( const char *value )
{
	return geotrans.DLL_set_secondary_datum(value);
}


int GEO_set_primary_format( const char *value )
{
	return geotrans.DLL_set_primary_format(value);
}


int GEO_set_secondary_format( const char *value )
{
	return geotrans.DLL_set_secondary_format(value);
}


int GEO_set_default_display( const char *value )
{
	return geotrans.DLL_set_default_display(value);
}

void GEO_get_primary_lat_lon_format(char *value, int value_len)
{
   geotrans.DLL_get_primary_lat_lon_format(value, value_len);
}

void GEO_get_secondary_lat_lon_format(char *value, int value_len)
{
   geotrans.DLL_get_secondary_lat_lon_format(value, value_len);
}

int GEO_set_primary_lat_lon_format(const char *value)
{
   return geotrans.DLL_set_primary_lat_lon_format(value);
}

int GEO_set_secondary_lat_lon_format(const char *value)
{
   return geotrans.DLL_set_secondary_lat_lon_format(value);
}
