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

///////////////////////////////////////////////////////////////////////////////
// DtedReader.h
// 
// File reading code for DTED1/2
///////////////////////////////////////////////////////////////////////////////

//
// This class is used to read DTED files as defined in MIL-PRF-89020A
//

#pragma once

#ifndef _WIN32
// Cross-platform port: the Windows product build pulls these in transitively
// through stdafx.h/windows.h (included by DtedReader.cpp before this header).
// This header uses std::string/std::vector directly and BOOL/BYTE/COLORREF/RGB
// in its member/method signatures, so on POSIX they come from the standard
// library plus the Win32 compatibility shim. Guarded so the MSVC build is
// byte-for-byte unchanged.
#include <string>
#include <vector>
#include "fv_compat.h"
#endif

#define DTED_READER_SUCCESS 0
#define DTED_READER_FAILURE -1

#define DTED_READER_EARTH_RADIUS_IN_METERS 6378137.0
#define DTED_READER_HALF_PI          1.57079632679489661923
#define DTED_READER_METERS_TO_FEET(meters)    (((double)(meters)) * 3.28083989501)


#define DTED_READER_NUM_COLORS 216
#define DTED_READER_SHADES 192

#define DTED_NUM_HORIZONTAL_TILES 10   // 1 to 100
#define DTED_NUM_VERTICAL_TILES 10     // 1 to 100

#define SUCCESS 0
#define FAILURE -1

struct user_header_label
{
   char recognition_sentinel[3];
   char fixed;
   char origin_lon[8];
   char origin_lat[8];
   char interval_lon[4];
   char interval_lat[4];
   char abs_vert_accuracy[4];
   char security_code[3];
   char unique_reference[12];
   char num_lon_lines[4];
   char num_lat_points[4];
   char multiple_accuracy;
   char reserved[24];
};

struct data_set_identification_record
{
   char recognition_sentinel[3];
   char security_code[1];
   char security_control_and_release_markings[2];
   char security_handling_instructions[27];
   char reserved_1[26];
   char series_designator[5];
   char unique_reference_number[15];
   char reserved_2[8];
   char data_edition_number[2];
   char match_merger_version[1];
   char maintenance_date[4];
   char match_merge_date[4];
   char maintenance_description_code[4];
   char producer_code[8];
   char reserved_3[16];
   char product_specification[9];
   char product_specification_amendment_number[2];
   char date_of_product_specification[4];
   char vertical_datum[3];
   char horizontal_datum[5];
   char digitizing_collection_system[10];
   char compilation_date[4];
   char reserved_4[22];
   char data_origin_lat[9];
   char data_origin_lon[10];
   char data_sw_lat[7];
   char data_sw_lon[8];
   char data_nw_lat[7];
   char data_nw_lon[8];
   char data_ne_lat[7];
   char data_ne_lon[8];
   char data_se_lat[7];
   char data_se_lon[8];
   char orientation_angle[9];
   char lat_interval[4];
   char lon_interval[4];
   char num_lat_lines[4];
   char num_lon_lines[4];
   char partial_cell_indicator[2];
   char reserved_for_dma[101];
   char reserved_producing_nation[100];
   char reserved_5[156];
};

struct coordinate_record
{
   char latitude[9];
   char longitude[10];
};

struct subregion_record
{
   char abs_vert_accuracy_meters[4];
   char abs_horz_accuracy_meters[4];
   char rel_vert_accuracy_meters[4];
   char rel_horz_accuracy_meters[4];
   char num_coordinates[2];
   coordinate_record coordinate_records[14];
};

struct accuracy_record
{
   char recognition_sentinel[3];
   char abs_horz_accuracy_meters[4];
   char abs_vert_accuracy_meters[4];
   char rel_horz_accuracy_meters[4];
   char rel_vert_accuracy_meters[4];
   char reserved_1[4];
   char reserved_2[1];
   char reserved_3[31];
   char mul_acc_outlines[2];  // 0 - no subregions defined or 2..9 subregions defined

   subregion_record subregion_records[9];
   
   char reserved_4[18];
   char reserved_5[69];
};

struct data_record
{
   unsigned char recognition_sentinel;
   char data_block_count[3];
   char longitude_count[2];
   char latitude_count[2];
   unsigned char elevation1[2];
   unsigned char elevation2[2];
   unsigned char elevation3[2];
   unsigned char elevation4[2];
   unsigned char elevation5[2];
   char checksum[4];
};

class CDtedReader
{
public:
   CDtedReader( );                     // default constructor
   CDtedReader( std::string filename );    // contruct and open file
   ~CDtedReader( );                    // destructor

   int open( std::string filename );       // open dted file, return DTED_READER_SUCCESS or DTED_READER_FAILURE
   void close( );                      // close object - closing file if one is open

   bool is_open( );                    // returns true if a file is open, false otherwise
	std::string get_filename( );            // returns the name of the open file ("" if not open)
	std::string get_error_message( );       // returns error message

   // UHL functions
   int get_origin( double &origin_lat, double &origin_lon );
   int get_intervals( double &interval_lat, double &interval_lon );
   double get_abs_vertical_accuracy( );
	std::string get_security_code( );
   int get_num_lon_lines( );
   int get_num_lat_points( );
   
   // DSI functions
	std::string get_series_designator( );   // returns "DTED1", "DTED2", etc.
	std::string get_vertical_datum( );      // returns "MSL", etc.
	std::string get_horizontal_datum( );    // returns "WGS84", etc.
   int get_data_bounding_rectangle( double &sw_lat, double &sw_lon,
                                    double &nw_lat, double &nw_lon, 
                                    double &ne_lat, double &ne_lon, 
                                    double &se_lat, double &se_lon
                                  );
   bool is_partial_cell( );            // returns true if it is a partial cell.

   short get_elevation_in_meters( double lat, double lon );


   // Image functions
   int get_image_width( );
   int get_image_height( );
   int get_image_bounding_rectangle( double &sw_lat, double &sw_lon,
                                     double &nw_lat, double &nw_lon, 
                                     double &ne_lat, double &ne_lon, 
                                     double &se_lat, double &se_lon
                                   );
   int get_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                     unsigned char *subimage );
   int get_subimage_2( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                     unsigned char *subimage );

   int get_elev_data(short*& data, int image_height, int image_width, 
                      int tile_index);

   int set_light_direction( double light_dir_x, double light_dir_y, double light_dir_z );
   int get_light_direction( double &light_dir_x, double &light_dir_y, double &light_dir_z );
   int set_exaggeration_factor( float exaggeration_factor );
   float get_exaggeration_factor( );
   int lat_lon_to_pixel( double lat, double lon, int &hpix, int &vpix );
   int pixel_to_lat_lon( int hpix, int vpix, double &lat, double &lon );
   int get_degrees_per_pixel( double &degrees_lat_per_pixel, 
      double &degrees_lon_per_pixel );
   int get_meters_per_pixel( double lat, double lon, double &meters_lat_per_pixel,
      double &meters_lon_per_pixel );
   int get_number_of_colors( );
   void get_color_table( unsigned char color_table[][3] );

   // Tile functions
   int get_num_tiles( );
   int get_tile_data( int tile_index, int &tile_id,
      int &tile_width, int & tile_height,
      double &tile_ll_lat, double &tile_ll_lon,
      double &tile_ur_lat, double &tile_ur_lon );
   int get_all_tile_data( int *tile_ids, int *tile_widths, int *tile_heights,
      double *tile_ll_lats, double *tile_ll_lons,
      double *tile_ur_lats, double *tile_ur_lons );
   int get_tile_image( int tile_index, unsigned char* tile_image );

// parameter functions

   void set_contour_lines_on(BOOL enabled);
   BOOL get_contour_lines_on() { return m_ContourLinesOn; }

   float set_contour_interval(float interval);
   float get_contour_interval();

   unsigned short set_contour_width(unsigned short width);

   unsigned short set_display_mode(unsigned short DisplayMode);
   unsigned short get_display_mode();

//   void set_elevation_bands(unsigned NumBands, long* pBands);
   void set_color_bands(std::string colorbands);
   void set_color_bands(int num_bands, long *red, long *green, long *blue);
   unsigned short get_num_color_bands() { return m_num_color_bands; }
   void get_color_bands(long *red, long *green, long *blue);

	void set_elevation_bands(std::string elevbands);
   void set_elevation_bands(int num_bands, int *bands);
   unsigned short get_num_elevation_bands() { return m_num_elev_pts; }
   void get_elevation_bands(int *bands);

	void set_slope_bands(std::string slopebands);
   void set_slope_bands(int num_bands, double *bands);
   unsigned short get_num_slope_bands() { return m_num_slope_pts; }
   void get_slope_bands(double *bands);
   



   void setup_color_table( );
   void set_contour_color(COLORREF color);
   COLORREF get_contour_color() { return m_contour_color; }

	int	parse_elevation_string();
	int	parse_slope_string();



   int parse_color_string(std::string & colorbands, unsigned short *cnt, double *arhue, double *arsat);

	void set_flat_is_black(bool is_black );
   bool get_flat_is_black() { return m_flat_is_black; }

   void set_color_mode(bool IsDTEDMono);
   bool get_color_mode();
   void set_contour_color_mode(bool IsContourColor);
	static void color_to_hsb(COLORREF rgbcolor, double *hue, double *sat, double *brit);
	static COLORREF text_to_color(std::string colorstr);

   user_header_label& GetUserHeaderLabel() { return m_uhl; }
   data_set_identification_record& GetDataSetIdentificationRecord() { return m_dsi; }
   accuracy_record& GetAccuracyRecord() { return m_acc; }

protected:
   bool m_open;               // true if file successfully opened
	std::string m_filename;        // the name of the open dted file
	std::string m_error_message;   // most recent error message
   FILE* m_file_ptr;          // file pointer for the dted file

   user_header_label m_uhl;
   data_set_identification_record m_dsi;
   accuracy_record m_acc;
   data_record m_data;

   // UHL data
   double m_origin_lon;
   double m_origin_lat;
   double m_interval_lon;
   double m_interval_lat;
   double m_abs_vertical_accuracy;
	std::string m_security_code;
   int m_num_lon_lines;
   int m_num_lat_points;
   bool m_multiple_accuracy;
   int m_max_color;

   // DSI data
   std::string m_series_designator;
   std::string m_vertical_datum;
   std::string m_horizontal_datum;
   double m_data_sw_lat;
   double m_data_sw_lon;
   double m_data_nw_lat;
   double m_data_nw_lon;
   double m_data_ne_lat;
   double m_data_ne_lon;
   double m_data_se_lat;
   double m_data_se_lon;
   bool m_partial_cell;

   // Image data
   int m_image_width;
   int m_image_height;
   double m_image_sw_lat;
   double m_image_sw_lon;
   double m_image_nw_lat;
   double m_image_nw_lon;
   double m_image_ne_lat;
   double m_image_ne_lon;
   double m_image_se_lat;
   double m_image_se_lon;
   short *m_col_buffer_1;
   short *m_col_buffer_2;
   double m_light_dir_x, m_light_dir_y, m_light_dir_z;
   float m_exaggeration_factor;
   unsigned char m_color_table[DTED_READER_NUM_COLORS][3];

   // Tile data
   int m_num_tiles;
   int *m_tile_ids;
   int *m_tile_min_hpixs;
   int *m_tile_min_vpixs;
   int *m_tile_max_hpixs;
   int *m_tile_max_vpixs;
   double *m_tile_ll_lats;
   double *m_tile_ll_lons;
   double *m_tile_ur_lats;
   double *m_tile_ur_lons;

   // Registry data
   bool m_dted_mono;
   double m_elev_breakpts[5];
   double m_slope_breakpts[5];
   unsigned short  m_num_elev_pts;
   unsigned short  m_num_slope_pts;
   unsigned short m_num_color_bands;
   COLORREF m_color_bands[6];
	bool m_dted_safe_colors;
	double m_safe_breakpts[2];

//	int m_breakpts_cnt;

   // Color range setup data
   unsigned short m_NumShades;
   unsigned short m_NumBrkPts;

//	unsigned short m_num_colors;

   // colors
   COLORREF m_color1;
   COLORREF m_color2;
   COLORREF m_color3;
   COLORREF m_color4;
   COLORREF m_color5;
   COLORREF m_color6;
   COLORREF m_contour_color;

   std::string m_ElevationBands;
   std::string m_SlopeBands;
	std::string m_ColorBands;

   double m_grid_spacing;

   // Protected member functions
   int get_column_of_elevations( int col, short *col_buffer );
   int get_subcolumn_of_elevations( int col, int min_row, int max_row,
      short *col_buffer );
   void hsb2rgb( double hue, double sat, double brit, BYTE *r, BYTE *g, BYTE *b );
   BYTE hsb2rgb1(double rm1, double rm2, double rh);
   static void rgb2hsb(BYTE red, BYTE green, BYTE blue, double *hue, double *sat, double *brt); 


   double calc_dot_prod(short Left, short Right, short Up,
                        float meters_lat_per_pixel, float meters_lon_per_pixel);
   unsigned char get_shaded_color(const float &elevation, const double& dot_prod);
   unsigned char get_slope_color(const double &slope, const double& dot_prod);
   unsigned char get_elevation_color(const double &elevation, const double min_elev,
											   const double max_elev);

	int parse_band(std::string input, std::vector<std::string> &band);

   bool CheckContour(short Left, short Right, short Up, float& level);

   // Parameterized operations variables
   float m_ContourInterval;
   unsigned short m_ContourWidth;
   BOOL m_ContourLinesOn;

   unsigned short m_DisplayMode;

   bool m_IsContourMono;
   bool m_flat_is_black;
};