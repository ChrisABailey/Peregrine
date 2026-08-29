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

// GeoTiffFrameFile.h : Declaration of the CGeoTiffFrameFile

#ifndef __GEOTIFFFRAMEFILE_H_
#define __GEOTIFFFRAMEFILE_H_

#pragma once

#include "resource.h"       // main symbols

// NOTE (port 2026-07-15): the helper classes below (CGeoData, CTiffTag,
// CGeoKey, CTiepointTransform, rgb_box, median_cut) are a diverged fork of
// same-named classes in ImageLib/geotiff.h. On Windows the two copies live
// in separate DLLs and never meet; linked into one binary they collide
// (ODR violation). Namespaced here; the using-declarations after the
// namespace keep every unqualified use compiling on both platforms.
namespace gtff {

// this class maintains the geodata
class CGeoData
{
public:
   CGeoData( );                                   // constructor
   ~CGeoData( );                                  // destructor
   void clear( );                                 // clears object
   void operator=( CGeoData& geodata ); // assignment operator

   double m_model_pixel_scale_x;
   double m_model_pixel_scale_y;
   double m_model_pixel_scale_z;

   int m_num_tie_points;
   double *m_model_tie_point_hpix;
   double *m_model_tie_point_vpix;
   double *m_model_tie_point_zpix;
   double *m_model_tie_point_x;
   double *m_model_tie_point_y;
   double *m_model_tie_point_z;

   unsigned short m_gt_model_type;
   unsigned short m_gt_raster_type;
   std::string m_gt_citation;

   unsigned short m_geographic_type;
   std::string m_geog_citation;
   unsigned short m_geog_geodetic_datum;
   unsigned short m_geog_prime_meridian;
   double m_geog_prime_meridian_long;
   unsigned short m_geog_ellipsoid;
   double m_geog_semi_major_axis;
   double m_geog_semi_minor_axis;
   double m_geog_inv_flattening;

   unsigned short m_projected_cs_type;
   std::string m_pcs_citation;
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
   std::string m_vertical_citation;
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
   std::string m_tag_name;           // name of the tag
   std::string m_type_name;          // name of tag type
   std::string m_value_name;         // name of tag value

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
   std::string m_geokey_name;              // name of the geokey
   std::string m_type_name;                // name of geokey type
   std::string m_value_name;               // name of geokey value

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

#define COLOR_QUANTIZE_GRAYSCALE 1
#define COLOR_QUANTIZE_PALETTE 2
#define COLOR_QUANTIZE_HISTOGRAM 3

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

}  // namespace gtff

using gtff::CGeoData;
using gtff::CTiffTag;
using gtff::CGeoKey;
using gtff::CTiepointTransform;
using gtff::rgb_box;
using gtff::median_cut;

/////////////////////////////////////////////////////////////////////////////
// CGeoTiffFrameFile
#ifdef _WIN32
class ATL_NO_VTABLE CGeoTiffFrameFile : 
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CGeoTiffFrameFile, &CLSID_GeoTiffFrameFile>,
	public IDispatchImpl<IGeoTiffFrameFile, &IID_IGeoTiffFrameFile, &LIBID_GEOTIFFMAPSERVERLib>
{
#else
// POSIX port: plain class; COM surface (raw_GetFrameProperties and the
// ImageLib/MrSID fallback) lives in the portable twin fv::GeoTiffFrame.
namespace fv { class GeoTiffFrame; }
class CGeoTiffFrameFile
{
   friend class fv::GeoTiffFrame;
#endif  // _WIN32
public:
	CGeoTiffFrameFile();

   ~CGeoTiffFrameFile();

#ifdef _WIN32
DECLARE_REGISTRY_RESOURCEID(IDR_GEOTIFFFRAMEFILE)

DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CGeoTiffFrameFile)
	COM_INTERFACE_ENTRY(IGeoTiffFrameFile)
	COM_INTERFACE_ENTRY(IDispatch)
END_COM_MAP()

// IGeoTiffFrameFile
public:
	STDMETHOD(raw_GetFrameProperties)(double *ll_lat, double *ll_lon, double *ur_lat, 
      double *ur_lon, double *scale_denom, short *sScaleUnits, BSTR *series, VARIANT_BOOL *frame_supported);

	STDMETHOD(put_m_FilePath)(/*[in]*/ BSTR newVal);
	STDMETHOD(put_m_FileName)(/*[in]*/ BSTR newVal);
#endif  // _WIN32

#ifdef _WIN32
protected:

    IImageLibPtr m_imagelib;
#else
public:  // the portable twin drives these directly
#endif
	int m_width;
	int m_height;
	std::string m_datum_str;
	std::string m_series_str;
	double m_ll_lat;
	double m_ll_lon;
	double m_ur_lat;
	double m_ur_lon;

   std::string m_FilePath;
   std::string m_FileName; 

public:
   // clear to initial state
   void clear( );

   BOOL m_file_loaded;
   std::string m_tiff_file_name;

   // load tiff file
   int load( std::string tiff_file_name, BOOL &image_type_supported,
      int &image_type, std::string &image_type_description,
             int &image_width, int &image_length, 
             BOOL &geodata_present, BOOL &geodata_supported,
             std::string &error_message );

   // get the geodata
   int get_geodata( CGeoData &geodata, int &error_code,
                    std::string &error_message );

   int get_image_description( std::string &image_description );
   
   int get_image_width( int &image_width );
   
   int get_image_length( int &image_length );
   
   int get_image_type( int &image_type );
   
   // get the image scale
   int get_scale( int &horizontal_scale, int &vertical_scale );

   // find the latitude and longitude of a pixel
   int inv_transform( int hpix, int vpix, double &latitude, double &longitude );

   // find the pixel corresponding to a latitude and longitude
   int fwd_transform( double latitude, double longitude, int &hpix, int &vpix );

#ifdef _WIN32
	int imagelib_load( IImageLibPtr & imagelib, std::string file_name, std::string & datum_str,
						int &image_width, int &image_length, std::string &error_msg );

	int imagelib_fwd_transform( IImageLibPtr & imagelib,	std::string datum_string,
							   double latitude, double longitude, int *hpix, int *vpix );

   int imagelib_inv_transform( IImageLibPtr & imagelib,	std::string datum_string,
							   int hpix, int vpix, double *latitude, double *longitude );
#endif  // _WIN32

   int determine_scale( double &scale );

   // get the latitude and longitude of the four image corners
   int get_image_bounds( double &lat_upper_left, double &long_upper_left,
                         double &lat_lower_left, double &long_lower_left,
                         double &lat_lower_right, double &long_lower_right,
                         double &lat_upper_right, double &long_upper_right );

   // get the image width and height
   int get_image_size( int &image_width, int &image_length );

   // get the subsampled image width and height for a subimage
   int get_subsampled_subimage_size( int sampling, 
      int min_hpix, int min_vpix, int max_hpix, int max_vpix,
      int &image_width, int &image_length );

   // get the subimage in rgb format
   int get_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                         unsigned char *red_array, unsigned char *green_array,
                         unsigned char *blue_array );

   // get the subimage, subsampled, in rgb format
   int get_subsampled_rgb_subimage( int sampling,
                         int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                         unsigned char *red_array, unsigned char *green_array,
                         unsigned char *blue_array );
   
	// is the image tiled
   BOOL is_tiled(); 

   BOOL is_east_of(double lon1, double lon2);

   // samples per pixel
   unsigned short m_samples_per_pixel;

   int TIF_lat_lon_to_xyz( double latitude, double longitude, 
      double &x, double &y, double &z );


// protected interface *********************************************************
protected:

   // byte order of tiff file: GEOTIFF_LITTLE_ENDIAN or GEOTIFF_BIG_ENDIAN
   int m_byte_order;

   // number of image file directories
   int m_num_directories;

   // offset of image file directory
   unsigned int m_directory_offset;

   // file pointer used to read tiff file
   FILE *m_file_ptr;

   // error flag
   BOOL m_error;
   
   // error description
   std::string m_cumulative_error_description;
   std::string m_new_error_description;

   // number of tags
   int m_num_tags;

   // tags
   CTiffTag *m_tags;

	// JPEGTables
	BYTE m_jpeg_table[1000];
	int m_jpeg_table_len;

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

   // geodata
   BOOL m_geodata_present, m_geodata_supported;
   int m_geodata_error_code;
   std::string m_geodata_error_message;
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

   // bits per sample
   unsigned short m_bits_per_sample;

   // fill order
   unsigned short m_fill_order;

   // orientation
   unsigned short m_orientation;

   unsigned short m_geog_linear_units;

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
   std::string m_image_type_description;

   // max strip size
   unsigned int m_max_strip_byte_count;
   
   // histogram array
   unsigned int m_histogram[32768];

   CTiepointTransform m_tiepoint_transform;

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
   
   // *** member functions ***

   // read tag directory
   int read_directory( );

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
   std::string get_tag_name( unsigned short tag_id );

   // get the name of a data type
   std::string get_type_name( unsigned short type );

   // get the name of a tag's data value or format its numeric values
   std::string get_tag_value_name( const CTiffTag &tag );

   // get the name of a geokey
   std::string get_geokey_name( unsigned short geokey_id );

   // get the name of a geokey's data value or format its numeric values
   std::string get_geokey_value_name( const CGeoKey &geokey );
   
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

   int decompress_packbits( int compressed_size, unsigned char *compressed,
                            int decompressed_size,
                            unsigned char *decompressed );

   // TIFF 6.0 LZW (compression tag 5), MSB-first codes with the "early
   // change" code-width bump every encoder in the wild writes.  Same
   // contract as decompress_packbits.
   int decompress_lzw( int compressed_size, unsigned char *compressed,
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

   int get_8bit_grayscale_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_16bit_grayscale_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_24bit_rgb_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_8bit_palette_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   // tiled image functions
   int get_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_8bit_grayscale_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_16bit_grayscale_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_24bit_rgb_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

   int get_8bit_palette_tile_as_rgb_image( int itile, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array );

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

   int fwd_transform_sp( double latitude, double longitude, int &hpix, int &vpix  );

   int inv_transform_sp( int hpix, int vpix, double &latitude, double &longitude );


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

   int TIF_get_drg_coverage( std::string tiff_file_name, int &scale,
                          double &ll_lat, double &ll_lon,
                          double &ur_lat, double &ur_lon );

   int TIF_get_doq_coverage( std::string tiff_file_name,
                          double &ll_lat, double &ll_lon,
                          double &ur_lat, double &ur_lon );

   int TIF_determine_coverage( double &ll_lat, double &ll_lon,
                            double &ur_lat, double &ur_lon );

   int TIF_determine_scale_and_series( double &scale, short &sShortUnits, std::wstring &series );

   int TIF_check_image_description( std::string &image_description,
                                double &scale, std::wstring &series );

#ifdef _WIN32
   int get_series_from_info( std::string info, double *scale_denom, short *sScaleUnits, BSTR *series);
#endif
   int extract_data_from_info( std::string info, std::string key, std::string *data);
};


#endif //__GEOTIFFFRAMEFILE_H_
