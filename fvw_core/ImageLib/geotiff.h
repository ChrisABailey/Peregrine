// Copyright (c) 1994-2009,2013 Georgia Tech Research Corporation, Atlanta, GA
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

// GeoTiff.h : Declarations of CGeoTiff, CGeoData, CTiffTag, CGeoKey



// Author: Barrett D. Flansburg
// GTRI FalconView

#ifndef GEOTIFF_H

#define GEOTIFF_H

// Select one and only one of the following geotiff file input methods
#ifdef _WIN32
#define MAPPEDFILE_FILE_INPUT    // File-mapped input
#endif  // POSIX: falls through to FREAD_FILE_INPUT below
//#define READFILE_FILE_INPUT      // ReadFile() input
//#define LLFILE_FILE_INPUT        // _read() input
//#define FREAD_FILE_INPUT         // fread() input (default)

#if !defined MAPPEDFILE_FILE_INPUT && !defined READFILE_FILE_INPUT \
   && !defined LLFILE_FILE_INPUT && !defined FREAD_FILE_INPUT
   #define FREAD_FILE_INPUT         // fread() input is the default
#endif

#ifndef NO_FULL_STRIP_READ
   #ifndef MAPPEDFILE_FILE_INPUT
      #define FULL_STRIP_READ    /* To avoid network fseek's */
   #endif
#endif


#include "jpeg//jpeg.h"
#include "image_d.h"
#include "tiffdata.h"

//#include "interfaces//imagelib.h"

struct value_name
{
   unsigned short id;
   char *name;
};

// this class maintains the geodata
class CGeoData
{
public:
   CGeoData( );                                   // constructor
   ~CGeoData( );                                  // destructor
   void clear( );                                 // clears object
   void operator=( CGeoData& geodata ); // assignment operator
   VOID CalcFwdCoeffs();                           // Forward coefficients from inverse
   VOID inv_transform( DOUBLE dX, DOUBLE dY, DOUBLE& dL, DOUBLE& dP );
   VOID fwd_transform( DOUBLE dL, DOUBLE dP, DOUBLE& dX, DOUBLE& dY );

   union
   {
      struct   // Output nomenclature from RPC spec: P=lat, L=lon, H=height
      {
         DOUBLE m_dPScaleX;         // Inverse coeffs
         DOUBLE m_dPScaleY;
         DOUBLE m_dLScaleX;
         DOUBLE m_dLScaleY;
         DOUBLE m_dXScaleP;         // Forward coeffs
         DOUBLE m_dXScaleL;
         DOUBLE m_dYScaleP;
         DOUBLE m_dYScaleL;
         DOUBLE m_dHScaleZ;
         DOUBLE m_dXOffset;         // Input
         DOUBLE m_dYOffset;
         DOUBLE m_dZOffset;
         DOUBLE m_dPOffset;         // Output, lat
         DOUBLE m_dLOffset;         // Lon
         DOUBLE m_dHOffset;         // Height
      };
      DOUBLE m_dModelPixelTransformation[ 15 ];
   };

   int m_num_tie_points;
   double *m_model_tie_point_hpix;
   double *m_model_tie_point_vpix;
   double *m_model_tie_point_zpix;
   double *m_model_tie_point_x;
   double *m_model_tie_point_y;
   double *m_model_tie_point_z;

   unsigned short m_gt_model_type;
   unsigned short m_gt_raster_type;
   CString m_gt_citation;

   unsigned short m_geographic_type;
   CString m_geog_citation;
   unsigned short m_geog_geodetic_datum;
   unsigned short m_geog_prime_meridian;
   double m_geog_prime_meridian_long;
   unsigned short m_geog_ellipsoid;
   double m_geog_semi_major_axis;
   double m_geog_semi_minor_axis;
   double m_geog_inv_flattening;

   unsigned short m_projected_cs_type;
   CString m_pcs_citation;
   unsigned short m_projection;
   unsigned short m_proj_coord_trans;
   double m_proj_std_parallel_1;
   double m_proj_std_parallel_2;
   double m_proj_nat_origin_long;
   double m_proj_nat_origin_lat;
   double m_proj_false_easting;
   double m_proj_false_northing;
   double m_proj_false_origin_long;
   double m_proj_false_origin_lat;
   double m_proj_false_origin_easting;
   double m_proj_false_origin_northing;
   double m_proj_center_long;
   double m_proj_center_lat;
   double m_proj_center_easting;
   double m_proj_center_northing;
   double m_proj_scale_at_nat_origin;
   double m_proj_scale_at_center;
   double m_proj_azimuth_angle;
   double m_proj_straight_vert_pole_long;

   unsigned short m_vertical_cs_type;
   CString m_vertical_citation;
   unsigned short m_vertical_datum;

   BOOL m_model_pixel_scale_present;
   BOOL m_model_tie_point_present;

   BOOL m_gt_model_type_present;
   BOOL m_gt_raster_type_present;
   BOOL m_gt_citation_present;

   BOOL m_geographic_type_present;
   BOOL m_geog_citation_present;
   BOOL m_geog_geodetic_datum_present;
   BOOL m_geog_prime_meridian_present;
   BOOL m_geog_prime_meridian_long_present;
   BOOL m_geog_ellipsoid_present;
   BOOL m_geog_semi_major_axis_present;
   BOOL m_geog_semi_minor_axis_present;
   BOOL m_geog_inv_flattening_present;

   BOOL m_projected_cs_type_present;
   BOOL m_pcs_citation_present;
   BOOL m_projection_present;
   BOOL m_proj_coord_trans_present;
   BOOL m_proj_std_parallel_1_present;
   BOOL m_proj_std_parallel_2_present;
   BOOL m_proj_nat_origin_long_present;
   BOOL m_proj_nat_origin_lat_present;
   BOOL m_proj_false_easting_present;
   BOOL m_proj_false_northing_present;
   BOOL m_proj_false_origin_long_present;
   BOOL m_proj_false_origin_lat_present;
   BOOL m_proj_false_origin_easting_present;
   BOOL m_proj_false_origin_northing_present;
   BOOL m_proj_center_long_present;
   BOOL m_proj_center_lat_present;
   BOOL m_proj_center_easting_present;
   BOOL m_proj_center_northing_present;
   BOOL m_proj_scale_at_nat_origin_present;
   BOOL m_proj_scale_at_center_present;
   BOOL m_proj_azimuth_angle_present;
   BOOL m_proj_straight_vert_pole_long_present;

   BOOL m_vertical_cs_type_present;
   BOOL m_vertical_citation_present;
   BOOL m_vertical_datum_present;
};

// this class maintains the data for a tiff tag
class CTiffTag
{
public:
   CTiffTag( );                  // constructor
   ~CTiffTag( );                 // destructor
   void clear( );                // clears object

   BOOL m_defined;               // TRUE if defined
   unsigned short m_tag_id;         // tiff tag number
   unsigned short m_type;        // data type
   unsigned int m_count;         // number of data values
   unsigned int m_value_offset;  // file offset of data value
   CString m_tag_name;           // name of the tag
   CString m_type_name;          // name of tag type
   CString m_value_name;         // name of tag value

   // arrays for storing all the different possible data types
   unsigned char *m_byte_values;                   // GEOTIFF_BYTE
   char *m_ascii_values;                           // GEOTIFF_ASCII
   unsigned short *m_short_values;                 // GEOTIFF_SHORT
   unsigned int *m_long_values;                    // GEOTIFF_LONG
   unsigned int *m_rational_numerator_values;      // GEOTIFF_RATIONAL
   unsigned int *m_rational_denominator_values;    // GEOTIFF_RATIONAL
   char *m_sbyte_values;                           // GEOTIFF_SBYTE
   char *m_undefined_values;                       // GEOTIFF_UNDEFINED_TYPE
   short *m_sshort_values;                         // GEOTIFF_SSHORT
   int *m_slong_values;                            // GEOTIFF_SLONG
   int *m_srational_numerator_values;              // GEOTIFF_SRATIONAL
   int *m_srational_denominator_values;            // GEOTIFF_SRATIONAL
   float *m_float_values;                          // GEOTIFF_FLOAT
   double *m_double_values;                        // GEOTIFF_DOUBLE
};

// this class maintains the data for a geokey
class CGeoKey
{
public:
   CGeoKey( );                         // constructor
   ~CGeoKey( );                        // destructor
   void clear( );                      // clears object

   BOOL m_defined;                     // TRUE if defined
   unsigned short m_geokey_id;         // geokey id number
   unsigned short m_tiff_tag_location; // indicates location of data
   unsigned short m_type;              // data type
   unsigned short m_count;             // number of data values
   unsigned short m_value_offset;      // offset of data value in tag array
   CString m_geokey_name;              // name of the geokey
   CString m_type_name;                // name of geokey type
   CString m_value_name;               // name of geokey value

   // arrays for storing all the different possible data types
   char *m_ascii_values;                           // GEOTIFF_ASCII
   unsigned short *m_short_values;                 // GEOTIFF_SHORT
   double *m_double_values;                        // GEOTIFF_DOUBLE
};

class CTiepointTransform
{
public:
   CTiepointTransform( );
   ~CTiepointTransform( );

   void clear( );
   int define_tiepoints( int num_tiepoints, double *x, double *y,
                         double *latitude, double *longitude );
   int invert_matrix( double *matrix, int n, double *inverse_matrix );
   int decompose_lu( double *a, int n, int *indx );
   void back_substitute_lu( double *a, int n, int *indx, double *b );
   int inv_transform( double x, double y, double &latitude, double &longitude );
   int fwd_transform( double latitude, double longitude, double &x, double &y );


   BOOL m_defined;
   int m_num_tiepoints, m_num_rows;
   double m_aprox0;
   double *m_tiepoint_x, *m_tiepoint_y;
   double *m_tiepoint_latitude, *m_tiepoint_longitude;
   double *m_x_coef, *m_y_coef, *m_latitude_coef, *m_longitude_coef;
};

// ****************************************************************
// ****************************************************************
// ****************************************************************

#define COLOR_QUANTIZE_GRAYSCALE 1
#define COLOR_QUANTIZE_PALETTE 2
#define COLOR_QUANTIZE_HISTOGRAM 3

class CImageMap;      // Forward reference

// this class maintains the data for a color quantizer object, which is used to
// select a color palette for one or more tiff files passed in a CStringArray
class CColorQuantizer
{
public:
   // constructor
   CColorQuantizer( );

   // destructor
   ~CColorQuantizer( );

   // clear to initial state
   void clear( );

   // select colors and quantization method for a list of tiff files
   int select_colors( CImageMap& image_map, int num_colors,
                      unsigned char *red, unsigned char *green,
                      unsigned char *blue, CString &error_message, IImageLibCallback *callback );

   int set_colors(int num_colors, BYTE *red, BYTE *green, BYTE *blue, CString & error_msg);

   // public data members
public:
   BOOL m_defined;
   int m_method, m_num_colors;
   unsigned int *m_histogram;
   unsigned char *m_histogram_indices;
   unsigned char *m_red, *m_green, *m_blue;
};

struct rgb_box
{
   int num_colors;
   BOOL small_enough;
   unsigned char min_red;
   unsigned char max_red;
   unsigned char min_green;
   unsigned char max_green;
   unsigned char min_blue;
   unsigned char max_blue;
};

int median_cut( unsigned int *histogram, int num_colors, unsigned char *red,
                unsigned char *green, unsigned char *blue,
                unsigned char *histogram_indices );

// this class maintains the data for a Tiff or GeoTiff file
class CGeoTiff
{

// public interface ***********************************************************
public:

   // constructors
   CGeoTiff( );

   // destructor
   ~CGeoTiff( );

   // clear to initial state
   void clear( );

   // load tiff file
   int load( const char *tiff_file_name, BOOL &image_type_supported,
             int &image_type, CString &image_type_description,
             int &image_width, int &image_length,
             BOOL &geodata_present, BOOL &geodata_supported,
             CString &error_message );

   // get the geodata
   int get_geodata( CGeoData &geodata, int &error_code,
                    CString &error_message );

   int get_image_description( CString &image_description );

   int get_software( CString &software );

   int get_image_width( int &image_width );

   int get_image_length( int &image_length );

   int get_image_type( int &image_type );

   // get the image scale
   int get_scale( int &horizontal_scale, int &vertical_scale );

   // find the latitude and longitude of a pixel
   int inv_transform( int hpix, int vpix, double &latitude, double &longitude );

   // find the pixel corresponding to a latitude and longitude
   int fwd_transform( double latitude, double longitude, int &hpix, int &vpix );

   // get the latitude and longitude of the four image corners
   int get_image_bounds( double &lat_upper_left, double &long_upper_left,
                         double &lat_lower_left, double &long_lower_left,
                         double &lat_lower_right, double &long_lower_right,
                         double &lat_upper_right, double &long_upper_right );

   // convert degrees to degrees-minutes-seconds
   void deg_to_dms( double deg, int &sign, int &ideg, int &imin, double &sec,
                    CString &dms );

   // convert degrees-minutes-seconds to degrees
   void dms_to_deg( int sign, int ideg, int imin, double sec, double &deg );

   // print the tag info to the specified file stream
   void print_tags( FILE *file_ptr );

   // print the tag info to the specified CStringArray
   void print_tags( CStringArray &cstring_array );

   // print the geokey info to the specified file stream
   void print_geokeys( FILE *file_ptr );

   // print the geokey info to the specified CStringArray
   void print_geokeys( CStringArray &cstring_array );

   // print the image info to the specified file stream
   void print_image_info( FILE *file_ptr );

   // print the image info to the specified CStringArray
   void print_image_info( CStringArray &cstring_array );

   CString get_info_string(void);


   // get the image width and height
   int get_image_size( int &image_width, int &image_length );

   // get the subsampled image width and height for entire image
   int get_subsampled_image_size( int sampling, int &image_width,
                                  int &image_length );

   // get the subsampled image width and height for a subimage
   int get_subsampled_subimage_size( int sampling,
      int min_hpix, int min_vpix, int max_hpix, int max_vpix,
      int &image_width, int &image_length );

   // select the sampling to display entire image within specified width &
   // length
   int get_sampling( int max_width, int max_length, int &sampling );

   // select the sampling to display a subimage within specified width & length
   int get_subimage_sampling( int min_hpix, int min_vpix, int max_hpix,
                              int max_vpix, int max_width, int max_length,
                              int &sampling );

   // get image format
   int get_image_format( unsigned short &photometric_interpretation,
                         unsigned short &bits_per_sample );

   // get the entire image in rgb format
   int get_rgb_image( unsigned char *img, IImageLibCallback *callback );

   // get the entire image, subsampled, in rgb format
   int get_subsampled_rgb_image( int sampling, unsigned char *img, IImageLibCallback *callback );

   // get the subimage in rgb format
   int get_rgb_subimage( int min_hpix, int min_vpix, int width, int height,
                         unsigned char *img, IImageLibCallback *callback );

   int get_8bit_grayscale_subimage( int min_hpix, int min_vpix, int width, int height,
                                   unsigned char *img, IImageLibCallback *callback );
   int get_16bit_grayscale_subimage( int min_hpix, int min_vpix, int width, int height,
                           unsigned short *img, IImageLibCallback *callback );


// int get_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix, unsigned char *img,
//                                IImageLibCallback *callback );

   // get the subimage, subsampled, in rgb format
   int get_subsampled_rgb_subimage( int sampling,
                         int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                         unsigned char *img, IImageLibCallback *callback );

   // get entire image in palette form
   int get_palette_image( CColorQuantizer &color_quantizer,
                          unsigned char *indices );

   // get entire image, subsampled, in palette form
   int get_subsampled_palette_image( int sampling,
         CColorQuantizer &color_quantizer, unsigned char *indices );

   // get subimage in palette form
   int get_palette_subimage( int min_hpix, int min_vpix, int max_hpix,
      int max_vpix, CColorQuantizer &color_quantizer, unsigned char *indices, IImageLibCallback *callback );

   // get subimage in palette form (simple indexes)
   int get_palette_subimage( int min_hpix, int min_vpix, int max_hpix,
      int max_vpix, unsigned char *indices );

   // get subimage, subsampled, in palette form
   int get_subsampled_palette_subimage( int sampling, int min_hpix,
      int min_vpix, int max_hpix, int max_vpix,
      CColorQuantizer &color_quantizer, unsigned char *indices, IImageLibCallback *callback );

   // get a copy of the image 32k histogram
   int get_histogram( unsigned int *histogram, IImageLibCallback *callback );

   // get compute histogram and color frequencies
   int compute_histogram2( int *hist, unsigned long *freq_red, unsigned long *freq_grn,
                      unsigned long *freq_blu, IImageLibCallback *callback);

   // get number of colors in color map
   int get_num_color_map_values( int &num_color_map_values );

   // get a copy of the image color map colors (256 values each R,G,B)
   int get_color_map( unsigned char *red, unsigned char *green,
                      unsigned char *blue );

   int get_documentation_strings( CString &document_name, CString &image_description,
      CString &image_size, CString &image_format, CString &scanner_make_and_model,
      CString &image_resolution, CString &software, CString &creation_date_and_time,
      CString &creator, CString &copyright_notice );

   // geotiff2 functions
   int get_32bit_elevation_data( int image_offset_x, int image_offset_y, // pixel offsets into the image
                        int width, int height,           // size of the image to be return in arrays
                        float *elev, CString & error_msg, IImageLibCallback *callback );

   int get_supersampled_rgb_subimage( double factor,  // the degree of oversampling
                         int image_offset_x, int image_offset_y, // pixel offsets into the image
                         int width, int height,                  // size of the image to be return in arrays
                         unsigned char *img, IImageLibCallback *callback );

   int get_supersampled_tiled_rgb_subimage( double factor,  // the degree of oversampling
                         int image_offset_x, int image_offset_y, // pixel offsets into the image
                         int width, int height,                  // size of the image to be return in arrays
                         unsigned char *img, IImageLibCallback *callback );

   int get_subsampled_rgb_subimage2( int sampling,
                         int image_offset_x, int image_offset_y, // pixel offsets into the image
                         int width, int height,                  // size of the image to be return in arrays
                         unsigned char *img, IImageLibCallback *callback );

   int get_subsampled_tiled_rgb_subimage( int sampling,           // the degree of oversampling
                           int image_offset_x, int image_offset_y, // pixel offsets into the image
                           int width, int height,              // size of the image to be return in arrays
                           unsigned char *img,
                           IImageLibCallback *callback );

   int get_subsampled_rgb_subimage( double factor,
                              int image_offset_x, int image_offset_y, // pixel offsets into the image
                              int width, int height,                   // size of the image to be return in arrays
                              BYTE *img, CString & error_msg, IImageLibCallback *callback );

   int get_supersampled_rgb_subimage( double factor,  // the degree of oversampling
                           int image_offset_x, int image_offset_y, // pixel offsets into the image
                           int width, int height,                   // size of the image to be return in arrays
                           BYTE *img, CString & error_msg, IImageLibCallback *callback );

   CString get_info(void);



   BOOL is_image_tiled();

   int get_geokeys(int *numgeokeys, CGeoKey **geokeys);

   void get_tile_size(int *width, int *height);

   int get_rgb_subimage3( int min_hpix, int min_vpix, int max_hpix,
                                int max_vpix, BYTE *color_index,
                        unsigned char *img,
                                unsigned char *pal_array, IImageLibCallback *callback );

// int get_tags(CTiffTag **tag, int *numtags);
   int get_tags(CTiffTag *tag);
   int copy_tag( CTiffTag &tag, CTiffTag *src_tag );
   void init_tag( CTiffTag &tag );

   int get_tiff_tags( CString & tagstr, int *err_code, CString & error_msg );


   int get_date_time( int *year, int *month, int *day, int *hour, int *minute, int *second, CString & datestr );

   int geotiff_datum_to_string(int datum_num, CString & datum_str);

   CString get_compression_scheme();

   int compute_histogram(IImageLibCallback *callback);

   int read_fid_file(CString filename);
   BOOL read_world_file(CString filename, double *lat_per_pix, double *lon_per_pix,
                        double *lat, double *lon, CString &error_msg);


   int get_filled_rgb_subimage(int fill_width, int fill_height,
                        int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                        BYTE *red, BYTE *grn, BYTE *blu);

   int check_for_usgs_cropping();
   int get_drg_coverage( CString tiff_file_name );
   int get_doq_coverage( CString tiff_file_name );



// protected interface *********************************************************
protected:
public:

   // TRUE if tiff file is successfully loaded
   BOOL m_file_loaded;

   // path name of loaded tiff file
   CString m_tiff_file_name;

// CString m_last_error_message;  // error message for functions not returning one

   CString m_raw_filename;  // file name without path

   // byte order of tiff file: GEOTIFF_LITTLE_ENDIAN or GEOTIFF_BIG_ENDIAN
   int m_byte_order;

   // number of image file directories
   int m_num_directories;

   // offset of image file directory
   unsigned int m_directory_offset;

   // offset of image file directory
   int *m_directory_offset_list;

   // file pointer used to read tiff file
   FILE *m_file_ptr;

   // error flag
   BOOL m_error;

   // error description
   CString m_cumulative_error_description;

   // projection type
   CString m_projection_type;

public:
   CString m_new_error_description;

   BOOL m_has_transparent_color;

   // transparent color
   COLORREF m_transparent_color;

   // page number of image to load
   int m_image_number;

public:
   // number of tags
   int m_num_tags;

   // tags
   CTiffTag *m_tags;
protected:
   // Jpeg
   CJpeg m_jpeg;
   // JPEGTables
   // NOTE (port 2026-07-15): auto_ptr<BYTE> -> unique_ptr<BYTE[]> (auto_ptr
   // removed in C++17; holds a BYTE[], so this also fixes scalar-delete of
   // array memory). Same for m_apuiHistogram below. Output unchanged.
   std::unique_ptr< BYTE[] > m_apbJpegTable;
   PBYTE m_jpeg_table;
   int m_jpeg_table_len;

   BYTE *m_buf_img;
   int m_buf_start_row;
   int m_buf_end_row;

   // geokey directory version number
   unsigned short m_geokey_directory_version;

   // geokey directory revision number
   unsigned short m_geokey_directory_revision;

   // geokey directory monor revision number
   unsigned short m_geokey_directory_minor_revision;

   // geokey directory number of keys
   unsigned short m_num_geokeys;

   // geokeys
   CGeoKey *m_geokeys;

public:
   // geodata
   BOOL m_geodata_present, m_geodata_supported;
   int m_geodata_error_code;
   CString m_geodata_error_message;
   CGeoData m_geodata;

   // image width in pixels
   unsigned int m_image_width;

   // image length (height) in pixels
   unsigned int m_image_length;

   // number of pixels
   unsigned int m_num_pixels;

   // compression scheme
   unsigned short m_compression_scheme;

   // predictor (tag 317): 1 = none, 2 = horizontal differencing.  Only ever
   // written alongside a lossless scheme (LZW here); 1 for everything else.
   unsigned short m_predictor;

   // type of image (monochrome, color palette, rgb, etc.)
   unsigned short m_photometric_interpretation;

   // number of pixel rows per strip
   unsigned int m_rows_per_strip;

   // number of strips
   unsigned int m_num_strips;

   // x & y resolution in pixels per resolution unit
   double m_x_resolution;
   double m_y_resolution;

   // resolution unit (none, inch, centimeter)
   unsigned short m_resolution_unit;

   // planar configuration
   unsigned short m_planar_configuration;

   // samples per pixel
   unsigned short m_samples_per_pixel;

   // bits per sample
   unsigned short m_bits_per_sample;

   // fill order
   unsigned short m_fill_order;

   // orientation
   unsigned short m_orientation;

   // max/min sample values
   unsigned short m_min_sample_value;
   unsigned short m_max_sample_value;

   // color map
   BOOL m_color_map_present;
   int m_num_color_map_values;
   unsigned short *m_red_color_map;
   unsigned short *m_green_color_map;
   unsigned short *m_blue_color_map;

   BOOL m_image_type_supported;
   int m_image_type;
   CString m_image_type_description;
   int m_linear_units;

   unsigned short m_geog_linear_units;
   unsigned short m_geog_angular_units, m_geog_azimuth_units;
   double m_geog_linear_unit_size;
   double m_geog_angular_unit_size;
   BOOL m_geog_linear_units_present;
   BOOL m_geog_angular_units_present;
   BOOL m_geog_azimuth_units_present;
   BOOL m_geog_linear_unit_size_present;
   BOOL m_geog_angular_unit_size_present;

   // max strip size
   unsigned int m_max_strip_byte_count;

   // histogram array
   std::unique_ptr< UINT[] > m_apuiHistogram;
   UINT (*m_puiHistogram)[ 32768 ];

   CTiepointTransform m_tiepoint_transform;

   double m_pixel_size;

   // tile data
   BOOL m_image_is_tiled;
   unsigned int m_tile_width;
   unsigned int m_tile_length;
   unsigned int m_num_tile_pixels;
   unsigned int m_num_tiles_across;
   unsigned int m_num_tiles_down;
   unsigned int m_num_tiles;
   unsigned char* m_tile_red_array;
   unsigned char* m_tile_green_array;
   unsigned char* m_tile_blue_array;
   unsigned char* m_tile_color_indices;

   unsigned short m_max_16bit_value;

   int m_highest_level;

   C_image *m_image;

   BOOL m_using_tfw_file;
   BOOL m_using_fid_file;

   double m_cropped_ul_lat;
   double m_cropped_ul_lon;
   double m_cropped_ur_lat;
   double m_cropped_ur_lon;
   double m_cropped_lr_lat;
   double m_cropped_lr_lon;
   double m_cropped_ll_lat;
   double m_cropped_ll_lon;
   BOOL m_is_cropped;

   int m_scale;
   int m_scale_from_file_name;

   BOOL m_has_elevation_data;

   // *** member functions ***

   // read tag directory
   int read_directory( int *next_dir_offset);

   // read a tag
   int read_tag( CTiffTag &tag );

   // find index of a tag
   int find_tag( unsigned short tag_id, int &index );

   // inspect tags for required data, image information
   int inspect_tags( );

   // read geokey directory
   int read_geokey_directory( );

   // find index of a geokey
   int find_geokey( unsigned short geokey_id, int &index );

   // get the name of a tag
   static CString get_tag_name( unsigned short tag_id );

   // get the name of a data type
   static CString get_type_name( unsigned short type );

   // get the name of a tag's data value or format its numeric values
   static CString get_tag_value_name( const CTiffTag &tag );

   // get the name of a geokey
   CString get_geokey_name( unsigned short geokey_id );

   // get the name of a geokey's data value or format its numeric values
   CString get_geokey_value_name( const CGeoKey &geokey );

   // read a string from the file
   int read_string( char *string, int length );

   // read a signed short integer from the file
   int read_signed_short( short &value );

   // read an unsigned short integer from the file
   int read_unsigned_short( unsigned short &value );

   // read a signed integer from the file
   int read_signed_int( signed int &value );

   // read an unsigned integer from the file
   int read_unsigned_int( unsigned int &value );

   // read a float from the file
   int read_float( float &value );

   // read a double from the file
   int read_double( double &value );

   void reverse_byte_order( char *buffer, int num_bytes );

   int __fastcall decompress_packbits( int compressed_size,
                                       BYTE* compressed,
                                       int decompressed_size,
                                       BYTE* decompressed);

   // TIFF 6.0 LZW (compression tag 5), MSB-first codes with the "early
   // change" code-width bump every encoder in the wild writes.
   int decompress_lzw( int compressed_size,
                           unsigned char *compressed,
                           int decompressed_size,
                           unsigned char *decompressed );

   // decompress one strip or tile: dispatch on m_compression_scheme, then
   // undo m_predictor.  Every reader below calls this rather than a codec.
   int decompress_strip( int compressed_size, unsigned char *compressed,
                         int decompressed_size,
                         unsigned char *decompressed );

   // undo horizontal differencing in place over rows of row_bytes bytes
   int undo_horizontal_predictor( unsigned char *data, int size,
                                  int row_bytes );

   int get_8bit_grayscale_as_rgb_image( unsigned char *img, IImageLibCallback *callback );

   int get_16bit_grayscale_as_rgb_image( unsigned char *img, IImageLibCallback *callback );

   int get_24bit_rgb_as_rgb_image( unsigned char *img, IImageLibCallback *callback );

   int get_8bit_palette_as_rgb_image( unsigned char *img, IImageLibCallback *callback );
   int get_8bit_palette_as_palette_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                       unsigned char *indices, IImageLibCallback *callback );


   int get_8bit_grayscale_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *img, IImageLibCallback *callback );

   int get_8bit_grayscale_as_palette_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *img, IImageLibCallback *callback );

//   int get_8bit_grayscale_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
//                                   unsigned char *img, IImageLibCallback *callback );


   int get_1bit_image_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *img, IImageLibCallback *callback );

// int get_1bit_image_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
//                                unsigned char *img, IImageLibCallback *callback );

   int get_16bit_grayscale_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *img, IImageLibCallback *callback );

   int get_8bit_grayscale_as_8bit_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                 unsigned char *img, IImageLibCallback *callback );

   int get_16bit_grayscale_as_16bit_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                   unsigned short *img, IImageLibCallback *callback );


//   int get_16bit_grayscale_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
//                                    unsigned char *img, IImageLibCallback *callback );


   int get_24bit_rgb_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *img, IImageLibCallback *callback );

   int get_48bit_rgb_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                           unsigned char *img, IImageLibCallback *callback );

   int get_24bit_planar_rgb_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                 unsigned char *img, IImageLibCallback *callback );

   int get_24bit_planar_rgb_as_palette_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                    CColorQuantizer &color_quantizer, unsigned char *indices,
                                    IImageLibCallback *callback);

   int get_multiband_subimage( int min_hpix, int min_vpix, int width, int height, IImageLibCallback *callback );

   int get_multiband_chunky_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix, IImageLibCallback *callback );

   int get_multiband_planar_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix, IImageLibCallback *callback );


// int get_24bit_rgb_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
//                               unsigned char *img, IImageLibCallback *callback );

   int get_tiled_24bit_rgb_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *img, IImageLibCallback *callback );

// int get_tiled_24bit_rgb_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
//                                    unsigned char *img, IImageLibCallback *callback );


   int get_8bit_palette_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *img, IImageLibCallback *callback );

// int get_8bit_palette_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
//                                  unsigned char *img, IImageLibCallback *callback );


   int calculate_histogram( IImageLibCallback *callback );

   int calculate_histogram_for_tiled_images(IImageLibCallback *callback);

   int compute_16bit_value_range(unsigned short *minval, unsigned short *maxval, IImageLibCallback *callback);

    // tiled image functions
   int get_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_8bit_grayscale_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_8bit_grayscale_tile_as_8bit( int itile, unsigned char *img );

   int get_16bit_grayscale_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_16bit_grayscale_tile_as_16bit( int itile, unsigned short *img );

   int get_24bit_rgb_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_24bit_planar_rgb_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_8bit_palette_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_tile_as_palette_image( int itile, CColorQuantizer &color_quantizer,
      unsigned char *color_indices );

   int get_8bit_grayscale_as_palette_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, CColorQuantizer &color_quantizer,
      unsigned char *indices );

   int get_8bit_grayscale_as_palette_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *indices );

   int get_16bit_grayscale_as_palette_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, CColorQuantizer &color_quantizer,
      unsigned char *indices );

   int get_24bit_rgb_as_palette_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                 CColorQuantizer &color_quantizer, unsigned char *indices,
                                 IImageLibCallback *callback);

   int get_8bit_palette_as_palette_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, CColorQuantizer &color_quantizer,
      unsigned char *indices );

   int get_8bit_palette_as_palette_subimage( int min_hpix, int min_vpix,
        int max_hpix, int max_vpix, unsigned char *indices );


   int get_tile_as_palette_image( int itile, unsigned char *color_indices );

   int get_8bit_grayscale_tile_as_palette_image( int itile,
      CColorQuantizer &color_quantizer, unsigned char *color_indices );

   int get_8bit_grayscale_tile_as_palette_image( int itile, unsigned char *color_indices );

   int get_16bit_grayscale_tile_as_palette_image( int itile,
      CColorQuantizer &color_quantizer, unsigned char *color_indices );

   int get_8bit_palette_tile_as_palette_image( int itile,
      CColorQuantizer &color_quantizer, unsigned char *color_indices );

   int get_8bit_palette_tile_as_palette_image( int itile, unsigned char *color_indices );

   int get_24bit_rgb_tile_as_palette_image( int itile,
      CColorQuantizer &color_quantizer, unsigned char *color_indices );

   int get_thumbnail(int max_width, int max_height, int *width, int *height, BYTE *img, CString &err_msg, IImageLibCallback *callback);
   int get_tiled_thumbnail(int max_width, int max_height, int *width, int *height, BYTE *img, CString &err_msg, IImageLibCallback *callback);
   int get_bufferred_rgb_row(int row, BYTE *img, CString &err_msg);
   int get_rgb_subblock(int starty, int height, BYTE *img, CString &err_msg);

   int log2(int num);
   int mag_to_level(double image_mag, int *level);
   int get_overview_info(CString filename, int *width, int *height, int *highest_level, CString & error_msg);




   void init_geodata();

   // check for geodata
   void check_geodata( );

   // convert linear units to meters
   int convert_linear_units( unsigned short linear_units,
                             double linear_unit_size, double &value );

   // convert angular units to degrees
   int convert_angular_units( unsigned short angular_units,
                              double angular_unit_size, double &value );

   // utm inverse transformation
   int inv_transform_utm( int hpix, int vpix, double &latitude,
                          double &longitude );
   // utm forward transformation
   int fwd_transform_utm( double latitude, double longitude, int &hpix,
                          int &vpix );

   // state plane inverse transformation
   int inv_transform_sp( int hpix, int vpix, double &latitude, double &longitude );

   // state plane forward transformation
   int fwd_transform_sp( double latitude, double longitude,
                                 int &hpix, int &vpix  );

   // lat/long inverse transformation
   int inv_transform_lat_long( int hpix, int vpix, double &latitude,
                               double &longitude );
   // lat/long forward transformation
   int fwd_transform_lat_long( double latitude, double longitude, int &hpix,
                               int &vpix );

   // lambert conformal conic inverse transformation
   int inv_transform_lambert_conf_conic( int hpix, int vpix,
                                         double &latitude, double &longitude );
   // lambert conformal conic forward transformation
   int fwd_transform_lambert_conf_conic( double latitude, double longitude,
                                         int &hpix, int &vpix );
   // albers equal area inverse transformation
   int inv_transform_albers_equal_area( int hpix, int vpix,
                                        double &latitude, double &longitude );
   // albers eual area forward transformation
   int fwd_transform_albers_equal_area( double latitude, double longitude,
                                        int &hpix, int &vpix );

   // Tiff Write Functions
   int write_geotiff_file(  CTiffData *data, CString &error_msg );
   int write_uncompressed_geotiff_file_from_tga( CTiffData *data, CString &error_msg );
   int write_packbits_geotiff_file_from_tga( CTiffData *data, CString &error_msg );
   int write_jpeg_geotiff_file_from_tga( CTiffData *data, CString &error_msg );
   int compress_packbits( int num_uncompressed_bytes, unsigned char* uncompressed_bytes,
                   int max_compressed_bytes, int &num_compressed_bytes, unsigned char* compressed_bytes );
   int get_packbits_compressed_size( int num_uncompressed_bytes, unsigned char* uncompressed_bytes );
   void write_packbits_stripped_256_color_palette_geotiff_file( CTiffData *data );

   void write_packbits_stripped_24bit_rgb_geotiff_file( CTiffData *data );
   int write_tiled_jpeg_geotiff_file( CTiffData *data, CString &error_msg );
   int get_temp_jpeg_size(int *jpeg_size, CString & error_msg);



   int create_multi_image_tiff_overview(int *err_code, CString &err_msg, IImageLibCallback *callback);

   BOOL GetGeoTiffReferenced() { return m_bCGeoTiffReferenced; }
   VOID SetGeoTiffReferenced( BOOL bReferenced ) { m_bCGeoTiffReferenced = bReferenced; }

   VOID SetCallingImageMap( CImageMap* pImageMap ) { m_pCallingImageMap = pImageMap; };

   VOID SetExpandedLastView( DOUBLE dULLat, DOUBLE dULLon, DOUBLE dLRLat, DOUBLE dLRLon );
   VOID GetExpandedBounds(
         DOUBLE& dViewULLat, DOUBLE& dViewULLon,
         DOUBLE& dViewLRLat, DOUBLE& dViewLRLon,
         DOUBLE& dImageULLat, DOUBLE& dImageULLon,
         DOUBLE& dImageLRLat, DOUBLE& dImageLRLon );

   // File open
   INT gtfOpen( LPCSTR pszFilename );

   // File seek
   INT gtfSeek( fpos_t qwFilePosition );

   // General file read
   INT gtfRead( DWORD dwFilePosition, PBYTE pbBuffer, DWORD dwByteCount );

   // File read at current position
   INT gtfRead( PBYTE pbBuffer, DWORD dwByteCount );

   // File close
   VOID gtfClose() { clear(); }

protected:
#if defined MAPPEDFILE_FILE_INPUT || defined READFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
   FILE     m_iobMappedFileIOBufDesc;  // Pseudo-FILE
#else    // No MAPPEDFILE_FILE_INPUT, LLFILE_FILE_INPUT, or READFILE_FILE_INPUT
   fpos_t   m_qwFilePosition;
#endif

   INT      GetTagStringValue( USHORT wTagID, CString& csValue );
   INT      GetTagUIntValue( INT iValueIndex, USHORT wTagID, UINT& uiValue );
   INT      GetTagIndexUIntValue( INT iValueIndex, INT iTagIndex, UINT& uiValue );
   INT      GetTagIndexULongValue( INT iValueIndex, INT iTagIndex, ULONG& ulValue );

private:
   CImageMap*  m_pCallingImageMap;

   INT      GetTagTileValues( INT iTile, UINT& uiTileOffset, UINT& cTileByteCount );

   // Data cache support
   BOOL     m_bCGeoTiffReferenced;
   DOUBLE   m_dExpandedLastView[4];    // Expanded last view used (ul lat/lon, lr lat/lon)

#if defined MAPPEDFILE_FILE_INPUT || defined READFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
   VOID     ClearFile();               // Clear special file objects
   #if defined LLFILE_FILE_INPUT
      INT      m_hGeoTiffFile;            // From _open()
   #else
      HANDLE   m_hGeoTiffFile;            // From CreateFile()
   #endif

#ifdef MAPPEDFILE_FILE_INPUT

   // File mapping
   HANDLE   m_hFileMapping;            // File mapping handle from CreateFileMapping()
#endif

#ifdef READFILE_FILE_INPUT
   OVERLAPPED  m_ovlpOverlapped;       // For overlapped file access
#endif

#else
#  define ClearFile()
#endif   // def MAPPEDFILE_FILE_INPUT || READFILE_FILE_INPUT || LLFILE_FILE_INPUT
}; // class CGeoTiff


// byte order constants
#define GEOTIFF_BYTE_ORDER_NOT_DEFINED 0       // not defined
#define GEOTIFF_LITTLE_ENDIAN 1                // INTEL convention
#define GEOTIFF_BIG_ENDIAN 2                   // Motorola convention

// data type constants
#define GEOTIFF_BYTE 1              // 8-bit unsigned integer
#define GEOTIFF_ASCII 2             // null-terminated string
#define GEOTIFF_SHORT 3             // unsigned short (16-bit) integer
#define GEOTIFF_LONG 4              // unsigned long (32-bit) integer
#define GEOTIFF_RATIONAL 5          // num/denom pair of unsigned longs
#define GEOTIFF_SBYTE 6             // 8-bit signed integer
#define GEOTIFF_UNDEFINED_TYPE 7    // 8-bit undefined data type
#define GEOTIFF_SSHORT 8            // signed short (16-bit) integer
#define GEOTIFF_SLONG 9             // signed long (32-bit) integer
#define GEOTIFF_SRATIONAL 10        // num/denom pair of signed longs
#define GEOTIFF_FLOAT 11            // 4-byte float value
#define GEOTIFF_DOUBLE 12           // 8-byte double value

// tag id constants
#define GEOTIFF_NEW_SUBFILE_TYPE_TAG 254
#define GEOTIFF_SUBFILE_TYPE_TAG 255
#define GEOTIFF_IMAGE_WIDTH_TAG 256
#define GEOTIFF_IMAGE_LENGTH_TAG 257
#define GEOTIFF_BITS_PER_SAMPLE_TAG 258
#define GEOTIFF_COMPRESSION_TAG 259
#define GEOTIFF_PHOTOMETRIC_INTERPRETATION_TAG 262
#define GEOTIFF_THRESHHOLDING_TAG 263
#define GEOTIFF_CELL_WIDTH_TAG 264
#define GEOTIFF_CELL_LENGTH_TAG 265
#define GEOTIFF_FILL_ORDER_TAG 266
#define GEOTIFF_DOCUMENT_NAME_TAG 269
#define GEOTIFF_IMAGE_DESCRIPTION_TAG 270
#define GEOTIFF_MAKE_TAG 271
#define GEOTIFF_MODEL_TAG 272
#define GEOTIFF_STRIP_OFFSETS_TAG 273
#define GEOTIFF_ORIENTATION_TAG 274
#define GEOTIFF_SAMPLES_PER_PIXEL_TAG 277
#define GEOTIFF_ROWS_PER_STRIP_TAG 278
#define GEOTIFF_STRIP_BYTE_COUNTS_TAG 279
#define GEOTIFF_MIN_SAMPLE_VALUE_TAG 280
#define GEOTIFF_MAX_SAMPLE_VALUE_TAG 281
#define GEOTIFF_X_RESOLUTION_TAG 282
#define GEOTIFF_Y_RESOLUTION_TAG 283
#define GEOTIFF_PLANAR_CONFIGURATION_TAG 284
#define GEOTIFF_PAGE_NAME_TAG 285
#define GEOTIFF_X_POSITION_TAG 286
#define GEOTIFF_Y_POSITION_TAG 287
#define GEOTIFF_FREE_OFFSETS_TAG 288
#define GEOTIFF_FREE_BYTE_COUNTS_TAG 289
#define GEOTIFF_GRAY_RESPONSE_UNIT_TAG 290
#define GEOTIFF_GRAY_RESPONSE_CURVE_TAG 291
#define GEOTIFF_T4_OPTIONS_TAG 292
#define GEOTIFF_T6_OPTIONS_TAG 293
#define GEOTIFF_RESOLUTION_UNIT_TAG 296
#define GEOTIFF_PAGE_NUMBER_TAG 297
#define GEOTIFF_TRANSFER_FUNCTION_TAG 301
#define GEOTIFF_SOFTWARE_TAG 305
#define GEOTIFF_DATE_TIME_TAG 306
#define GEOTIFF_ARTIST_TAG 315
#define GEOTIFF_HOST_COMPUTER_TAG 316
#define GEOTIFF_PREDICTOR_TAG 317
#define GEOTIFF_WHITE_POINT_TAG 318
#define GEOTIFF_PRIMARY_CHROMATICITIES_TAG 319
#define GEOTIFF_COLOR_MAP_TAG 320
#define GEOTIFF_HALFTONE_HINTS_TAG 321
#define GEOTIFF_TILE_WIDTH_TAG 322
#define GEOTIFF_TILE_LENGTH_TAG 323
#define GEOTIFF_TILE_OFFSETS_TAG 324
#define GEOTIFF_TILE_BYTE_COUNTS_TAG 325
#define GEOTIFF_INK_SET_TAG 332
#define GEOTIFF_INK_NAMES_TAG 333
#define GEOTIFF_NUMBER_OF_INKS_TAG 334
#define GEOTIFF_DOT_RANGE_TAG 336
#define GEOTIFF_TARGET_PRINTER_TAG 337
#define GEOTIFF_EXTRA_SAMPLES_TAG 338
#define GEOTIFF_SAMPLE_FORMAT_TAG 339
#define GEOTIFF_S_MIN_SAMPLE_VALUE_TAG 340
#define GEOTIFF_S_MAX_SAMPLE_VALUE_TAG 341
#define GEOTIFF_TRANSFER_RANGE_TAG 342
#define GEOTIFF_JPEG_TABLES_TAG 347
#define GEOTIFF_JPEG_PROC_TAG 512
#define GEOTIFF_JPEG_INTERCHANGE_FORMAT_TAG 513
#define GEOTIFF_JPEG_INTERCHANGE_FORMAT_LNGTH_TAG 514
#define GEOTIFF_JPEG_RESTART_INTERVAL_TAG 515
#define GEOTIFF_JPEG_LOSSLESS_PREDICTORS_TAG 517
#define GEOTIFF_JPEG_POINT_TRANSFORMS_TAG 518
#define GEOTIFF_JPEG_Q_TABLES_TAG 519
#define GEOTIFF_JPEG_DC_TABLES_TAG 520
#define GEOTIFF_JPEC_AC_TABLES_TAG 521
#define GEOTIFF_YCBCR_COEFFICIENTS_TAG 529
#define GEOTIFF_YCBCR_SUB_SAMPLING_TAG 530
#define GEOTIFF_YCBCR_POSITIONING_TAG 531
#define GEOTIFF_REFERENCE_BLACK_WHITE_TAG 532
#define GEOTIFF_XML_PACKET_TAG 700
#define GEOTIFF_COPYRIGHT_TAG 33432
#define GEOTIFF_IPTC_NAA_CHUNK 33723
#define GEOTIFF_PHOTOSHOP_CHUNK 34377
#define GEOTIFF_EXIF_IFD 34665

// compression scheme constants
#define GEOTIFF_NO_COMPRESSION 1
#define GEOTIFF_CCITT_1D 2
#define GEOTIFF_GROUP_3_FAX 3
#define GEOTIFF_GROUP_4_FAX 4
#define GEOTIFF_LZW 5
#define GEOTIFF_JPEG 7
#define GEOTIFF_PACKBITS 32773

// photometric interpretation constants
#define GEOTIFF_WHITE_IS_ZERO 0
#define GEOTIFF_BLACK_IS_ZERO 1
#define GEOTIFF_RGB 2
#define GEOTIFF_RGB_PALETTE 3
#define GEOTIFF_TRANSPARENCY_MASK 4
#define GEOTIFF_CMYK 5
#define GEOTIFF_YCBCR 6
#define GEOTIFF_CIE_LAB 8

// resolution_units
#define GEOTIFF_RESOLUTION_UNIT_NONE 1
#define GEOTIFF_RESOLUTION_UNIT_INCH 2
#define GEOTIFF_RESOLUTION_UNIT_CENTIMETER 3

// planar configurations
#define GEOTIFF_UNKNOWN_FORMAT 0
#define GEOTIFF_CHUNKY_FORMAT 1
#define GEOTIFF_PLANAR_FORMAT 2

// supported image types
#define GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED 0
#define GEOTIFF_IMAGE_TYPE_256_GRAYSCALE 1
#define GEOTIFF_IMAGE_TYPE_256_COLOR 2
#define GEOTIFF_IMAGE_TYPE_24BIT_COLOR 3
#define GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE 4
#define GEOTIFF_IMAGE_TYPE_JPEG 5
#define GEOTIFF_IMAGE_TYPE_1BIT 6
#define GEOTIFF_IMAGE_TYPE_32BIT_FLOAT 7
#define GEOTIFF_IMAGE_TYPE_48BIT_COLOR 8

// geodata error codes
#define GEOTIFF_GEODATA_OK 1
#define GEOTIFF_GEODATA_NOT_PRESENT 2
#define GEOTIFF_GEODATA_ERROR 3

// special values
#define GEOTIFF_UNDEFINED 0
#define GEOTIFF_USER_DEFINED 32767

// geotiff tag id constants
#define GEOTIFF_MODEL_PIXEL_SCALE_TAG 33550
#define GEOTIFF_MODEL_TRANSFORMATION_TAG 34264
#define GEOTIFF_MODEL_TIEPOINT_TAG 33922
#define GEOTIFF_GEOKEY_DIRECTORY_TAG 34735
#define GEOTIFF_GEO_DOUBLE_PARAMS_TAG 34736
#define GEOTIFF_GEO_ASCII_PARAMS_TAG 34737
#define GEOTIFF_INTERGRAPH_MATRIX_TAG 33920

// geokey id constants
#define GEOTIFF_GT_MODEL_TYPE_GEOKEY 1024
#define GEOTIFF_GT_RASTER_TYPE_GEOKEY 1025
#define GEOTIFF_GT_CITATION_GEOKEY 1026

#define GEOTIFF_GEOGRAPHIC_TYPE_GEOKEY 2048
#define GEOTIFF_GEOG_CITATION_GEOKEY 2049
#define GEOTIFF_GEOG_GEODETIC_DATUM_GEOKEY 2050
#define GEOTIFF_GEOG_PRIME_MERIDIAN_GEOKEY 2051
#define GEOTIFF_GEOG_LINEAR_UNITS_GEOKEY 2052
#define GEOTIFF_GEOG_LINEAR_UNIT_SIZE_GEOKEY 2053
#define GEOTIFF_GEOG_ANGULAR_UNITS_GEOKEY 2054
#define GEOTIFF_GEOG_ANGULAR_UNIT_SIZE_GEOKEY 2055
#define GEOTIFF_GEOG_ELLIPSOID_GEOKEY 2056
#define GEOTIFF_GEOG_SEMI_MAJOR_AXIS_GEOKEY 2057
#define GEOTIFF_GEOG_SEMI_MINOR_AXIS_GEOKEY 2058
#define GEOTIFF_GEOG_INV_FLATTENING_GEOKEY 2059
#define GEOTIFF_GEOG_AZIMUTH_UNITS_GEOKEY 2060
#define GEOTIFF_GEOG_PRIME_MERIDIAN_LONG_GEOKEY 2061

#define GEOTIFF_PROJECTED_CS_TYPE_GEOKEY 3072
#define GEOTIFF_PCS_CITATION_GEOKEY 3073
#define GEOTIFF_PROJECTION_GEOKEY 3074
#define GEOTIFF_PROJ_COORD_TRANS_GEOKEY 3075
#define GEOTIFF_PROJ_LINEAR_UNITS_GEOKEY 3076
#define GEOTIFF_PROJ_LINEAR_UNIT_SIZE_GEOKEY 3077
#define GEOTIFF_PROJ_STD_PARALLEL_1_GEOKEY 3078
#define GEOTIFF_PROJ_STD_PARALLEL_2_GEOKEY 3079
#define GEOTIFF_PROJ_NAT_ORIGIN_LONG_GEOKEY 3080
#define GEOTIFF_PROJ_NAT_ORIGIN_LAT_GEOKEY 3081
#define GEOTIFF_PROJ_FALSE_EASTING_GEOKEY 3082
#define GEOTIFF_PROJ_FALSE_NORTHING_GEOKEY 3083
#define GEOTIFF_PROJ_FALSE_ORIGIN_LONG_GEOKEY 3084
#define GEOTIFF_PROJ_FALSE_ORIGIN_LAT_GEOKEY 3085
#define GEOTIFF_PROJ_FALSE_ORIGIN_EASTING_GEOKEY 3086
#define GEOTIFF_PROJ_FALSE_ORIGIN_NORTHING_GEOKEY 3087
#define GEOTIFF_PROJ_CENTER_LONG_GEOKEY 3088
#define GEOTIFF_PROJ_CENTER_LAT_GEOKEY 3089
#define GEOTIFF_PROJ_CENTER_EASTING_GEOKEY 3090
#define GEOTIFF_PROJ_CENTER_NORTHING_GEOKEY 3091
#define GEOTIFF_PROJ_SCALE_AT_NAT_ORIGIN_GEOKEY 3092
#define GEOTIFF_PROJ_SCALE_AT_CENTER_GEOKEY 3093
#define GEOTIFF_PROJ_AZIMUTH_ANGLE_GEOKEY 3094
#define GEOTIFF_PROJ_STRAIGHT_VERT_POLE_LONG_GEOKEY 3095

#define GEOTIFF_VERTICAL_CS_TYPE_GEOKEY 4096
#define GEOTIFF_VERTICAL_CITATION_GEOKEY 4097
#define GEOTIFF_VERTICAL_DATUM_GEOKEY 4098
#define GEOTIFF_VERTICAL_UNITS_GEOKEY 4099

// model type geokey constants
#define GEOTIFF_MODEL_TYPE_PROJECTED 1
#define GEOTIFF_MODEL_TYPE_GEOGRAPHIC 2
#define GEOTIFF_MODEL_TYPE_GEOCENTRIC 3

// raster type geokey constants
#define GEOTIFF_RASTER_PIXEL_IS_AREA 1
#define GEOTIFF_RASTER_PIXEL_IS_POINT 2

// linear units geokey constants
#define GEOTIFF_LINEAR_METER 9001
#define GEOTIFF_LINEAR_FOOT 9002
#define GEOTIFF_LINEAR_FOOT_US_SURVEY 9003
#define GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN 9004
#define GEOTIFF_LINEAR_FOOT_CLARKE 9005
#define GEOTIFF_LINEAR_FOOT_INDIAN 9006
#define GEOTIFF_LINEAR_LINK 9007
#define GEOTIFF_LINEAR_LINK_BENOIT 9008
#define GEOTIFF_LINEAR_LINK_SEARS 9009
#define GEOTIFF_LINEAR_CHAIN_BENOIT 9010
#define GEOTIFF_LINEAR_CHAIN_SEARS 9011
#define GEOTIFF_LINEAR_YARD_SEARS 9012
#define GEOTIFF_LINEAR_YARD_INDIAN 9013
#define GEOTIFF_LINEAR_FATHOM 9014
#define GEOTIFF_LINEAR_MILE_INTERNATIONAL_NAUTICAL 9015

// angular units geokey constants
#define GEOTIFF_ANGULAR_RADIAN 9101
#define GEOTIFF_ANGULAR_DEGREE 9102
#define GEOTIFF_ANGULAR_ARC_MINUTE 9103
#define GEOTIFF_ANGULAR_ARC_SECOND 9104
#define GEOTIFF_ANGULAR_GRAD 9105
#define GEOTIFF_ANGULAR_GON 9106
#define GEOTIFF_ANGULAR_DMS 9107
#define GEOTIFF_ANGULAR_DMS_HEMISPHERE 9108

// geographic coordinate system types
#define GEOTIFF_GCSE_AIRY_1830 4001
#define GEOTIFF_GCSE_AIRY_MODIFIED_1849 4002
#define GEOTIFF_GCSE_AUSTRALIAN_NATIONAL_SPHEROID 4003
#define GEOTIFF_GCSE_BESSEL_1841 4004
#define GEOTIFF_GCSE_BESSEL_MODIFIED 4005
#define GEOTIFF_GCSE_BESSEL_NAMIBIA 4006
#define GEOTIFF_GCSE_CLARKE_1858 4007
#define GEOTIFF_GCSE_CLARKE_1866 4008
#define GEOTIFF_GCSE_CLARKE_1866_MICHIGAN 4009
#define GEOTIFF_GCSE_CLARKE_1880_BENOIT 4010
#define GEOTIFF_GCSE_CLARKE_1880_IGN 4011
#define GEOTIFF_GCSE_CLARKE_1880_RGS 4012
#define GEOTIFF_GCSE_CLARKE_1880_ARC 4013
#define GEOTIFF_GCSE_CLARKE_1880_SGA_1922 4014
#define GEOTIFF_GCSE_EVEREST_1830_1937_ADJUSTMENT 4015
#define GEOTIFF_GCSE_EVEREST_1830_1967_DEFINITION 4016
#define GEOTIFF_GCSE_EVEREST_1830_1975_DEFINITION 4017
#define GEOTIFF_GCSE_EVEREST_1830_MODIFIED 4018
#define GEOTIFF_GCSE_GRS_1980 4019
#define GEOTIFF_GCSE_HELMERT_1906 4020
#define GEOTIFF_GCSE_INDONESIAN_NATIONAL_SPHEROID 4021
#define GEOTIFF_GCSE_INTERNATIONAL_1924 4022
#define GEOTIFF_GCSE_INTERNATIONAL_1967 4023
#define GEOTIFF_GCSE_KRASSOWSKY_1940 4024
#define GEOTIFF_GCSE_NWL9D 4025
#define GEOTIFF_GCSE_NWL10D 4026
#define GEOTIFF_GCSE_PLESSIS_1817 4027
#define GEOTIFF_GCSE_STRUVE_1860 4028
#define GEOTIFF_GCSE_WAR_OFFICE 4029
#define GEOTIFF_GCSE_WGS84 4030
#define GEOTIFF_GCSE_GEM10C 4031
#define GEOTIFF_GCSE_OSU86F 4032
#define GEOTIFF_GCSE_OSU91A 4033
#define GEOTIFF_GCSE_CLARKE_1880 4034
#define GEOTIFF_GCSE_SPHERE 4035
#define GEOTIFF_GCS_ADINDAN 4201
#define GEOTIFF_GCS_AGD66 4202
#define GEOTIFF_GCS_AGD84 4203
#define GEOTIFF_GCS_AIN_EL_ABD 4204
#define GEOTIFF_GCS_AFGOOYE 4205
#define GEOTIFF_GCS_AGADEZ 4206
#define GEOTIFF_GCS_LISBON 4207
#define GEOTIFF_GCS_ARATU 4208
#define GEOTIFF_GCS_ARC_1950 4209
#define GEOTIFF_GCS_ARC_1960 4210
#define GEOTIFF_GCS_BATAVIA 4211
#define GEOTIFF_GCS_BARBADOS 4212
#define GEOTIFF_GCS_BEDUARAM 4213
#define GEOTIFF_GCS_BEIJING_1954 4214
#define GEOTIFF_GCS_BELGE_1950 4215
#define GEOTIFF_GCS_BERMUDA_1957 4216
#define GEOTIFF_GCS_BERN_1898 4217
#define GEOTIFF_GCS_BOGOTA 4218
#define GEOTIFF_GCS_BUKIT_RIMPAH 4219
#define GEOTIFF_GCS_CAMACUPA 4220
#define GEOTIFF_GCS_CAMPO_INCHAUSPE 4221
#define GEOTIFF_GCS_CAPE 4222
#define GEOTIFF_GCS_CARTHAGE 4223
#define GEOTIFF_GCS_CHUA 4224
#define GEOTIFF_GCS_CORREGO_ALEGRE 4225
#define GEOTIFF_GCS_COTE_D_IVOIRE 4226
#define GEOTIFF_GCS_DEIR_EZ_ZOR 4227
#define GEOTIFF_GCS_DOUALA 4228
#define GEOTIFF_GCS_EGYPT_1907 4229
#define GEOTIFF_GCS_ED50 4230
#define GEOTIFF_GCS_ED87 4231
#define GEOTIFF_GCS_FAHUD 4232
#define GEOTIFF_GCS_GANDAJIKA_1970 4233
#define GEOTIFF_GCS_GAROUA 4234
#define GEOTIFF_GCS_GUYANE_FRANCAISE 4235
#define GEOTIFF_GCS_HU_TZU_SHAN 4236
#define GEOTIFF_GCS_HD72 4237
#define GEOTIFF_GCS_ID74 4238
#define GEOTIFF_GCS_INDIAN_1954 4239
#define GEOTIFF_GCS_INDIAN_1975 4240
#define GEOTIFF_GCS_JAMAICA_1875 4241
#define GEOTIFF_GCS_JAD69 4242
#define GEOTIFF_GCS_KALIANPUR 4243
#define GEOTIFF_GCS_KANDAWALA 4244
#define GEOTIFF_GCS_KERTAU 4245
#define GEOTIFF_GCS_KOC 4246
#define GEOTIFF_GCS_LA_CANOA 4247
#define GEOTIFF_GCS_PSAD56 4248
#define GEOTIFF_GCS_LAKE 4249
#define GEOTIFF_GCS_LEIGON 4250
#define GEOTIFF_GCS_LIBERIA_1964 4251
#define GEOTIFF_GCS_LOME 4252
#define GEOTIFF_GCS_LUZON_1911 4253
#define GEOTIFF_GCS_HITO_XVIII_1963 4254
#define GEOTIFF_GCS_HERAT_NORTH 4255
#define GEOTIFF_GCS_MAHE_1971 4256
#define GEOTIFF_GCS_MAKASSAR 4257
#define GEOTIFF_GCS_EUREF89 4258
#define GEOTIFF_GCS_MALONGO_1987 4259
#define GEOTIFF_GCS_MANOCA 4260
#define GEOTIFF_GCS_MERCHICH 4261
#define GEOTIFF_GCS_MASSAWA 4262
#define GEOTIFF_GCS_MINNA 4263
#define GEOTIFF_GCS_MHAST 4264
#define GEOTIFF_GCS_MONTE_MARIO 4265
#define GEOTIFF_GCS_M_PORALOKO 4266
#define GEOTIFF_GCS_NAD27 4267
#define GEOTIFF_GCS_NAD_MICHIGAN 4268
#define GEOTIFF_GCS_NAD83 4269
#define GEOTIFF_GCS_NAHRWAN_1967 4270
#define GEOTIFF_GCS_NAPARIMA_1972 4271
#define GEOTIFF_GCS_GD49 4272
#define GEOTIFF_GCS_NGO_1948 4273
#define GEOTIFF_GCS_DATUM_73 4274
#define GEOTIFF_GCS_NTF 4275
#define GEOTIFF_GCS_NSWC_9Z_2 4276
#define GEOTIFF_GCS_OSGB_1936 4277
#define GEOTIFF_GCS_OSGB70 4278
#define GEOTIFF_GCS_OS_SN80 4279
#define GEOTIFF_GCS_PADANG 4280
#define GEOTIFF_GCS_PALESTINE_1923 4281
#define GEOTIFF_GCS_POINTE_NOIRE 4282
#define GEOTIFF_GCS_GDA94 4283
#define GEOTIFF_GCS_PULKOVO_1942 4284
#define GEOTIFF_GCS_QATAR 4285
#define GEOTIFF_GCS_QATAR_1948 4286
#define GEOTIFF_GCS_QORNOQ 4287
#define GEOTIFF_GCS_LOMA_QUINTANA 4288
#define GEOTIFF_GCS_AMERSFOORT 4289
#define GEOTIFF_GCS_RT38 4290
#define GEOTIFF_GCS_SAD69 4291
#define GEOTIFF_GCS_SAPPER_HILL_1943 4292
#define GEOTIFF_GCS_SCHWARZECK 4293
#define GEOTIFF_GCS_SEGORA 4294
#define GEOTIFF_GCS_SERINDUNG 4295
#define GEOTIFF_GCS_SUDAN 4296
#define GEOTIFF_GCS_TANANARIVE 4297
#define GEOTIFF_GCS_TIMBALAI_1948 4298
#define GEOTIFF_GCS_TM65 4299
#define GEOTIFF_GCS_TM75 4300
#define GEOTIFF_GCS_TOKYO 4301
#define GEOTIFF_GCS_TRINIDAD_1903 4302
#define GEOTIFF_GCS_TC_1948 4303
#define GEOTIFF_GCS_VOIROL_1875 4304
#define GEOTIFF_GCS_VOIROL_UNIFIE 4305
#define GEOTIFF_GCS_BERN_1938 4306
#define GEOTIFF_GCS_NORD_SAHARA_1959 4307
#define GEOTIFF_GCS_STOCKHOLM_1938 4308
#define GEOTIFF_GCS_YACARE 4309
#define GEOTIFF_GCS_YOFF 4310
#define GEOTIFF_GCS_ZANDERIJ 4311
#define GEOTIFF_GCS_MGI 4312
#define GEOTIFF_GCS_BELGE_1972 4313
#define GEOTIFF_GCS_DHDN 4314
#define GEOTIFF_GCS_CONAKRY_1905 4315
#define GEOTIFF_GCS_WGS_72 4322
#define GEOTIFF_GCS_WGS_72BE 4324
#define GEOTIFF_GCS_WGS_84 4326
#define GEOTIFF_GCS_BERN_1898_BERN 4801
#define GEOTIFF_GCS_BOGOTA_BOGOTA 4802
#define GEOTIFF_GCS_LISBON_LISBON 4803
#define GEOTIFF_GCS_MAKASSAR_JAKARTA 4804
#define GEOTIFF_GCS_MGI_FERRO 4805
#define GEOTIFF_GCS_MONTE_MARIO_ROME 4806
#define GEOTIFF_GCS_NTF_PARIS 4807
#define GEOTIFF_GCS_PADANG_JAKARTA 4808
#define GEOTIFF_GCS_BELGE_1950_BRUSSELS 4809
#define GEOTIFF_GCS_TANANARIVE_PARIS 4810
#define GEOTIFF_GCS_VOIROL_1875_PARIS 4811
#define GEOTIFF_GCS_VOIROL_UNIFIE_PARIS 4812
#define GEOTIFF_GCS_BATAVIA_JAKARTA 4813
#define GEOTIFF_GCS_ATF_PARIS 4901
#define GEOTIFF_GCS_NDG_PARIS 4902

// geodetic datums
#define GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1927 6267
#define GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1983 6269
#define GEOTIFF_DATUM_WGS72 6322
#define GEOTIFF_DATUM_WGS84 6326
#define GEOTIFF_DATUME_AIRY_1830 6001
#define GEOTIFF_DATUME_AIRY_MODIFIED1849 6002
#define GEOTIFF_DATUME_AUSTRALIAN_NATIONAL_SPHEROID 6003
#define GEOTIFF_DATUME_BESSEL_1841 6004
#define GEOTIFF_DATUME_BESSEL_MODIFIED 6005
#define GEOTIFF_DATUME_BESSEL_NAMIBIA 6006
#define GEOTIFF_DATUME_CLARKE_1858 6007
#define GEOTIFF_DATUME_CLARKE_1866 6008
#define GEOTIFF_DATUME_CLARKE_1866_MICHIGAN 6009
#define GEOTIFF_DATUME_CLARKE_1880_BENOIT 6010
#define GEOTIFF_DATUME_CLARKE_1880_IGN 6011
#define GEOTIFF_DATUME_CLARKE_1880_RGS 6012
#define GEOTIFF_DATUME_CLARKE_1880_ARC 6013
#define GEOTIFF_DATUME_CLARKE_1880_SGA_1922 6014
#define GEOTIFF_DATUME_EVEREST_1830_1937_ADJUSTMENT 6015
#define GEOTIFF_DATUME_EVEREST_1830_1967_DEFINITION 6016
#define GEOTIFF_DATUME_EVEREST1830_1975_DEFINITION 6017
#define GEOTIFF_DATUME_EVEREST_1830_MODIFIED 6018
#define GEOTIFF_DATUME_GRS_1980 6019
#define GEOTIFF_DATUME_HELMERT_1906 6020
#define GEOTIFF_DATUME_INDONESIAN_NATIONAL_SPHEROID 6021
#define GEOTIFF_DATUME_INTERNATIONAL_1924 6022
#define GEOTIFF_DATUME_INTERNATIONAL_1967 6023
#define GEOTIFF_DATUME_KRASSOWSKY_1960 6024
#define GEOTIFF_DATUME_NWL9D 6025
#define GEOTIFF_DATUME_NWL10D 6026
#define GEOTIFF_DATUME_PLESSIS_1817 6027
#define GEOTIFF_DATUME_STRUVE_1860 6028
#define GEOTIFF_DATUME_WAR_OFFICE 6029
#define GEOTIFF_DATUME_WGS_84 6030
#define GEOTIFF_DATUME_GEM10C 6031
#define GEOTIFF_DATUME_OSU86F 6032
#define GEOTIFF_DATUME_OSU91A 6033
#define GEOTIFF_DATUME_CLARKE_1880 6034
#define GEOTIFF_DATUME_SPHERE 6035
#define GEOTIFF_DATUM_ADINDAN 6201
#define GEOTIFF_DATUM_AUSTRALIAN_GEODETIC_DATUM_1966 6202
#define GEOTIFF_DATUM_AUSTRALIAN_GEODETIC_DATUM_1984 6203
#define GEOTIFF_DATUM_AIN_EL_ABD_1970 6204
#define GEOTIFF_DATUM_AFGOOYE 6205
#define GEOTIFF_DATUM_AGADEZ 6206
#define GEOTIFF_DATUM_LISBON 6207
#define GEOTIFF_DATUM_ARATU 6208
#define GEOTIFF_DATUM_ARC_1950 6209
#define GEOTIFF_DATUM_ARC_1960 6210
#define GEOTIFF_DATUM_BATAVIA 6211
#define GEOTIFF_DATUM_BARBADOS 6212
#define GEOTIFF_DATUM_BEDUARAM 6213
#define GEOTIFF_DATUM_BEIJING_1954 6214
#define GEOTIFF_DATUM_RESEAU_NATIONAL_BELGE_1950 6215
#define GEOTIFF_DATUM_BERMUDA_1957 6216
#define GEOTIFF_DATUM_BERN_1898 6217
#define GEOTIFF_DATUM_BOGOTA 6218
#define GEOTIFF_DATUM_BUKIT_RIMPAH 6219
#define GEOTIFF_DATUM_CAMACUPA 6220
#define GEOTIFF_DATUM_CAMPO_INCHAUSPE 6221
#define GEOTIFF_DATUM_CAPE 6222
#define GEOTIFF_DATUM_CARTHAGE 6223
#define GEOTIFF_DATUM_CHUA 6224
#define GEOTIFF_DATUM_CORREGO_ALEGRE 6225
#define GEOTIFF_DATUM_COTE_D_IVOIRE 6226
#define GEOTIFF_DATUM_DEIR_EZ_ZOR 6227
#define GEOTIFF_DATUM_DOUALA 6228
#define GEOTIFF_DATUM_EGYPT_1907 6229
#define GEOTIFF_DATUM_EUROPEAN_DATUM_1950 6230
#define GEOTIFF_DATUM_EUROPEAN_DATUM_1987 6231
#define GEOTIFF_DATUM_FAHUD 6232
#define GEOTIFF_DATUM_GANDAJIKA_1970 6233
#define GEOTIFF_DATUM_GAROUA 6234
#define GEOTIFF_DATUM_GUYANE_FRANCAISE 6235
#define GEOTIFF_DATUM_HU_TZU_SHAN 6236
#define GEOTIFF_DATUM_HUNGARIAN_DATUM_1972 6237
#define GEOTIFF_DATUM_INDONESIAN_DATUM_1974 6238
#define GEOTIFF_DATUM_INDIAN_1954 6239
#define GEOTIFF_DATUM_INDIAN_1975 6240
#define GEOTIFF_DATUM_JAMAICA_1875 6241
#define GEOTIFF_DATUM_JAMAICA_1969 6242
#define GEOTIFF_DATUM_KALIANPUR 6243
#define GEOTIFF_DATUM_KANDAWALA 6244
#define GEOTIFF_DATUM_KERTAU 6245
#define GEOTIFF_DATUM_KUWAIT_OIL_COMPANY 6246
#define GEOTIFF_DATUM_LA_CANOA 6247
#define GEOTIFF_DATUM_PROVISIONAL_S_AMERICAN_DATUM_1956 6248
#define GEOTIFF_DATUM_LAKE 6249
#define GEOTIFF_DATUM_LEIGON 6250
#define GEOTIFF_DATUM_LIBERIA_1964 6251
#define GEOTIFF_DATUM_LOME 6252
#define GEOTIFF_DATUM_LUZON_1911 6253
#define GEOTIFF_DATUM_HITO_XVIII_1963 6254
#define GEOTIFF_DATUM_HERAT_NORTH 6255
#define GEOTIFF_DATUM_MAHE_1971 6256
#define GEOTIFF_DATUM_MAKASSAR 6257
#define GEOTIFF_DATUM_EUROPEAN_REFERENCE_SYSTEM_1989 6258
#define GEOTIFF_DATUM_MALONGO_1987 6259
#define GEOTIFF_DATUM_MANOCA 6260
#define GEOTIFF_DATUM_MERCHICH 6261
#define GEOTIFF_DATUM_MASSAWA 6262
#define GEOTIFF_DATUM_MINNA 6263
#define GEOTIFF_DATUM_MHAST 6264
#define GEOTIFF_DATUM_MONTE_MARIO 6265
#define GEOTIFF_DATUM_M_PORALOKO 6266
#define GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1927 6267
#define GEOTIFF_DATUM_NAD_MICHIGAN 6268
#define GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1983 6269
#define GEOTIFF_DATUM_NAHRWAN_1967 6270
#define GEOTIFF_DATUM_NAPARIMA_1972 6271
#define GEOTIFF_DATUM_NEW_ZEALAND_GEODETIC_DATUM_1949 6272
#define GEOTIFF_DATUM_NGO_1948 6273
#define GEOTIFF_DATUM_DATUM_73 6274
#define GEOTIFF_DATUM_NOUVELLE_TRIANGULATION_FRANCAISE 6275
#define GEOTIFF_DATUM_NSWC_9Z_2 6276
#define GEOTIFF_DATUM_OSGB_1936 6277
#define GEOTIFF_DATUM_OSGB_1970_SN 6278
#define GEOTIFF_DATUM_OS_SN_1980 6279
#define GEOTIFF_DATUM_PADANG_1884 6280
#define GEOTIFF_DATUM_PALESTINE_1923 6281
#define GEOTIFF_DATUM_POINTE_NOIRE 6282
#define GEOTIFF_DATUM_GEOCENTRIC_DATUM_OF_AUSTRALIA_1994 6283
#define GEOTIFF_DATUM_PULKOVO_1942 6284
#define GEOTIFF_DATUM_QATAR 6285
#define GEOTIFF_DATUM_QATAR_1948 6286
#define GEOTIFF_DATUM_QORNOQ 6287
#define GEOTIFF_DATUM_LOMA_QUINTANA 6288
#define GEOTIFF_DATUM_AMERSFOORT 6289
#define GEOTIFF_DATUM_RT38 6290
#define GEOTIFF_DATUM_SOUTH_AMERICAN_DATUM_1969 6291
#define GEOTIFF_DATUM_SAPPER_HILL_1943 6292
#define GEOTIFF_DATUM_SCHWARZECK 6293
#define GEOTIFF_DATUM_SEGORA 6294
#define GEOTIFF_DATUM_SERINDUNG 6295
#define GEOTIFF_DATUM_SUDAN 6296
#define GEOTIFF_DATUM_TANANARIVE_1925 6297
#define GEOTIFF_DATUM_TIMBALAI_1948 6298
#define GEOTIFF_DATUM_TM65 6299
#define GEOTIFF_DATUM_TM75 6300
#define GEOTIFF_DATUM_TOKYO 6301
#define GEOTIFF_DATUM_TRINIDAD_1903 6302
#define GEOTIFF_DATUM_TRUCIAL_COAST_1948 6303
#define GEOTIFF_DATUM_VOIROL_1875 6304
#define GEOTIFF_DATUM_VOIROL_UNIFIE_1960 6305
#define GEOTIFF_DATUM_BERN_1938 6306
#define GEOTIFF_DATUM_NORD_SAHARA_1959 6307
#define GEOTIFF_DATUM_STOCKHOLM_1938 6308
#define GEOTIFF_DATUM_YACARE 6309
#define GEOTIFF_DATUM_YOFF 6310
#define GEOTIFF_DATUM_ZANDERIJ 6311
#define GEOTIFF_DATUM_MILITAR_GEOGRAPHISCHE_INSTITUT 6312
#define GEOTIFF_DATUM_RESEAU_NATIONAL_BELGE_1972 6313
#define GEOTIFF_DATUM_DEUTSCHE_HAUPTDREIECKSNETZ 6314
#define GEOTIFF_DATUM_CONAKRY_1905 6315
#define GEOTIFF_DATUM_WGS72 6322
#define GEOTIFF_DATUM_WGS72_TRANSIT_BROADCAST_EPHEMERIS 6324
#define GEOTIFF_DATUM_WGS84 6326
#define GEOTIFF_DATUM_ANCIENNE_TRIANGULATION_FRANCAISE 6901
#define GEOTIFF_DATUM_NORD_DE_GUERRE 6902

// prime meridians
#define GEOTIFF_PM_GREENWICH 8901
#define GEOTIFF_PM_LISBON 8902
#define GEOTIFF_PM_PARIS 8903
#define GEOTIFF_PM_BOGOTA 8904
#define GEOTIFF_PM_MADRID 8905
#define GEOTIFF_PM_ROME 8906
#define GEOTIFF_PM_BERN 8907
#define GEOTIFF_PM_JAKARTA 8908
#define GEOTIFF_PM_FERRO 8909
#define GEOTIFF_PM_BRUSSELS 8910
#define GEOTIFF_PM_STOCKHOLM 8911

// ellipsoids
#define GEOTIFF_ELLIPSE_AIRY_1830 7001
#define GEOTIFF_ELLIPSE_AIRY_MODIFIED_1849 7002
#define GEOTIFF_ELLIPSE_AUSTRALIAN_NATIONAL_SPHEROID 7003
#define GEOTIFF_ELLIPSE_BESSEL_1841 7004
#define GEOTIFF_ELLIPSE_BESSEL_MODIFIED 7005
#define GEOTIFF_ELLIPSE_BESSEL_NAMIBIA 7006
#define GEOTIFF_ELLIPSE_CLARKE_1858 7007
#define GEOTIFF_ELLIPSE_CLARKE_1866 7008
#define GEOTIFF_ELLIPSE_CLARKE_1866_MICHIGAN 7009
#define GEOTIFF_ELLIPSE_CLARKE_1880_BENOIT 7010
#define GEOTIFF_ELLIPSE_CLARKE_1880_IGN 7011
#define GEOTIFF_ELLIPSE_CLARKE_1880_RGS 7012
#define GEOTIFF_ELLIPSE_CLARKE_1880_ARC 7013
#define GEOTIFF_ELLIPSE_CLARKE_1880_SGA_1922 7014
#define GEOTIFF_ELLIPSE_EVEREST_1830_1937_ADJUSTMENT 7015
#define GEOTIFF_ELLIPSE_EVEREST_1830_1967_DEFINITION 7016
#define GEOTIFF_ELLIPSE_EVEREST_1830_1975_DEFINITION 7017
#define GEOTIFF_ELLIPSE_EVEREST_1830_MODIFIED 7018
#define GEOTIFF_ELLIPSE_GRS_1980 7019
#define GEOTIFF_ELLIPSE_HELMERT_1906 7020
#define GEOTIFF_ELLIPSE_INDONESIAN_NATIONAL_SPHEROID 7021
#define GEOTIFF_ELLIPSE_INTERNATIONAL_1924 7022
#define GEOTIFF_ELLIPSE_INTERNATIONAL_1967 7023
#define GEOTIFF_ELLIPSE_KRASSOWSKY_1940 7024
#define GEOTIFF_ELLIPSE_NWL_9D 7025
#define GEOTIFF_ELLIPSE_NWL_10D 7026
#define GEOTIFF_ELLIPSE_PLESSIS_1817 7027
#define GEOTIFF_ELLIPSE_STRUVE_1860 7028
#define GEOTIFF_ELLIPSE_WAR_OFFICE 7029
#define GEOTIFF_ELLIPSE_WGS_84 7030
#define GEOTIFF_ELLIPSE_GEM_10C 7031
#define GEOTIFF_ELLIPSE_OSU86F 7032
#define GEOTIFF_ELLIPSE_OSU91A 7033
#define GEOTIFF_ELLIPSE_CLARKE_1880 7034
#define GEOTIFF_ELLIPSE_SPHERE 7035

// projection coordinate systems
#define GEOTIFF_PCS_ADINDAN_UTM_ZONE_37N               20137
#define GEOTIFF_PCS_ADINDAN_UTM_ZONE_38N               20138
#define GEOTIFF_PCS_AGD66_AMG_ZONE_48                  20248
#define GEOTIFF_PCS_AGD66_AMG_ZONE_49                  20249
#define GEOTIFF_PCS_AGD66_AMG_ZONE_50                  20250
#define GEOTIFF_PCS_AGD66_AMG_ZONE_51                  20251
#define GEOTIFF_PCS_AGD66_AMG_ZONE_52                  20252
#define GEOTIFF_PCS_AGD66_AMG_ZONE_53                  20253
#define GEOTIFF_PCS_AGD66_AMG_ZONE_54                  20254
#define GEOTIFF_PCS_AGD66_AMG_ZONE_55                  20255
#define GEOTIFF_PCS_AGD66_AMG_ZONE_56                  20256
#define GEOTIFF_PCS_AGD66_AMG_ZONE_57                  20257
#define GEOTIFF_PCS_AGD66_AMG_ZONE_58                  20258
#define GEOTIFF_PCS_AGD84_AMG_ZONE_48                  20348
#define GEOTIFF_PCS_AGD84_AMG_ZONE_49                  20349
#define GEOTIFF_PCS_AGD84_AMG_ZONE_50                  20350
#define GEOTIFF_PCS_AGD84_AMG_ZONE_51                  20351
#define GEOTIFF_PCS_AGD84_AMG_ZONE_52                  20352
#define GEOTIFF_PCS_AGD84_AMG_ZONE_53                  20353
#define GEOTIFF_PCS_AGD84_AMG_ZONE_54                  20354
#define GEOTIFF_PCS_AGD84_AMG_ZONE_55                  20355
#define GEOTIFF_PCS_AGD84_AMG_ZONE_56                  20356
#define GEOTIFF_PCS_AGD84_AMG_ZONE_57                  20357
#define GEOTIFF_PCS_AGD84_AMG_ZONE_58                  20358
#define GEOTIFF_PCS_AIN_EL_ABD_UTM_ZONE_37N            20437
#define GEOTIFF_PCS_AIN_EL_ABD_UTM_ZONE_38N            20438
#define GEOTIFF_PCS_AIN_EL_ABD_UTM_ZONE_39N            20439
#define GEOTIFF_PCS_AIN_EL_ABD_BAHRAIN_GRID            20499
#define GEOTIFF_PCS_AFGOOYE_UTM_ZONE_38N               20538
#define GEOTIFF_PCS_AFGOOYE_UTM_ZONE_39N               20539
#define GEOTIFF_PCS_LISBON_PORTUGESE_GRID              20700
#define GEOTIFF_PCS_ARATU_UTM_ZONE_22S                 20822
#define GEOTIFF_PCS_ARATU_UTM_ZONE_23S                 20823
#define GEOTIFF_PCS_ARATU_UTM_ZONE_24S                 20824
#define GEOTIFF_PCS_ARC_1950_LO13                      20973
#define GEOTIFF_PCS_ARC_1950_LO15                      20975
#define GEOTIFF_PCS_ARC_1950_LO17                      20977
#define GEOTIFF_PCS_ARC_1950_LO19                      20979
#define GEOTIFF_PCS_ARC_1950_LO21                      20981
#define GEOTIFF_PCS_ARC_1950_LO23                      20983
#define GEOTIFF_PCS_ARC_1950_LO25                      20985
#define GEOTIFF_PCS_ARC_1950_LO27                      20987
#define GEOTIFF_PCS_ARC_1950_LO29                      20989
#define GEOTIFF_PCS_ARC_1950_LO31                      20991
#define GEOTIFF_PCS_ARC_1950_LO33                      20993
#define GEOTIFF_PCS_ARC_1950_LO35                      20995
#define GEOTIFF_PCS_BATAVIA_NEIEZ                      21100
#define GEOTIFF_PCS_BATAVIA_UTM_ZONE_48S               21148
#define GEOTIFF_PCS_BATAVIA_UTM_ZONE_49S               21149
#define GEOTIFF_PCS_BATAVIA_UTM_ZONE_50S               21150
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_13              21413
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_14              21414
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_15              21415
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_16              21416
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_17              21417
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_18              21418
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_19              21419
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_20              21420
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_21              21421
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_22              21422
#define GEOTIFF_PCS_BEIJING_GAUSS_ZONE_23              21423
#define GEOTIFF_PCS_BEIJING_GAUSS_13N                  21473
#define GEOTIFF_PCS_BEIJING_GAUSS_14N                  21474
#define GEOTIFF_PCS_BEIJING_GAUSS_15N                  21475
#define GEOTIFF_PCS_BEIJING_GAUSS_16N                  21476
#define GEOTIFF_PCS_BEIJING_GAUSS_17N                  21477
#define GEOTIFF_PCS_BEIJING_GAUSS_18N                  21478
#define GEOTIFF_PCS_BEIJING_GAUSS_19N                  21479
#define GEOTIFF_PCS_BEIJING_GAUSS_20N                  21480
#define GEOTIFF_PCS_BEIJING_GAUSS_21N                  21481
#define GEOTIFF_PCS_BEIJING_GAUSS_22N                  21482
#define GEOTIFF_PCS_BEIJING_GAUSS_23N                  21483
#define GEOTIFF_PCS_BELGE_LAMBERT_50                   21500
#define GEOTIFF_PCS_BERN_1898_SWISS_OLD                21790
#define GEOTIFF_PCS_BOGOTA_UTM_ZONE_17N                21817
#define GEOTIFF_PCS_BOGOTA_UTM_ZONE_18N                21818
#define GEOTIFF_PCS_BOGOTA_COLOMBIA_3W                 21891
#define GEOTIFF_PCS_BOGOTA_COLOMBIA_BOGOTA             21892
#define GEOTIFF_PCS_BOGOTA_COLOMBIA_3E                 21893
#define GEOTIFF_PCS_BOGOTA_COLOMBIA_6E                 21894
#define GEOTIFF_PCS_CAMACUPA_UTM_32S                   22032
#define GEOTIFF_PCS_CAMACUPA_UTM_33S                   22033
#define GEOTIFF_PCS_C_INCHAUSPE_ARGENTINA_1            22191
#define GEOTIFF_PCS_C_INCHAUSPE_ARGENTINA_2            22192
#define GEOTIFF_PCS_C_INCHAUSPE_ARGENTINA_3            22193
#define GEOTIFF_PCS_C_INCHAUSPE_ARGENTINA_4            22194
#define GEOTIFF_PCS_C_INCHAUSPE_ARGENTINA_5            22195
#define GEOTIFF_PCS_C_INCHAUSPE_ARGENTINA_6            22196
#define GEOTIFF_PCS_C_INCHAUSPE_ARGENTINA_7            22197
#define GEOTIFF_PCS_CARTHAGE_UTM_ZONE_32N              22332
#define GEOTIFF_PCS_CARTHAGE_NORD_TUNISIE              22391
#define GEOTIFF_PCS_CARTHAGE_SUD_TUNISIE               22392
#define GEOTIFF_PCS_CORREGO_ALEGRE_UTM_23S             22523
#define GEOTIFF_PCS_CORREGO_ALEGRE_UTM_24S             22524
#define GEOTIFF_PCS_DOUALA_UTM_ZONE_32N                22832
#define GEOTIFF_PCS_EGYPT_1907_RED_BELT                22992
#define GEOTIFF_PCS_EGYPT_1907_PURPLE_BELT             22993
#define GEOTIFF_PCS_EGYPT_1907_EXT_PURPLE              22994
#define GEOTIFF_PCS_ED50_UTM_ZONE_28N                  23028
#define GEOTIFF_PCS_ED50_UTM_ZONE_29N                  23029
#define GEOTIFF_PCS_ED50_UTM_ZONE_30N                  23030
#define GEOTIFF_PCS_ED50_UTM_ZONE_31N                  23031
#define GEOTIFF_PCS_ED50_UTM_ZONE_32N                  23032
#define GEOTIFF_PCS_ED50_UTM_ZONE_33N                  23033
#define GEOTIFF_PCS_ED50_UTM_ZONE_34N                  23034
#define GEOTIFF_PCS_ED50_UTM_ZONE_35N                  23035
#define GEOTIFF_PCS_ED50_UTM_ZONE_36N                  23036
#define GEOTIFF_PCS_ED50_UTM_ZONE_37N                  23037
#define GEOTIFF_PCS_ED50_UTM_ZONE_38N                  23038
#define GEOTIFF_PCS_FAHUD_UTM_ZONE_39N                 23239
#define GEOTIFF_PCS_FAHUD_UTM_ZONE_40N                 23240
#define GEOTIFF_PCS_GAROUA_UTM_ZONE_33N                23433
#define GEOTIFF_PCS_ID74_UTM_ZONE_46N                  23846
#define GEOTIFF_PCS_ID74_UTM_ZONE_47N                  23847
#define GEOTIFF_PCS_ID74_UTM_ZONE_48N                  23848
#define GEOTIFF_PCS_ID74_UTM_ZONE_49N                  23849
#define GEOTIFF_PCS_ID74_UTM_ZONE_50N                  23850
#define GEOTIFF_PCS_ID74_UTM_ZONE_51N                  23851
#define GEOTIFF_PCS_ID74_UTM_ZONE_52N                  23852
#define GEOTIFF_PCS_ID74_UTM_ZONE_53N                  23853
#define GEOTIFF_PCS_ID74_UTM_ZONE_46S                  23886
#define GEOTIFF_PCS_ID74_UTM_ZONE_47S                  23887
#define GEOTIFF_PCS_ID74_UTM_ZONE_48S                  23888
#define GEOTIFF_PCS_ID74_UTM_ZONE_49S                  23889
#define GEOTIFF_PCS_ID74_UTM_ZONE_50S                  23890
#define GEOTIFF_PCS_ID74_UTM_ZONE_51S                  23891
#define GEOTIFF_PCS_ID74_UTM_ZONE_52S                  23892
#define GEOTIFF_PCS_ID74_UTM_ZONE_53S                  23893
#define GEOTIFF_PCS_ID74_UTM_ZONE_54S                  23894
#define GEOTIFF_PCS_INDIAN_1954_UTM_47N                23947
#define GEOTIFF_PCS_INDIAN_1954_UTM_48N                23948
#define GEOTIFF_PCS_INDIAN_1975_UTM_47N                24047
#define GEOTIFF_PCS_INDIAN_1975_UTM_48N                24048
#define GEOTIFF_PCS_JAMAICA_1875_OLD_GRID              24100
#define GEOTIFF_PCS_JAD69_JAMAICA_GRID                 24200
#define GEOTIFF_PCS_KALIANPUR_INDIA_0                  24370
#define GEOTIFF_PCS_KALIANPUR_INDIA_I                  24371
#define GEOTIFF_PCS_KALIANPUR_INDIA_IIA                24372
#define GEOTIFF_PCS_KALIANPUR_INDIA_IIIA               24373
#define GEOTIFF_PCS_KALIANPUR_INDIA_IVA                24374
#define GEOTIFF_PCS_KALIANPUR_INDIA_IIB                24382
#define GEOTIFF_PCS_KALIANPUR_INDIA_IIIB               24383
#define GEOTIFF_PCS_KALIANPUR_INDIA_IVB                24384
#define GEOTIFF_PCS_KERTAU_SINGAPORE_GRID              24500
#define GEOTIFF_PCS_KERTAU_UTM_ZONE_47N                24547
#define GEOTIFF_PCS_KERTAU_UTM_ZONE_48N                24548
#define GEOTIFF_PCS_LA_CANOA_UTM_ZONE_20N              24720
#define GEOTIFF_PCS_LA_CANOA_UTM_ZONE_21N              24721
#define GEOTIFF_PCS_PSAD56_UTM_ZONE_18N                24818
#define GEOTIFF_PCS_PSAD56_UTM_ZONE_19N                24819
#define GEOTIFF_PCS_PSAD56_UTM_ZONE_20N                24820
#define GEOTIFF_PCS_PSAD56_UTM_ZONE_21N                24821
#define GEOTIFF_PCS_PSAD56_UTM_ZONE_17S                24877
#define GEOTIFF_PCS_PSAD56_UTM_ZONE_18S                24878
#define GEOTIFF_PCS_PSAD56_UTM_ZONE_19S                24879
#define GEOTIFF_PCS_PSAD56_UTM_ZONE_20S                24880
#define GEOTIFF_PCS_PSAD56_PERU_WEST_ZONE              24891
#define GEOTIFF_PCS_PSAD56_PERU_CENTRAL                24892
#define GEOTIFF_PCS_PSAD56_PERU_EAST_ZONE              24893
#define GEOTIFF_PCS_LEIGON_GHANA_GRID                  25000
#define GEOTIFF_PCS_LOME_UTM_ZONE_31N                  25231
#define GEOTIFF_PCS_LUZON_PHILIPPINES_I                25391
#define GEOTIFF_PCS_LUZON_PHILIPPINES_II               25392
#define GEOTIFF_PCS_LUZON_PHILIPPINES_III              25393
#define GEOTIFF_PCS_LUZON_PHILIPPINES_IV               25394
#define GEOTIFF_PCS_LUZON_PHILIPPINES_V                25395
#define GEOTIFF_PCS_MAKASSAR_NEIEZ                     25700
#define GEOTIFF_PCS_MALONGO_1987_UTM_32S               25932
#define GEOTIFF_PCS_MERCHICH_NORD_MAROC                26191
#define GEOTIFF_PCS_MERCHICH_SUD_MAROC                 26192
#define GEOTIFF_PCS_MERCHICH_SAHARA                    26193
#define GEOTIFF_PCS_MASSAWA_UTM_ZONE_37N               26237
#define GEOTIFF_PCS_MINNA_UTM_ZONE_31N                 26331
#define GEOTIFF_PCS_MINNA_UTM_ZONE_32N                 26332
#define GEOTIFF_PCS_MINNA_NIGERIA_WEST                 26391
#define GEOTIFF_PCS_MINNA_NIGERIA_MID_BELT             26392
#define GEOTIFF_PCS_MINNA_NIGERIA_EAST                 26393
#define GEOTIFF_PCS_MHAST_UTM_ZONE_32S                 26432
#define GEOTIFF_PCS_MONTE_MARIO_ITALY_1                26591
#define GEOTIFF_PCS_MONTE_MARIO_ITALY_2                26592
#define GEOTIFF_PCS_M_PORALOKO_UTM_32N                 26632
#define GEOTIFF_PCS_M_PORALOKO_UTM_32S                 26692
#define GEOTIFF_PCS_NAD27_UTM_ZONE_3N                  26703
#define GEOTIFF_PCS_NAD27_UTM_ZONE_4N                  26704
#define GEOTIFF_PCS_NAD27_UTM_ZONE_5N                  26705
#define GEOTIFF_PCS_NAD27_UTM_ZONE_6N                  26706
#define GEOTIFF_PCS_NAD27_UTM_ZONE_7N                  26707
#define GEOTIFF_PCS_NAD27_UTM_ZONE_8N                  26708
#define GEOTIFF_PCS_NAD27_UTM_ZONE_9N                  26709
#define GEOTIFF_PCS_NAD27_UTM_ZONE_10N                 26710
#define GEOTIFF_PCS_NAD27_UTM_ZONE_11N                 26711
#define GEOTIFF_PCS_NAD27_UTM_ZONE_12N                 26712
#define GEOTIFF_PCS_NAD27_UTM_ZONE_13N                 26713
#define GEOTIFF_PCS_NAD27_UTM_ZONE_14N                 26714
#define GEOTIFF_PCS_NAD27_UTM_ZONE_15N                 26715
#define GEOTIFF_PCS_NAD27_UTM_ZONE_16N                 26716
#define GEOTIFF_PCS_NAD27_UTM_ZONE_17N                 26717
#define GEOTIFF_PCS_NAD27_UTM_ZONE_18N                 26718
#define GEOTIFF_PCS_NAD27_UTM_ZONE_19N                 26719
#define GEOTIFF_PCS_NAD27_UTM_ZONE_20N                 26720
#define GEOTIFF_PCS_NAD27_UTM_ZONE_21N                 26721
#define GEOTIFF_PCS_NAD27_UTM_ZONE_22N                 26722
#define GEOTIFF_PCS_NAD27_ALABAMA_EAST                 26729
#define GEOTIFF_PCS_NAD27_ALABAMA_WEST                 26730
#define GEOTIFF_PCS_NAD27_ALASKA_ZONE_1                26731
#define GEOTIFF_PCS_NAD27_ALASKA_ZONE_2                26732
#define GEOTIFF_PCS_NAD27_ALASKA_ZONE_3                26733
#define GEOTIFF_PCS_NAD27_ALASKA_ZONE_4                26734
#define GEOTIFF_PCS_NAD27_ALASKA_ZONE_5                26735
#define GEOTIFF_PCS_NAD27_ALASKA_ZONE_6                26736
#define GEOTIFF_PCS_NAD27_ALASKA_ZONE_7                26737
#define GEOTIFF_PCS_NAD27_ALASKA_ZONE_8                26738
#define GEOTIFF_PCS_NAD27_ALASKA_ZONE_9                26739
#define GEOTIFF_PCS_NAD27_ALASKA_ZONE_10               26740
#define GEOTIFF_PCS_NAD27_CALIFORNIA_I                 26741
#define GEOTIFF_PCS_NAD27_CALIFORNIA_II                26742
#define GEOTIFF_PCS_NAD27_CALIFORNIA_III               26743
#define GEOTIFF_PCS_NAD27_CALIFORNIA_IV                26744
#define GEOTIFF_PCS_NAD27_CALIFORNIA_V                 26745
#define GEOTIFF_PCS_NAD27_CALIFORNIA_VI                26746
#define GEOTIFF_PCS_NAD27_CALIFORNIA_VII               26747
#define GEOTIFF_PCS_NAD27_ARIZONA_EAST                 26748
#define GEOTIFF_PCS_NAD27_ARIZONA_CENTRAL              26749
#define GEOTIFF_PCS_NAD27_ARIZONA_WEST                 26750
#define GEOTIFF_PCS_NAD27_ARKANSAS_NORTH               26751
#define GEOTIFF_PCS_NAD27_ARKANSAS_SOUTH               26752
#define GEOTIFF_PCS_NAD27_COLORADO_NORTH               26753
#define GEOTIFF_PCS_NAD27_COLORADO_CENTRAL             26754
#define GEOTIFF_PCS_NAD27_COLORADO_SOUTH               26755
#define GEOTIFF_PCS_NAD27_CONNECTICUT                  26756
#define GEOTIFF_PCS_NAD27_DELAWARE                     26757
#define GEOTIFF_PCS_NAD27_FLORIDA_EAST                 26758
#define GEOTIFF_PCS_NAD27_FLORIDA_WEST                 26759
#define GEOTIFF_PCS_NAD27_FLORIDA_NORTH                26760
#define GEOTIFF_PCS_NAD27_HAWAII_ZONE_1                26761
#define GEOTIFF_PCS_NAD27_HAWAII_ZONE_2                26762
#define GEOTIFF_PCS_NAD27_HAWAII_ZONE_3                26763
#define GEOTIFF_PCS_NAD27_HAWAII_ZONE_4                26764
#define GEOTIFF_PCS_NAD27_HAWAII_ZONE_5                26765
#define GEOTIFF_PCS_NAD27_GEORGIA_EAST                 26766
#define GEOTIFF_PCS_NAD27_GEORGIA_WEST                 26767
#define GEOTIFF_PCS_NAD27_IDAHO_EAST                   26768
#define GEOTIFF_PCS_NAD27_IDAHO_CENTRAL                26769
#define GEOTIFF_PCS_NAD27_IDAHO_WEST                   26770
#define GEOTIFF_PCS_NAD27_ILLINOIS_EAST                26771
#define GEOTIFF_PCS_NAD27_ILLINOIS_WEST                26772
#define GEOTIFF_PCS_NAD27_INDIANA_EAST                 26773
#define GEOTIFF_PCS_NAD27_INDIANA_WEST                 26774
#define GEOTIFF_PCS_NAD27_BLM_14N_FEET                 26774   // duplicate
#define GEOTIFF_PCS_NAD27_IOWA_NORTH                   26775
#define GEOTIFF_PCS_NAD27_BLM_15N_FEET                 26775   // duplicate
#define GEOTIFF_PCS_NAD27_IOWA_SOUTH                   26776
#define GEOTIFF_PCS_NAD27_BLM_16N_FEET                 26776   // duplicate
#define GEOTIFF_PCS_NAD27_KANSAS_NORTH                 26777
#define GEOTIFF_PCS_NAD27_BLM_17N_FEET                 26777   // duplicate
#define GEOTIFF_PCS_NAD27_KANSAS_SOUTH                 26778
#define GEOTIFF_PCS_NAD27_KENTUCKY_NORTH               26779
#define GEOTIFF_PCS_NAD27_KENTUCKY_SOUTH               26780
#define GEOTIFF_PCS_NAD27_LOUISIANA_NORTH              26781
#define GEOTIFF_PCS_NAD27_LOUISIANA_SOUTH              26782
#define GEOTIFF_PCS_NAD27_MAINE_EAST                   26783
#define GEOTIFF_PCS_NAD27_MAINE_WEST                   26784
#define GEOTIFF_PCS_NAD27_MARYLAND                     26785
#define GEOTIFF_PCS_NAD27_MASSACHUSETTS                26786
#define GEOTIFF_PCS_NAD27_MASSACHUSETTS_IS             26787
#define GEOTIFF_PCS_NAD27_MICHIGAN_NORTH               26788
#define GEOTIFF_PCS_NAD27_MICHIGAN_CENTRAL             26789
#define GEOTIFF_PCS_NAD27_MICHIGAN_SOUTH               26790
#define GEOTIFF_PCS_NAD27_MINNESOTA_NORTH              26791
#define GEOTIFF_PCS_NAD27_MINNESOTA_CENT               26792
#define GEOTIFF_PCS_NAD27_MINNESOTA_SOUTH              26793
#define GEOTIFF_PCS_NAD27_MISSISSIPPI_EAST             26794
#define GEOTIFF_PCS_NAD27_MISSISSIPPI_WEST             26795
#define GEOTIFF_PCS_NAD27_MISSOURI_EAST                26796
#define GEOTIFF_PCS_NAD27_MISSOURI_CENTRAL             26797
#define GEOTIFF_PCS_NAD27_MISSOURI_WEST                26798
#define GEOTIFF_PCS_NAD_MICHIGAN_MICHIGAN_EAST         26801
#define GEOTIFF_PCS_NAD_MICHIGAN_MICHIGAN_OLD_CENTRAL  26802
#define GEOTIFF_PCS_NAD_MICHIGAN_MICHIGAN_WEST         26803
#define GEOTIFF_PCS_NAD83_UTM_ZONE_3N                  26903
#define GEOTIFF_PCS_NAD83_UTM_ZONE_4N                  26904
#define GEOTIFF_PCS_NAD83_UTM_ZONE_5N                  26905
#define GEOTIFF_PCS_NAD83_UTM_ZONE_6N                  26906
#define GEOTIFF_PCS_NAD83_UTM_ZONE_7N                  26907
#define GEOTIFF_PCS_NAD83_UTM_ZONE_8N                  26908
#define GEOTIFF_PCS_NAD83_UTM_ZONE_9N                  26909
#define GEOTIFF_PCS_NAD83_UTM_ZONE_10N                 26910
#define GEOTIFF_PCS_NAD83_UTM_ZONE_11N                 26911
#define GEOTIFF_PCS_NAD83_UTM_ZONE_12N                 26912
#define GEOTIFF_PCS_NAD83_UTM_ZONE_13N                 26913
#define GEOTIFF_PCS_NAD83_UTM_ZONE_14N                 26914
#define GEOTIFF_PCS_NAD83_UTM_ZONE_15N                 26915
#define GEOTIFF_PCS_NAD83_UTM_ZONE_16N                 26916
#define GEOTIFF_PCS_NAD83_UTM_ZONE_17N                 26917
#define GEOTIFF_PCS_NAD83_UTM_ZONE_18N                 26918
#define GEOTIFF_PCS_NAD83_UTM_ZONE_19N                 26919
#define GEOTIFF_PCS_NAD83_UTM_ZONE_20N                 26920
#define GEOTIFF_PCS_NAD83_UTM_ZONE_21N                 26921
#define GEOTIFF_PCS_NAD83_UTM_ZONE_22N                 26922
#define GEOTIFF_PCS_NAD83_UTM_ZONE_23N                 26923
#define GEOTIFF_PCS_NAD83_ALABAMA_EAST                 26929
#define GEOTIFF_PCS_NAD83_ALABAMA_WEST                 26930
#define GEOTIFF_PCS_NAD83_ALASKA_ZONE_1                26931
#define GEOTIFF_PCS_NAD83_ALASKA_ZONE_2                26932
#define GEOTIFF_PCS_NAD83_ALASKA_ZONE_3                26933
#define GEOTIFF_PCS_NAD83_ALASKA_ZONE_4                26934
#define GEOTIFF_PCS_NAD83_ALASKA_ZONE_5                26935
#define GEOTIFF_PCS_NAD83_ALASKA_ZONE_6                26936
#define GEOTIFF_PCS_NAD83_ALASKA_ZONE_7                26937
#define GEOTIFF_PCS_NAD83_ALASKA_ZONE_8                26938
#define GEOTIFF_PCS_NAD83_ALASKA_ZONE_9                26939
#define GEOTIFF_PCS_NAD83_ALASKA_ZONE_10               26940
#define GEOTIFF_PCS_NAD83_CALIFORNIA_1                 26941
#define GEOTIFF_PCS_NAD83_CALIFORNIA_2                 26942
#define GEOTIFF_PCS_NAD83_CALIFORNIA_3                 26943
#define GEOTIFF_PCS_NAD83_CALIFORNIA_4                 26944
#define GEOTIFF_PCS_NAD83_CALIFORNIA_5                 26945
#define GEOTIFF_PCS_NAD83_CALIFORNIA_6                 26946
#define GEOTIFF_PCS_NAD83_ARIZONA_EAST                 26948
#define GEOTIFF_PCS_NAD83_ARIZONA_CENTRAL              26949
#define GEOTIFF_PCS_NAD83_ARIZONA_WEST                 26950
#define GEOTIFF_PCS_NAD83_ARKANSAS_NORTH               26951
#define GEOTIFF_PCS_NAD83_ARKANSAS_SOUTH               26952
#define GEOTIFF_PCS_NAD83_COLORADO_NORTH               26953
#define GEOTIFF_PCS_NAD83_COLORADO_CENTRAL             26954
#define GEOTIFF_PCS_NAD83_COLORADO_SOUTH               26955
#define GEOTIFF_PCS_NAD83_CONNECTICUT                  26956
#define GEOTIFF_PCS_NAD83_DELAWARE                     26957
#define GEOTIFF_PCS_NAD83_FLORIDA_EAST                 26958
#define GEOTIFF_PCS_NAD83_FLORIDA_WEST                 26959
#define GEOTIFF_PCS_NAD83_FLORIDA_NORTH                26960
#define GEOTIFF_PCS_NAD83_HAWAII_ZONE_1                26961
#define GEOTIFF_PCS_NAD83_HAWAII_ZONE_2                26962
#define GEOTIFF_PCS_NAD83_HAWAII_ZONE_3                26963
#define GEOTIFF_PCS_NAD83_HAWAII_ZONE_4                26964
#define GEOTIFF_PCS_NAD83_HAWAII_ZONE_5                26965
#define GEOTIFF_PCS_NAD83_GEORGIA_EAST                 26966
#define GEOTIFF_PCS_NAD83_GEORGIA_WEST                 26967
#define GEOTIFF_PCS_NAD83_IDAHO_EAST                   26968
#define GEOTIFF_PCS_NAD83_IDAHO_CENTRAL                26969
#define GEOTIFF_PCS_NAD83_IDAHO_WEST                   26970
#define GEOTIFF_PCS_NAD83_ILLINOIS_EAST                26971
#define GEOTIFF_PCS_NAD83_ILLINOIS_WEST                26972
#define GEOTIFF_PCS_NAD83_INDIANA_EAST                 26973
#define GEOTIFF_PCS_NAD83_INDIANA_WEST                 26974
#define GEOTIFF_PCS_NAD83_IOWA_NORTH                   26975
#define GEOTIFF_PCS_NAD83_IOWA_SOUTH                   26976
#define GEOTIFF_PCS_NAD83_KANSAS_NORTH                 26977
#define GEOTIFF_PCS_NAD83_KANSAS_SOUTH                 26978
#define GEOTIFF_PCS_NAD83_KENTUCKY_NORTH               26979
#define GEOTIFF_PCS_NAD83_KENTUCKY_SOUTH               26980
#define GEOTIFF_PCS_NAD83_LOUISIANA_NORTH              26981
#define GEOTIFF_PCS_NAD83_LOUISIANA_SOUTH              26982
#define GEOTIFF_PCS_NAD83_MAINE_EAST                   26983
#define GEOTIFF_PCS_NAD83_MAINE_WEST                   26984
#define GEOTIFF_PCS_NAD83_MARYLAND                     26985
#define GEOTIFF_PCS_NAD83_MASSACHUSETTS                26986
#define GEOTIFF_PCS_NAD83_MASSACHUSETTS_IS             26987
#define GEOTIFF_PCS_NAD83_MICHIGAN_NORTH               26988
#define GEOTIFF_PCS_NAD83_MICHIGAN_CENTRAL             26989
#define GEOTIFF_PCS_NAD83_MICHIGAN_SOUTH               26990
#define GEOTIFF_PCS_NAD83_MINNESOTA_NORTH              26991
#define GEOTIFF_PCS_NAD83_MINNESOTA_CENT               26992
#define GEOTIFF_PCS_NAD83_MINNESOTA_SOUTH              26993
#define GEOTIFF_PCS_NAD83_MISSISSIPPI_EAST             26994
#define GEOTIFF_PCS_NAD83_MISSISSIPPI_WEST             26995
#define GEOTIFF_PCS_NAD83_MISSOURI_EAST                26996
#define GEOTIFF_PCS_NAD83_MISSOURI_CENTRAL             26997
#define GEOTIFF_PCS_NAD83_MISSOURI_WEST                26998
#define GEOTIFF_PCS_NAHRWAN_1967_UTM_38N               27038
#define GEOTIFF_PCS_NAHRWAN_1967_UTM_39N               27039
#define GEOTIFF_PCS_NAHRWAN_1967_UTM_40N               27040
#define GEOTIFF_PCS_NAPARIMA_UTM_20N                   27120
#define GEOTIFF_PCS_GD49_NZ_MAP_GRID                   27200
#define GEOTIFF_PCS_GD49_NORTH_ISLAND_GRID             27291
#define GEOTIFF_PCS_GD49_SOUTH_ISLAND_GRID             27292
#define GEOTIFF_PCS_DATUM_73_UTM_ZONE_29N              27429
#define GEOTIFF_PCS_ATF_NORD_DE_GUERRE                 27500
#define GEOTIFF_PCS_NTF_FRANCE_I                       27581
#define GEOTIFF_PCS_NTF_FRANCE_II                      27582
#define GEOTIFF_PCS_NTF_FRANCE_III                     27583
#define GEOTIFF_PCS_NTF_NORD_FRANCE                    27591
#define GEOTIFF_PCS_NTF_CENTRE_FRANCE                  27592
#define GEOTIFF_PCS_NTF_SUD_FRANCE                     27593
#define GEOTIFF_PCS_BRITISH_NATIONAL_GRID              27700
#define GEOTIFF_PCS_POINT_NOIRE_UTM_32S                28232
#define GEOTIFF_PCS_GDA94_MGA_ZONE_48                  28348
#define GEOTIFF_PCS_GDA94_MGA_ZONE_49                  28349
#define GEOTIFF_PCS_GDA94_MGA_ZONE_50                  28350
#define GEOTIFF_PCS_GDA94_MGA_ZONE_51                  28351
#define GEOTIFF_PCS_GDA94_MGA_ZONE_52                  28352
#define GEOTIFF_PCS_GDA94_MGA_ZONE_53                  28353
#define GEOTIFF_PCS_GDA94_MGA_ZONE_54                  28354
#define GEOTIFF_PCS_GDA94_MGA_ZONE_55                  28355
#define GEOTIFF_PCS_GDA94_MGA_ZONE_56                  28356
#define GEOTIFF_PCS_GDA94_MGA_ZONE_57                  28357
#define GEOTIFF_PCS_GDA94_MGA_ZONE_58                  28358
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_4               28404
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_5               28405
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_6               28406
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_7               28407
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_8               28408
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_9               28409
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_10              28410
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_11              28411
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_12              28412
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_13              28413
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_14              28414
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_15              28415
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_16              28416
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_17              28417
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_18              28418
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_19              28419
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_20              28420
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_21              28421
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_22              28422
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_23              28423
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_24              28424
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_25              28425
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_26              28426
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_27              28427
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_28              28428
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_29              28429
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_30              28430
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_31              28431
#define GEOTIFF_PCS_PULKOVO_GAUSS_ZONE_32              28432
#define GEOTIFF_PCS_PULKOVO_GAUSS_4N                   28464
#define GEOTIFF_PCS_PULKOVO_GAUSS_5N                   28465
#define GEOTIFF_PCS_PULKOVO_GAUSS_6N                   28466
#define GEOTIFF_PCS_PULKOVO_GAUSS_7N                   28467
#define GEOTIFF_PCS_PULKOVO_GAUSS_8N                   28468
#define GEOTIFF_PCS_PULKOVO_GAUSS_9N                   28469
#define GEOTIFF_PCS_PULKOVO_GAUSS_10N                  28470
#define GEOTIFF_PCS_PULKOVO_GAUSS_11N                  28471
#define GEOTIFF_PCS_PULKOVO_GAUSS_12N                  28472
#define GEOTIFF_PCS_PULKOVO_GAUSS_13N                  28473
#define GEOTIFF_PCS_PULKOVO_GAUSS_14N                  28474
#define GEOTIFF_PCS_PULKOVO_GAUSS_15N                  28475
#define GEOTIFF_PCS_PULKOVO_GAUSS_16N                  28476
#define GEOTIFF_PCS_PULKOVO_GAUSS_17N                  28477
#define GEOTIFF_PCS_PULKOVO_GAUSS_18N                  28478
#define GEOTIFF_PCS_PULKOVO_GAUSS_19N                  28479
#define GEOTIFF_PCS_PULKOVO_GAUSS_20N                  28480
#define GEOTIFF_PCS_PULKOVO_GAUSS_21N                  28481
#define GEOTIFF_PCS_PULKOVO_GAUSS_22N                  28482
#define GEOTIFF_PCS_PULKOVO_GAUSS_23N                  28483
#define GEOTIFF_PCS_PULKOVO_GAUSS_24N                  28484
#define GEOTIFF_PCS_PULKOVO_GAUSS_25N                  28485
#define GEOTIFF_PCS_PULKOVO_GAUSS_26N                  28486
#define GEOTIFF_PCS_PULKOVO_GAUSS_27N                  28487
#define GEOTIFF_PCS_PULKOVO_GAUSS_28N                  28488
#define GEOTIFF_PCS_PULKOVO_GAUSS_29N                  28489
#define GEOTIFF_PCS_PULKOVO_GAUSS_30N                  28490
#define GEOTIFF_PCS_PULKOVO_GAUSS_31N                  28491
#define GEOTIFF_PCS_PULKOVO_GAUSS_32N                  28492
#define GEOTIFF_PCS_QATAR_NATIONAL_GRID                28600
#define GEOTIFF_PCS_RD_NETHERLANDS_OLD                 28991
#define GEOTIFF_PCS_RD_NETHERLANDS_NEW                 28992
#define GEOTIFF_PCS_SAD69_UTM_ZONE_18N                 29118
#define GEOTIFF_PCS_SAD69_UTM_ZONE_19N                 29119
#define GEOTIFF_PCS_SAD69_UTM_ZONE_20N                 29120
#define GEOTIFF_PCS_SAD69_UTM_ZONE_21N                 29121
#define GEOTIFF_PCS_SAD69_UTM_ZONE_22N                 29122
#define GEOTIFF_PCS_SAD69_UTM_ZONE_17S                 29177
#define GEOTIFF_PCS_SAD69_UTM_ZONE_18S                 29178
#define GEOTIFF_PCS_SAD69_UTM_ZONE_19S                 29179
#define GEOTIFF_PCS_SAD69_UTM_ZONE_20S                 29180
#define GEOTIFF_PCS_SAD69_UTM_ZONE_21S                 29181
#define GEOTIFF_PCS_SAD69_UTM_ZONE_22S                 29182
#define GEOTIFF_PCS_SAD69_UTM_ZONE_23S                 29183
#define GEOTIFF_PCS_SAD69_UTM_ZONE_24S                 29184
#define GEOTIFF_PCS_SAD69_UTM_ZONE_25S                 29185
#define GEOTIFF_PCS_SAPPER_HILL_UTM_20S                29220
#define GEOTIFF_PCS_SAPPER_HILL_UTM_21S                29221
#define GEOTIFF_PCS_SCHWARZECK_UTM_33S                 29333
#define GEOTIFF_PCS_SUDAN_UTM_ZONE_35N                 29635
#define GEOTIFF_PCS_SUDAN_UTM_ZONE_36N                 29636
#define GEOTIFF_PCS_TANANARIVE_LABORDE                 29700
#define GEOTIFF_PCS_TANANARIVE_UTM_38S                 29738
#define GEOTIFF_PCS_TANANARIVE_UTM_39S                 29739
#define GEOTIFF_PCS_TIMBALAI_1948_BORNEO               29800
#define GEOTIFF_PCS_TIMBALAI_1948_UTM_49N              29849
#define GEOTIFF_PCS_TIMBALAI_1948_UTM_50N              29850
#define GEOTIFF_PCS_TM65_IRISH_NAT_GRID                29900
#define GEOTIFF_PCS_TRINIDAD_1903_TRINIDAD             30200
#define GEOTIFF_PCS_TC_1948_UTM_ZONE_39N               30339
#define GEOTIFF_PCS_TC_1948_UTM_ZONE_40N               30340
#define GEOTIFF_PCS_VOIROL_N_ALGERIE_ANCIEN            30491
#define GEOTIFF_PCS_VOIROL_S_ALGERIE_ANCIEN            30492
#define GEOTIFF_PCS_VOIROL_UNIFIE_N_ALGERIE            30591
#define GEOTIFF_PCS_VOIROL_UNIFIE_S_ALGERIE            30592
#define GEOTIFF_PCS_BERN_1938_SWISS_NEW                30600
#define GEOTIFF_PCS_NORD_SAHARA_UTM_29N                30729
#define GEOTIFF_PCS_NORD_SAHARA_UTM_30N                30730
#define GEOTIFF_PCS_NORD_SAHARA_UTM_31N                30731
#define GEOTIFF_PCS_NORD_SAHARA_UTM_32N                30732
#define GEOTIFF_PCS_YOFF_UTM_ZONE_28N                  31028
#define GEOTIFF_PCS_ZANDERIJ_UTM_ZONE_21N              31121
#define GEOTIFF_PCS_MGI_AUSTRIA_WEST                   31291
#define GEOTIFF_PCS_MGI_AUSTRIA_CENTRAL                31292
#define GEOTIFF_PCS_MGI_AUSTRIA_EAST                   31293
#define GEOTIFF_PCS_BELGE_LAMBERT_72                   31300
#define GEOTIFF_PCS_DHDN_GERMANY_ZONE_1                31491
#define GEOTIFF_PCS_DHDN_GERMANY_ZONE_2                31492
#define GEOTIFF_PCS_DHDN_GERMANY_ZONE_3                31493
#define GEOTIFF_PCS_DHDN_GERMANY_ZONE_4                31494
#define GEOTIFF_PCS_DHDN_GERMANY_ZONE_5                31495
#define GEOTIFF_PCS_NAD27_MONTANA_NORTH                32001
#define GEOTIFF_PCS_NAD27_MONTANA_CENTRAL              32002
#define GEOTIFF_PCS_NAD27_MONTANA_SOUTH                32003
#define GEOTIFF_PCS_NAD27_NEBRASKA_NORTH               32005
#define GEOTIFF_PCS_NAD27_NEBRASKA_SOUTH               32006
#define GEOTIFF_PCS_NAD27_NEVADA_EAST                  32007
#define GEOTIFF_PCS_NAD27_NEVADA_CENTRAL               32008
#define GEOTIFF_PCS_NAD27_NEVADA_WEST                  32009
#define GEOTIFF_PCS_NAD27_NEW_HAMPSHIRE                32010
#define GEOTIFF_PCS_NAD27_NEW_JERSEY                   32011
#define GEOTIFF_PCS_NAD27_NEW_MEXICO_EAST              32012
#define GEOTIFF_PCS_NAD27_NEW_MEXICO_CENT              32013
#define GEOTIFF_PCS_NAD27_NEW_MEXICO_WEST              32014
#define GEOTIFF_PCS_NAD27_NEW_YORK_EAST                32015
#define GEOTIFF_PCS_NAD27_NEW_YORK_CENTRAL             32016
#define GEOTIFF_PCS_NAD27_NEW_YORK_WEST                32017
#define GEOTIFF_PCS_NAD27_NEW_YORK_LONG_IS             32018
#define GEOTIFF_PCS_NAD27_NORTH_CAROLINA               32019
#define GEOTIFF_PCS_NAD27_NORTH_DAKOTA_N               32020
#define GEOTIFF_PCS_NAD27_NORTH_DAKOTA_S               32021
#define GEOTIFF_PCS_NAD27_OHIO_NORTH                   32022
#define GEOTIFF_PCS_NAD27_OHIO_SOUTH                   32023
#define GEOTIFF_PCS_NAD27_OKLAHOMA_NORTH               32024
#define GEOTIFF_PCS_NAD27_OKLAHOMA_SOUTH               32025
#define GEOTIFF_PCS_NAD27_OREGON_NORTH                 32026
#define GEOTIFF_PCS_NAD27_OREGON_SOUTH                 32027
#define GEOTIFF_PCS_NAD27_PENNSYLVANIA_N               32028
#define GEOTIFF_PCS_NAD27_PENNSYLVANIA_S               32029
#define GEOTIFF_PCS_NAD27_RHODE_ISLAND                 32030
#define GEOTIFF_PCS_NAD27_SOUTH_CAROLINA_N             32031
#define GEOTIFF_PCS_NAD27_SOUTH_CAROLINA_S             32033
#define GEOTIFF_PCS_NAD27_SOUTH_DAKOTA_N               32034
#define GEOTIFF_PCS_NAD27_SOUTH_DAKOTA_S               32035
#define GEOTIFF_PCS_NAD27_TENNESSEE                    32036
#define GEOTIFF_PCS_NAD27_TEXAS_NORTH                  32037
#define GEOTIFF_PCS_NAD27_TEXAS_NORTH_CEN              32038
#define GEOTIFF_PCS_NAD27_TEXAS_CENTRAL                32039
#define GEOTIFF_PCS_NAD27_TEXAS_SOUTH_CEN              32040
#define GEOTIFF_PCS_NAD27_TEXAS_SOUTH                  32041
#define GEOTIFF_PCS_NAD27_UTAH_NORTH                   32042
#define GEOTIFF_PCS_NAD27_UTAH_CENTRAL                 32043
#define GEOTIFF_PCS_NAD27_UTAH_SOUTH                   32044
#define GEOTIFF_PCS_NAD27_VERMONT                      32045
#define GEOTIFF_PCS_NAD27_VIRGINIA_NORTH               32046
#define GEOTIFF_PCS_NAD27_VIRGINIA_SOUTH               32047
#define GEOTIFF_PCS_NAD27_WASHINGTON_NORTH             32048
#define GEOTIFF_PCS_NAD27_WASHINGTON_SOUTH             32049
#define GEOTIFF_PCS_NAD27_WEST_VIRGINIA_N              32050
#define GEOTIFF_PCS_NAD27_WEST_VIRGINIA_S              32051
#define GEOTIFF_PCS_NAD27_WISCONSIN_NORTH              32052
#define GEOTIFF_PCS_NAD27_WISCONSIN_CEN                32053
#define GEOTIFF_PCS_NAD27_WISCONSIN_SOUTH              32054
#define GEOTIFF_PCS_NAD27_WYOMING_EAST                 32055
#define GEOTIFF_PCS_NAD27_WYOMING_E_CEN                32056
#define GEOTIFF_PCS_NAD27_WYOMING_W_CEN                32057
#define GEOTIFF_PCS_NAD27_WYOMING_WEST                 32058
#define GEOTIFF_PCS_NAD27_PUERTO_RICO                  32059
#define GEOTIFF_PCS_NAD27_ST_CROIX                     32060
#define GEOTIFF_PCS_NAD83_MONTANA                      32100
#define GEOTIFF_PCS_NAD83_NEBRASKA                     32104
#define GEOTIFF_PCS_NAD83_NEVADA_EAST                  32107
#define GEOTIFF_PCS_NAD83_NEVADA_CENTRAL               32108
#define GEOTIFF_PCS_NAD83_NEVADA_WEST                  32109
#define GEOTIFF_PCS_NAD83_NEW_HAMPSHIRE                32110
#define GEOTIFF_PCS_NAD83_NEW_JERSEY                   32111
#define GEOTIFF_PCS_NAD83_NEW_MEXICO_EAST              32112
#define GEOTIFF_PCS_NAD83_NEW_MEXICO_CENT              32113
#define GEOTIFF_PCS_NAD83_NEW_MEXICO_WEST              32114
#define GEOTIFF_PCS_NAD83_NEW_YORK_EAST                32115
#define GEOTIFF_PCS_NAD83_NEW_YORK_CENTRAL             32116
#define GEOTIFF_PCS_NAD83_NEW_YORK_WEST                32117
#define GEOTIFF_PCS_NAD83_NEW_YORK_LONG_IS             32118
#define GEOTIFF_PCS_NAD83_NORTH_CAROLINA               32119
#define GEOTIFF_PCS_NAD83_NORTH_DAKOTA_N               32120
#define GEOTIFF_PCS_NAD83_NORTH_DAKOTA_S               32121
#define GEOTIFF_PCS_NAD83_OHIO_NORTH                   32122
#define GEOTIFF_PCS_NAD83_OHIO_SOUTH                   32123
#define GEOTIFF_PCS_NAD83_OKLAHOMA_NORTH               32124
#define GEOTIFF_PCS_NAD83_OKLAHOMA_SOUTH               32125
#define GEOTIFF_PCS_NAD83_OREGON_NORTH                 32126
#define GEOTIFF_PCS_NAD83_OREGON_SOUTH                 32127
#define GEOTIFF_PCS_NAD83_PENNSYLVANIA_N               32128
#define GEOTIFF_PCS_NAD83_PENNSYLVANIA_S               32129
#define GEOTIFF_PCS_NAD83_RHODE_ISLAND                 32130
#define GEOTIFF_PCS_NAD83_SOUTH_CAROLINA               32133
#define GEOTIFF_PCS_NAD83_SOUTH_DAKOTA_N               32134
#define GEOTIFF_PCS_NAD83_SOUTH_DAKOTA_S               32135
#define GEOTIFF_PCS_NAD83_TENNESSEE                    32136
#define GEOTIFF_PCS_NAD83_TEXAS_NORTH                  32137
#define GEOTIFF_PCS_NAD83_TEXAS_NORTH_CEN              32138
#define GEOTIFF_PCS_NAD83_TEXAS_CENTRAL                32139
#define GEOTIFF_PCS_NAD83_TEXAS_SOUTH_CEN              32140
#define GEOTIFF_PCS_NAD83_TEXAS_SOUTH                  32141
#define GEOTIFF_PCS_NAD83_UTAH_NORTH                   32142
#define GEOTIFF_PCS_NAD83_UTAH_CENTRAL                 32143
#define GEOTIFF_PCS_NAD83_UTAH_SOUTH                   32144
#define GEOTIFF_PCS_NAD83_VERMONT                      32145
#define GEOTIFF_PCS_NAD83_VIRGINIA_NORTH               32146
#define GEOTIFF_PCS_NAD83_VIRGINIA_SOUTH               32147
#define GEOTIFF_PCS_NAD83_WASHINGTON_NORTH             32148
#define GEOTIFF_PCS_NAD83_WASHINGTON_SOUTH             32149
#define GEOTIFF_PCS_NAD83_WEST_VIRGINIA_N              32150
#define GEOTIFF_PCS_NAD83_WEST_VIRGINIA_S              32151
#define GEOTIFF_PCS_NAD83_WISCONSIN_NORTH              32152
#define GEOTIFF_PCS_NAD83_WISCONSIN_CEN                32153
#define GEOTIFF_PCS_NAD83_WISCONSIN_SOUTH              32154
#define GEOTIFF_PCS_NAD83_WYOMING_EAST                 32155
#define GEOTIFF_PCS_NAD83_WYOMING_E_CEN                32156
#define GEOTIFF_PCS_NAD83_WYOMING_W_CEN                32157
#define GEOTIFF_PCS_NAD83_WYOMING_WEST                 32158
#define GEOTIFF_PCS_NAD83_PUERTO_RICO_VIRGIN_IS        32161
#define GEOTIFF_PCS_WGS72_UTM_ZONE_1N                  32201
#define GEOTIFF_PCS_WGS72_UTM_ZONE_2N                  32202
#define GEOTIFF_PCS_WGS72_UTM_ZONE_3N                  32203
#define GEOTIFF_PCS_WGS72_UTM_ZONE_4N                  32204
#define GEOTIFF_PCS_WGS72_UTM_ZONE_5N                  32205
#define GEOTIFF_PCS_WGS72_UTM_ZONE_6N                  32206
#define GEOTIFF_PCS_WGS72_UTM_ZONE_7N                  32207
#define GEOTIFF_PCS_WGS72_UTM_ZONE_8N                  32208
#define GEOTIFF_PCS_WGS72_UTM_ZONE_9N                  32209
#define GEOTIFF_PCS_WGS72_UTM_ZONE_10N                 32210
#define GEOTIFF_PCS_WGS72_UTM_ZONE_11N                 32211
#define GEOTIFF_PCS_WGS72_UTM_ZONE_12N                 32212
#define GEOTIFF_PCS_WGS72_UTM_ZONE_13N                 32213
#define GEOTIFF_PCS_WGS72_UTM_ZONE_14N                 32214
#define GEOTIFF_PCS_WGS72_UTM_ZONE_15N                 32215
#define GEOTIFF_PCS_WGS72_UTM_ZONE_16N                 32216
#define GEOTIFF_PCS_WGS72_UTM_ZONE_17N                 32217
#define GEOTIFF_PCS_WGS72_UTM_ZONE_18N                 32218
#define GEOTIFF_PCS_WGS72_UTM_ZONE_19N                 32219
#define GEOTIFF_PCS_WGS72_UTM_ZONE_20N                 32220
#define GEOTIFF_PCS_WGS72_UTM_ZONE_21N                 32221
#define GEOTIFF_PCS_WGS72_UTM_ZONE_22N                 32222
#define GEOTIFF_PCS_WGS72_UTM_ZONE_23N                 32223
#define GEOTIFF_PCS_WGS72_UTM_ZONE_24N                 32224
#define GEOTIFF_PCS_WGS72_UTM_ZONE_25N                 32225
#define GEOTIFF_PCS_WGS72_UTM_ZONE_26N                 32226
#define GEOTIFF_PCS_WGS72_UTM_ZONE_27N                 32227
#define GEOTIFF_PCS_WGS72_UTM_ZONE_28N                 32228
#define GEOTIFF_PCS_WGS72_UTM_ZONE_29N                 32229
#define GEOTIFF_PCS_WGS72_UTM_ZONE_30N                 32230
#define GEOTIFF_PCS_WGS72_UTM_ZONE_31N                 32231
#define GEOTIFF_PCS_WGS72_UTM_ZONE_32N                 32232
#define GEOTIFF_PCS_WGS72_UTM_ZONE_33N                 32233
#define GEOTIFF_PCS_WGS72_UTM_ZONE_34N                 32234
#define GEOTIFF_PCS_WGS72_UTM_ZONE_35N                 32235
#define GEOTIFF_PCS_WGS72_UTM_ZONE_36N                 32236
#define GEOTIFF_PCS_WGS72_UTM_ZONE_37N                 32237
#define GEOTIFF_PCS_WGS72_UTM_ZONE_38N                 32238
#define GEOTIFF_PCS_WGS72_UTM_ZONE_39N                 32239
#define GEOTIFF_PCS_WGS72_UTM_ZONE_40N                 32240
#define GEOTIFF_PCS_WGS72_UTM_ZONE_41N                 32241
#define GEOTIFF_PCS_WGS72_UTM_ZONE_42N                 32242
#define GEOTIFF_PCS_WGS72_UTM_ZONE_43N                 32243
#define GEOTIFF_PCS_WGS72_UTM_ZONE_44N                 32244
#define GEOTIFF_PCS_WGS72_UTM_ZONE_45N                 32245
#define GEOTIFF_PCS_WGS72_UTM_ZONE_46N                 32246
#define GEOTIFF_PCS_WGS72_UTM_ZONE_47N                 32247
#define GEOTIFF_PCS_WGS72_UTM_ZONE_48N                 32248
#define GEOTIFF_PCS_WGS72_UTM_ZONE_49N                 32249
#define GEOTIFF_PCS_WGS72_UTM_ZONE_50N                 32250
#define GEOTIFF_PCS_WGS72_UTM_ZONE_51N                 32251
#define GEOTIFF_PCS_WGS72_UTM_ZONE_52N                 32252
#define GEOTIFF_PCS_WGS72_UTM_ZONE_53N                 32253
#define GEOTIFF_PCS_WGS72_UTM_ZONE_54N                 32254
#define GEOTIFF_PCS_WGS72_UTM_ZONE_55N                 32255
#define GEOTIFF_PCS_WGS72_UTM_ZONE_56N                 32256
#define GEOTIFF_PCS_WGS72_UTM_ZONE_57N                 32257
#define GEOTIFF_PCS_WGS72_UTM_ZONE_58N                 32258
#define GEOTIFF_PCS_WGS72_UTM_ZONE_59N                 32259
#define GEOTIFF_PCS_WGS72_UTM_ZONE_60N                 32260
#define GEOTIFF_PCS_WGS72_UTM_ZONE_1S                  32301
#define GEOTIFF_PCS_WGS72_UTM_ZONE_2S                  32302
#define GEOTIFF_PCS_WGS72_UTM_ZONE_3S                  32303
#define GEOTIFF_PCS_WGS72_UTM_ZONE_4S                  32304
#define GEOTIFF_PCS_WGS72_UTM_ZONE_5S                  32305
#define GEOTIFF_PCS_WGS72_UTM_ZONE_6S                  32306
#define GEOTIFF_PCS_WGS72_UTM_ZONE_7S                  32307
#define GEOTIFF_PCS_WGS72_UTM_ZONE_8S                  32308
#define GEOTIFF_PCS_WGS72_UTM_ZONE_9S                  32309
#define GEOTIFF_PCS_WGS72_UTM_ZONE_10S                 32310
#define GEOTIFF_PCS_WGS72_UTM_ZONE_11S                 32311
#define GEOTIFF_PCS_WGS72_UTM_ZONE_12S                 32312
#define GEOTIFF_PCS_WGS72_UTM_ZONE_13S                 32313
#define GEOTIFF_PCS_WGS72_UTM_ZONE_14S                 32314
#define GEOTIFF_PCS_WGS72_UTM_ZONE_15S                 32315
#define GEOTIFF_PCS_WGS72_UTM_ZONE_16S                 32316
#define GEOTIFF_PCS_WGS72_UTM_ZONE_17S                 32317
#define GEOTIFF_PCS_WGS72_UTM_ZONE_18S                 32318
#define GEOTIFF_PCS_WGS72_UTM_ZONE_19S                 32319
#define GEOTIFF_PCS_WGS72_UTM_ZONE_20S                 32320
#define GEOTIFF_PCS_WGS72_UTM_ZONE_21S                 32321
#define GEOTIFF_PCS_WGS72_UTM_ZONE_22S                 32322
#define GEOTIFF_PCS_WGS72_UTM_ZONE_23S                 32323
#define GEOTIFF_PCS_WGS72_UTM_ZONE_24S                 32324
#define GEOTIFF_PCS_WGS72_UTM_ZONE_25S                 32325
#define GEOTIFF_PCS_WGS72_UTM_ZONE_26S                 32326
#define GEOTIFF_PCS_WGS72_UTM_ZONE_27S                 32327
#define GEOTIFF_PCS_WGS72_UTM_ZONE_28S                 32328
#define GEOTIFF_PCS_WGS72_UTM_ZONE_29S                 32329
#define GEOTIFF_PCS_WGS72_UTM_ZONE_30S                 32330
#define GEOTIFF_PCS_WGS72_UTM_ZONE_31S                 32331
#define GEOTIFF_PCS_WGS72_UTM_ZONE_32S                 32332
#define GEOTIFF_PCS_WGS72_UTM_ZONE_33S                 32333
#define GEOTIFF_PCS_WGS72_UTM_ZONE_34S                 32334
#define GEOTIFF_PCS_WGS72_UTM_ZONE_35S                 32335
#define GEOTIFF_PCS_WGS72_UTM_ZONE_36S                 32336
#define GEOTIFF_PCS_WGS72_UTM_ZONE_37S                 32337
#define GEOTIFF_PCS_WGS72_UTM_ZONE_38S                 32338
#define GEOTIFF_PCS_WGS72_UTM_ZONE_39S                 32339
#define GEOTIFF_PCS_WGS72_UTM_ZONE_40S                 32340
#define GEOTIFF_PCS_WGS72_UTM_ZONE_41S                 32341
#define GEOTIFF_PCS_WGS72_UTM_ZONE_42S                 32342
#define GEOTIFF_PCS_WGS72_UTM_ZONE_43S                 32343
#define GEOTIFF_PCS_WGS72_UTM_ZONE_44S                 32344
#define GEOTIFF_PCS_WGS72_UTM_ZONE_45S                 32345
#define GEOTIFF_PCS_WGS72_UTM_ZONE_46S                 32346
#define GEOTIFF_PCS_WGS72_UTM_ZONE_47S                 32347
#define GEOTIFF_PCS_WGS72_UTM_ZONE_48S                 32348
#define GEOTIFF_PCS_WGS72_UTM_ZONE_49S                 32349
#define GEOTIFF_PCS_WGS72_UTM_ZONE_50S                 32350
#define GEOTIFF_PCS_WGS72_UTM_ZONE_51S                 32351
#define GEOTIFF_PCS_WGS72_UTM_ZONE_52S                 32352
#define GEOTIFF_PCS_WGS72_UTM_ZONE_53S                 32353
#define GEOTIFF_PCS_WGS72_UTM_ZONE_54S                 32354
#define GEOTIFF_PCS_WGS72_UTM_ZONE_55S                 32355
#define GEOTIFF_PCS_WGS72_UTM_ZONE_56S                 32356
#define GEOTIFF_PCS_WGS72_UTM_ZONE_57S                 32357
#define GEOTIFF_PCS_WGS72_UTM_ZONE_58S                 32358
#define GEOTIFF_PCS_WGS72_UTM_ZONE_59S                 32359
#define GEOTIFF_PCS_WGS72_UTM_ZONE_60S                 32360
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_1N                32401
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_2N                32402
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_3N                32403
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_4N                32404
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_5N                32405
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_6N                32406
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_7N                32407
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_8N                32408
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_9N                32409
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_10N               32410
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_11N               32411
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_12N               32412
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_13N               32413
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_14N               32414
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_15N               32415
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_16N               32416
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_17N               32417
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_18N               32418
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_19N               32419
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_20N               32420
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_21N               32421
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_22N               32422
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_23N               32423
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_24N               32424
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_25N               32425
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_26N               32426
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_27N               32427
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_28N               32428
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_29N               32429
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_30N               32430
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_31N               32431
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_32N               32432
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_33N               32433
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_34N               32434
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_35N               32435
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_36N               32436
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_37N               32437
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_38N               32438
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_39N               32439
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_40N               32440
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_41N               32441
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_42N               32442
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_43N               32443
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_44N               32444
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_45N               32445
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_46N               32446
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_47N               32447
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_48N               32448
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_49N               32449
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_50N               32450
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_51N               32451
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_52N               32452
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_53N               32453
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_54N               32454
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_55N               32455
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_56N               32456
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_57N               32457
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_58N               32458
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_59N               32459
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_60N               32460
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_1S                32501
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_2S                32502
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_3S                32503
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_4S                32504
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_5S                32505
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_6S                32506
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_7S                32507
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_8S                32508
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_9S                32509
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_10S               32510
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_11S               32511
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_12S               32512
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_13S               32513
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_14S               32514
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_15S               32515
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_16S               32516
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_17S               32517
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_18S               32518
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_19S               32519
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_20S               32520
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_21S               32521
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_22S               32522
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_23S               32523
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_24S               32524
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_25S               32525
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_26S               32526
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_27S               32527
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_28S               32528
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_29S               32529
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_30S               32530
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_31S               32531
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_32S               32532
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_33S               32533
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_34S               32534
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_35S               32535
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_36S               32536
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_37S               32537
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_38S               32538
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_39S               32539
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_40S               32540
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_41S               32541
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_42S               32542
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_43S               32543
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_44S               32544
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_45S               32545
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_46S               32546
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_47S               32547
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_48S               32548
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_49S               32549
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_50S               32550
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_51S               32551
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_52S               32552
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_53S               32553
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_54S               32554
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_55S               32555
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_56S               32556
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_57S               32557
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_58S               32558
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_59S               32559
#define GEOTIFF_PCS_WGS72BE_UTM_ZONE_60S               32560
#define GEOTIFF_PCS_WGS84_UTM_ZONE_1N                  32601
#define GEOTIFF_PCS_WGS84_UTM_ZONE_2N                  32602
#define GEOTIFF_PCS_WGS84_UTM_ZONE_3N                  32603
#define GEOTIFF_PCS_WGS84_UTM_ZONE_4N                  32604
#define GEOTIFF_PCS_WGS84_UTM_ZONE_5N                  32605
#define GEOTIFF_PCS_WGS84_UTM_ZONE_6N                  32606
#define GEOTIFF_PCS_WGS84_UTM_ZONE_7N                  32607
#define GEOTIFF_PCS_WGS84_UTM_ZONE_8N                  32608
#define GEOTIFF_PCS_WGS84_UTM_ZONE_9N                  32609
#define GEOTIFF_PCS_WGS84_UTM_ZONE_10N                 32610
#define GEOTIFF_PCS_WGS84_UTM_ZONE_11N                 32611
#define GEOTIFF_PCS_WGS84_UTM_ZONE_12N                 32612
#define GEOTIFF_PCS_WGS84_UTM_ZONE_13N                 32613
#define GEOTIFF_PCS_WGS84_UTM_ZONE_14N                 32614
#define GEOTIFF_PCS_WGS84_UTM_ZONE_15N                 32615
#define GEOTIFF_PCS_WGS84_UTM_ZONE_16N                 32616
#define GEOTIFF_PCS_WGS84_UTM_ZONE_17N                 32617
#define GEOTIFF_PCS_WGS84_UTM_ZONE_18N                 32618
#define GEOTIFF_PCS_WGS84_UTM_ZONE_19N                 32619
#define GEOTIFF_PCS_WGS84_UTM_ZONE_20N                 32620
#define GEOTIFF_PCS_WGS84_UTM_ZONE_21N                 32621
#define GEOTIFF_PCS_WGS84_UTM_ZONE_22N                 32622
#define GEOTIFF_PCS_WGS84_UTM_ZONE_23N                 32623
#define GEOTIFF_PCS_WGS84_UTM_ZONE_24N                 32624
#define GEOTIFF_PCS_WGS84_UTM_ZONE_25N                 32625
#define GEOTIFF_PCS_WGS84_UTM_ZONE_26N                 32626
#define GEOTIFF_PCS_WGS84_UTM_ZONE_27N                 32627
#define GEOTIFF_PCS_WGS84_UTM_ZONE_28N                 32628
#define GEOTIFF_PCS_WGS84_UTM_ZONE_29N                 32629
#define GEOTIFF_PCS_WGS84_UTM_ZONE_30N                 32630
#define GEOTIFF_PCS_WGS84_UTM_ZONE_31N                 32631
#define GEOTIFF_PCS_WGS84_UTM_ZONE_32N                 32632
#define GEOTIFF_PCS_WGS84_UTM_ZONE_33N                 32633
#define GEOTIFF_PCS_WGS84_UTM_ZONE_34N                 32634
#define GEOTIFF_PCS_WGS84_UTM_ZONE_35N                 32635
#define GEOTIFF_PCS_WGS84_UTM_ZONE_36N                 32636
#define GEOTIFF_PCS_WGS84_UTM_ZONE_37N                 32637
#define GEOTIFF_PCS_WGS84_UTM_ZONE_38N                 32638
#define GEOTIFF_PCS_WGS84_UTM_ZONE_39N                 32639
#define GEOTIFF_PCS_WGS84_UTM_ZONE_40N                 32640
#define GEOTIFF_PCS_WGS84_UTM_ZONE_41N                 32641
#define GEOTIFF_PCS_WGS84_UTM_ZONE_42N                 32642
#define GEOTIFF_PCS_WGS84_UTM_ZONE_43N                 32643
#define GEOTIFF_PCS_WGS84_UTM_ZONE_44N                 32644
#define GEOTIFF_PCS_WGS84_UTM_ZONE_45N                 32645
#define GEOTIFF_PCS_WGS84_UTM_ZONE_46N                 32646
#define GEOTIFF_PCS_WGS84_UTM_ZONE_47N                 32647
#define GEOTIFF_PCS_WGS84_UTM_ZONE_48N                 32648
#define GEOTIFF_PCS_WGS84_UTM_ZONE_49N                 32649
#define GEOTIFF_PCS_WGS84_UTM_ZONE_50N                 32650
#define GEOTIFF_PCS_WGS84_UTM_ZONE_51N                 32651
#define GEOTIFF_PCS_WGS84_UTM_ZONE_52N                 32652
#define GEOTIFF_PCS_WGS84_UTM_ZONE_53N                 32653
#define GEOTIFF_PCS_WGS84_UTM_ZONE_54N                 32654
#define GEOTIFF_PCS_WGS84_UTM_ZONE_55N                 32655
#define GEOTIFF_PCS_WGS84_UTM_ZONE_56N                 32656
#define GEOTIFF_PCS_WGS84_UTM_ZONE_57N                 32657
#define GEOTIFF_PCS_WGS84_UTM_ZONE_58N                 32658
#define GEOTIFF_PCS_WGS84_UTM_ZONE_59N                 32659
#define GEOTIFF_PCS_WGS84_UTM_ZONE_60N                 32660
#define GEOTIFF_PCS_WGS84_UTM_ZONE_1S                  32701
#define GEOTIFF_PCS_WGS84_UTM_ZONE_2S                  32702
#define GEOTIFF_PCS_WGS84_UTM_ZONE_3S                  32703
#define GEOTIFF_PCS_WGS84_UTM_ZONE_4S                  32704
#define GEOTIFF_PCS_WGS84_UTM_ZONE_5S                  32705
#define GEOTIFF_PCS_WGS84_UTM_ZONE_6S                  32706
#define GEOTIFF_PCS_WGS84_UTM_ZONE_7S                  32707
#define GEOTIFF_PCS_WGS84_UTM_ZONE_8S                  32708
#define GEOTIFF_PCS_WGS84_UTM_ZONE_9S                  32709
#define GEOTIFF_PCS_WGS84_UTM_ZONE_10S                 32710
#define GEOTIFF_PCS_WGS84_UTM_ZONE_11S                 32711
#define GEOTIFF_PCS_WGS84_UTM_ZONE_12S                 32712
#define GEOTIFF_PCS_WGS84_UTM_ZONE_13S                 32713
#define GEOTIFF_PCS_WGS84_UTM_ZONE_14S                 32714
#define GEOTIFF_PCS_WGS84_UTM_ZONE_15S                 32715
#define GEOTIFF_PCS_WGS84_UTM_ZONE_16S                 32716
#define GEOTIFF_PCS_WGS84_UTM_ZONE_17S                 32717
#define GEOTIFF_PCS_WGS84_UTM_ZONE_18S                 32718
#define GEOTIFF_PCS_WGS84_UTM_ZONE_19S                 32719
#define GEOTIFF_PCS_WGS84_UTM_ZONE_20S                 32720
#define GEOTIFF_PCS_WGS84_UTM_ZONE_21S                 32721
#define GEOTIFF_PCS_WGS84_UTM_ZONE_22S                 32722
#define GEOTIFF_PCS_WGS84_UTM_ZONE_23S                 32723
#define GEOTIFF_PCS_WGS84_UTM_ZONE_24S                 32724
#define GEOTIFF_PCS_WGS84_UTM_ZONE_25S                 32725
#define GEOTIFF_PCS_WGS84_UTM_ZONE_26S                 32726
#define GEOTIFF_PCS_WGS84_UTM_ZONE_27S                 32727
#define GEOTIFF_PCS_WGS84_UTM_ZONE_28S                 32728
#define GEOTIFF_PCS_WGS84_UTM_ZONE_29S                 32729
#define GEOTIFF_PCS_WGS84_UTM_ZONE_30S                 32730
#define GEOTIFF_PCS_WGS84_UTM_ZONE_31S                 32731
#define GEOTIFF_PCS_WGS84_UTM_ZONE_32S                 32732
#define GEOTIFF_PCS_WGS84_UTM_ZONE_33S                 32733
#define GEOTIFF_PCS_WGS84_UTM_ZONE_34S                 32734
#define GEOTIFF_PCS_WGS84_UTM_ZONE_35S                 32735
#define GEOTIFF_PCS_WGS84_UTM_ZONE_36S                 32736
#define GEOTIFF_PCS_WGS84_UTM_ZONE_37S                 32737
#define GEOTIFF_PCS_WGS84_UTM_ZONE_38S                 32738
#define GEOTIFF_PCS_WGS84_UTM_ZONE_39S                 32739
#define GEOTIFF_PCS_WGS84_UTM_ZONE_40S                 32740
#define GEOTIFF_PCS_WGS84_UTM_ZONE_41S                 32741
#define GEOTIFF_PCS_WGS84_UTM_ZONE_42S                 32742
#define GEOTIFF_PCS_WGS84_UTM_ZONE_43S                 32743
#define GEOTIFF_PCS_WGS84_UTM_ZONE_44S                 32744
#define GEOTIFF_PCS_WGS84_UTM_ZONE_45S                 32745
#define GEOTIFF_PCS_WGS84_UTM_ZONE_46S                 32746
#define GEOTIFF_PCS_WGS84_UTM_ZONE_47S                 32747
#define GEOTIFF_PCS_WGS84_UTM_ZONE_48S                 32748
#define GEOTIFF_PCS_WGS84_UTM_ZONE_49S                 32749
#define GEOTIFF_PCS_WGS84_UTM_ZONE_50S                 32750
#define GEOTIFF_PCS_WGS84_UTM_ZONE_51S                 32751
#define GEOTIFF_PCS_WGS84_UTM_ZONE_52S                 32752
#define GEOTIFF_PCS_WGS84_UTM_ZONE_53S                 32753
#define GEOTIFF_PCS_WGS84_UTM_ZONE_54S                 32754
#define GEOTIFF_PCS_WGS84_UTM_ZONE_55S                 32755
#define GEOTIFF_PCS_WGS84_UTM_ZONE_56S                 32756
#define GEOTIFF_PCS_WGS84_UTM_ZONE_57S                 32757
#define GEOTIFF_PCS_WGS84_UTM_ZONE_58S                 32758
#define GEOTIFF_PCS_WGS84_UTM_ZONE_59S                 32759
#define GEOTIFF_PCS_WGS84_UTM_ZONE_60S                 32760

#define GEOTIFF_PROJ_ALABAMA_CS27_EAST                    10101
#define GEOTIFF_PROJ_ALABAMA_CS27_WEST                    10102
#define GEOTIFF_PROJ_ALABAMA_CS83_EAST                    10131
#define GEOTIFF_PROJ_ALABAMA_CS83_WEST                    10132
#define GEOTIFF_PROJ_ARIZONA_COORDINATE_SYSTEM_EAST       10201
#define GEOTIFF_PROJ_ARIZONA_COORDINATE_SYSTEM_CENTRAL    10202
#define GEOTIFF_PROJ_ARIZONA_COORDINATE_SYSTEM_WEST       10203
#define GEOTIFF_PROJ_ARIZONA_CS83_EAST                    10231
#define GEOTIFF_PROJ_ARIZONA_CS83_CENTRAL                 10232
#define GEOTIFF_PROJ_ARIZONA_CS83_WEST                    10233
#define GEOTIFF_PROJ_ARKANSAS_CS27_NORTH                  10301
#define GEOTIFF_PROJ_ARKANSAS_CS27_SOUTH                  10302
#define GEOTIFF_PROJ_ARKANSAS_CS83_NORTH                  10331
#define GEOTIFF_PROJ_ARKANSAS_CS83_SOUTH                  10332
#define GEOTIFF_PROJ_CALIFORNIA_CS27_I                    10401
#define GEOTIFF_PROJ_CALIFORNIA_CS27_II                   10402
#define GEOTIFF_PROJ_CALIFORNIA_CS27_III                  10403
#define GEOTIFF_PROJ_CALIFORNIA_CS27_IV                   10404
#define GEOTIFF_PROJ_CALIFORNIA_CS27_V                    10405
#define GEOTIFF_PROJ_CALIFORNIA_CS27_VI                   10406
#define GEOTIFF_PROJ_CALIFORNIA_CS27_VII                  10407
#define GEOTIFF_PROJ_CALIFORNIA_CS83_1                    10431
#define GEOTIFF_PROJ_CALIFORNIA_CS83_2                    10432
#define GEOTIFF_PROJ_CALIFORNIA_CS83_3                    10433
#define GEOTIFF_PROJ_CALIFORNIA_CS83_4                    10434
#define GEOTIFF_PROJ_CALIFORNIA_CS83_5                    10435
#define GEOTIFF_PROJ_CALIFORNIA_CS83_6                    10436
#define GEOTIFF_PROJ_COLORADO_CS27_NORTH                  10501
#define GEOTIFF_PROJ_COLORADO_CS27_CENTRAL                10502
#define GEOTIFF_PROJ_COLORADO_CS27_SOUTH                  10503
#define GEOTIFF_PROJ_COLORADO_CS83_NORTH                  10531
#define GEOTIFF_PROJ_COLORADO_CS83_CENTRAL                10532
#define GEOTIFF_PROJ_COLORADO_CS83_SOUTH                  10533
#define GEOTIFF_PROJ_CONNECTICUT_CS27                     10600
#define GEOTIFF_PROJ_CONNECTICUT_CS83                     10630
#define GEOTIFF_PROJ_DELAWARE_CS27                        10700
#define GEOTIFF_PROJ_DELAWARE_CS83                        10730
#define GEOTIFF_PROJ_FLORIDA_CS27_EAST                    10901
#define GEOTIFF_PROJ_FLORIDA_CS27_WEST                    10902
#define GEOTIFF_PROJ_FLORIDA_CS27_NORTH                   10903
#define GEOTIFF_PROJ_FLORIDA_CS83_EAST                    10931
#define GEOTIFF_PROJ_FLORIDA_CS83_WEST                    10932
#define GEOTIFF_PROJ_FLORIDA_CS83_NORTH                   10933
#define GEOTIFF_PROJ_GEORGIA_CS27_EAST                    11001
#define GEOTIFF_PROJ_GEORGIA_CS27_WEST                    11002
#define GEOTIFF_PROJ_GEORGIA_CS83_EAST                    11031
#define GEOTIFF_PROJ_GEORGIA_CS83_WEST                    11032
#define GEOTIFF_PROJ_IDAHO_CS27_EAST                      11101
#define GEOTIFF_PROJ_IDAHO_CS27_CENTRAL                   11102
#define GEOTIFF_PROJ_IDAHO_CS27_WEST                      11103
#define GEOTIFF_PROJ_IDAHO_CS83_EAST                      11131
#define GEOTIFF_PROJ_IDAHO_CS83_CENTRAL                   11132
#define GEOTIFF_PROJ_IDAHO_CS83_WEST                      11133
#define GEOTIFF_PROJ_ILLINOIS_CS27_EAST                   11201
#define GEOTIFF_PROJ_ILLINOIS_CS27_WEST                   11202
#define GEOTIFF_PROJ_ILLINOIS_CS83_EAST                   11231
#define GEOTIFF_PROJ_ILLINOIS_CS83_WEST                   11232
#define GEOTIFF_PROJ_INDIANA_CS27_EAST                    11301
#define GEOTIFF_PROJ_INDIANA_CS27_WEST                    11302
#define GEOTIFF_PROJ_INDIANA_CS83_EAST                    11331
#define GEOTIFF_PROJ_INDIANA_CS83_WEST                    11332
#define GEOTIFF_PROJ_IOWA_CS27_NORTH                      11401
#define GEOTIFF_PROJ_IOWA_CS27_SOUTH                      11402
#define GEOTIFF_PROJ_IOWA_CS83_NORTH                      11431
#define GEOTIFF_PROJ_IOWA_CS83_SOUTH                      11432
#define GEOTIFF_PROJ_KANSAS_CS27_NORTH                    11501
#define GEOTIFF_PROJ_KANSAS_CS27_SOUTH                    11502
#define GEOTIFF_PROJ_KANSAS_CS83_NORTH                    11531
#define GEOTIFF_PROJ_KANSAS_CS83_SOUTH                    11532
#define GEOTIFF_PROJ_KENTUCKY_CS27_NORTH                  11601
#define GEOTIFF_PROJ_KENTUCKY_CS27_SOUTH                  11602
#define GEOTIFF_PROJ_KENTUCKY_CS83_NORTH                  11631
#define GEOTIFF_PROJ_KENTUCKY_CS83_SOUTH                  11632
#define GEOTIFF_PROJ_LOUISIANA_CS27_NORTH                 11701
#define GEOTIFF_PROJ_LOUISIANA_CS27_SOUTH                 11702
#define GEOTIFF_PROJ_LOUISIANA_CS83_NORTH                 11731
#define GEOTIFF_PROJ_LOUISIANA_CS83_SOUTH                 11732
#define GEOTIFF_PROJ_MAINE_CS27_EAST                      11801
#define GEOTIFF_PROJ_MAINE_CS27_WEST                      11802
#define GEOTIFF_PROJ_MAINE_CS83_EAST                      11831
#define GEOTIFF_PROJ_MAINE_CS83_WEST                      11832
#define GEOTIFF_PROJ_MARYLAND_CS27                        11900
#define GEOTIFF_PROJ_MARYLAND_CS83                        11930
#define GEOTIFF_PROJ_MASSACHUSETTS_CS27_MAINLAND          12001
#define GEOTIFF_PROJ_MASSACHUSETTS_CS27_ISLAND            12002
#define GEOTIFF_PROJ_MASSACHUSETTS_CS83_MAINLAND          12031
#define GEOTIFF_PROJ_MASSACHUSETTS_CS83_ISLAND            12032
#define GEOTIFF_PROJ_MICHIGAN_STATE_PLANE_EAST            12101
#define GEOTIFF_PROJ_MICHIGAN_STATE_PLANE_OLD_CENTRAL     12102
#define GEOTIFF_PROJ_MICHIGAN_STATE_PLANE_WEST            12103
#define GEOTIFF_PROJ_MICHIGAN_CS27_NORTH                  12111
#define GEOTIFF_PROJ_MICHIGAN_CS27_CENTRAL                12112
#define GEOTIFF_PROJ_MICHIGAN_CS27_SOUTH                  12113
#define GEOTIFF_PROJ_MICHIGAN_CS83_NORTH                  12141
#define GEOTIFF_PROJ_MICHIGAN_CS83_CENTRAL                12142
#define GEOTIFF_PROJ_MICHIGAN_CS83_SOUTH                  12143
#define GEOTIFF_PROJ_MINNESOTA_CS27_NORTH                 12201
#define GEOTIFF_PROJ_MINNESOTA_CS27_CENTRAL               12202
#define GEOTIFF_PROJ_MINNESOTA_CS27_SOUTH                 12203
#define GEOTIFF_PROJ_MINNESOTA_CS83_NORTH                 12231
#define GEOTIFF_PROJ_MINNESOTA_CS83_CENTRAL               12232
#define GEOTIFF_PROJ_MINNESOTA_CS83_SOUTH                 12233
#define GEOTIFF_PROJ_MISSISSIPPI_CS27_EAST                12301
#define GEOTIFF_PROJ_MISSISSIPPI_CS27_WEST                12302
#define GEOTIFF_PROJ_MISSISSIPPI_CS83_EAST                12331
#define GEOTIFF_PROJ_MISSISSIPPI_CS83_WEST                12332
#define GEOTIFF_PROJ_MISSOURI_CS27_EAST                   12401
#define GEOTIFF_PROJ_MISSOURI_CS27_CENTRAL                12402
#define GEOTIFF_PROJ_MISSOURI_CS27_WEST                   12403
#define GEOTIFF_PROJ_MISSOURI_CS83_EAST                   12431
#define GEOTIFF_PROJ_MISSOURI_CS83_CENTRAL                12432
#define GEOTIFF_PROJ_MISSOURI_CS83_WEST                   12433
#define GEOTIFF_PROJ_MONTANA_CS27_NORTH                   12501
#define GEOTIFF_PROJ_MONTANA_CS27_CENTRAL                 12502
#define GEOTIFF_PROJ_MONTANA_CS27_SOUTH                   12503
#define GEOTIFF_PROJ_MONTANA_CS83                         12530
#define GEOTIFF_PROJ_NEBRASKA_CS27_NORTH                  12601
#define GEOTIFF_PROJ_NEBRASKA_CS27_SOUTH                  12602
#define GEOTIFF_PROJ_NEBRASKA_CS83                        12630
#define GEOTIFF_PROJ_NEVADA_CS27_EAST                     12701
#define GEOTIFF_PROJ_NEVADA_CS27_CENTRAL                  12702
#define GEOTIFF_PROJ_NEVADA_CS27_WEST                     12703
#define GEOTIFF_PROJ_NEVADA_CS83_EAST                     12731
#define GEOTIFF_PROJ_NEVADA_CS83_CENTRAL                  12732
#define GEOTIFF_PROJ_NEVADA_CS83_WEST                     12733
#define GEOTIFF_PROJ_NEW_HAMPSHIRE_CS27                   12800
#define GEOTIFF_PROJ_NEW_HAMPSHIRE_CS83                   12830
#define GEOTIFF_PROJ_NEW_JERSEY_CS27                      12900
#define GEOTIFF_PROJ_NEW_JERSEY_CS83                      12930
#define GEOTIFF_PROJ_NEW_MEXICO_CS27_EAST                 13001
#define GEOTIFF_PROJ_NEW_MEXICO_CS27_CENTRAL              13002
#define GEOTIFF_PROJ_NEW_MEXICO_CS27_WEST                 13003
#define GEOTIFF_PROJ_NEW_MEXICO_CS83_EAST                 13031
#define GEOTIFF_PROJ_NEW_MEXICO_CS83_CENTRAL              13032
#define GEOTIFF_PROJ_NEW_MEXICO_CS83_WEST                 13033
#define GEOTIFF_PROJ_NEW_YORK_CS27_EAST                   13101
#define GEOTIFF_PROJ_NEW_YORK_CS27_CENTRAL                13102
#define GEOTIFF_PROJ_NEW_YORK_CS27_WEST                   13103
#define GEOTIFF_PROJ_NEW_YORK_CS27_LONG_ISLAND            13104
#define GEOTIFF_PROJ_NEW_YORK_CS83_EAST                   13131
#define GEOTIFF_PROJ_NEW_YORK_CS83_CENTRAL                13132
#define GEOTIFF_PROJ_NEW_YORK_CS83_WEST                   13133
#define GEOTIFF_PROJ_NEW_YORK_CS83_LONG_ISLAND            13134
#define GEOTIFF_PROJ_NORTH_CAROLINA_CS27                  13200
#define GEOTIFF_PROJ_NORTH_CAROLINA_CS83                  13230
#define GEOTIFF_PROJ_NORTH_DAKOTA_CS27_NORTH              13301
#define GEOTIFF_PROJ_NORTH_DAKOTA_CS27_SOUTH              13302
#define GEOTIFF_PROJ_NORTH_DAKOTA_CS83_NORTH              13331
#define GEOTIFF_PROJ_NORTH_DAKOTA_CS83_SOUTH              13332
#define GEOTIFF_PROJ_OHIO_CS27_NORTH                      13401
#define GEOTIFF_PROJ_OHIO_CS27_SOUTH                      13402
#define GEOTIFF_PROJ_OHIO_CS83_NORTH                      13431
#define GEOTIFF_PROJ_OHIO_CS83_SOUTH                      13432
#define GEOTIFF_PROJ_OKLAHOMA_CS27_NORTH                  13501
#define GEOTIFF_PROJ_OKLAHOMA_CS27_SOUTH                  13502
#define GEOTIFF_PROJ_OKLAHOMA_CS83_NORTH                  13531
#define GEOTIFF_PROJ_OKLAHOMA_CS83_SOUTH                  13532
#define GEOTIFF_PROJ_OREGON_CS27_NORTH                    13601
#define GEOTIFF_PROJ_OREGON_CS27_SOUTH                    13602
#define GEOTIFF_PROJ_OREGON_CS83_NORTH                    13631
#define GEOTIFF_PROJ_OREGON_CS83_SOUTH                    13632
#define GEOTIFF_PROJ_PENNSYLVANIA_CS27_NORTH              13701
#define GEOTIFF_PROJ_PENNSYLVANIA_CS27_SOUTH              13702
#define GEOTIFF_PROJ_PENNSYLVANIA_CS83_NORTH              13731
#define GEOTIFF_PROJ_PENNSYLVANIA_CS83_SOUTH              13732
#define GEOTIFF_PROJ_RHODE_ISLAND_CS27                    13800
#define GEOTIFF_PROJ_RHODE_ISLAND_CS83                    13830
#define GEOTIFF_PROJ_SOUTH_CAROLINA_CS27_NORTH            13901
#define GEOTIFF_PROJ_SOUTH_CAROLINA_CS27_SOUTH            13902
#define GEOTIFF_PROJ_SOUTH_CAROLINA_CS83                  13930
#define GEOTIFF_PROJ_SOUTH_DAKOTA_CS27_NORTH              14001
#define GEOTIFF_PROJ_SOUTH_DAKOTA_CS27_SOUTH              14002
#define GEOTIFF_PROJ_SOUTH_DAKOTA_CS83_NORTH              14031
#define GEOTIFF_PROJ_SOUTH_DAKOTA_CS83_SOUTH              14032
#define GEOTIFF_PROJ_TENNESSEE_CS27                       14100
#define GEOTIFF_PROJ_TENNESSEE_CS83                       14130
#define GEOTIFF_PROJ_TEXAS_CS27_NORTH                     14201
#define GEOTIFF_PROJ_TEXAS_CS27_NORTH_CENTRAL             14202
#define GEOTIFF_PROJ_TEXAS_CS27_CENTRAL                   14203
#define GEOTIFF_PROJ_TEXAS_CS27_SOUTH_CENTRAL             14204
#define GEOTIFF_PROJ_TEXAS_CS27_SOUTH                     14205
#define GEOTIFF_PROJ_TEXAS_CS83_NORTH                     14231
#define GEOTIFF_PROJ_TEXAS_CS83_NORTH_CENTRAL             14232
#define GEOTIFF_PROJ_TEXAS_CS83_CENTRAL                   14233
#define GEOTIFF_PROJ_TEXAS_CS83_SOUTH_CENTRAL             14234
#define GEOTIFF_PROJ_TEXAS_CS83_SOUTH                     14235
#define GEOTIFF_PROJ_UTAH_CS27_NORTH                      14301
#define GEOTIFF_PROJ_UTAH_CS27_CENTRAL                    14302
#define GEOTIFF_PROJ_UTAH_CS27_SOUTH                      14303
#define GEOTIFF_PROJ_UTAH_CS83_NORTH                      14331
#define GEOTIFF_PROJ_UTAH_CS83_CENTRAL                    14332
#define GEOTIFF_PROJ_UTAH_CS83_SOUTH                      14333
#define GEOTIFF_PROJ_VERMONT_CS27                         14400
#define GEOTIFF_PROJ_VERMONT_CS83                         14430
#define GEOTIFF_PROJ_VIRGINIA_CS27_NORTH                  14501
#define GEOTIFF_PROJ_VIRGINIA_CS27_SOUTH                  14502
#define GEOTIFF_PROJ_VIRGINIA_CS83_NORTH                  14531
#define GEOTIFF_PROJ_VIRGINIA_CS83_SOUTH                  14532
#define GEOTIFF_PROJ_WASHINGTON_CS27_NORTH                14601
#define GEOTIFF_PROJ_WASHINGTON_CS27_SOUTH                14602
#define GEOTIFF_PROJ_WASHINGTON_CS83_NORTH                14631
#define GEOTIFF_PROJ_WASHINGTON_CS83_SOUTH                14632
#define GEOTIFF_PROJ_WEST_VIRGINIA_CS27_NORTH             14701
#define GEOTIFF_PROJ_WEST_VIRGINIA_CS27_SOUTH             14702
#define GEOTIFF_PROJ_WEST_VIRGINIA_CS83_NORTH             14731
#define GEOTIFF_PROJ_WEST_VIRGINIA_CS83_SOUTH             14732
#define GEOTIFF_PROJ_WISCONSIN_CS27_NORTH                 14801
#define GEOTIFF_PROJ_WISCONSIN_CS27_CENTRAL               14802
#define GEOTIFF_PROJ_WISCONSIN_CS27_SOUTH                 14803
#define GEOTIFF_PROJ_WISCONSIN_CS83_NORTH                 14831
#define GEOTIFF_PROJ_WISCONSIN_CS83_CENTRAL               14832
#define GEOTIFF_PROJ_WISCONSIN_CS83_SOUTH                 14833
#define GEOTIFF_PROJ_WYOMING_CS27_EAST                    14901
#define GEOTIFF_PROJ_WYOMING_CS27_EAST_CENTRAL            14902
#define GEOTIFF_PROJ_WYOMING_CS27_WEST_CENTRAL            14903
#define GEOTIFF_PROJ_WYOMING_CS27_WEST                    14904
#define GEOTIFF_PROJ_WYOMING_CS83_EAST                    14931
#define GEOTIFF_PROJ_WYOMING_CS83_EAST_CENTRAL            14932
#define GEOTIFF_PROJ_WYOMING_CS83_WEST_CENTRAL            14933
#define GEOTIFF_PROJ_WYOMING_CS83_WEST                    14934
#define GEOTIFF_PROJ_ALASKA_CS27_1                        15001
#define GEOTIFF_PROJ_ALASKA_CS27_2                        15002
#define GEOTIFF_PROJ_ALASKA_CS27_3                        15003
#define GEOTIFF_PROJ_ALASKA_CS27_4                        15004
#define GEOTIFF_PROJ_ALASKA_CS27_5                        15005
#define GEOTIFF_PROJ_ALASKA_CS27_6                        15006
#define GEOTIFF_PROJ_ALASKA_CS27_7                        15007
#define GEOTIFF_PROJ_ALASKA_CS27_8                        15008
#define GEOTIFF_PROJ_ALASKA_CS27_9                        15009
#define GEOTIFF_PROJ_ALASKA_CS27_10                       15010
#define GEOTIFF_PROJ_ALASKA_CS83_1                        15031
#define GEOTIFF_PROJ_ALASKA_CS83_2                        15032
#define GEOTIFF_PROJ_ALASKA_CS83_3                        15033
#define GEOTIFF_PROJ_ALASKA_CS83_4                        15034
#define GEOTIFF_PROJ_ALASKA_CS83_5                        15035
#define GEOTIFF_PROJ_ALASKA_CS83_6                        15036
#define GEOTIFF_PROJ_ALASKA_CS83_7                        15037
#define GEOTIFF_PROJ_ALASKA_CS83_8                        15038
#define GEOTIFF_PROJ_ALASKA_CS83_9                        15039
#define GEOTIFF_PROJ_ALASKA_CS83_10                       15040
#define GEOTIFF_PROJ_HAWAII_CS27_1                        15101
#define GEOTIFF_PROJ_HAWAII_CS27_2                        15102
#define GEOTIFF_PROJ_HAWAII_CS27_3                        15103
#define GEOTIFF_PROJ_HAWAII_CS27_4                        15104
#define GEOTIFF_PROJ_HAWAII_CS27_5                        15105
#define GEOTIFF_PROJ_HAWAII_CS83_1                        15131
#define GEOTIFF_PROJ_HAWAII_CS83_2                        15132
#define GEOTIFF_PROJ_HAWAII_CS83_3                        15133
#define GEOTIFF_PROJ_HAWAII_CS83_4                        15134
#define GEOTIFF_PROJ_HAWAII_CS83_5                        15135
#define GEOTIFF_PROJ_PUERTO_RICO_CS27                     15201
#define GEOTIFF_PROJ_ST_CROIX                             15202
#define GEOTIFF_PROJ_PUERTO_RICO_VIRGIN_IS                15230
#define GEOTIFF_PROJ_BLM_14N_FEET                         15914
#define GEOTIFF_PROJ_BLM_15N_FEET                         15915
#define GEOTIFF_PROJ_BLM_16N_FEET                         15916
#define GEOTIFF_PROJ_BLM_17N_FEET                         15917
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_48             17348
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_49             17349
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_50             17350
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_51             17351
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_52             17352
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_53             17353
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_54             17354
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_55             17355
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_56             17356
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_57             17357
#define GEOTIFF_PROJ_MAP_GRID_OF_AUSTRALIA_58             17358
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_48               17448
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_49               17449
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_50               17450
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_51               17451
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_52               17452
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_53               17453
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_54               17454
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_55               17455
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_56               17456
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_57               17457
#define GEOTIFF_PROJ_AUSTRALIAN_MAP_GRID_58               17458
#define GEOTIFF_PROJ_ARGENTINA_1                          18031
#define GEOTIFF_PROJ_ARGENTINA_2                          18032
#define GEOTIFF_PROJ_ARGENTINA_3                          18033
#define GEOTIFF_PROJ_ARGENTINA_4                          18034
#define GEOTIFF_PROJ_ARGENTINA_5                          18035
#define GEOTIFF_PROJ_ARGENTINA_6                          18036
#define GEOTIFF_PROJ_ARGENTINA_7                          18037
#define GEOTIFF_PROJ_COLOMBIA_3W                          18051
#define GEOTIFF_PROJ_COLOMBIA_BOGOTA                      18052
#define GEOTIFF_PROJ_COLOMBIA_3E                          18053
#define GEOTIFF_PROJ_COLOMBIA_6E                          18054
#define GEOTIFF_PROJ_EGYPT_RED_BELT                       18072
#define GEOTIFF_PROJ_EGYPT_PURPLE_BELT                    18073
#define GEOTIFF_PROJ_EXTENDED_PURPLE_BELT                 18074
#define GEOTIFF_PROJ_NEW_ZEALAND_NORTH_ISLAND_NAT_GRID    18141
#define GEOTIFF_PROJ_NEW_ZEALAND_SOUTH_ISLAND_NAT_GRID    18142
#define GEOTIFF_PROJ_BAHRAIN_GRID                         19900
#define GEOTIFF_PROJ_NETHERLANDS_E_INDIES_EQUATORIAL      19905
#define GEOTIFF_PROJ_RSO_BORNEO                           19912

#define GEOTIFF_CT_TRANSVERSE_MERCATOR                  1
#define GEOTIFF_CT_GAUSS_BOAGA                          1
#define GEOTIFF_CT_GAUSS_KRUGER                         1
#define GEOTIFF_CT_TRANSV_MERCATOR_MODIFIED_ALASKA      2
#define GEOTIFF_CT_ALASKA_CONFORMAL                     2
#define GEOTIFF_CT_OBLIQUE_MERCATOR                     3
#define GEOTIFF_CT_OBLIQUE_MERCATOR_HOTINE              3
#define GEOTIFF_CT_OBLIQUE_MERCATOR_LABORDE             4
#define GEOTIFF_CT_OBLIQUE_MERCATOR_ROSENMUND           5
#define GEOTIFF_CT_SWISS_OBLIQUE_CYLINDRICAL            5
#define GEOTIFF_CT_OBLIQUE_MERCATOR_SPHERICAL           6
#define GEOTIFF_CT_MERCATOR                             7
#define GEOTIFF_CT_LAMBERT_CONF_CONIC_2SP               8
#define GEOTIFF_CT_LAMBERT_CONF_CONIC                   8
#define GEOTIFF_CT_LAMBERT_CONF_CONIC_HELMERT           9
#define GEOTIFF_CT_LAMBERT_CONF_CONIC_1SP               9
#define GEOTIFF_CT_LAMBERT_AZIM_EQUAL_AREA              10
#define GEOTIFF_CT_ALBERS_EQUAL_AREA                    11
#define GEOTIFF_CT_AZIMUTHAL_EQUIDISTANT                12
#define GEOTIFF_CT_EQUIDISTANT_CONIC                    13
#define GEOTIFF_CT_STEREOGRAPHIC                        14
#define GEOTIFF_CT_POLAR_STEREOGRAPHIC                  15
#define GEOTIFF_CT_OBLIQUE_STEREOGRAPHIC                16
#define GEOTIFF_CT_EQUIRECTANGULAR                      17
#define GEOTIFF_CT_CASSINI_SOLDNER                      18
#define GEOTIFF_CT_TRANSV_EQUIDIST_CYLINDRICAL          18
#define GEOTIFF_CT_GNOMONIC                             19
#define GEOTIFF_CT_MILLER_CYLINDRICAL                   20
#define GEOTIFF_CT_ORTHOGRAPHIC                         21
#define GEOTIFF_CT_POLYCONIC                            22
#define GEOTIFF_CT_ROBINSON                             23
#define GEOTIFF_CT_SINUSOIDAL                           24
#define GEOTIFF_CT_VANDERGRINTEN                        25
#define GEOTIFF_CT_NEW_ZEALAND_MAP_GRID                 26
#define GEOTIFF_CT_TRANSV_MERCATOR_SOUTH_ORIENTED       27
#define GEOTIFF_CT_SOUTH_ORIENTED_GAUSS_CONFORMAL       27

#define GEOTIFF_VERTCS_AIRY_1830_ELLIPSOID                        5001
#define GEOTIFF_VERTCS_AIRY_MODIFIED_1849_ELLIPSOID               5002
#define GEOTIFF_VERTCS_ANS_ELLIPSOID                              5003
#define GEOTIFF_VERTCS_BESSEL_1841_ELLIPSOID                      5004
#define GEOTIFF_VERTCS_BESSEL_MODIFIED_ELLIPSOID                  5005
#define GEOTIFF_VERTCS_BESSEL_NAMIBIA_ELLIPSOID                   5006
#define GEOTIFF_VERTCS_CLARKE_1858_ELLIPSOID                      5007
#define GEOTIFF_VERTCS_CLARKE_1866_ELLIPSOID                      5008
#define GEOTIFF_VERTCS_CLARKE_1880_BENOIT_ELLIPSOID               5010
#define GEOTIFF_VERTCS_CLARKE_1880_IGN_ELLIPSOID                  5011
#define GEOTIFF_VERTCS_CLARKE_1880_RGS_ELLIPSOID                  5012
#define GEOTIFF_VERTCS_CLARKE_1880_ARC_ELLIPSOID                  5013
#define GEOTIFF_VERTCS_CLARKE_1880_SGA_1922_ELLIPSOID             5014
#define GEOTIFF_VERTCS_EVEREST_1830_1937_ADJUSTMENT_ELLIPSOID     5015
#define GEOTIFF_VERTCS_EVEREST_1830_1967_DEFINITION_ELLIPSOID     5016
#define GEOTIFF_VERTCS_EVEREST_1830_1975_DEFINITION_ELLIPSOID     5017
#define GEOTIFF_VERTCS_EVEREST_1830_MODIFIED_ELLIPSOID            5018
#define GEOTIFF_VERTCS_GRS_1980_ELLIPSOID                         5019
#define GEOTIFF_VERTCS_HELMERT_1906_ELLIPSOID                     5020
#define GEOTIFF_VERTCS_INS_ELLIPSOID                              5021
#define GEOTIFF_VERTCS_INTERNATIONAL_1924_ELLIPSOID               5022
#define GEOTIFF_VERTCS_INTERNATIONAL_1967_ELLIPSOID               5023
#define GEOTIFF_VERTCS_KRASSOWSKY_1940_ELLIPSOID                  5024
#define GEOTIFF_VERTCS_NWL_9D_ELLIPSOID                           5025
#define GEOTIFF_VERTCS_NWL_10D_ELLIPSOID                          5026
#define GEOTIFF_VERTCS_PLESSIS_1817_ELLIPSOID                     5027
#define GEOTIFF_VERTCS_STRUVE_1860_ELLIPSOID                      5028
#define GEOTIFF_VERTCS_WAR_OFFICE_ELLIPSOID                       5029
#define GEOTIFF_VERTCS_WGS_84_ELLIPSOID                           5030
#define GEOTIFF_VERTCS_GEM_10C_ELLIPSOID                          5031
#define GEOTIFF_VERTCS_OSU86F_ELLIPSOID                           5032
#define GEOTIFF_VERTCS_OSU91A_ELLIPSOID                           5033
#define GEOTIFF_VERTCS_NEWLYN                                     5101
#define GEOTIFF_VERTCS_NORTH_AMERICAN_VERTICAL_DATUM_1929         5102
#define GEOTIFF_VERTCS_NORTH_AMERICAN_VERTICAL_DATUM_1988         5103
#define GEOTIFF_VERTCS_YELLOW_SEA_1956                            5104
#define GEOTIFF_VERTCS_BALTIC_SEA                                 5105
#define GEOTIFF_VERTCS_CASPIAN_SEA                                5106

#endif

//345678901234567890123456789012345678901234567890123456789012345678901234567890
