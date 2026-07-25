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



#ifdef _WIN32
#include "stdafx.h"
#endif
#include "DtedReader.h"
#ifdef _WIN32
#include "ComErrorObject.h"
#else
// Cross-platform port: standard headers replace the precompiled stdafx.h, and
// fv_compat.h supplies the Win32/secure-CRT shims this file uses (sprintf_s,
// strncpy_s, THROW_ERROR_MSG, GetLastError, __min/__max, BYTE/BOOL/COLORREF).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "fv_compat.h"
#endif

//
// This class is used to read DTED files as defined in MIL-PRF-89020A
//

#define FEET_TO_METERS(feet)      (((double)(feet)) * 0.3048)
#define METERS_TO_FEET(meters)    (((double)(meters)) * 3.28083989501)

//
// Default constructor
//
CDtedReader::CDtedReader( )
{
   int i;

   // Initially not open.
   m_open = false;

   // Initialize error message.
   m_error_message = "";

   // Initialize column buffers.
   m_col_buffer_1 = NULL;
   m_col_buffer_2 = NULL;

   m_NumShades = 32;
   m_num_elev_pts = 5;
   m_elev_breakpts[4] = 12500.0;
   m_elev_breakpts[3] = 10000.0;
   m_elev_breakpts[2] =  7500.0;
   m_elev_breakpts[1] =  5000.0;
   m_elev_breakpts[0] =  2500.0;
   
   // Initialize color table as a grayscale plus blue for water and black for missing data.
   for( i = 0; i <= DTED_READER_NUM_COLORS-4; i++ )
   {
      m_color_table[i][0] =
         (unsigned char)( (double)i / (DTED_READER_NUM_COLORS-3) * 255 + 0.5 );
      m_color_table[i][1] = m_color_table[i][0];
      m_color_table[i][2] = m_color_table[i][0];
   }
   // Sealevel (elevation 0) will be shown in blue.
   m_color_table[i][0] = 0;
   m_color_table[i][1] = 0;
   m_color_table[i][2] = 255;
   i++;
   // Missing data (elevation -32767) will be shown in black.
   m_color_table[i][0] = 0;
   m_color_table[i][1] = 0;
   m_color_table[i][2] = 0;

   // init the contour color
   m_contour_color = RGB(0,0,255);

   // Initialize tile data.
   m_num_tiles = 0;
   m_tile_ids = NULL;
   m_tile_min_hpixs = NULL;
   m_tile_min_vpixs = NULL;
   m_tile_max_hpixs = NULL;
   m_tile_max_vpixs = NULL;
   m_tile_ll_lats = NULL;
   m_tile_ll_lons = NULL;
   m_tile_ur_lats = NULL;
   m_tile_ur_lons = NULL;

	// Initialize Parameterized values
   m_ContourLinesOn = FALSE;
	m_ContourInterval = 1000;
	m_ContourWidth = 0;
	m_DisplayMode = 0;
	m_ElevationBands = "5/2500/5000/7500/10000/12500";
	m_SlopeBands = "5/3/6/9/12/15";
	m_ColorBands = "6/050200062/133175075/200200050/225158025/250125000/250050000";

   m_num_color_bands = 6;
   m_color_bands[0] = RGB(50, 200, 62);
   m_color_bands[1] = RGB(133, 175, 75);
   m_color_bands[2] = RGB(200, 200, 50);
   m_color_bands[3] = RGB(225, 158, 25);
   m_color_bands[4] = RGB(250, 125, 0);
   m_color_bands[5] = RGB(250, 50, 0);


   //m_num_elev_pts = 0;
   m_num_slope_pts = 0;
//   set_elevation_bands(0,0);
   m_IsContourMono = false;
   m_flat_is_black = true;

   setup_color_table();

   m_dted_safe_colors = 0;

   // Initialize to closed state.
   close( );
}

//
// This constuctor that also opens a dted file. To check if open succeeded, call
// is_open( ).
//
CDtedReader::CDtedReader( std::string filename )
{
   int i;

   // initially not open
   m_open = false;

   // Initialize error message.
   m_error_message = "";

   // Initialize column buffers.
   m_col_buffer_1 = NULL;
   m_col_buffer_2 = NULL;

   m_NumShades = 32;
   m_num_elev_pts = 5;
   m_elev_breakpts[4] = 12500.0;
   m_elev_breakpts[3] = 10000.0;
   m_elev_breakpts[2] =  7500.0;
   m_elev_breakpts[1] =  5000.0;
   m_elev_breakpts[0] =  2500.0;

   // Initialize color table as a grayscale plus blue for water and black for missing data.
   for( i = 0; i <= DTED_READER_NUM_COLORS-4; i++ )
   {
      m_color_table[i][0] =
         (unsigned char)( (double)i / (DTED_READER_NUM_COLORS-3) * 255 + 0.5 );
      m_color_table[i][1] = m_color_table[i][0];
      m_color_table[i][2] = m_color_table[i][0];
   }
   // Contour will be shown in blue.
   m_color_table[i][0] = 0;
   m_color_table[i][1] = 0;
   m_color_table[i][2] = 255;
   i++;
   // Sealevel (elevation 0) will be shown in blue.
   m_color_table[i][0] = 0;
   m_color_table[i][1] = 0;
   m_color_table[i][2] = 255;
   i++;
   // Missing data (elevation -32767) will be shown in black.
   m_color_table[i][0] = 0;
   m_color_table[i][1] = 0;
   m_color_table[i][2] = 0;

   // Initialize tile data.
   m_num_tiles = 0;
   m_tile_ids = NULL;
   m_tile_min_hpixs = NULL;
   m_tile_min_vpixs = NULL;
   m_tile_max_hpixs = NULL;
   m_tile_max_vpixs = NULL;
   m_tile_ll_lats = NULL;
   m_tile_ll_lons = NULL;
   m_tile_ur_lats = NULL;
   m_tile_ur_lons = NULL;

   setup_color_table();

   // Initialize to closed state.
   close( );

   // Initialize Parameterized values
   m_ContourLinesOn = FALSE;
   m_ContourInterval = 1000;
   m_ContourWidth = 0;
   m_DisplayMode = 0;
   //num_elev_pts = 0;
   m_num_slope_pts = 0;

   m_dted_safe_colors = 0;

//   set_elevation_bands(0,0);

   // Try to open the file.
   open( filename );
}

//
// Detructor
//
CDtedReader::~CDtedReader( )
{
   // Return to closed state on destruction.
   close( );

//   if (m_pElevationBands)
//      delete [] m_pElevationBands;
}

//
// Open a dted file. Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
//
int CDtedReader::open( std::string filename )
{
   int i, j, index;
   int shifted_lat, shifted_lon, tile_delta_hpix, tile_delta_vpix;
   int tile_min_hpix, tile_min_vpix, tile_max_hpix, tile_max_vpix;
   const int LEN = 10;
   char temp_s[LEN];
	int pos;
   const int BUF_LEN = 80;
   char buf[BUF_LEN];

   // First, re-initialize to closed state.
   close( );

   // Attempt to open the file for reading.
   m_file_ptr = NULL;
   fopen_s( &m_file_ptr, filename.c_str(), "rb" );
   if( m_file_ptr == NULL )
   {
      // error opening file
      THROW_ERROR_MSG(HRESULT_FROM_WIN32(GetLastError()), "open", "Error opening DTED frame")
   }

   // Attempt to read user header label (UHL).
   if( fread( &m_uhl, sizeof(user_header_label), 1, m_file_ptr ) != 1 )
   {
      m_error_message = "Error reading User Header Label (UHL) in file " + filename;
      goto FAIL;
   }

   // See if the UHL contains the expected recognition sentinel
   if( strncmp( m_uhl.recognition_sentinel, "UHL", 3 ) != 0 )
   {
      m_error_message = "User Header Label (UHL) not found in file " + filename;
      goto FAIL;
   }

   // Get the origin longitude.
   strncpy_s( temp_s, LEN, m_uhl.origin_lon, 3 );        // degrees
   temp_s[3] = 0;
   m_origin_lon = atoi( temp_s );
   strncpy_s( temp_s, LEN, &m_uhl.origin_lon[3], 2 );    // minutes
   temp_s[2] = 0;
   m_origin_lon += atoi( temp_s ) / 60.0;    
   strncpy_s( temp_s, LEN, &m_uhl.origin_lon[5], 2 );    // seconds
   temp_s[2] = 0;
   m_origin_lon += atoi( temp_s ) / 3600.0;
   if( m_uhl.origin_lon[7] == 'W' || m_uhl.origin_lon[7] == 'w' )               // hemisphere
      m_origin_lon *= -1;
   // Check for validity.
   if( m_origin_lon < -180.0 || m_origin_lon > 180.0 )
   {
      const int BUF_LEN = 80;
		char buf[BUF_LEN];
		sprintf_s(buf, BUF_LEN, "Origin longitude is invalid: %lf", m_origin_lon);
		m_error_message = std::string(buf);
      goto FAIL;
   }

   // Get the origin latitude.
   strncpy_s( temp_s, LEN, m_uhl.origin_lat, 3 );        // degrees
   temp_s[3] = 0;
   m_origin_lat = atoi( temp_s );
   strncpy_s( temp_s, LEN, &m_uhl.origin_lat[3], 2 );    // minutes
   temp_s[2] = 0;
   m_origin_lat += atoi( temp_s ) / 60.0;    
   strncpy_s( temp_s, LEN, &m_uhl.origin_lat[5], 2 );    // seconds
   temp_s[2] = 0;
   m_origin_lat += atoi( temp_s ) / 3600.0;
   if(( m_uhl.origin_lat[7] == 'S' ) || ( m_uhl.origin_lat[7] == 's' ))               // hemisphere
      m_origin_lat *= -1;
   // Check for validity.
   if( m_origin_lat < -90.0 || m_origin_lat > 90.0 )
   {
		sprintf_s(buf, BUF_LEN, "Origin longitude is invalid: %lf", m_origin_lat);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Get the longitude data interval.
   strncpy_s( temp_s, LEN, m_uhl.interval_lon, 4 );        // tenths of arc seconds
   temp_s[4] = 0;
   m_interval_lon = atoi( temp_s ) / 3600.0 / 10.0;

   // Get the latitude data interval.
   strncpy_s( temp_s, LEN, m_uhl.interval_lat, 4 );        // tenths of arc seconds
   temp_s[4] = 0;
   m_interval_lat = atoi( temp_s ) / 3600.0 / 10.0;

   m_grid_spacing = (m_interval_lat / 90.0) * DTED_READER_EARTH_RADIUS_IN_METERS * DTED_READER_HALF_PI;
   m_grid_spacing = DTED_READER_METERS_TO_FEET(m_grid_spacing);

   // Get the absolute vertical accuracy.
   strncpy_s( temp_s, LEN, m_uhl.abs_vert_accuracy, 4 );        // meters
   temp_s[4] = 0;
   m_abs_vertical_accuracy = atoi( temp_s );

   // Get the security code.
   strncpy_s( temp_s, LEN, m_uhl.security_code, 3 );
   temp_s[3] = 0;
   m_security_code = temp_s;

	pos = m_security_code.find_last_not_of(' ');
	if (pos != std::string::npos) 
		m_security_code = m_security_code.substr(0, pos + 1);

   // Get the number of longitude lines.
   strncpy_s( temp_s, LEN, m_uhl.num_lon_lines, 4 );
   temp_s[4] = 0;
   m_num_lon_lines = atoi( temp_s );

   // Get the number of latitude points per longitude line.
   strncpy_s( temp_s, LEN, m_uhl.num_lat_points, 4 );
   temp_s[4] = 0;
   m_num_lat_points = atoi( temp_s );

   // Get multiple accuracy.
   if( m_uhl.multiple_accuracy == '1' )
      m_multiple_accuracy = true;
   else
      m_multiple_accuracy = false;


   // Attempt to read data set identification record (DSI).
   if( fread( &m_dsi, sizeof(data_set_identification_record), 1, m_file_ptr ) != 1 )
   {
      m_error_message = "Error reading Data Set Identification record (DSI) in file " + filename;
      goto FAIL;
   }

   // See if the DSI contains the expected recognition sentinel
   if( strncmp( m_dsi.recognition_sentinel, "DSI", 3 ) != 0 )
   {
      m_error_message = "Data Set Identification record (DSI) not found in file " + filename;
      goto FAIL;
   }

   // Get the series designator.
   strncpy_s( temp_s, LEN, m_dsi.series_designator, 5 );
   temp_s[5] = 0;
   m_series_designator = temp_s;

   // Get the vertical datum.
   strncpy_s( temp_s, LEN, m_dsi.vertical_datum, 3 );
   temp_s[3] = 0;
   m_vertical_datum = temp_s;

   // Get the horizontal datum.
   strncpy_s( temp_s, LEN, m_dsi.horizontal_datum, 5 );
   temp_s[5] = 0;
   m_horizontal_datum = temp_s;

   // Get the bounding rectangle sw latitude.
   strncpy_s( temp_s, LEN, m_dsi.data_sw_lat, 2 );        // degrees
   temp_s[2] = 0;
   m_data_sw_lat = atoi( temp_s );
   strncpy_s( temp_s, LEN, &m_dsi.data_sw_lat[2], 2 );    // minutes
   temp_s[2] = 0;
   m_data_sw_lat += atoi( temp_s ) / 60.0;    
   strncpy_s( temp_s, LEN, &m_dsi.data_sw_lat[4], 2 );    // seconds
   temp_s[2] = 0;
   m_data_sw_lat += atoi( temp_s ) / 3600.0;
   if(( m_dsi.data_sw_lat[6] == 'S' ) || ( m_dsi.data_sw_lat[6] == 's' ))               // hemisphere
      m_data_sw_lat *= -1;
   // Check for validity.
   if( m_data_sw_lat < -90.0 || m_data_sw_lat > 90.0 )
   {
		sprintf_s(buf, BUF_LEN, "Bounding rectangle sw latitude is invalid: %lf", m_data_sw_lat);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Get the bounding rectangle sw longitude.
   strncpy_s( temp_s, LEN, m_dsi.data_sw_lon, 3 );        // degrees
   temp_s[3] = 0;
   m_data_sw_lon = atoi( temp_s );
   strncpy_s( temp_s, LEN, &m_dsi.data_sw_lon[3], 2 );    // minutes
   temp_s[2] = 0;
   m_data_sw_lon += atoi( temp_s ) / 60.0;    
   strncpy_s( temp_s, LEN, &m_dsi.data_sw_lon[5], 2 );    // seconds
   temp_s[2] = 0;
   m_data_sw_lon += atoi( temp_s ) / 3600.0;
   if( m_dsi.data_sw_lon[7] == 'W' || m_dsi.data_sw_lon[7] == 'w' )               // hemisphere
      m_data_sw_lon *= -1;
   // Check for validity.
   if( m_data_sw_lon < -180.0 || m_data_sw_lon > 180.0 )
   {
		sprintf_s(buf, BUF_LEN, "Bounding rectangle sw longitude is invalid: %lf", m_data_sw_lon);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Get the bounding rectangle nw latitude.
   strncpy_s( temp_s, LEN, m_dsi.data_nw_lat, 2 );        // degrees
   temp_s[2] = 0;
   m_data_nw_lat = atoi( temp_s );
   strncpy_s( temp_s, LEN, &m_dsi.data_nw_lat[2], 2 );    // minutes
   temp_s[2] = 0;
   m_data_nw_lat += atoi( temp_s ) / 60.0;    
   strncpy_s( temp_s, LEN, &m_dsi.data_nw_lat[4], 2 );    // seconds
   temp_s[2] = 0;
   m_data_nw_lat += atoi( temp_s ) / 3600.0;
   if(( m_dsi.data_nw_lat[6] == 'S' ) || ( m_dsi.data_nw_lat[6] == 's' ))               // hemisphere
      m_data_nw_lat *= -1;
   // Check for validity.
   if( m_data_nw_lat < -90.0 || m_data_nw_lat > 90.0 )
   {
		sprintf_s(buf, BUF_LEN, "Bounding rectangle nw latitude is invalid: %lf", m_data_nw_lat);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Get the bounding rectangle nw longitude.
   strncpy_s( temp_s, LEN, m_dsi.data_nw_lon, 3 );        // degrees
   temp_s[3] = 0;
   m_data_nw_lon = atoi( temp_s );
   strncpy_s( temp_s, LEN, &m_dsi.data_nw_lon[3], 2 );    // minutes
   temp_s[2] = 0;
   m_data_nw_lon += atoi( temp_s ) / 60.0;    
   strncpy_s( temp_s, LEN, &m_dsi.data_nw_lon[5], 2 );    // seconds
   temp_s[2] = 0;
   m_data_nw_lon += atoi( temp_s ) / 3600.0;
   if( m_dsi.data_nw_lon[7] == 'W' || m_dsi.data_nw_lon[7] == 'w' )               // hemisphere
      m_data_nw_lon *= -1;
   // Check for validity.
   if( m_data_nw_lon < -180.0 || m_data_nw_lon > 180.0 )
   {
		char buf[80];
		sprintf_s(buf, BUF_LEN, "Bounding rectangle nw longitude is invalid: %lf", m_data_nw_lon);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Get the bounding rectangle ne latitude.
   strncpy_s( temp_s, LEN, m_dsi.data_ne_lat, 2 );        // degrees
   temp_s[2] = 0;
   m_data_ne_lat = atoi( temp_s );
   strncpy_s( temp_s, LEN, &m_dsi.data_ne_lat[2], 2 );    // minutes
   temp_s[2] = 0;
   m_data_ne_lat += atoi( temp_s ) / 60.0;    
   strncpy_s( temp_s, LEN, &m_dsi.data_ne_lat[4], 2 );    // seconds
   temp_s[2] = 0;
   m_data_ne_lat += atoi( temp_s ) / 3600.0;
   if(( m_dsi.data_ne_lat[6] == 'S' ) || ( m_dsi.data_ne_lat[6] == 's' ))               // hemisphere
      m_data_ne_lat *= -1;
   // Check for validity.
   if( m_data_ne_lat < -90.0 || m_data_ne_lat > 90.0 )
   {
		char buf[80];
		sprintf_s(buf, BUF_LEN, "Bounding rectangle ne longitude is invalid: %lf", m_data_ne_lat);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Get the bounding rectangle ne longitude.
   strncpy_s( temp_s, LEN, m_dsi.data_ne_lon, 3 );        // degrees
   temp_s[3] = 0;
   m_data_ne_lon = atoi( temp_s );
   strncpy_s( temp_s, LEN, &m_dsi.data_ne_lon[3], 2 );    // minutes
   temp_s[2] = 0;
   m_data_ne_lon += atoi( temp_s ) / 60.0;    
   strncpy_s( temp_s, LEN, &m_dsi.data_ne_lon[5], 2 );    // seconds
   temp_s[2] = 0;
   m_data_ne_lon += atoi( temp_s ) / 3600.0;
   if( m_dsi.data_ne_lon[7] == 'W' || m_dsi.data_ne_lon[7] == 'w' )               // hemisphere
      m_data_ne_lon *= -1;
   // Check for validity.
   if( m_data_ne_lon < -180.0 || m_data_ne_lon > 180.0 )
   {
		char buf[80];
		sprintf_s(buf, BUF_LEN, "Bounding rectangle ne longitude is invalid: %lf", m_data_ne_lon);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Get the bounding rectangle sw latitude.
   strncpy_s( temp_s, LEN, m_dsi.data_se_lat, 2 );        // degrees
   temp_s[2] = 0;
   m_data_se_lat = atoi( temp_s );
   strncpy_s( temp_s, LEN, &m_dsi.data_se_lat[2], 2 );    // minutes
   temp_s[2] = 0;
   m_data_se_lat += atoi( temp_s ) / 60.0;    
   strncpy_s( temp_s, LEN, &m_dsi.data_se_lat[4], 2 );    // seconds
   temp_s[2] = 0;
   m_data_se_lat += atoi( temp_s ) / 3600.0;
   if(( m_dsi.data_se_lat[6] == 'S' ) || ( m_dsi.data_se_lat[6] == 's' ))               // hemisphere
      m_data_se_lat *= -1;
   // Check for validity.
   if( m_data_se_lat < -90.0 || m_data_se_lat > 90.0 )
   {
		char buf[80];
		sprintf_s(buf, BUF_LEN, "Bounding rectangle se latitude is invalid: %lf", m_data_se_lat);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Get the bounding rectangle se longitude.
   strncpy_s( temp_s, LEN, m_dsi.data_se_lon, 3 );        // degrees
   temp_s[3] = 0;
   m_data_se_lon = atoi( temp_s );
   strncpy_s( temp_s, LEN, &m_dsi.data_se_lon[3], 2 );    // minutes
   temp_s[2] = 0;
   m_data_se_lon += atoi( temp_s ) / 60.0;    
   strncpy_s( temp_s, LEN, &m_dsi.data_se_lon[5], 2 );    // seconds
   temp_s[2] = 0;
   m_data_se_lon += atoi( temp_s ) / 3600.0;
   if( m_dsi.data_se_lon[7] == 'W' || m_dsi.data_se_lon[7] == 'w' )               // hemisphere
      m_data_se_lon *= -1;
   // Check for validity.
   if( m_data_se_lon < -180.0 || m_data_se_lon > 180.0 )
   {
		char buf[80];
		sprintf_s(buf, BUF_LEN, "Bounding rectangle se longitude is invalid: %lf", m_data_se_lon);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Determine if it is a partial cell
   if( m_dsi.partial_cell_indicator[0] == '0' && m_dsi.partial_cell_indicator[1] == '0' )
      m_partial_cell = false;
   else
      m_partial_cell = true;

   // Read accuracy record.
   if( fread( &m_acc, sizeof(accuracy_record), 1, m_file_ptr ) != 1 )
   {
      m_error_message = "Error reading Accuracy record (ACC) in file " + filename;
      goto FAIL;
   }
   
   // Read data record.
   if( fread( &m_data, sizeof(data_record), 1, m_file_ptr ) != 1 )
   {
      m_error_message = "Error reading Data record in file " + filename;
      goto FAIL;
   }

   // Allocate memory for column buffers
   m_col_buffer_1 = new short[m_num_lat_points];
   if( m_col_buffer_1 == NULL )
   {
      m_error_message = "Error allocating buffer memory!";
      goto FAIL;
   }
   m_col_buffer_2 = new short[m_num_lat_points];
   if( m_col_buffer_2 == NULL )
   {
      m_error_message = "Error allocating buffer memory!";
      goto FAIL;
   }
   
   m_open = true;
   m_filename = filename;
   m_image_width = m_num_lon_lines;
   m_image_height = m_num_lat_points;
   m_image_sw_lat = m_data_sw_lat;
   m_image_sw_lon = m_data_sw_lon;
   m_image_nw_lat = m_data_nw_lat;
   m_image_nw_lon = m_data_nw_lon;
   m_image_ne_lat = m_data_ne_lat;
   m_image_ne_lon = m_data_ne_lon;
   m_image_se_lat = m_data_se_lat;
   m_image_se_lon = m_data_se_lon;
//   m_light_dir_x = sqrt( 1.0/3.0 );    // From upper left.
//   m_light_dir_y = -m_light_dir_x;
//   m_light_dir_z = -m_light_dir_x;
   m_exaggeration_factor = 1.0f;

   // Initialize tile data.
   m_num_tiles = DTED_NUM_HORIZONTAL_TILES * DTED_NUM_VERTICAL_TILES;
   // Allocate memory for tile_data.
   m_tile_ids = new int[m_num_tiles];
   m_tile_min_hpixs = new int[m_num_tiles];
   m_tile_min_vpixs = new int[m_num_tiles];
   m_tile_max_hpixs = new int[m_num_tiles];
   m_tile_max_vpixs = new int[m_num_tiles];
   m_tile_ll_lats = new double[m_num_tiles];
   m_tile_ll_lons = new double[m_num_tiles];
   m_tile_ur_lats = new double[m_num_tiles];
   m_tile_ur_lons = new double[m_num_tiles];
   if( m_tile_ids == NULL || m_tile_min_hpixs == NULL || m_tile_min_vpixs == NULL ||
       m_tile_max_hpixs == NULL || m_tile_max_vpixs == NULL ||
       m_tile_ll_lats == NULL || m_tile_ll_lons == NULL ||
       m_tile_ur_lats == NULL || m_tile_ur_lons == NULL )
   {
      m_error_message = "Error allocating tile memory!";
      goto FAIL;
   }

   // The delta represents an INCLUSIVE range of pixles (0..120, 120..240, 240..360,...)
   tile_delta_hpix = ((m_image_width -1) / DTED_NUM_HORIZONTAL_TILES);
   tile_delta_vpix = ((m_image_height-1) / DTED_NUM_VERTICAL_TILES);


   // Normalize the lat/lon for use as a part of a sub-tile id
   shifted_lat = int( m_data_nw_lat + 90.0 );
   shifted_lon = int( m_data_nw_lon + 180.0 );

   index = 0;
   for( j = 0; j < DTED_NUM_VERTICAL_TILES; j++ )
   {
      // Set Vertical sub-tile data -- same for row of tiles
      tile_min_vpix = j * tile_delta_vpix;               // Sub tile "lower pixel number"
      tile_max_vpix = tile_min_vpix + tile_delta_vpix;   // Sub-tile "upper pixel number"

      for( i = 0; i < DTED_NUM_HORIZONTAL_TILES; i++ )
      {
         // Set horizontal data for sub-tiles
         tile_min_hpix = i * tile_delta_hpix;            // Sub-tile "left pixel number"
         tile_max_hpix = tile_min_hpix + tile_delta_hpix;// Sub-tile "left pixel number"

         m_tile_ids[index] = 10000000*shifted_lat + 10000*shifted_lon + index;

         m_tile_min_hpixs[index] = tile_min_hpix;
         m_tile_min_vpixs[index] = tile_min_vpix;
         m_tile_max_hpixs[index] = tile_max_hpix;
         m_tile_max_vpixs[index] = tile_max_vpix;

         // Set Lat/Lon data for the sub-tile
         pixel_to_lat_lon( tile_min_hpix, tile_max_vpix,
            m_tile_ll_lats[index], m_tile_ll_lons[index] );
         pixel_to_lat_lon( tile_max_hpix, tile_min_vpix, 
            m_tile_ur_lats[index], m_tile_ur_lons[index] );

         index++;
      }
   }

   // Clear the error message.
   m_error_message = "";

   return DTED_READER_SUCCESS;

FAIL:
   // On failure, close and return failure.
   close( );
   return DTED_READER_FAILURE;
}

//
// Close dted file if open, clean up, and re-initialize to closed state.
//
void CDtedReader::close( )
{
   // Check for currently open.
   if( m_open )
   {
      // Close the file.
      fclose( m_file_ptr );
   }

   // Free column buffer memory
   if( m_col_buffer_1 != NULL )
   {
      delete [] m_col_buffer_1;
      m_col_buffer_1 = NULL;
   }
   if( m_col_buffer_2 != NULL )
   {
      delete [] m_col_buffer_2;
      m_col_buffer_2 = NULL;
   }
   
   // Set to closed state
   m_open  = false;
   m_filename = "";
   m_file_ptr = NULL;

   m_origin_lon = 0.0;
   m_origin_lat = 0.0;
   m_interval_lon = 0.0;
   m_interval_lat = 0.0;
   m_abs_vertical_accuracy = 0.0;
   m_security_code = "";
   m_num_lon_lines = 0;
   m_num_lat_points = 0;
   m_multiple_accuracy = false;

   m_series_designator = "";
   m_vertical_datum = "";
   m_horizontal_datum = "";
   m_data_sw_lat = 0.0;
   m_data_sw_lon = 0.0;
   m_data_nw_lat = 0.0;
   m_data_nw_lon = 0.0;
   m_data_ne_lat = 0.0;
   m_data_ne_lon = 0.0;
   m_data_se_lat = 0.0;
   m_data_se_lon = 0.0;
   m_partial_cell = false;

   // Initialize tile data.
   m_num_tiles = 0;
   if( m_tile_ids != NULL )
      delete []m_tile_ids;
   m_tile_ids = NULL;
   if( m_tile_min_hpixs != NULL )
      delete []m_tile_min_hpixs;
   m_tile_min_hpixs = NULL;
   if( m_tile_min_vpixs != NULL )
      delete []m_tile_min_vpixs;
   m_tile_min_vpixs = NULL;
   if( m_tile_max_hpixs != NULL )
      delete []m_tile_max_hpixs;
   m_tile_max_hpixs = NULL;
   if( m_tile_max_vpixs != NULL )
      delete []m_tile_max_vpixs;
   m_tile_max_vpixs = NULL;
   if( m_tile_ll_lats != NULL )
      delete []m_tile_ll_lats;
   m_tile_ll_lats = NULL;
   if( m_tile_ll_lons != NULL )
      delete []m_tile_ll_lons;
   m_tile_ll_lons = NULL;
   if( m_tile_ur_lats != NULL )
      delete []m_tile_ur_lats;
   m_tile_ur_lats = NULL;
   if( m_tile_ur_lons != NULL )
      delete []m_tile_ur_lons;
   m_tile_ur_lons = NULL;
}

// ********************************************************************
// ********************************************************************

// Returns true if a dted file is open or false otherwise.
//
bool CDtedReader::is_open( )
{
   return m_open;
}

// ********************************************************************
// ********************************************************************

//
// Returns name of open dted file (full file specification). If a file is not open,
// returns "".
//
std::string CDtedReader::get_filename( )
{
   return m_filename;
}

// ********************************************************************
// ********************************************************************

//
// Returns the most recent error message;
//
std::string CDtedReader::get_error_message( )
{
   return m_error_message;
}

// ********************************************************************
// ********************************************************************

//
// Returns the dted series "DTED1", "DTED2", etc.
//
std::string CDtedReader::get_series_designator( )
{
   return m_series_designator;
}

// ********************************************************************
// ********************************************************************

//
// Returns the vertical datum "MSL", etc.
//
std::string CDtedReader::get_vertical_datum( )
{
   return m_vertical_datum;
}

// ********************************************************************
// ********************************************************************

//
// Returns the horizontal datum "WGS84", etc.
//
std::string CDtedReader::get_horizontal_datum( )
{
   return m_horizontal_datum;
}

// ********************************************************************
// ********************************************************************

//
// Gets the bounding rectangle for the file's elevation data.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE (if no file open).
//
int CDtedReader::get_data_bounding_rectangle( double &sw_lat, double &sw_lon,
                                              double &nw_lat, double &nw_lon, 
                                              double &ne_lat, double &ne_lon, 
                                              double &se_lat, double &se_lon
                                             )
{
   if( !m_open )
      return DTED_READER_FAILURE;

   sw_lat = m_data_sw_lat;
   sw_lon = m_data_sw_lon;
   nw_lat = m_data_nw_lat;
   nw_lon = m_data_nw_lon;
   ne_lat = m_data_ne_lat;
   ne_lon = m_data_ne_lon;
   se_lat = m_data_se_lat;
   se_lon = m_data_se_lon;

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Gets the coordinates of the data origin.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE (if no file open).
//
int CDtedReader::get_origin( double &origin_lat, double &origin_lon )
{
   if( !m_open )
      return DTED_READER_FAILURE;

   origin_lat = m_origin_lat;
   origin_lon = m_origin_lon;

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Gets the intervals of the data latitudes and longitudes in degrees.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE (if no file open).
//
int CDtedReader::get_intervals( double &interval_lat, double &interval_lon )
{
   if( !m_open )
      return DTED_READER_FAILURE;

   interval_lat = m_interval_lat;
   interval_lon = m_interval_lon;

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Gets the absolute vertical accuracy of the data in meters.
//
double CDtedReader::get_abs_vertical_accuracy( )
{
   return m_abs_vertical_accuracy;
}

// ********************************************************************
// ********************************************************************

//
// Gets the security classification of the data ("T","S","C","U","R", etc.).
//
std::string CDtedReader::get_security_code( )
{
   return m_security_code;
}

// ********************************************************************
// ********************************************************************

//
// Gets the number of longitudinal lines of data in the file.
//
int CDtedReader::get_num_lon_lines( )
{
   return m_num_lon_lines;
}
   
// ********************************************************************
// ********************************************************************

//
// Gets the number of latitude points for each longitudinal line.
//
int CDtedReader::get_num_lat_points( )
{
   return m_num_lat_points;
}

// ********************************************************************
// ********************************************************************

//
// Returns true if it is a partially filled cell.
//
bool CDtedReader::is_partial_cell( )
{
   return m_partial_cell;
}

// ********************************************************************
// ********************************************************************

//
// Returns the elevation at a lat/lon in meters or -32767 if error.
//
short CDtedReader::get_elevation_in_meters( double lat, double lon )
{
   int row, col, sign;
   int column_start_byte, start_byte;
   unsigned char val[2];
   short elevation;
   
   if( !m_open )
      goto FAIL;

   // Check for lat/lon within cell.
   if ( lat < m_data_sw_lat || lat > m_data_nw_lat ||
        lon < m_data_sw_lon || lon > m_data_se_lon )
        goto FAIL;

   // Determine nearest column from longitude.
   col = (int)( ( lon - m_data_sw_lon ) / m_interval_lon + 0.5 );

   // Determine nearest row from latitude.
   row = (int)( ( lat - m_data_sw_lat ) / m_interval_lat + 0.5 );

   column_start_byte = 80 + 648 + 2700 + col * ( 12 + 2*m_num_lat_points );

   start_byte = column_start_byte + 8 + 2*row;

   if( fseek( m_file_ptr, start_byte, 0 ) != 0 )
      goto FAIL;

   if( fread( val, 2, 1, m_file_ptr ) != 1 )
      goto FAIL;

   // Determine sign of data.
   if( val[0] & 128 )
      sign = -1;
   else
      sign = 1;

   val[0] = val[0] & 127;  // Clear sign bit.

   elevation = val[0];     // High order byte.
   elevation = elevation << 8;
   elevation += val[1];
   elevation *= sign;

   return elevation;

FAIL:
   return -32767;
}

// ********************************************************************
// ********************************************************************

//
// Returns the elevations for the requested column in the buffer provided in order of 
// south to north.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
//
int CDtedReader::get_column_of_elevations( int col, short *col_buffer )
{
   int i, start_byte;//, sign;
   unsigned char *byte_buffer;
   unsigned char* point = (unsigned char*)(void*)col_buffer;
   unsigned char temp;
   //short elevation;
   const int BUF_LEN = 80;
   char buf[BUF_LEN];
   
   // Check for file open.
   if( !m_open )
   {
      m_error_message = "File is not open.";
      goto FAIL;
   }

   // Check for valid column.
   if( col < 0 || col > m_num_lon_lines-1 )
   {
		sprintf_s(buf, BUF_LEN, "Column number is invalid: %i", col);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Check for null buffer.
   if( col_buffer == NULL )
   {
      m_error_message = "Buffer is NULL.";
      goto FAIL;
   }

   // We need to manipulate the data as bytes.
   byte_buffer = (unsigned char*)col_buffer;
   
   // Find the offset of the column in the file.
   start_byte = 80 + 648 + 2700 + col * ( 12 + 2*m_num_lat_points ) + 8;

   // Seek to the offset of the column in the file
   if( fseek( m_file_ptr, start_byte, 0 ) != 0 )
   {
		sprintf_s(buf, BUF_LEN, "Error seeking to start of column %i", col);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Read the raw data.
   if( fread( byte_buffer, 2*m_num_lat_points, 1, m_file_ptr ) != 1 )
   {
		sprintf_s(buf, BUF_LEN, "Error reading data for column %i", col);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // We need to reverse the byte order of the short's in the buffer
   /*
   // Convert data format.
   for( i = 0, index = 0; i < num_rows; i++, index += 2 )
   {
      high_byte = byte_buffer[index];      // Data is initially high-endian.
      low_byte = byte_buffer[index+1];

      // Determine sign of data from highest bit.
      if( high_byte & 128 )
         sign = -1;
      else
         sign = 1;

      high_byte = high_byte & 127;  // Clear sign bit.

      elevation = high_byte;
      elevation = elevation << 8;
      elevation += low_byte;
      elevation *= sign;
      col_buffer[i] = elevation;
   }
   */
   for( i = 0; i < m_num_lat_points; i++)
   {
       temp = *point;
      *point = *(point + 1);
      *(point + 1) = temp;

      // The negative numbers in the file are not complemented, 
      // apply the new complement by looking for the sign bit,
      // then clearing it (if needed) and multiplying by -1
      if (temp & 0x80)
      {
         *(point+1) &= 0x7f;
         *(unsigned short*)(point) *= -1;
      }

      point += 2;
   }

   return DTED_READER_SUCCESS;

FAIL:
   return DTED_READER_FAILURE;
}

// ********************************************************************
// ********************************************************************


   int get_subcolumn_of_elevations( int col, int min_row, int max_row,
      short *col_buffer );

// ********************************************************************
// ********************************************************************

//
// Returns the elevations for the requested subcolumn in the buffer provided in order of 
// south to north.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
//
int CDtedReader::get_subcolumn_of_elevations( int col, int min_row, int max_row,
                                              short *col_buffer )
{
   int i, start_byte, num_rows;
   unsigned char *byte_buffer;
   unsigned char* point = (unsigned char*)(void*)col_buffer;
   unsigned char temp;
   const int BUF_LEN = 80;
   char buf[BUF_LEN];
   
   // Check for file open.
   if( !m_open )
   {
      m_error_message = "File is not open.";
      goto FAIL;
   }

   // Check for valid column.
   if( col < 0 || col > m_num_lon_lines-1 )
   {
		sprintf_s(buf, BUF_LEN, "Column number is invalid: %i", col);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Check for valid rows.
   if( min_row < 0 || max_row > m_num_lat_points-1 || min_row > max_row )
   {
		sprintf_s(buf, BUF_LEN, "Row range is invalid: %i to %i", min_row, max_row);
      m_error_message = std::string(buf);
      goto FAIL;
   }
   num_rows = max_row - min_row + 1;

   // Check for null buffer.
   if( col_buffer == NULL )
   {
      m_error_message = "Buffer is NULL.";
      goto FAIL;
   }

   // We need to manipulate the data as bytes.
   byte_buffer = (unsigned char*)col_buffer;
   
   // Find the offset of the column in the file.
   start_byte = 80 + 648 + 2700 + col * ( 12 + 2*m_num_lat_points ) + 8;

   // Add the offset of the minimum row.
   start_byte += 2 * min_row;

   // Seek to the offset of the minimum row in the file
   if( fseek( m_file_ptr, start_byte, 0 ) != 0 )
   {
		sprintf_s(buf, BUF_LEN, "Error seeking to start of column %i row %i", col, min_row);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // Read the raw data.
   if( fread( byte_buffer, 2*num_rows, 1, m_file_ptr ) != 1 )
   {
		sprintf_s(buf, BUF_LEN, "Error reading data for column %i rows %i to %i",
         col, min_row, max_row);
      m_error_message = std::string(buf);
      goto FAIL;
   }

   // We need to reverse the byte order of the short's in the buffer
   /*
   // Convert data format.
   for( i = 0, index = 0; i < num_rows; i++, index += 2 )
   {
      high_byte = byte_buffer[index];      // Data is initially high-endian.
      low_byte = byte_buffer[index+1];

      // Determine sign of data from highest bit.
      if( high_byte & 128 )
         sign = -1;
      else
         sign = 1;

      high_byte = high_byte & 127;  // Clear sign bit.

      elevation = high_byte;
      elevation = elevation << 8;
      elevation += low_byte;
      elevation *= sign;
      col_buffer[i] = elevation;
   }
   */
   for( i = 0; i < num_rows; i++)
   {
       temp = *point;
      *point = *(point + 1);
      *(point + 1) = temp;

      // The negative numbers in the file are not complemented, 
      // apply the new complement by looking for the sign bit,
      // then clearing it (if needed) and multiplying by -1
      if (temp & 0x80)
      {
         *(point+1) &= 0x7f;
         *(unsigned short*)(point) *= -1;
      }

      point += 2;
   }

   return DTED_READER_SUCCESS;

FAIL:
   return DTED_READER_FAILURE;
}




// ********************************************************************
// ********************************************************************

//
// Gets the image width in pixels.
//
int CDtedReader::get_image_width( )
{
   return m_image_width;
}
   
// ********************************************************************
// ********************************************************************

//
// Gets the image height in pixels.
//
int CDtedReader::get_image_height( )
{
   return m_image_height;
}
   
// ********************************************************************
// ********************************************************************

//
// Gets the bounding rectangle for the file's image. This is smaller than the
// elevation data's bounding rectangle because pixels are not calculated for the 
// bottom row and right column of elevation posts.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE (if no file open).
//
int CDtedReader::get_image_bounding_rectangle( double &sw_lat, double &sw_lon,
                                              double &nw_lat, double &nw_lon, 
                                              double &ne_lat, double &ne_lon, 
                                              double &se_lat, double &se_lon
                                             )
{
   if( !m_open )
      return DTED_READER_FAILURE;

   sw_lat = m_image_sw_lat;
   sw_lon = m_image_sw_lon;
   nw_lat = m_image_nw_lat;
   nw_lon = m_image_nw_lon;
   ne_lat = m_image_ne_lat;
   ne_lon = m_image_ne_lon;
   se_lat = m_image_se_lat;
   se_lon = m_image_se_lon;

   return DTED_READER_SUCCESS;
}


// ********************************************************************
// ********************************************************************

int CDtedReader::get_elev_data(short*& ElevData, int image_height, int image_width, 
                               int tile_index)
{
   if( !m_open )
   {
      m_error_message = "File is not open.";
      return DTED_READER_FAILURE;
   }

   int status = DTED_READER_SUCCESS;
   
   int StartRow = m_tile_min_vpixs[tile_index];
   int StartCol = m_tile_min_hpixs[tile_index];

   // "Get column" is returning 1 too many shorts!
   ElevData = new short[image_width*image_height];
   short* TempData = new short[image_height];

   int min_row = m_num_lat_points - (StartRow+image_height);
   int max_row = m_num_lat_points - StartRow - 1;

   for( int col = StartCol; col < StartCol+image_width; col++ )
   {
      if( get_subcolumn_of_elevations( col, 
                  min_row, 
                  max_row, 
                  TempData) 
                  != DTED_READER_SUCCESS )
      {
         status = DTED_READER_FAILURE;
         break;
      }

      for (int ctt=0; ctt< image_height; ctt++)
      {
         // Flip the image so UL is NW
         //*(ElevData+((col-StartCol)*image_height)+ctt) = TempData[image_height - ctt - 1];

         // orig
         //*(ElevData+((col-StartCol)*image_height)+ctt) = TempData[ctt];

         *(ElevData+(ctt*image_width) + (col-StartCol)) = TempData[ctt];
      }
      
   }

   delete [] TempData;
   return status;
}

//
// Returns the specified light-shaded subimage shaded in the buffer argument,
// with the bottom row and proceding toward the top row.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
//
int CDtedReader::get_subimage_2( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                               unsigned char *subimage )

{
   int row, col,image_index;
   int num_rows, min_row, max_row;
   int subimage_width, subimage_height;
   short *left_col_buffer, *right_col_buffer, *temp_col_buffer;

   float meters_lat_per_pixel, meters_lon_per_pixel;
   double mlat, mlon;
   double min_data_elev, max_data_elev;

   const int BUF_LEN = 80;
   char buf[BUF_LEN];
   
   if( !m_open )
   {
      m_error_message = "File is not open.";
      return DTED_READER_FAILURE;
   }

   // Check for valid subimage.
   if( min_hpix < 0 || max_hpix > m_image_width-1 || min_hpix > max_hpix ||
       min_vpix < 0 || max_vpix > m_image_height-1 || min_vpix > max_vpix )
   {
		sprintf_s(buf, BUF_LEN, "Invalid subimage: min_hpix = %i  min_vpix = %i  max_hpix = %i  max_vpix = %i",
         min_hpix, min_vpix, max_hpix, max_vpix);
      m_error_message = std::string(buf);
      return DTED_READER_FAILURE;
   }
   subimage_width = max_hpix - min_hpix + 1;
   subimage_height = max_vpix - min_vpix + 1;

   min_row = m_num_lat_points - max_vpix - 1;

   // Note: we need to read in one extra row of data.  The smallest min_vpix can be is 1.
   max_row = m_num_lat_points - min_vpix;     
   num_rows = max_row - min_row + 1;
   
   // Initially we will use m_col_buffer_1 for the left column of elevations, 
   // and m_col_buffer_2 for the right column of elevations. Then we will swap 
   // the buffers as we move from left to right, so that we are always loading 
   // data in the right column.
   left_col_buffer = m_col_buffer_1;
   right_col_buffer = m_col_buffer_2;
   
   // Get elevation data for left column.
   if( get_subcolumn_of_elevations( min_hpix, min_row, max_row, left_col_buffer ) != DTED_READER_SUCCESS )
      return DTED_READER_FAILURE;

   // Get an average number for the tile center, match response from get_image()
   get_meters_per_pixel((m_image_sw_lat+m_image_nw_lat)/2, 
      (m_image_sw_lon+m_image_nw_lon)/2,
      mlat, mlon);
   meters_lat_per_pixel = (float)mlat;
   meters_lon_per_pixel = (float)mlon;

   min_data_elev = 32000.0;
   max_data_elev = -32000.0;

   // Loop through all the columns.
   for( col = 0; col < subimage_width; col++ )
   {
      // Get column to the right.
      if( get_subcolumn_of_elevations( col+min_hpix+1, min_row, max_row, right_col_buffer ) != DTED_READER_SUCCESS )
         return DTED_READER_FAILURE;

      //lon = m_data_sw_lon + (col+min_hpix)*m_interval_lon;

      for( row = 0; row < subimage_height; row++ )
      {
         image_index = row * subimage_width + col;
         
         // Test for missing data: elevation = -32767.
         if( left_col_buffer[row+1] == -32767 )
         {
            subimage[image_index] = DTED_READER_NUM_COLORS-1;
            continue;
         }

		 if (min_data_elev > right_col_buffer[row+1])
			 min_data_elev = right_col_buffer[row+1];
		 if (max_data_elev < right_col_buffer[row+1])
			 max_data_elev = right_col_buffer[row+1];

         if (m_ContourLinesOn && m_ContourInterval > 0)
         {
            float level;

            if (CheckContour(left_col_buffer[row+1],
               right_col_buffer[row+1],
               left_col_buffer[row],
               level))
            {
               if(!m_IsContourMono)
               {
                  subimage[image_index] = DTED_READER_NUM_COLORS-3;
                  continue;
               }

               const double delta = (m_elev_breakpts[m_num_elev_pts-1] - m_elev_breakpts[0])/5;
               const double MaxElev = m_elev_breakpts[m_num_elev_pts-1] + delta; // Add 20% to range
               const double MinElev = m_elev_breakpts[0] - delta;
               const double Elev = level;
               
               if (Elev <= MinElev)
                  subimage[image_index] = (unsigned char)0;
               else if (Elev >= MaxElev)
                  subimage[image_index] = (unsigned char)(m_num_elev_pts*m_NumShades-1);
               else if(m_dted_mono)
                  subimage[image_index] = (unsigned char)(((Elev-MinElev)/(MaxElev - MinElev))*DTED_READER_SHADES);
               else
               {
                  //ASSERT(m_num_elev_pts>0);
                  
                  if (Elev < m_elev_breakpts[0])
                  {
                     subimage[image_index] = (unsigned char)(((Elev-MinElev)/(m_elev_breakpts[0]-MinElev))*m_NumShades/2);
                  }
                  else if (Elev < m_elev_breakpts[m_num_elev_pts-1])
                  {
                     for(int ct=1; ct<m_num_elev_pts; ct++)
                     {
                        if (Elev < m_elev_breakpts[ct])
                        {
                           subimage[image_index] = (unsigned char)(((Elev-m_elev_breakpts[ct-1])/(m_elev_breakpts[ct]-m_elev_breakpts[ct-1]))*m_NumShades/2 + ct*m_NumShades);
                           break;
                        }
                     }
                  }
                  else // (Elev < MaxElev) known 
                  {
                     subimage[image_index] = (unsigned char)(((Elev-m_elev_breakpts[m_num_elev_pts-1])/(MaxElev - m_elev_breakpts[m_num_elev_pts-1]))*m_NumShades/2 + m_num_elev_pts*m_NumShades);
                  }
               }
               
			   if (subimage[image_index] > 212)
				   subimage[image_index] = 212;
               continue;
            }
            else if(m_IsContourMono)
            {
               subimage[image_index] = DTED_READER_NUM_COLORS-4;
               continue;
            }
         }

         // Simple test for sea level.
         if( left_col_buffer[row+1] == 0 )
         {
            subimage[image_index] = 214;
            continue;
         }

         switch (m_DisplayMode)
         {
         case 0:
            {
               //Standard color shading
               const double dot_prod = calc_dot_prod(left_col_buffer[row], 
                                               right_col_buffer[row], 
                                               left_col_buffer[row+1],
                                               meters_lat_per_pixel, meters_lon_per_pixel);

               subimage[image_index] = get_shaded_color(left_col_buffer[row+1], dot_prod);

               break;
            }

         case 1:
            {

//               const double delta = (m_elev_breakpts[m_elev_breakpts-1] - m_elev_breakpts[0])/5;
//               const double MaxElev = m_elev_breakpts[m_elev_breakpts-1] + delta; // Add 20% to range
//               const double MinElev = m_elev_breakpts[0] - delta;

               const double delta = (m_elev_breakpts[m_num_elev_pts-1] - m_elev_breakpts[0])/5;
               const double MaxElev = m_elev_breakpts[m_num_elev_pts-1] + delta; // Add 20% to range
               const double MinElev = m_elev_breakpts[0] - delta;
               const double Elev = left_col_buffer[row];

			   subimage[image_index] = get_elevation_color(Elev, m_elev_breakpts[0], m_elev_breakpts[m_num_elev_pts-1]);
			   break;

               if (Elev <= MinElev)
                  subimage[image_index] = (unsigned char)0;
               else if (Elev >= MaxElev)
                  subimage[image_index] = (unsigned char)(m_num_elev_pts*m_NumShades-1);
               else if(m_dted_mono)
                  subimage[image_index] = (unsigned char)(((Elev-MinElev)/(MaxElev - MinElev))*DTED_READER_SHADES);
               else
               {
                  //ASSERT(m_num_elev_pts>0);
                  
                  if (Elev < m_elev_breakpts[0])
                  {
                     subimage[image_index] = (unsigned char)(((Elev-MinElev)/(m_elev_breakpts[0]-MinElev))*m_NumShades);
                  }
                  else if (Elev < m_elev_breakpts[m_num_elev_pts-1])
                  {
                     for(int ct=1; ct<m_num_elev_pts; ct++)
                     {
                        if (Elev < m_elev_breakpts[ct])
                        {
                           subimage[image_index] = (unsigned char)(((Elev-m_elev_breakpts[ct-1])/(m_elev_breakpts[ct]-m_elev_breakpts[ct-1]))*m_NumShades + ct*m_NumShades);
                           break;
                        }
                     }
                  }
                  else // (Elev < MaxElev) known 
                  {
                     subimage[image_index] = (unsigned char)(((Elev-m_elev_breakpts[m_num_elev_pts-1])/(MaxElev - m_elev_breakpts[m_num_elev_pts-1]))*m_NumShades + m_num_elev_pts*m_NumShades);
                  }
               }
               
               break;
            }

         case 2:
            {
               // Slope encoding test (must fix lighting to overhead)

               m_light_dir_x = m_light_dir_y = 0;
               m_light_dir_z = -1;
               double dot_prod = 1 - calc_dot_prod(left_col_buffer[row], 
                                                   right_col_buffer[row], 
                                                   left_col_buffer[row+1],
                                                   meters_lat_per_pixel, meters_lon_per_pixel);

			   // calc slope
			   double slope, slope1, slope2;
			   slope1 = (double) abs(left_col_buffer[row+1] - left_col_buffer[row]) / m_grid_spacing;
			   slope2 = (double) abs(left_col_buffer[row+1] - right_col_buffer[row+1]) / m_grid_spacing;

			   slope = slope1;
			   if (slope < slope2)
				   slope = slope2;

			   if (row > 1 && row < subimage_height-2)
			   {
				   if (slope == 0.0)
				   {
					   if ((left_col_buffer[row+1] != left_col_buffer[row+2]) 
						   || (left_col_buffer[row+1] != left_col_buffer[row-1])
						   || (left_col_buffer[row+1] != right_col_buffer[row+2])
						   || (left_col_buffer[row+1] != right_col_buffer[row-1]))
						   slope = 0.001;
				   }
			   }

			   slope *= 100.0;

               // Brighten up the display!
               dot_prod *= 7.46; // 30 degree slope is white

				subimage[image_index] = get_slope_color(slope, dot_prod);
            }
         }
      }

      // Swap left and right column buffer pointers so we don't have to copy the
      // data from the right column to the left column.
      temp_col_buffer = left_col_buffer;
      left_col_buffer = right_col_buffer;    // Now points to right column of data.
      right_col_buffer = temp_col_buffer;    // Now points to left column of data - to be overwritten.
   }

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Returns the specified light-shaded subimage shaded in the buffer argument,
// with the bottom row and proceding toward the top row.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
//
int CDtedReader::get_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                               unsigned char *subimage )
{
   // dvl :: temporarily call second function before modifying this one
   return get_subimage_2( min_hpix, min_vpix, max_hpix, max_vpix, subimage );

}

// ********************************************************************
// ********************************************************************

//
// Sets the light direction vector. Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
// Vector must have nonzero magnitude since it will be stored internally as a unit 
// vector.
//
int CDtedReader::set_light_direction( double light_dir_x, double light_dir_y,
                                      double light_dir_z )
{
   double mag;

   // Calculate magnitude.
   mag = sqrt( light_dir_x*light_dir_x + light_dir_y*light_dir_y + light_dir_z*light_dir_z );
   if( mag < 1.e-10 )
      return DTED_READER_FAILURE;

   m_light_dir_x = light_dir_x / mag;
   m_light_dir_y = light_dir_y / mag;
   m_light_dir_z = light_dir_z / mag;

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Gets the light direction unit vector. Returns DTED_READER_SUCCESS or
// DTED_READER_FAILURE (file not open).
//
int CDtedReader::get_light_direction( double &light_dir_x, double &light_dir_y,
                                      double &light_dir_z )
{
   light_dir_x = m_light_dir_x;
   light_dir_y = m_light_dir_y;
   light_dir_z = m_light_dir_z;

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Sets the elevation exaggeration factor. Returns DTED_READER_SUCCESS or
// DTED_READER_FAILURE (file not open).
//
int CDtedReader::set_exaggeration_factor( float exaggeration_factor )
{
   // Check for file open.
   if( !m_open )
   {
      m_error_message = "File is not open.";
      return DTED_READER_FAILURE;
   }

   m_exaggeration_factor = exaggeration_factor;

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Returns the elevation exaggeration factor.
//
float CDtedReader::get_exaggeration_factor( )
{
   return m_exaggeration_factor;
}

// ********************************************************************
// ********************************************************************

//
// Sets the color of the contour line.
//
void CDtedReader::set_contour_color(COLORREF color )
{
   m_contour_color = color;
}

// ********************************************************************
// ********************************************************************

//
// Sets the flat is black flag.
//
void CDtedReader::set_flat_is_black(bool is_black )
{
   m_flat_is_black = is_black;
}

// ********************************************************************
// ********************************************************************

//
// Gets the pixel coordinates corresponding to the specified lat/lon.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
//
int CDtedReader::lat_lon_to_pixel( double lat, double lon, int &hpix, int &vpix )
{
   // Check for file open.
   if( !m_open )
   {
      m_error_message = "File is not open.";
      return DTED_READER_FAILURE;
   }

   // Check for lat/lon within bounding rectangle.
   if( lat < m_data_sw_lat || lat > m_data_nw_lat ||
       lon < m_data_sw_lon || lon > m_data_se_lon )
   {
      m_error_message = "Lat/Lon is outside of bounding rectangle.";
      return DTED_READER_FAILURE;
   }

   // Note that this assumes the origin is at the northwest corner of the tile but the
   // origin of the data is actually at the southwest corner of the tile
   //
   // Note that a tile is always one degree
   //
   // Note that the returned index should be 0-based, hence the -1
   hpix = static_cast<int>( (lon - m_data_nw_lon) * (m_image_width - 1) + 0.5);
   vpix = static_cast<int>( (m_data_nw_lat - lat) * (m_image_height - 1) + 0.5);

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Gets the lat/lon corresponding to the center of the specified pixel.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
//
int CDtedReader::pixel_to_lat_lon( int hpix, int vpix, double &lat, double &lon )
{
   double fraction;

   // Check for file open.
   if( !m_open )
   {
      m_error_message = "File is not open.";
      return DTED_READER_FAILURE;
   }

   // Check for pixel within image.
   if( hpix < 0 || hpix > m_image_width-1 || vpix < 0 || vpix > m_image_height-1 )
   {
      m_error_message = "Pixel is outside of image.";
      return DTED_READER_FAILURE;
   }

   // Calculate latitude from vpix.
   fraction = (double)vpix / (m_image_height - 1);
   lat = m_data_nw_lat + fraction * ( m_data_sw_lat - m_data_nw_lat );

   // Calculate longitude from hpix.
   fraction = (double)hpix / (m_image_width - 1);
   lon = m_data_sw_lon + fraction * ( m_data_se_lon - m_data_sw_lon );

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Returns the degrees latitude and longitude degrees per pixel in the arguments.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
//
int CDtedReader::get_degrees_per_pixel( double &degrees_lat_per_pixel, 
   double &degrees_lon_per_pixel )
{
   // Check for file open.
   if( !m_open )
   {
      m_error_message = "File is not open.";
      return DTED_READER_FAILURE;
   }

   degrees_lat_per_pixel = m_interval_lat;
   degrees_lon_per_pixel = m_interval_lon;

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Returns the meters per pixel latitude and longitude in the arguments for the 
// specified lat/lon location (must be within the file's bounds).
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
//
int CDtedReader::get_meters_per_pixel( double lat, double lon, double &meters_lat_per_pixel,
   double &meters_lon_per_pixel )
{
   double pi = 4.0 * atan( 1.0 );

   // Check for file open.
   if( !m_open )
   {
      m_error_message = "File is not open.";
      return DTED_READER_FAILURE;
   }

   // Check for lat/lon within bounds.
   if( lat < m_data_sw_lat || lat > m_data_nw_lat ||
       lon < m_data_sw_lon || lon > m_data_se_lon )
   {
      m_error_message = "Lat/Lon is outside of file bounds.";
      return DTED_READER_FAILURE;
   }

   meters_lat_per_pixel = m_interval_lat * DTED_READER_EARTH_RADIUS_IN_METERS * 
      pi / 180.0;

   meters_lon_per_pixel = m_interval_lon * DTED_READER_EARTH_RADIUS_IN_METERS *
      cos( lat * pi / 180.0 ) * pi / 180;

   return DTED_READER_SUCCESS;
}

//
// Returns the number of colors in the color table.
//
int CDtedReader::get_number_of_colors( )
{
   return DTED_READER_NUM_COLORS;
}

// ********************************************************************
// ********************************************************************

//
// Returns a copy of the color table in the argument array.
//
void CDtedReader::get_color_table( unsigned char color_table[][3] )
{
   int i;

//   if (m_dted_mono)
//   {
//	   for( i = 0; i < DTED_READER_NUM_COLORS; i++ )
//	   {
//		  color_table[i][0] = (unsigned char) i;
//		  color_table[i][1] = (unsigned char) i;
//		  color_table[i][2] = (unsigned char) i;
//	   }
//	   return;
//	 }

   for( i = 0; i < DTED_READER_NUM_COLORS; i++ )
   {
      color_table[i][0] = m_color_table[i][0];
      color_table[i][1] = m_color_table[i][1];
      color_table[i][2] = m_color_table[i][2];
   }

   return;
}

//
// Returns the number of tiles into which the image is divided for purposes of 
// map caching.
//
int CDtedReader::get_num_tiles( )
{
   return m_num_tiles;
}

// ********************************************************************
// ********************************************************************

//
// Returns the data for the requested tile index.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE (invalid index or file not open).
//
int CDtedReader::get_tile_data( int tile_index, int &tile_id,
   int &tile_width, int &tile_height,
   double &tile_ll_lat, double &tile_ll_lon,
   double &tile_ur_lat, double &tile_ur_lon )
{
   const int BUF_LEN = 80;
   char buf[BUF_LEN];

   // Check for file open.
   if( !m_open )
   {
      m_error_message = "File is not open.";
      return DTED_READER_FAILURE;
   }

   // Check for invalid index
   if( tile_index < 0 || tile_index >= m_num_tiles )
   {
		sprintf_s(buf, BUF_LEN, "Tile index is invalid: %i", tile_index);
      m_error_message = std::string(buf);
      return DTED_READER_FAILURE;
   }

   //
   // Copy data.
   //
   tile_id = m_tile_ids[tile_index];

   // Width height are INCLUISIVE ranges (0..120, 120..240,...)
   tile_width = m_tile_max_hpixs[tile_index] - m_tile_min_hpixs[tile_index] + 1;
   tile_height = m_tile_max_vpixs[tile_index] - m_tile_min_vpixs[tile_index] + 1;
   tile_ll_lat = m_tile_ll_lats[tile_index];
   tile_ll_lon = m_tile_ll_lons[tile_index];
   tile_ur_lat = m_tile_ur_lats[tile_index];
   tile_ur_lon = m_tile_ur_lons[tile_index];

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Returns the data for all of the tiles in the argument arrays.
// Call get_num_tiles() to determine required array dimensions.
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE (file not open).
//
int CDtedReader::get_all_tile_data( int *tile_ids,
   int *tile_widths, int *tile_heights,
   double *tile_ll_lats, double *tile_ll_lons,
   double *tile_ur_lats, double *tile_ur_lons )
{
   int i;

   // Check for file open.
   if( !m_open )
   {
      m_error_message = "File is not open.";
      return DTED_READER_FAILURE;
   }

   // Copy data.
   for( i = 0; i < m_num_tiles; i++ )
   {
      tile_ids[i] = m_tile_ids[i];
      tile_widths[i] = m_tile_max_hpixs[i] - m_tile_min_hpixs[i] + 1;
      tile_heights[i] = m_tile_max_vpixs[i] - m_tile_min_vpixs[i] + 1;
      tile_ll_lats[i] = m_tile_ll_lats[i];
      tile_ll_lons[i] = m_tile_ll_lons[i];
      tile_ur_lats[i] = m_tile_ur_lats[i];
      tile_ur_lons[i] = m_tile_ur_lons[i];
   }

   return DTED_READER_SUCCESS;
}

// ********************************************************************
// ********************************************************************

//
// Returns the image for the specified tile.
// Call get_tile_info to determine width and height of image in order
// to know how much space to allocate for the image;
// Returns DTED_READER_SUCCESS or DTED_READER_FAILURE.
//
// While DTed data tiles are overlapped by a single pixel, the images
// are not -- although, the overlap is needed for calculation
int CDtedReader::get_tile_image( int tile_index, unsigned char* tile_image )
{
   const int BUF_LEN = 80;
   char buf[BUF_LEN];

   // Check for file open.
   if( !m_open )
   {
      m_error_message = "File is not open.";
      return DTED_READER_FAILURE;
   }

   // Check for invalid index
   if( tile_index < 0 || tile_index >= m_num_tiles )
   {
		sprintf_s(buf, BUF_LEN, "Tile index is invalid: %i", tile_index);
      m_error_message = std::string(buf);
      return DTED_READER_FAILURE;
   }

   return get_subimage( m_tile_min_hpixs[tile_index], m_tile_min_vpixs[tile_index]+1,
      m_tile_max_hpixs[tile_index]-1, m_tile_max_vpixs[tile_index],tile_image );
}


// ********************************************************************
// ********************************************************************

//
// Sets up the color table for monochrome or color, based on the registry value m_dted_mono.
//
void CDtedReader::setup_color_table( )
{
	int i;
	int x, y;
	BYTE r, g, b, color;
	double hue, sat, brit;
	double arhue[6];
	double arsat[6];
	double tf;

	if ( m_dted_mono )
	{
		m_max_color = DTED_READER_NUM_COLORS-4;
		// Initialize color table as a grayscale plus blue for water and black for missing data.
		for ( i = 0; i <= m_max_color; i++ )
		{
			tf = ((double)i / (double)m_max_color) * 255.0;
			if (tf > 255.0)
				tf = 255.0;
			color = (BYTE) tf;
			m_color_table[i][0] = color;
			m_color_table[i][1] = m_color_table[i][0];
			m_color_table[i][2] = m_color_table[i][0];
		}
		// Color for contour lines.
		m_color_table[DTED_READER_NUM_COLORS-3][0] = GetRValue(m_contour_color);
		m_color_table[DTED_READER_NUM_COLORS-3][1] = GetGValue(m_contour_color);
		m_color_table[DTED_READER_NUM_COLORS-3][2] = GetBValue(m_contour_color);
		i++;
		// Sea level (elevation 0) will be shown in blue.
		m_color_table[DTED_READER_NUM_COLORS-2][0] = 0;
		m_color_table[DTED_READER_NUM_COLORS-2][1] = 0;
		m_color_table[DTED_READER_NUM_COLORS-2][2] = 255;
		i++;
		// Missing data (elevation -32767) will be shown in black.
		m_color_table[DTED_READER_NUM_COLORS-1][0] = 0;
		m_color_table[DTED_READER_NUM_COLORS-1][1] = 0;
		m_color_table[DTED_READER_NUM_COLORS-1][2] = 0;
	}
	else
	{
		m_max_color = 191;

      for(i=0;i<m_num_color_bands;++i)
      {
         double brightness;
         rgb2hsb(GetRValue(m_color_bands[i]), GetGValue(m_color_bands[i]), GetBValue(m_color_bands[i]),
            &arhue[i], &arsat[i], &brightness);
      }

		for (y=0; y<m_num_color_bands; y++)
		{
			hue = arhue[y];
			sat = arsat[y];

			for (x=0; x<m_NumShades; x++)
			{
				brit = (10.0 + ((double) x * 2.3)) / 100.0;
				hsb2rgb(hue, sat, brit, &r, &g, &b);

				i = (y * m_NumShades) + x;

				m_color_table[i][0] = r;
				m_color_table[i][1] = g;
				m_color_table[i][2] = b;
			}
		}

		/*
		*  set DTED_READER_SHADES-213 to all black
		*/
		for ( i=DTED_READER_SHADES; i<213; i++)
			m_color_table[i][0] = m_color_table[i][1] = m_color_table[i][2] = 0;

		// Background white
		m_color_table[DTED_READER_NUM_COLORS-4][0] = 255;
		m_color_table[DTED_READER_NUM_COLORS-4][1] = 255;
		m_color_table[DTED_READER_NUM_COLORS-4][2] = 255;

		// Contour Line Color
		m_color_table[DTED_READER_NUM_COLORS-3][0] = GetRValue(m_contour_color);
		m_color_table[DTED_READER_NUM_COLORS-3][1] = GetGValue(m_contour_color);
		m_color_table[DTED_READER_NUM_COLORS-3][2] = GetBValue(m_contour_color);

		// Sea level (elevation 0) will be shown in blue.
		m_color_table[DTED_READER_NUM_COLORS-2][0] = 0;
		m_color_table[DTED_READER_NUM_COLORS-2][1] = 0;
		m_color_table[DTED_READER_NUM_COLORS-2][2] = 255;

		// Missing data (elevation -32767) will be shown in black.
		m_color_table[DTED_READER_NUM_COLORS-1][0] = 0;
		m_color_table[DTED_READER_NUM_COLORS-1][1] = 0;
		m_color_table[DTED_READER_NUM_COLORS-1][2] = 0;
	}
}
// end of setup_color_table

// *****************************************************************
// *****************************************************************

int	CDtedReader::parse_elevation_string()
{
	std::vector<std::string> array;
	std::string tstr;
	int rslt, num, k;

	rslt = parse_band(m_ElevationBands, array);
	if (rslt != SUCCESS)
	{
		tstr = "5/2500/5000/7500/10000/12500";
		rslt = parse_band(tstr, array);
		if (rslt != SUCCESS)
			return FAILURE;
	}

	num = atoi(array[0].c_str());
	if ((num < 1) || (num > 6))
		return FAILURE;

	for (k=0; k<num; k++)
		m_elev_breakpts[k] = atof(array[k+1].c_str()) * 0.3048;

	m_num_elev_pts = num;

	return SUCCESS;
}
// end of parse_elevation_string

// *****************************************************************
// *****************************************************************

int	CDtedReader::parse_slope_string()
{
	std::vector<std::string> array;
	std::string tstr;
	int rslt, num, k;

	rslt = parse_band(m_SlopeBands, array);
	if (rslt != SUCCESS)
	{
		tstr = "5/3/6/9/12/15";
		rslt = parse_band(tstr, array);
		if (rslt != SUCCESS)
			return FAILURE;
	}

	num = atoi(array[0].c_str());
	if ((num < 1) || (num > 6))
		return FAILURE;

	for (k=0; k<num; k++)
		m_slope_breakpts[k] = atof(array[k+1].c_str());

	m_num_slope_pts = num;

	return SUCCESS;
}
// end of parse_slope_string

// *****************************************************************
// *****************************************************************

int	CDtedReader::parse_color_string(std::string & colorbands, unsigned short *cnt, double *arhue, double *arsat)
{
	double hue, sat, brit;
	std::vector<std::string> array;
	std::string colorstr;
	int rslt, num, k;
	COLORREF color;

	rslt = parse_band(colorbands, array);
	if (rslt != SUCCESS)
		return FAILURE;

	num = atoi(array[0].c_str());
	if ((num < 2) || (num > 6))
		return FAILURE;

	for (k=0; k<num; k++)
	{
		colorstr = array[k+1];
		if (colorstr.size() != 9)
			return FAILURE;
		color = text_to_color(colorstr);
		color_to_hsb(color, &hue, &sat, &brit);
		arhue[k] = hue;
		arsat[k] = sat;
	}

	m_num_color_bands = num;

	return SUCCESS;
}
// end of parse_color_string

// *****************************************************************
// *****************************************************************

COLORREF CDtedReader::text_to_color(std::string colorstr)
{
	int r, g, b, len;
	COLORREF rgbcolor;

	len = colorstr.size();

	if (len != 9)
	{
		//ASSERT(0);
		return RGB(0,0,0);
	}

	r = atoi(colorstr.substr(0, 3).c_str());
	g = atoi(colorstr.substr(3, 3).c_str());
	b = atoi(colorstr.substr(colorstr.size() - 3, 3).c_str());

	rgbcolor = RGB(r, g, b);
	return rgbcolor;
}

// *****************************************************************
// *****************************************************************

void CDtedReader::color_to_hsb(COLORREF rgbcolor, double *hue, double *sat, double *brit)
{
	BYTE r, g, b;

	r = GetRValue(rgbcolor);
	g = GetGValue(rgbcolor);
	b = GetBValue(rgbcolor);
	rgb2hsb(r, g, b, hue, sat, brit);
}

// *****************************************************************
// *****************************************************************

void CDtedReader::rgb2hsb(BYTE red, BYTE green, BYTE blue, double *hue, double *sat, double *brt) 
{
	BYTE minval, maxval; 

	minval = __min(red, green);
	minval = __min(minval, blue);
	maxval = __max(red, green);
	maxval = __max(maxval, blue);

	float mdiff  = float(maxval) - float(minval);
	float msum   = float(maxval) + float(minval);

	*brt = msum / 510.0f;
//	*brt = msum / 200.0f;

	if (maxval == minval) 
	{
		*sat = 0.0f;
		*hue = 0.0f; 
    }   
    else 
    { 
		float rnorm = (maxval - red  ) / mdiff;      
		float gnorm = (maxval - green) / mdiff;
		float bnorm = (maxval - blue ) / mdiff;   

		*sat = (*brt <= 0.5f) ? (mdiff / msum) : (mdiff / (510.0f - msum));

		if (red   == maxval)
			*hue = 60.0f * (6.0f + bnorm - gnorm);
		if (green == maxval) 
			*hue = 60.0f * (2.0f + rnorm - bnorm);
		if (blue == maxval) 
			*hue = 60.0f * (4.0f + gnorm - rnorm);
		if (*hue > 360.0f) 
			*hue = *hue - 360.0f;
    }

//	*sat *= 100.0;
//	*brt *= 100.0;
}

// *****************************************************************
// *****************************************************************

void CDtedReader::hsb2rgb( double hue,                 // 0 to 360
				           double sat,                 // 0 to 1.0
				           double brit,                // 0 to 1.0
				           BYTE *r, BYTE *g, BYTE *b   // 0 to 255
                         )
{
	if (sat == 0.0) // Grauton, einfacher Fall
	{
		*r = *g = *b  = (BYTE) (brit * 255.0);
	}
	else
	{
		double rm1, rm2;

		if (brit <= 0.5f) 
			rm2 = brit + brit * sat;  
		else                     
			rm2 = brit + sat - brit * sat;
		rm1 = 2.0f * brit - rm2;   
		*r = hsb2rgb1(rm1, rm2, hue + 120.0f);   
		*g = hsb2rgb1(rm1, rm2, hue);
		*b = hsb2rgb1(rm1, rm2, hue - 120.0f);
	}
}

// *****************************************************************
// *****************************************************************

BYTE CDtedReader::hsb2rgb1(double rm1, double rm2, double rh)
{
  if (rh > 360.0f) 
	  rh -= 360.0f;
  else if (rh <   0.0f) 
	  rh += 360.0f;
 
  if  (rh <  60.0f) 
	  rm1 = rm1 + (rm2 - rm1) * rh / 60.0f;   
  else if (rh < 180.0f) 
	  rm1 = rm2;
  else if (rh < 240.0f) 
	  rm1 = rm1 + (rm2 - rm1) * (240.0f - rh) / 60.0f;      
                   
  return static_cast<unsigned char>(rm1 * 255);
}

// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************

void CDtedReader::set_contour_lines_on(BOOL enabled) 
{
   m_ContourLinesOn = enabled;
}

float CDtedReader::set_contour_interval(float interval) 
{
   return m_ContourInterval = static_cast<float>(FEET_TO_METERS(interval));
}

float CDtedReader::get_contour_interval()
{
   return static_cast<float>(METERS_TO_FEET(m_ContourInterval));
}

// *****************************************************************
// *****************************************************************

unsigned short CDtedReader::set_contour_width(unsigned short width) 
{
   return m_ContourWidth = static_cast<unsigned short>(FEET_TO_METERS(width));
}

// *****************************************************************
// *****************************************************************

unsigned short CDtedReader::set_display_mode(unsigned short DisplayMode)
{
   return m_DisplayMode = DisplayMode;
}

unsigned short CDtedReader::get_display_mode()
{
   return m_DisplayMode;
}

// *****************************************************************
// *****************************************************************

void CDtedReader::set_elevation_bands(std::string elevbands)
{
	m_ElevationBands = elevbands;
	parse_elevation_string();
}

void CDtedReader::set_elevation_bands(int num_bands, int *bands)
{
   m_num_elev_pts = num_bands;
   for(int i=0;i<num_bands;++i)
   {
      m_elev_breakpts[i] = FEET_TO_METERS(bands[i]);
   }
}

void CDtedReader::get_elevation_bands(int *bands)
{
   for(int i=0;i<m_num_elev_pts;++i)
   {
      bands[i] = static_cast<int>(METERS_TO_FEET(m_elev_breakpts[i]) + 0.5);
   }
}

// *****************************************************************
// *****************************************************************

void CDtedReader::set_slope_bands(std::string slopebands)
{
	m_SlopeBands = slopebands;
	parse_slope_string();
}

void CDtedReader::set_slope_bands(int num_bands, double *bands)
{
   m_num_slope_pts = num_bands;
   memcpy(m_slope_breakpts, bands, sizeof(double) * num_bands);
}

void CDtedReader::get_slope_bands(double *bands)
{
   memcpy(bands, m_slope_breakpts, sizeof(double) * m_num_slope_pts);
}

// *****************************************************************
// *****************************************************************

void CDtedReader::set_color_bands(std::string colorbands)
{
	m_ColorBands = colorbands;
}

void CDtedReader::set_color_bands(int num_bands, long *red, long *green, long *blue)
{
   m_num_color_bands = num_bands;

   for(int i=0;i<num_bands;++i)
      m_color_bands[i] = RGB(red[i], green[i], blue[i]);

   setup_color_table();
}

void CDtedReader::get_color_bands(long *red, long *green, long *blue)
{
   for(int i=0;i<m_num_color_bands;++i)
   {
      red[i] = GetRValue(m_color_bands[i]);
      green[i] = GetGValue(m_color_bands[i]);
      blue[i] = GetBValue(m_color_bands[i]);
   }
}

// *****************************************************************
// *****************************************************************

/*
void CDtedReader::set_elevation_bands(unsigned NumBands, long* pBands)
{
   // A maximum of 5 break points (6 colors) are used at the moment!!!
   if (NumBands > 5)
      NumBands = 5;

   if (NumBands > 0 && pBands != NULL)
   {
      if (m_pElevationBands)
         delete [] m_pElevationBands;

      m_NumElevationBands = NumBands;

      m_pElevationBands = pBands;

      m_elev_breakpts = NumBands;
      m_elev_breakpts = 5; // always use all the breakpoints (the unused ones will be 0s)
//      m_NumShades = DTED_READER_SHADES / (m_elev_breakpts+1);
//      if (m_NumShades > 32)
         m_NumShades = 32;
   }
   else if (NumBands == 0 || pBands == NULL)
   {
      // Input is bad, don't trust it
      if (pBands)
         delete pBands;

      // default settings requested for loading

      m_NumElevationBands = 5;

      if (m_pElevationBands)
         delete m_pElevationBands;

      m_pElevationBands = new long [m_NumElevationBands];

      m_pElevationBands[0] = 2500;
      m_pElevationBands[1] = 5000;
      m_pElevationBands[2] = 7500;
      m_pElevationBands[3] = 10000;
      m_pElevationBands[4] = 12500;

      m_elev_breakpts = 5;
      m_NumShades = 32;

      return;
   }
   else 
   {
      delete [] pBands;
   }

	// find out the used number of bands
	int k;

	m_breakpts_cnt = 0;
	for (k=0;  k<m_NumElevationBands; k++)
	{
		m_elev_breakpts[k] = m_pElevationBands[k];
		if (m_pElevationBands[k] > -1390)
			m_breakpts_cnt++;
	}

}
// end of set_elevation_bands

// *****************************************************************
// *****************************************************************

void CDtedReader::set_slope_bands(unsigned NumBands, double* pBands)
{
	int k;

	// A maximum of 5 break points (6 colors) are used at the moment!!!
	if (NumBands > 5)
		NumBands = 5;

	if (NumBands < 1)
		return;

	for (k=0; k<NumBands; k++)
	{
		m_slope_breakpts[k] = pBands[k];
	}

	m_num_slope_pts = NumBands;
}
// end of set_slope_bands

// *****************************************************************
// *****************************************************************

void CDtedReader::set_actual_number_of_breakpoints(int num)
{
   m_breakpts_cnt = num;
}
*/

// *****************************************************************
// *****************************************************************

void CDtedReader::set_color_mode(bool IsDTEDMono)
{
   m_dted_mono = IsDTEDMono;
   setup_color_table();
}

bool CDtedReader::get_color_mode()
{
   return m_dted_mono;
}

// *****************************************************************
// *****************************************************************

void CDtedReader::set_contour_color_mode(bool IsContourColor)
{
   m_IsContourMono = IsContourColor;
}



// *****************************************************************
// *****************************************************************

unsigned char CDtedReader::get_shaded_color(const float &elevation, const double& dot_prod)
{
   int elev_range;
   BYTE color;

   if (m_dted_safe_colors)
   {
      int trange;
      
      if ( dot_prod <= 0.0 )
         return (DTED_READER_SHADES+1);
      else
      {
         trange = 1;
         if ( elevation < m_safe_breakpts[0] )
            trange = 0;
         if ( elevation > m_safe_breakpts[1] )
            trange = 2;
         return (m_NumShades * trange + int((m_NumShades-1) * dot_prod + 0.5));
      }
   }
   else if ( m_dted_mono )
   {
      if ( dot_prod <= 0.0 )
         return (0);
      else
         return (int( (DTED_READER_SHADES+1) * dot_prod + 0.5 ));
   }
   else
   {
      if ( dot_prod <= 0.0 )
         return (DTED_READER_SHADES+1);
      else
      {
         for ( elev_range = 0; elev_range < m_num_elev_pts; elev_range ++ )
         {
            if ( elevation < m_elev_breakpts[elev_range] )
               break;
         }
         
         color =  (m_NumShades * elev_range + int(m_NumShades * dot_prod));
		 if (color > m_max_color)
			 color = m_max_color;
		 return color;
      }
   }
}
// end of get_shaded_color

// *****************************************************************
// *****************************************************************

unsigned char CDtedReader::get_slope_color(const double &slope, const double& dot_prod)
{
   int slope_range;
   BYTE color;
   double tf;

   if ( m_dted_mono )
   {
      if (( dot_prod <= 0.0 ) && m_flat_is_black)
         return (0);
      else
	  {
		  if (slope < m_slope_breakpts[0])
			  return 0;
		  if (slope > m_slope_breakpts[1])
			  return m_max_color;

		  tf = m_slope_breakpts[1] - m_slope_breakpts[0];
		  tf = (slope - m_slope_breakpts[0]) / tf;
		  tf *= (double) m_max_color;
		  color = (int) tf;
		 if (color > m_max_color)
			 color = m_max_color;
		 return color;
	  }

   }
   else
   {
      if ( dot_prod < 0.0 )
         return (DTED_READER_SHADES+1);
      else if ((slope == 0.0) && m_flat_is_black)
         return (DTED_READER_SHADES+1);
	else
      {
         for ( slope_range = 0; slope_range < m_num_slope_pts; slope_range ++ )
         {
            if ( slope < m_slope_breakpts[slope_range] )
               break;
         }
         
//         return (m_NumShades * slope_range + int(m_NumShades * dot_prod));
         return (m_NumShades * slope_range + int(m_NumShades * 0.5));
      }
   }
}
// end of get_slope_color

// *****************************************************************
// *****************************************************************

unsigned char CDtedReader::get_elevation_color(const double &elevation, const double min_elev,
											   const double max_elev)
{
	int elev_range;
	int color;
	double factor;

   if (m_dted_safe_colors)
   {
      int trange;
      
		trange = 1;
		if ( elevation < m_safe_breakpts[0] )
			trange = 0;
		if ( elevation > m_safe_breakpts[1] )
			trange = 2;
		factor = elevation / m_elev_breakpts[m_num_elev_pts-1];
		if (factor > 0.99)
			factor = 0.99;
		if (trange == m_num_elev_pts)
			factor = 0.5;
		if (trange == 0)
			factor = 0.5;
		color = (m_NumShades * trange + int(m_NumShades * factor));
		if (color > m_max_color)
			color = m_max_color;
		if (color < 0)
			color = 0;
		return color;
   }
   else if ( m_dted_mono )
   {
//      if ( dot_prod <= 0.0 )
//         return (0);
//      else
//         return (int( (DTED_READER_SHADES+1) * dot_prod + 0.5 ));
	   double tf, elev;
	   double range = max_elev - min_elev;
	   elev = elevation;
	   if (elev < min_elev)
		   elev = min_elev;
	   if (elev > max_elev)
		   elev = max_elev;
	   tf = (elev - min_elev) / range;
	   color = (unsigned char) (tf * (double) m_max_color);
	   if (color > m_max_color)
		   color = m_max_color;
	   return color;
   }
   else
   {
		for ( elev_range = 0; elev_range < m_num_elev_pts; elev_range ++ )
		{
			if ( elevation < m_elev_breakpts[elev_range] )
				break;
		}

		factor = elevation / m_elev_breakpts[m_num_elev_pts-1];
		if (factor > 0.99)
			factor = 0.99;
		if (elev_range == m_num_elev_pts)
			factor = 0.5;
		if (elev_range == 0)
			factor = 0.5;
		color = (m_NumShades * elev_range + int(m_NumShades * factor));
		if (color > m_max_color)
		   color = m_max_color;
		if (color < 0)
			color = 0;
		return color;
//		return (m_NumShades * elev_range + int(m_NumShades * 0.5));
   }
}
// end of get_elevation_color

// *****************************************************************
// *****************************************************************

bool CDtedReader::CheckContour(short Left, short Right, short Up, float& level)
{
   if (m_ContourLinesOn && m_ContourInterval != 0) 
   {
      // Simple graphic contour interval draw
      short a, b, ct;
      a = (Left < Right) ? Left : Right;
      b = (Left > Right) ? Left : Right;
      ct = (short) (a / m_ContourInterval);
      b -= (short) (ct * m_ContourInterval);

      if (b>(m_ContourInterval-m_ContourWidth))
      {
         level = ct*m_ContourInterval;
         return true;
      }
      
      a = (Left < Up) ? Left : Up;
      b = (Left > Up) ? Left : Up;
      ct = (short) (a / m_ContourInterval);
      b -= (short) (ct * m_ContourInterval);

      if (b>(m_ContourInterval-m_ContourWidth))
      {
         level = ct*m_ContourInterval;
         return true;
      }
      
      // Check for the 0 contour
      if (Left==0)
      {
         if (Right!=0 || Up != 0)
         {
            level = 0;
            return true;
         }
      }
      else if (Right==0 || Up == 0)
         {
            level = 0;
            return true;
         }
   }

   return false;
}

// *****************************************************************
// *****************************************************************

double CDtedReader::calc_dot_prod(short Left, short Right, short Up,
                                  float meters_lat_per_pixel, float meters_lon_per_pixel)
{
   // See get_image() for full calculations

   double vb_z, vr_z;
   double norm_x, norm_y, norm_z, mag;
   
   double dot_prod;
   
   // Make references
   double yb = meters_lat_per_pixel;
   double xr = meters_lon_per_pixel;
   double &vb_y=yb, &vr_x=xr;
   
   
   // Vector from point to point below.
   vb_z = m_exaggeration_factor * (Up - Left);
   
   // Vector from point to point to the right.
   vr_z = m_exaggeration_factor * (Right - Left);
   
   // Form normal unit vector by cross product.
   norm_x = vb_y*vr_z;
   norm_y = vb_z*vr_x;
   norm_z = - vb_y*vr_x;
   
   mag = sqrt( norm_x*norm_x + norm_y*norm_y + norm_z*norm_z );
   norm_x /= mag;
   norm_y /= mag;
   norm_z /= mag;
   
   // Form dot product of normal unit vector and light direction vector to 
   // calculate brightness.
   dot_prod = norm_x*m_light_dir_x + norm_y*m_light_dir_y + norm_z*m_light_dir_z;
   
	// normalize to avoid floating point errors
	
	if ( dot_prod > 1.0 )
		dot_prod = 1.0;

	if ( dot_prod < -1.0 )
		dot_prod = -1.0;

   return dot_prod;
}
// end of calc_dot_prod

// *****************************************************************
// *****************************************************************

int CDtedReader::parse_band(std::string input, std::vector<std::string>& band)
{
	int len, pos, cnt, k;
	char txt[81];

	band.erase(band.begin(), band.end());
	band.resize(7);
	len = input.size();

	if (len < 3)
		return FAILURE;

	cnt = atoi(&input[0]);
	if (cnt > 6)
		return FAILURE;

	for (k=0; k<cnt; k++)
		band[k] = "0";

	std::string TempString = input;

	const char* pstr = input.c_str();

	cnt = 0;
	pos = 0;
	for (k=0; k<len; k++)
	{
		if (pstr[k] == '/')
		{
			txt[pos] = '\0';
			band[cnt] = txt;
			cnt++;
			pos = 0;
		}
		else
		{
			txt[pos] = pstr[k];
			pos++;
		}
	}

	txt[pos] = '\0';
	band[cnt] = txt;

	return SUCCESS;
}
// end of parse_band

// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************