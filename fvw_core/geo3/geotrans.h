// Copyright (c) 1994-2013 Georgia Tech Research Corporation, Atlanta, GA
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



/////////////////////////////////////////////////////////////////////////////
// HEADER_FILE: geotrans.h
//
//
// SUBJECT    : GEODLL (Geographical Utilities)
//
// DESCRIPTION: This is the header that must be included in files that use the
//              CGeoTrans funcitons. Most of the code and all of the code that
//              performs conversion algorithms is taken from DMA's MADTRAN
//              program. These functions give results that agree exactly with
//              the MADTRAN program.
/////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef GEOTRANS_H
#define GEOTRANS_H

#include <math.h>
#include <sstream>

// forward declarations
//

namespace MSP {
   namespace CCS {
      class DatumLibrary;
      class EllipsoidLibrary;
   }  // namespace CCS
}  // namespace MSP


/////////////////////////////////////////////////////////////////////////////
//  ERROR SYMBOLIC CONSTANTS
/////////////////////////////////////////////////////////////////////////////
#define INVALID_DATUM_CODE          -30
#define NOT_INITIALIZED             -31
#define INVALID_INPUT_DATUM         -32
#define INVALID_OUTPUT_DATUM        -33
#define INVALID_MILGRID_VALUE       -34
#define INVALID_MILGRID_DATUM       -35
#define INVALID_MILGRID_LATITUDE    -36
#define MILGRID_MAJOR_GRID_ZONE     -37
#define MILGRID_MAJOR_GRID_LETTER   -38
#define MILGRID_MINOR_GRID_LETTER   -39
#define MILGRID_EASTING_NORTHING    -40
#define MILGRID_UNDEFINED_FOR_DATUM -41
#define INVALID_STRING_LENGTH       -42
#define INVALID_LOCATION_STRING     -43
#define INVALID_UTM_STRING          -44

#define GEOTRANS_OUTPUT_WARNING     4

/////////////////////////////////////////////////////////////////////////////
// The maximum length of a parameter value string without the trailing \0
/////////////////////////////////////////////////////////////////////////////
#define GEO_MAX_VALUE_LENGTH 128
#define GEO_MAX_DATUM_LENGTH 6      // e.g., INH-A1
#define GEO_MAX_ELLIPSOID_NAME_LENGTH 31
#define GEO_MAX_MGRS_LENGTH 21
#define GEO_MAX_GARS_LENGTH 7


/////////////////////////////////////////////////////////////////////////////
// The following are defines for variable names that can be in the .ini file
/////////////////////////////////////////////////////////////////////////////
#define GEO_PRIMARY_FORMAT    "PRIMARY_FORMAT"
#define GEO_SECONDARY_FORMAT  "SECONDARY_FORMAT"
#define GEO_PRIMARY_DATUM     "PRIMARY_DATUM"
#define GEO_SECONDARY_DATUM   "SECONDARY_DATUM"
#define GEO_DEFAULT_DISPLAY   "DEFAULT_DIPLAY"


/////////////////////////////////////////////////////////////////////////////
//  EXPORT & IMPORT SYMBOLIC CONSTANTS (_export replaced by _declspec)
/////////////////////////////////////////////////////////////////////////////
#ifndef _WIN32
#define GEOTRANSDLL_API
#elif defined(GEOTRANSDLL_EXPORTS)
#define GEOTRANSDLL_API __declspec(dllexport)
#else
#define GEOTRANSDLL_API __declspec(dllimport)
#endif

#define BUFSIZE 80

///////////////////////////////////////////////////////////////////////////////
//taken to remove reliance on common.h, which requires a specific stdafx.h,
//which may conflict with other projects
#define SUCCESS       0
#define FAILURE      -1

typedef int boolean_t;
typedef double FLOAT8;
typedef FLOAT8 degrees_t;

/////////////////////////////////////////////////////////////////////////////
//
class GEOTRANSDLL_API CGeoTrans
{
   // Construction
public:
   CGeoTrans();   // standard constructor
   ~CGeoTrans();  // destructor

private:
   LPSTR m_pszErrorMsg;

   // Implementation
public:
   // performs datum conversion on a geo
   long convert_datum( double lat_in, double long_in,       // input in degrees (W is minus)
      double &lat_out, double &long_out,   // output in degree
      LPCSTR sdatum_in,    // input datum of input
      LPCSTR sdatum_out ); // input datum of output

   // converts a geo to UTM and MilGrid
   long convert_geo( double lat_in, double long_in,         // input in degrees (W is minus)
      LPCSTR sdatum,
      int &utm_zone, double &utm_northing, double &utm_easting,
      LPSTR milgrid, int milgrid_len );

   long convert_geo( double lat_in, double long_in,         // input in degrees (W is minus)
      LPCSTR sdatum,
      int &utm_zone, char& utm_hemisphere, double &utm_northing, double &utm_easting,
      LPSTR milgrid, int milgrid_len );

   // converts a geo to gars
   long convert_geo(double lat_in, double long_in, //input in degrees (W is minus)
      LPCSTR sdatum, LPSTR gars, int gars_len);

   // converts UTM to geo and MilGrid
   long convert_utm(
      int utm_zone, double utm_northing, double utm_easting,
      LPCSTR sdatum,
      double &lat_out, double &long_out,     // output in degrees (W is minus)
      LPSTR milgrid, int milgrid_len );

   long convert_utm(
      int utm_zone, char utm_hemisphere, double utm_northing, double utm_easting,
      LPCSTR sdatum,
      double &lat_out, double &long_out,     // output in degrees (W is minus)
      LPSTR milgrid, int milgrid_len );

   // converts MilGrid to geo and UTM
   long convert_milgrid( LPCSTR  milgrid,
      LPCSTR sdatum,
      double &lat_out, double &long_out, // output in degrees (W is minus)
      int &utm_zone, double &utm_northing, double &utm_easting );

   long convert_milgrid( LPCSTR  milgrid,
      LPCSTR sdatum,
      double &lat_out, double &long_out, // output in degrees (W is minus)
      int &utm_zone, char& utm_hemisphere,
      double &utm_northing, double &utm_easting );

   // convert alias datum names to the single common name
   short get_common_datum_name( LPCSTR datum, LPSTR item, int item_len);
   long get_descriptive_name( LPCSTR code, LPSTR name, int name_len);

   // code is a 5 char string,
   long get_datum_index( LPCSTR code, long &index );
   long get_datum_index( LPCSTR datum_str );

public:

   BOOL geo2text( double lat, double lon, LPSTR slat, int slat_len, LPSTR slon, int slon_len );
   BOOL text2geo( LPCSTR slat, LPCSTR slon, double* lat, double* lon );

   // Given a WGS-84 latitude and longitude; calculate the georef coordinate
   // string.  This function uses the maximum precision of 0.000001 degrees.
   // Output will be displayed in the datum and format specified by the user,
   // i.e., the default datum and format.
   int DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon,LPSTR geo_string, int geo_string_len);
   int DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon,
      LPSTR geo_string, int geo_string_len, boolean_t default_format);

   // Given a WGS-84 latitude, longitude, and degrees per pixel; calculate
   // the georef coordinate string.  dpp determines the number of decimal places.
   // Output will be displayed in the datum and format specified by the user,
   // i.e., the default datum and format.
   int DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon, degrees_t dpp,
      char *geo_string, int geo_string_len);

   int DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon,degrees_t dpp,
      char *geo_string, int geo_string_len, boolean_t default_format);

   // Given a WGS-84 latitude and longitude, and a datum, this function returns
   // a coordinate string in the default or secondary format, BUT in not the given datum.
   int DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon, LPCSTR datum,
      LPSTR geo_string, int geo_string_len, boolean_t default_format);

   // Given a WGS-84 latitude and longitude, and a datum, this function returns
   // a coordinate string in the default format, BUT in not the given datum.
   int DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon, LPCSTR datum,
      char *geo_string, int geo_string_len);

   // Given a WGS-84 latitude and longitude, and a datum, this function returns
   // a coordinate string in the default or secondary format, BUT in the given datum.
   // dpp determines the number of decimal places, e.g., 0.000001 would mean
   // 1/millionth of a degree of precision.
   int DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon, LPCSTR datum,
      degrees_t dpp,LPSTR geo_string, int geo_string_len, boolean_t default_format);

   // Given a WGS-84 latitude and longitude, and a datum, this function returns
   // a coordinate string in the default format, BUT in the given datum.
   // dpp determines the number of decimal places, e.g., 0.000001 would mean
   // 1/millionth of a degree of precision.
   int DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon, LPCSTR datum,
      degrees_t dpp,LPSTR geo_string, int geo_string_len);

   // Given a coordinate string and datum this function returns a WGS-84
   // lat-lon and a formated coordinate string.  This function uses the
   // maximum precision of 0.000001 degrees.  Output will be displayed in
   // the datum and format specified by the user.
   int DLL_location_to_geo( LPCSTR location, LPCSTR datum,
      degrees_t &lat, degrees_t &lon,LPSTR new_location,
      boolean_t default_format);

    // Given a coordinate string and datum this function returns a WGS-84
   // lat-lon and a formated coordinate string.  This function uses the
   // maximum precision of 0.000001 degrees.  Output will be displayed in
   // the datum and format specified by the user.
   int DLL_location_to_geo( LPCSTR location, LPCSTR datum,
      degrees_t &lat, degrees_t &lon,LPSTR new_location);

   // Given a coordinate string, datum and degrees per pixel this function
   // returns a WGS-84 lat-lon and a formated coordinate string.  dpp
   // determines the number of decimal places in the output string.
   // Output will be displayed in the datum and format specified by the user.
   int  DLL_location_to_geo( LPCSTR location, char const *datum,
      degrees_t dpp, degrees_t &lat, degrees_t &lon,LPSTR new_location,
      boolean_t default_format);

   // Given a coordinate string, datum and degrees per pixel this function
   // returns a WGS-84 lat-lon and a formated coordinate string.  dpp
   // determines the number of decimal places in the output string.
   // Output will be displayed in the datum and format specified by the user.
   int  DLL_location_to_geo( LPCSTR location, char const *datum,
      degrees_t dpp, degrees_t &lat, degrees_t &lon,LPSTR new_location);

   // Given a coordinate string, datum and degrees per pixel this function
   // returns a lat-lon and a formated coordinate string.  dpp
   // determines the number of decimal places in the output string.
   // Output will be displayed in the datum and format specified by the user.
   int  DLL_location_to_geo( LPCSTR location, char const *datum, boolean_t convert_to_wge,
      degrees_t dpp, degrees_t &lat, degrees_t &lon,LPSTR new_location,
      boolean_t default_format);

   // Given a coordinate string, datum and degrees per pixel this function
   // returns a lat-lon and a formated coordinate string.  dpp
   // determines the number of decimal places in the output string.
   // Output will be displayed in the datum and format specified by the user.
   int  DLL_location_to_geo( LPCSTR location, char const *datum, boolean_t convert_to_wge,
      degrees_t dpp, degrees_t &lat, degrees_t &lon,LPSTR new_location);

    // Given a coordinate string and datum, calcualte the WGS84 latitude
   // and longitude in degrees.
   int DLL_string_to_lat_lon( LPCSTR string, LPCSTR datum,
      degrees_t &lat, degrees_t &lon);

    // this function is only use inside the GEO3
   int DLL_string_to_lat_lon( LPCSTR string, LPCSTR datum,
      LPCSTR format_s, degrees_t &lat, degrees_t &lon,
      boolean_t convert_to_wge);

    // Given a datum, WGS84 latitude, and WGS84 longitude; calculate a UTM
   // in the given datum.
   int DLL_calc_utm_string(LPCSTR datum, degrees_t lat,
      degrees_t lon,LPSTR str, int str_len);

   // Given a datum, WGS84 latitude, and WGS84 longitude; calcualte a MGRS
   // in the given datum.
   int DLL_calc_milgrid_string(LPCSTR datum, degrees_t lat,
      degrees_t lon,LPSTR str, int str_len);

   // Given a datum, WGS84 latitude, and WGS84 longitude; calcualte a MGRS
   // in the given datum.
   int DLL_calc_gars_string(degrees_t lat, degrees_t lon,LPSTR str, int str_len);

    // Given a MGRS coordinate string and datum; calcualte the WGS84 latitude
   // and WGS84 longitude.
   int DLL_milgrid_string_to_lat_lon(LPCSTR geo_string, LPCSTR datum,
      degrees_t &lat, degrees_t &lon);

   // Given a UTM coordinate string and datum; calcualte the WGS84 latitude
   // and WGS84 longitude.
   int DLL_utm_string_to_lat_lon( LPCSTR geo_string, LPCSTR datum,
      degrees_t &lat, degrees_t &lon);

   // Given latitude OR longitude string; convert to degrees
   int DLL_string_to_degrees( LPCSTR string, boolean_t lat_not_lon,
      degrees_t *degrees );

   // Given a datum, WGS84 latitude, WGS 84 longitude, and degrees_per_pixel;
   // transform to coordinate string in the given datum.
   int DLL_lat_lon_to_string(LPCSTR datum,
      degrees_t lat, degrees_t lon, degrees_t dpp,
      char *lat_lon_str, int lat_lon_str_len, boolean_t reload_format = TRUE);

   // Given latitude, longitude and degrees_per_pixel; transform to
   // coordinate string.  Note: datum in equals datum out, i.e., if the
   // input is a WGS84 lat-lon, then the output will be a WGS84 lat-lon
   // string.
   int DLL_lat_lon_to_string(degrees_t lat, degrees_t lon, degrees_t dpp,
      char *lat_lon_str, int lat_lon_str_len, boolean_t reload_format = TRUE);

   int DLL_datum_valid( LPCSTR datum, long &index_datum );

   void DLL_get_primary_datum( LPSTR value, int value_len );

   void DLL_get_secondary_datum( LPSTR value, int value_len );

   void DLL_get_primary_format( LPSTR value, int value_len );

   void DLL_get_secondary_format( LPSTR value, int value_len );

   void DLL_get_default_display( LPSTR value, int value_len );

   int  DLL_set_primary_datum( LPCSTR value );

   int  DLL_set_secondary_datum( LPCSTR value );

   int  DLL_set_primary_format( LPCSTR value );

   int  DLL_set_secondary_format( LPCSTR value );

   int  DLL_set_default_display( LPCSTR value );

   void DLL_report_string_to_degrees_error( int error );

   void DLL_get_primary_lat_lon_format( LPSTR value, int value_len );

   void DLL_get_secondary_lat_lon_format( LPSTR value, int value_len );

   int DLL_set_primary_lat_lon_format( LPCSTR value) ;

   int DLL_set_secondary_lat_lon_format( LPCSTR value );

   // performs datum conversion on a geo - BDF
   long DLL_convert_datum(
      double lat_in, double long_in,// input in degrees (W is minus)
      double &lat_out, double &long_out,   // output in degree
      LPCSTR sdatum_in,    // input datum of input
      LPCSTR sdatum_out ); // input datum of output

   void DLL_get_return_code_string(
      long  error_code,
      LPCSTR separator,
      LPSTR string, int string_len);

   long get_user_datum_code( LPCSTR common_code, LPSTR ui_code, int ui_code_len );
   long get_datum_ellipsoid_name( LPCSTR datum, LPSTR ellipsoid_name, int ellipsoid_name_len );
   long get_datum_count( long& datum_items );
   long get_datum_code ( const long index, LPSTR code, INT code_len );

   int strdelr( int c,LPSTR s );
   int chr_ins( int, int, char,LPSTR );
   // helper method to determine whether to return th status

   static LPCSTR get_common_datum_name( LPCSTR pszDatumName );

protected:
   long report_error( long lStatus, const std::stringstream& ssErrorMsg );

private:
   LONG report_error( __in long lStatus,
      __in const std::stringstream& ssErrorMsg, __in boolean_t bLogError );
   MSP::CCS::DatumLibrary* GetDatumLibrary();
   MSP::CCS::EllipsoidLibrary* GetEllipsoidLibrary();
   LONG GetDatumIndex( __in LPCSTR pszDatumName, __out long& lDatumIndex,
      __in boolean_t bLogIfInvalid );
   long convert_geo_to_mgrs(double lat_in, double long_in, LPCSTR sdatum,
      char *milgrid, int milgrid_len);
};

extern "C" 
{ 
   BOOL GeoToText( double lat, double lon, 
      LPSTR slat, int slat_len, 
      LPSTR slon, int slon_len ); 
 
   int CalcUtmString(LPCSTR datum, degrees_t lat, degrees_t lon, 
      LPSTR str, int str_len); 
 
   int CalcMilgridString(LPCSTR datum, degrees_t lat, degrees_t lon, 
      LPSTR str, int str_len); 
 
   int CalcGarsString(degrees_t lat, degrees_t lon, LPSTR str, int str_len); 
} 
#endif  // ifndef GEOTRANS_H
