// Copyright (c) 1994-2014 Georgia Tech Research Corporation, Atlanta, GA
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



#include "stdafx.h"
#include "geotrans.h"
#include "defs.h"
#include <string>

//included to make sure that we are using the correct version of isalpha
#include <ctype.h>

#define FORMAT_DEGREES 1
#define FORMAT_DEGREES_MINUTES 2
#define FORMAT_DEGREES_MINUTES_SECONDS 3

static std::string
   strPrimaryDatum( "" ),
   strSecondaryDatum( "" );

static boolean_t overflow( double *flow_from, double *flow_into,
                          double overflow_threshold );

static
boolean_t is_valid_grid_letter(char c);

static
int process_milgrid_string(LPCSTR input, int length,LPSTR output, int output_len);

static
int process_utm_string( LPCSTR input, int *zone, double *easting, double *northing);

// functions used to read/write format options to registry
static
std::string GEO3_get_registry_string(LPCSTR value_name,
                                     LPCSTR default_value /*= NULL*/);
static
int GEO3_read_registry(LPCSTR sub_key, LPCSTR value_name,
                       DWORD* type, BYTE* storage_loc, DWORD* storage_size);

static
boolean_t GEO3_set_registry_string(LPCSTR value_name, LPCSTR value);

static
int GEO3_write_registry(LPCSTR sub_key,
                        LPCSTR value_name, DWORD type, const BYTE* storage_loc,
                        DWORD storage_size);


// ********************************************************************
// ********************************************************************

// old style translate double lat and lon into individual character arrays

BOOL CGeoTrans::geo2text(double lat, double lon,LPSTR slat, int slat_len,LPSTR slon, int slon_len)
{
#if 0  // needs unit tests before changing to new implementation
   if ( lat < 0.0 )
      sprintf_s( slat, slat_len, "S%9.6f", -lat );
   else
      sprintf_s( slat, slat_len, "N%9.6f", +lat );

   if ( lon < 0 )
      sprintf_s( slon, slon_len, "W%10.6f", -lon );
   else
      sprintf_s( slon, slon_len, "E%10.6f", +lon );
#else
   const int LEN = 12;
   char tlat[LEN];
   char tlon[LEN];
   char tgeo[LEN];
   double tmp;

   // convert the latitude to a string
   if (lat >= 0.0)
   {
      tlat[0] = 'N';
      tmp = lat;
   }
   else
   {
      tlat[0] = 'S';
      tmp = -lat;
   }

   sprintf_s(tgeo, LEN, "%9.6f", tmp);
   strncpy_s(tlat+1, LEN-1, tgeo, 9);
   tlat[10] = '\0';

   // convert the longitude to a string
   if (lon >= 0.0)
   {
      tlon[0] = 'E';
      tmp = lon;
   }
   else
   {
      tlon[0] = 'W';
      tmp = -lon;
   }

   sprintf_s(tgeo, LEN, "%10.6f", tmp);
   strncpy_s(tlon+1, LEN-1, tgeo, 10);
   tlon[11] = '\0';
   strcpy_s(slat, slat_len, tlat);
   strcpy_s(slon, slon_len, tlon);
#endif
   return TRUE;
}
// end of geo3text

// ********************************************************************
// ********************************************************************

// old style translate character array lat and lon into individual doubles

BOOL CGeoTrans::text2geo( LPCSTR slat, LPCSTR slon, double* lat, double* lon)
{
#if 0  // needs unit tests before changing to new implementation
   if ( slat == nullptr || slon == nullptr
      || slat[ 0 ] == '\0' || slon[ 0 ] == '\0' )
   {
      *lon = *lat = 0.0;
      return FALSE;
   }
   *lat = atof( slat + 1 );
   if ( slat[ 0 ] == 'S' || slat[ 0 ] == 's' )
      *lat = -*lat;

   *lon = atof( slon + 1 );
   if ( slon[ 0 ] == 'W' || slon[ 0 ] == 'w' )
      *lat = -*lat;

#else
   char dir;
   const int LEN = 21;
   char tstr[LEN];
   double tgeo;

   dir = slat[0];
   strncpy_s(tstr, LEN, slat+1, 20);
   tstr[20] = '\0';
   tgeo = atof(tstr);
   if ((dir == 'S') || (dir == 's'))
      tgeo = -tgeo;
   *lat = tgeo;

   dir = slon[0];
   strncpy_s(tstr, LEN, slon+1, 20);
   tstr[20] = '\0';
   tgeo = atof(tstr);
   if ((dir == 'W') || (dir == 'w'))
      tgeo = -tgeo;
   *lon = tgeo;
#endif
   return TRUE;
}
// end of text2geo


// *************************************************************
// *************************************************************

// Given a latitude and longitude, this function returns
// a coordinate string in the default format in the default datum.

int CGeoTrans::DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon,
                                 LPSTR geo_string, int geo_string_len, boolean_t default_format)
{
    return DLL_lat_lon_to_geo(lat, lon, 0.000001, geo_string, geo_string_len, default_format);
}

// *************************************************************
// *************************************************************

// Given a latitude and longitude, this function returns
// a coordinate string in the default format in the default datum.

int CGeoTrans::DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon,
                                 LPSTR geo_string, int geo_string_len)
{
    return DLL_lat_lon_to_geo(lat, lon, geo_string, geo_string_len, TRUE);
}

// *************************************************************
// *************************************************************

int CGeoTrans::DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon,
                                  degrees_t dpp,LPSTR geo_string, int geo_string_len)
{
    return DLL_lat_lon_to_geo(lat, lon, dpp, geo_string, geo_string_len, TRUE);
}

// *************************************************************
// *************************************************************

// Given a latitude and longitude, this function returns
// a coordinate string in the default format in the default datum.

int CGeoTrans::DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon,
                                  degrees_t dpp,LPSTR geo_string, int geo_string_len,
                                  boolean_t default_format)
{
    const int DATUM_LEN = 6;
    char sDatum[DATUM_LEN];

    char display[GEO_MAX_VALUE_LENGTH+1];
    DLL_get_default_display(display, GEO_MAX_VALUE_LENGTH+1);

    if ( !strcmp(display, "PRIMARY") )
    {
        DLL_get_primary_datum(sDatum, DATUM_LEN);
    }
    else
    {
        DLL_get_secondary_datum(sDatum, DATUM_LEN);
    }

    return DLL_lat_lon_to_geo(lat, lon, sDatum, dpp, geo_string, geo_string_len, default_format);
}


// *************************************************************
// *************************************************************

// Given a latitude and longitude in the given datum, this function returns
// a coordinate string in the default format, but in the WGE datum.

int CGeoTrans::DLL_lat_lon_to_geo( degrees_t lat, degrees_t lon,
                                  LPCSTR datum, LPSTR geo_string, int geo_string_len,
                                  boolean_t default_format)
{
    return DLL_lat_lon_to_geo( lat, lon, datum, 0.000001, geo_string, geo_string_len, default_format );
}

// *************************************************************
// *************************************************************

// Given a latitude and longitude in the given datum, this function returns
// a coordinate string in the default format, but in the WGE datum.

int CGeoTrans::DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon,
                                  LPCSTR datum,LPSTR geo_string, int geo_string_len)
{
    return DLL_lat_lon_to_geo(lat, lon, datum, geo_string, geo_string_len, TRUE);
}

// *************************************************************
// *************************************************************

// Given a latitude and longitude in the given datum, this function returns
// a coordinate string in the default format, but in the WGE datum.
// dpp determines the number of decimal places, e.g., 0.000001 would mean
// 1/millionth of a degree of precision.

int CGeoTrans::DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon,
                                  LPCSTR datum, degrees_t dpp,
                                 LPSTR geo_string, int geo_string_len)
{
    return DLL_lat_lon_to_geo(lat, lon, datum, dpp, geo_string, geo_string_len, TRUE);
}

// *************************************************************
// *************************************************************

// Given a latitude and longitude in the given datum, this function returns
// a coordinate string in the default format, but in the WGE datum.
// dpp determines the number of decimal places, e.g., 0.000001 would mean
// 1/millionth of a degree of precision.

int CGeoTrans::DLL_lat_lon_to_geo(degrees_t lat, degrees_t lon,
                                  LPCSTR datum, degrees_t dpp,
                                 LPSTR geo_string, int geo_string_len, boolean_t default_format)
{
    int status;
    // insure the values passed in are valid
    if (lat < MIN_LAT_DEG || lat > MAX_LAT_DEG ||
        lon < MIN_LON_DEG || lon > MAX_LON_DEG)
    {
        OutputDebugString("** Invalid lat lon passed into DLL_lat_lon_to_geo\n");
        return FAILURE;
    }

    char format[GEO_MAX_VALUE_LENGTH+1];
    // get default display
    char display[GEO_MAX_VALUE_LENGTH+1];
    DLL_get_default_display(display, GEO_MAX_VALUE_LENGTH+1);

    // get coordinate format
    if (!( default_format ^ !strcmp(display, "PRIMARY") ))
    {
        DLL_get_primary_format(format, GEO_MAX_VALUE_LENGTH+1);
    }
    else
    {
        DLL_get_secondary_format(format, GEO_MAX_VALUE_LENGTH+1);
    }

    if ( !strcmp(format, "MILGRID") )
        status = DLL_calc_milgrid_string(datum, lat, lon, geo_string, geo_string_len);
    else if ( !strcmp(format, "UTM") )
        status = DLL_calc_utm_string(datum, lat, lon, geo_string, geo_string_len);
    else if ( !strcmp(format, "GARS") )
        status = DLL_calc_gars_string(lat, lon, geo_string, geo_string_len);
    else
        status = DLL_lat_lon_to_string(datum, lat, lon, dpp, geo_string, geo_string_len, FALSE);

    return status;
}

// *************************************************************
// *************************************************************

int CGeoTrans::DLL_string_to_lat_lon( LPCSTR geo_string, LPCSTR datum,
                                     degrees_t &lat, degrees_t &lon)
{
    int status;
    degrees_t latitude;
    degrees_t longitude;
    char format[GEO_MAX_VALUE_LENGTH+1];
    // get default display
    char display[GEO_MAX_VALUE_LENGTH+1];
    DLL_get_default_display(display, GEO_MAX_VALUE_LENGTH+1);

    // get coordinate format
    if ( !strcmp(display, "PRIMARY") )
    {
        DLL_get_primary_format(format, GEO_MAX_VALUE_LENGTH+1);
    }
    else
    {
        DLL_get_secondary_format(format, GEO_MAX_VALUE_LENGTH+1);
    }

    status = DLL_string_to_lat_lon(geo_string, datum, format, latitude,
        longitude, TRUE);

    if (status == SUCCESS)
    {
        lat = latitude;
        lon = longitude;
    }

    return status;
}


// *************************************************************
// *************************************************************

int CGeoTrans::DLL_string_to_lat_lon( LPCSTR input,
                                     LPCSTR datum,
                                     LPCSTR format_s,
                                     degrees_t &lat,
                                     degrees_t &lon,
                                     boolean_t convert_to_wge)
{
    // First Try GARS

    {
        // Copy input into geo_string and format to all uppercase with no spaces

        std::string geo_string(input);

        size_t nPos;
        while ( (nPos = geo_string.find_first_of(' ')) != std::string::npos)
            geo_string.erase(nPos, 1);

        ::CharUpper(const_cast<char *>(geo_string.c_str()));

        // Validate the input length

        size_t length = geo_string.size();
        if (length < 5 || length > 7)
            goto not_gars;

        // Validate the cell

        LPCSTR geo_buf = geo_string.c_str();

        char cell_num[4], cell_alpha[2];

        memcpy(cell_num, geo_buf, 3);
        cell_num[3] = '\0';

        memcpy(cell_alpha, geo_buf + 3, 2);

        double cell_int = (double)atoi(cell_num);

        if (cell_int < 1 || cell_int > 720)
            goto not_gars;

        if (cell_alpha[0] < 'A' || cell_alpha[0] > 'R')
            goto not_gars;

        if (cell_alpha[1] < 'A' || cell_alpha[1] > 'Z')
            goto not_gars;

        // Calculate the cell lower left corner

        int lat_half_degrees_from_south_pole;
        for (lat_half_degrees_from_south_pole = 0; cell_alpha[0] > 'A'; cell_alpha[0]--)
        {
            lat_half_degrees_from_south_pole += 24;
            if (cell_alpha[0] == 'I' || cell_alpha[1] == 'O') cell_alpha[0]--;
        }
        for (/* lat_half_degrees_from_south_pole = lat_half_degrees_from_south_pole */; cell_alpha[1] > 'A'; cell_alpha[1]--)
        {
            lat_half_degrees_from_south_pole++;
            if (cell_alpha[1] == 'I' || cell_alpha[1] == 'O') cell_alpha[1]--;
        }

        lat = ((double)lat_half_degrees_from_south_pole)/2 - 90;

        if (lat > 90) // For example, "RZ" would be out of range
            goto not_gars;

        lon = (cell_int - 1)/2 - 180;

        if (length > 5) // The quadrant is specified
        {
            // Move lat, lon to lower left corner of the quadrant

            switch (geo_buf[5])
            {
            case '1':
                lat += 0.25;
                break;
            case '2':
                lat += 0.25;
                lon += 0.25;
                break;
            case '3':
                break;
            case '4':
                lon += 0.25;
                break;
            default:
                goto not_gars;
            }

            if (length > 6) // The keypad is specified
            {
                // Move lat, lon to the center of the keypad

                char keypad_string[2];

                keypad_string[0] = geo_buf[6];
                keypad_string[1] = '\0';

                int keypad = atoi(keypad_string);

                if (keypad < 1 || keypad > 9)
                    goto not_gars;

                lat += ((double)((9 - keypad) / 3))/12 + 1.0/24;
                lon += ((double)((keypad - 1) % 3))/12 + 1.0/24;
            }
            else // No keypad is specified so set to center of quadrant
            {
                lat += 0.125;
                lon += 0.125;
            }
        }
        else // No quadrant is specified so set to center of cell
        {
            lat += 0.25;
            lon += 0.25;
        }

        return SUCCESS;
    }

not_gars:
    const rsize_t str_lat_len = 21;
    const rsize_t str_lon_len = 21;
    char str_lat[str_lat_len], str_lon[str_lon_len], ch;
    size_t i, k, count, len;
    int alpha_count;
    char start;
    boolean_t milgrid_not_degrees, no_alpha;
    int status;
    const rsize_t tstr_len = 51;
    char geo_str[51], tstr[tstr_len];
    char tst[5] = "-'\xB0 ";
    std::string slat, slon;

    std::string geo_string(input);
    ::CharUpper(const_cast<char *>(geo_string.c_str()));
    while (geo_string.size() && geo_string[0] == ' ')
        geo_string = geo_string.substr(1);

    tst[3] = '"';

    // if the input contains fewer than 4 characters, then it can not
    // possibly be a valid input
    count = geo_string.size();
    if (count < 2 || count > 50)
        return INVALID_STRING_LENGTH;

    strcpy_s(tstr, tstr_len, geo_string.c_str());

    // replace odd characters with spaces
    len = strlen(tstr);
    i = 0;
    for (k=0; k<len; k++)
    {
        ch = tstr[k];
        if ((ch != '\'') && (ch != '"') && (ch != '\xB0'))
            geo_str[i] = ch;
        else
            geo_str[i] = ' ';
        i++;
    }

    geo_str[i] = '\0';

    // check for decimal degrees
    no_alpha = TRUE;
    len = strlen(geo_str);
    for (k=0; k<len; k++)
    {
        if (isalpha(geo_str[k]))
            no_alpha = FALSE;
    }

    if (no_alpha)
    {
        size_t tlen;

        slon = geo_str;

        while (slon.size() > 0 && slon[0] == ' ')
            slon = slon.substr(1);

        while (slon.size() && slon[slon.size() - 1] == ' ')
            slon = slon.substr(0, slon.size() - 1);

        i = slon.find(' ');
        if (i == std::string::npos)
            return INVALID_LOCATION_STRING;

        tlen = slon.size();

        slat = slon.substr(0, i);
        slon = slon.substr(i+1, tlen - i - 1);

        while (slon.size() && slon[0] == ' ')
            slon = slon.substr(1);

        lat = atof(slat.c_str());
        lon = atof(slon.c_str());

        if ((lat < -90.0) || (lat > 90.0))
            return LAT_MAGNITUDE_ERROR;

        if ((lon < -180.0) || (lon > 180.0))
            return LON_MAGNITUDE_ERROR;

        status = SUCCESS;

        // convert lat-lon to WGS84 lat-lon
        if ( convert_to_wge &&
            _stricmp(datum, "WGS84") != 0
            && _stricmp(datum, "WGE") != 0)
        {
            degrees_t temp_lat = lat;
            degrees_t temp_lon = lon;

            status = convert_datum(temp_lat, temp_lon, lat, lon, datum, "WGE");
        }

        return SUCCESS;
    }


    // convert dashes to blanks
    for (k=0; k<count; k++)
        for (i=0; i<4; i++)
            if (geo_str[k] == tst[i])
                geo_str[k] = ' ';


    // determine if the input string is milgrid or lat_lon
    start = ' ';
    count = 0;
    alpha_count = 0;
    int consecutive = 0;
    int max_consecutive = 0;
    while (geo_str[count] != '\0')
    {
        if (isdigit(geo_str[count]))
        {
            if (count == 0)
                start = 'd';
        }
        else if (isalpha(geo_str[count]))
        {
            if (count == 0)
                start = 'a';
            alpha_count++;

            if (count > 0 && isalpha(geo_str[count-1]))
            {
                consecutive++;

                if (consecutive > max_consecutive)
                    max_consecutive = consecutive;
            }
            else
                consecutive = 0;
        }

        count++;
    }

    // if it has more than 3 letters, then it is garbage
    if (alpha_count > 3)
        return INVALID_LOCATION_STRING;

    // if it contains exactly three letters, or it starts with a
    // digit and it contains exactly two letters, try milgrid
    if (alpha_count == 3 || (start == 'd' && max_consecutive == 2))
        milgrid_not_degrees = TRUE;
    // if it starts with a letter and it contains exactly two letters,
    // it contains 1 or 0 letters and at least 2 digits, or it, try
    // lat-lon
    else if (alpha_count == 2 || (alpha_count < 2 && count > 1))
        milgrid_not_degrees = FALSE;
    else
        return INVALID_LOCATION_STRING;

    if (alpha_count == 1)
        milgrid_not_degrees = TRUE;

    // if the string is more likely to be milgrid than degrees
    if (milgrid_not_degrees)
    {
        // get lat and lon from a milgrid value (geo_string)
        status = DLL_milgrid_string_to_lat_lon(geo_str, datum, lat, lon);

        // if this fails try UTM
        if (status != SUCCESS)
            status = DLL_utm_string_to_lat_lon(geo_str, datum, lat, lon);
    }
    else
    {
        char n_s[5], e_w[5];
        char comma[5];

        bool parsed = false;
        // first try to see if lat/lon has hemisphere specified second and a comma
        if (sscanf_s(geo_str, "%[ 0123456789.\260\'\"]%[ NnSs]%[ ,]"
            "%[ 0123456789.\260\'\"]%[ EWew]", str_lat, str_lat_len, n_s, 5, comma, 5, str_lon, str_lon_len, e_w, 5)
            == 5)
        {
            std::string lat, lon;
            lat = n_s;
            lat += str_lat;

            lon = e_w;
            lon += str_lon;

            strcpy_s(str_lat, str_lat_len, lat.c_str());
            strcpy_s(str_lon, str_lon_len, lon.c_str());
            parsed = true;
        }
        // next try to see if lat/lon has hemisphere specified second and no comma
        else if (sscanf_s(geo_str, "%[ 0123456789.\260\'\"]%[ NnSs]"
            "%[ 0123456789.\260\'\"]%[ EWew]", str_lat, str_lat_len, n_s, 5, str_lon, str_lon_len, e_w, 5)
            == 4)
        {
            std::string lat, lon;
            lat = n_s;
            lat += str_lat;

            lon = e_w;
            lon += str_lon;

            strcpy_s(str_lat, str_lat_len, lat.c_str());
            strcpy_s(str_lon, str_lon_len, lon.c_str());
            parsed = true;
        }
        //next try to see if lat/lon has hemispehere specified first and a comma
        else if(sscanf_s(geo_str, "%[ NSns0123456789.\260\'\"]%[ ,]"
            "%[ EWew01234567890.\260\'\"]", str_lat, str_lat_len, comma, 5, str_lon, str_lon_len) == 3 )
        {
            parsed = true;
        }
        //finally try to see if lat/lon has hemispehere specified first and no comma
        else if(sscanf_s(geo_str, "%[ NSns0123456789.\260\'\"]"
            "%[ EWew01234567890.\260\'\"]", str_lat, str_lat_len, str_lon, str_lon_len) == 2 )
        {
            parsed = true;
        }

        if(parsed)
        {
            // get latitude and longitude from two lat/lon strings
            status = DLL_string_to_degrees(str_lat, TRUE, &lat);
            if (status == SUCCESS)
            {
                status = DLL_string_to_degrees(str_lon, FALSE, &lon);
                if (status == SUCCESS)
                {
                    // convert lat-lon to WGS84 lat-lon
                    if (convert_to_wge &&
                        _stricmp(datum, "WGS84") != 0
                        && _stricmp(datum, "WGE") != 0)
                    {
                        degrees_t temp_lat = lat;
                        degrees_t temp_lon = lon;

                        convert_datum(temp_lat, temp_lon, lat, lon,
                            datum, "WGE");
                    }

                    if ( !strcmp(format_s, "MILGRID") )
                    {
                        if (lat > 84 || lat < -80 )
                            status = LAT_OUT_OF_RANGE;
                    }
                }
            }
        }

        else
        {
            return INVALID_LOCATION_STRING;
        }
    }

    return status;
}  // DLL_string_to_lat_lon()


// *************************************************************
// *************************************************************

int CGeoTrans::DLL_location_to_geo( LPCSTR location, LPCSTR datum,
                                   degrees_t &lat, degrees_t &lon,
                                  LPSTR new_location)
{
    return DLL_location_to_geo(location, datum, lat, lon,
        new_location, TRUE);
}

// *************************************************************
// *************************************************************

int CGeoTrans::DLL_location_to_geo(LPCTSTR location, LPCSTR datum,
                                   degrees_t &lat, degrees_t &lon,
                                  LPSTR new_location, boolean_t default_format)
{
    return DLL_location_to_geo(location, datum, 0.000001, lat, lon,
        new_location, default_format);
}

// *************************************************************
// *************************************************************

int CGeoTrans::DLL_location_to_geo( LPCSTR location,
                                   LPCSTR datum,
                                   degrees_t dpp,
                                   degrees_t &lat,
                                   degrees_t &lon,
                                  LPSTR geo_string)
{
    return DLL_location_to_geo(location,datum,dpp,lat,lon,geo_string, TRUE);
}
// *************************************************************
// *************************************************************

int CGeoTrans::DLL_location_to_geo( LPCSTR location,
                                   LPCSTR datum,
                                   degrees_t dpp,
                                   degrees_t &lat,
                                   degrees_t &lon,
                                  LPSTR geo_string,
                                   boolean_t default_format)
{
    return DLL_location_to_geo(location,datum,TRUE,dpp,lat,lon,geo_string, default_format);
}
// *************************************************************
// *************************************************************


int CGeoTrans::DLL_location_to_geo( LPCSTR location,
                                   LPCSTR datum,
                                   boolean_t convert_to_wge,
                                   degrees_t dpp,
                                   degrees_t &lat,
                                   degrees_t &lon,
                                  LPSTR geo_string)
{
    return DLL_location_to_geo(location,datum,convert_to_wge,dpp,lat,lon,geo_string,TRUE);
}


// *************************************************************
// *************************************************************

int CGeoTrans::DLL_location_to_geo( LPCSTR location,
                                   LPCSTR datum,
                                   boolean_t convert_to_wge,
                                   degrees_t dpp,
                                   degrees_t &lat,
                                   degrees_t &lon,
                                  LPSTR geo_string,
                                   boolean_t default_format)
{
    int status;
    char format[GEO_MAX_VALUE_LENGTH+1];

    char display[GEO_MAX_VALUE_LENGTH+1];
    DLL_get_default_display(display, GEO_MAX_VALUE_LENGTH+1);

    if (!(default_format ^ !strcmp(display, "PRIMARY") ))
    {
        DLL_get_primary_format(format, GEO_MAX_VALUE_LENGTH+1);
    }
    else
    {
        DLL_get_secondary_format(format, GEO_MAX_VALUE_LENGTH+1);
    }

    status = DLL_string_to_lat_lon(location, datum, format, lat, lon, convert_to_wge);
    if (status != SUCCESS)
        return status;

    // A string length for this method was not passed in. We will assume GEO_MAX_LAT_LON_STRING
    const int GEO_MAX_LAT_LON_STRING = 40;
    status = DLL_lat_lon_to_geo(lat, lon, dpp, geo_string, GEO_MAX_LAT_LON_STRING + 1, default_format);

    return status;
}


// *************************************************************
// *************************************************************
// Given a datum, WGS84 latitude, and WGS84 longitude; calcualte a MGRS
// in the given datum.

int CGeoTrans::DLL_calc_milgrid_string(LPCSTR datum, degrees_t lat,
    degrees_t lon, LPSTR str, int str_len)
{
   // Convert lat-lon to desired datum if the desired datum is not WGS-84.
   // Note: input lat-lon is a WGS84 lat-lon.
   if (_stricmp(datum, "WGS84") != 0 && _stricmp(datum, "WGE") != 0)
   {
      degrees_t temp_lat = lat;
      degrees_t temp_lon = lon;

      convert_datum(temp_lat, temp_lon, lat, lon,
         "WGE", datum);
   }

   // get Milgrid string for the given lat-lon and datum
   char chMilGrid[ GEO_MAX_MGRS_LENGTH + 1 ];
   int status = convert_geo_to_mgrs(lat, lon, datum, chMilGrid, GEO_MAX_MGRS_LENGTH + 1);
   if ( status != SUCCESS )
   {
      strcpy_s(str, str_len, "*** ERROR ***");
      return status;
   }

   sprintf_s( str, str_len, "%3.3s  %2.2s  %5.5s  %s",
      &chMilGrid[ 0 ], &chMilGrid[ 3 ], &chMilGrid[ 5 ], &chMilGrid[ 10 ] );

   return SUCCESS;
}
// end of DLL_calc_milgrid_string

// **********************************************************************
// **********************************************************************

int CGeoTrans::DLL_calc_utm_string(LPCSTR datum, degrees_t lat,
    degrees_t lon,LPSTR str, int str_len)
{
    int status;
    int utm_zone;
    double utm_northing, utm_easting;
    const int LEN = 81;
    char milgrid[LEN];

    if (lat > 84 || lat < -80 )
    {
      strcpy_s(str, str_len, "*** Latitude Out of Range ***");
        return INVALID_MILGRID_LATITUDE;
    }

    // Convert lat-lon to desired datum if the desired datum is not WGS-84.
    // Note: input lat-lon is a WGS84 lat-lon.
    if (_stricmp(datum, "WGS84") != 0 && _stricmp(datum, "WGE") != 0)
    {
      degrees_t temp_lat = lat;
      degrees_t temp_lon = lon;

      convert_datum(temp_lat, temp_lon, lat, lon,
         "WGE", datum);
    }

    // get UTM and Milgrid string for the given lat-lon and datum
    status = convert_geo(lat, lon, datum, utm_zone, utm_northing, utm_easting, milgrid, LEN);
   if ( status != SUCCESS )
      return status;

    // check for south of the equator
    // TODO: These lines should be replaced when we upgrade to use GEOTRANS 3.2. See #2161.
    //if (lat < 0.0)
        //utm_northing = -utm_northing;
   sprintf_s( str, str_len, "%c %6.0f %07.0f", lat >= 0.0 ? 'N' : 'S', utm_easting, fabs( utm_northing ) );

   return SUCCESS;
}
// end of DLL_calc_utm_string

// **********************************************************************
// **********************************************************************

// GARS is always in WGS84
// str must point to at least 8 bytes

int CGeoTrans::DLL_calc_gars_string(degrees_t lat_in, degrees_t lon_in,LPSTR str, int str_len)
{
   return convert_geo(lat_in,lon_in,"WGE",str,str_len);
}
// end of DLL_calc_gars_string

// **********************************************************************
// **********************************************************************

// Given a MGRS coordinate string and datum; calcualte the WGS84 latitude
// and WGS84 longitude.

int CGeoTrans::DLL_utm_string_to_lat_lon(LPCSTR geo_string, LPCSTR datum,
                                         degrees_t &lat, degrees_t &lon)
{
    int utm_zone, status;
    double utm_northing, utm_easting;
    const int LEN = 51;
    char milgrid[LEN];

    status = process_utm_string(geo_string, &utm_zone, &utm_easting, &utm_northing);
    if (status != SUCCESS)
        return status;

    // attempt to convert processed milgrid to a lat-lon
    status = convert_utm(utm_zone, utm_northing, utm_easting, datum, lat, lon, milgrid, LEN);

   // if successfull, convert the lat-lon to a WGS-84 lat-lon
   if ( status == SUCCESS )
   {
      // convert lat-lon to WGS84 lat-lon
      if (_stricmp(datum, "WGS84") != 0 && _stricmp(datum, "WGE") != 0)
      {
         degrees_t temp_lat = lat;
         degrees_t temp_lon = lon;

         convert_datum(temp_lat, temp_lon, lat, lon,
            datum, "WGE");
      }
   }
   return status;
}
// end of DLL_utm_string_to_lat_lon


// **********************************************************************
// **********************************************************************

// Given a MGRS coordinate string and datum; calcualte the WGS84 latitude
// and WGS84 longitude.

int CGeoTrans::DLL_milgrid_string_to_lat_lon(LPCSTR geo_string, LPCSTR datum,
    degrees_t &lat, degrees_t &lon)
{
   int utm_zone;
   double utm_northing, utm_easting;
   const int MILGRID_LEN = 51;
   char milgrid[MILGRID_LEN];
   size_t length;
   int status;

   // make sure the input string does not exceed the length of milgrid
   length = strlen(geo_string);
   if (length > 50)
      return INVALID_MILGRID_VALUE;

   // take the input milgrid string in geo_string and convert it into
   // a format acceptable to the convert_milgrid function
   status = process_milgrid_string(geo_string, (int) length, milgrid, MILGRID_LEN);
   if (status != SUCCESS)
      return status;

   // attempt to convert processed milgrid to a lat-lon
   status = convert_milgrid(milgrid, datum, lat, lon,
                                 utm_zone, utm_northing, utm_easting);

   // if successfull, convert the lat-lon to a WGS-84 lat-lon
   if ( status == SUCCESS )
   {
      // convert lat-lon to WGS84 lat-lon
      if (_stricmp(datum, "WGS84") != 0 && _stricmp(datum, "WGE") != 0)
      {
         degrees_t temp_lat = lat;
         degrees_t temp_lon = lon;

         convert_datum(temp_lat, temp_lon, lat, lon,
            datum, "WGE");
      }
   }

   return status;
}

// **********************************************************************
// **********************************************************************


int CGeoTrans::DLL_string_to_degrees( LPCSTR string,
                                     boolean_t lat_not_lon, degrees_t *degrees)
{
    const int BUF_LEN = 31;
    char buffer[BUF_LEN];
    char buffer2[BUF_LEN];
    char buffer3[BUF_LEN];
    double deg;
    double min = 0.0;    // initialize in case it is not scanned
    double sec = 0.0;    // initialize in case it is not scanned
    double sign;         // +/- 1.0 from dir character
    double loc_degrees;
    double max_degrees;
    int count;
    size_t index;
   LPSTR symbol;

    // got to draw the line some where
    if (strlen(string) > 30)
        return (lat_not_lon) ? LAT_STRING_TOO_LONG : LON_STRING_TOO_LONG;

    // Replace degree and minute symbols with spaces so that input containing
    // only one of the two symbols or both resembles input with space between
    // the degree, minute, and seconds field.  A trailing second symbol is not
    // relavent.
    symbol = const_cast<char *>(strchr(string, '\260'));
    if (symbol)
        *symbol = ' ';
    symbol = const_cast<char *>(strchr(string, '\''));
    if (symbol)
        *symbol = ' ';

    // parse string into dir character and magnitude string, set the sign from
    // the dir character, and set max_degrees from lat_not_lon
    if (lat_not_lon)
    {
        // read direction character with or without leading spaces
        if (sscanf_s(string, "%[NSns]", buffer, BUF_LEN) != 1 &&
            sscanf_s(string,"%*[ ]%[NSns]", buffer, BUF_LEN) != 1)
            return LAT_DIR_ERROR;

        if (strlen(buffer) != 1)
            return LAT_DIR_ERROR;

        if (buffer[0] == 'N' || buffer[0] == 'n')
            sign = 1.0;
        else
            sign = -1.0;

        // depending on the use of spaces the numeric portion of the string may
        // be divided into 1, 2, or 3 sections
        count = sscanf_s(string, "%*[NSns ]%s %s %s", buffer, BUF_LEN, buffer2, BUF_LEN, buffer3, BUF_LEN);

        // dd.ddddd, ddmm.mmm, or ddmmss.sss - spaceless entry
        if (count == 1)
        {
            symbol = strchr(buffer, '.');
            if (symbol)
                index = symbol - buffer;
            else
                index = strlen(buffer);

            // dd.ddddd or d.ddddd
            if (index <= 2)
            {
                deg = atof(buffer);
                count = 1;
            }
            // ddmm.mmm or dmm.mmm
            else if (index <= 4)
            {
                strcpy_s(buffer2, BUF_LEN, &buffer[index - 2]);
                buffer[index - 2] = '\0';
                deg = atof(buffer);
                min = atof(buffer2);
                count = 2;
            }
            // ddmmss.ss or dmmss.ss
            else
            {
                strcpy_s(buffer3, BUF_LEN, &buffer[index - 2]);
                buffer[index - 2] = '\0';
                strcpy_s(buffer2, BUF_LEN, &buffer[index - 4]);
                buffer[index - 4] = '\0';
                deg = atof(buffer);
                min = atof(buffer2);
                sec = atof(buffer3);
                count = 3;
            }
        }
        // dd mm.mmm or dd mmss.ss or ddmm ss.ss
        else if (count == 2)
        {
            // ddmm ss.ss or dmm ss.ss
            size_t length = strlen(buffer);
            if (length > 2)
            {
                strcpy_s(buffer3, BUF_LEN, &buffer[length - 2]);
                buffer[length - 2] = '\0';
                deg = atof(buffer);
                min = atof(buffer3);
                sec = atof(buffer2);
                count = 3;
            }
            else
            {
                symbol = strchr(buffer2, '.');
                if (symbol)
                    index = symbol - buffer2;
                else
                    index = strlen(buffer2);

                // dd mm.mmm or dd m.mmm
                if (index <= 2)
                {
                    deg = atof(buffer);
                    min = atof(buffer2);
                }
                // dd mmss.ss or dd mss.ss
                else
                {
                    strcpy_s(buffer3, BUF_LEN, &buffer2[index - 2]);
                    buffer2[index - 2] = '\0';
                    deg = atof(buffer);
                    min = atof(buffer2);
                    sec = atof(buffer3);
                    count = 3;
                }
            }
        }
        // dd mm ss.ss (all integer amounts could be 1 or 2 digits)
        else if (count == 3)
        {
            deg = atof(buffer);
            min = atof(buffer2);
            sec = atof(buffer3);
        }
        else
            return LAT_MAGNITUDE_ERROR;

        max_degrees = 90.0;
    }
    else
    {
        // read direction character with or without leading spaces
        if (sscanf_s(string, "%[EWew]", buffer, BUF_LEN) != 1 &&
            sscanf_s(string, "%*[ ]%[EWew]", buffer, BUF_LEN) != 1)
            return LON_DIR_ERROR;

        if (strlen(buffer) != 1)
            return LON_DIR_ERROR;

        if (buffer[0] == 'E' || buffer[0] == 'e')
            sign = 1.0;
        else
            sign = -1.0;

        // depending on the use of spaces the numeric portion of the string may
        // be divided into 1, 2, or 3 sections
        count = sscanf_s(string, "%*[EWew ]%s %s %s", buffer, BUF_LEN, buffer2, BUF_LEN, buffer3, BUF_LEN);

        // ddd.ddddd, dddmm.mmm, or dddmmss.sss - spaceless entry
        if (count == 1)
        {
            symbol = strchr(buffer, '.');
            if (symbol)
                index = symbol - buffer;
            else
                index = strlen(buffer);

            // ddd.ddddd or dd.ddddd or d.ddddd
            if (index <= 3)
            {
                deg = atof(buffer);
                count = 1;
            }
            // dddmm.mmm or ddmm.mmm
            else if (index <= 5)
            {
                strcpy_s(buffer2, BUF_LEN, &buffer[index - 2]);
                buffer[index - 2] = '\0';
                deg = atof(buffer);
                min = atof(buffer2);
                count = 2;
            }
            // dddmmss.ss or ddmmss.ss
            else
            {
                strcpy_s(buffer3, BUF_LEN, &buffer[index - 2]);
                buffer[index - 2] = '\0';
                strcpy_s(buffer2, BUF_LEN, &buffer[index - 4]);
                buffer[index - 4] = '\0';
                deg = atof(buffer);
                min = atof(buffer2);
                sec = atof(buffer3);
                count = 3;
            }
        }
        // ddd mm.mmm or ddd mmss.ss or dddmm ss.ss
        else if (count == 2)
        {
            // dddmm ss.ss or ddmm ss.ss or dmm ss.ss
            size_t length = strlen(buffer);
            if (length > 3)
            {
                strcpy_s(buffer3, BUF_LEN, &buffer[length - 2]);
                buffer[length - 2] = '\0';
                deg = atof(buffer);
                min = atof(buffer3);
                sec = atof(buffer2);
                count = 3;
            }
            else
            {
                symbol = strchr(buffer2, '.');
                if (symbol)
                    index = symbol - buffer2;
                else
                    index = strlen(buffer2);

                // ddd mm.mmm (both integer amounts could be 1 or 2 digits)
                if (index <= 2)
                {
                    deg = atof(buffer);
                    min = atof(buffer2);
                }
                // ddd mmss.ss or ddd mss.ss (degrees could be 1 or 2 digits)
                else
                {
                    strcpy_s(buffer3, BUF_LEN, &buffer2[index - 2]);
                    buffer2[index - 2] = '\0';
                    deg = atof(buffer);
                    min = atof(buffer2);
                    sec = atof(buffer3);
                    count = 3;
                }
            }
        }
        // ddd mm ss.ss (all integer amounts could be 1 or 2 digits)
        else if (count == 3)
        {
            deg = atof(buffer);
            min = atof(buffer2);
            sec = atof(buffer3);
        }
        else
            return LON_MAGNITUDE_ERROR;

        max_degrees = 180.0;
    }

    // range test each field, sec and min are initialize to 0.0
    if (sec < 0.0 || sec >= 60.0)
        return (lat_not_lon) ? LAT_SECONDS_ERROR : LON_SECONDS_ERROR;
    if (min < 0.0 || min >= 60.0)
        return (lat_not_lon) ? LAT_MINUTES_ERROR : LON_MINUTES_ERROR;
    if (deg < 0.0 || deg > max_degrees)
        return (lat_not_lon) ? LAT_DEGREES_ERROR : LON_DEGREES_ERROR;

    // store magnitude in degrees in loc_degrees
    switch(count)
    {
    case 1:
        loc_degrees = deg;
        break;

    case 2:
        if ((double)((int)deg) != deg)
            return (lat_not_lon) ? LAT_DEGREES_ERROR : LON_DEGREES_ERROR;
        loc_degrees = deg + MIN_TO_DEG(min);
        if (loc_degrees > max_degrees)
            return (lat_not_lon) ? LAT_OUT_OF_RANGE : LON_OUT_OF_RANGE;
        break;

    case 3:
        if ((double)((int)deg) != deg)
            return (lat_not_lon) ? LAT_DEGREES_ERROR : LON_DEGREES_ERROR;
        if ((double)((int)min) != min)
            return (lat_not_lon) ? LAT_MINUTES_ERROR : LON_MINUTES_ERROR;
        loc_degrees = deg + MIN_TO_DEG(min) + SEC_TO_DEG(sec);
        if (loc_degrees > max_degrees)
            return (lat_not_lon) ? LAT_OUT_OF_RANGE : LON_OUT_OF_RANGE;
        break;
    }

    *degrees = sign * loc_degrees;

    return SUCCESS;
}

// *************************************************************
// *************************************************************

// Given a datum, WGS84 latitude, WGS 84 longitude, and degrees_per_pixel;
// transform to coordinate string in the given datum.

int CGeoTrans::DLL_lat_lon_to_string(LPCSTR datum,
                                     degrees_t lat, degrees_t lon,
                                     degrees_t dpp,LPSTR lat_lon_str,
                                     int lat_lon_str_len, boolean_t reload_format)
{
    // Convert lat-lon to desired datum if the desired datum is not WGS-84.
    // Note: input lat-lon is a WGS84 lat-lon.
    if (_stricmp(datum, "WGS84") != 0 && _stricmp(datum, "WGE") != 0)
    {
        degrees_t temp_lat = lat;
        degrees_t temp_lon = lon;

        convert_datum(temp_lat, temp_lon, lat, lon,
            "WGE", datum);
    }

    return DLL_lat_lon_to_string(lat, lon, dpp, lat_lon_str, lat_lon_str_len, reload_format);
}

// *************************************************************
// *************************************************************

int CGeoTrans::DLL_lat_lon_to_string(degrees_t lat, degrees_t lon,
                                     degrees_t dpp,LPSTR lat_lon_str,
                                     int lat_lon_str_len, boolean_t reload_format)
{
    char lat_dir;         // N or S
    char lon_dir;         // E or W
    minutes_t min_lat, min_lon;
    double sec_lat, sec_lon;
    int lat_lon_format;
    double mpp;
    double spp;
    char str_lat_lon_format[GEO_MAX_VALUE_LENGTH+1];

    // if load_format, set lat_lon_format from the registry, otherwise
    // use its current value
    // we now ignore this, because we assume reading from the registry is fast, but
    // access to this object is better if it's static and does not depend on member variables
    //if (reload_format)
    {
       char display[GEO_MAX_VALUE_LENGTH+1];
       // get default display
       DLL_get_default_display(display, GEO_MAX_VALUE_LENGTH+1);

       // get coordinate format
       if (strcmp(display, "PRIMARY") == 0)
       {
          DLL_get_primary_lat_lon_format(str_lat_lon_format, GEO_MAX_VALUE_LENGTH+1);
       }
       else
       {
          DLL_get_secondary_lat_lon_format(str_lat_lon_format, GEO_MAX_VALUE_LENGTH+1);
       }
    }

    // set coordinate_format from format string
    if (strcmp(str_lat_lon_format, "DEGREES") == 0)
    {
        lat_lon_format = FORMAT_DEGREES;
    }
    else if (strcmp(str_lat_lon_format, "DEGREES MINUTES") == 0)
    {
        lat_lon_format = FORMAT_DEGREES_MINUTES;
    }
    else if (strcmp(str_lat_lon_format, "DEGREES MINUTES SECONDS") == 0)
    {
        lat_lon_format = FORMAT_DEGREES_MINUTES_SECONDS;
    }
    else
    {
        lat_lon_format = FORMAT_DEGREES_MINUTES;
    }

    // convert to unsigned N, S, E, W decimal degrees
    if (lat < 0.0)
    {
        lat_dir = 'S';
        lat = -lat;
    }
    else
        lat_dir = 'N';

    if (lon < 0.0)
    {
        lon_dir = 'W';
        lon = -lon;
    }
    else
        lon_dir = 'E';

    // get degrees per pixel
    mpp = dpp * 60.0L;
    spp = mpp * 60.0L;

    // convert to string
    switch (lat_lon_format)
    {
    case FORMAT_DEGREES:
        if (dpp > 1.0)
            sprintf_s(lat_lon_str, lat_lon_str_len, "%c  %02.0f\260   %c  %03.0f\260",
            lat_dir, lat, lon_dir, lon);
        else if (dpp > 0.1)
            sprintf_s(lat_lon_str, lat_lon_str_len, "%c  %04.1f\260   %c  %05.1f\260",
            lat_dir, lat, lon_dir, lon);
        else if (dpp > 0.01)
            sprintf_s(lat_lon_str, lat_lon_str_len, "%c  %05.2f\260   %c  %06.2f\260",
            lat_dir, lat, lon_dir, lon);
        else if (dpp > 0.001)
            sprintf_s(lat_lon_str, lat_lon_str_len, "%c  %06.3f\260   %c  %07.3f\260",
            lat_dir, lat, lon_dir, lon);
        else if (dpp > 0.0001)
            sprintf_s(lat_lon_str, lat_lon_str_len, "%c  %07.4f\260   %c  %08.4f\260",
            lat_dir, lat, lon_dir, lon);
        else
            sprintf_s(lat_lon_str, lat_lon_str_len, "%c  %08.5f\260   %c  %09.5f\260",
            lat_dir, lat, lon_dir, lon);
        break;

    case FORMAT_DEGREES_MINUTES:
        min_lat = (lat - ((degrees_t)((int)lat))) * 60.0;
        min_lon = (lon - ((degrees_t)((int)lon))) * 60.0;
        if (mpp > 1.0)
        {
            overflow(&min_lon, &lon, 59.5);
            overflow(&min_lat, &lat, 59.5);
            sprintf_s(lat_lon_str, lat_lon_str_len,
                "%c  %02d\260 %02.0f\'   %c  %03d\260 %02.0f\'",
                lat_dir, (int)lat, min_lat, lon_dir, (int)lon, min_lon);
        }
        else if (mpp > 0.1)
        {
            overflow(&min_lon, &lon, 59.95);
            overflow(&min_lat, &lat, 59.95);
            sprintf_s(lat_lon_str, lat_lon_str_len,
                "%c  %02d\260 %04.1f\'   %c  %03d\260 %04.1f\'",
                lat_dir, (int)lat, min_lat, lon_dir, (int)lon, min_lon);
        }
        else if (mpp > 0.01)
        {
            overflow(&min_lon, &lon, 59.995);
            overflow(&min_lat, &lat, 59.995);
            sprintf_s(lat_lon_str, lat_lon_str_len,
                "%c  %02d\260 %05.2f\'   %c  %03d\260 %05.2f\'",
                lat_dir, (int)lat, min_lat, lon_dir, (int)lon, min_lon);
        }
        else if (mpp > 0.001)
        {
            overflow(&min_lon, &lon, 59.9995);
            overflow(&min_lat, &lat, 59.9995);
            sprintf_s(lat_lon_str, lat_lon_str_len,
                "%c  %02d\260 %06.3f\'   %c  %03d\260 %06.3f\'",
                lat_dir, (int)lat, min_lat, lon_dir, (int)lon, min_lon);
        }
        else
        {
            overflow(&min_lon, &lon, 59.99995);
            overflow(&min_lat, &lat, 59.99995);
            sprintf_s(lat_lon_str, lat_lon_str_len,
                "%c  %02d\260 %07.4f\'   %c  %03d\260 %07.4f\'",
                lat_dir, (int)lat, min_lat, lon_dir, (int)lon, min_lon);
        }
        break;

    case FORMAT_DEGREES_MINUTES_SECONDS:
        min_lat = (lat - ((degrees_t)((int)lat))) * 60.0;
        min_lon = (lon - ((degrees_t)((int)lon))) * 60.0;
        sec_lat = (min_lat - ((degrees_t)((int)min_lat))) * 60.0;
        sec_lon = (min_lon - ((degrees_t)((int)min_lon))) * 60.0;
        if (spp > 1.0)
        {
            if (overflow(&sec_lon, &min_lon, 59.5))
                overflow(&min_lon, &lon, 60.0);
            if (overflow(&sec_lat, &min_lat, 59.5))
                overflow(&min_lat, &lat, 60.0);
            sprintf_s(lat_lon_str, lat_lon_str_len,
                "%c  %02d\260 %02d\' %02.0f\"   %c  %03d\260 %02d\' %02.0f\"",
                lat_dir, (int)lat, (int)min_lat, sec_lat,
                lon_dir, (int)lon, (int)min_lon, sec_lon);
        }
        else if (spp > 0.1)
        {
            if (overflow(&sec_lon, &min_lon, 59.95))
                overflow(&min_lon, &lon, 60.0);
            if (overflow(&sec_lat, &min_lat, 59.95))
                overflow(&min_lat, &lat, 60.0);
            sprintf_s(lat_lon_str, lat_lon_str_len,
                "%c  %02d\260 %02d\' %04.1f\"   %c  %03d\260 %02d\' %04.1f\"",
                lat_dir, (int)lat, (int)min_lat, sec_lat,
                lon_dir, (int)lon, (int)min_lon, sec_lon);
        }
        else
        {
            if (overflow(&sec_lon, &min_lon, 59.995))
                overflow(&min_lon, &lon, 60.0);
            if (overflow(&sec_lat, &min_lat, 59.995))
                overflow(&min_lat, &lat, 60.0);
            sprintf_s(lat_lon_str, lat_lon_str_len,
                "%c  %02d\260 %02d\' %05.2f\"   %c  %03d\260 %02d\' %05.2f\"",
                lat_dir, (int)lat, (int)min_lat, sec_lat,
                lon_dir, (int)lon, (int)min_lon, sec_lon);
        }
        break;

    default:
        return DEGREES_ERROR;
    }

    return SUCCESS;
}  // DLL_lat_lon_to_string()


// *************************************************************
// *************************************************************

/////////////////////////////////////////////////////////////////////////////
//
int CGeoTrans::DLL_datum_valid( LPCSTR datum, long &index_datum )
{
   return GetDatumIndex( datum, index_datum, FALSE ); // Don't log
}


/////////////////////////////////////////////////////////////////////////////
//
void CGeoTrans::DLL_get_primary_datum( LPSTR value, int value_len )
{
    std::string val_str = GEO3_get_registry_string("PrimaryDatum", "WGS84");
    strcpy_s(value, value_len, val_str.c_str());
}

/////////////////////////////////////////////////////////////////////////////
//
void CGeoTrans::DLL_get_secondary_datum( LPSTR value, int value_len )
{
    std::string val_str = GEO3_get_registry_string("SecondaryDatum", "WGS84");
    strcpy_s(value, value_len, val_str.c_str());
}

/////////////////////////////////////////////////////////////////////////////
//

void CGeoTrans::DLL_get_primary_format( LPSTR value, int value_len)
{
    std::string val_str = GEO3_get_registry_string("PrimaryFormat","LAT_LON");
    strcpy_s(value, value_len, val_str.c_str());
}

/////////////////////////////////////////////////////////////////////////////
//
void CGeoTrans::DLL_get_primary_lat_lon_format( LPSTR value, int value_len )
{
   std::string val_str = GEO3_get_registry_string("PrimaryLatLonFormat","DEGREES MINUTES");
   strcpy_s( value, value_len, val_str.c_str() );
}

/////////////////////////////////////////////////////////////////////////////
//
void CGeoTrans::DLL_get_secondary_lat_lon_format( LPSTR value, int value_len )
{
   std::string val_str = GEO3_get_registry_string("SecondaryLatLonFormat","DEGREES MINUTES");
   strcpy_s( value, value_len, val_str.c_str() );
}


/////////////////////////////////////////////////////////////////////////////
//

void CGeoTrans::DLL_get_secondary_format( LPSTR value, int value_len)
{
    std::string val_str = GEO3_get_registry_string("SecondaryFormat","LAT_LON");
    strcpy_s(value, value_len, val_str.c_str());
}


/////////////////////////////////////////////////////////////////////////////
//
void CGeoTrans::DLL_get_default_display( LPSTR value, int value_len )
{
   strcpy_s( value, value_len,
      GEO3_get_registry_string( "DefaultDisplay", "PRIMARY" ).c_str() );
}


/////////////////////////////////////////////////////////////////////////////
//
int CGeoTrans::DLL_set_primary_datum( LPCSTR value )
{
    return GEO3_set_registry_string( "PrimaryDatum", value );
}


/////////////////////////////////////////////////////////////////////////////
//
int CGeoTrans::DLL_set_secondary_datum( LPCSTR value )
{
    return GEO3_set_registry_string( "SecondaryDatum", value );
}


/////////////////////////////////////////////////////////////////////////////
//
int CGeoTrans::DLL_set_primary_format(LPCSTR value)
{
    return GEO3_set_registry_string("PrimaryFormat", value);
}


/////////////////////////////////////////////////////////////////////////////
//
int CGeoTrans::DLL_set_secondary_format( LPCSTR value )
{
    return GEO3_set_registry_string( "SecondaryFormat", value );
}


/////////////////////////////////////////////////////////////////////////////
//
int CGeoTrans::DLL_set_primary_lat_lon_format( LPCSTR value )
{
   return GEO3_set_registry_string( "PrimaryLatLonFormat", value );
}


/////////////////////////////////////////////////////////////////////////////
//
int CGeoTrans::DLL_set_secondary_lat_lon_format(LPCSTR value)
{
    return GEO3_set_registry_string("SecondaryLatLonFormat", value);
}

// performs datum conversion on a geo (public version) - BDF
long CGeoTrans::DLL_convert_datum(
                                   double lat_in, double long_in,// input in degrees (W is minus)
                                   double &lat_out, double &long_out,   // output in degree
                                   LPCSTR sdatum_in,    // input datum of input
                                   LPCSTR sdatum_out )   // input datum of output
{
    // BDF
    // added to enable public datum conversions, particularly for GeoTIFF

    return convert_datum( lat_in, long_in, lat_out, long_out,
        sdatum_in, sdatum_out );
}


/////////////////////////////////////////////////////////////////////////////
//
int CGeoTrans::DLL_set_default_display( LPCSTR value )
{
    return GEO3_set_registry_string( "DefaultDisplay", value );
}


/////////////////////////////////////////////////////////////////////////////
// Get error string associated with a GEO operation error return value
//

void CGeoTrans::DLL_report_string_to_degrees_error(int error)
{
    switch (error)
    {
    case LAT_SECONDS_ERROR:
        MessageBox(NULL, "Error in seconds field of latitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LON_SECONDS_ERROR:
        MessageBox(NULL, "Error in seconds field of longitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LAT_MINUTES_ERROR:
        MessageBox(NULL, "Error in minutes field of latitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LON_MINUTES_ERROR:
        MessageBox(NULL, "Error in minutes field of longitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LAT_DEGREES_ERROR:
        MessageBox(NULL, "Error in degrees field of latitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LON_DEGREES_ERROR:
        MessageBox(NULL, "Error in degrees field of longitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LAT_OUT_OF_RANGE:
        MessageBox(NULL, "Value is outside of valid range for latitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LON_OUT_OF_RANGE:
        MessageBox(NULL, "Value is outside of valid range for longitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LAT_PARSE_ERROR:
        MessageBox(NULL, "Error occurred reading latitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LON_PARSE_ERROR:
        MessageBox(NULL, "Error occurred reading longitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LAT_DIR_ERROR:
        MessageBox(NULL, "Error reading direction character (N/S).",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LON_DIR_ERROR:
        MessageBox(NULL, "Error reading direction character (E/W).",
            "Location Input Error", MB_OK | MB_TASKMODAL);
        break;

    case LAT_MAGNITUDE_ERROR:
        MessageBox(NULL, "Error reading the magnitude of the latitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LON_MAGNITUDE_ERROR:
        MessageBox(NULL, "Error reading the magnitude of the longitude.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LAT_STRING_TOO_LONG:
        MessageBox(NULL, "The latitude must contain no more than 20 characters.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case LON_STRING_TOO_LONG:
        MessageBox(NULL, "The longitude must contain no more than 20 characters.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case DIR_ERROR:
        MessageBox(NULL, "Error reading direction character.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case INVALID_DATUM_CODE:
        MessageBox(NULL, "Error invalid datum code.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case NOT_INITIALIZED:
        MessageBox(NULL, "Error Milgrid not initialized.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case INVALID_INPUT_DATUM:
        MessageBox(NULL, "Error invalid input datum.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case INVALID_OUTPUT_DATUM:
        MessageBox(NULL, "Error invalid output datum.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case INVALID_MILGRID_VALUE:
        MessageBox(NULL, "Input Milgrid string is not valid.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case INVALID_MILGRID_DATUM:
        MessageBox(NULL, "Milgrid is not supported for the current datum.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case INVALID_MILGRID_LATITUDE:
        MessageBox(NULL, "Milgrid is not supported in the polar regions.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case MILGRID_MAJOR_GRID_ZONE:
        MessageBox(NULL, "Invalid Milgrid major grid zone number.  "
            "The zone number must be between 1 and 60.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case MILGRID_MAJOR_GRID_LETTER:
        MessageBox(NULL, "Invalid Milgrid major grid latitude designator.  "
            "This one letter identifier can be any letter except for \'O\' "
            "and \'I\'.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case MILGRID_MINOR_GRID_LETTER:
        MessageBox(NULL, "Invalid Milgrid minor grid designator.  "
            "This two letter identifier can include any letter "
            "except for \'O\' and \'I\'.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case MILGRID_EASTING_NORTHING:
        MessageBox(NULL, "Unable to parse the Easting and Northing portion "
            "of the Milgrid string.\n\nThe Easting and Northing values must be "
            "specified with 1 to 5 digits each,\nand they must contain the same "
            "number of digits.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case MILGRID_UNDEFINED_FOR_DATUM:
        MessageBox(NULL, "The Milgrid string is out of range for the "
            "input datum.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case INVALID_STRING_LENGTH:
        MessageBox(NULL, "Location string inputs must contain at least "
            "2, and no more than, 50 characters.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case INVALID_LOCATION_STRING:
        MessageBox(NULL, "Invalid location string.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;

    case INVALID_UTM_STRING:
        MessageBox(NULL, "Invalid UTM string.",
            "Location Input Error",  MB_OK | MB_TASKMODAL);
        break;
    default:
        MessageBox(NULL, "Unknown error.",
            "Location Input Error", MB_OK | MB_TASKMODAL);
    }
}

// *************************************************************
// *************************************************************

/////////////////////////////////////////////////////////////////////////////
// Insert a character into a string one or more times
// st, position in string for start of insertion
// k,  # of times character is to be inserted
// c,  insert character
// *s, pointer to string
/////////////////////////////////////////////////////////////////////////////

int CGeoTrans::chr_ins(int st, int k, char c,LPSTR s)
{
    int i,j;

    if (st < 0)
        return -2; // invalid start position
    if (k < 0)
        return -1; // invalid # of times for insertion

    // find end of string
    for(i = 0; ; ++i)
        if (s[i] == '\0') break;

    if (st > i)
        return -3; // start position passed null

    // move characters to the right of the insertion
    for(j = i; j >= st; --j)
        s[j+k] = s[j];

    // insert character(s)
    for(j = 0; j < k; ++j)
        s[j+st] = c;

    return k;
}

// *************************************************************
// *************************************************************

/////////////////////////////////////////////////////////////////////////////
// Delete a passed # of rightmost chars in a string
// c   # of chars to delete
// *s  pointer to string
/////////////////////////////////////////////////////////////////////////////

int CGeoTrans::strdelr(int c,LPSTR s)
{
    if (c < 0)
        return -1; // invalid # of chars to delete

    if (c == 0)
        return 0; // no chars to delete

#if 0 // needs unit tests before changing to this implementation
   int l = strlen( s );
   if ( c <= l )
   {
      s[ l - c ] = '\0';
      return c;
   }
   s[ 0 ] = '\0'; // Zero length remainder
   return l;
#else
    int i,j;

    // find end of string
    for (i = 0; ; ++i)
    {
        if (s[i] == '\0')
            break;
    }

    // start deleting
    if (c >= i)
    {
        for(j = 0; j < i; ++j)
            s[j] = '\0';

        return j;
    }

    for (j = i - c; j < i; ++j)
        s[j] = '\0';

    return c;
#endif
}

/////////////////////////////////////////////////////////////////////////////
//

static boolean_t overflow(double *flow_from,
                          double *flow_into, double overflow_threshold)
{
    if (*flow_from >= overflow_threshold)
    {
        *flow_from = 0.0;
        *flow_into += 1.0;
        return TRUE;
    }

    return FALSE;
}

boolean_t is_valid_grid_letter(char c)
{
    if (isalpha(c) == 0 || c == 'I' || c == 'O' || c == 'i' || c == 'o')
        return FALSE;

    return TRUE;
}


// **********************************************************************
// **********************************************************************

int process_milgrid_string(LPCSTR input, int length,LPSTR output, int output_len)
{
    const int TEMP_LEN = 6;
    char temp1[TEMP_LEN] = "00000";
    char temp2[TEMP_LEN] = "00000";
   LPSTR tinput;
    int zone;         // major grid zone #
    char l1, l2, l3;  // grid letters
    int j;
    size_t i, len;

    // copy input string to milgrid where it can be tinkered with
    tinput = new char[length+1];
    strcpy_s(tinput, length+1, input);

    // Remove all white space characters
    i = 0;
    j = 0;
    while (j < length)
    {
        if (tinput[j] != ' ')
            output[i++] = tinput[j];

        j++;
    }
    output[i] = '\0';
    delete [] tinput;

    // get string length, must be 7, 9, 11, 13, or 15 for a valid
    // milgrid string
    len = strlen(output);
    if (len <= 5)
        return INVALID_MILGRID_VALUE;

    // must have number followed by three letters
    if (sscanf_s(output, "%d%c%c%c", &zone, &l1, 1, &l2, 1, &l3, 1) != 4)
        return INVALID_MILGRID_VALUE;

    // validate major grid zone
    if (zone < 1 || zone > 60)
        return MILGRID_MAJOR_GRID_ZONE;

    // validate major grid letter
    if (!is_valid_grid_letter(l1))
        return MILGRID_MAJOR_GRID_LETTER;

    // validate minor grid letters
    if (!is_valid_grid_letter(l2) || !is_valid_grid_letter(l3))
        return MILGRID_MINOR_GRID_LETTER;

    // check for minimum and maximum length for valid northing and easting
    if (len < 7 || len > 15)
        return MILGRID_EASTING_NORTHING;

    // there must be an even number of characters in the northing and easting
    len -= 5;
    if (len % 2 != 0)
        return MILGRID_EASTING_NORTHING;

    // make sure all northing and easting characters are numbers
    for (i=5; i < (len+5); i++)
    {
        if (!isdigit(output[i]))
            return MILGRID_EASTING_NORTHING;
    }

    // pad northing and easting with zeroes if necessary
    // length is even so split and pad with zeros (e.g., 55 -> 5000050000)
    strncpy_s(temp1, TEMP_LEN, &output[5], len/2);
    strncpy_s(temp2, TEMP_LEN, &output[5+len/2], len/2);
    // copy temp1 temp2 back into milgrid[]
    strncpy_s(&output[5], output_len - 5, temp1, 5);
    strncpy_s(&output[10], output_len - 10, temp2, 5);

    return SUCCESS;
}
// end of process_milgrid_string

// **********************************************************************
// **********************************************************************

// the utm must be of the form "16S 111111 1111111"
int process_utm_string(LPCSTR input, int *zone, double *easting, double *northing)
{
    char temp1[6] = "00000";
    char temp2[6] = "00000";
    size_t pos;
    int j, tzone, cnt, zonepos;
    size_t k, length;
    char sline[81];
    const int UTM_LEN = 81;
    char utm[UTM_LEN];
    char grid_letter;
    double tnorth;
    BOOL new_form = FALSE;
    BOOL notdone;

    // copy input string to utm where it can be tinkered with
    strcpy_s(utm, UTM_LEN, input);

    // trim the input string
    k = strlen(utm) - 1;
    while (utm[k] == ' ' && k >= 0)
    {
        utm[k] = '\0';
        k--;
    }

    while (utm[0] == ' ')
    {
        length = strlen(utm);
        for (k = 0; k <= length; k++)
            utm[k] = input[k+1];
    }

    // make sure the input string does not exceed the length of milgrid
    length = strlen(utm);
    if (length > 50)
        return INVALID_UTM_STRING;

    if (length < 11)
        return INVALID_UTM_STRING;

    pos = 0;

    if ((utm[0] == 'N') || (utm[0] == 'S'))
        new_form = TRUE;

    if (new_form)
    {
        if (utm[0] == 'S')
            grid_letter = 'D';
        else
            grid_letter = 'R';

        // get the zone
        notdone = TRUE;
        pos = 1;
        cnt = 0;
        zonepos = 0;
        if (utm[pos] == ' ')
            pos = 2;

        if ((utm[pos] < '0') || (utm[pos] > '9'))
            return MILGRID_MAJOR_GRID_ZONE;

        sline[zonepos] = utm[pos];
        zonepos++;
        pos++;
        if ((utm[pos] < '0') || (utm[pos] > '9'))
            return MILGRID_MAJOR_GRID_ZONE;
        sline[zonepos] = utm[pos];
        pos++;
        sline[pos] = '\0';
        *zone = atoi(sline);
    }
    else
    {
        // old form

        if ((utm[0] < '0') || (utm[0] > '9'))
        {
            return MILGRID_MAJOR_GRID_ZONE;
        }

        sline[0] = utm[0];
        sline[1] = '\0';
        pos = 1;
        if ((utm[0] < '0') || (utm[0] > '9'))
        {
            sline[0] = '0';
            sline[1] = utm[0];
            sline[2] = '\0';
        }
        else
        {
            sline[1] = utm[1];
            sline[2] = '\0';
        }
        tzone = atoi(sline);

        pos = 2;

        // validate major grid zone
        if (tzone < 1 || tzone > 60)
            return MILGRID_MAJOR_GRID_ZONE;

        grid_letter = utm[pos];
        pos++;

        if (!is_valid_grid_letter(grid_letter))
            return MILGRID_MAJOR_GRID_LETTER;


        *zone = tzone;
    }

    // find the easting
    while ((pos < length) && ((utm[pos] < '0') || (utm[pos] > '9')))
        pos++;

    if (pos >= length)
        return INVALID_UTM_STRING;

    j = 0;
    while ((pos < length) && (utm[pos] >= '0') && (utm[pos] <= '9'))
    {
        sline[j] = utm[pos];
        j++;
        pos++;
    }

    sline[j] = '\0';

    *easting = atof(sline);

    if (*easting > 999999.9)
        return INVALID_UTM_STRING;

    // find the northing
    while ((pos < length) && ((utm[pos] < '0') || (utm[pos] > '9')))
        pos++;

    if (pos >= length)
        return INVALID_UTM_STRING;

    j = 0;
    while ((pos < length) && (utm[pos] >= '0') && (utm[pos] <= '9'))
    {
        sline[j] = utm[pos];
        j++;
        pos++;
    }

    sline[j] = '\0';

    tnorth = atof(sline);
    if (tnorth > 9999999.9)
        return INVALID_UTM_STRING;

    if (grid_letter < 'N')
        *northing = -tnorth;
    else
        *northing = tnorth;

    return SUCCESS;
}
// end of process_utm_string


// **********************************************************************
// **********************************************************************

#ifndef _WIN32

// POSIX port: coordinate display-format preferences are held in memory for
// the session only. (On Windows they persist under
// HKCU\Software\XPlan\FalconView\CoordinateFormat.)
#include <map>

static std::map<std::string, std::string>& GEO3_format_prefs()
{
    static std::map<std::string, std::string> prefs;
    return prefs;
}

static std::string GEO3_get_registry_string( LPCSTR value_name,
                                     LPCSTR default_value /*= NULL*/)
{
    std::map<std::string, std::string>& prefs = GEO3_format_prefs();
    std::map<std::string, std::string>::const_iterator it =
        prefs.find(value_name);
    if (it != prefs.end())
        return it->second;
    return default_value == NULL ? std::string("") : std::string(default_value);
}

static boolean_t GEO3_set_registry_string(LPCSTR value_name, LPCSTR value)
{
    GEO3_format_prefs()[value_name] = value;
    return TRUE;
}

#else  // _WIN32

static std::string GEO3_get_registry_string( LPCSTR value_name,
                                     LPCSTR default_value /*= NULL*/)
{
    char buffer[256];
    DWORD buffer_size = 256;
    DWORD type;

    LPCSTR sub_key = "Software\\XPlan\\FalconView\\CoordinateFormat";

    if (GEO3_read_registry(sub_key, value_name, &type,
        (PBYTE) &buffer, &buffer_size) == SUCCESS)
    {
        // make sure the type is correct
        if (type == REG_SZ)
            return std::string(buffer);
    }

    // return a string with the result in case of failure
    if (default_value == NULL)
        return "";

    return std::string(default_value);
}

static int GEO3_read_registry(LPCSTR sub_key, LPCSTR value_name,
                       DWORD* type, BYTE* storage_loc, DWORD* storage_size)
{
    HKEY    key;

    //open key
    if (RegOpenKeyEx(HKEY_CURRENT_USER, sub_key, 0, KEY_READ, &key) !=
        ERROR_SUCCESS)
    {
        //INFO_report("RegOpenKeyEx failed.");
        return FAILURE;
    }

    //query value
    if (RegQueryValueEx(key, value_name, 0, type, storage_loc,
        storage_size) != ERROR_SUCCESS)
    {
        RegCloseKey(key);
        //INFO_report("RegOpenKeyEx failed.");
        return FAILURE;
    }

    //close key
    if (RegCloseKey(key) != ERROR_SUCCESS)
    {
        //TRACE("RegCloseKey failed.");
    }

    return SUCCESS;   //success
}


static boolean_t GEO3_set_registry_string(LPCSTR value_name, LPCSTR value)
{
    LPCSTR sub_key = "Software\\XPlan\\FalconView\\CoordinateFormat";

    return GEO3_write_registry(sub_key, value_name,
        REG_SZ, (const BYTE*) value, (DWORD) strlen(value)+1);
}

static int GEO3_write_registry(LPCSTR sub_key,
                        LPCSTR value_name, DWORD type, const BYTE* storage_loc,
                        DWORD storage_size)
{
    HKEY        key;
    DWORD       dwDisposition;

    //open key
    if (RegOpenKeyEx(HKEY_CURRENT_USER, sub_key, 0, KEY_WRITE, &key)
        != ERROR_SUCCESS)
    {
        if (RegCreateKeyEx(HKEY_CURRENT_USER, sub_key, 0, "", REG_OPTION_NON_VOLATILE,
            KEY_WRITE, NULL, &key, &dwDisposition) != ERROR_SUCCESS)
        {
            //ERR_report("RegCreateKeyEx failed.");
            return FAILURE;
        }
    }

    //set key
    if (RegSetValueEx(key, value_name, 0, type, storage_loc,
        storage_size) != ERROR_SUCCESS)
    {
        //ERR_report("RegSetValueEx failed.");
        return FAILURE;
    }

    //close key
    if (RegCloseKey(key) != ERROR_SUCCESS)
    {
        //ERR_report("RegCloseKey failed.");
    }
    return SUCCESS;
}

#endif  // _WIN32

BOOL GeoToText( double lat, double lon, 
   LPSTR slat, int slat_len, 
   LPSTR slon, int slon_len ) 
{ 
   CGeoTrans geotrans; 
   return geotrans.geo2text(lat, lon, slat, slat_len, slon, slon_len); 
} 
 
int CalcUtmString(LPCSTR datum, degrees_t lat, degrees_t lon, 
   LPSTR str, int str_len) 
{ 
   CGeoTrans geotrans; 
   return geotrans.DLL_calc_utm_string(datum, lat, lon, str, str_len); 
} 
 
int CalcMilgridString(LPCSTR datum, degrees_t lat, degrees_t lon, 
   LPSTR str, int str_len) 
{ 
   CGeoTrans geotrans; 
   return geotrans.DLL_calc_milgrid_string(datum, lat, lon, str, str_len); 
} 
 
int CalcGarsString(degrees_t lat, degrees_t lon, LPSTR str, int str_len) 
{ 
   CGeoTrans geotrans; 
   return geotrans.DLL_calc_gars_string(lat, lon, str, str_len); 
} 

