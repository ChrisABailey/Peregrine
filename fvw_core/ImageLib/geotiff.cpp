// Copyright (c) 1994-2011,2013 Georgia Tech Research Corporation, Atlanta, GA
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

// GeoTiff.cpp : Implementation of CGeoTiff, CGeoData, CTiffTag, CGeoKey



// 07-Jul-2005 (RAC)  Do fseek() only when file position is invalid.  Doing fseek's seem
//                   to flush the file caches (at least for network files.

// Author: Barrett D. Flansburg
// GTRI FalconView

#include "stdafx.h"
#include "common.h"
#include "err.h"
#include "file.h"
#include "mem.h"
#include "map.h"        // CImageMap
#include "geotiff.h"
#include "projsp.h"
#include "util.h"

#include <errno.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <fcntl.h>
#include <math.h>

#include "geotiff.inc"


//345678901234567890123456789012345678901234567890123456789012345678901234567890

// ********************************************************************************************
// ********************************************************************************************

static inline void byteswap_2(void* ptr)
{
   unsigned char tmp[2];

   tmp[0] = ((unsigned char*)ptr)[0];
   tmp[1] = ((unsigned char*)ptr)[1];

   ((unsigned char*)ptr)[0] = tmp[1];
   ((unsigned char*)ptr)[1] = tmp[0];
}

// ********************************************************************************************
// ********************************************************************************************

// CGeoData class:
CGeoData::CGeoData( )
{
   m_model_tie_point_hpix = NULL;
   m_model_tie_point_vpix = NULL;
   m_model_tie_point_zpix = NULL;
   m_model_tie_point_x = NULL;
   m_model_tie_point_y = NULL;
   m_model_tie_point_z = NULL;

   clear( );
}

// ********************************************************************************************
// ********************************************************************************************

CGeoData::~CGeoData( )
{
   clear( );
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoData::clear( )
{
   // this function clears the geodata
   ZeroMemory( m_dModelPixelTransformation, sizeof(m_dModelPixelTransformation) );

   m_num_tie_points = 0;
   if( m_model_tie_point_hpix != NULL )
   {
      MEM_free( m_model_tie_point_hpix );
      m_model_tie_point_hpix = NULL;
   }
   if( m_model_tie_point_vpix != NULL )
   {
      MEM_free( m_model_tie_point_vpix );
      m_model_tie_point_vpix = NULL;
   }
   if( m_model_tie_point_zpix != NULL )
   {
      MEM_free( m_model_tie_point_zpix );
      m_model_tie_point_zpix = NULL;
   }
   if( m_model_tie_point_x != NULL )
   {
      MEM_free( m_model_tie_point_x );
      m_model_tie_point_x = NULL;
   }
   if( m_model_tie_point_y != NULL )
   {
      MEM_free( m_model_tie_point_y );
      m_model_tie_point_y = NULL;
   }
   if( m_model_tie_point_z != NULL )
   {
      MEM_free( m_model_tie_point_z );
      m_model_tie_point_z = NULL;
   }
   m_gt_model_type = 0;
   m_gt_raster_type = 0;
   m_gt_citation = "";
   m_geographic_type = 0;
   m_geog_citation = "";
   m_geog_geodetic_datum = 0;
   m_geog_prime_meridian = 0;
   m_geog_prime_meridian_long = 0.0;
   m_geog_ellipsoid = 0;
   m_geog_semi_major_axis = 0.0;
   m_geog_semi_minor_axis = 0.0;
   m_geog_inv_flattening = 0.0;
   m_projected_cs_type = 0;
   m_pcs_citation = "";
   m_projection = 0;
   m_proj_coord_trans = 0;
   m_proj_std_parallel_1 = 0.0;
   m_proj_std_parallel_2 = 0.0;
   m_proj_nat_origin_long = 0.0;
   m_proj_nat_origin_lat = 0.0;
   m_proj_false_easting = 0.0;
   m_proj_false_northing = 0.0;
   m_proj_false_origin_long = 0.0;
   m_proj_false_origin_lat = 0.0;
   m_proj_false_origin_easting = 0.0;
   m_proj_false_origin_northing = 0.0;
   m_proj_center_long = 0.0;
   m_proj_center_lat = 0.0;
   m_proj_center_easting = 0.0;
   m_proj_center_northing = 0.0;
   m_proj_scale_at_nat_origin = 0.0;
   m_proj_scale_at_center = 0.0;
   m_proj_azimuth_angle = 0.0;
   m_proj_straight_vert_pole_long = 0.0;
   m_vertical_cs_type = 0;
   m_vertical_citation = "";
   m_vertical_datum = 0;

   m_model_pixel_scale_present = FALSE;
   m_model_tie_point_present = FALSE;
   m_gt_model_type_present = FALSE;
   m_gt_raster_type_present = FALSE;
   m_gt_citation_present = FALSE;
   m_geographic_type_present = FALSE;
   m_geog_citation_present = FALSE;
   m_geog_geodetic_datum_present = FALSE;
   m_geog_prime_meridian_present = FALSE;
   m_geog_prime_meridian_long_present = FALSE;
   m_geog_ellipsoid_present = FALSE;
   m_geog_semi_major_axis_present = FALSE;
   m_geog_semi_minor_axis_present = FALSE;
   m_geog_inv_flattening_present = FALSE;
   m_projected_cs_type_present = FALSE;
   m_pcs_citation_present = FALSE;
   m_projection_present = FALSE;
   m_proj_coord_trans_present = FALSE;
   m_proj_std_parallel_1_present = FALSE;
   m_proj_std_parallel_2_present = FALSE;
   m_proj_nat_origin_long_present = FALSE;
   m_proj_nat_origin_lat_present = FALSE;
   m_proj_false_easting_present = FALSE;
   m_proj_false_northing_present = FALSE;
   m_proj_false_origin_long_present = FALSE;
   m_proj_false_origin_lat_present = FALSE;
   m_proj_false_origin_easting_present = FALSE;
   m_proj_false_origin_northing_present = FALSE;
   m_proj_center_long_present = FALSE;
   m_proj_center_lat_present = FALSE;
   m_proj_center_easting_present = FALSE;
   m_proj_center_northing_present = FALSE;
   m_proj_scale_at_nat_origin_present = FALSE;
   m_proj_scale_at_center_present = FALSE;
   m_proj_azimuth_angle_present = FALSE;
   m_proj_straight_vert_pole_long_present = FALSE;
   m_vertical_cs_type_present = FALSE;
   m_vertical_citation_present = FALSE;
   m_vertical_datum_present = FALSE;
}
// end of clear

// ********************************************************************************************
// ********************************************************************************************

void CGeoData::operator=( CGeoData& geodata )
{
   // this function allows you to copy one CGeoData object to another,
   // being careful to allocate new memory for the tiepoint arrays
   int i;

   // first, clear this CGeoData object
   clear( );

   // now start copying values
   memcpy( m_dModelPixelTransformation, geodata.m_dModelPixelTransformation,
      sizeof(m_dModelPixelTransformation) );

   m_num_tie_points = geodata.m_num_tie_points;
   if( m_num_tie_points > 0 )
   {
      // allocate memory for tiepoints
      m_model_tie_point_hpix =
         (double*)MEM_malloc( m_num_tie_points * sizeof(double) );
      if( m_model_tie_point_hpix == NULL )
         goto FAIL;
      m_model_tie_point_vpix =
         (double*)MEM_malloc( m_num_tie_points * sizeof(double) );
      if( m_model_tie_point_vpix == NULL )
         goto FAIL;
      m_model_tie_point_zpix =
         (double*)MEM_malloc( m_num_tie_points * sizeof(double) );
      if( m_model_tie_point_zpix == NULL )
         goto FAIL;
      m_model_tie_point_x =
         (double*)MEM_malloc( m_num_tie_points * sizeof(double) );
      if( m_model_tie_point_x == NULL )
         goto FAIL;
      m_model_tie_point_y =
         (double*)MEM_malloc( m_num_tie_points * sizeof(double) );
      if( m_model_tie_point_y == NULL )
         goto FAIL;
      m_model_tie_point_z =
         (double*)MEM_malloc( m_num_tie_points * sizeof(double) );
      if( m_model_tie_point_z == NULL )
         goto FAIL;
      // copy the tie point coordinates
      for( i = 0; i < m_num_tie_points; i++ )
      {
         m_model_tie_point_hpix[i] = geodata.m_model_tie_point_hpix[i];
         m_model_tie_point_vpix[i] = geodata.m_model_tie_point_vpix[i];
         m_model_tie_point_zpix[i] = geodata.m_model_tie_point_zpix[i];
         m_model_tie_point_x[i] = geodata.m_model_tie_point_x[i];
         m_model_tie_point_y[i] = geodata.m_model_tie_point_y[i];
         m_model_tie_point_z[i] = geodata.m_model_tie_point_z[i];
      }
   }

   m_gt_model_type = geodata.m_gt_model_type;
   m_gt_raster_type = geodata.m_gt_raster_type;
   m_gt_citation = geodata.m_gt_citation;
   m_geographic_type = geodata.m_geographic_type;
   m_geog_citation = geodata.m_geog_citation;
   m_geog_geodetic_datum = geodata.m_geog_geodetic_datum;
   m_geog_prime_meridian = geodata.m_geog_prime_meridian;
   m_geog_prime_meridian_long = geodata.m_geog_prime_meridian_long;
   m_geog_ellipsoid = geodata.m_geog_ellipsoid;
   m_geog_semi_major_axis = geodata.m_geog_semi_major_axis;
   m_geog_semi_minor_axis = geodata.m_geog_semi_minor_axis;
   m_geog_inv_flattening = geodata.m_geog_inv_flattening;
   m_projected_cs_type = geodata.m_projected_cs_type;
   m_pcs_citation = geodata.m_pcs_citation;
   m_projection = geodata.m_projection;
   m_proj_coord_trans = geodata.m_proj_coord_trans;
   m_proj_std_parallel_1 = geodata.m_proj_std_parallel_1;
   m_proj_std_parallel_2 = geodata.m_proj_std_parallel_2;
   m_proj_nat_origin_long = geodata.m_proj_nat_origin_long;
   m_proj_nat_origin_lat = geodata.m_proj_nat_origin_lat;
   m_proj_false_easting = geodata.m_proj_false_easting;
   m_proj_false_northing = geodata.m_proj_false_northing;
   m_proj_false_origin_long = geodata.m_proj_false_origin_long;
   m_proj_false_origin_lat = geodata.m_proj_false_origin_lat;
   m_proj_false_origin_easting = geodata.m_proj_false_origin_easting;
   m_proj_false_origin_northing = geodata.m_proj_false_origin_northing;
   m_proj_center_long = geodata.m_proj_center_long;
   m_proj_center_lat = geodata.m_proj_center_lat;
   m_proj_center_easting = geodata.m_proj_center_easting;
   m_proj_center_northing = geodata.m_proj_center_northing;
   m_proj_scale_at_nat_origin = geodata.m_proj_scale_at_nat_origin;
   m_proj_scale_at_center = geodata.m_proj_scale_at_center;
   m_proj_azimuth_angle = geodata.m_proj_azimuth_angle;
   m_proj_straight_vert_pole_long = geodata.m_proj_straight_vert_pole_long;
   m_vertical_cs_type = geodata.m_vertical_cs_type;
   m_vertical_citation = geodata.m_vertical_citation;
   m_vertical_datum = geodata.m_vertical_datum;

   m_model_pixel_scale_present = geodata.m_model_pixel_scale_present;
   m_model_tie_point_present = geodata.m_model_tie_point_present;
   m_gt_model_type_present = geodata.m_gt_model_type_present;
   m_gt_raster_type_present = geodata.m_gt_raster_type_present;
   m_gt_citation_present = geodata.m_gt_citation_present;
   m_geographic_type_present = geodata.m_geographic_type_present;
   m_geog_citation_present = geodata.m_geog_citation_present;
   m_geog_geodetic_datum_present = geodata.m_geog_geodetic_datum_present;
   m_geog_prime_meridian_present = geodata.m_geog_prime_meridian_present;
   m_geog_prime_meridian_long_present =
      geodata.m_geog_prime_meridian_long_present;
   m_geog_ellipsoid_present = geodata.m_geog_ellipsoid_present;
   m_geog_semi_major_axis_present = geodata.m_geog_semi_major_axis_present;
   m_geog_semi_minor_axis_present = geodata.m_geog_semi_minor_axis_present;
   m_geog_inv_flattening_present = geodata.m_geog_inv_flattening_present;
   m_projected_cs_type_present = geodata.m_projected_cs_type_present;
   m_pcs_citation_present = geodata.m_pcs_citation_present;
   m_projection_present = geodata.m_projection_present;
   m_proj_coord_trans_present = geodata.m_proj_coord_trans_present;
   m_proj_std_parallel_1_present = geodata.m_proj_std_parallel_1_present;
   m_proj_std_parallel_2_present = geodata.m_proj_std_parallel_2_present;
   m_proj_nat_origin_long_present = geodata.m_proj_nat_origin_long_present;
   m_proj_nat_origin_lat_present = geodata.m_proj_nat_origin_lat_present;
   m_proj_false_easting_present = geodata.m_proj_false_easting_present;
   m_proj_false_northing_present = geodata.m_proj_false_northing_present;
   m_proj_false_origin_long_present = geodata.m_proj_false_origin_long_present;
   m_proj_false_origin_lat_present = geodata.m_proj_false_origin_lat_present;
   m_proj_false_origin_easting_present =
      geodata.m_proj_false_origin_easting_present;
   m_proj_false_origin_northing_present =
      geodata.m_proj_false_origin_northing_present;
   m_proj_center_long_present = geodata.m_proj_center_long_present;
   m_proj_center_lat_present = geodata.m_proj_center_lat_present;
   m_proj_center_easting_present = geodata.m_proj_center_easting_present;
   m_proj_center_northing_present = geodata.m_proj_center_northing_present;
   m_proj_scale_at_nat_origin_present =
      geodata.m_proj_scale_at_nat_origin_present;
   m_proj_scale_at_center_present = geodata.m_proj_scale_at_center_present;
   m_proj_azimuth_angle_present = geodata.m_proj_azimuth_angle_present;
   m_proj_straight_vert_pole_long_present =
      geodata.m_proj_straight_vert_pole_long_present;
   m_vertical_cs_type_present = geodata.m_vertical_cs_type_present;
   m_vertical_citation_present = geodata.m_vertical_citation_present;
   m_vertical_datum_present = geodata.m_vertical_datum_present;

   return;

FAIL:
   clear( );
   return;
}
// end of copy operator


//
// CGeoData::CalcFwdCoeffs() - Compute lat/lon to x/y coeffs from x/y to lat/lon coeffs
//
VOID CGeoData::CalcFwdCoeffs()
{
   DOUBLE dD = ( m_dLScaleX * m_dPScaleY ) - ( m_dLScaleY * m_dPScaleX );
   m_dXScaleL = m_dPScaleY / dD;
   m_dXScaleP = -m_dLScaleY / dD;
   m_dYScaleL = -m_dPScaleX / dD;
   m_dYScaleP = m_dLScaleX / dD;
#if 0 && defined _DEBUG
   ATLTRACE( _T("Check matrix:\n")
            _T("  [0,0] = %.8f\n")
            _T("  [1,0] = %.8f\n")
            _T("  [0,1] = %.8f\n")
            _T("  [1,1] = %.8f\n"),
            ( m_dLScaleX * m_dXScaleL ) + ( m_dLScaleY * m_dYScaleL ),
            ( m_dLScaleX * m_dXScaleP ) + ( m_dLScaleY * m_dYScaleP ),
            ( m_dPScaleX * m_dXScaleL ) + ( m_dPScaleY * m_dYScaleL ),
            ( m_dPScaleX * m_dXScaleP ) + ( m_dPScaleY * m_dYScaleP ) );
#endif
}


//
// CGeoData::inv_transform()
//
VOID CGeoData::inv_transform( DOUBLE dX, DOUBLE dY, DOUBLE& dL, DOUBLE& dP )
{
   dL = m_dLOffset
      + ( m_dLScaleX * ( dX - m_dXOffset ) )
      + ( m_dLScaleY * ( dY - m_dYOffset ) );
   dP = m_dPOffset
      + ( m_dPScaleX * ( dX - m_dXOffset ) )
      + ( m_dPScaleY * ( dY - m_dYOffset ) );
}

//
// CGeoData::fwd_transform()
//
VOID CGeoData::fwd_transform( DOUBLE dL, DOUBLE dP, DOUBLE& dX, DOUBLE& dY )
{
   dX = m_dXOffset
      + ( m_dXScaleL * ( dL - m_dLOffset ) )
      + ( m_dXScaleP * ( dP - m_dPOffset ) );
   dY = m_dYOffset
      + ( m_dYScaleL * ( dL - m_dLOffset ) )
      + ( m_dYScaleP * ( dP - m_dPOffset ) );
}



// ********************************************************************************************
// ********************************************************************************************

// CTiffTag class:

CTiffTag::CTiffTag( )
{
   // constructor

   // set all array pointers to NULL
   m_byte_values = NULL;
   m_ascii_values = NULL;
   m_short_values = NULL;
   m_long_values = NULL;
   m_rational_numerator_values = NULL;
   m_rational_denominator_values = NULL;
   m_sbyte_values = NULL;
   m_undefined_values = NULL;
   m_sshort_values = NULL;
   m_slong_values = NULL;
   m_srational_numerator_values = NULL;
   m_srational_denominator_values = NULL;
   m_float_values = NULL;
   m_double_values = NULL;

   // clear to uninitialized state
   clear( );
}

// ********************************************************************************************
// ********************************************************************************************

CTiffTag::~CTiffTag( )
{
   // destructor

   // clear to uninitialized state
   clear( );
}

// ********************************************************************************************
// ********************************************************************************************

void CTiffTag::clear( )
{
   // clear to uninitialized state

   m_defined = FALSE;
   m_tag_id = 0;
   m_type = 0;
   m_count = 0;
   m_value_offset = 0;
//   m_tag_name = "";
//   m_type_name = "";
//   m_value_name = "";

   // deallocate memory
   if( m_byte_values != NULL )
   {
      MEM_free( m_byte_values );
      m_byte_values = NULL;
   }

   if( m_ascii_values != NULL )
   {
      MEM_free( m_ascii_values );
      m_ascii_values = NULL;
   }

   if( m_short_values != NULL )
   {
      MEM_free( m_short_values );
      m_short_values = NULL;
   }

   if( m_long_values != NULL )
   {
      MEM_free( m_long_values );
      m_long_values = NULL;
   }

   if( m_rational_numerator_values != NULL )
   {
      MEM_free( m_rational_numerator_values );
      m_rational_numerator_values = NULL;
   }

   if( m_rational_denominator_values != NULL )
   {
      MEM_free( m_rational_denominator_values );
      m_rational_denominator_values = NULL;
   }

   if( m_sbyte_values != NULL )
   {
      MEM_free( m_sbyte_values );
      m_sbyte_values = NULL;
   }

   if( m_undefined_values != NULL )
   {
      MEM_free( m_undefined_values );
      m_undefined_values = NULL;
   }

   if( m_sshort_values != NULL )
   {
      MEM_free( m_sshort_values );
      m_sshort_values = NULL;
   }

   if( m_slong_values != NULL )
   {
      MEM_free( m_slong_values );
      m_slong_values = NULL;
   }

   if( m_srational_numerator_values != NULL )
   {
      MEM_free( m_srational_numerator_values );
      m_srational_numerator_values = NULL;
   }

   if( m_srational_denominator_values != NULL )
   {
      MEM_free( m_srational_denominator_values );
      m_srational_denominator_values = NULL;
   }

   if( m_float_values != NULL )
   {
      MEM_free( m_float_values );
      m_float_values = NULL;
   }

   if( m_double_values != NULL )
   {
      MEM_free( m_double_values );
      m_double_values = NULL;
   }
}
// end of clear

//345678901234567890123456789012345678901234567890123456789012345678901234567890

// ********************************************************************************************
// ********************************************************************************************

// CGeoKey class:

CGeoKey::CGeoKey( )
{
   // constructor

   // set all array pointers to NULL
   m_ascii_values = NULL;
   m_short_values = NULL;
   m_double_values = NULL;

   // clear to uninitialized state
   clear( );
}

// ********************************************************************************************
// ********************************************************************************************

CGeoKey::~CGeoKey( )
{
   // destructor

   // clear to uninitialized state
   clear( );
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoKey::clear( )
{
   // clear to uninitialized state

   m_defined = FALSE;
   m_geokey_id = 0;
   m_tiff_tag_location = 0;
   m_type = 0;
   m_count = 0;
   m_value_offset = 0;
   m_geokey_name = "";
   m_type_name = "";
   m_value_name = "";

   // deallocate memory
   if( m_ascii_values != NULL )
   {
      MEM_free( m_ascii_values );
      m_ascii_values = NULL;
   }

   if( m_short_values != NULL )
   {
      MEM_free( m_short_values );
      m_short_values = NULL;
   }

   if( m_double_values != NULL )
   {
      MEM_free( m_double_values );
      m_double_values = NULL;
   }
}

// ********************************************************************************************
// ********************************************************************************************

// median cut function - not a class member
int median_cut( unsigned int *histogram, int num_colors, unsigned char *red,
                unsigned char *green, unsigned char *blue,
                unsigned char *histogram_indices )
{
   // this function implements the median cut method of color quantization
   // the histogram argument contains the cumulative pixel counts for the 32768
   // colors in the reduced 32k color space. the num_colors argument specifies
   // the number of colors after quantization (must be 1 to 256). the red,
   // green and blue arrays should be dimensioned [num_colors] and will be
   // loaded with the quantized colors. the histogram_indices argument should
   // be dimensioned [32768] and will contain the indices for the quantized
   // colors for each of the colors in the histogram
   // returns SUCCESS or FAILURE

   int i, color_index, iparent, ichild, num_parents, num_children, generation;
   int num_colors_used;
   unsigned int red_counts[256], green_counts[256], blue_counts[256];
   unsigned int count, half_count;
   rgb_box parent_boxes[128], child_boxes[256];
   unsigned char histogram_red[32768], histogram_green[32768];
   unsigned char histogram_blue[32768];
   unsigned char min_red, max_red, min_green, max_green, min_blue, max_blue;
   unsigned char mid_red, mid_green, mid_blue;
   unsigned char red_range, green_range, blue_range;
   unsigned char median_red, median_green, median_blue;

   // check for invalid number of quantized colors
   if( num_colors < 1 || num_colors > 256 )
      return FAILURE;

   // initialize histogram colors and find number of colors used
   num_colors_used = 0;
   for( i = 0; i < 32768; i++ )
   {
      if( histogram[i] != 0 ) num_colors_used++;
      histogram_red[i] = (BYTE) (i >> 10);
      histogram_green[i] = (BYTE) ((i >> 5) & 31);
      histogram_blue[i] = (BYTE) (i & 31);
   }

   // if the number of colors used is <= the palette size, just copy them
   if( num_colors_used <= num_colors )
   {
      color_index = 0;
      for( i = 0; i < 32768; i++ )
      {
         if( histogram[i] == 0 )
          continue;
         // convert from 31 to 255 range
         red[color_index] = (unsigned char)( (int)histogram_red[i] *
                            255 / 31 );
         green[color_index] = (unsigned char)( (int)histogram_green[i] *
                              255 / 31 );
         blue[color_index] = (unsigned char)( (int)histogram_blue[i] *
                             255 / 31 );
         histogram_indices[i] = (BYTE) (color_index);
         color_index++;
      }
      return SUCCESS;
   }

   // create first parent box containing entire color space and all pixels
   num_parents = 1;
   parent_boxes[0].num_colors = 0;
   parent_boxes[0].small_enough = FALSE;
   parent_boxes[0].min_red = 0;
   parent_boxes[0].max_red = 31;
   parent_boxes[0].min_green = 0;
   parent_boxes[0].max_green = 31;
   parent_boxes[0].min_blue = 0;
   parent_boxes[0].max_blue = 31;

   generation = 1;

CREATE_CHILDREN:
   num_children = 0;
   for( iparent = 0; iparent < num_parents; iparent++ )
   {
      // check for limit on number of colors
      if( num_children + num_parents - iparent + 1 > num_colors )
      {
         // copy remaining parents to children
         for( i = iparent; i < num_parents; i++ )
         {
            child_boxes[num_children] = parent_boxes[i];
            num_children++;
         }
         goto QUANTIZE;
      }

      // check for small enough without subdividing
      if( parent_boxes[iparent].small_enough )
      {
         child_boxes[num_children] = parent_boxes[iparent];
         num_children++;
         continue;
      }

      // find max/min red, green, and blue for colors in box
      min_red = 31;
      max_red = 0;
      min_green = 31;
      max_green = 0;
      min_blue = 31;
      max_blue = 0;
      for( i = 0; i < 256; i++ )
         red_counts[i] = green_counts[i] = blue_counts[i] = 0;
      for( i = 0; i < 32768; i++ )
      {
         if( histogram[i] == 0 )
          continue;
         if(
            histogram_red[i] >= parent_boxes[iparent].min_red &&
            histogram_red[i] <= parent_boxes[iparent].max_red &&
            histogram_green[i] >= parent_boxes[iparent].min_green &&
            histogram_green[i] <= parent_boxes[iparent].max_green &&
            histogram_blue[i] >= parent_boxes[iparent].min_blue &&
            histogram_blue[i] <= parent_boxes[iparent].max_blue )
         {
            parent_boxes[iparent].num_colors++;
            if( histogram_red[i] < min_red )
            min_red = histogram_red[i];
            if( histogram_red[i] > max_red )
            max_red = histogram_red[i];
            if( histogram_green[i] < min_green )
            min_green = histogram_green[i];
            if( histogram_green[i] > max_green )
            max_green = histogram_green[i];
            if( histogram_blue[i] < min_blue )
            min_blue = histogram_blue[i];
            if( histogram_blue[i] > max_blue )
            max_blue = histogram_blue[i];
            red_counts[histogram_red[i]]++;
            green_counts[histogram_green[i]]++;
            blue_counts[histogram_blue[i]]++;
         }
      }
      parent_boxes[iparent].min_red = min_red;
      parent_boxes[iparent].max_red = max_red;
      parent_boxes[iparent].min_green = min_green;
      parent_boxes[iparent].max_green = max_green;
      parent_boxes[iparent].min_blue = min_blue;
      parent_boxes[iparent].max_blue = max_blue;

      // check for no colors in box -> no children
      if( parent_boxes[iparent].num_colors == 0 )
        continue;

      red_range = (BYTE) (max_red - min_red);
      green_range = (BYTE) (max_green - min_green);
      blue_range = (BYTE) (max_blue - min_blue);

      // check for too small to bother subdividing
      if( red_range < 2 && green_range < 2 && blue_range < 2 )
      {
         child_boxes[num_children] = parent_boxes[iparent];
         num_children++;
         continue;
      }

      // create two children

      // split in red direction
      if( red_range >= green_range && red_range >= blue_range )
      {
         // find median red value
        median_red = 0;
         count = 0;
         for( i = 0; i < 256; i++ )
            count += red_counts[i];
         half_count = count / 2;
         count = 0;
         for( i = 0; i < 256; i++ )
         {
            count += red_counts[i];
            if( count >= half_count )
            {
               median_red = (BYTE) i;
               break;
            }
         }
         mid_red = median_red;
//         mid_red = (unsigned char)( ( (int)min_red + (int)max_red ) / 2 );
         if( mid_red == max_red )
          mid_red--;
         // first child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = mid_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         red_range = (BYTE) (mid_red - min_red);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         // second child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = (BYTE) (mid_red+1);
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         red_range = (BYTE) (max_red - mid_red - 1);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         continue;
      }

      // split in green direction
      if( green_range >= red_range && green_range >= blue_range )
      {
         // find median green value
        median_green = 0;
         count = 0;
         for( i = 0; i < 256; i++ )
            count += green_counts[i];
         half_count = count / 2;
         count = 0;
         for( i = 0; i < 256; i++ )
         {
            count += green_counts[i];
            if( count >= half_count )
            {
               median_green = (BYTE) i;
               break;
            }
         }
         mid_green = median_green;
//         mid_green = (unsigned char)( ( (int)min_green + (int)max_green ) /
//             2 );
         if( mid_green == max_green )
          mid_green--;
         // first child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = mid_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         green_range = (BYTE) (mid_green - min_green);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         // second child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = (BYTE) (mid_green+1);
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         green_range = (BYTE) (max_green - mid_green - 1);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         continue;
      }

      // split in blue direction
      if( blue_range >= red_range && blue_range >= green_range )
      {
         // find median blue value
        median_blue = 0;
         count = 0;
         for( i = 0; i < 256; i++ )
            count += blue_counts[i];
         half_count = count / 2;
         count = 0;
         for( i = 0; i < 256; i++ )
         {
            count += blue_counts[i];
            if( count >= half_count )
            {
               median_blue = (BYTE) i;
               break;
            }
         }
         mid_blue = median_blue;
//         mid_blue = (unsigned char)( ( (int)min_blue + (int)max_blue ) / 2 );
         if( mid_blue == max_blue )
          mid_blue--;
         // first child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = mid_blue;
         blue_range = (BYTE) (mid_blue - min_blue);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         // second child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = (BYTE) (mid_blue+1);
         child_boxes[num_children].max_blue = max_blue;
         blue_range = (BYTE) (max_blue - mid_blue - 1);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         continue;
      }
   }

   generation++;
   if( generation == 9 )
      goto QUANTIZE;
   else
   {
      // make parents of children
      for( i = 0; i < num_children; i++ )
         parent_boxes[i] = child_boxes[i];
      num_parents = num_children;
      goto CREATE_CHILDREN;
   }

QUANTIZE:
   // find midpoint of each child box as the color map color
   for( i = 0; i < num_children; i++ )
   {
      red[i] = (unsigned char)( ( (int)child_boxes[i].min_red +
                      (int)child_boxes[i].max_red ) / 2 );
      green[i] = (unsigned char)( ( (int)child_boxes[i].min_green +
                        (int)child_boxes[i].max_green ) / 2 );
      blue[i] = (unsigned char)( ( (int)child_boxes[i].min_blue +
                       (int)child_boxes[i].max_blue ) / 2 );
      // convert from 31 to 255 range
      red[i] = (unsigned char)( (int)red[i] * 255 / 31 );
      green[i] = (unsigned char)( (int)green[i] * 255 / 31 );
      blue[i] = (unsigned char)( (int)blue[i] * 255 / 31 );
   }

   // find the child box for each color
   for( i = 0; i < 32768; i++ )
   {
      histogram_indices[i] = 0;
      if( histogram[i] == 0 )
        continue;
      for( ichild = 0; ichild < num_children; ichild++ )
      {
         if(
            histogram_red[i] >= child_boxes[ichild].min_red &&
            histogram_red[i] <= child_boxes[ichild].max_red &&
            histogram_green[i] >= child_boxes[ichild].min_green &&
            histogram_green[i] <= child_boxes[ichild].max_green &&
            histogram_blue[i] >= child_boxes[ichild].min_blue &&
            histogram_blue[i] <= child_boxes[ichild].max_blue )
         {
            histogram_indices[i] = (BYTE) ichild;
            break;
         }
      }
   }

   return SUCCESS;
}


//345678901234567890123456789012345678901234567890123456789012345678901234567890

// ********************************************************************************************
// ********************************************************************************************
// ********************************************************************************************
// ********************************************************************************************

#if defined MAPPEDFILE_FILE_INPUT || defined FULL_STRIP_READ
static DWORD dwSystemPageSize;    // Common use
#endif

// CGeoTiff class

CGeoTiff::CGeoTiff( ) :
#if defined MAPPEDFILE_FILE_INPUT || defined READFILE_FILE_INPUT
   m_hGeoTiffFile( INVALID_HANDLE_VALUE ),   // No file open
#endif
#ifdef MAPPEDFILE_FILE_INPUT
   m_hFileMapping( INVALID_HANDLE_VALUE ),   // No mapping object
   m_pCallingImageMap( NULL ),               // No caller
#endif
   m_bCGeoTiffReferenced( FALSE )
{
   // constructor

   // initialize file pointer to NULL
   m_file_ptr = NULL;

   // initialize tag array pointer to NULL
   m_tags = NULL;

   // initialize geokey array pointer to NULL
   m_geokeys = NULL;

   // initialize color map array pointers to NULL
   m_red_color_map = NULL;
   m_green_color_map = NULL;
   m_blue_color_map = NULL;

   // initialize tile arrays
   m_tile_red_array = NULL;
   m_tile_green_array = NULL;
   m_tile_blue_array = NULL;
   m_tile_color_indices = NULL;

      m_image = NULL;

   // init jpeg table len
   m_jpeg_table_len = 0;

   // init linear units
   m_linear_units = 0;  // unknown

      m_projection_type = "<Unknown>";

   // init transparency
   m_has_transparent_color = FALSE;
   m_transparent_color = RGB(0,0,0);

   // buffer
   m_buf_img = NULL;
   m_buf_start_row = -1;
   m_buf_end_row = -1;

   m_highest_level = 0;

   m_max_16bit_value = 65535;

   m_has_elevation_data = FALSE;

   m_directory_offset_list = NULL;

#ifndef FREAD_FILE_INPUT
   m_iobMappedFileIOBufDesc._base = NULL;
#endif

   // clear
   clear( );

#ifdef MAPPEDFILE_FILE_INPUT
   m_iobMappedFileIOBufDesc._base = NULL; // No memory is mapped
#endif
#ifdef READFILE_FILE_INPUT
   ZeroMemory( &m_ovlpOverlapped, sizeof(m_ovlpOverlapped) );
#endif

}

// ********************************************************************************************
// ********************************************************************************************

CGeoTiff::~CGeoTiff( )
{
   // destructor

   // clear
   clear( );
#if defined MAPPEDFILE_FILE_INPUT || defined READFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
   ClearFile();

   #if defined READFILE_FILE_INPUT
      if ( m_ovlpOverlapped.hEvent != NULL )
         CloseHandle( m_ovlpOverlapped.hEvent );
   #endif
#endif
}



void CGeoTiff::clear( )
{
   // set loaded flag to false
   m_file_loaded = FALSE;

   // clear file name
   m_tiff_file_name = "";

   // clear error flag
   m_error = FALSE;

   // clear error description
   m_cumulative_error_description = "";
   m_new_error_description = "";

   // byte order
   m_byte_order = GEOTIFF_BYTE_ORDER_NOT_DEFINED;

   // number of directories
   m_num_directories = 0;

   // free directory offsets
   if (m_directory_offset_list != NULL)
      free(m_directory_offset_list);
   m_directory_offset_list = NULL;
   m_directory_offset = 0;

   // if file is open, close and set file pointer to null
   ClearFile();      // Clear any file mapping
   if( m_file_ptr != NULL )
   {
//      fclose( m_file_ptr );
      FIL_close( m_file_ptr );
      m_file_ptr = NULL;
   }

   // number of tags
   m_num_tags = 0;

   // delete any allocated tags
   if( m_tags != NULL )
   {
      delete [] m_tags;
      m_tags = NULL;
   }

   m_apbJpegTable.reset( NULL );
   m_jpeg_table = nullptr;

   // geokey directory info
   m_geokey_directory_version = 0;
   m_geokey_directory_revision = 0;
   m_geokey_directory_minor_revision = 0;
   m_num_geokeys = 0;

   // delete any allocated geokeys
   if( m_geokeys != NULL )
   {
      delete [] m_geokeys;
      m_geokeys = NULL;
   }

   // image information
   m_image_width = 0;
   m_image_length = 0;
   m_num_pixels = 0;
   m_compression_scheme = 0;
   m_predictor = 1;
   m_photometric_interpretation = 0;
   m_rows_per_strip = 0;
   m_num_strips = 0;
   m_x_resolution = 0.0;
   m_y_resolution = 0.0;
   m_resolution_unit = 0;
   m_planar_configuration = GEOTIFF_UNKNOWN_FORMAT;
   m_samples_per_pixel = 0;
   m_bits_per_sample = 0;
   m_fill_order = 0;
   m_orientation = 0;
   m_image_type_supported = FALSE;
   m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
   m_image_type_description = "Image type is unknown.";

   m_min_sample_value = 0;
   m_max_sample_value = 0;
   m_geog_linear_units = 0;
   m_geog_angular_units = 0;
   m_geog_azimuth_units = 0;
   m_geog_linear_unit_size = 0;
   m_geog_angular_unit_size = 0;
   m_geog_linear_units_present = FALSE;
   m_geog_angular_units_present = FALSE;
   m_geog_azimuth_units_present = FALSE;
   m_geog_linear_unit_size_present = FALSE;
   m_geog_angular_unit_size_present = FALSE;

   // color map
   m_color_map_present = FALSE;
   m_num_color_map_values = 0;
   if( m_red_color_map != NULL )
   {
      MEM_free( m_red_color_map );
      m_red_color_map = NULL;
   }
   if( m_green_color_map != NULL )
   {
      MEM_free( m_green_color_map );
      m_green_color_map = NULL;
   }
   if( m_blue_color_map != NULL )
   {
      MEM_free( m_blue_color_map );
      m_blue_color_map = NULL;
   }

   // max image strip size
   m_max_strip_byte_count = 0;

   // histogram
   m_apuiHistogram.reset( NULL );
   m_puiHistogram = nullptr;

   // geodata
   m_geodata_present = FALSE;
   m_geodata_supported = FALSE;
   m_geodata_error_code = GEOTIFF_GEODATA_NOT_PRESENT;
   m_geodata_error_message = "";

   // geodata object
   m_geodata.clear( );

   // tiepoint transform
   m_tiepoint_transform.clear( );

   // tile data
   m_image_is_tiled = FALSE;
   m_tile_width = 0;
   m_tile_length = 0;
   m_num_tile_pixels = 0;
   m_num_tiles_across = 0;
   m_num_tiles_down = 0;
   m_num_tiles = 0;
   m_using_tfw_file = FALSE;
   m_using_fid_file = FALSE;;
   m_pixel_size = 0.0;

   m_cropped_ul_lat = 0.0;
   m_cropped_ul_lon = 0.0;
   m_cropped_ur_lat = 0.0;
   m_cropped_ur_lon = 0.0;
   m_cropped_lr_lat = 0.0;
   m_cropped_lr_lon = 0.0;
   m_cropped_ll_lat = 0.0;
   m_cropped_ll_lon = 0.0;
   m_is_cropped = FALSE;
   m_scale = NULL_SCALE;
   m_image_number = 1;

   if( m_tile_red_array != NULL )
   {
      MEM_free( m_tile_red_array );
      m_tile_red_array = NULL;
   }
   if( m_tile_green_array != NULL )
   {
      MEM_free( m_tile_green_array );
      m_tile_green_array = NULL;
   }
   if( m_tile_blue_array != NULL )
   {
      MEM_free( m_tile_blue_array );
      m_tile_blue_array = NULL;
   }
   if( m_tile_color_indices != NULL )
   {
      MEM_free( m_tile_color_indices );
      m_tile_color_indices = NULL;
   }

      if (m_image != NULL)
   {
      delete m_image;
      m_image = NULL;
   }

   if (m_buf_img)
   {
      free(m_buf_img);
      m_buf_img = NULL;
   }

}  // CGeoTiff::clear()


#if defined MAPPEDFILE_FILE_INPUT || defined READFILE_FILE_INPUT || defined LLFILE_FILE_INPUT

VOID CGeoTiff::ClearFile()
{
#if defined MAPPEDFILE_FILE_INPUT
   if ( m_iobMappedFileIOBufDesc._base != NULL )
   {
      UnmapViewOfFile( m_iobMappedFileIOBufDesc._base );
      m_iobMappedFileIOBufDesc._base = NULL;
   }

   if ( m_hFileMapping != INVALID_HANDLE_VALUE )
   {
      CloseHandle( m_hFileMapping );
      m_hFileMapping = INVALID_HANDLE_VALUE;
   }
#endif

#if defined LLFILE_FILE_INPUT
   if ( m_hGeoTiffFile != 0 )
   {
      _close( m_hGeoTiffFile );
      m_hGeoTiffFile = 0;
   }
#else
   if ( m_hGeoTiffFile != INVALID_HANDLE_VALUE )
   {
      CloseHandle( m_hGeoTiffFile );
      m_hGeoTiffFile = INVALID_HANDLE_VALUE;
   }
#endif
}  // ClearFile()

#endif   // def MAPPEDFILE_FILE_INPUT || READFILE_FILE_INPUT || LLFILE_FILE_INPUT

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::load( const char *tiff_file_name, BOOL &image_type_supported,
                    int &image_type, CString &image_type_description,
                    int &image_width, int &image_length,
                    BOOL &geodata_present, BOOL &geodata_supported,
                    CString &error_message )
{
   // this function loads the specified tiff file, obtaining information
   // about the file
   // error message returned in error_message argument
   // returns SUCCESS or FAILURE

   char byte_order_string[3];   // first 2 characters determine byte order
   short short_value;
   int len, pos;

   error_message = "";
   image_type_supported = FALSE;
   image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
   image_type_description = "";
   image_width = 0;
   image_length = 0;
   m_image_is_tiled = FALSE;
   m_tile_width = 0;
   m_tile_length = 0;
   m_num_tile_pixels = 0;
   m_num_tiles_across = 0;
   m_num_tiles_down = 0;
   m_num_tiles = 0;

#if defined MAPPEDFILE_FILE_INPUT || defined FULL_STRIP_READ
   SYSTEM_INFO si;
   GetSystemInfo( &si );
   dwSystemPageSize = si.dwPageSize;    // For optimizations
#endif

   geodata_present = FALSE;
   geodata_supported = FALSE;

   // clear first
   int imgnum = m_image_number;
   clear( );
   m_image_number = imgnum;

   // Attempt to open file
   if ( SUCCESS != gtfOpen( tiff_file_name ) )
   {
      error_message = m_cumulative_error_description;
      return FAILURE;
   }

   len = m_tiff_file_name.GetLength();
   pos = m_tiff_file_name.ReverseFind('\\');
   if (pos >= 0)
      m_raw_filename = m_tiff_file_name.Right(len-pos-1);


   // read first two bytes, which determine byte order
   byte_order_string[2] = NULL;
   if ( gtfRead( 0L, (PBYTE) &byte_order_string, 2 ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description =
         "Error reading byte order string in file header.";
      error_message = m_cumulative_error_description;
      return FAILURE;
   }

   // test for byte order
   if( strcmp( byte_order_string, "II" ) == 0 )
   {
      // byte order is Intel little-endian
      m_byte_order = GEOTIFF_LITTLE_ENDIAN;
   }
   else
      if( strcmp( byte_order_string, "MM" ) == 0 )
      {
         // byte order is Motorola big-endian
         m_byte_order = GEOTIFF_BIG_ENDIAN;
      }
   if( m_byte_order == GEOTIFF_BYTE_ORDER_NOT_DEFINED )
   {
      m_error = TRUE;
      m_cumulative_error_description = "Error in byte order string: ";
      m_cumulative_error_description += byte_order_string;
      error_message = m_cumulative_error_description;
      return FAILURE;
   }

   // check for short int with value 42 in bytes 2-3
   if( read_signed_short( short_value ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description =
         "Error reading bytes 2-3 of file header.";
      error_message = m_cumulative_error_description;
      return FAILURE;
   }
   if( short_value != 42 )
   {
      m_error = TRUE;
      m_cumulative_error_description =
         "Error in value of bytes 2-3 of file header.";
      error_message = m_cumulative_error_description;
      return FAILURE;
   }

   int next_dir, first_dir, rslt;
   int cnt = 0;

   // read offset of first image file directory from bytes 4-7
   if( read_unsigned_int( m_directory_offset ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description =
         "Error reading first directory offset in file header.";
      error_message = m_cumulative_error_description;
      return FAILURE;
   }
   m_num_directories = 1;
   first_dir = m_directory_offset;

   // read tag directory and tags
   if( read_directory(&next_dir ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description += "Error in Tag Data: ";
      m_cumulative_error_description += m_new_error_description;
      m_cumulative_error_description += " / ";
   }

   // check for other directories
   while (next_dir != NULL)
   {
      m_directory_offset = next_dir;
      rslt = read_directory(&next_dir );
      if (rslt != SUCCESS)
      {
         error_message = "Error reading directory offset in file header.";
         return FAILURE;
      }
      m_num_directories++;
   }

   m_directory_offset_list = (int*) malloc(m_num_directories * sizeof(int));

   m_directory_offset = first_dir;
   // check for other directories
   next_dir = first_dir;
   while (next_dir != NULL)
   {
      m_directory_offset_list[cnt] = next_dir;
      cnt++;
      m_directory_offset = next_dir;
      rslt = read_directory(&next_dir );
      if (rslt != SUCCESS)
      {
         error_message = "Error reading directory offset in file header.";
         return FAILURE;
      }
   }

   // read the first directory again
      // read tag directory and tags
// m_directory_offset = first_dir;
   if ( m_image_number > m_num_directories )
   {
      error_message = _T("Invalid image number");
      return FAILURE;
   }

   m_directory_offset = m_directory_offset_list[m_image_number-1];

   if ( read_directory(&next_dir ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description += "Error in Tag Data: ";
      m_cumulative_error_description += m_new_error_description;
      m_cumulative_error_description += " / ";
   }

   // inspect tags for image information
   if( inspect_tags( ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description += "Error in Image Data: ";
      m_cumulative_error_description += m_new_error_description;
      m_cumulative_error_description += " / ";
   }

   // read geokey directory and geokeys
   if( read_geokey_directory( ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description += "Error in GeoKey Data: ";
      m_cumulative_error_description += m_new_error_description;
      m_cumulative_error_description += " / ";
   }

   // file has been loaded
   m_file_loaded = TRUE;

   // set error message argument
   error_message = m_cumulative_error_description;

   // check for geodata
   if( !m_error )
      check_geodata( );

   // set image type arguments
   image_type_supported = m_image_type_supported;
   image_type = m_image_type;
   image_type_description = m_image_type_description;
   image_width = m_image_width;
   image_length = m_image_length;
   geodata_present = m_geodata_present;
   geodata_supported = m_geodata_supported;


   if (m_geodata_supported)
   {
      // check the fid file (if there is one) for units
      read_fid_file(m_tiff_file_name);
   }
   else
   {
      int rslt;
      CString error_msg;
      double clat, clon;
      double lat_per_pix, lon_per_pix;
      BOOL georef = TRUE;

      rslt = read_world_file(m_tiff_file_name, &lat_per_pix, &lon_per_pix, &clat, &clon, error_msg);
      if (rslt)
      {
         m_using_tfw_file = TRUE;
         if ((clat < -90.0) || (clat > 90.0))
            georef = FALSE;

         if ((clon < -180.0) || (clon > 180.0))
            georef = FALSE;

         m_geodata.m_model_pixel_scale_present = TRUE;
         m_geodata.m_dXOffset = 0.0;
         m_geodata.m_dYOffset = 0.0;
         m_geodata.m_dLScaleX = lon_per_pix;
         m_geodata.m_dLScaleY = 0.0;      // No rotation
         m_geodata.m_dLOffset = clon;
         m_geodata.m_dPScaleX = 0.0;
         m_geodata.m_dPScaleY = -lat_per_pix;
         m_geodata.m_dPOffset = clat;
         m_geodata.CalcFwdCoeffs();    // Fill fwd coeffs
         m_geodata.m_model_tie_point_x = (double*) malloc( 1 * sizeof(double) );
         m_geodata.m_model_tie_point_y = (double*) malloc( 1 * sizeof(double) );
         m_geodata.m_model_tie_point_z = (double*) malloc( 1 * sizeof(double) );
         *m_geodata.m_model_tie_point_x = clon;
         *m_geodata.m_model_tie_point_y = clat;
         *m_geodata.m_model_tie_point_z = 0.0;
         m_geodata.m_model_tie_point_hpix = (double*) malloc( 1 * sizeof(double) );
         m_geodata.m_model_tie_point_vpix = (double*) malloc( 1 * sizeof(double) );
         *m_geodata.m_model_tie_point_hpix = 0.0;
         *m_geodata.m_model_tie_point_vpix = 0.0;
         m_geodata.m_model_tie_point_present = TRUE;
         if (georef)
         {
            m_geodata.m_gt_raster_type = 1;
            m_geodata.m_gt_raster_type_present = TRUE;
            m_geodata.m_gt_model_type = GEOTIFF_MODEL_TYPE_GEOGRAPHIC;
            m_geodata.m_gt_model_type_present = TRUE;
            m_geodata.m_projected_cs_type = GEOTIFF_GCS_WGS_84;
            m_geodata.m_projected_cs_type_present = TRUE;
         }

         m_geodata_present = TRUE;

         int rslt = read_fid_file(m_tiff_file_name);
         if (rslt)
         {
            m_using_fid_file = TRUE;
            m_geodata.m_gt_raster_type = 1;
            m_geodata.m_gt_raster_type_present = TRUE;
            m_geodata.m_gt_model_type_present;
         }
         init_geodata();
         geodata_supported = m_geodata_supported;
         if (geodata_supported)
            geodata_present = TRUE;

      }
   }


   // fill in m_image structure
   if (m_image != NULL)
      delete m_image;
   m_image = new C_nitf_image;
   if (m_image == NULL)
   {
      error_message = "Memory allocation error";
      return FAILURE;
   }

// m_filename = m_tiff_file_name;
   m_image->m_image_width = m_image_width;
   m_image->m_image_height = m_image_length;
   m_image->m_bpp = m_bits_per_sample;
   m_image->m_is_tiled = m_image_is_tiled;

   if (m_image_is_tiled)
   {
      m_image->m_tile_size_x = m_tile_width;
      m_image->m_tile_size_y = m_tile_length;
      m_image->m_tile_cnt_x = m_num_tiles_across;
      m_image->m_tile_cnt_y = m_num_tiles_down;
   }


   // return SUCCESS or FAILURE depending on whether errors ocurred
   if ( m_error )
      return FAILURE;
   else
      return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_geodata( CGeoData &geodata, int &error_code,
                           CString &error_message )
{
   // this function returns the geodata in the geodata object
   // all quantities are returned in length units of meters and
   // angle units of degrees by adjusting file values if necessary
   // returns SUCCESS or FAILURE and descriptive error code and message
   // returns FAILURE if geodata is not present or is not supported

   // return failure if no file loaded or there was an error loading the
   // file
   if( !m_file_loaded || m_error )
   {
      m_geodata_error_message = "File is not loaded.";
      return FAILURE;
   }

   geodata = m_geodata;
   error_code = m_geodata_error_code;
   error_message = m_geodata_error_message;

   // return failure if geodata is not present
   if( m_geodata_present )
      return SUCCESS;
   else
      return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_image_description( CString &image_description )
{
   // returns the image description if present
   // returns SUCCESS or FAILURE if image description tag is not present

   // return failure if no file loaded or there was an error loading the
   // file
   if ( !m_file_loaded || m_error )
      return FAILURE;

   return GetTagStringValue( GEOTIFF_IMAGE_DESCRIPTION_TAG, image_description );
}

int CGeoTiff::get_software( CString &software )
{
   // returns the software string if present
   // returns SUCCESS or FAILURE if software tag is not present

   // return failure if no file loaded or there was an error loading the
   // file
   if ( !m_file_loaded || m_error )
      return FAILURE;

   return GetTagStringValue( GEOTIFF_SOFTWARE_TAG, software );
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_image_width( int &image_width )
{
   // returns the image width in the argument
   // returns SUCCESS or FAILURE

   // return failure if no file loaded or there was an error loading the
   // file
   if( !m_file_loaded || m_error )
      return FAILURE;

   image_width = m_image_width;
   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_image_length( int &image_length )
{
   // returns the image length in the argument
   // returns SUCCESS or FAILURE

   // return failure if no file loaded or there was an error loading the
   // file
   if( !m_file_loaded || m_error )
      return FAILURE;

   image_length = m_image_length;
   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_image_type( int &image_type )
{
   // returns the image type in the argument
   // returns SUCCESS or FAILURE

   // return failure if no file loaded or there was an error loading the
   // file
   if( !m_file_loaded || m_error )
      return FAILURE;

   image_type = m_image_type;
   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_scale( int &horizontal_scale, int &vertical_scale )
{
   // returns the horizontal and vertical scales of the image if available
   // such as 24000 for a 1:24000 image
   // returns SUCCESS or FAILURE

   int tag_index;
   unsigned short resolution_unit;
   double horizontal_meters_per_pixel, vertical_meters_per_pixel;
   double horizontal_resolution, vertical_resolution;
   double h_scale, v_scale;

   // check for model pixel scale present
   if( !m_geodata.m_model_pixel_scale_present )
      goto FAIL;

   horizontal_meters_per_pixel = fabs( m_geodata.m_dLScaleX );
   vertical_meters_per_pixel = fabs( m_geodata.m_dPScaleY );

   // get resolution (scanning) unit
   if( find_tag( GEOTIFF_RESOLUTION_UNIT_TAG, tag_index ) != SUCCESS )
      goto FAIL;
   if (m_tags[tag_index].m_short_values != NULL)
      resolution_unit = m_tags[tag_index].m_short_values[0];

   // get horizontal scanning resolution
   if( find_tag( GEOTIFF_X_RESOLUTION_TAG, tag_index ) != SUCCESS )
      goto FAIL;
   if (m_tags[tag_index].m_double_values != NULL)
      horizontal_resolution = m_tags[tag_index].m_double_values[0];

   // get vertical scanning resolution
   if( find_tag( GEOTIFF_Y_RESOLUTION_TAG, tag_index ) != SUCCESS )
      goto FAIL;
   if (m_tags[tag_index].m_double_values != NULL)
      vertical_resolution = m_tags[tag_index].m_double_values[0];

   switch( resolution_unit )
   {
      case GEOTIFF_RESOLUTION_UNIT_INCH:
         h_scale = horizontal_resolution * horizontal_meters_per_pixel *
                   (100.0/2.54);
         v_scale = vertical_resolution * vertical_meters_per_pixel *
                   (100.0/2.54);
         break;

      case GEOTIFF_RESOLUTION_UNIT_CENTIMETER:
         h_scale = horizontal_resolution * horizontal_meters_per_pixel *
                   100.0;
         v_scale = vertical_resolution * vertical_meters_per_pixel *
                   100.0;
         break;

      default:
         goto FAIL;
   }

   // round scales to nearest integers
   horizontal_scale = (int)( h_scale + 0.5 );
   vertical_scale  = (int)( v_scale + 0.5 );
   return SUCCESS;

FAIL:
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::init_geodata()
{
   // check that raster type is present recognized
   if( !m_geodata.m_gt_raster_type_present )
   {
      m_geodata_error_message = "Raster type geokey is missing.";
      goto FAIL;
   }
   if( m_geodata.m_gt_raster_type != GEOTIFF_RASTER_PIXEL_IS_AREA &&
       m_geodata.m_gt_raster_type != GEOTIFF_RASTER_PIXEL_IS_POINT )
   {
      m_geodata_error_message = "Raster type is not supported.";
      goto FAIL;
   }

   // model type must be present
   if( !m_geodata.m_gt_model_type_present )
   {
      m_geodata_error_message = "Model type geokey is missing.";
      goto FAIL;
   }

      // add support for projected file with improper model type value
   if (m_geodata.m_projected_cs_type_present)
      m_geodata.m_gt_model_type = GEOTIFF_MODEL_TYPE_PROJECTED;

// test for model type
   switch( m_geodata.m_gt_model_type )
   {
      case GEOTIFF_MODEL_TYPE_PROJECTED:
         // projection coordinate system must be present
         if( !m_geodata.m_projected_cs_type_present )
         {
            m_geodata_error_message =
               "Projection coordinate system geokey is missing.";
            goto FAIL;
         }
         // check for user-defined projection coordinate_system
         if( m_geodata.m_projected_cs_type == GEOTIFF_USER_DEFINED )
         {
            if( !m_geodata.m_proj_coord_trans_present )
            {
               m_geodata_error_message =
               "User-defined projection coordinate transformation not present.";
               goto FAIL;
            }
            switch( m_geodata.m_proj_coord_trans )
            {
               case GEOTIFF_CT_LAMBERT_CONF_CONIC_2SP:
                  // check for necessary data.  The origin of a 2SP Lambert is
                  // ProjFalseOrigin{Lat,Long} (3084/3085) per the GeoTIFF
                  // spec - ProjNatOrigin* (3080/3081) is the 1SP spelling.
                  // Older libgeotiff output wrote the natural-origin keys
                  // here, and current FAA sectionals write the false-origin
                  // ones, so either spelling of either axis is accepted.
                  if( !m_geodata.m_proj_std_parallel_1_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 1 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_std_parallel_2_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 2 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_nat_origin_long_present &&
                      !m_geodata.m_proj_false_origin_long_present )
                  {
                     m_geodata_error_message =
                        "Origin longitude is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_false_origin_lat_present &&
                      !m_geodata.m_proj_nat_origin_lat_present )
                  {
                     m_geodata_error_message =
                        "Origin latitude is missing.";
                     goto FAIL;
                  }
              m_projection_type = "Lambert Conformal Conic";
                  m_geodata_supported = TRUE;
                  break;
               case GEOTIFF_CT_ALBERS_EQUAL_AREA:
                  // check for necessary data
                  if( !m_geodata.m_proj_std_parallel_1_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 1 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_std_parallel_2_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 2 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_nat_origin_lat_present )
                  {
                     m_geodata_error_message =
                        "Natural origin latitude is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_center_lat_present )
                  {
                     m_geodata_error_message = "Center latitude is missing.";
                     goto FAIL;
                  }
                  // we should test for center longitude, but the sample
                  // acea.tif doesn't include center longitude. so, to be able
                  // to load this sample, we disable this test and 0.0 is used
                  // for center longitude
//                  if( !m_geodata.m_proj_center_long_present )
//                  {
//                     m_geodata_error_message = "Center longitude is missing.";
//                     goto FAIL;
//                  }
              m_projection_type = "Albers Equal Area";
                  m_geodata_supported = TRUE;
                  break;
               default:
                  // user-defined
                  m_geodata_supported = FALSE;
                  break;
            }
         }
         else
         {
            // pre-defined projection coordinate system

            if( m_geodata.m_projected_cs_type >= 26703 &&
                m_geodata.m_projected_cs_type <= 26722 )
            {
               // NAD27 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD27;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 26903 &&
                m_geodata.m_projected_cs_type <= 26923 )
            {
               // NAD83 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD83;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32201 &&
                m_geodata.m_projected_cs_type <= 32260 )
            {
               // WGS72 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_72;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32301 &&
                m_geodata.m_projected_cs_type <= 32360 )
            {
               // WGS72 UTM southern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_72;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 10000000.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32601 &&
                m_geodata.m_projected_cs_type <= 32660 )
            {
               // WGS84 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_84;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32701 &&
                m_geodata.m_projected_cs_type <= 32760 )
            {
               // WGS84 UTM southern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_84;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 10000000.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if(( m_geodata.m_projected_cs_type >= 26729 && m_geodata.m_projected_cs_type <= 26798 ) ||
            ( m_geodata.m_projected_cs_type >= 32001 && m_geodata.m_projected_cs_type <= 32060 ))
            {
               // state plane NAD27
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD27;
               m_geodata.m_geographic_type_present = TRUE;
            m_projection_type = "State Plane";
               m_geodata_supported = TRUE;
               break;
            }

            if(( m_geodata.m_projected_cs_type >= 26929 && m_geodata.m_projected_cs_type <= 26998 ) ||
            ( m_geodata.m_projected_cs_type >= 32100 && m_geodata.m_projected_cs_type <= 32161 ))
            {
               // state plane NAD83
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD83;
               m_geodata.m_geographic_type_present = TRUE;
            m_projection_type = "State Plane";
               m_geodata_supported = TRUE;
               break;
            }

            m_geodata_supported = FALSE;
         }
         break;

      case GEOTIFF_MODEL_TYPE_GEOGRAPHIC:
         if( m_geodata.m_model_pixel_scale_present &&
             m_geodata.m_num_tie_points == 1 )
         {
            // direct lat/long
            m_geodata_supported = TRUE;
         }
         else
//         if( m_geodata.m_num_tie_points >= 3 && !m_geodata.m_model_pixel_scale_present )
         if( m_geodata.m_num_tie_points >= 3 )  // use only the tie points and ignore the scale
         {
            // georeferenced by tiepoints only
            // set up tiepoint transform
            if( m_tiepoint_transform.define_tiepoints(
                m_geodata.m_num_tie_points, m_geodata.m_model_tie_point_hpix,
                m_geodata.m_model_tie_point_vpix, m_geodata.m_model_tie_point_y,
                m_geodata.m_model_tie_point_x ) != SUCCESS )
            {
               m_geodata_error_message =
                  "Tiepoint transformation is invalid.";
               goto FAIL;
            }
         m_projection_type = "Equal Arc";
            m_geodata_supported = TRUE;
         }
         break;

      case GEOTIFF_MODEL_TYPE_GEOCENTRIC:
         m_geodata_error_message = "Geocentric model type is not supported.";
         goto FAIL;

      default:
         m_geodata_error_message = "Geometric model type is unknown.";
         goto FAIL;
   }

   // assign datum
   switch( m_geodata.m_geographic_type )
   {
      case GEOTIFF_GCS_NAD27:
         m_geodata.m_geog_geodetic_datum =
            GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1927;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_ellipsoid = GEOTIFF_ELLIPSE_CLARKE_1866;
         m_geodata.m_geog_ellipsoid_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378206.4;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356583.8;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 294.98;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      case GEOTIFF_GCS_NAD83:
         m_geodata.m_geog_geodetic_datum =
            GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1983;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_ellipsoid = GEOTIFF_ELLIPSE_GRS_1980;
         m_geodata.m_geog_ellipsoid_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378137.0;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356752.314;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 298.2572221010;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      case GEOTIFF_GCS_WGS_72:
         m_geodata.m_geog_geodetic_datum = GEOTIFF_DATUM_WGS72;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378135.0;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356750.5;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 298.26;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      case GEOTIFF_GCS_WGS_84:
      case GEOTIFF_GCSE_WGS84:
         m_geodata.m_geog_geodetic_datum = GEOTIFF_DATUM_WGS84;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_ellipsoid = GEOTIFF_ELLIPSE_WGS_84;
         m_geodata.m_geog_ellipsoid_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378137.0;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356752.3;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 298.2572235630;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      default:
         m_geodata_error_message =
            "Geographic coordinate system is not supported.";
         goto FAIL;
   }

   m_geodata_present = TRUE;
   m_geodata_error_code = GEOTIFF_GEODATA_OK;
   m_geodata_error_message = "";
   return;

FAIL:
   m_geodata_present = TRUE;
   m_geodata_supported = FALSE;
   m_geodata_error_code = GEOTIFF_GEODATA_ERROR;
   return;
}
// end of init_geodata

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::check_geodata(  )
{
   // this function checks for the presence of geodata and, if present,
   // loads the geodata object
   // all quantities are converted to length units of meters and
   // angle units of degrees by adjusting file values if necessary

   int i, j, index;

//   unsigned short geog_linear_units, geog_angular_units, geog_azimuth_units;
//   double geog_linear_unit_size, geog_angular_unit_size;
//   BOOL geog_linear_units_present, geog_angular_units_present;
//   BOOL geog_azimuth_units_present;
//   BOOL geog_linear_unit_size_present, geog_angular_unit_size_present;

   unsigned short proj_linear_units;
   double proj_linear_unit_size;
   BOOL proj_linear_units_present, proj_linear_unit_size_present;

   unsigned short vertical_units;
   BOOL vertical_units_present;

   // clear the geodata structure
   m_geodata.clear( );

   // check for geokeys present
   if( m_num_geokeys == 0 )
   {
      m_geodata_present = FALSE;
      m_geodata_supported = FALSE;
      m_geodata_error_code = GEOTIFF_GEODATA_NOT_PRESENT;
      m_geodata_error_message = "Geodata not present";
      return;
   }
   else
      m_geodata_present = TRUE;

   // initialize geographic units as unspecified
   m_geog_linear_units = 0;
   m_geog_angular_units = 0;
   m_geog_azimuth_units = 0;
   m_geog_linear_unit_size = 0.0;
   m_geog_angular_unit_size = 0.0;
   m_geog_linear_units_present = FALSE;
   m_geog_angular_units_present = FALSE;
   m_geog_azimuth_units_present = FALSE;
   m_geog_linear_unit_size_present = FALSE;
   m_geog_angular_unit_size_present = FALSE;

   // initialize projection units as unspecified
   proj_linear_units = 0;
   proj_linear_unit_size = 0.0;
   proj_linear_units_present = FALSE;
   proj_linear_unit_size_present = FALSE;

   // initialize vertical units as unspecified
   vertical_units = 0;
   vertical_units_present = FALSE;

   // fill in data from tags and geokeys

   // get model pixel scale
   if( find_tag( GEOTIFF_MODEL_PIXEL_SCALE_TAG, index ) == SUCCESS )
   {
      CTiffTag& tag = m_tags[ index ];
      m_geodata.m_dLScaleX = tag.m_double_values[0];
      m_geodata.m_dPScaleY = -fabs( tag.m_double_values[1] );
      m_geodata.m_dHScaleZ = tag.m_double_values[2];
      if( m_geodata.m_dLScaleX == 0.0 )
      {
         m_geodata_error_message = "Model X pixel scale is zero.";
         goto FAIL;
      }
      if( m_geodata.m_dPScaleY == 0.0 )
      {
         m_geodata_error_message = "Model Y pixel scale is zero.";
         goto FAIL;
      }
      m_geodata.CalcFwdCoeffs();
      m_geodata.m_model_pixel_scale_present = TRUE;
   }

   // Get model transform
   BOOL bModelTransformationPresent;  // no initializer: FAIL gotos above jump past this declaration
   bModelTransformationPresent = FALSE;
   if ( find_tag( GEOTIFF_MODEL_TRANSFORMATION_TAG, index ) == SUCCESS )
   {
      CTiffTag& tag = m_tags[ index ];
      if ( tag.m_count != 16 )
      {
         m_geodata_error_message = "Model transformation matrix is wrong size.";
         goto FAIL;
      }
      m_geodata.m_dLScaleX = tag.m_double_values[ 0 ];
      m_geodata.m_dLScaleY = tag.m_double_values[ 1 ];
      m_geodata.m_dLOffset = tag.m_double_values[ 3 ];
      m_geodata.m_dPScaleX = tag.m_double_values[ 4 ];
      m_geodata.m_dPScaleY = tag.m_double_values[ 5 ];
      m_geodata.m_dPOffset = tag.m_double_values[ 7 ];
      m_geodata.m_dHScaleZ = tag.m_double_values[ 10 ];
      m_geodata.m_dHOffset = tag.m_double_values[ 11 ];
      m_geodata.CalcFwdCoeffs();
      bModelTransformationPresent = TRUE;
   }

   if( find_tag( GEOTIFF_MODEL_TIEPOINT_TAG, index ) == SUCCESS )
      m_geodata.m_num_tie_points = m_tags[index].m_count / 6;

   if( m_geodata.m_num_tie_points > 0 )
   {
      // allocate memory for tiepoints
      m_geodata.m_model_tie_point_hpix =
         (double*)MEM_malloc( m_geodata.m_num_tie_points * sizeof(double) );
      if( m_geodata.m_model_tie_point_hpix == NULL )
         goto FAIL;
      m_geodata.m_model_tie_point_vpix =
         (double*)MEM_malloc( m_geodata.m_num_tie_points * sizeof(double) );
      if( m_geodata.m_model_tie_point_vpix == NULL )
         goto FAIL;
      m_geodata.m_model_tie_point_zpix =
         (double*)MEM_malloc( m_geodata.m_num_tie_points * sizeof(double) );
      if( m_geodata.m_model_tie_point_zpix == NULL )
         goto FAIL;
      m_geodata.m_model_tie_point_x =
         (double*)MEM_malloc( m_geodata.m_num_tie_points * sizeof(double) );
      if( m_geodata.m_model_tie_point_x == NULL )
         goto FAIL;
      m_geodata.m_model_tie_point_y =
         (double*)MEM_malloc( m_geodata.m_num_tie_points * sizeof(double) );
      if( m_geodata.m_model_tie_point_y == NULL )
         goto FAIL;
      m_geodata.m_model_tie_point_z =
         (double*)MEM_malloc( m_geodata.m_num_tie_points * sizeof(double) );
      if( m_geodata.m_model_tie_point_z == NULL )
         goto FAIL;

      // copy the tie point coordinates
      CTiffTag& tag = m_tags[ index ];
      for( i = 0; i < m_geodata.m_num_tie_points; i++ )
      {
         j = 6 * i;
         m_geodata.m_model_tie_point_hpix[i] =
            tag.m_double_values[j];
         m_geodata.m_model_tie_point_vpix[i] =
            tag.m_double_values[j+1];
         m_geodata.m_model_tie_point_zpix[i] =
            tag.m_double_values[j+2];
         m_geodata.m_model_tie_point_x[i] =
            tag.m_double_values[j+3];
         m_geodata.m_model_tie_point_y[i] =
            tag.m_double_values[j+4];
         m_geodata.m_model_tie_point_z[i] =
            tag.m_double_values[j+5];
      }

      m_geodata.m_model_tie_point_present = TRUE;
      if ( m_geodata.m_model_pixel_scale_present
         && !bModelTransformationPresent )
      {
         m_geodata.m_dLOffset = m_geodata.m_model_tie_point_x[ 0 ];
         m_geodata.m_dPOffset = m_geodata.m_model_tie_point_y[ 0 ];
      }
   }

   // loop through all geokeys
   for( index = 0; index < m_num_geokeys; index++ )
   {
      switch( m_geokeys[index].m_geokey_id )
      {
         case GEOTIFF_GT_MODEL_TYPE_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_gt_model_type = m_geokeys[index].m_short_values[0];
            m_geodata.m_gt_model_type_present = TRUE;
          }
            break;
         case GEOTIFF_GT_RASTER_TYPE_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_gt_raster_type = m_geokeys[index].m_short_values[0];
            m_geodata.m_gt_raster_type_present = TRUE;
          }
            break;
         case GEOTIFF_GT_CITATION_GEOKEY:
          if (m_geokeys[index].m_ascii_values != NULL)
          {
            m_geodata.m_gt_citation = m_geokeys[index].m_ascii_values;
            m_geodata.m_gt_citation_present = TRUE;
          }
            break;

         case GEOTIFF_GEOGRAPHIC_TYPE_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_geographic_type = m_geokeys[index].m_short_values[0];
            m_geodata.m_geographic_type_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_CITATION_GEOKEY:
          if (m_geokeys[index].m_ascii_values != NULL)
          {
            m_geodata.m_geog_citation = m_geokeys[index].m_ascii_values;
            m_geodata.m_geog_citation_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_GEODETIC_DATUM_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_geog_geodetic_datum = m_geokeys[index].m_short_values[0];
            m_geodata.m_geog_geodetic_datum_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_PRIME_MERIDIAN_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_geog_prime_meridian = m_geokeys[index].m_short_values[0];
            m_geodata.m_geog_prime_meridian_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_PRIME_MERIDIAN_LONG_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_geog_prime_meridian_long = m_geokeys[index].m_double_values[0];
            m_geodata.m_geog_prime_meridian_long_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_LINEAR_UNITS_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geog_linear_units = m_geokeys[index].m_short_values[0];
            m_geog_linear_units_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_LINEAR_UNIT_SIZE_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geog_linear_unit_size = m_geokeys[index].m_double_values[0];
            m_geog_linear_unit_size_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_ANGULAR_UNITS_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geog_angular_units = m_geokeys[index].m_short_values[0];
            m_geog_angular_units_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_ANGULAR_UNIT_SIZE_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geog_angular_unit_size = m_geokeys[index].m_double_values[0];
            m_geog_angular_unit_size_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_ELLIPSOID_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_geog_ellipsoid = m_geokeys[index].m_short_values[0];
            m_geodata.m_geog_ellipsoid_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_SEMI_MAJOR_AXIS_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_geog_semi_major_axis = m_geokeys[index].m_double_values[0];
            m_geodata.m_geog_semi_major_axis_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_SEMI_MINOR_AXIS_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_geog_semi_minor_axis = m_geokeys[index].m_double_values[0];
            m_geodata.m_geog_semi_minor_axis_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_INV_FLATTENING_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_geog_inv_flattening = m_geokeys[index].m_double_values[0];
            m_geodata.m_geog_inv_flattening_present = TRUE;
          }
            break;
         case GEOTIFF_GEOG_AZIMUTH_UNITS_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geog_azimuth_units = m_geokeys[index].m_short_values[0];
            m_geog_azimuth_units_present = FALSE;
          }
            break;

         case GEOTIFF_PROJECTED_CS_TYPE_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_projected_cs_type = m_geokeys[index].m_short_values[0];
            m_geodata.m_projected_cs_type_present = TRUE;
          }
            break;
         case GEOTIFF_PCS_CITATION_GEOKEY:
          if (m_geokeys[index].m_ascii_values != NULL)
          {
            m_geodata.m_pcs_citation = m_geokeys[index].m_ascii_values;
            m_geodata.m_pcs_citation_present = TRUE;
          }
            break;
         case GEOTIFF_PROJECTION_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_projection = m_geokeys[index].m_short_values[0];
            m_geodata.m_projection_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_COORD_TRANS_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_proj_coord_trans = m_geokeys[index].m_short_values[0];
            m_geodata.m_proj_coord_trans_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_LINEAR_UNITS_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            proj_linear_units = m_geokeys[index].m_short_values[0];
            proj_linear_units_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_LINEAR_UNIT_SIZE_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            proj_linear_unit_size = m_geokeys[index].m_double_values[0];
            proj_linear_unit_size_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_STD_PARALLEL_1_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_std_parallel_1 = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_std_parallel_1_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_STD_PARALLEL_2_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_std_parallel_2 = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_std_parallel_2_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_NAT_ORIGIN_LONG_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_nat_origin_long = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_nat_origin_long_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_NAT_ORIGIN_LAT_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_nat_origin_lat = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_nat_origin_lat_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_FALSE_EASTING_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_false_easting = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_false_easting_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_FALSE_NORTHING_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_false_northing = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_false_northing_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_FALSE_ORIGIN_LONG_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_false_origin_long = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_false_origin_long_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_FALSE_ORIGIN_LAT_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_false_origin_lat = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_false_origin_lat_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_FALSE_ORIGIN_EASTING_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_false_origin_easting = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_false_origin_easting_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_FALSE_ORIGIN_NORTHING_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_false_origin_northing = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_false_origin_northing_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_CENTER_LONG_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_center_long = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_center_long_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_CENTER_LAT_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_center_lat = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_center_lat_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_CENTER_EASTING_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_center_easting = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_center_easting_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_CENTER_NORTHING_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_center_northing = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_center_northing_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_SCALE_AT_NAT_ORIGIN_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_scale_at_nat_origin = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_scale_at_nat_origin_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_SCALE_AT_CENTER_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_scale_at_center = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_scale_at_center_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_AZIMUTH_ANGLE_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_azimuth_angle = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_azimuth_angle_present = TRUE;
          }
            break;
         case GEOTIFF_PROJ_STRAIGHT_VERT_POLE_LONG_GEOKEY:
          if (m_geokeys[index].m_double_values != NULL)
          {
            m_geodata.m_proj_straight_vert_pole_long = m_geokeys[index].m_double_values[0];
            m_geodata.m_proj_straight_vert_pole_long_present = TRUE;
          }
            break;

         case GEOTIFF_VERTICAL_CS_TYPE_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_vertical_cs_type = m_geokeys[index].m_short_values[0];
            m_geodata.m_vertical_cs_type_present = TRUE;
          }
            break;
         case GEOTIFF_VERTICAL_CITATION_GEOKEY:
          if (m_geokeys[index].m_ascii_values != NULL)
          {
            m_geodata.m_vertical_citation = m_geokeys[index].m_ascii_values;
            m_geodata.m_vertical_citation_present = TRUE;
          }
            break;
         case GEOTIFF_VERTICAL_DATUM_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            m_geodata.m_vertical_datum = m_geokeys[index].m_short_values[0];
            m_geodata.m_vertical_datum_present = TRUE;
          }
            break;
         case GEOTIFF_VERTICAL_UNITS_GEOKEY:
          if (m_geokeys[index].m_short_values != NULL)
          {
            vertical_units = m_geokeys[index].m_short_values[0];
            vertical_units_present = TRUE;
          }
            break;
         case GEOTIFF_UNDEFINED:
            break;
         case GEOTIFF_USER_DEFINED:
            break;
         default:
            break;
      }
   }

   // check that raster type is present recognized
   if( !m_geodata.m_gt_raster_type_present )
   {
      // not specified, use the default
      m_geodata.m_gt_raster_type = GEOTIFF_RASTER_PIXEL_IS_AREA;
      m_geodata.m_gt_raster_type_present = TRUE;
   }
   else if ( m_geodata.m_gt_raster_type != GEOTIFF_RASTER_PIXEL_IS_AREA &&
         m_geodata.m_gt_raster_type != GEOTIFF_RASTER_PIXEL_IS_POINT )
   {
      // invalid raster type, use the default
      m_geodata.m_gt_raster_type = GEOTIFF_RASTER_PIXEL_IS_AREA;
      m_geodata.m_gt_raster_type_present = TRUE;
   }

   // model type must be present
   if( !m_geodata.m_gt_model_type_present )
   {
      m_geodata_error_message = "Model type geokey is missing.";
      goto FAIL;
   }

   // convert geographic linear units to meters
   if( m_geog_linear_units_present )
   {
      // check for supported units
      switch( m_geog_linear_units )
      {
         case GEOTIFF_USER_DEFINED:
            if( !m_geog_linear_unit_size_present )
            {
               m_geodata_error_message =
                  "User-defined geographic linear unit size not present.";
               goto FAIL;
            }
         case GEOTIFF_LINEAR_METER:
         case GEOTIFF_LINEAR_FOOT:
         case GEOTIFF_LINEAR_FOOT_US_SURVEY:
         case GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN:
         case GEOTIFF_LINEAR_FOOT_CLARKE:
         case GEOTIFF_LINEAR_FOOT_INDIAN:
         case GEOTIFF_LINEAR_LINK:
         case GEOTIFF_LINEAR_LINK_BENOIT:
         case GEOTIFF_LINEAR_LINK_SEARS:
         case GEOTIFF_LINEAR_CHAIN_BENOIT:
         case GEOTIFF_LINEAR_CHAIN_SEARS:
         case GEOTIFF_LINEAR_YARD_SEARS:
         case GEOTIFF_LINEAR_YARD_INDIAN:
         case GEOTIFF_LINEAR_FATHOM:
         case GEOTIFF_LINEAR_MILE_INTERNATIONAL_NAUTICAL:
            convert_linear_units( m_geog_linear_units, m_geog_linear_unit_size,
                                  m_geodata.m_geog_semi_major_axis );
            convert_linear_units( m_geog_linear_units, m_geog_linear_unit_size,
                                  m_geodata.m_geog_semi_minor_axis );
            break;
         case GEOTIFF_UNDEFINED:
            m_geodata_error_message = "Geographic linear unit is undefined.";
            goto FAIL;
         default:
            m_geodata_error_message = "Geographic linear unit is unknown.";
            goto FAIL;
      }
   }

   // convert angular units to degrees
   if( m_geog_angular_units_present )
   {
      // check for supported units
      switch( m_geog_angular_units )
      {
         case GEOTIFF_USER_DEFINED:
            if( !m_geog_angular_unit_size_present )
            {
               m_geodata_error_message =
                  "User-defined geographic angular unit size not present.";
               goto FAIL;
            }
         case GEOTIFF_ANGULAR_RADIAN:
         case GEOTIFF_ANGULAR_DEGREE:
         case GEOTIFF_ANGULAR_ARC_MINUTE:
         case GEOTIFF_ANGULAR_ARC_SECOND:
         case GEOTIFF_ANGULAR_GRAD:
         case GEOTIFF_ANGULAR_GON:
            convert_angular_units( m_geog_angular_units, m_geog_angular_unit_size,
                                   m_geodata.m_geog_prime_meridian_long );
            convert_angular_units( m_geog_angular_units, m_geog_angular_unit_size,
                                   m_geodata.m_proj_std_parallel_1 );
            convert_angular_units( m_geog_angular_units, m_geog_angular_unit_size,
                                   m_geodata.m_proj_std_parallel_2 );
            convert_angular_units( m_geog_angular_units, m_geog_angular_unit_size,
                                   m_geodata.m_proj_nat_origin_long );
            convert_angular_units( m_geog_angular_units, m_geog_angular_unit_size,
                                   m_geodata.m_proj_nat_origin_lat );
            convert_angular_units( m_geog_angular_units, m_geog_angular_unit_size,
                                   m_geodata.m_proj_false_origin_long );
            convert_angular_units( m_geog_angular_units, m_geog_angular_unit_size,
                                   m_geodata.m_proj_false_origin_lat );
            convert_angular_units( m_geog_angular_units, m_geog_angular_unit_size,
                                   m_geodata.m_proj_center_long );
            convert_angular_units( m_geog_angular_units, m_geog_angular_unit_size,
                                   m_geodata.m_proj_center_lat );
            convert_angular_units( m_geog_angular_units, m_geog_angular_unit_size,
                                   m_geodata.m_proj_straight_vert_pole_long );
            break;

         case GEOTIFF_ANGULAR_DMS:
            m_geodata_error_message =
               "Geographic angular units not supported: DMS.";
            goto FAIL;
         case GEOTIFF_ANGULAR_DMS_HEMISPHERE:
            m_geodata_error_message =
               "Geographic angular units not supported: DMS Hemisphere.";
            goto FAIL;
         case GEOTIFF_UNDEFINED:
            m_geodata_error_message = "Geographic angular unit is undefined.";
            goto FAIL;
         default:
            m_geodata_error_message = "Geographic angular unit is unknown.";
            goto FAIL;
      }
   }

   // convert projection linear units to meters
   if( proj_linear_units_present )
   {
      // check for supported units
      switch( proj_linear_units )
      {
         case GEOTIFF_USER_DEFINED:
            if( !proj_linear_unit_size_present )
            {
               m_geodata_error_message =
                  "User-defined projection linear unit size not present.";
               goto FAIL;
            }
         case GEOTIFF_LINEAR_METER:
         case GEOTIFF_LINEAR_FOOT:
         case GEOTIFF_LINEAR_FOOT_US_SURVEY:
         case GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN:
         case GEOTIFF_LINEAR_FOOT_CLARKE:
         case GEOTIFF_LINEAR_FOOT_INDIAN:
         case GEOTIFF_LINEAR_LINK:
         case GEOTIFF_LINEAR_LINK_BENOIT:
         case GEOTIFF_LINEAR_LINK_SEARS:
         case GEOTIFF_LINEAR_CHAIN_BENOIT:
         case GEOTIFF_LINEAR_CHAIN_SEARS:
         case GEOTIFF_LINEAR_YARD_SEARS:
         case GEOTIFF_LINEAR_YARD_INDIAN:
         case GEOTIFF_LINEAR_FATHOM:
         case GEOTIFF_LINEAR_MILE_INTERNATIONAL_NAUTICAL:
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_false_easting );
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_false_northing );
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_false_origin_easting );
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_false_origin_northing );
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_center_easting );
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_center_northing );
            break;

         case GEOTIFF_UNDEFINED:
            m_geodata_error_message = "Projection linear unit is undefined.";
            goto FAIL;
         default:
            m_geodata_error_message = "Projection linear unit is unknown.";
            goto FAIL;
      }
   }

   // convert azimuth angular units to degrees
   if( m_geog_azimuth_units_present )
   {
      // check for supported units
      switch( m_geog_azimuth_units )
      {
         case GEOTIFF_ANGULAR_RADIAN:
         case GEOTIFF_ANGULAR_DEGREE:
         case GEOTIFF_ANGULAR_ARC_MINUTE:
         case GEOTIFF_ANGULAR_ARC_SECOND:
         case GEOTIFF_ANGULAR_GRAD:
         case GEOTIFF_ANGULAR_GON:
            convert_angular_units( m_geog_azimuth_units, 0.0,
                                   m_geodata.m_proj_azimuth_angle );
            break;

         case GEOTIFF_ANGULAR_DMS:
            m_geodata_error_message =
               "Azimuth angular units not supported: DMS.";
            goto FAIL;
         case GEOTIFF_ANGULAR_DMS_HEMISPHERE:
            m_geodata_error_message =
               "Azimuth angular units not supported: DMS Hemisphere.";
            goto FAIL;
         case GEOTIFF_USER_DEFINED:
            m_geodata_error_message =
               "User-defined azimuth unit is not supported.";
            goto FAIL;
         case GEOTIFF_UNDEFINED:
            m_geodata_error_message = "Azimuth unit is undefined.";
            goto FAIL;
         default:
            m_geodata_error_message = "Azimuth unit is unknown.";
            goto FAIL;
      }
   }

   // add support for projected file with improper model type value
   if (m_geodata.m_projected_cs_type_present)
      m_geodata.m_gt_model_type = GEOTIFF_MODEL_TYPE_PROJECTED;

   // test for model type
   switch( m_geodata.m_gt_model_type )
   {
      case GEOTIFF_MODEL_TYPE_PROJECTED:
         // projection coordinate system must be present
         if( !m_geodata.m_projected_cs_type_present )
         {
            m_geodata_error_message =
               "Projection coordinate system geokey is missing.";
            goto FAIL;
         }
         // check for user-defined projection coordinate_system
         if( m_geodata.m_projected_cs_type == GEOTIFF_USER_DEFINED )
         {
            if( !m_geodata.m_proj_coord_trans_present )
            {
               m_geodata_error_message =
               "User-defined projection coordinate transformation not present.";
               goto FAIL;
            }
            switch( m_geodata.m_proj_coord_trans )
            {
               case GEOTIFF_CT_LAMBERT_CONF_CONIC_2SP:
                  // check for necessary data.  The origin of a 2SP Lambert is
                  // ProjFalseOrigin{Lat,Long} (3084/3085) per the GeoTIFF
                  // spec - ProjNatOrigin* (3080/3081) is the 1SP spelling.
                  // Older libgeotiff output wrote the natural-origin keys
                  // here, and current FAA sectionals write the false-origin
                  // ones, so either spelling of either axis is accepted.
                  if( !m_geodata.m_proj_std_parallel_1_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 1 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_std_parallel_2_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 2 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_nat_origin_long_present &&
                      !m_geodata.m_proj_false_origin_long_present )
                  {
                     m_geodata_error_message =
                        "Origin longitude is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_false_origin_lat_present &&
                      !m_geodata.m_proj_nat_origin_lat_present )
                  {
                     m_geodata_error_message =
                        "Origin latitude is missing.";
                     goto FAIL;
                  }
              m_projection_type = "Lambert Conformal Conic";
                  m_geodata_supported = TRUE;
                  break;
               case GEOTIFF_CT_ALBERS_EQUAL_AREA:
                  // check for necessary data
                  if( !m_geodata.m_proj_std_parallel_1_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 1 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_std_parallel_2_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 2 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_nat_origin_lat_present && !m_geodata.m_proj_center_lat_present)
                  {
                     m_geodata_error_message =
                        "Natural origin latitude is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_nat_origin_long_present && !m_geodata.m_proj_center_long_present)
                  {
                     m_geodata_error_message =
                        "Natural origin longitude is missing.";
                     goto FAIL;
                  }
              m_projection_type = "Albers Equal Area";
                  m_geodata_supported = TRUE;
                  break;
               default:
                  // user-defined
                  m_geodata_supported = FALSE;
                  break;
            }
         }
         else
         {
            // pre-defined projection coordinate system

            if( m_geodata.m_projected_cs_type >= 26703 &&
                m_geodata.m_projected_cs_type <= 26722 )
            {
               // NAD27 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD27;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 26903 &&
                m_geodata.m_projected_cs_type <= 26923 )
            {
               // NAD83 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD83;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32201 &&
                m_geodata.m_projected_cs_type <= 32260 )
            {
               // WGS72 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_72;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
               m_geodata_supported = TRUE;
            m_projection_type = "UTM";
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32301 &&
                m_geodata.m_projected_cs_type <= 32360 )
            {
               // WGS72 UTM southern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_72;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 10000000.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32601 &&
                m_geodata.m_projected_cs_type <= 32660 )
            {
               // WGS84 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_84;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32701 &&
                m_geodata.m_projected_cs_type <= 32760 )
            {
               // WGS84 UTM southern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_84;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 10000000.0;
               m_geodata.m_proj_false_northing_present = TRUE;
            m_projection_type = "UTM";
               m_geodata_supported = TRUE;
               break;
            }

            if(( m_geodata.m_projected_cs_type >= 26729 && m_geodata.m_projected_cs_type <= 26798 ) ||
            ( m_geodata.m_projected_cs_type >= 32001 && m_geodata.m_projected_cs_type <= 32060 ))
            {
               // state plane NAD27
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD27;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata_supported = TRUE;
            m_projection_type = "State Plane";
               break;
            }

            if(( m_geodata.m_projected_cs_type >= 26929 && m_geodata.m_projected_cs_type <= 26998 ) ||
            ( m_geodata.m_projected_cs_type >= 32100 && m_geodata.m_projected_cs_type <= 32161 ))
            {
               // state plane NAD83
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD83;
               m_geodata.m_geographic_type_present = TRUE;
            m_projection_type = "State Plane";
               m_geodata_supported = TRUE;
               break;
            }

            m_geodata_supported = FALSE;
         }
         break;

      case GEOTIFF_MODEL_TYPE_GEOGRAPHIC:
         if( m_geodata.m_model_pixel_scale_present &&
             m_geodata.m_num_tie_points == 1 )
         {
            // direct lat/long
            m_geodata_supported = TRUE;
         }
         else
//         if( m_geodata.m_num_tie_points >= 3 && !m_geodata.m_model_pixel_scale_present )
         if( m_geodata.m_num_tie_points >= 3 )  // use only the tie points and ignore the scale
         {
            // georeferenced by tiepoints only
            // set up tiepoint transform
            if( m_tiepoint_transform.define_tiepoints(
                m_geodata.m_num_tie_points, m_geodata.m_model_tie_point_hpix,
                m_geodata.m_model_tie_point_vpix, m_geodata.m_model_tie_point_y,
                m_geodata.m_model_tie_point_x ) != SUCCESS )
            {
               m_geodata_error_message =
                  "Tiepoint transformation is invalid.";
               goto FAIL;
            }
         m_projection_type = "Equal Arc";
            m_geodata_supported = TRUE;
         }
         break;

      case GEOTIFF_MODEL_TYPE_GEOCENTRIC:
         m_geodata_error_message = "Geocentric model type is not supported.";
         goto FAIL;

      default:
         m_geodata_error_message = "Geometric model type is unknown.";
         goto FAIL;
   }

   if ((m_geodata.m_geographic_type == 0) && (m_geodata.m_geog_geodetic_datum > 0))
   {
      switch (m_geodata.m_geog_geodetic_datum)
      {
         case GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1927:
            m_geodata.m_geographic_type = GEOTIFF_GCS_NAD27;
            break;
         case GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1983:
            m_geodata.m_geographic_type = GEOTIFF_GCS_NAD83;
            break;
         case GEOTIFF_DATUM_WGS72:
            m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_72;
            break;
         case GEOTIFF_DATUM_WGS84:
            m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_84;
            break;
      }
   }

   // assign datum
   switch( m_geodata.m_geographic_type )
   {
      case GEOTIFF_GCS_NAD27:
         m_geodata.m_geog_geodetic_datum =
            GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1927;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_ellipsoid = GEOTIFF_ELLIPSE_CLARKE_1866;
         m_geodata.m_geog_ellipsoid_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378206.4;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356583.8;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 294.98;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      case GEOTIFF_GCS_NAD83:
         m_geodata.m_geog_geodetic_datum =
            GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1983;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_ellipsoid = GEOTIFF_ELLIPSE_GRS_1980;
         m_geodata.m_geog_ellipsoid_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378137.0;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356752.314;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 298.2572221010;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      case GEOTIFF_GCS_WGS_72:
         m_geodata.m_geog_geodetic_datum = GEOTIFF_DATUM_WGS72;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378135.0;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356750.5;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 298.26;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      case GEOTIFF_GCS_WGS_84:
      case GEOTIFF_GCSE_WGS84:
         m_geodata.m_geog_geodetic_datum = GEOTIFF_DATUM_WGS84;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_ellipsoid = GEOTIFF_ELLIPSE_WGS_84;
         m_geodata.m_geog_ellipsoid_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378137.0;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356752.3;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 298.2572235630;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      default:
         m_geodata_error_message =
            "Geographic coordinate system is not supported.";
         goto FAIL;
   }

   m_geodata_present = TRUE;
   m_geodata_error_code = GEOTIFF_GEODATA_OK;
   m_geodata_error_message = "";
   return;

FAIL:
   m_geodata_present = TRUE;
   m_geodata_supported = FALSE;
   m_geodata_error_code = GEOTIFF_GEODATA_ERROR;
   return;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::convert_linear_units( unsigned short linear_units,
                                    double linear_unit_size, double &value )
{
   // this function converts the value argument from the specified linear units
   // to meters
   // returns SUCCESS or FAILURE (fails if linear units are not supported)

   switch( linear_units )
   {
      case GEOTIFF_LINEAR_METER:
         value *= 1.0;
         break;
      case GEOTIFF_LINEAR_FOOT:
         value *= 0.3048;
         break;
      case GEOTIFF_LINEAR_FOOT_US_SURVEY:
         value *= 12.0 / 39.37;
         break;
      case GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN:
         value *= 0.3048 * 1.000040166747;
         break;
      case GEOTIFF_LINEAR_FOOT_CLARKE:
         value *= 12.0 / 39.370432;
         break;
      case GEOTIFF_LINEAR_FOOT_INDIAN:
         value *= 12.0 / 39.370142;
         break;
      case GEOTIFF_LINEAR_LINK:
         value *= 7.92 / 39.370432;
         break;
      case GEOTIFF_LINEAR_LINK_BENOIT:
         value *= 7.92 / 39.370113;
         break;
      case GEOTIFF_LINEAR_LINK_SEARS:
         value *= 7.92 / 39.370147;
         break;
      case GEOTIFF_LINEAR_CHAIN_BENOIT:
         value *= 792.0 / 39.370113;
         break;
      case GEOTIFF_LINEAR_CHAIN_SEARS:
         value *= 792.0 / 39.370147;
         break;
      case GEOTIFF_LINEAR_YARD_SEARS:
         value *= 36.0 / 39.370147;
         break;
      case GEOTIFF_LINEAR_YARD_INDIAN:
         value *= 36.0 / 39.370141;
         break;
      case GEOTIFF_LINEAR_FATHOM:
         value *= 6.0 * 0.3048;
         break;
      case GEOTIFF_LINEAR_MILE_INTERNATIONAL_NAUTICAL:
         value *= 1852.0;
         break;
      case GEOTIFF_USER_DEFINED:
         value *= linear_unit_size;
         break;
      case GEOTIFF_UNDEFINED:
      default:
         goto FAIL;
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::convert_angular_units( unsigned short angular_units,
                           double angular_unit_size, double &value )
{
   // this function converts the value argument from the specified
   // angular units to degrees
   // returns SUCCESS or FAILURE (fails if angular units are not supported)

   double deg_per_rad = 180.0 / 4.0 / atan( 1.0 );

   switch( angular_units )
   {
      case GEOTIFF_ANGULAR_RADIAN:
         value *= deg_per_rad;
         break;
      case GEOTIFF_ANGULAR_DEGREE:
         value *= 1.0;
         break;
      case GEOTIFF_ANGULAR_ARC_MINUTE:
         value /= 60.0;
         break;
      case GEOTIFF_ANGULAR_ARC_SECOND:
         value /= 3600.0;
         break;
      case GEOTIFF_ANGULAR_GRAD:
         value *= 0.9;
         break;
      case GEOTIFF_ANGULAR_GON:
         value *= 0.9;
         break;
      case GEOTIFF_USER_DEFINED:
         value *= angular_unit_size * deg_per_rad;
         break;
      case GEOTIFF_ANGULAR_DMS:
      case GEOTIFF_ANGULAR_DMS_HEMISPHERE:
      case GEOTIFF_UNDEFINED:
      default:
         goto FAIL;
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::inv_transform( int hpix, int vpix, double &latitude,
                             double &longitude )
{
   // this function returns the latitude and longitude in degrees for the
   // specified pixel. the pixel must lie within the image.
   // returns SUCCESS or FAILURE

   unsigned short pcs;

   // return failure if no file loaded or there was an error loading
   // the tiff file or if geodata is not supported
   if( !m_file_loaded || m_error || !m_geodata_supported )
      goto FAIL;

   // test for pixel within image bounds
   if( hpix < 0 || hpix >= (int)m_image_width || vpix < 0 ||
       vpix >= (int)m_image_length )
      goto FAIL;

  // test for model type
   switch( m_geodata.m_gt_model_type )
   {
      case GEOTIFF_MODEL_TYPE_PROJECTED:
         // check for user-defined projection coordinate_system
         if( m_geodata.m_projected_cs_type == GEOTIFF_USER_DEFINED )
         {
            switch( m_geodata.m_proj_coord_trans )
            {
               case GEOTIFF_CT_LAMBERT_CONF_CONIC_2SP:
                  if( inv_transform_lambert_conf_conic( hpix, vpix,
                      latitude, longitude ) != SUCCESS )
                 goto FAIL;
                  break;
               case GEOTIFF_CT_ALBERS_EQUAL_AREA:
                  if( inv_transform_albers_equal_area( hpix, vpix,
                      latitude, longitude ) != SUCCESS )
                 goto FAIL;
                  break;
               default:
                  goto FAIL;
            }
            break;
         }
         else
         {
            // pre-defined projection coordinate system
            pcs = m_geodata.m_projected_cs_type;
            if( pcs >= 26703 && pcs <= 26722 ||
                pcs >= 26903 && pcs <= 26922 ||
                pcs >= 32201 && pcs <= 32260 ||
                pcs >= 32301 && pcs <= 32360 ||
                pcs >= 32601 && pcs <= 32660 ||
                pcs >= 32701 && pcs <= 32760 )
            {
               if( inv_transform_utm( hpix, vpix, latitude, longitude ) != SUCCESS )
               goto FAIL;
               break;
            }
            else if( pcs >= 26729 && pcs <= 26798 ||
               pcs >= 32001 && pcs <= 32060 ||
               pcs >= 26929 && pcs <= 26998 ||
               pcs >= 32100 && pcs <= 32161 )
            {
               if( inv_transform_sp( hpix, vpix, latitude, longitude ) != SUCCESS )
               goto FAIL;
               break;
            }
            else
               goto FAIL;
         }

      case GEOTIFF_MODEL_TYPE_GEOGRAPHIC:
         if( m_geodata.m_model_pixel_scale_present &&
             m_geodata.m_num_tie_points == 1 )
         {
            // direct lat/long
           if( inv_transform_lat_long( hpix, vpix, latitude, longitude ) != SUCCESS )
               goto FAIL;
         }
         else
//         if( m_geodata.m_num_tie_points >= 3 && !m_geodata.m_model_pixel_scale_present )
         if( m_geodata.m_num_tie_points >= 3 )  // use only the tie points and ignore the scale
         {
            // georeferenced by tiepoints only
            if( m_tiepoint_transform.inv_transform( (double)hpix, (double)vpix,
               latitude, longitude ) != SUCCESS )
               goto FAIL;
         }
         break;

      case GEOTIFF_MODEL_TYPE_GEOCENTRIC:
          goto FAIL;

      default:
          goto FAIL;
   }

   // put longitude in correct range
   if (longitude > 180.0)
      longitude -= 360.0;
   if (longitude < -180.0)
      longitude += 360.0;

   return SUCCESS;

FAIL:
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::fwd_transform( double latitude, double longitude, int &hpix,
                             int &vpix )
{
   // this function returns the pixel which corresponds to the specified
   // latitude and longitude, provided it lies within the image
   // returns SUCCESS or FAILURE

   unsigned short pcs;
   double x, y;

   // return failure if no file loaded or there was an error loading the
   // tiff file or if geodata is not supported
   if( !m_file_loaded || m_error || !m_geodata_supported )
      goto FAIL;

   // test for model type
   switch( m_geodata.m_gt_model_type )
   {
      case GEOTIFF_MODEL_TYPE_PROJECTED:
         // check for user-defined projection coordinate_system
         if( m_geodata.m_projected_cs_type == GEOTIFF_USER_DEFINED )
         {
            switch( m_geodata.m_proj_coord_trans )
            {
               case GEOTIFF_CT_LAMBERT_CONF_CONIC_2SP:
                  if( fwd_transform_lambert_conf_conic( latitude, longitude, hpix, vpix ) != SUCCESS )
                 goto FAIL;
                  break;
               case GEOTIFF_CT_ALBERS_EQUAL_AREA:
                  if( fwd_transform_albers_equal_area( latitude, longitude, hpix, vpix ) != SUCCESS )
                 goto FAIL;
                  break;
               default:
                  goto FAIL;
            }
         }
         else
         {
            // pre-defined projection coordinate system
            pcs = m_geodata.m_projected_cs_type;
            if( pcs >= 26703 && pcs <= 26722 ||
                pcs >= 26903 && pcs <= 26922 ||
                pcs >= 32201 && pcs <= 32260 ||
                pcs >= 32301 && pcs <= 32360 ||
                pcs >= 32601 && pcs <= 32660 ||
                pcs >= 32701 && pcs <= 32760 )
            {
               if( fwd_transform_utm( latitude, longitude, hpix, vpix) != SUCCESS )
               goto FAIL;
               break;
            }
            else if( pcs >= 26729 && pcs <= 26798 ||
               pcs >= 32001 && pcs <= 32060 ||
               pcs >= 26929 && pcs <= 26998 ||
               pcs >= 32100 && pcs <= 32161 )
            {
               if ( fwd_transform_sp( latitude, longitude, hpix, vpix ) != SUCCESS )
               goto FAIL;
               break;
            }
            else
               goto FAIL;
         }
         break;

      case GEOTIFF_MODEL_TYPE_GEOGRAPHIC:
         if( m_geodata.m_model_pixel_scale_present &&
             m_geodata.m_num_tie_points == 1 )
         {
            // direct lat/long
            if( fwd_transform_lat_long( latitude, longitude, hpix, vpix) != SUCCESS )
                goto FAIL;
         }
         else
//         if( m_geodata.m_num_tie_points >= 3 && !m_geodata.m_model_pixel_scale_present )
         if( m_geodata.m_num_tie_points >= 3 )  // use only the tie points and ignore the scale
         {
            // georeferenced by tiepoints only
            if( m_tiepoint_transform.fwd_transform( latitude, longitude, x, y ) != SUCCESS )
               goto FAIL;
            if( x >= 0.0 )
               hpix = (int)(x + 0.5);
            else
               hpix = (int)(x - 0.5);
            if( y >= 0.0 )
               vpix = (int)(y + 0.5);
            else
               vpix = (int)(y - 0.5);
         }
         break;

      default:
          goto FAIL;
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_image_bounds( double &lat_upper_left, double &long_upper_left,
                                double &lat_lower_left, double &long_lower_left,
                                double &lat_lower_right,
                                double &long_lower_right,
                                double &lat_upper_right,
                                double &long_upper_right )
{
   // this function returns the latitude and longitude in degrees for the four
   // image corner points
   // returns SUCCESS or FAILURE

   // return failure if no file loaded or there was an error loading
   // the tiff file or if geodata is not supported
   if( !m_file_loaded || m_error || !m_geodata_supported )
      goto FAIL;

   // upper left corner
   if( inv_transform( 0, 0, lat_upper_left, long_upper_left ) != SUCCESS )
      goto FAIL;

   // lower left corner
   if( inv_transform( 0, m_image_length-1, lat_lower_left, long_lower_left ) != SUCCESS )
      goto FAIL;

   // lower right corner
   if( inv_transform( m_image_width-1, m_image_length-1, lat_lower_right, long_lower_right ) != SUCCESS )
      goto FAIL;

   // upper right corner
   if( inv_transform( m_image_width-1, 0, lat_upper_right, long_upper_right ) != SUCCESS )
      goto FAIL;

   return SUCCESS;

FAIL:
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::deg_to_dms( double deg, int &sign, int &ideg, int &imin,
                           double &sec, CString &dms )
{
   // converts degrees to signed degrees minutes seconds and formats
   // in a CString
   if( deg >= 0.0 )
      sign = 1;
   else
      sign = -1;
   deg *= sign;

   ideg = (int)deg;
   imin = (int)( 60 * ( deg - ideg ) );
   sec = 3600.0 * ( deg - ideg) - 60.0 * imin;

   // prevent rounding up to 60 seconds when formatting
   double temp_sec = sec;
   if( temp_sec > 59.99 )
      temp_sec = 59.99;
   if( sign == 1 )
      dms.Format( "+%03i %02i\' %05.2lf\"", ideg, imin, temp_sec );
   else
      dms.Format( "-%03i %02i\' %05.2lf\"", ideg, imin, temp_sec );
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::dms_to_deg( int sign, int ideg, int imin, double sec,
                           double &deg )
{
   // converts degrees minutes seconds to degrees

   deg = sign*( ideg + imin/60.0 + sec/3600.0 );
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::inv_transform_utm( int hpix, int vpix, double &latitude,
                                 double &longitude )
{
   // performs inverse transform for UTM projection using data in m_geodata
   // returns latitude and longitude in degrees for specified pixel
   // returns SUCCESS or FAILURE
   int zone;
   double x, y;
   double k0 = 0.9996;
   double phi0 = 0.0;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
   double lambda0, phi1;
   double m, m0;
   double a, b, f, e_sqrd, e_prime_sqrd, e1, mu;
   double c1, t1, n1, r1, d;

   // find zone (1-60)
   if( m_geodata.m_projected_cs_type >= 26703 &&
       m_geodata.m_projected_cs_type <= 26722 )
   {
      zone = m_geodata.m_projected_cs_type - 26700;
      goto CALC_XY;
   }

   if( m_geodata.m_projected_cs_type >= 26903 &&
       m_geodata.m_projected_cs_type <= 26923 )
   {
      zone = m_geodata.m_projected_cs_type - 26900;
      goto CALC_XY;
   }

   if( m_geodata.m_projected_cs_type >= 32201 &&
       m_geodata.m_projected_cs_type <= 32260 )
   {
      zone = m_geodata.m_projected_cs_type - 32200;
      goto CALC_XY;
   }

   if( m_geodata.m_projected_cs_type >= 32301 &&
       m_geodata.m_projected_cs_type <= 32360 )
   {
      zone = m_geodata.m_projected_cs_type - 33200;
      goto CALC_XY;
   }

   if( m_geodata.m_projected_cs_type >= 32601 &&
       m_geodata.m_projected_cs_type <= 32660 )
   {
      zone = m_geodata.m_projected_cs_type - 32600;
      goto CALC_XY;
   }

   if( m_geodata.m_projected_cs_type >= 32701 &&
       m_geodata.m_projected_cs_type <= 32760 )
   {
      zone = m_geodata.m_projected_cs_type - 32700;
      goto CALC_XY;
   }

   // unknown range, return failure
   goto FAIL;

CALC_XY:
   m_geodata.inv_transform( hpix, vpix, x, y );

   // subtract false easting from x and false northing from y
   x -= m_geodata.m_proj_false_easting;
   y -= m_geodata.m_proj_false_northing;

   // calculate flatenning and eccentricity
   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;

   lambda0 = ( -183.0 + zone * 6.0 ) * pi_over_180;

   m0 = 111132.0894*phi0 -16216.94*sin(2*phi0) +
        17.21*sin(4*phi0)-0.02*sin(6*phi0);

   e_prime_sqrd = e_sqrd / ( 1.0-e_sqrd);
   m = m0 + y / k0;
   e1 = (1.0-sqrt(1.0-e_sqrd)) / (1.0+sqrt(1.0-e_sqrd));
   mu = m /
        ( a * (1-e_sqrd/4 -3*e_sqrd*e_sqrd/64 -5*e_sqrd*e_sqrd*e_sqrd/256 ) );
   phi1 = mu +
          ( 3*e1/2 - 27*pow(e1,3.0) ) * sin( 2*mu ) +
          ( 21*e1*e1/16 - 55*pow(e1,4.0)/32 ) * sin( 4*mu ) +
          ( 151*pow(e1,3.0)/96 ) * sin( 6*mu ) +
          ( 1097*pow(e1,4.0)/512 ) * sin( 8*mu );

   c1 = e_prime_sqrd * cos( phi1 ) * cos( phi1 );
   t1 = tan( phi1 ) * tan( phi1 );
   n1 = a / sqrt( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ) );
   r1 = a * (1-e_sqrd) / pow( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ), 1.5 );
   d = x / n1 / k0;

   latitude = phi1 - ( n1* tan(phi1) / r1 ) *
              (
                 d*d/2 -
                 ( 5 + 3*t1 + 10*c1 -4*c1*c1 - 9*e_prime_sqrd ) *
                   pow(d,4.0)/24 +
                 ( 61 + 90*t1 + 298*c1 + 45*t1*t1 - 252*e_prime_sqrd -
                   3*c1*c1 ) * pow(d,6.0)/720
              );

   longitude = lambda0 + ( d - (1 + 2*t1 + c1)*pow(d,3.0)/6 +
               (5 - 2*c1 + 28*t1 - 3*c1*c1 + 8*e_prime_sqrd + 24*t1*t1) *
               pow(d,5.0)/120 ) / cos( phi1 );

   // convert from radians to degrees
   latitude /= pi_over_180;
   longitude /= pi_over_180;

   // check for valid values
   if ((latitude < -90.0) || (latitude > 90.0))
      return FAILURE;
   if ((longitude < -180.0) || (longitude > 180.0))
      return FAILURE;

   return SUCCESS;

FAIL:
   return FAILURE;
}

// ********************************************************************************************
// *****************************************************************

int CGeoTiff::fwd_transform_sp( double latitude, double longitude,
                                 int &hpix, int &vpix  )
{
   // performs inverse transform for State Plane projection using data in m_geodata
   // returns latitude and longitude in degrees for specified pixel
   // returns SUCCESS or FAILURE
   int rslt;
   double x, y, dhpix, dvpix;
   CProj proj;

   rslt = proj.set_proj_type(PROJ_TYPE_SPCS, m_geodata.m_projected_cs_type);
   if (rslt != SUCCESS)
      return FAILURE;

   switch(m_geog_linear_units)
   {
      case GEOTIFF_LINEAR_METER:
         proj.set_units(PROJ_UNITS_METERS);
         break;
      case GEOTIFF_LINEAR_FOOT:
      case GEOTIFF_LINEAR_FOOT_US_SURVEY:
      case GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN:
      case GEOTIFF_LINEAR_FOOT_CLARKE:
      case GEOTIFF_LINEAR_FOOT_INDIAN:
         proj.set_units(PROJ_UNITS_US_FEET);
         break;
   }

   rslt = proj.geo_to_xy(latitude, longitude, &x, &y);
   if (rslt != SUCCESS)
      return FAILURE;

   if (m_linear_units > 0)
      proj.set_units(m_linear_units);

   m_geodata.fwd_transform( x, y, dhpix, dvpix );

   // round to nearest integer
   if( dhpix >= 0.0 )
      hpix = (int)(dhpix + 0.5);
   else
      hpix = (int)(dhpix - 0.5);
   if( dvpix >= 0.0 )
      vpix = (int)(dvpix + 0.5);
   else
      vpix = (int)(dvpix - 0.5);

   return SUCCESS;
}

// *****************************************************************
// ********************************************************************************************

int CGeoTiff::inv_transform_sp( int hpix, int vpix, double &latitude, double &longitude )
{
   // performs inverse transform for State Plane projection using data in m_geodata
   // returns latitude and longitude in degrees for specified pixel
   // returns SUCCESS or FAILURE
   int rslt;
   double x, y, lat, lon;
   CProj proj;

   if (!m_geodata.m_model_tie_point_x || !m_geodata.m_model_tie_point_hpix ||
      !m_geodata.m_model_tie_point_y || !m_geodata.m_model_tie_point_vpix)
      return FAILURE;

   m_geodata.inv_transform( hpix, vpix, x, y );

   // the scaling factor scales to meters; convert to feet
// x /= 0.3048;
// y /= 0.3048;

   rslt = proj.set_proj_type(PROJ_TYPE_SPCS, m_geodata.m_projected_cs_type);
   if (rslt != SUCCESS)
      return FAILURE;

   if (m_linear_units > 0)
      proj.set_units(m_linear_units);

   switch(m_geog_linear_units)
   {
      case GEOTIFF_LINEAR_METER:
         proj.set_units(PROJ_UNITS_METERS);
         break;
      case GEOTIFF_LINEAR_FOOT:
      case GEOTIFF_LINEAR_FOOT_US_SURVEY:
      case GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN:
      case GEOTIFF_LINEAR_FOOT_CLARKE:
      case GEOTIFF_LINEAR_FOOT_INDIAN:
         proj.set_units(PROJ_UNITS_US_FEET);
         break;
   }

   rslt = proj.xy_to_geo(x, y, &lat, &lon);
   if (rslt == SUCCESS)
   {
      latitude = lat;
      longitude = lon;
      return SUCCESS;
   }

   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::fwd_transform_utm( double latitude, double longitude,
                                 int &hpix, int &vpix )
{
   // performs forward transform for UTM projection using data in m_geodata
   // returns pixel coordinates for specified latitude and longitude
   // returns SUCCESS or FAILURE

   int zone;
   double x, y, dhpix, dvpix;
   double k0 = 0.9996;
   double phi0 = 0.0;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
   double lambda0, lambda, phi;
   double m, m0;
   double a, b, f, e_sqrd, e_prime_sqrd;
   double c, t, n, aa;

   // find zone (1-60)
   if( m_geodata.m_projected_cs_type >= 26703 &&
       m_geodata.m_projected_cs_type <= 26722 )
   {
      zone = m_geodata.m_projected_cs_type - 26700;
      goto CALC_GEOID;
   }

   if( m_geodata.m_projected_cs_type >= 26903 &&
       m_geodata.m_projected_cs_type <= 26923 )
   {
      zone = m_geodata.m_projected_cs_type - 26900;
      goto CALC_GEOID;
   }

   if( m_geodata.m_projected_cs_type >= 32201 &&
       m_geodata.m_projected_cs_type <= 32260 )
   {
      zone = m_geodata.m_projected_cs_type - 32200;
      goto CALC_GEOID;
   }

   if( m_geodata.m_projected_cs_type >= 32301 &&
       m_geodata.m_projected_cs_type <= 32360 )
   {
      zone = m_geodata.m_projected_cs_type - 33200;
      goto CALC_GEOID;
   }

   if( m_geodata.m_projected_cs_type >= 32601 &&
       m_geodata.m_projected_cs_type <= 32660 )
   {
      zone = m_geodata.m_projected_cs_type - 32600;
      goto CALC_GEOID;
   }

   if( m_geodata.m_projected_cs_type >= 32701 &&
       m_geodata.m_projected_cs_type <= 32760 )
   {
      zone = m_geodata.m_projected_cs_type - 32700;
      goto CALC_GEOID;
   }

   // unknown range, return failure
   goto FAIL;

CALC_GEOID:
   // calculate flatenning and eccentricity
   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;
   e_prime_sqrd = e_sqrd / ( 1.0-e_sqrd);

   // latitude and longitude in radians
   phi = latitude * pi_over_180;
   lambda = longitude * pi_over_180;

   if( fabs( latitude ) < 89.9 )       // equations blow up at poles
   {
      lambda0 = ( -183.0 + zone * 6.0 ) * pi_over_180;

      n = a / sqrt( 1 - e_sqrd * sin( phi ) * sin( phi ) );
      t = tan( phi ) * tan( phi );
      c = e_prime_sqrd * cos( phi ) * cos( phi );
      aa = ( lambda - lambda0 ) * cos( phi );
      m = a * (
            (1 - e_sqrd/4 - 3*e_sqrd*e_sqrd/64 - 5*e_sqrd*e_sqrd*e_sqrd/256)*
            phi -
            (3*e_sqrd/8 + 3*e_sqrd*e_sqrd/32 + 45*e_sqrd*e_sqrd*e_sqrd/1024)*
            sin(2*phi) +
            (15*e_sqrd*e_sqrd/256 + 45*e_sqrd*e_sqrd*e_sqrd/1024)*sin(4*phi) -
            (35*e_sqrd*e_sqrd*e_sqrd/3072)*sin(6*phi) );

      m0 = 111132.0894*phi0 - 16216.94*sin(2*phi0) + 17.21*sin(4*phi0) -
           0.02*sin(6*phi0);

      x = k0*n*( aa +
                 (1-t+c)*pow(aa,3.)/6 +
                 (5-18*t+t*t+72*c-58*e_prime_sqrd)*pow(aa,5.)/120 );
      y = k0*( m - m0 + n*tan(phi)*(
               aa*aa/2 + (5-t+9*c+4*c*c)*pow(aa,4.)/24 +
               (61-58*t+t*t+600*c-330*e_prime_sqrd)*pow(aa,6.)/720) );
   }
   else
   {
      m = a * (
            (1 - e_sqrd/4 - 3*e_sqrd*e_sqrd/64 - 5*e_sqrd*e_sqrd*e_sqrd/256) *
            phi -
            (3*e_sqrd/8 + 3*e_sqrd*e_sqrd/32 + 45*e_sqrd*e_sqrd*e_sqrd/1024) *
            sin(2*phi) +
            (15*e_sqrd*e_sqrd/256 + 45*e_sqrd*e_sqrd*e_sqrd/1024)*sin(4*phi) -
            (35*e_sqrd*e_sqrd*e_sqrd/3072)*sin(6*phi) );

      m0 = 111132.0894*phi0 - 16216.94*sin(2*phi0) + 17.21*sin(4*phi0) -
           0.02*sin(6*phi0);

      x = 0.0;
      y = k0*( m - m0 );
   }

   // add false easting to x and false northing to y
   x += m_geodata.m_proj_false_easting;
   y += m_geodata.m_proj_false_northing;

   m_geodata.fwd_transform( x, y, dhpix, dvpix );

   // round to nearest integer
   if( dhpix >= 0.0 )
      hpix = (int)(dhpix + 0.5);
   else
      hpix = (int)(dhpix - 0.5);
   if( dvpix >= 0.0 )
      vpix = (int)(dvpix + 0.5);
   else
      vpix = (int)(dvpix - 0.5);

   return SUCCESS;

FAIL:
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::inv_transform_lat_long( int hpix, int vpix,
                                      double &latitude, double &longitude )
{
   // performs inverse transform for lat/long projection using data in m_geodata
   // returns latitude and longitude in degrees for specified pixel
   // returns SUCCESS or FAILURE

   m_geodata.inv_transform( hpix, vpix, longitude, latitude );

   // check for longitude limits
   if( longitude > 180.0 )
      longitude -= 360.0;
   if( longitude < -180.0 )
      longitude += 360.0;

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::fwd_transform_lat_long( double latitude, double longitude,
                                      int &hpix, int &vpix )
{
   // performs forward transform for lat/long projection using data in m_geodata
   // returns pixel coordinates for specified latitude and longitude
   // returns SUCCESS or FAILURE

   double dhpix, dvpix;

   m_geodata.fwd_transform( longitude, latitude, dhpix, dvpix );

   // round to nearest integer
   if( dhpix >= 0.0 )
      hpix = (int)(dhpix + 0.5);
   else
      hpix = (int)(dhpix - 0.5);
   if( dvpix >= 0.0 )
      vpix = (int)(dvpix + 0.5);
   else
      vpix = (int)(dvpix - 0.5);

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::inv_transform_lambert_conf_conic( int hpix, int vpix,
                                                double &latitude,
                                                double &longitude )
{
   // performs inverse transform for lambert conformal conic projection
   // using data in m_geodata
   // returns latitude and longitude in degrees for specified pixel
   // returns SUCCESS or FAILURE

   int sign_n;
   double a, b, f, e_sqrd, e, x, y;
   double phi0, phi1, phi2, lambda0, theta, chi;
   double t, rho, rho0, F, n, m1, m2, t0, t1, t2;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
   double pi_over_4 = atan( 1.0 );

   m_geodata.inv_transform( hpix, vpix, x, y );

   // subtract false easting from x and false northing from y.  A 2SP Lambert
   // spells these ProjFalseOrigin{Easting,Northing} (3086/3087); fall back to
   // those when the 1SP keys (3082/3083) are absent.
   if( m_geodata.m_proj_false_easting_present ||
       !m_geodata.m_proj_false_origin_easting_present )
      x -= m_geodata.m_proj_false_easting;
   else
      x -= m_geodata.m_proj_false_origin_easting;
   if( m_geodata.m_proj_false_northing_present ||
       !m_geodata.m_proj_false_origin_northing_present )
      y -= m_geodata.m_proj_false_northing;
   else
      y -= m_geodata.m_proj_false_origin_northing;

   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;
   e = sqrt( e_sqrd );

   // 2SP's origin is ProjFalseOrigin{Lat,Long}; ProjNatOrigin* is the 1SP
   // spelling, which older writers used here.  Take whichever is present -
   // a file that loads today has the same one it always had.
   if( m_geodata.m_proj_false_origin_lat_present )
      phi0 = m_geodata.m_proj_false_origin_lat * pi_over_180;
   else
      phi0 = m_geodata.m_proj_nat_origin_lat * pi_over_180;
   phi1 = m_geodata.m_proj_std_parallel_1 * pi_over_180;
   phi2 = m_geodata.m_proj_std_parallel_2 * pi_over_180;
   if( m_geodata.m_proj_nat_origin_long_present )
      lambda0 = m_geodata.m_proj_nat_origin_long * pi_over_180;
   else
      lambda0 = m_geodata.m_proj_false_origin_long * pi_over_180;

   m1 = cos( phi1 ) / sqrt( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ) );
   m2 = cos( phi2 ) / sqrt( 1 - e_sqrd * sin( phi2 ) * sin( phi2 ) );

   t0 = tan( pi_over_4 - phi0/2 ) /
        pow( (1-e*sin(phi0)) / (1+e*sin(phi0)), e/2 );
   t1 = tan( pi_over_4 - phi1/2 ) /
        pow( (1-e*sin(phi1)) / (1+e*sin(phi1)), e/2 );
   t2 = tan( pi_over_4 - phi2/2 ) /
        pow( (1-e*sin(phi2)) / (1+e*sin(phi2)), e/2 );

   n = ( log( m1 ) - log( m2 ) ) / ( log( t1 ) - log( t2 ) );
   if( n >= 0.0 )
      sign_n = 1;
   else
      sign_n = -1;
   F = m1 / ( n * pow( t1, n ) );
   rho0 = a * F * pow( t0, n );

   rho = sign_n * sqrt( x*x + pow(rho0-y,2) );
   if( sign_n == 1 )
      theta = atan2( x, rho0-y );
   else
      theta = atan2( -x, y-rho0 );

   t = pow( rho/a/F, 1.0/n );

   chi = 2 * pi_over_4 - 2 * atan( t );

   latitude = chi +
              (e_sqrd/2 + 5*pow(e,4)/24 + pow(e,6)/12 + 13*pow(e,8)/360) *
              sin(2*chi) +
              (7*pow(e,4)/48 + 29*pow(e,6)/240 + 811*pow(e,8)/11520) *
              sin(4*chi) +
              (7*pow(e,6)/120 + 81*pow(e,8)/1120)*sin(6*chi) +
              (4279*pow(e,8)/161280)*sin(8*chi);
   longitude = theta / n + lambda0;

   // convert from radians to degrees
   latitude /= pi_over_180;
   longitude /= pi_over_180;

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::fwd_transform_lambert_conf_conic( double latitude,
                                                double longitude,
                                                int &hpix, int &vpix )
{
   // performs forward transform for lambert conformal conic projection
   // using data in m_geodata
   // returns pixel coordinates for specified latitude and longitude
   // returns SUCCESS or FAILURE

   int sign_n;
   double dhpix, dvpix;
   double a, b, f, e_sqrd, e, x, y, phi, lambda;
   double phi0, phi1, phi2, lambda0, theta;
   double t, rho, rho0, F, n, m1, m2, t0, t1, t2;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
   double pi_over_4 = atan( 1.0 );

   phi = latitude * pi_over_180;
   lambda = longitude * pi_over_180;

   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;
   e = sqrt( e_sqrd );

   // 2SP's origin is ProjFalseOrigin{Lat,Long}; ProjNatOrigin* is the 1SP
   // spelling, which older writers used here.  Take whichever is present -
   // a file that loads today has the same one it always had.
   if( m_geodata.m_proj_false_origin_lat_present )
      phi0 = m_geodata.m_proj_false_origin_lat * pi_over_180;
   else
      phi0 = m_geodata.m_proj_nat_origin_lat * pi_over_180;
   phi1 = m_geodata.m_proj_std_parallel_1 * pi_over_180;
   phi2 = m_geodata.m_proj_std_parallel_2 * pi_over_180;
   if( m_geodata.m_proj_nat_origin_long_present )
      lambda0 = m_geodata.m_proj_nat_origin_long * pi_over_180;
   else
      lambda0 = m_geodata.m_proj_false_origin_long * pi_over_180;

   m1 = cos( phi1 ) / sqrt( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ) );
   m2 = cos( phi2 ) / sqrt( 1 - e_sqrd * sin( phi2 ) * sin( phi2 ) );

   t0 = tan( pi_over_4 - phi0/2 ) /
        pow( (1-e*sin(phi0)) / (1+e*sin(phi0)), e/2 );
   t1 = tan( pi_over_4 - phi1/2 ) /
        pow( (1-e*sin(phi1)) / (1+e*sin(phi1)), e/2 );
   t2 = tan( pi_over_4 - phi2/2 ) /
        pow( (1-e*sin(phi2)) / (1+e*sin(phi2)), e/2 );

   n = ( log( m1 ) - log( m2 ) ) / ( log( t1 ) - log( t2 ) );
   if( n >= 0.0 )
      sign_n = 1;
   else
      sign_n = -1;
   F = m1 / ( n * pow( t1, n ) );
   rho0 = a * F * pow( t0, n );

   t = tan( pi_over_4 - phi/2 ) /
        pow( (1-e*sin(phi)) / (1+e*sin(phi)), e/2 );
   rho = a * F * pow( t, n );
   theta = n * (lambda - lambda0);
   x = rho * sin(theta);
   y = rho0 - rho * cos(theta);

   // add false easting to x and false northing to y - the same 2SP fallback
   // the inverse transform makes
   if( m_geodata.m_proj_false_easting_present ||
       !m_geodata.m_proj_false_origin_easting_present )
      x += m_geodata.m_proj_false_easting;
   else
      x += m_geodata.m_proj_false_origin_easting;
   if( m_geodata.m_proj_false_northing_present ||
       !m_geodata.m_proj_false_origin_northing_present )
      y += m_geodata.m_proj_false_northing;
   else
      y += m_geodata.m_proj_false_origin_northing;

   m_geodata.fwd_transform( x, y, dhpix, dvpix );

   // round to nearest integer
   if( dhpix >= 0.0 )
      hpix = (int)(dhpix + 0.5);
   else
      hpix = (int)(dhpix - 0.5);
   if( dvpix >= 0.0 )
      vpix = (int)(dvpix + 0.5);
   else
      vpix = (int)(dvpix - 0.5);

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::inv_transform_albers_equal_area( int hpix, int vpix,
                                        double &latitude, double &longitude )
{
   // performs inverse transform for albers equal area projection
   // using data in m_geodata
   // returns latitude and longitude in degrees for specified pixel
   // returns SUCCESS or FAILURE

   int sign_n;
   double a, b, f, e_sqrd, e, x, y;
   double phi0, phi1, phi2, lambda0, theta, beta;
   double rho, rho0, n, m1, m2;
   double q, q0, q1, q2, c;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
//   double pi_over_4 = atan( 1.0 );

   m_geodata.inv_transform( hpix, vpix, x, y );

   // subtract false easting from x and false northing from y
   x -= m_geodata.m_proj_false_easting;
   y -= m_geodata.m_proj_false_northing;

   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;
   e = sqrt( e_sqrd );

   phi0 = m_geodata.m_proj_nat_origin_lat * pi_over_180;
   phi1 = m_geodata.m_proj_std_parallel_1 * pi_over_180;
   phi2 = m_geodata.m_proj_std_parallel_2 * pi_over_180;
   if (m_geodata.m_proj_center_long_present)
      lambda0 = m_geodata.m_proj_center_long * pi_over_180;
   else if (m_geodata.m_proj_nat_origin_long_present)
      lambda0 = m_geodata.m_proj_nat_origin_long * pi_over_180;
   else
      return FAILURE;


   m1 = cos( phi1 ) / sqrt( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ) );
   m2 = cos( phi2 ) / sqrt( 1 - e_sqrd * sin( phi2 ) * sin( phi2 ) );

   q0 = (1-e_sqrd) * (
                        sin(phi0)/(1-e_sqrd*sin(phi0)*sin(phi0)) -
                        (1./2./e)*log( (1-e*sin(phi0))/(1+e*sin(phi0)) )
                     );
   q1 = (1-e_sqrd) * (
                        sin(phi1)/(1-e_sqrd*sin(phi1)*sin(phi1)) -
                        (1./2./e)*log( (1-e*sin(phi1))/(1+e*sin(phi1)) )
                     );
   q2 = (1-e_sqrd) * (
                        sin(phi2)/(1-e_sqrd*sin(phi2)*sin(phi2)) -
                        (1./2./e)*log( (1-e*sin(phi2))/(1+e*sin(phi2)) )
                     );

   n = ( m1*m1 - m2*m2 ) / ( q2 - q1 );
   if( n >= 0.0 )
      sign_n = 1;
   else
      sign_n = -1;

   c = m1*m1 + n*q1;
   rho0 = a * sqrt(c-n*q0) / n;

   rho = sqrt( x*x + pow(rho0-y,2) );
   q = ( c - rho*rho*n*n/a/a ) / n;
   if( sign_n == 1 )
      theta = atan2( x, rho0-y );
   else
      theta = atan2( -x, y-rho0 );

   beta = asin( q / ( 1 - ((1-e_sqrd)/2/e) * log( (1-e)/(1+e) ) ) );

   latitude = beta +
              ( e_sqrd/3 + 31*pow(e,4)/180 + 517*pow(e,6)/5040 ) * sin(2*beta) +
              ( 23*pow(e,4)/360 + 251*pow(e,6)/3780 ) * sin(4*beta) +
              ( 761*pow(e,6)/45360 ) * sin(6*beta);
   longitude = lambda0 + theta/n;

   // convert from radians to degrees
   latitude /= pi_over_180;
   longitude /= pi_over_180;

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::fwd_transform_albers_equal_area( double latitude,
                                               double longitude,
                                               int &hpix, int &vpix )
{
   // performs forard transform for albers equal area projection
   // using data in m_geodata
   // returns pixel coordinates for specified latitude and longitude
   // returns SUCCESS or FAILURE

   int sign_n;
   double dhpix, dvpix;
   double a, b, f, e_sqrd, e, x, y;
   double phi, lambda;
   double phi0, phi1, phi2, lambda0, theta;
   double rho, rho0, n, m1, m2;
   double q, q0, q1, q2, c;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
//   double pi_over_4 = atan( 1.0 );

   phi = latitude * pi_over_180;
   lambda = longitude * pi_over_180;

   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;
   e = sqrt( e_sqrd );

   phi0 = m_geodata.m_proj_nat_origin_lat * pi_over_180;
   phi1 = m_geodata.m_proj_std_parallel_1 * pi_over_180;
   phi2 = m_geodata.m_proj_std_parallel_2 * pi_over_180;
   if (m_geodata.m_proj_center_long_present)
      lambda0 = m_geodata.m_proj_center_long * pi_over_180;
   else if (m_geodata.m_proj_nat_origin_long_present)
      lambda0 = m_geodata.m_proj_nat_origin_long * pi_over_180;
   else
      return FAILURE;

   m1 = cos( phi1 ) / sqrt( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ) );
   m2 = cos( phi2 ) / sqrt( 1 - e_sqrd * sin( phi2 ) * sin( phi2 ) );

   q0 = (1-e_sqrd) * (
                        sin(phi0)/(1-e_sqrd*sin(phi0)*sin(phi0)) -
                        (1./2./e)*log( (1-e*sin(phi0))/(1+e*sin(phi0)) )
                     );
   q1 = (1-e_sqrd) * (
                        sin(phi1)/(1-e_sqrd*sin(phi1)*sin(phi1)) -
                        (1./2./e)*log( (1-e*sin(phi1))/(1+e*sin(phi1)) )
                     );
   q2 = (1-e_sqrd) * (
                        sin(phi2)/(1-e_sqrd*sin(phi2)*sin(phi2)) -
                        (1./2./e)*log( (1-e*sin(phi2))/(1+e*sin(phi2)) )
                     );

   n = ( m1*m1 - m2*m2 ) / ( q2 - q1 );
   if( n >= 0.0 )
      sign_n = 1;
   else
      sign_n = -1;

   c = m1*m1 + n*q1;
   rho0 = a * sqrt(c-n*q0) / n;

   q = (1-e_sqrd) * ( sin(phi)/(1-e_sqrd*sin(phi)*sin(phi)) -
                      (1./2./e) * log( (1-e*sin(phi))/(1+e*sin(phi)) ) );
   rho = a * sqrt(c-n*q) / n;
   theta = n*(lambda-lambda0);

   x = rho * sin(theta);
   y = rho0 - rho*cos(theta);

   // add false easting to x and false northing to y
   x += m_geodata.m_proj_false_easting;
   y += m_geodata.m_proj_false_northing;

   m_geodata.fwd_transform( x, y, dhpix, dvpix );

   // round to nearest integer
   if( dhpix >= 0.0 )
      hpix = (int)(dhpix + 0.5);
   else
      hpix = (int)(dhpix - 0.5);
   if( dvpix >= 0.0 )
      vpix = (int)(dvpix + 0.5);
   else
      vpix = (int)(dvpix - 0.5);

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::print_tags( FILE *file_ptr )
{
   // this function prints the tags to the specified file pointer
   INT_PTR i, imax;
   CStringArray cstring_array;

   // use CstringArray version to assure identical output
   print_tags( cstring_array );

   imax = cstring_array.GetUpperBound( );

   for( i = 0; i <= imax; i++ )
      fprintf( file_ptr, "%s\n", (LPCSTR)cstring_array.GetAt( i ) );
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::print_tags( CStringArray &cstring_array )
{
   // this function prints the tags to the specified CStringArray
   // tag info is appended to any existing data in the CStringArray
   // so that various data can be concatenated

   int i;
   char string[81];
   CString tstr;

   for( i = 0; i < 80; i++ )
      string[i] = '-';
   string[80] = NULL;
   cstring_array.Add( string );

   if( m_error )
   {
      tstr.Format("ERROR: %s", (LPCSTR)m_cumulative_error_description );
      cstring_array.Add( tstr );
      cstring_array.Add( "" );
   }

   // print file name
   tstr.Format("Tag Info for File: %s", (LPCSTR)m_tiff_file_name );
   cstring_array.Add( tstr );
   cstring_array.Add( "" );

   // number of tags
   tstr.Format("No. of Tags: %i", m_num_tags );
   cstring_array.Add( tstr );
   cstring_array.Add( "" );

   for( i = 0; i < m_num_tags; i++ )
   {
      tstr.Format("Tag No.: %i", i );
      cstring_array.Add( tstr );
      tstr.Format("Tag ID: %u => %s", m_tags[i].m_tag_id, (LPCSTR)m_tags[i].m_tag_name );
      cstring_array.Add( tstr );
      tstr.Format("Type: %u => %s", m_tags[i].m_type, (LPCSTR)m_tags[i].m_type_name );
      cstring_array.Add( tstr );
      tstr.Format("%i Value(s) => %s", m_tags[i].m_count, (LPCSTR)m_tags[i].m_value_name );
      cstring_array.Add( tstr );
      cstring_array.Add( "" );
   }
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::print_geokeys( FILE *file_ptr )
{
   // this function prints the geokeys to the specified file pointer
   INT_PTR i, imax;
   CStringArray cstring_array;

   // use CstringArray version to assure identical output
   print_geokeys( cstring_array );

   imax = cstring_array.GetUpperBound( );

   for( i = 0; i <= imax; i++ )
      fprintf( file_ptr, "%s\n", (LPCSTR)cstring_array.GetAt( i ) );
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::print_geokeys( CStringArray &cstring_array )
{
   // this function prints the geokeys to the specified CStringArray
   // tag info is appended to any existing data in the CStringArray
   // so that various data can be concatenated

   int i;
   const int STR_LEN = 1000;
   char string[STR_LEN];

   for( i = 0; i < 200; i++ )
      string[i] = '-';
   string[200] = NULL;
   cstring_array.Add( string );

   if( m_error )
   {
      sprintf_s( string, STR_LEN, "ERROR: %s", (LPCSTR)m_cumulative_error_description );
      cstring_array.Add( string );
      cstring_array.Add( "" );
   }

   // print file name
   sprintf_s( string, STR_LEN, "GeoKey Info for File: %s", (LPCSTR)m_tiff_file_name );
   cstring_array.Add( string );
   cstring_array.Add( "" );

   if( m_geodata_present )
      cstring_array.Add( "Geodata is Present" );
   else
      cstring_array.Add( "Geodata is Not Present" );
   if( m_geodata_supported )
      cstring_array.Add( "Geodata is Supported" );
   else
      cstring_array.Add( "Geodata is Not Supported" );
   cstring_array.Add( "" );

   // number of geokeys
   sprintf_s( string, STR_LEN, "No. of GeoKeys: %i", m_num_geokeys );
   cstring_array.Add( string );
   cstring_array.Add( "" );

   for( i = 0; i < m_num_geokeys; i++ )
   {
      sprintf_s( string, STR_LEN, "GeoKey No.: %i", i );
      cstring_array.Add( string );
      sprintf_s( string, STR_LEN, "GeoKey ID: %u => %s",
               m_geokeys[i].m_geokey_id, (LPCSTR)m_geokeys[i].m_geokey_name );
      cstring_array.Add( string );
      sprintf_s( string, STR_LEN, "Type: %u => %s",
               m_geokeys[i].m_type, (LPCSTR)m_geokeys[i].m_type_name );
      cstring_array.Add( string );
      sprintf_s( string, STR_LEN, "%i Value(s) => %s",
               m_geokeys[i].m_count, (LPCSTR)m_geokeys[i].m_value_name );
      cstring_array.Add( string );
      cstring_array.Add( "" );
   }
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::print_image_info( FILE *file_ptr )
{
   // this function prints the image info to the specified file pointer
   INT_PTR i, imax;
   CStringArray cstring_array;

   // use CstringArray version to assure identical output
   print_image_info( cstring_array );

   imax = cstring_array.GetUpperBound( );

   for( i = 0; i <= imax; i++ )
      fprintf( file_ptr, "%s\n", (LPCSTR)cstring_array.GetAt( i ) );
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::print_image_info( CStringArray &cstring_array )
{
   // this function prints the image info to the specified CStringArray
   // tag info is appended to any existing data in the CStringArray
   // so that various data can be concatenated

   int i;
   const int STR_LEN = 1000;
   char string[STR_LEN];

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);

   for( i = 0; i < 200; i++ )
      string[i] = '-';
   string[200] = NULL;
   cstring_array.Add( string );

   if( m_error )
   {
      sprintf_s( string, STR_LEN, "ERROR: %s", (LPCSTR)m_cumulative_error_description );
      cstring_array.Add( string );
      cstring_array.Add( "" );
   }

   // print file name
   sprintf_s( string, STR_LEN, "Image Info for File: %s", (LPCSTR)m_tiff_file_name );
   cstring_array.Add( string );
   cstring_array.Add( "" );

   // byte order
   switch( m_byte_order )
   {
      case GEOTIFF_LITTLE_ENDIAN:
         cstring_array.Add( "Byte Order: Little Endian (Intel)" );
         break;
      case GEOTIFF_BIG_ENDIAN:
         cstring_array.Add( "Byte Order: Big Endian (Motorola)" );
         break;
      default:
         cstring_array.Add( "Byte Order: Undefined" );
         break;
   }

   // image type
   if( m_image_type_supported )
      sprintf_s( string, STR_LEN, "Image Type is Supported" );
   else
      sprintf_s( string, STR_LEN, "Image Type is Not Supported" );
   cstring_array.Add( string );
   sprintf_s( string, STR_LEN, "Image Type: %s", (LPCSTR)m_image_type_description );
   cstring_array.Add( string );

   // image width
   sprintf_s( string, STR_LEN, "Image Width: %u", m_image_width );
   cstring_array.Add( string );

   // image length
   sprintf_s( string, STR_LEN, "Image Length: %u", m_image_length );
   cstring_array.Add( string );

   // pixel size
   if (m_pixel_size >= 1000.0)
      sprintf_s( string, STR_LEN, "Native Pixel Size: %.0f meters", m_pixel_size);
   else if (m_pixel_size >= 100.0)
      sprintf_s( string, STR_LEN, "Native Pixel Size: %.1f meters", m_pixel_size);
   else if (m_pixel_size >= 10.0)
      sprintf_s( string, STR_LEN, "Native Pixel Size: %.2f meters", m_pixel_size);
   else
      sprintf_s( string, STR_LEN, "Native Pixel Size: %.3f meters", m_pixel_size);
   cstring_array.Add( string );

   // compression scheme
   strcpy_s( string, STR_LEN, "Compression Scheme: " );
   switch(m_compression_scheme)
   {
      case GEOTIFF_NO_COMPRESSION:
         strcat_s(string, STR_LEN, "Uncompressed");
         break;
      case GEOTIFF_PACKBITS:
         strcat_s(string, STR_LEN, "Packbits");
         break;
      case GEOTIFF_JPEG:
         strcat_s(string, STR_LEN, "JPEG");
         break;
      case GEOTIFF_CCITT_1D:
         strcat_s(string, STR_LEN, "CCITT");
         break;
      case GEOTIFF_GROUP_3_FAX:
         strcat_s(string, STR_LEN, "GROUP 3 FAX");
         break;
      case GEOTIFF_GROUP_4_FAX:
         strcat_s(string, STR_LEN, "GROUP 4 FAX");
         break;
      case GEOTIFF_LZW:
         strcat_s(string, STR_LEN, "LZW");
         break;
      default:
         strcat_s(string, STR_LEN, "Unknown");
         break;
   }

   cstring_array.Add( string );

   // photometric interpretation
   strcpy_s( string, STR_LEN, "Photometric Interpretation: ");
   switch(m_photometric_interpretation)
   {
      case GEOTIFF_RGB:
         strcat_s(string, STR_LEN, "RGB");
         break;
      case GEOTIFF_RGB_PALETTE:
         strcat_s(string, STR_LEN, "RGB Palette");
         break;
      case GEOTIFF_CMYK:
         strcat_s(string, STR_LEN, "CMYK");
         break;
      case GEOTIFF_WHITE_IS_ZERO:
         strcat_s(string, STR_LEN, "White is Zero");
         break;
      case GEOTIFF_BLACK_IS_ZERO:
         strcat_s(string, STR_LEN, "Black is Zero");
         break;
      case GEOTIFF_TRANSPARENCY_MASK:
         strcat_s(string, STR_LEN, "Transparency Mask");
         break;
      case GEOTIFF_YCBCR:
         strcat_s(string, STR_LEN, "YCbCr");
         break;
      case GEOTIFF_CIE_LAB:
         strcat_s(string, STR_LEN, "CIE Lab");
         break;
      default:
         strcat_s(string, STR_LEN, "Unknown");
         break;
   }
   cstring_array.Add( string );

   // check for tiled
   if( m_image_is_tiled )
   {
      cstring_array.Add( "Image is Tiled" );

      // tile width
      sprintf_s( string, STR_LEN, "Tile Width: %u", m_tile_width );
      cstring_array.Add( string );

      // tile length
      sprintf_s( string, STR_LEN, "Tile Length: %u", m_tile_length );
      cstring_array.Add( string );

      // number of tiles across
      sprintf_s( string, STR_LEN, "No. Tiles Across: %u", m_num_tiles_across );
      cstring_array.Add( string );

      // number of tiles down
      sprintf_s( string, STR_LEN, "No. Tiles Down: %u", m_num_tiles_down );
      cstring_array.Add( string );

      // number of tiles
      sprintf_s( string, STR_LEN, "No. Tiles: %u", m_num_tiles );
      cstring_array.Add( string );
   }
   else
   {
      cstring_array.Add( "Image is Not Tiled" );

      // rows per strip
      sprintf_s( string, STR_LEN, "Rows Per Strip: %u", m_rows_per_strip );
      cstring_array.Add( string );

      // number of strips
      sprintf_s( string, STR_LEN, "Number of Strips: %u", m_num_strips );
      cstring_array.Add( string );

      // max strip byte count
      sprintf_s( string, STR_LEN, "Max Strip Byte Count: %u", m_max_strip_byte_count );
      cstring_array.Add( string );
   }

   // x resolution
   sprintf_s( string, STR_LEN, "X Resolution: %lf", m_x_resolution );
   cstring_array.Add( string );

   // y resolution
   sprintf_s( string, STR_LEN, "Y Resolution: %lf", m_y_resolution );
   cstring_array.Add( string );

   // resolution unit
   sprintf_s( string, STR_LEN, "Resolution Unit: %u", m_resolution_unit );
   cstring_array.Add( string );

   // planar configuration
   strcpy_s( string, STR_LEN, "Planar Configuration: ");
   if ( m_planar_configuration == GEOTIFF_CHUNKY_FORMAT )
      strcat_s(string, STR_LEN, "CHUNKY");
   else if (m_planar_configuration == GEOTIFF_PLANAR_FORMAT )
      strcat_s(string, STR_LEN, "PLANAR");
   else
      strcat_s(string, STR_LEN, "<unknown>");
   cstring_array.Add( string );

   // samples per pixel
   sprintf_s( string, STR_LEN, "Samples Per Pixel: %u", m_samples_per_pixel );
   cstring_array.Add( string );

   // bits per sample
   sprintf_s( string, STR_LEN, "Bits Per Sample: %u", m_bits_per_sample );
   cstring_array.Add( string );

   // fill order
   sprintf_s( string, STR_LEN, "Fill Order: %u", m_fill_order );
   cstring_array.Add( string );

   // orientation
   sprintf_s( string, STR_LEN, "Orientation: %u", m_orientation );
   cstring_array.Add( string );

   // color map
   if( m_color_map_present )
      sprintf_s( string, STR_LEN, "Color Map: Present with %i Indices",
               m_num_color_map_values );
   else
      sprintf_s( string, STR_LEN, "Color Map: Not Present" );
   cstring_array.Add( string );

   cstring_array.Add( "" );
}

// ********************************************************************************************
// ********************************************************************************************

CString CGeoTiff::get_info(void)
{
   CString edit;
   CString tstr, tstr2;
   CString cr("\r\n");
   int rslt;

   if (!m_image)
      return "";

   tstr.Format("Filename: %s", (LPCSTR)m_tiff_file_name);
   edit = tstr;
   edit += cr;
// tstr.Format("File Date: %s", m_image->m_date);
// edit += tstr;
// edit += cr;
// tstr.Format("File Title: %s", m_image->m_file_title);
// edit += tstr;
// edit += cr;
// tstr.Format("Image Title: %s", m_image->m_image_title);
// edit += tstr;
// edit += cr;
// tstr.Format("Num Images: %d, Symbols: %d, Text: %d, Data: %d",
//          m_image->m_num_images, m_image->m_num_symbols, m_image->m_num_text, m_image->m_num_data);
// edit += tstr;
// edit += cr;


   tstr.Format("Image Size: %dx%d", m_image->m_image_width, m_image->m_image_height);
   edit += tstr;
   edit += cr;

   if (m_pixel_size > 0.0)
   {
      if (m_pixel_size >= 1000.0)
         tstr2.Format("%.0f", m_pixel_size);
      else if (m_image->m_pixel_size >= 100.0)
         tstr2.Format("%.1f", m_pixel_size);
      else if (m_image->m_pixel_size >= 10.0)
         tstr2.Format("%.2f", m_pixel_size);
      else
         tstr2.Format("%.3f", m_pixel_size);
      tstr.Format("Native Pixel Size: %s meters", (LPCSTR)tstr2 );
      edit += tstr;
      edit += cr;
   }

   if (m_num_directories > 1)
   {
      tstr.Format("Num Pages: %d", m_num_directories);
      edit += tstr;
      edit += cr;
   }

   // check for tiled
   if ( m_image_is_tiled )
   {
      tstr.Format("Tile Size: %dx%d, Count: %dx%d", m_image->m_tile_size_x, m_image->m_tile_size_y,
      m_image->m_tile_cnt_x, m_image->m_tile_cnt_y);
      edit += tstr;
      edit += cr;
   }
   else
   {
      // rows per strip
      tstr.Format("Rows Per Strip: %u", m_rows_per_strip );
      edit += tstr;
      edit += cr;

      // number of strips
      tstr.Format("Number of Strips: %u", m_num_strips );
      edit += tstr;
      edit += cr;

      // max strip byte count
      tstr.Format("Max Strip Byte Count: %u", m_max_strip_byte_count );
      edit += tstr;
      edit += cr;
   }

   tstr.Format("Bits Per Pixel: %d", m_image->m_bpp * m_samples_per_pixel);
   edit += tstr;
   if (m_image->m_abpp > 0)
   {
      tstr.Format(" (%d)", m_image->m_abpp);
      edit += tstr;
   }
   edit += cr;
// tstr.Format("Num Bands: %d", m_image->m_num_bands);
// edit += tstr;
// edit += cr;
   tstr.Format("Compression: %s", (LPCSTR)get_compression_scheme());
   edit += tstr;
   edit += cr;

   // photometric interpretation
   tstr = "Photometric Interpretation: ";
   switch(m_photometric_interpretation)
   {
      case GEOTIFF_RGB:
         tstr += "RGB";
         break;
      case GEOTIFF_RGB_PALETTE:
         tstr += "RGB Palette";
         break;
      case GEOTIFF_CMYK:
         tstr += "CMYK";
         break;
      case GEOTIFF_WHITE_IS_ZERO:
         tstr += "White is Zero";
         break;
      case GEOTIFF_BLACK_IS_ZERO:
         tstr += "Black is Zero";
         break;
      case GEOTIFF_TRANSPARENCY_MASK:
         tstr += "Transparency Mask";
         break;
      case GEOTIFF_YCBCR:
         tstr += "YCbCr";
         break;
      case GEOTIFF_CIE_LAB:
         tstr += "CIE Lab";
         break;
      default:
         tstr += "Unknown";
         break;
   }

   // software text
   rslt = get_software(tstr2);
   if (rslt == SUCCESS)
   {
      tstr.Format("Software: %s", (LPCSTR)tstr2 );
      edit += tstr;
      edit += cr;
   }

   rslt = get_image_description(tstr);
   if (rslt == SUCCESS)
   {
      edit += "Image Description: ";
      edit += tstr;
      edit += cr;
   }

   // x origin
   if (m_geodata.m_model_tie_point_x != NULL)
   {
      tstr.Format("X Origin: %.12f", m_geodata.m_model_tie_point_x[0] );
      edit += tstr;
      edit += cr;
   }

   // y origin
   if (m_geodata.m_model_tie_point_y != NULL)
   {
      tstr.Format("Y Origin: %.12f", m_geodata.m_model_tie_point_y[0] );
      edit += tstr;
      edit += cr;
   }

   // x resolution
   tstr.Format("X Resolution: %.12f", m_geodata.m_dLScaleX );
   edit += tstr;
   edit += cr;

   // y resolution
   tstr.Format("Y Resolution: %.12f", m_geodata.m_dPScaleY );
   edit += tstr;
   edit += cr;

   // resolution unit
   tstr2 = "NONE";
   if (m_resolution_unit == 2)
      tstr2 = "INCH";
   if (m_resolution_unit == 3)
      tstr2 = "CENTIMETER";
   tstr.Format("Resolution Unit: %s", (LPCSTR)tstr2 );
// tstr.Format("Resolution Unit: %u", m_resolution_unit );
   edit += tstr;
   edit += cr;

   // planar configuration
   tstr2 =  "<unknown>";
   if ( m_planar_configuration == GEOTIFF_CHUNKY_FORMAT )
      tstr2 = "CHUNKY";
   else if ( m_planar_configuration == GEOTIFF_PLANAR_FORMAT )
      tstr2 = "PLANAR";
   tstr.Format("Planar Configuration: %s", (LPCSTR)tstr2 );
   edit += tstr;
   edit += cr;

   // samples per pixel
   tstr.Format("Samples Per Pixel: %u", m_samples_per_pixel );
   edit += tstr;
   edit += cr;

   // image representaion
   tstr = "Representation: ";
   if (m_samples_per_pixel == 1)
      tstr += "MONO";
   else if (m_samples_per_pixel == 3)
      tstr += "RGB";
   else if (m_samples_per_pixel > 3)
      tstr += "MULTI";
   edit += tstr;
   edit += cr;

   // bits per sample
   tstr.Format("Bits Per Sample: %u", m_bits_per_sample );
   edit += tstr;
   edit += cr;

   // datum
   if (m_geodata.m_geog_geodetic_datum_present)
   {
      CString tif_datum;
      geotiff_datum_to_string(m_geodata.m_geog_geodetic_datum, tif_datum);
      tstr.Format("Datum: %s", (LPCSTR)tif_datum );
      edit += tstr;
      edit += "\r\n";
   }

   // is image color
   edit += "Color Image: ";
   if ((m_samples_per_pixel > 1) || m_color_map_present)
      edit += "YES";
   else
      edit += "NO";
   edit += cr;

   // projection type
   tstr.Format("Projection Type: %s", (LPCSTR)m_projection_type );
   edit += tstr;
   edit += cr;

   // fill order
   tstr.Format("Fill Order: %u", m_fill_order );
   edit += tstr;
   edit += cr;

   // orientation
   tstr.Format("Orientation: %u", m_orientation );
   edit += tstr;
   edit += cr;

   edit += "OrientationText: ";
   tstr = "<undefined>";
   switch(m_orientation)
   {
      case 1: tstr = "Upper Left"; break;
      case 2: tstr = "Upper Right"; break;
      case 3: tstr = "Lower Right"; break;
      case 4: tstr = "Lower Left"; break;
      case 5: tstr = "Upper Left (xy reversed)"; break;
      case 6: tstr = "Upper Right (xy reversed)"; break;
      case 7: tstr = "Lower Right (xy reversed)"; break;
      case 8: tstr = "Lower Left (xy reversed)"; break;
   }
   edit += tstr;
   edit += cr;

   if (m_using_fid_file && m_using_tfw_file)
   {
      tstr = "Using External Files: TFW, FID";
      edit += tstr;
      edit += cr;
   }
   if (m_using_fid_file && !m_using_tfw_file)
   {
      tstr = "Using External Files: FID";
      edit += tstr;
      edit += cr;
   }
   if (!m_using_fid_file && m_using_tfw_file)
   {
      tstr = "Using External Files: TFW";
      edit += tstr;
      edit += cr;
   }

   if (m_has_elevation_data)
   {
      tstr = "Elevation Data: YES";
      edit += tstr;
      edit += cr;
   }

   if (m_cropped_ll_lat != 0.0)
   {
      tstr.Format("Cropped LL Lat: %f", m_cropped_ll_lat);
      edit += tstr;
      edit += cr;
      tstr.Format("Cropped LL Lon: %f", m_cropped_ll_lon);
      edit += tstr;
      edit += cr;
      tstr.Format("Cropped UR Lat: %f", m_cropped_ur_lat);
      edit += tstr;
      edit += cr;
      tstr.Format("Cropped UR Lon: %f", m_cropped_ur_lon);
      edit += tstr;
      edit += cr;
   }

   if (m_geodata.m_gt_model_type_present)
   {
      tstr.Format("GTModelTypeGeoKey: %u", m_geodata.m_gt_model_type );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_gt_raster_type_present)
   {
      tstr.Format("GTRasterTypeGeoKey: %u", m_geodata.m_gt_raster_type );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_gt_citation_present)
   {
      tstr.Format("GTCitationGeoKey: %u", (LPCSTR)m_geodata.m_gt_citation );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_geographic_type_present)
   {
      tstr.Format("GeographicTypeGeoKey: %u", m_geodata.m_geographic_type );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_geog_citation_present)
   {
      tstr.Format("GeogCitationGeoKey: %u", (LPCSTR)m_geodata.m_geog_citation );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_geog_geodetic_datum_present)
   {
      tstr.Format("GeogGeodeticDatumGeoKey: %u", m_geodata.m_geog_geodetic_datum );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_geog_prime_meridian_present)
   {
      tstr.Format("GeogPrimeMeridianGeoKey: %u", m_geodata.m_geog_prime_meridian );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_geog_ellipsoid_present)
   {
      tstr.Format("GeogEllipsoidGeoKey: %u", m_geodata.m_geog_ellipsoid );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_geog_semi_major_axis_present)
   {
      tstr.Format("GeogSemiMajorAxisGeoKey: %f", m_geodata.m_geog_semi_major_axis );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_geog_semi_minor_axis_present)
   {
      tstr.Format("GeogSemiMinorAxisGeoKey: %f", m_geodata.m_geog_semi_minor_axis );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_geog_inv_flattening_present)
   {
      tstr.Format("GeogInvFlatteningGeoKey: %f", m_geodata.m_geog_inv_flattening );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_projected_cs_type_present)
   {
      tstr.Format("ProjectedCSTypeGeoKey: %u", m_geodata.m_projected_cs_type );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_pcs_citation_present)
   {
      tstr.Format("PCSCitationGeoKey: %s", (LPCSTR)m_geodata.m_pcs_citation );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_projection_present)
   {
      tstr.Format("ProjectionGeoKey: %u", m_geodata.m_projection );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_coord_trans_present)
   {
      tstr.Format("ProjCoordTransGeoKey: %u", m_geodata.m_proj_coord_trans );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_std_parallel_1_present)
   {
      tstr.Format("ProjStdParallel1GeoKey: %f", m_geodata.m_proj_std_parallel_1 );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_std_parallel_2_present)
   {
      tstr.Format("ProjStdParallel2GeoKey: %f", m_geodata.m_proj_std_parallel_2 );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_nat_origin_long_present)
   {
      tstr.Format("ProjNatOriginLongGeoKey: %f", m_geodata.m_proj_nat_origin_long );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_nat_origin_lat_present)
   {
      tstr.Format("ProjNatOriginLatGeoKey: %f", m_geodata.m_proj_nat_origin_lat );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_false_easting_present)
   {
      tstr.Format("ProjFalseEastingGeoKey: %f", m_geodata.m_proj_false_easting );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_false_northing_present)
   {
      tstr.Format("ProjFalseNorthingGeoKey: %f", m_geodata.m_proj_false_northing );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_false_origin_long_present)
   {
      tstr.Format("ProjFalseOriginLongGeoKey: %f", m_geodata.m_proj_false_origin_long );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_false_origin_lat_present)
   {
      tstr.Format("ProjFalseOriginLatGeoKey: %f", m_geodata.m_proj_false_origin_lat );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_false_origin_easting_present)
   {
      tstr.Format("ProjFalseOriginEastingGeoKey: %f", m_geodata.m_proj_false_origin_easting );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_false_origin_northing_present)
   {
      tstr.Format("ProjFalseOriginNorthingGeoKey: %f", m_geodata.m_proj_false_origin_northing );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_center_long_present)
   {
      tstr.Format("ProjCenterLongGeoKey: %f", m_geodata.m_proj_center_long );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_center_lat_present)
   {
      tstr.Format("ProjCenterLatGeoKey: %f", m_geodata.m_proj_center_lat );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_center_easting_present)
   {
      tstr.Format("ProjCenterEastingGeoKey: %f", m_geodata.m_proj_center_easting );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_center_northing_present)
   {
      tstr.Format("ProjCenterNorthingGeoKey: %f", m_geodata.m_proj_center_northing );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_scale_at_nat_origin_present)
   {
      tstr.Format("ProjScaleAtNatOriginGeoKey: %f", m_geodata.m_proj_scale_at_nat_origin );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_scale_at_center_present)
   {
      tstr.Format("ProjScaleAtCenterGeoKey: %f", m_geodata.m_proj_scale_at_center );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_azimuth_angle_present)
   {
      tstr.Format("ProjAzimuthAngleGeoKey: %f", m_geodata.m_proj_azimuth_angle );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_proj_straight_vert_pole_long_present)
   {
      tstr.Format("ProjStraightVertPoleLongGeoKey: %f", m_geodata.m_proj_straight_vert_pole_long );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_vertical_cs_type_present)
   {
      tstr.Format("VertCSTypeGeoKey: %u", m_geodata.m_vertical_cs_type );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_vertical_citation_present)
   {
      tstr.Format("VertCitationGeoKey: %s", (LPCSTR)m_geodata.m_vertical_citation );
      edit += tstr;
      edit += cr;
   }
   if (m_geodata.m_vertical_datum_present)
   {
      tstr.Format("VerticalDatumGeoKey: %u", m_geodata.m_vertical_datum );
      edit += tstr;
      edit += cr;
   }
   if (m_geog_linear_units_present)
   {
      tstr.Format("GeogLinearUnitsGeoKey: %u", m_geog_linear_units );
      edit += tstr;
      edit += cr;
   }

   return edit;
}
// end of get_info_string(void)

// ********************************************************************************************
// ********************************************************************************************

CString CGeoTiff::get_compression_scheme()
{
   CString text;
   switch(m_compression_scheme)
   {
      case GEOTIFF_NO_COMPRESSION:
         text = "Uncompressed";
         break;
      case GEOTIFF_PACKBITS:
         text = "Packbits";
         break;
      case GEOTIFF_JPEG:
         text = "JPEG";
         break;
      case GEOTIFF_CCITT_1D:
         text = "CCITT";
         break;
      case GEOTIFF_GROUP_3_FAX:
         text = "GROUP 3 FAX";
         break;
      case GEOTIFF_GROUP_4_FAX:
         text = "GROUP 4 FAX";
         break;
      case GEOTIFF_LZW:
         text = "LZW";
         break;
      default:
         text = "Unknown";
         break;
   }

   return text;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_image_size( int &image_width, int &image_length )
{
   // this function returns the image width and length in the arguments
   // returns SUCCESS or FAILURE

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   image_width = m_image_width;
   image_length = m_image_length;

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_subsampled_image_size( int sampling, int &image_width,
                                         int &image_length )
{
   // this function returns the subsampled image width and length in the
   // arguments. the sampling argument specifies the rows and columns that are
   // retained. for example, a sampling value of 3 specifies that every 3rd row
   // and column should be retained in the subsampled image.
   // returns SUCCESS or FAILURE

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // check for valid sampling
   if( sampling < 1 )
      return FAILURE;

   image_width = (m_image_width-1) / sampling + 1;
   image_length = (m_image_length-1) / sampling + 1;

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_subsampled_subimage_size( int sampling,
      int min_hpix, int min_vpix, int max_hpix, int max_vpix,
      int &image_width, int &image_length )
{
   // this function returns the subsampled image width and length in the
   // arguments for the subimage specified by min_hpix, min_vpix, max_hpix,
   // max_vpix. the sampling argument specifies the rows and columns that are
   // retained. for example, a sampling value of 3 specifies that every 3rd row
   // and column should be retained in the subsampled image.
   // returns SUCCESS or FAILURE

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // check for valid sampling
   if( sampling < 1 )
      return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
      return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
      return FAILURE;

   image_width = (max_hpix-min_hpix) / sampling + 1;
   image_length = (max_vpix-min_vpix) / sampling + 1;

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_sampling( int max_width, int max_length, int &sampling )
{
   // this function returns the sampling required to display the entire image
   // within the specified width and length.
   // the sampling specifies the rows and columns that are retained.
   // for example, a sampling value of 3 specifies that every 3rd row and column
   // should be retained in the subsampled image.
   // returns SUCCESS or FAILURE

   int width, length;

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // check for valid max_width and max_length
   if( max_width < 2 || max_width < 2 )
      return FAILURE;

   for( sampling = 1;sampling < (int)m_image_width; sampling++ )
   {
      width = (m_image_width-1) / sampling + 1;
      length = (m_image_length-1) / sampling + 1;
      if( width <= max_width && length <= max_length )
        return SUCCESS;
   }

   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_subimage_sampling( int min_hpix, int min_vpix, int max_hpix,
                                     int max_vpix, int max_width,
                                     int max_length, int &sampling )
{
   // this function returns the sampling required to display the specified
   // subimage within the specified width and length.
   // the sampling specifies the rows and columns that are retained.
   // for example, a sampling value of 3 specifies that every 3rd row and column
   // should be retained in the subsampled image.
   // returns SUCCESS or FAILURE

   int width, length;

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // check for valid max_width and max_length
   if( max_width < 2 || max_width < 2 )
      return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
      return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
      return FAILURE;

   for( sampling = 1;sampling < (int)m_image_width; sampling++ )
   {
      width = (max_hpix-min_hpix) / sampling + 1;
      length = (max_vpix-min_vpix) / sampling + 1;
      if( width <= max_width && length <= max_length )
        return SUCCESS;
   }

   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_image_format( unsigned short &photometric_interpretation,
                                unsigned short &bits_per_sample )
{
   // this function returns the image format in terms of the photometric
   // interpretation and bits per sample
   // returns SUCCESS or FAILURE

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   photometric_interpretation = m_photometric_interpretation;
   bits_per_sample = m_bits_per_sample;

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_rgb_image( unsigned char *img, IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays
   // returns SUCCESS or FAILURE

   int tile_row, tile_col, tile, rslt;
   int image_hpix, image_vpix, image_index;
   int tile_hpix, tile_vpix, tile_index;

   // return FAILURE if no file loaded or there was an error loading
   // the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // return error if image type is not supported
   if( !m_image_type_supported )
      return FAILURE;

   // check for tiled image
   if( m_image_is_tiled )
   {
      // image is tiled

      // loop through all tiles
      for( tile_row = 0; tile_row < (int)m_num_tiles_down; tile_row++ )
      {
         for( tile_col = 0; tile_col < (int)m_num_tiles_across; tile_col++ )
         {
            tile = m_num_tiles_across * tile_row + tile_col;

            if( get_tile_as_rgb_image( tile, m_tile_red_array, m_tile_green_array,
               m_tile_blue_array ) != SUCCESS )
               goto FAIL;

            // copy data from tile
            for( tile_vpix = 0; tile_vpix < (int)m_tile_length; tile_vpix++ )
            {
               image_vpix = tile_row * m_tile_length + tile_vpix;
               if( image_vpix >= (int)m_image_length )
                  break;
               for( tile_hpix = 0; tile_hpix < (int)m_tile_width; tile_hpix++ )
               {
                  image_hpix = tile_col * m_tile_width + tile_hpix;
                  if( image_hpix >= (int)m_image_width )
                     break;
                  tile_index = tile_vpix * m_tile_width + tile_hpix;
                  image_index = image_vpix * m_image_width + image_hpix;
                  img[image_index*3+0] = m_tile_red_array[tile_index];
                  img[image_index*3+1] = m_tile_green_array[tile_index];
                  img[image_index*3+2] = m_tile_blue_array[tile_index];
                  tile_index++;
               }
            }
         }
      }
   }
   else
   {
      // image is not tiled
      // select image type and call appropriate function
      switch( m_image_type )
      {
         case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
            goto FAIL;

         case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
          if (m_samples_per_pixel == 3)
            rslt = get_24bit_rgb_as_rgb_image( img, callback );
          else
            rslt = get_8bit_grayscale_as_rgb_image( img, callback );
          if (rslt != SUCCESS )
            goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
            if( get_16bit_grayscale_as_rgb_image( img, callback ) != SUCCESS )
            goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_256_COLOR:
            if( get_8bit_palette_as_rgb_image( img, callback ) != SUCCESS )
            goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
            if( get_24bit_rgb_as_rgb_image( img, callback ) != SUCCESS )
            goto FAIL;
            break;

         default:                           // should always be one of above
            ASSERT( FALSE );
            goto FAIL;
      }
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_subsampled_rgb_image( int sampling, unsigned char *img, IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays
   // for the entire image, subsampled according to the sampling argument
   // the sampling argument indicates the fraction of the image's rows and
   // columns that are to be retained in the subsampled image. a sampling of 3
   // indicates that every third row and column should be retained
   // returns SUCCESS or FAILURE

   int row, col, index, rslt;
   unsigned char *img_row;

   // return FAILURE if no file loaded or there was an error loading the
   // tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // return FAILURE if image type is not supported
   if( !m_image_type_supported )
      return FAILURE;

   // return FAILURE if sampling is less than 1
   if( sampling < 1 ) return FAILURE;

   // allocate arrays to hold one row of data
   img_row = NULL;

   img_row = (unsigned char*)MEM_malloc( m_image_width );
   if( img_row == NULL )
      goto FAIL;

   switch( m_image_type )
   {
      case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
         goto FAIL;

     case GEOTIFF_IMAGE_TYPE_1BIT:
         index = 0;
         for( row = 0; row < (int)m_image_length; row += sampling )
         {
            // read the row
            if( get_1bit_image_as_rgb_subimage( 0, row, m_image_width-1, row, img_row, callback ) != SUCCESS )
            goto FAIL;
            for( col = 0; col < (int)m_image_width; col += sampling )
            {
               img[index*3+0] = img_row[col*3+0];
               img[index*3+1] = img_row[col*3+1];
               img[index*3+2] = img_row[col*3+2];
               index++;
            }
         }
        break;

      case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
         index = 0;
         for( row = 0; row < (int)m_image_length; row += sampling )
         {
            // read the row
          if (m_samples_per_pixel == 3)
             rslt = get_24bit_rgb_as_rgb_subimage(0, row, m_image_width-1, row, img_row, callback);
          else
            rslt = get_8bit_grayscale_as_rgb_subimage(0, row, m_image_width-1, row, img_row, callback);
         if (rslt != SUCCESS )
            goto FAIL;
            for( col = 0; col < (int)m_image_width; col += sampling )
            {
               img[index*3+0] = img_row[col*3+0];
               img[index*3+1] = img_row[col*3+1];
               img[index*3+2] = img_row[col*3+2];
               index++;
            }
         }
         break;

      case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
         index = 0;
         for( row = 0; row < (int)m_image_length; row += sampling )
         {
            // read the row
            if( get_16bit_grayscale_as_rgb_subimage( 0, row, m_image_width-1, row, img_row, callback ) != SUCCESS )
            goto FAIL;
            for( col = 0; col < (int)m_image_width; col += sampling )
            {
               img[index*3+0] = img_row[col*3+0];
               img[index*3+1] = img_row[col*3+1];
               img[index*3+2] = img_row[col*3+2];
               index++;
            }
         }
         break;

      case GEOTIFF_IMAGE_TYPE_256_COLOR:
         index = 0;
         for( row = 0; row < (int)m_image_length; row += sampling )
         {
            // read the row
            if( get_8bit_palette_as_rgb_subimage( 0, row, m_image_width-1, row, img_row, callback ) != SUCCESS )
            goto FAIL;
            for( col = 0; col < (int)m_image_width; col += sampling )
            {
               img[index*3+0] = img_row[col*3+0];
               img[index*3+1] = img_row[col*3+1];
               img[index*3+2] = img_row[col*3+2];
               index++;
            }
         }
         break;

      case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
         index = 0;
         for( row = 0; row < (int)m_image_length; row += sampling )
         {
            // read the row
            if( get_24bit_rgb_as_rgb_subimage( 0, row, m_image_width-1, row, img_row, callback ) != SUCCESS )
            goto FAIL;
            for( col = 0; col < (int)m_image_width; col += sampling )
            {
               img[index*3+0] = img_row[col*3+0];
               img[index*3+1] = img_row[col*3+1];
               img[index*3+2] = img_row[col*3+2];
               index++;
            }
         }
         break;

      default:                           // should always be one of above
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate memory
   MEM_free( img_row );

   return SUCCESS;

FAIL:
   // deallocate memory
   if ( img_row != NULL )
      MEM_free( img_row );
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_subsampled_palette_subimage( int sampling,
                             int min_hpix, int min_vpix, int max_hpix,
                             int max_vpix, CColorQuantizer &color_quantizer,
                             unsigned char *indices, IImageLibCallback *callback )
{
   // this function returns the color indices for each pixel in the specified
   // for subimage, subsampled according to the sampling argument
   // the sampling argument indicates the fraction of the image's rows and
   // columns that are to be retained in the subsampled image. a sampling of 3
   // indicates that every third row and column should be retained
   // returns SUCCESS or FAILURE

   int row, col, index;
   unsigned char *one_row;

   // return failure if no file loaded or there was an error loading the
   // tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // return error if image type is not supported
   if( !m_image_type_supported )
      return FAILURE;

   // return failure if color quantizer is not defined
   if( !color_quantizer.m_defined )
      return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
      return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
      return FAILURE;

   // return FAILURE if sampling is less than 1
   if( sampling < 1 )
      return FAILURE;

#ifdef FULL_STRIP_READ
   // Allocate full subimage work area
   one_row = (PBYTE) MEM_malloc( ( max_vpix + 1 - min_vpix ) * ( max_hpix + 1 - min_hpix )
      * sizeof(BYTE) );
   if ( one_row == NULL )
      goto FAIL;     // No work area

   // Full resolution subimage to avoid partial strip reads
   if ( get_palette_subimage( min_hpix, min_vpix, max_hpix, max_vpix, color_quantizer,
      one_row, NULL ) != SUCCESS )
      goto FAIL;
   index = 0;        // Output index
   for ( row = min_vpix; row <= max_vpix; row += sampling )
   {
      col = ( max_hpix + 1 - min_hpix ) * ( row - min_vpix );
      INT iColEnd = col + max_hpix - min_hpix;
      for ( ; col <= iColEnd; col += sampling, index++ )
         indices[index] = one_row[col];
   }

#else // Partial strip read
   // allocate array to hold one row of data
   one_row = NULL;

//   one_row = new unsigned char[m_image_width];
   one_row = (unsigned char*)MEM_malloc( m_image_width );
   if( one_row == NULL )
      goto FAIL;

   index = 0;
   for( row = min_vpix; row <= max_vpix; row += sampling )
   {
      // read the row
      if( get_palette_subimage( min_hpix, row, max_hpix, row, color_quantizer, one_row, callback ) != SUCCESS )
         goto FAIL;
      for( col = min_hpix; col <= max_hpix; col += sampling )
      {
         indices[index] = one_row[col-min_hpix];
         index++;
      }
   }
#endif   // Partial strip read

   // deallocate memory
//   delete [] one_row;
   MEM_free( one_row );

   return SUCCESS;

FAIL:
   // deallocate memory
//   if( one_row != NULL ) delete [] one_row;
   if( one_row != NULL )
      MEM_free( one_row );
   return FAILURE;
}
// end of get_subsampled_palette_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_rgb_subimage( int min_hpix, int min_vpix, int width, int height,
                                unsigned char *img, IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage
   // returns SUCCESS or FAILURE

   int min_tile_row, min_tile_col, max_tile_row, max_tile_col;
   int tile_row, tile_col, tile;
   int image_hpix, image_vpix;
   int subimage_hpix, subimage_vpix, subimage_width, subimage_index;
   int tile_hpix, tile_vpix, tile_index, rslt, tile_cnt;
   int max_hpix, max_vpix;
   CUtil util;

   tile_index = 0;

   max_hpix = min_hpix + width - 1;
   max_vpix = min_vpix + height - 1;

   // return if no file loaded or there was an error loading the tiff file
//   if( !m_file_loaded || m_error )
   if( !m_file_loaded )
      return FAILURE;

   // return error if image type is not supported
//   if( !m_image_type_supported )
//    return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
      return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
      return FAILURE;

   subimage_width = max_hpix - min_hpix + 1;

   // check for tiled image
   if( m_image_is_tiled )
   {
      // image is tiled
      // determine range of tile rows and columns in subimage
      min_tile_row = min_vpix / m_tile_length;
      max_tile_row = max_vpix / m_tile_length;
      min_tile_col = min_hpix / m_tile_width;
      max_tile_col = max_hpix / m_tile_width;

     tile_cnt = (max_tile_col - min_tile_col + 1) * (max_tile_row - min_tile_row + 1);

      // loop through tiles covering the subimage
      for( tile_row = min_tile_row; tile_row <= max_tile_row; tile_row++ )
      {
         for( tile_col = min_tile_col; tile_col <= max_tile_col; tile_col++ )
         {
            tile = m_num_tiles_across * tile_row + tile_col;

            if( get_tile_as_rgb_image( tile, m_tile_red_array, m_tile_green_array,
               m_tile_blue_array ) != SUCCESS )
               goto FAIL;

            // copy data from tile
            for( tile_vpix = 0; tile_vpix < (int)m_tile_length; tile_vpix++ )
            {
               image_vpix = tile_row * m_tile_length + tile_vpix;
               if( image_vpix < min_vpix )
                  continue;
               if( image_vpix > max_vpix )
                  break;
               for( tile_hpix = 0; tile_hpix < (int)m_tile_width; tile_hpix++ )
               {
                  image_hpix = tile_col * m_tile_width + tile_hpix;
                  if( image_hpix < min_hpix )
                     continue;
                  if( image_hpix > max_hpix )
                     break;
                  tile_index = tile_vpix * m_tile_width + tile_hpix;
                  subimage_hpix = image_hpix - min_hpix;
                  subimage_vpix = image_vpix - min_vpix;
                  subimage_index = subimage_vpix * subimage_width + subimage_hpix;
              ASSERT((subimage_index*3+2) < (width * height * 3));
                  img[subimage_index*3+0] = m_tile_red_array[tile_index];
                  img[subimage_index*3+1] = m_tile_green_array[tile_index];
                  img[subimage_index*3+2] = m_tile_blue_array[tile_index];
                  tile_index++;
               }
            }
         double percent = (double) tile_index / (double) tile_cnt;
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
        }
      }
   }
   else
   {
      // image is not tiled
      // select image type and call appropriate function
      switch( m_image_type )
      {
         case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
            goto FAIL;

         case GEOTIFF_IMAGE_TYPE_1BIT:
            if ( get_1bit_image_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
          if (m_samples_per_pixel == 3)
             rslt = get_24bit_rgb_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback );
          else
            rslt =  get_8bit_grayscale_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback );
          if (rslt != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
            if( get_16bit_grayscale_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_256_COLOR:
            if( get_8bit_palette_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
            if( get_24bit_rgb_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_48BIT_COLOR:
            if( get_48bit_rgb_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_32BIT_FLOAT:
          {
               float minval, maxval, range, pix_size;
               int k, size, width, height, x, y, pos;
               BYTE val;
               CString error_msg;
               CUtil util;
               float exag, dirx, diry, dirz, left, right, up;
               double brt;

               width = max_hpix - min_hpix + 1;
               height = max_vpix - min_vpix + 1;
               float *elev = (float*) malloc(width * height * sizeof(float));
               if (elev == NULL)
                  goto FAIL;
               rslt = get_32bit_elevation_data(min_hpix, min_vpix, width, height, elev, error_msg, callback);
               if (rslt != SUCCESS)
               {
                  free(elev);
                  goto FAIL;
               }
               pix_size = (float) m_pixel_size;
               if (pix_size <= 0.0)
                  pix_size = 5.0;
               size = width * height;
               minval = maxval = elev[0];
               for (k=0; k<size; k++)
               {
                  if (elev[k] < minval)
                     minval = elev[k];
                  if (elev[k] > maxval)
                     maxval = elev[k];
               }
               range = maxval - minval;
               if (range < 0.0)
               {
                  free(elev);
                  goto FAIL;
               }
#if 0
               for (k=0; k<size; k++)
               {
                  val = (BYTE) (elev[k] / range * 255.0);
                  img[k*3+0] = val;
                  img[k*3+1] = val;
                  img[k*3+2] = val;
               }
#else
               float fac;
               int ival, minival, maxival;
               dirx = (float) sqrt( 1.0/3.0 );    // From upper left.
               diry = dirx;
               dirz = -dirx;
               exag = 5.0;
               k = 0;
               x = y = 1;
               pos = (y * width) + x;
               left = elev[pos - 1];
               right = elev[pos];
               up = elev[pos - width];
               brt = util.calc_dot_prod(left, right, up, pix_size, pix_size, dirx, diry, dirz, exag);
               ival = (int) (brt * 255.0);
               minival = maxival = ival;
               for (y=0; y<height; y++)
               {
                  for (x=0; x<width; x++)
                  {
                     if (x==0 || y==0)
                     {
                        val = 0;
                        img[k*3+0] = val;
                        img[k*3+1] = val;
                        img[k*3+2] = val;
                        k++;
                        continue;
                     }

                     pos = (y * width) + x;
                     left = elev[pos - 1];
                     right = elev[pos];
                     up = elev[pos - width];
                     brt = util.calc_dot_prod(left, right, up, pix_size, pix_size, dirx, diry, dirz, exag);
                     ival = (int) (brt * 255.0);
                     if (ival < minival)
                        minival = ival;
                     if (ival > maxival)
                        maxival = ival;
                  }
               }
               range = (float) (maxival - minival);
               fac = (float) 255.0 / range;
               k = 0;
               for (y=0; y<height; y++)
               {
                  for (x=0; x<width; x++)
                  {
                     if (x==0 || y==0)
                     {
                        val = 0;
                        img[k*3+0] = val;
                        img[k*3+1] = val;
                        img[k*3+2] = val;
                        k++;
                        continue;
                     }

                     pos = (y * width) + x;
                     right = elev[pos];
                     if (right == -99)
                     {
                        img[k*3+0] = 255;
                        img[k*3+1] = 0;
                        img[k*3+2] = 0;
                        k++;
                        continue;
                     }
                     left = elev[pos - 1];
                     up = elev[pos - width];

                     brt = util.calc_dot_prod(left, right, up, 5.0, 5.0, dirx, diry, dirz, exag);
                     ival = (int) (brt * 255.0);
                     ival = (int) (((double) ival - minival) * fac);
                     if (ival > 255)
                        ival = 255;
                     if (ival < 0)
                        ival = 0;
                     val = (BYTE) ival;
                     img[k*3+0] = val;
                     img[k*3+1] = val;
                     img[k*3+2] = val;
                     k++;
                  }
               }
#endif
               free(elev);
               return SUCCESS;
          }
            break;

         default:                           // should always be one of above
            ASSERT( FALSE );
            goto FAIL;
      }
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}
// end of get_rgb_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_multiband_subimage( int min_hpix, int min_vpix, int width, int height, IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage
   // returns SUCCESS or FAILURE

   int size, j, tsize;
   int subimage_width;
   int rslt;
   int max_hpix, max_vpix;
   CUtil util;

   max_hpix = min_hpix + width - 1;
   max_vpix = min_vpix + height - 1;

   // return if no file loaded or there was an error loading the tiff file
//   if( !m_file_loaded || m_error )
   if ( !m_file_loaded )
      return FAILURE;

   // return error if image type is not supported
//   if( !m_image_type_supported )
//    return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
      return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
      return FAILURE;

   subimage_width = max_hpix - min_hpix + 1;

   size = width * height;
   tsize = size * m_samples_per_pixel;

   // set up bands
   m_image->m_num_bands = m_samples_per_pixel;
   if (m_image->m_band)
      delete [] m_image->m_band;
   m_image->m_band = new C_band[m_samples_per_pixel];
   m_image->m_band[0].m_label = "450-520nm Blue-Green";
   m_image->m_band[1].m_label = "520-600nm Green";
   m_image->m_band[2].m_label = "630-690nm Red";
   m_image->m_band[3].m_label = "760-900nm Near IR";
   for (j=0; j<m_image->m_num_bands; j++)
   {
      m_image->m_band[j].m_image_width = width;
      m_image->m_band[j].m_image_height = height;
      m_image->m_band[j].m_img16bit = new USHORT[ size ];

   }

   // check for tiled image
   if ( m_image_is_tiled )
   {
      m_new_error_description = "Multiband tiled images not supported";
      return FAILURE;  // we don't yet handle multiband tiled images
   }
   else
   {
      // image is not tiled
      if ( m_planar_configuration != GEOTIFF_PLANAR_FORMAT)
         rslt = get_multiband_chunky_subimage(min_hpix, min_vpix, max_hpix, max_vpix, callback);
      else
         rslt = get_multiband_planar_subimage(min_hpix, min_vpix, max_hpix, max_vpix, callback);
      if (rslt != SUCCESS)
      {
      return FAILURE;
      }
   }

   return SUCCESS;
}
// end of get_multiband_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_grayscale_subimage( int min_hpix, int min_vpix, int width, int height,
                                   unsigned char *img, IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage
   // returns SUCCESS or FAILURE

   int min_tile_row, min_tile_col, max_tile_row, max_tile_col;
   int tile_row, tile_col, tile;
   int image_hpix, image_vpix;
   int subimage_hpix, subimage_vpix, subimage_width, subimage_index;
   int tile_hpix, tile_vpix, tile_index, rslt, tile_cnt;
   int max_hpix, max_vpix, twidth, theight;
   CUtil util;
   unsigned char *tile_buf = NULL;

   if (m_image_type != GEOTIFF_IMAGE_TYPE_256_GRAYSCALE)
   {
      ASSERT(0);
      return FAILURE;
   }

   tile_index = 0;

   max_hpix = min_hpix + width - 1;
   max_vpix = min_vpix + height - 1;

   // return if no file loaded or there was an error loading the tiff file
//   if( !m_file_loaded || m_error )
   if ( !m_file_loaded )
   {
      m_new_error_description = "Image is not loaded";
      return FAILURE;
   }

   // return error if image type is not supported
//   if( !m_image_type_supported )
//    return FAILURE;

   twidth = width;
   theight = height;

   // return failure if subimage is invalid
   if ( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
   {
      m_new_error_description = "min_hpix is out of range";
      return FAILURE;
   }
   if ( max_hpix < 0 || max_hpix >= (int)m_image_width )
      twidth = m_image_width - min_hpix;
   if ( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
   {
      m_new_error_description = "min_vpix is out of range";
      return FAILURE;
   }
   if ( max_vpix < 0 || max_vpix >= (int)m_image_length )
      theight = m_image_length - min_vpix;

   max_hpix = min_hpix + twidth - 1;
   max_vpix = min_vpix + theight - 1;
   subimage_width = max_hpix - min_hpix + 1;

   FillMemory(img,width * height * sizeof(unsigned char),  0);

   // check for tiled image
   if ( m_image_is_tiled )
   {
      // image is tiled

      tile_buf = (unsigned char*) malloc(m_tile_width * m_tile_length * sizeof(unsigned char));
      if (tile_buf == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Memory allocation failure";
         return FAILURE;
      }


      // determine range of tile rows and columns in subimage
      min_tile_row = min_vpix / m_tile_length;
      max_tile_row = max_vpix / m_tile_length;
      min_tile_col = min_hpix / m_tile_width;
      max_tile_col = max_hpix / m_tile_width;

     tile_cnt = (max_tile_col - min_tile_col + 1) * (max_tile_row - min_tile_row + 1);

      // loop through tiles covering the subimage
      for ( tile_row = min_tile_row; tile_row <= max_tile_row; tile_row++ )
      {
         for ( tile_col = min_tile_col; tile_col <= max_tile_col; tile_col++ )
         {
            tile = m_num_tiles_across * tile_row + tile_col;

            rslt = get_8bit_grayscale_tile_as_8bit( tile, tile_buf );
            if ( rslt != SUCCESS )
               goto FAIL;

            // copy data from tile
            for ( tile_vpix = 0; tile_vpix < (int)m_tile_length; tile_vpix++ )
            {
               image_vpix = tile_row * m_tile_length + tile_vpix;
               if( image_vpix < min_vpix )
                  continue;
               if( image_vpix > max_vpix )
                  break;
               for( tile_hpix = 0; tile_hpix < (int)m_tile_width; tile_hpix++ )
               {
                  image_hpix = tile_col * m_tile_width + tile_hpix;
                  if( image_hpix < min_hpix )
                     continue;
                  if( image_hpix > max_hpix )
                     break;
                  tile_index = tile_vpix * m_tile_width + tile_hpix;
                  subimage_hpix = image_hpix - min_hpix;
                  subimage_vpix = image_vpix - min_vpix;
                  subimage_index = subimage_vpix * subimage_width + subimage_hpix;
              ASSERT((subimage_index*3+2) < (width * height * 3));
                  img[subimage_index] = tile_buf[tile_index];
                  tile_index++;
               }
            }
         double percent = (double) tile_index / (double) tile_cnt;
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
        }
      }
   }
   else
   {
      // image is not tiled
      // select image type and call appropriate function
      if ((width != twidth) || (height != theight))
      {
         int ipos, opos, x, y;

         unsigned char *buf = (unsigned char*) malloc(twidth * theight * sizeof(unsigned char));
         if (buf == NULL)
         {
            m_new_error_description = "Memory allocation failure";
            goto FAIL;
         }

         rslt = get_8bit_grayscale_as_8bit_subimage( min_hpix, min_vpix, max_hpix, max_vpix, buf, callback );
         if (rslt != SUCCESS )
         {
            m_new_error_description = "Operation canceled by user";
            free(buf);
            goto FAIL;
         }
         ipos = 0;
         for (y = 0; y < theight; y++)
         {
            for (x=0; x<twidth; x++)
            {
               opos = y*width + x;
               img[opos] = buf[ipos];
               ipos++;
            }
         }
         free(buf);
      }
      else
      {
         if ( get_8bit_grayscale_as_8bit_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
            goto FAIL;
      }

   }

   if (tile_buf != NULL)
      free(tile_buf);
   return SUCCESS;

FAIL:
   if (tile_buf != NULL)
      free(tile_buf);
   return FAILURE;
}
// end of get_8bit_grayscale_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_16bit_grayscale_subimage( int min_hpix, int min_vpix, int width, int height,
                                   unsigned short *img, IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage
   // returns SUCCESS or FAILURE

   int min_tile_row, min_tile_col, max_tile_row, max_tile_col;
   int tile_row, tile_col, tile;
   int image_hpix, image_vpix;
   int subimage_hpix, subimage_vpix, subimage_width, subimage_index;
   int tile_hpix, tile_vpix, tile_index, rslt, tile_cnt;
   int max_hpix, max_vpix, twidth, theight;
   CUtil util;
   unsigned short *tile_buf = NULL;

   if (m_image_type != GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE)
   {
      ASSERT(0);
      return FAILURE;
   }

   tile_index = 0;

   max_hpix = min_hpix + width - 1;
   max_vpix = min_vpix + height - 1;

   // return if no file loaded or there was an error loading the tiff file
//   if( !m_file_loaded || m_error )
   if ( !m_file_loaded )
   {
      m_new_error_description = "Image is not loaded";
      return FAILURE;
   }

   // return error if image type is not supported
//   if( !m_image_type_supported )
//    return FAILURE;

   twidth = width;
   theight = height;

   // return failure if subimage is invalid
   if ( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
   {
      m_new_error_description = "min_hpix is out of range";
      return FAILURE;
   }
   if ( max_hpix < 0 || max_hpix >= (int)m_image_width )
      twidth = m_image_width - min_hpix;
   if ( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
   {
      m_new_error_description = "min_vpix is out of range";
      return FAILURE;
   }
   if ( max_vpix < 0 || max_vpix >= (int)m_image_length )
      theight = m_image_length - min_vpix;

   max_hpix = min_hpix + twidth - 1;
   max_vpix = min_vpix + theight - 1;
   subimage_width = max_hpix - min_hpix + 1;

   FillMemory(img,width * height * sizeof(unsigned short),  0);

   // check for tiled image
   if ( m_image_is_tiled )
   {
      // image is tiled

      tile_buf = (unsigned short*) malloc(m_tile_width * m_tile_length * sizeof(unsigned short));
      if (tile_buf == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Memory allocation failure";
         return FAILURE;
      }


      // determine range of tile rows and columns in subimage
      min_tile_row = min_vpix / m_tile_length;
      max_tile_row = max_vpix / m_tile_length;
      min_tile_col = min_hpix / m_tile_width;
      max_tile_col = max_hpix / m_tile_width;

     tile_cnt = (max_tile_col - min_tile_col + 1) * (max_tile_row - min_tile_row + 1);

      // loop through tiles covering the subimage
      for ( tile_row = min_tile_row; tile_row <= max_tile_row; tile_row++ )
      {
         for ( tile_col = min_tile_col; tile_col <= max_tile_col; tile_col++ )
         {
            tile = m_num_tiles_across * tile_row + tile_col;

            rslt = get_16bit_grayscale_tile_as_16bit( tile, tile_buf );
            if ( rslt != SUCCESS )
               goto FAIL;

            // copy data from tile
            for ( tile_vpix = 0; tile_vpix < (int)m_tile_length; tile_vpix++ )
            {
               image_vpix = tile_row * m_tile_length + tile_vpix;
               if( image_vpix < min_vpix )
                  continue;
               if( image_vpix > max_vpix )
                  break;
               for( tile_hpix = 0; tile_hpix < (int)m_tile_width; tile_hpix++ )
               {
                  image_hpix = tile_col * m_tile_width + tile_hpix;
                  if( image_hpix < min_hpix )
                     continue;
                  if( image_hpix > max_hpix )
                     break;
                  tile_index = tile_vpix * m_tile_width + tile_hpix;
                  subimage_hpix = image_hpix - min_hpix;
                  subimage_vpix = image_vpix - min_vpix;
                  subimage_index = subimage_vpix * subimage_width + subimage_hpix;
              ASSERT((subimage_index*3+2) < (width * height * 3));
                  img[subimage_index] = tile_buf[tile_index];
                  tile_index++;
               }
            }
         double percent = (double) tile_index / (double) tile_cnt;
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
        }
      }
   }
   else
   {
      // image is not tiled
      // select image type and call appropriate function
      if ((width != twidth) || (height != theight))
      {
         int ipos, opos, x, y;

         unsigned short *buf = (unsigned short*) malloc(twidth * theight * sizeof(unsigned short));
         if (buf == NULL)
         {
            m_new_error_description = "Memory allocation failure";
            goto FAIL;
         }

         rslt = get_16bit_grayscale_as_16bit_subimage( min_hpix, min_vpix, max_hpix, max_vpix, buf, callback );
         if (rslt != SUCCESS )
         {
            m_new_error_description = "Operation canceled by user";
            free(buf);
            goto FAIL;
         }
         ipos = 0;
         for (y = 0; y < theight; y++)
         {
            for (x=0; x<twidth; x++)
            {
               opos = y*width + x;
               img[opos] = buf[ipos];
               ipos++;
            }
         }
         free(buf);
      }
      else
      {
         if ( get_16bit_grayscale_as_16bit_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
            goto FAIL;
      }

   }

   if (tile_buf != NULL)
      free(tile_buf);
   return SUCCESS;

FAIL:
   if (tile_buf != NULL)
      free(tile_buf);
   return FAILURE;
}
// end of get_16bit_grayscale_subimage

// ********************************************************************************************
// *****************************************************************

int CGeoTiff::get_palette_subimage( int min_hpix, int min_vpix, int max_hpix,
                                    int max_vpix,
                                    CColorQuantizer &color_quantizer,
                                    unsigned char *indices, IImageLibCallback *callback)
{
   // this function returns the color indices for each pixel in the specified
   // subimage
   // returns SUCCESS or FAILURE

   int min_tile_row, min_tile_col, max_tile_row, max_tile_col;
   int tile_row, tile_col, tile, rslt;
   int image_hpix, image_vpix;
   int subimage_hpix, subimage_vpix, subimage_width, subimage_index;
   int tile_hpix, tile_vpix, tile_index;

   // return failure if no file loaded or there was an error loading the tiff
   // file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // return error if image type is not supported
   if( !m_image_type_supported )
      return FAILURE;

   // return failure if color quantizer is not defined
   if( !color_quantizer.m_defined )
      return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
      return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
      return FAILURE;

   subimage_width = max_hpix - min_hpix + 1;

   // check for tiled image
   if( m_image_is_tiled )
   {
      // image is tiled
      // determine range of tile rows and columns in subimage
      min_tile_row = min_vpix / m_tile_length;
      max_tile_row = max_vpix / m_tile_length;
      min_tile_col = min_hpix / m_tile_width;
      max_tile_col = max_hpix / m_tile_width;

      // loop through tiles covering the subimage
      for( tile_row = min_tile_row; tile_row <= max_tile_row; tile_row++ )
      {
         for( tile_col = min_tile_col; tile_col <= max_tile_col; tile_col++ )
         {
            tile = m_num_tiles_across * tile_row + tile_col;

            if( get_tile_as_palette_image( tile, color_quantizer,
               m_tile_color_indices ) != SUCCESS )
               goto FAIL;

            // copy data from tile
            for( tile_vpix = 0; tile_vpix < (int)m_tile_length; tile_vpix++ )
            {
               image_vpix = tile_row * m_tile_length + tile_vpix;
               if( image_vpix < min_vpix )
                  continue;
               if( image_vpix > max_vpix )
                  break;
               for( tile_hpix = 0; tile_hpix < (int)m_tile_width; tile_hpix++ )
               {
                  image_hpix = tile_col * m_tile_width + tile_hpix;
                  if( image_hpix < min_hpix )
                     continue;
                  if( image_hpix > max_hpix )
                     break;
                  tile_index = tile_vpix * m_tile_width + tile_hpix;
                  subimage_hpix = image_hpix - min_hpix;
                  subimage_vpix = image_vpix - min_vpix;
                  subimage_index = subimage_vpix * subimage_width + subimage_hpix;
                  indices[subimage_index] = m_tile_color_indices[tile_index];
                  tile_index++;
               }
            }
         }
      }
   }
   else
   {
      // image is not tiled
      // select image type and call appropriate function
      switch( m_image_type )
      {
         case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
            goto FAIL;

         case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
          if (m_samples_per_pixel == 3)
            rslt = get_24bit_rgb_as_palette_subimage( min_hpix, min_vpix, max_hpix,
                        max_vpix, color_quantizer, indices, callback );
          else
             rslt = get_8bit_grayscale_as_palette_subimage( min_hpix, min_vpix,
                       max_hpix, max_vpix, color_quantizer, indices );
         if (rslt != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
            if( get_16bit_grayscale_as_palette_subimage( min_hpix, min_vpix,
                max_hpix, max_vpix, color_quantizer, indices ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_256_COLOR:
            if( get_8bit_palette_as_palette_subimage( min_hpix, min_vpix, max_hpix,
                max_vpix, color_quantizer, indices ) != SUCCESS )
                goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
            if( get_24bit_rgb_as_palette_subimage( min_hpix, min_vpix, max_hpix,
                max_vpix, color_quantizer, indices, callback ) != SUCCESS )
                goto FAIL;
            break;

         default:                           // should always be one of above
            ASSERT( FALSE );
            goto FAIL;
      }
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}
// end of get_palette_subimage

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_8bit_grayscale_as_palette_subimage( int min_hpix,
      int min_vpix, int max_hpix, int max_vpix,
      CColorQuantizer &color_quantizer, unsigned char *indices )
{
   // this function returns the palette indices for the subimage pixels in the
   // argument indices based on the values in the color_quantizer argument
   // returns SUCCESS or FAILURE

   int i, j, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   int diff, width, height, num_pix;
   unsigned int strip_offset, strip_byte_count;
   unsigned int color_value;
   unsigned char red, green, blue;
   unsigned char *strip_data = NULL;
   unsigned char *decompressed_strip_data = NULL;
   double value;
   unsigned char cross_reference[256];
   BYTE color_ndx[256];
#ifdef FULL_STRIP_READ
   BOOL bFullStripRead;
#endif

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   width = max_hpix - min_hpix + 1;
   height = max_vpix - min_vpix + 1;
   num_pix = width * height;

   // allocate data for strips
   strip_data = NULL;
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );

   // if compressed, allocate memory for a decompressed strip
   decompressed_strip_data = NULL;
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data =
         (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip *
                           m_samples_per_pixel );
   }

   // if necessary, create cross-reference table
   // by finding nearest grayscale entry
   if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
   {
      for( i = 0; i < 256; i++ )
      {
         cross_reference[i] = 0;
         diff = 256;
         for( j = 0; j < color_quantizer.m_num_colors; j++ )
         {
            red = color_quantizer.m_red[j];
            green = color_quantizer.m_green[j];
            blue = color_quantizer.m_blue[j];
            if( green == red && blue == red )
            {
               if( abs( i - red ) < diff )
               {
                  cross_reference[i] = j;
                  diff = abs( i - red );
                  if( diff == 0 )
                     break;
               }
            }
         }
      }
   }

  if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
  {
     for (i=0; i<256; i++)
     {
       value = (double)(i) * (color_quantizer.m_num_colors-1) /
             255.0 + 0.5;
       color_ndx[i] = (BYTE) floor( value );
     }
  }
  else
  {
     for (i=0; i<256; i++)
     {

       if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
       {
         color_ndx[i] = cross_reference[i];
       }
     }
  }

#ifdef FULL_STRIP_READ
   bFullStripRead =
      // Full strip read if compressed data
      m_compression_scheme != GEOTIFF_NO_COMPRESSION  // Full strip if compressed

      // Or not too much scrap data (2x the system page size
      || ( m_max_strip_byte_count / m_rows_per_strip ) < width + ( 2 * dwSystemPageSize );
#endif

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip

   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

#ifdef FULL_STRIP_READ
      if ( bFullStripRead )         // If want full strip
#else                               // Partial strip read for non-compressed data
      if ( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
#endif
      {
         // Read full strip
         if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
           goto FAIL;
      }

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // copy strip data to pixel array
            for( j = 0; j < (int)m_rows_per_strip; j++ )
            {
               if ( vpix >= min_vpix && vpix <= max_vpix )
               {
#ifdef FULL_STRIP_READ
                  if ( bFullStripRead )
                     i = min_hpix + ( j * m_image_width );
                  else
                  {
                     if ( gtfRead( strip_offset + min_hpix + ( j * m_image_width ),
                        strip_data, width ) != SUCCESS )
                        goto FAIL;

                     i = 0;   // Start at beginning of buffer
                  }

                  INT iEnd = i + width;
                  for ( ; i < iEnd; i++ )
                  {
#else // Partial strip read
                  if ( gtfRead( strip_offset + min_hpix + ( j * m_image_width ),
                     strip_data, width ) != SUCCESS )
                     goto FAIL;

                  hpix = min_hpix;
                  for (i=0; i<width; i++)
                  {
                     if ( hpix >= min_hpix && hpix <= max_hpix )
                     {
#endif
                        red = strip_data[i];
                        // check for reversed grayscale
                        if ( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                           red = 255 - red;
                        if ( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
                        {
                           indices[subimage_pixel_index] = color_ndx[red];
                        }
                        else
                        {
                           if ( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                              indices[subimage_pixel_index] = cross_reference[red];
                           else
                           {
                              green = blue = red;
                              color_value = (red >> 3) << 10;
                              color_value |= (green >> 3) << 5;
                              color_value |= (blue >> 3);
                              indices[subimage_pixel_index] =
                              color_quantizer.m_histogram_indices[color_value];
                           }
                        }

#ifndef FULL_STRIP_READ // Partial strip read
                        if (subimage_pixel_index < num_pix-1)
                           subimage_pixel_index++;
                     }  // hpix in range
                     hpix++;
                     if ( hpix == (int)m_image_width )
                     {
                        hpix = 0;
//                      vpix++;
                        if ( vpix > max_vpix )
                           goto DONE;
                     }
                     ipixel++;
                  }  // Column loop
#else // Full strip read
                     subimage_pixel_index++;
                  }  // Column loop
#endif
               }  // vpix in range
               vpix++;
               if( vpix > max_vpix )
                  goto DONE;
            }  // Rows in strip loop
            break;
         case GEOTIFF_CCITT_1D:
            goto FAIL;                           // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                  // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
           num_remaining_bytes =
              (m_num_pixels - ipixel) * m_samples_per_pixel;
           decompressed_size =
              __min( (int)(m_image_width * m_rows_per_strip *
              m_samples_per_pixel), num_remaining_bytes );
           if( decompress_strip( (int)strip_byte_count, strip_data,
               decompressed_size, decompressed_strip_data ) != SUCCESS )
               goto FAIL;
            for( i = 0; (unsigned int) i < decompressed_size; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
               {
                  red = decompressed_strip_data[i];
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = 255 - red;
                  if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
                  {
                 indices[subimage_pixel_index] = color_ndx[red];
                  }
                  else
                  {
                     if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                        indices[subimage_pixel_index] =
                        cross_reference[red];
                     else
                     {
                        green = blue = red;
                        color_value = (red >> 3) << 10;
                        color_value |= (green >> 3) << 5;
                        color_value |= (blue >> 3);
                        indices[subimage_pixel_index] =
                           color_quantizer.m_histogram_indices[color_value];
                     }
                  }
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
                 goto DONE;
               }
               ipixel++;
            }
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }
   }

DONE:

   MEM_free( strip_data );
   strip_data = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
// end of get_8bit_grayscale_as_palette_subimage

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_16bit_grayscale_as_palette_subimage( int min_hpix,
      int min_vpix, int max_hpix, int max_vpix,
      CColorQuantizer &color_quantizer, unsigned char *indices )
{
   // this function returns the palette indices for the subimage pixels in the
   // argument indices based on the values in the color_quantizer argument
   // returns SUCCESS or FAILURE

   int i, j, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   int diff;
   unsigned int strip_offset, strip_byte_count;
   unsigned int color_value;
   unsigned char red, green, blue;
   unsigned char *strip_data = NULL, *decompressed_strip_data = NULL;
   unsigned char byte1, byte2;
   double value;
   unsigned char cross_reference[256];
   unsigned short *strip_16bit_data;
   double fraction;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   // allocate data for strips
   strip_data = NULL;
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );

   // if compressed, allocate memory for a decompressed strip
   decompressed_strip_data = NULL;
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data =
         (unsigned char*)MEM_malloc( 2 * m_image_width * m_rows_per_strip *
                           m_samples_per_pixel );
   }

   // if necessary, create cross-reference table
   // by finding nearest grayscale entry
   if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
   {
      for( i = 0; i < 256; i++ )
      {
         cross_reference[i] = 0;
         diff = 256;
         for( j = 0; j < color_quantizer.m_num_colors; j++ )
         {
            red = color_quantizer.m_red[j];
            green = color_quantizer.m_green[j];
            blue = color_quantizer.m_blue[j];
            if( green == red && blue == red )
            {
               if( abs( i - red ) < diff )
               {
                  cross_reference[i] = j;
                  diff = abs( i - red );
                  if( diff == 0 )
                     break;
               }
            }
         }
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
        goto FAIL;

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // copy strip data to pixel array
            // swap byte order if necessary
            if( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               for( i = 0; i < (int)strip_byte_count; i += 2 )
               {
                  byte1 = strip_data[i];
                  byte2 = strip_data[i+1];
                  strip_data[i] = byte2;
                  strip_data[i+1] = byte1;
               }
            }
            strip_16bit_data = (unsigned short*)strip_data;
            if( m_rows_per_strip == 1 )
            {
               i = min_hpix;
               for( hpix = min_hpix; hpix <= max_hpix; hpix++ )
               {
                  fraction = (double)(strip_16bit_data[i]-m_min_sample_value)/
                     (m_max_sample_value-m_min_sample_value);
                  if( fraction < 0.0 )
                     fraction = 0.0;
                  if( fraction > 1.0 )
                     fraction = 1.0;
                  red = (unsigned char)(255*fraction+0.5);
                  i++;
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = 255 - red;
                  if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
                  {
                     value = (double)(red) * (color_quantizer.m_num_colors-1) /
                             255.0 + 0.5;
                     indices[subimage_pixel_index] =
                        (unsigned char) floor( value );
                  }
                  else
                  {
                     if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                        indices[subimage_pixel_index] =
                        cross_reference[red];
                     else
                     {
                        green = blue = red;
                        color_value = (red >> 3) << 10;
                        color_value |= (green >> 3) << 5;
                        color_value |= (blue >> 3);
                        indices[subimage_pixel_index] =
                           color_quantizer.m_histogram_indices[color_value];
                     }
                  }
                  subimage_pixel_index++;
               }
            }
            else
            {
               for( i = 0; i < (int)strip_byte_count/2; i++ )
               {
                  if( hpix >= min_hpix && hpix <= max_hpix &&
                      vpix >= min_vpix && vpix <= max_vpix )
                  {
                     fraction = (double)(strip_16bit_data[i]-m_min_sample_value)/
                        (m_max_sample_value-m_min_sample_value);
                     if( fraction < 0.0 )
                        fraction = 0.0;
                     if( fraction > 1.0 )
                        fraction = 1.0;
                     red = (unsigned char)(255*fraction+0.5);
                     // check for reversed grayscale
                     if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                        red = 255 - red;
                     if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
                     {
                        value = (double)(red) * (color_quantizer.m_num_colors-1) /
                                255.0 + 0.5;
                        indices[subimage_pixel_index] =
                           (unsigned char) floor( value );
                     }
                     else
                     {
                        if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                           indices[subimage_pixel_index] =
                           cross_reference[red];
                        else
                        {
                           green = blue = red;
                           color_value = (red >> 3) << 10;
                           color_value |= (green >> 3) << 5;
                           color_value |= (blue >> 3);
                           indices[subimage_pixel_index] =
                              color_quantizer.m_histogram_indices[color_value];
                        }
                     }
                     subimage_pixel_index++;
                  }
                  hpix++;
                  if( hpix == (int)m_image_width )
                  {
                     hpix = 0;
                     vpix++;
                     if( vpix > max_vpix )
                   goto DONE;
                  }
                  ipixel++;
               }
            }
            break;
         case GEOTIFF_CCITT_1D:
            goto FAIL;                           // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                  // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            num_remaining_bytes =
               2 * (m_num_pixels - ipixel) * m_samples_per_pixel;
            decompressed_size =
               __min( (int)(2 * m_image_width * m_rows_per_strip *
               m_samples_per_pixel), num_remaining_bytes );
            if( decompress_strip( (int)strip_byte_count, strip_data,
                decompressed_size, decompressed_strip_data ) != SUCCESS )
                goto FAIL;
            // swap byte order if necessary
            if( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               for( i = 0; i < (int)decompressed_size; i += 2 )
               {
                  byte1 = decompressed_strip_data[i];
                  byte2 = decompressed_strip_data[i+1];
                  decompressed_strip_data[i] = byte2;
                  decompressed_strip_data[i+1] = byte1;
               }
            }
            strip_16bit_data = (unsigned short*)decompressed_strip_data;
            for( i = 0; (unsigned int) i < decompressed_size/2; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
               {
                  fraction = (double)(strip_16bit_data[i]-m_min_sample_value)/
                     (m_max_sample_value-m_min_sample_value);
                  if( fraction < 0.0 )
                     fraction = 0.0;
                  if( fraction > 1.0 )
                     fraction = 1.0;
                  red = (unsigned char)(255*fraction+0.5);
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = 255 - red;
                  if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
                  {
                     value = (double)(red) * (color_quantizer.m_num_colors-1) /
                             255.0 + 0.5;
                     indices[subimage_pixel_index] =
                        (unsigned char) floor( value );
                  }
                  else
                  {
                     if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                        indices[subimage_pixel_index] =
                        cross_reference[red];
                     else
                     {
                        green = blue = red;
                        color_value = (red >> 3) << 10;
                        color_value |= (green >> 3) << 5;
                        color_value |= (blue >> 3);
                        indices[subimage_pixel_index] =
                           color_quantizer.m_histogram_indices[color_value];
                     }
                  }
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
                 goto DONE;
               }
               ipixel++;
            }
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
   MEM_free( strip_data );
   strip_data = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
// end of get_16bit_grayscale_as_palette_subimage

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_24bit_rgb_as_palette_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                    CColorQuantizer &color_quantizer, unsigned char *indices,
                                    IImageLibCallback *callback)
{
   // this function returns the palette indices for the subimage pixels in the
   // argument indices based on the values in the color_quantizer argument
   // returns SUCCESS or FAILURE

   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int ibyte, decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip, rslt;
   unsigned int color_value;
   unsigned char red, green, blue;
   unsigned int strip_offset, strip_byte_count, psize;
   unsigned char *strip_data = NULL, *decompressed_strip_data = NULL;

   if ( m_planar_configuration == GEOTIFF_PLANAR_FORMAT)
   {
      rslt = get_24bit_planar_rgb_as_palette_subimage(min_hpix, min_vpix, max_hpix, max_vpix, color_quantizer, indices, callback);
      return rslt;
   }

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   // allocate data for strips
   strip_data = NULL;
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );

   // if compressed, allocate memory for a decompressed strip
   decompressed_strip_data = NULL;
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data =
         (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip *
                           m_samples_per_pixel );
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
        goto FAIL;

      psize = strip_byte_count / 3;

      // select planar configuration - either chunky (rgbrgbrgb) or
      // planar (rrrgggbbb) format
      switch( m_planar_configuration )
      {
         case GEOTIFF_PLANAR_FORMAT:
            switch( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
                  ibyte = 0;
                  // copy strip data to pixel array
                  while( ibyte < (int)psize)
                  {
                     red = strip_data[ibyte ];
                     ibyte++;
                     green = strip_data[ibyte + psize];
                     ibyte++;
                     blue = strip_data[ibyte + psize+psize];
                     ibyte++;
                     if( hpix >= min_hpix && hpix <= max_hpix &&
                         vpix >= min_vpix && vpix <= max_vpix )
                     {
                        color_value = (red >> 3) << 10;
                        color_value |= (green >> 3) << 5;
                        color_value |= (blue >> 3);
                        indices[subimage_pixel_index] =
                           color_quantizer.m_histogram_indices[color_value];
                        subimage_pixel_index++;
                     }
                     hpix++;
                     if( hpix == (int)m_image_width )
                     {
                        hpix = 0;
                        vpix++;
                        if( vpix > max_vpix )
                     goto DONE;
                     }
                     ipixel++;
                  }
                  break;
               case GEOTIFF_CCITT_1D:
                  goto FAIL;                     // not yet supported
               case GEOTIFF_LZW:                 // LZW compression scheme
               case GEOTIFF_PACKBITS:            // PackBits compression scheme
                  // decompress data
                 num_remaining_bytes = (m_num_pixels - ipixel) * m_samples_per_pixel;
                 decompressed_size = __min( (int)(m_image_width * m_rows_per_strip * m_samples_per_pixel), num_remaining_bytes );
                 if ( decompress_strip( (int)strip_byte_count, strip_data, decompressed_size, decompressed_strip_data ) != SUCCESS )
                goto FAIL;
                  ibyte = 0;
              psize = decompressed_size / 3;
                  while( ibyte < (int)psize )
                  {
                     red = decompressed_strip_data[ibyte];
                     ibyte++;
                     green = decompressed_strip_data[ibyte+psize];
                     ibyte++;
                     blue = decompressed_strip_data[ibyte+psize+psize];
                     ibyte++;
                     if( hpix >= min_hpix && hpix <= max_hpix &&
                         vpix >= min_vpix && vpix <= max_vpix )
                     {
                        color_value = (red >> 3) << 10;
                        color_value |= (green >> 3) << 5;
                        color_value |= (blue >> 3);
                        indices[subimage_pixel_index] = color_quantizer.m_histogram_indices[color_value];
                        subimage_pixel_index++;
                     }
                     hpix++;
                     if( hpix == (int)m_image_width )
                     {
                        hpix = 0;
                        vpix++;
                        if( vpix > max_vpix )
                     goto DONE;
                     }
                     ipixel++;
                  }
                  break;
               default:
                  ASSERT( FALSE );
                  goto FAIL;
            }
            break;

         case GEOTIFF_CHUNKY_FORMAT:
            switch( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
                  ibyte = 0;
                  // copy strip data to pixel array
                  while( ibyte < (int)strip_byte_count )
                  {
                     red = strip_data[ibyte];
                     ibyte++;
                     green = strip_data[ibyte];
                     ibyte++;
                     blue = strip_data[ibyte];
                     ibyte++;
                     if( hpix >= min_hpix && hpix <= max_hpix &&
                         vpix >= min_vpix && vpix <= max_vpix )
                     {
                        color_value = (red >> 3) << 10;
                        color_value |= (green >> 3) << 5;
                        color_value |= (blue >> 3);
                        indices[subimage_pixel_index] =
                           color_quantizer.m_histogram_indices[color_value];
                        subimage_pixel_index++;
                     }
                     hpix++;
                     if( hpix == (int)m_image_width )
                     {
                        hpix = 0;
                        vpix++;
                        if( vpix > max_vpix )
                     goto DONE;
                     }
                     ipixel++;
                  }
                  break;
               case GEOTIFF_CCITT_1D:
                  goto FAIL;                     // not yet supported
               case GEOTIFF_LZW:                 // LZW compression scheme
               case GEOTIFF_PACKBITS:            // PackBits compression scheme
                  // decompress data
                 num_remaining_bytes = (m_num_pixels - ipixel) * m_samples_per_pixel;
                 decompressed_size = __min( (int)(m_image_width * m_rows_per_strip * m_samples_per_pixel), num_remaining_bytes );
                 if ( decompress_strip( (int)strip_byte_count, strip_data, decompressed_size, decompressed_strip_data ) != SUCCESS )
                goto FAIL;
                  ibyte = 0;
                  while( ibyte < (int)decompressed_size )
                  {
                     red = decompressed_strip_data[ibyte];
                     ibyte++;
                     green = decompressed_strip_data[ibyte];
                     ibyte++;
                     blue = decompressed_strip_data[ibyte];
                     ibyte++;
                     if( hpix >= min_hpix && hpix <= max_hpix &&
                         vpix >= min_vpix && vpix <= max_vpix )
                     {
                        color_value = (red >> 3) << 10;
                        color_value |= (green >> 3) << 5;
                        color_value |= (blue >> 3);
                        indices[subimage_pixel_index] = color_quantizer.m_histogram_indices[color_value];
                        subimage_pixel_index++;
                     }
                     hpix++;
                     if( hpix == (int)m_image_width )
                     {
                        hpix = 0;
                        vpix++;
                        if( vpix > max_vpix )
                     goto DONE;
                     }
                     ipixel++;
                  }
                  break;
               default:
                  ASSERT( FALSE );
                  goto FAIL;
            }
            break;

         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
   MEM_free( strip_data );
   strip_data = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
// end of get_24bit_rgb_as_palette_subimage

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_24bit_planar_rgb_as_palette_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                          CColorQuantizer &color_quantizer, unsigned char *indices,
                                          IImageLibCallback *callback)
{
   // this function returns the palette indices for the subimage pixels in the
   // argument indices based on the values in the color_quantizer argument
   // returns SUCCESS or FAILURE

   unsigned int color_value;
   unsigned char red, green, blue;
   int size, rslt, k;
   BYTE *img;

   if ( m_planar_configuration != GEOTIFF_PLANAR_FORMAT)
   {
      ASSERT(0);
      m_new_error_description = "Function is only for planar images";
      return FAILURE;
   }

   size = (max_hpix - min_hpix + 1) * (max_vpix - min_vpix +1);

   // allocate memory for rgb image buffer
   img = (BYTE*) malloc(size * 3);
   if (img == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      return FAILURE;
   }

   rslt = get_24bit_planar_rgb_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
   if (rslt != SUCCESS)
   {
      free(img);
      return rslt;
   }

   for (k=0; k<size; k++)
   {
      red   = img[k*3+0];
      green = img[k*3+1];
      blue  = img[k*3+2];
        color_value = (red >> 3) << 10;
        color_value |= (green >> 3) << 5;
        color_value |= (blue >> 3);
        indices[k] = color_quantizer.m_histogram_indices[color_value];
   }

   free(img);

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_8bit_palette_as_palette_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, CColorQuantizer &color_quantizer,
      unsigned char *indices )
{
   // this function returns the palette indices for the subimage pixels in the
   // argument indices based on the values in the color_quantizer argument
   // returns SUCCESS or FAILURE

   int i, j, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int ibyte, decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int color_value;
   unsigned char red, green, blue;
   unsigned int strip_offset, strip_byte_count, palette_index;
   unsigned char *strip_data = NULL, *decompressed_strip_data = NULL;
   unsigned char cross_reference[256];

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   // allocate data for strips
   strip_data = NULL;
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );

   // if compressed, allocate memory for a decompressed strip
   decompressed_strip_data = NULL;
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data =
         (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip *
                           m_samples_per_pixel );
   }

   // if necessary, create cross-reference table
   if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
   {
      for( i = 0; i < 256; i++ )
      {
         cross_reference[i] = 0;
         for( j = 0; j < color_quantizer.m_num_colors; j++ )
         {
            if( color_quantizer.m_red[j] == m_red_color_map[i]/256 &&
                color_quantizer.m_green[j] == m_green_color_map[i]/256 &&
                color_quantizer.m_blue[j] == m_blue_color_map[i]/256 )
            {
               cross_reference[i] = j;
               break;
            }
         }
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
         goto FAIL;

      // select planar configuration - either chunky (rgbrgbrgb) or
      // planar (rrrgggbbb) format
      switch( m_planar_configuration )
      {
         case GEOTIFF_CHUNKY_FORMAT:
            switch( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
                  if( m_rows_per_strip == 1 )
                  {
                     ibyte = min_hpix;
                     for( hpix = min_hpix; hpix <= max_hpix; hpix++ )
                     {
                        palette_index = strip_data[ibyte];
                        if( (int)palette_index >= m_num_color_map_values )
                           goto FAIL;
                        ibyte++;
                        if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                           indices[subimage_pixel_index] =
                           cross_reference[palette_index];
                        else
                        {
                           red = (unsigned char)
                              (m_red_color_map[palette_index]/256);
                           green = (unsigned char)
                              (m_green_color_map[palette_index]/256);
                           blue = (unsigned char)
                              (m_blue_color_map[palette_index]/256);
                           color_value = (red >> 3) << 10;
                           color_value |= (green >> 3) << 5;
                           color_value |= (blue >> 3);
                           indices[subimage_pixel_index] =
                              color_quantizer.m_histogram_indices[color_value];
                        }
                        subimage_pixel_index++;
                     }
                  }
                  else
                  {
                     ibyte = 0;
                     while( ibyte < (int)strip_byte_count )
                     {
                        palette_index = strip_data[ibyte];
                        if( (int)palette_index >= m_num_color_map_values )
                           goto FAIL;
                        ibyte++;
                        if( hpix >= min_hpix && hpix <= max_hpix &&
                            vpix >= min_vpix && vpix <= max_vpix )
                        {
                           if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                              indices[subimage_pixel_index] =
                              cross_reference[palette_index];
                           else
                           {
                              red = (unsigned char)
                                 (m_red_color_map[palette_index]/256);
                              green = (unsigned char)
                                 (m_green_color_map[palette_index]/256);
                              blue = (unsigned char)
                                 (m_blue_color_map[palette_index]/256);
                              color_value = (red >> 3) << 10;
                              color_value |= (green >> 3) << 5;
                              color_value |= (blue >> 3);
                              indices[subimage_pixel_index] =
                                 color_quantizer.m_histogram_indices[color_value];
                           }
                           subimage_pixel_index++;
                        }
                        hpix++;
                        if( hpix == (int)m_image_width )
                        {
                           hpix = 0;
                           vpix++;
                           if( vpix > max_vpix )
                        goto DONE;
                        }
                        ipixel++;
                     }
                  }
                  break;
               case GEOTIFF_CCITT_1D:
                  goto FAIL;                     // not yet supported
               case GEOTIFF_LZW:                 // LZW compression scheme
               case GEOTIFF_PACKBITS:            // PackBits compression scheme
                  // decompress data
                  num_remaining_bytes =
                     (m_num_pixels - ipixel) * m_samples_per_pixel;
                  decompressed_size =
                     __min( (int)(m_image_width * m_rows_per_strip *
                     m_samples_per_pixel), num_remaining_bytes );
                  if( decompress_strip( (int)strip_byte_count, strip_data,
                      decompressed_size, decompressed_strip_data ) != SUCCESS )
                 goto FAIL;

                  if( m_rows_per_strip == 1 )
                  {
                     ibyte = min_hpix;
                     for( hpix = min_hpix; hpix <= max_hpix; hpix++ )
                     {
                        palette_index = decompressed_strip_data[ibyte];
                        if( (int)palette_index >= m_num_color_map_values )
                           goto FAIL;
                        ibyte++;
                        if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                           indices[subimage_pixel_index] =
                           cross_reference[palette_index];
                        else
                        {
                           red = (unsigned char)
                              (m_red_color_map[palette_index]/256);
                           green = (unsigned char)
                              (m_green_color_map[palette_index]/256);
                           blue = (unsigned char)
                              (m_blue_color_map[palette_index]/256);
                           color_value = (red >> 3) << 10;
                           color_value |= (green >> 3) << 5;
                           color_value |= (blue >> 3);
                           indices[subimage_pixel_index] =
                              color_quantizer.m_histogram_indices[color_value];
                        }
                        subimage_pixel_index++;
                     }
                  }
                  else
                  {
                     ibyte = 0;
                     while( ibyte < decompressed_size )
                     {
                   if( hpix <= max_hpix )
                   {
                     palette_index = decompressed_strip_data[ibyte];
                     if ( (int)palette_index >= m_num_color_map_values )
                        goto FAIL;

                           if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                              indices[subimage_pixel_index] =
                              cross_reference[palette_index];
                           else
                           {
                              red = (unsigned char)
                                 (m_red_color_map[palette_index]/256);
                              green = (unsigned char)
                                 (m_green_color_map[palette_index]/256);
                              blue = (unsigned char)
                                 (m_blue_color_map[palette_index]/256);
                              color_value = (red >> 3) << 10;
                              color_value |= (green >> 3) << 5;
                              color_value |= (blue >> 3);
                              indices[subimage_pixel_index] =
                                 color_quantizer.m_histogram_indices[color_value];
                           }
                           subimage_pixel_index++;
                     ibyte++;
                     hpix++;
                        }
                        hpix++;
                        if( hpix == (int)m_image_width )
                        {
                           hpix = 0;
                           vpix++;
                     ipixel += m_image_width;
                     ibyte += (m_image_width - max_hpix - 1) + min_hpix; // jump to end of row + jump to start of next row
                           if( vpix > max_vpix )
                        goto DONE;
                        }
                     }
                  }
                  break;
               default:
                  ASSERT( FALSE );
                  goto FAIL;
            }
            break;

         case GEOTIFF_PLANAR_FORMAT:
            goto FAIL;                           // not yet supported

         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
   MEM_free( strip_data );
   strip_data = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_tile_as_palette_image( int itile, CColorQuantizer &color_quantizer,
   unsigned char *color_indices )
{
   // this function returns a tile as a palette image using the specified color
   // quantizer object

   int rslt;

   // select image type and call appropriate function
   switch( m_image_type )
   {
      case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
         return FAILURE;

      case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
        if (m_samples_per_pixel == 3)
         rslt = get_24bit_rgb_tile_as_palette_image( itile, color_quantizer, color_indices );
        else
         rslt = get_8bit_grayscale_tile_as_palette_image( itile, color_quantizer, color_indices );
        if (rslt != SUCCESS )
            return FAILURE;
         break;

      case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
        if( get_16bit_grayscale_tile_as_palette_image( itile, color_quantizer,
           color_indices ) != SUCCESS )
           return FAILURE;
         break;

      case GEOTIFF_IMAGE_TYPE_256_COLOR:
        if( get_8bit_palette_tile_as_palette_image( itile, color_quantizer,
           color_indices ) != SUCCESS )
           return FAILURE;
         break;

      case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
         if( get_24bit_rgb_tile_as_palette_image( itile, color_quantizer,
            color_indices ) != SUCCESS )
            return FAILURE;
         break;

      default:                           // should always be one of above
         ASSERT( FALSE );
         return FAILURE;
   }

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_8bit_grayscale_tile_as_palette_image( int itile, CColorQuantizer &color_quantizer,
      unsigned char *color_indices )
{
   // this function reads a 8-bit grayscale image tile into the argument
   // color_indices arrays
   // returns SUCCESS or FAILURE

   int i, j;
   unsigned int decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   unsigned int color_value;
   unsigned char *tile_data = NULL, *decompressed_tile_data = NULL;
   unsigned char cross_reference[256];
   unsigned char red, green, blue;
   int diff;
   double value;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;


   // Get the tile offset and byte count
   if ( GetTagTileValues( itile, tile_offset, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // allocate memory to hold the tile data
   tile_data = NULL;
//   tile_data = new unsigned char[tile_byte_count];
   tile_data = (unsigned char*)MEM_malloc( tile_byte_count  );
   if( tile_data == NULL )
      goto FAIL;

   // Read the tile
   if ( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // if necessary, create cross-reference table
   // by finding nearest grayscale entry
   if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
   {
      for( i = 0; i < 256; i++ )
      {
         cross_reference[i] = 0;
         diff = 256;
         for( j = 0; j < color_quantizer.m_num_colors; j++ )
         {
            red = color_quantizer.m_red[j];
            green = color_quantizer.m_green[j];
            blue = color_quantizer.m_blue[j];
            if( green == red && blue == red )
            {
               if( abs( i - red ) < diff )
               {
                  cross_reference[i] = j;
                  diff = abs( i - red );
                  if( diff == 0 )
                     break;
               }
            }
         }
      }
   }

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // copy strip data to pixel array
         for( i = 0; i < (int)tile_byte_count; i++ )
         {
            red = tile_data[i];
            // check for reversed grayscale
            if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
               red = 255 - red;
            if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
            {
               value = (double)(red) * (color_quantizer.m_num_colors-1) /
                       255.0 + 0.5;
               color_indices[i] = (unsigned char) floor( value );
            }
            else
            {
               if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                  color_indices[i] =
                  cross_reference[red];
               else
               {
                  green = blue = red;
                  color_value = (red >> 3) << 10;
                  color_value |= (green >> 3) << 5;
                  color_value |= (blue >> 3);
                  color_indices[i] =
                     color_quantizer.m_histogram_indices[color_value];
               }
            }
         }
         break;
      case GEOTIFF_LZW:                 // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
//          decompressed_strip_data =
//          new unsigned char[m_tile_width * m_tile_length];
         decompressed_tile_data =
            (unsigned char*)MEM_malloc( m_tile_width * m_tile_length );
         num_remaining_bytes = m_tile_width * m_tile_length;
         decompressed_size = m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
             goto FAIL;

         // copy decompressed tile data to pixel array
         for( i = 0; i < (int)m_num_tile_pixels; i++ )
         {
            red = decompressed_tile_data[i];
            // check for reversed grayscale
            if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
               red = 255 - red;
            if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
            {
               value = (double)(red) * (color_quantizer.m_num_colors-1) /
                       255.0 + 0.5;
               color_indices[i] = (unsigned char) floor( value );
            }
            else
            {
               if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                  color_indices[i] =
                  cross_reference[red];
               else
               {
                  green = blue = red;
                  color_value = (red >> 3) << 10;
                  color_value |= (green >> 3) << 5;
                  color_value |= (blue >> 3);
                  color_indices[i] =
                     color_quantizer.m_histogram_indices[color_value];
               }
            }
         }

         break;
      default:
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_16bit_grayscale_tile_as_palette_image( int itile, CColorQuantizer &color_quantizer,
      unsigned char *color_indices )
{
   // this function reads a 16-bit grayscale image tile into the argument
   // color_indices arrays
   // returns SUCCESS or FAILURE

   int i, j;
   unsigned int decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   unsigned int color_value;
   unsigned char *tile_data = NULL, *decompressed_tile_data = NULL;
   unsigned char cross_reference[256];
   unsigned char red, green, blue;
   int diff;
   double value;
   unsigned char byte1, byte2;
   unsigned short *tile_16bit_data;
   double fraction;
   int pixel_index;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // Get the tile offset and byte count
   if ( GetTagTileValues( itile, tile_offset, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // allocate memory to hold the tile data
   tile_data = NULL;
   tile_data = (unsigned char*)MEM_malloc( tile_byte_count  );
   if( tile_data == NULL )
      goto FAIL;

   // Read the tile
   if ( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // if necessary, create cross-reference table
   // by finding nearest grayscale entry
   if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
   {
      for( i = 0; i < 256; i++ )
      {
         cross_reference[i] = 0;
         diff = 256;
         for( j = 0; j < color_quantizer.m_num_colors; j++ )
         {
            red = color_quantizer.m_red[j];
            green = color_quantizer.m_green[j];
            blue = color_quantizer.m_blue[j];
            if( green == red && blue == red )
            {
               if( abs( i - red ) < diff )
               {
                  cross_reference[i] = j;
                  diff = abs( i - red );
                  if( diff == 0 )
                     break;
               }
            }
         }
      }
   }

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // swap byte order if necessary
         if( m_byte_order == GEOTIFF_BIG_ENDIAN )
         {
            for( i = 0; i < (int)tile_byte_count; i += 2 )
            {
               byte1 = tile_data[i];
               byte2 = tile_data[i+1];
               tile_data[i] = byte2;
               tile_data[i+1] = byte1;
            }
         }
         // copy tile data to pixel array
         tile_16bit_data = (unsigned short*)tile_data;
         pixel_index = 0;
         for( i = 0; i < (int)tile_byte_count/2; i++ )
         {
            fraction = (double)(tile_16bit_data[i]-m_min_sample_value)/
               (m_max_sample_value-m_min_sample_value);
            if( fraction < 0.0 )
               fraction = 0.0;
            if( fraction > 1.0 )
               fraction = 1.0;
            red = (unsigned char)(255*fraction+0.5);
            // check for reversed grayscale
            if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
               red = 255 - red;
            if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
            {
               value = (double)(red) * (color_quantizer.m_num_colors-1) /
                       255.0 + 0.5;
               color_indices[i] = (unsigned char) floor( value );
            }
            else
            {
               if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                  color_indices[i] =
                  cross_reference[red];
               else
               {
                  green = blue = red;
                  color_value = (red >> 3) << 10;
                  color_value |= (green >> 3) << 5;
                  color_value |= (blue >> 3);
                  color_indices[i] =
                     color_quantizer.m_histogram_indices[color_value];
               }
            }
            pixel_index++;
         }
         break;
      case GEOTIFF_LZW:                 // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
         decompressed_tile_data =
            (unsigned char*)MEM_malloc( 2 * m_tile_width * m_tile_length );
         num_remaining_bytes = 2 * m_tile_width * m_tile_length;
         decompressed_size = 2 * m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
             goto FAIL;

         // swap byte order if necessary
         if( m_byte_order == GEOTIFF_BIG_ENDIAN )
         {
            for( i = 0; i < (int)decompressed_size; i += 2 )
            {
               byte1 = decompressed_tile_data[i];
               byte2 = decompressed_tile_data[i+1];
               decompressed_tile_data[i] = byte2;
               decompressed_tile_data[i+1] = byte1;
            }
         }
         // copy tile data to pixel array
         tile_16bit_data = (unsigned short*)decompressed_tile_data;
         pixel_index = 0;
         for( i = 0; i < (int)m_num_tile_pixels; i++ )
         {
            fraction = (double)(tile_16bit_data[i]-m_min_sample_value)/
               (m_max_sample_value-m_min_sample_value);
            if( fraction < 0.0 )
               fraction = 0.0;
            if( fraction > 1.0 )
               fraction = 1.0;
            red = (unsigned char)(255*fraction+0.5);
            // check for reversed grayscale
            if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
               red = 255 - red;
            if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
            {
               value = (double)(red) * (color_quantizer.m_num_colors-1) /
                       255.0 + 0.5;
               color_indices[i] = (unsigned char) floor( value );
            }
            else
            {
               if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                  color_indices[i] =
                  cross_reference[red];
               else
               {
                  green = blue = red;
                  color_value = (red >> 3) << 10;
                  color_value |= (green >> 3) << 5;
                  color_value |= (blue >> 3);
                  color_indices[i] =
                     color_quantizer.m_histogram_indices[color_value];
               }
            }
            pixel_index++;
         }
         break;
      default:
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_8bit_palette_tile_as_palette_image( int itile, CColorQuantizer &color_quantizer,
      unsigned char *color_indices )
{
   // this function reads a 8-bit color palette image tile into the argument
   // color_indices arrays
   // returns SUCCESS or FAILURE

   int i, j;
   unsigned int decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   unsigned int color_value;
   unsigned char *tile_data = NULL, *decompressed_tile_data = NULL;
   unsigned char cross_reference[256];
   unsigned char red, green, blue, palette_index;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // Get the tile offset and byte count
   if ( GetTagTileValues( itile, tile_offset, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // allocate memory to hold the tile data
   tile_data = NULL;
//   tile_data = new unsigned char[tile_byte_count];
   tile_data = (unsigned char*)MEM_malloc( tile_byte_count  );
   if( tile_data == NULL )
      goto FAIL;

   // Read the tile
   if ( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // if necessary, create cross-reference table
   if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
   {
      for( i = 0; i < 256; i++ )
      {
         cross_reference[i] = 0;
         for( j = 0; j < color_quantizer.m_num_colors; j++ )
         {
            if( color_quantizer.m_red[j] == m_red_color_map[i]/256 &&
                color_quantizer.m_green[j] == m_green_color_map[i]/256 &&
                color_quantizer.m_blue[j] == m_blue_color_map[i]/256 )
            {
               cross_reference[i] = j;
               break;
            }
         }
      }
   }

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // copy tile data to pixel array
         for( i = 0; i < (int)tile_byte_count; i++ )
         {
            palette_index = tile_data[i];
            if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
               color_indices[i] = cross_reference[palette_index];
            else
            {
               red = (unsigned char)(m_red_color_map[palette_index]/256);
               green = (unsigned char)(m_green_color_map[palette_index]/256);
               blue = (unsigned char)(m_blue_color_map[palette_index]/256);
               color_value = (red >> 3) << 10;
               color_value |= (green >> 3) << 5;
               color_value |= (blue >> 3);
               color_indices[i] = color_quantizer.m_histogram_indices[color_value];
            }
         }
         break;
      case GEOTIFF_LZW:                 // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
//          decompressed_strip_data =
//          new unsigned char[m_tile_width * m_tile_length];
         decompressed_tile_data =
            (unsigned char*)MEM_malloc( m_tile_width * m_tile_length );
         num_remaining_bytes = m_tile_width * m_tile_length;
         decompressed_size = m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
             goto FAIL;

         // copy decompressed tile data to pixel array
         for( i = 0; i < (int)m_num_tile_pixels; i++ )
         {
            palette_index = decompressed_tile_data[i];
            if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
               color_indices[i] = cross_reference[palette_index];
            else
            {
               red = (unsigned char)(m_red_color_map[palette_index]/256);
               green = (unsigned char)(m_green_color_map[palette_index]/256);
               blue = (unsigned char)(m_blue_color_map[palette_index]/256);
               color_value = (red >> 3) << 10;
               color_value |= (green >> 3) << 5;
               color_value |= (blue >> 3);
               color_indices[i] = color_quantizer.m_histogram_indices[color_value];
            }
         }
         break;
      default:
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
//      delete [] tile_data;
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiff::get_24bit_rgb_tile_as_palette_image( int itile, CColorQuantizer &color_quantizer,
      unsigned char *color_indices )
{
   // this function reads a 24bit rgb image tile into the argument
   // color_indices arrays
   // returns SUCCESS or FAILURE

   int i, pixel_index;
   unsigned int decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   unsigned int color_value;
   unsigned char *tile_data = NULL, *decompressed_tile_data = NULL;
   unsigned char red, green, blue;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // Get the tile offset and byte count
   if ( GetTagTileValues( itile, tile_offset, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // allocate memory to hold the tile data
   tile_data = NULL;
//   tile_data = new unsigned char[tile_byte_count];
   tile_data = (unsigned char*)MEM_malloc( tile_byte_count  );
   if( tile_data == NULL )
      goto FAIL;

   // Read the tile
   if ( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // copy tile data to pixel array
         pixel_index = 0;
         for( i = 0; i < (int)tile_byte_count; i += 3 )
         {
            red = tile_data[i];
            green = tile_data[i+1];
            blue = tile_data[i+2];
            color_value = (red >> 3) << 10;
            color_value |= (green >> 3) << 5;
            color_value |= (blue >> 3);
            color_indices[pixel_index] =
               color_quantizer.m_histogram_indices[color_value];
            pixel_index++;
         }
         break;
      case GEOTIFF_LZW:                 // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
//          decompressed_strip_data =
//          new unsigned char[3 * m_num_tile_pixels];
         decompressed_tile_data =
            (unsigned char*)MEM_malloc( 3 * m_num_tile_pixels );
         num_remaining_bytes = 3 * m_num_tile_pixels;
         decompressed_size = 3 * m_num_tile_pixels;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
             goto FAIL;

         // copy decompressed tile data to pixel array
         pixel_index = 0;
         for( i = 0; i < (int)(3 * m_num_tile_pixels); i += 3 )
         {
            red = decompressed_tile_data[i];
            green = decompressed_tile_data[i+1];
            blue = decompressed_tile_data[i+2];
            color_value = (red >> 3) << 10;
            color_value |= (green >> 3) << 5;
            color_value |= (blue >> 3);
            color_indices[pixel_index] =
               color_quantizer.m_histogram_indices[color_value];
            pixel_index++;
         }

         break;
      default:
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}


// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_subsampled_rgb_subimage( int sampling,
                         int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                         unsigned char *img, IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage, subsampled according to the sampling argument
   // the sampling argument indicates the fraction of the image's rows and
   // columns that are to be retained in the subsampled image. a sampling of 3
   // indicates that every third row and column should be retained
   // returns SUCCESS or FAILURE

   int row, col, index, icol;
   unsigned char *img_row = NULL;
   int min_tile_row, min_tile_col, max_tile_row, max_tile_col;
   int tile_row, tile_col, tile;
   int image_hpix, image_vpix;
   int subimage_hpix, subimage_vpix, subimage_width, subimage_height, subimage_index;
   int subsampled_subimage_hpix, subsampled_subimage_vpix;
   int subsampled_subimage_width, subsampled_subimage_height, subsampled_subimage_index;
   int tile_hpix, tile_vpix, tile_index, maxpix;

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // return error if image type is not supported
   if( !m_image_type_supported )
      return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
      return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
      return FAILURE;

   // return FAILURE if sampling is less than 1
   if( sampling < 1 )
      return FAILURE;

   subimage_width = max_hpix - min_hpix + 1;
   subimage_height = max_vpix - min_vpix + 1;
   subsampled_subimage_width = (subimage_width-1) / sampling + 1;
   subsampled_subimage_height = (subimage_height-1) / sampling + 1;

   maxpix = subsampled_subimage_width * subsampled_subimage_height;
   // check for tiled image
   if( m_image_is_tiled )
   {
      // image is tiled

      // determine range of tile rows and columns in subimage
      min_tile_row = min_vpix / m_tile_length;
      max_tile_row = max_vpix / m_tile_length;
      min_tile_col = min_hpix / m_tile_width;
      max_tile_col = max_hpix / m_tile_width;

      // loop through tiles covering the subimage
      for ( tile_row = min_tile_row; tile_row <= max_tile_row; tile_row++ )
      {
         for ( tile_col = min_tile_col; tile_col <= max_tile_col; tile_col++ )
         {
            tile = m_num_tiles_across * tile_row + tile_col;

            if ( get_tile_as_rgb_image( tile, m_tile_red_array, m_tile_green_array,
               m_tile_blue_array ) != SUCCESS )
               goto FAIL;

            // copy data from tile
            for( tile_vpix = 0; tile_vpix < (int)m_tile_length; tile_vpix++ )
            {
               image_vpix = tile_row * m_tile_length + tile_vpix;
               if ( image_vpix < min_vpix )
                  continue;
               if ( image_vpix > max_vpix )
                  break;
               for ( tile_hpix = 0; tile_hpix < (int)m_tile_width; tile_hpix++ )
               {
                  image_hpix = tile_col * m_tile_width + tile_hpix;
                  if ( image_hpix < min_hpix )
                     continue;
                  if ( image_hpix > max_hpix )
                     break;
                  tile_index = tile_vpix * m_tile_width + tile_hpix;
                  subimage_hpix = image_hpix - min_hpix;
                  subimage_vpix = image_vpix - min_vpix;
                  subimage_index = subimage_vpix * subimage_width + subimage_hpix;
                  if ( subimage_hpix % sampling == 0 && subimage_vpix % sampling == 0 )
                  {
                     subsampled_subimage_hpix = subimage_hpix / sampling;
                     subsampled_subimage_vpix = subimage_vpix / sampling;
                     subsampled_subimage_index = subsampled_subimage_vpix * subsampled_subimage_width + subsampled_subimage_hpix;
                     img[subsampled_subimage_index*3+0] = m_tile_red_array[tile_index];
                     img[subsampled_subimage_index*3+1] = m_tile_green_array[tile_index];
                     img[subsampled_subimage_index*3+2] = m_tile_blue_array[tile_index];
                  }
               }
            }
         }
      }
   }
   else
   {
      // allocate arrays to hold one row of data

      img_row = (unsigned char*)MEM_malloc( subimage_width * 3 );
      if( img_row == NULL )
        goto FAIL;

      // read the subsampled rows, then subsample the columns
      index = 0;
      for ( row = min_vpix; row <= max_vpix; row += sampling )
      {
         // read the row
         if ( get_rgb_subimage( min_hpix, row, max_hpix-min_hpix+1, 1, img_row, callback ) != SUCCESS )
            goto FAIL;
         for ( col = min_hpix; col <= max_hpix; col += sampling )
         {
            icol = col - min_hpix;
         ASSERT(index < maxpix);
         if (icol < subimage_width)
         {
            img[index*3+0] = img_row[icol*3+0];
            img[index*3+1] = img_row[icol*3+1];
            img[index*3+2] = img_row[icol*3+2];
         }
            index++;
         }
      }
      // deallocate row memory
      MEM_free( img_row );
   }

   return SUCCESS;

FAIL:
   // deallocate memory
   if( img_row != NULL )
      MEM_free( img_row );
   return FAILURE;
}
// end of get_subsampled_rgb_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_rgb_subimage3( int min_hpix, int min_vpix, int max_hpix,
                                int max_vpix, BYTE *color_index,
                        unsigned char *img, unsigned char *pal_array, IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage
   // returns SUCCESS or FAILURE

   int min_tile_row, min_tile_col, max_tile_row, max_tile_col;
   int tile_row, tile_col, tile, rslt;
   int image_hpix, image_vpix;
   int subimage_hpix, subimage_vpix, subimage_width, subimage_index;
   int tile_hpix, tile_vpix, tile_index, ti, val;

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // return error if image type is not supported
   if( !m_image_type_supported )
      return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
      return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
      return FAILURE;

   subimage_width = max_hpix - min_hpix + 1;

   // check for tiled image
   if( m_image_is_tiled )
   {
      // image is tiled
      // determine range of tile rows and columns in subimage
      min_tile_row = min_vpix / m_tile_length;
      max_tile_row = max_vpix / m_tile_length;
      min_tile_col = min_hpix / m_tile_width;
      max_tile_col = max_hpix / m_tile_width;

      // loop through tiles covering the subimage
      for( tile_row = min_tile_row; tile_row <= max_tile_row; tile_row++ )
      {
         for( tile_col = min_tile_col; tile_col <= max_tile_col; tile_col++ )
         {
            tile = m_num_tiles_across * tile_row + tile_col;

            if( get_tile_as_rgb_image( tile, m_tile_red_array, m_tile_green_array, m_tile_blue_array ) != SUCCESS )
               goto FAIL;

            // copy data from tile
            for( tile_vpix = 0; tile_vpix < (int)m_tile_length; tile_vpix++ )
            {
               image_vpix = tile_row * m_tile_length + tile_vpix;
               if( image_vpix < min_vpix )
                  continue;
               if( image_vpix > max_vpix )
                  break;
               for( tile_hpix = 0; tile_hpix < (int)m_tile_width; tile_hpix++ )
               {
               image_hpix = tile_col * m_tile_width + tile_hpix;
               if( image_hpix < min_hpix )
                  continue;
               if( image_hpix > max_hpix )
                  break;
               tile_index = tile_vpix * m_tile_width + tile_hpix;
               subimage_hpix = image_hpix - min_hpix;
               subimage_vpix = image_vpix - min_vpix;
               subimage_index = subimage_vpix * subimage_width + subimage_hpix;
               img[subimage_index*3+0] = m_tile_red_array[tile_index];
               img[subimage_index*3+1] = m_tile_green_array[tile_index];
               img[subimage_index*3+2] = m_tile_blue_array[tile_index];
               ti = m_tile_red_array[tile_index];
               ti = ti >> 3;
               val = ti << 10;
               ti = m_tile_green_array[tile_index];
               ti = ti >> 3;
               val += ti << 5;
               ti = m_tile_blue_array[tile_index];
               ti = ti >> 3;
               val += ti;
               pal_array[subimage_index] = color_index[val];
               tile_index++;
               }
            }
         }
      }
   }
   else
   {
      // image is not tiled
      // select image type and call appropriate function
      switch( m_image_type )
      {
         case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
            goto FAIL;

         case GEOTIFF_IMAGE_TYPE_1BIT:
            if( get_1bit_image_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
          if (m_samples_per_pixel == 3)
             rslt = get_24bit_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
          else
            rslt = get_8bit_grayscale_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback );
          if (rslt != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
            if( get_16bit_grayscale_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_256_COLOR:
            if( get_8bit_palette_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
            if( get_24bit_rgb_as_rgb_subimage( min_hpix, min_vpix, max_hpix, max_vpix, img, callback ) != SUCCESS )
               goto FAIL;
            break;

         default:                           // should always be one of above
            ASSERT( FALSE );
            goto FAIL;
      }
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}
// end of get_rgb_subimage3

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_histogram( unsigned int *histogram, IImageLibCallback *callback )
{
   // calculates the images's 32k histogram and copies to the argument
   // returns SUCCESS or FAILURE

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // calculate histogram if no errors
   if( calculate_histogram(callback ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description = "Error Calculating Histogram";
      m_cumulative_error_description += " / ";
      return FAILURE;
   }

   // copy the histogram to the argument array
   memcpy( histogram, m_puiHistogram, sizeof(*m_puiHistogram) );

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_num_color_map_values( int &num_color_map_values )
{
   // returns number of colors in the color map if present
   // returns SUCCESS or FAILURE

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // image must be 8-bit palette
   if( m_image_type != GEOTIFF_IMAGE_TYPE_256_COLOR )
      return FAILURE;

   num_color_map_values = m_num_color_map_values;

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_color_map( unsigned char *red, unsigned char *green,
                             unsigned char *blue )
{
   // this function copies the image color map colors (256 values R,G,B)
   // to the argument arrays, mapped from 65536 to 256 values
   // returns SUCCESS or FAILURE

   int i;

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // image must be 8-bit palette
   if( m_image_type != GEOTIFF_IMAGE_TYPE_256_COLOR )
      return FAILURE;

   // copy the color map to the argument arrays
   for( i = 0; i < m_num_color_map_values; i++ )
   {
      red[i] = (BYTE) (m_red_color_map[i] / 256);
      green[i] = (BYTE) (m_green_color_map[i] / 256);
      blue[i] = (BYTE) (m_blue_color_map[i] / 256);
   }

   // fill out color map with zeroes
   for( i = m_num_color_map_values; i < 256; i++ )
   {
      red[i] = 0;
      green[i] = 0;
      blue[i] = 0;
   }

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_documentation_strings( CString &document_name, CString &image_description,
   CString &image_size, CString &image_format, CString &scanner_make_and_model,
   CString &image_resolution, CString &software, CString &creation_date_and_time,
   CString &creator, CString &copyright_notice )
{
   int tag_index;

   // initialize all arguments to blank
   document_name = "";
   image_description = "";
   image_size = "";
   image_format = "";
   scanner_make_and_model = "";
   image_resolution = "";
   software = "";
   creation_date_and_time = "";
   creator = "";
   copyright_notice = "";

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // get document name
   if ( find_tag( GEOTIFF_DOCUMENT_NAME_TAG, tag_index ) == SUCCESS )
      if (m_tags[tag_index].m_ascii_values != NULL)
         document_name = m_tags[tag_index].m_ascii_values;

   // get image description
   if ( find_tag( GEOTIFF_IMAGE_DESCRIPTION_TAG, tag_index ) == SUCCESS )
      if (m_tags[tag_index].m_ascii_values != NULL)
         image_description = m_tags[tag_index].m_ascii_values;

   // get image size
   image_size.Format( "Width %i Pixels  Height %i Pixels", m_image_width,
      m_image_length );

   // get scanner make and model
   if( find_tag( GEOTIFF_MAKE_TAG, tag_index ) == SUCCESS )
   {
      if (m_tags[tag_index].m_ascii_values != NULL)
      {
        scanner_make_and_model = m_tags[tag_index].m_ascii_values;
        scanner_make_and_model += " ";
      }
   }
   if( find_tag( GEOTIFF_MODEL_TAG, tag_index ) == SUCCESS )
      if (m_tags[tag_index].m_ascii_values != NULL)
         scanner_make_and_model += m_tags[tag_index].m_ascii_values;

   // image resolution
   switch( m_resolution_unit )
   {
      case GEOTIFF_RESOLUTION_UNIT_NONE:
         image_resolution.Format( "X Resolution %.3lf Pixels  Y Resolution %.3lf Pixels",
            m_x_resolution, m_y_resolution );
         break;

      case GEOTIFF_RESOLUTION_UNIT_INCH:
         image_resolution.Format( "X Resolution %.3lf Pixels/Inch  Y Resolution %.3lf Pixels/Inch",
            m_x_resolution, m_y_resolution );
         break;

      case GEOTIFF_RESOLUTION_UNIT_CENTIMETER:
         image_resolution.Format( "X Resolution %.3lf Pixels/cm  Y Resolution %.3lf Pixels/cm",
            m_x_resolution, m_y_resolution );
         break;

      default:
         break;
   }

   // get image format
   image_format = m_image_type_description;

   // get software
   if( find_tag( GEOTIFF_SOFTWARE_TAG, tag_index ) == SUCCESS )
      if (m_tags[tag_index].m_ascii_values != NULL)
         software = m_tags[tag_index].m_ascii_values;

   // get creation date and time
   if( find_tag( GEOTIFF_DATE_TIME_TAG, tag_index ) == SUCCESS )
      if (m_tags[tag_index].m_ascii_values != NULL)
         creation_date_and_time = m_tags[tag_index].m_ascii_values;

   // get creator
   if( find_tag( GEOTIFF_ARTIST_TAG, tag_index ) == SUCCESS )
      if (m_tags[tag_index].m_ascii_values != NULL)
          creator = m_tags[tag_index].m_ascii_values;

   // get copyright notice
   if( find_tag( GEOTIFF_COPYRIGHT_TAG, tag_index ) == SUCCESS )
      if (m_tags[tag_index].m_ascii_values != NULL)
         copyright_notice = m_tags[tag_index].m_ascii_values;

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

// private interface

int CGeoTiff::read_directory( int *next_dir_offset)
{
   // this function reads the image file directory pointed
   // to by m_directory_offset, including all tags.
   // returns SUCCESS or FAILURE

   int i;
   unsigned short num_tags;
   const int STR_LEN = 50;
   char string[STR_LEN];

   //TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);

   // Move file pointer to directory offset
   if ( gtfSeek( m_directory_offset ) != SUCCESS )
   {
      m_new_error_description = "Tag directory offset is invalid.";
      return FAILURE;
   }

   // read number of entries
   if( read_unsigned_short( num_tags ) != SUCCESS )
   {
      m_new_error_description = "Error reading number of tag entries.";
      return FAILURE;
   }
   if (num_tags > 200)
   {
      m_new_error_description = "Error reading number of tag entries.";
      return FAILURE;
   }

   m_num_tags = num_tags;

   if (m_tags)
      delete [] m_tags;

   // allocate memory for tags
   m_tags = new CTiffTag[m_num_tags];

   // read each tag
   for( i = 0; i < m_num_tags; i++ )
      if( read_tag( m_tags[i] ) != SUCCESS )
      {
         sprintf_s( string, STR_LEN, "Error reading tag %i.", i );
         m_new_error_description = string;
         return FAILURE;
      }

   // Next directory offset
   if ( gtfSeek( m_directory_offset + (num_tags * 12) + 2 ) != SUCCESS )
   {
      m_new_error_description = "Tag directory offset is invalid.";
      return FAILURE;
   }

   return read_signed_int( *next_dir_offset );
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::read_tag( CTiffTag &tag )
{
   // this function reads a tiff tag into the argument
   // from the current file position
   // returns SUCCESS or FAILURE

   int i, data_size_in_bytes;

#if defined MAPPEDFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
   PCHAR pchSavedFilePosition;
#else
   fpos_t qwSavedFilePosition;
#endif

   double ratio;

   // clear the tag
   tag.clear( );

   // read tag number
   if( read_unsigned_short( tag.m_tag_id ) != SUCCESS )
      goto FAIL;

   // read data type
   if( read_unsigned_short( tag.m_type ) != SUCCESS )
      goto FAIL;

   // read data count
   if( read_unsigned_int( tag.m_count ) != SUCCESS )
      goto FAIL;

   // Save file position
#if defined MAPPEDFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
   pchSavedFilePosition = m_iobMappedFileIOBufDesc._ptr;
#elif defined READFILE_FILE_INPUT
   qwSavedFilePosition = m_ovlpOverlapped.Offset | ( (fpos_t) m_ovlpOverlapped.OffsetHigh << 32 );
#else
   qwSavedFilePosition = m_qwFilePosition;
#endif

   // allocate memory for data and determine data size
   switch( tag.m_type )
   {
      case GEOTIFF_BYTE:              // 8-bit unsigned integer
         data_size_in_bytes = sizeof(BYTE);
         tag.m_byte_values = (PBYTE) MEM_malloc( tag.m_count * sizeof(BYTE) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // Seek value offset position in file
            if ( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }
         // Read value(s) from current file location
         if ( gtfRead( tag.m_byte_values, tag.m_count * sizeof(BYTE) ) != SUCCESS )
            goto FAIL;
         break;

      case GEOTIFF_ASCII:             // null-terminated string
         data_size_in_bytes = sizeof(CHAR);
         tag.m_ascii_values = (PCHAR) MEM_malloc( tag.m_count * sizeof(CHAR) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // seek value offset position in file
            if ( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }

         // Read value(s) from current file location
         if ( read_string( tag.m_ascii_values, tag.m_count ) !=  SUCCESS )
            goto FAIL;
         break;

      case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
         data_size_in_bytes = sizeof(USHORT);
         tag.m_short_values =
            (PUSHORT) MEM_malloc( tag.m_count * sizeof(USHORT) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // seek value offset position in file
            if ( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_unsigned_short( tag.m_short_values[i] ) !=  SUCCESS )
               goto FAIL;
        break;

      case GEOTIFF_LONG:              // unsigned long (32-bit) integer
         data_size_in_bytes = sizeof(UINT);
         tag.m_long_values =
            (PUINT) MEM_malloc( tag.m_count * sizeof(UINT) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // Seek value offset position in file
            if( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_unsigned_int( tag.m_long_values[i] ) !=  SUCCESS )
               goto FAIL;
         break;

      case GEOTIFF_RATIONAL:          // num/denom pair of unsigned longs
         data_size_in_bytes = 2 * sizeof(UINT);
         tag.m_rational_numerator_values =
            (PUINT) MEM_malloc( tag.m_count * sizeof(UINT) );
         tag.m_rational_denominator_values =
            (PUINT) MEM_malloc( tag.m_count * sizeof(UINT) );

         // Store ratio as a double
         tag.m_double_values =
            (DOUBLE*) MEM_malloc( tag.m_count * sizeof(DOUBLE) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // seek value offset position in file
            if( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
         {
            if( read_unsigned_int( tag.m_rational_numerator_values[i] ) != SUCCESS)
               goto FAIL;
            if( read_unsigned_int( tag.m_rational_denominator_values[i] ) != SUCCESS)
               goto FAIL;
            // calculate and store ratio as a double, checking for division by
            // zero
            if( tag.m_rational_denominator_values[i] != 0 )
               ratio = tag.m_rational_numerator_values[i] /
                       tag.m_rational_denominator_values[i];
            else
               ratio = HUGE_VAL;
            tag.m_double_values[i] = ratio;
         }
         break;

      case GEOTIFF_SBYTE:             // 8-bit signed integer
         data_size_in_bytes = sizeof(CHAR);
         tag.m_sbyte_values = (PCHAR) MEM_malloc( tag.m_count * sizeof(CHAR) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // Seek value offset position in file
            if ( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }
         // Read value(s) from current file location
         if ( gtfRead( (PBYTE) tag.m_sbyte_values, tag.m_count * sizeof(CHAR) ) != SUCCESS )
            goto FAIL;
         break;

      case GEOTIFF_UNDEFINED_TYPE:         // 8-bit undefined data type
         data_size_in_bytes = sizeof(CHAR);
         tag.m_undefined_values = (PCHAR) MEM_malloc( tag.m_count * sizeof(CHAR) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // Seek value offset position in file
            if ( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }

         // Read value(s) from current file location
         if ( gtfRead( (PBYTE) tag.m_undefined_values, tag.m_count * sizeof(CHAR) ) != SUCCESS )
            goto FAIL;
         break;

      case GEOTIFF_SSHORT:            // signed short (16-bit) integer
         data_size_in_bytes = sizeof(SHORT);
         tag.m_sshort_values =
            (PSHORT) MEM_malloc( tag.m_count * sizeof(SHORT) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // Seek value offset position in file
            if ( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }
         // Read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_signed_short( tag.m_sshort_values[i] ) !=  SUCCESS )
               goto FAIL;
         break;

      case GEOTIFF_SLONG:             // signed long (32-bit) integer
         data_size_in_bytes = sizeof(INT);
         tag.m_slong_values = (PINT) MEM_malloc( tag.m_count * sizeof(INT) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // Seek value offset position in file
            if ( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }
         // Read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_signed_int( tag.m_slong_values[i] ) !=  SUCCESS )
            goto FAIL;
         break;

      case GEOTIFF_SRATIONAL:         // num/denom pair of signed longs
         data_size_in_bytes = 2 * sizeof(INT);
         tag.m_srational_numerator_values = (PINT) MEM_malloc( tag.m_count * sizeof(INT) );
         tag.m_srational_denominator_values = (PINT) MEM_malloc( tag.m_count * sizeof(INT) );
         tag.m_double_values = (DOUBLE*) MEM_malloc( tag.m_count * sizeof(DOUBLE) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // seek value offset position in file
            if ( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
         {
            if( read_signed_int( tag.m_srational_numerator_values[i] ) != SUCCESS )
               goto FAIL;
            if( read_signed_int( tag.m_srational_denominator_values[i] ) != SUCCESS )
               goto FAIL;
            // calculate and store ratio as a double, checking for division by
            // zero
            if( tag.m_srational_denominator_values[i] != 0 )
               ratio = tag.m_srational_numerator_values[i] /
                       tag.m_srational_denominator_values[i];
            else
            {
               if( tag.m_srational_numerator_values[i] >= 0 )
                  ratio = HUGE_VAL;
               else
                  ratio = -HUGE_VAL;
            }
            tag.m_double_values[i] = ratio;
         }
         break;

      case GEOTIFF_FLOAT:             // 4-byte float value
         data_size_in_bytes = sizeof(FLOAT);
         tag.m_float_values =
            (FLOAT*) MEM_malloc( tag.m_count * sizeof(FLOAT) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // Seek value offset position in file
            if ( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_float( tag.m_float_values[i] ) !=  SUCCESS )
            goto FAIL;
         break;

      case GEOTIFF_DOUBLE:            // 8-byte double value
         data_size_in_bytes = sizeof(DOUBLE);
         tag.m_double_values = (DOUBLE*) MEM_malloc( tag.m_count * sizeof(DOUBLE) );
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
               goto FAIL;

            // Seek value offset position in file
            if ( gtfSeek( tag.m_value_offset ) != SUCCESS )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_double( tag.m_double_values[i] ) !=  SUCCESS )
            goto FAIL;
         break;

      default:
         // data type is unknown - don't read values
         break;
   }

   // Restore file position + 4 bytes
#if defined MAPPEDFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
   if ( gtfSeek( pchSavedFilePosition - m_iobMappedFileIOBufDesc._base + 4 ) != SUCCESS )
#else
   if ( gtfSeek( qwSavedFilePosition + 4 ) != SUCCESS )
#endif
      goto FAIL;

   tag.m_tag_name = get_tag_name( tag.m_tag_id );
   tag.m_type_name = get_type_name( tag.m_type );
   tag.m_value_name = get_tag_value_name( tag );
   tag.m_defined = TRUE;

   // return SUCCESS
   return SUCCESS;

FAIL:
   // on failure, clear the tag and return FAILURE
   tag.clear( );
   return FAILURE;
}



// ********************************************************************************************
// ********************************************************************************************

INT CGeoTiff::GetTagStringValue( USHORT wTagID, CString& csValue )
{
   INT iResult, iTagIndex;

   iResult = find_tag( wTagID, iTagIndex );
   if ( iResult == SUCCESS )
      if (m_tags[iTagIndex].m_ascii_values != NULL)
         csValue = m_tags[iTagIndex].m_ascii_values;

   return iResult;
}



// ********************************************************************************************
// ********************************************************************************************

INT CGeoTiff::GetTagTileValues( INT iTile, UINT& uiTileOffset, UINT& cTileByteCount )
{
   INT iResult;

   iResult = GetTagUIntValue( iTile, GEOTIFF_TILE_OFFSETS_TAG, uiTileOffset );
   if ( iResult == SUCCESS )
      iResult = GetTagUIntValue( iTile, GEOTIFF_TILE_BYTE_COUNTS_TAG, cTileByteCount );

   return iResult;
}




// ********************************************************************************************
// ********************************************************************************************

INT CGeoTiff::GetTagUIntValue( INT iValueIndex, USHORT wTagID, UINT& uiValue )
{
   int iResult, iTagIndex;

   // Find the tag
   iResult = find_tag( wTagID, iTagIndex );
   if ( iResult == SUCCESS )
      iResult = GetTagIndexUIntValue( iValueIndex, iTagIndex, uiValue );

   return iResult;
}



// ********************************************************************************************
// ********************************************************************************************

INT CGeoTiff::GetTagIndexULongValue( INT iValueIndex, INT iTagIndex, ULONG& ulValue )
{
   if ( sizeof(UINT) == sizeof(ULONG) )
   {
      return GetTagIndexUIntValue( iValueIndex, iTagIndex, *(UINT*) &ulValue );
   }
   else
   {
      UINT uiValue;
      INT iResult = GetTagIndexUIntValue( iValueIndex, iTagIndex, uiValue );
      ulValue = uiValue;
      return iResult;
   }
}

INT CGeoTiff::GetTagIndexUIntValue( INT iValueIndex, INT iTagIndex, UINT& uiValue )
{
   // Decode depending upon type
   switch ( m_tags[iTagIndex].m_type )
   {
      case GEOTIFF_SHORT:
         uiValue = m_tags[iTagIndex].m_short_values[iValueIndex];
         return SUCCESS;

      case GEOTIFF_LONG:
         uiValue = m_tags[iTagIndex].m_long_values[iValueIndex];
         return SUCCESS;

      default:
         ASSERT( FALSE );
         break;
   }

   return FAILURE;
}



// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::find_tag( unsigned short tag_id, int &index )
{
   // finds the index of the specified tag_id in the tag array
   // returns SUCCESS or FAILURE

   for( index = 0; index < m_num_tags; index++ )
   {
      if( m_tags[index].m_tag_id == tag_id )
         return SUCCESS;
   }
   index = -1;
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::inspect_tags( )
{
   // this function inspects the tags for required data and image information
   // returns SUCCESS or FAILURE depending on whether or not the required
   // data is present in the tags

   int i, index, tag_index, count;

   // get image width (no default)
   if( find_tag( GEOTIFF_IMAGE_WIDTH_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_image_width = m_tags[tag_index].m_short_values[0];
            break;
         case GEOTIFF_LONG:
            m_image_width = m_tags[tag_index].m_long_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "ImageWidth tag is invalid data type.";
            return FAILURE;
      }
   }
   else
   {
      m_new_error_description = "ImageWidth tag is missing.";
      return FAILURE;
   }
   if( m_image_width == 0 )
   {
      m_new_error_description = "Image width is zero.";
      return FAILURE;
   }

   // get image length (no default)
   if( find_tag( GEOTIFF_IMAGE_LENGTH_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_image_length = m_tags[tag_index].m_short_values[0];
            break;
         case GEOTIFF_LONG:
            m_image_length = m_tags[tag_index].m_long_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "ImageLength tag is invalid data type.";
            return FAILURE;
      }
   }
   else
   {
      m_new_error_description = "ImageLength tag is missing.";
      return FAILURE;
   }
   if( m_image_length == 0 )
   {
      m_new_error_description = "Image length is zero.";
      return FAILURE;
   }

   // calculate number of pixels
   m_num_pixels = m_image_width * m_image_length;

   // get compression scheme (default = no compression)
   if( find_tag( GEOTIFF_COMPRESSION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_compression_scheme = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "Compression tag is invalid data type.";
            return FAILURE;
      }
      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
//         case GEOTIFF_CCITT_1D:
         case GEOTIFF_PACKBITS:
         // LZW was kept out of this reader by the Unisys patent, which
         // expired in 2003.  It is now the ordinary choice for a palettised
         // chart - every current FAA sectional ships it.
         case GEOTIFF_LZW:
       case GEOTIFF_JPEG:
            break;
       case GEOTIFF_CCITT_1D:
         m_new_error_description = "CCITT compression is not supported";
            return FAILURE;
       case GEOTIFF_GROUP_3_FAX:
         m_new_error_description = "Group 3 Fax compression is not supported";
            return FAILURE;
       case GEOTIFF_GROUP_4_FAX:
         m_new_error_description = "Group 4 Fax compression is not supported";
            return FAILURE;
         default:                      // all others unsupported at present
            m_new_error_description = "Compression tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_compression_scheme = GEOTIFF_NO_COMPRESSION;

   // get the predictor (default is 1, none).  Only 1 and 2 exist for integer
   // samples; 3 is the floating-point predictor, which no reader here wants.
   if( find_tag( GEOTIFF_PREDICTOR_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_predictor = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "Predictor tag is invalid data type.";
            return FAILURE;
      }
      switch( m_predictor )
      {
         case 1:                        // no predictor
            break;
         case 2:                        // horizontal differencing
            break;
         default:
            m_new_error_description = "Predictor tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_predictor = 1;

   // get the jpeg table info if it's there
   if (m_compression_scheme == GEOTIFF_JPEG)
   {
      if( find_tag( GEOTIFF_JPEG_TABLES_TAG, tag_index ) == SUCCESS )
      {
         int k, cnt;

         cnt = m_jpeg_table_len = m_tags[tag_index].m_count;
         m_apbJpegTable.reset( new BYTE[ cnt ] );
         m_jpeg_table = m_apbJpegTable.get();
         for (k=0; k<cnt; k++)
            m_jpeg_table[k] = m_tags[tag_index].m_undefined_values[k];

      }
   }



   // get photometric interpretation (no default)
   if( find_tag( GEOTIFF_PHOTOMETRIC_INTERPRETATION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_photometric_interpretation = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description =
               "PhotometricInterpretation tag is invalid data type.";
            return FAILURE;
      }
      switch( m_photometric_interpretation )
      {
         case GEOTIFF_WHITE_IS_ZERO:
         case GEOTIFF_BLACK_IS_ZERO:
         case GEOTIFF_RGB:
         case GEOTIFF_RGB_PALETTE:
       case GEOTIFF_YCBCR:
       case GEOTIFF_CMYK:  // multispectral overview
            break;
         default:                       // all others not supported at present
            m_new_error_description =
               "PhotometricInterpretation tag value is not supported.";
            return FAILURE;
      }
   }
   else
   {
      m_new_error_description =
         "PhotometricInterpretation tag method is missing.";
      return FAILURE;
   }

   // get samples per pixel (default is 1)
   if( find_tag( GEOTIFF_SAMPLES_PER_PIXEL_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_samples_per_pixel = m_tags[tag_index].m_short_values[0];
            break;
         case GEOTIFF_LONG:
            m_samples_per_pixel = m_tags[tag_index].m_long_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description =
               "SamplesPerPixel tag is invalid data type.";
            return FAILURE;
      }
      switch( m_samples_per_pixel )
      {
         case 1:
         case 3:
            break;
//         default:                       // any others not supported at present
//            m_new_error_description =
//               "SamplesPerPixel tag value is not supported.";
//            return FAILURE;
      }
   }
   else
      m_samples_per_pixel = 1;

   // determine if image is tiled
   if( find_tag( GEOTIFF_TILE_WIDTH_TAG, tag_index ) == SUCCESS )
   {
      m_image_is_tiled = TRUE;
      // get tile width (same for all tiles)
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_tile_width = m_tags[tag_index].m_short_values[0];
            break;
         case GEOTIFF_LONG:
            m_tile_width = m_tags[tag_index].m_long_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "TileWidth tag is invalid data type.";
            return FAILURE;
      }
      // tile width must be an even multiple of 16
      if( m_tile_width < 16 || (m_tile_width % 16) != 0 )
      {
         m_new_error_description = "Tile width is not a multiple of 16.";
         return FAILURE;
      }

      // get tile length (same for all tiles)
      if( find_tag( GEOTIFF_TILE_LENGTH_TAG, tag_index ) == SUCCESS )
      {
         switch( m_tags[tag_index].m_type )
         {
            case GEOTIFF_SHORT:
               m_tile_length = m_tags[tag_index].m_short_values[0];
               break;
            case GEOTIFF_LONG:
               m_tile_length = m_tags[tag_index].m_long_values[0];
               break;
            default:
               // data is wrong type
               m_new_error_description = "TileLength tag is invalid data type.";
               return FAILURE;
         }
         // tile length must be an even multiple of 16
         if( m_tile_length < 16 || (m_tile_length % 16) != 0 )
         {
            m_new_error_description = "Tile length is not a multiple of 16.";
            return FAILURE;
         }
      }
      else
      {
         m_new_error_description = "TileLength tag is missing.";
         return FAILURE;
      }

      // verify that tile offsets are present
      if( find_tag( GEOTIFF_TILE_OFFSETS_TAG, tag_index ) != SUCCESS )
      {
         m_new_error_description = "TileOffsets tag is missing.";
         return FAILURE;
      }

      // verify that tile byte counts are present
      if( find_tag( GEOTIFF_TILE_BYTE_COUNTS_TAG, tag_index ) != SUCCESS )
      {
         m_new_error_description = "TileByteCounts tag is missing.";
         return FAILURE;
      }

      m_num_tile_pixels = m_tile_width * m_tile_length;
      m_num_tiles_across = (m_image_width + m_tile_width - 1) / m_tile_width;
      m_num_tiles_down = (m_image_length + m_tile_length - 1) / m_tile_length;
      m_num_tiles = m_num_tiles_across * m_num_tiles_down;
      // allocate memory to store a tile
      m_tile_red_array = (unsigned char*)MEM_malloc( m_num_tile_pixels );
      if( m_tile_red_array == NULL )
         return FAILURE;
      m_tile_green_array = (unsigned char*)MEM_malloc( m_num_tile_pixels );
      if( m_tile_green_array == NULL )
         return FAILURE;
      m_tile_blue_array = (unsigned char*)MEM_malloc( m_num_tile_pixels );
      if( m_tile_blue_array == NULL )
         return FAILURE;
      m_tile_color_indices = (unsigned char*)MEM_malloc( m_num_tile_pixels );
      if( m_tile_color_indices == NULL )
         return FAILURE;
   }
   else
   {
      // image is stripped, not tiled
      m_image_is_tiled = FALSE;
      // get rows per strip (default is 2^32-1 = 4,294,967,295)
      if( find_tag( GEOTIFF_ROWS_PER_STRIP_TAG, tag_index ) == SUCCESS )
      {
         switch( m_tags[tag_index].m_type )
         {
            case GEOTIFF_SHORT:
               m_rows_per_strip = m_tags[tag_index].m_short_values[0];
               break;
            case GEOTIFF_LONG:
               m_rows_per_strip = m_tags[tag_index].m_long_values[0];
               break;
            default:
               // data is wrong type
               m_new_error_description = "RowsPerStrip tag is invalid data type.";
               return FAILURE;
         }
      }
      else
         m_rows_per_strip = 4294967295;   // default value is essentially infinity
      if( m_rows_per_strip == 0 )
      {
         m_new_error_description = "RowsPerStrip tag value is zero (invalid).";
         return FAILURE;
      }
      m_num_strips = (unsigned int)
                     /*floor*/( ( m_image_length + m_rows_per_strip - 1 ) /
                     m_rows_per_strip );

      // find max strip size in bytes (no default)
      if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, tag_index ) == SUCCESS )
      {
         // check for number of strip byte counts = number of strips
//         if( m_tags[tag_index].m_count != m_num_strips )
//         {
//            m_new_error_description =
//               "StripByteCount tag count does not equal number of strips.";
//            return FAILURE;
//         }
         switch( m_tags[tag_index].m_type )
         {
            case GEOTIFF_SHORT:
               m_max_strip_byte_count = 0;
               for( i = 0; i < (int)m_num_strips; i++ )
               {
                  if( m_tags[tag_index].m_short_values[i] >
                     m_max_strip_byte_count )
                     m_max_strip_byte_count = m_tags[tag_index].m_short_values[i];
               }
               if( m_max_strip_byte_count == 0 )
               {
                  m_new_error_description = "StripByteCount is zero.";
                  return FAILURE;
               }
               break;
            case GEOTIFF_LONG:
               m_max_strip_byte_count = 0;
               for( i = 0; i < (int)m_num_strips; i++ )
               {
                  if( m_tags[tag_index].m_long_values[i] >
                      m_max_strip_byte_count )
                     m_max_strip_byte_count = m_tags[tag_index].m_long_values[i];
               }
// yes, it can!
            // this should not happen
//          if (m_max_strip_byte_count > m_image_width * m_samples_per_pixel * m_rows_per_strip)
//             m_max_strip_byte_count = m_image_width * m_samples_per_pixel * m_rows_per_strip;
               if( m_max_strip_byte_count == 0 )
               {
                  m_new_error_description = "StripByteCount is zero.";
                  return FAILURE;
               }
               break;
            default:
               // data is wrong type
               m_new_error_description = "RowsPerStrip tag is invalid data type.";
               return FAILURE;
         }
       if (m_bits_per_sample > 8)
          m_max_strip_byte_count *= 2;
      }
      else
      {
         m_new_error_description = "StripByteCounts tag is missing.";
         return FAILURE;
      }
   }

   // get x resolution (default=1.0)
   if( find_tag( GEOTIFF_X_RESOLUTION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_RATIONAL:
            m_x_resolution = m_tags[tag_index].m_double_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "XResolution tag is invalid data type.";
            return FAILURE;
      }
   }
   else
      m_x_resolution = 1.0;

   // get y resolution (default=1.0)
   if( find_tag( GEOTIFF_Y_RESOLUTION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_RATIONAL:
            m_y_resolution = m_tags[tag_index].m_double_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "YResolution tag is invalid data type.";
            return FAILURE;
      }
   }
   else
      m_y_resolution = 1.0;

   // get resolution unit (default is inch)
   if( find_tag( GEOTIFF_RESOLUTION_UNIT_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_resolution_unit = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description =
               "ResolutionUnit tag is invalid data type.";
            return FAILURE;
      }
      switch( m_resolution_unit )
      {
         case GEOTIFF_RESOLUTION_UNIT_NONE:
         case GEOTIFF_RESOLUTION_UNIT_INCH:
         case GEOTIFF_RESOLUTION_UNIT_CENTIMETER:
            break;
         default:                       // any others not supported at present
            m_new_error_description =
               "ResolutionUnit tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_resolution_unit = GEOTIFF_RESOLUTION_UNIT_INCH;

   // get planar configuration (default is chunky format)
   if( find_tag( GEOTIFF_PLANAR_CONFIGURATION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_planar_configuration = m_tags[tag_index].m_short_values[0];
            if (m_samples_per_pixel == 1)
               m_planar_configuration = GEOTIFF_CHUNKY_FORMAT;
            break;
         default:
            // data is wrong type
          if (m_samples_per_pixel != 1)
          {
            m_new_error_description =
               "PlanarConfiguration tag is invalid data type.";
            return FAILURE;
          }
      }
      switch( m_planar_configuration )
      {
         case GEOTIFF_CHUNKY_FORMAT:
            break;
         case GEOTIFF_PLANAR_FORMAT:
            break;
         default:                       // any others not supported at present
            m_new_error_description =
               "Image is in planar format which is not supported.";
            return FAILURE;
      }
   }
   else
      m_planar_configuration = GEOTIFF_CHUNKY_FORMAT;

   // get bits per sample (default is 1)
   if( find_tag( GEOTIFF_BITS_PER_SAMPLE_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_bits_per_sample = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "BitsPerSample tag is invalid data type.";
            return FAILURE;
      }
      switch( m_bits_per_sample )
      {
         case 1:
         case 4:
         case 8:
         case 16:
         case 32:
            break;
         default:                     // any others not supported at present
            m_new_error_description.Format("Image has %d bits per sample which is not supported.", m_bits_per_sample);
            return FAILURE;
      }
   }
   else
      m_bits_per_sample = 1;

   // get fill order (default is 1)
   if( find_tag( GEOTIFF_FILL_ORDER_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_fill_order = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "FillOrder tag is invalid data type.";
            return FAILURE;
      }
      switch( m_fill_order )
      {
         case 1:
            break;
         default:                       // any others not supported at present
            m_new_error_description = "FillOrder tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_fill_order = 1;

   // get orientation (default is 1)
   if( find_tag( GEOTIFF_ORIENTATION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_orientation = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "Orientation tag is invalid data type.";
            return FAILURE;
      }
      switch( m_orientation )
      {
         case 1:
            break;
         default:                     // any others not supported at present
            m_new_error_description = "Orientation tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_orientation = 1;

   // color map
   if( m_photometric_interpretation == GEOTIFF_RGB_PALETTE )
   {
      if( find_tag( GEOTIFF_COLOR_MAP_TAG, tag_index ) == SUCCESS )
      {
         switch( m_tags[tag_index].m_type )
         {
            case GEOTIFF_SHORT:
               count = m_tags[tag_index].m_count;
               m_num_color_map_values = 2 << (m_bits_per_sample-1);
               if( count != 3 * m_num_color_map_values )
               {
                  m_new_error_description =
                     "ColorMap tag contains incorrect data count.";
                  return FAILURE;   // wrong size
               }
               // allocate memory for color map arrays
               m_red_color_map =
                  (unsigned short*)MEM_malloc(
                  m_num_color_map_values * sizeof( unsigned short) );
               m_green_color_map =
                  (unsigned short*)MEM_malloc(
                  m_num_color_map_values * sizeof( unsigned short) );
               m_blue_color_map =
                  (unsigned short*)MEM_malloc(
                  m_num_color_map_values * sizeof( unsigned short) );
               // copy data from color map tag into color map arrays:
               // first red, then green, then blue
               index = 0;
               for( i = 0; i < m_num_color_map_values; i++, index++ )
                  m_red_color_map[i] = m_tags[tag_index].m_short_values[index];
               for( i = 0; i < m_num_color_map_values; i++, index++ )
                  m_green_color_map[i] =
                     m_tags[tag_index].m_short_values[index];
               for( i = 0; i < m_num_color_map_values; i++, index++ )
                  m_blue_color_map[i] = m_tags[tag_index].m_short_values[index];
               m_color_map_present = TRUE;
               break;
            default:
               // data is wrong type
               m_new_error_description = "ColorMap tag is invalid data type.";
               return FAILURE;
         }
      }
      else
      {
         m_new_error_description = "ColorMap tag is missing.";
         return FAILURE;   // color map must be present for GEOTIFF_RGB_PALETTE
      }
   }

   // min sample value (default value is 0)
   if( find_tag( GEOTIFF_MIN_SAMPLE_VALUE_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_min_sample_value = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "Min sample value tag is invalid data type.";
            return FAILURE;
      }
   }
   else
      m_min_sample_value = 0;   // default value

   // max sample value (default is all bits set)
   if( find_tag( GEOTIFF_MAX_SAMPLE_VALUE_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_max_sample_value = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "Max sample value tag is invalid data type.";
            return FAILURE;
      }
   }
   else
      m_max_sample_value = (unsigned short) ((2 << m_bits_per_sample) - 1);    // default value

   // check for valid max/min sample values
   // use defaults if invalid
   if( m_max_sample_value <= m_min_sample_value )
   {
      m_min_sample_value = 0;
      m_max_sample_value = (unsigned short) ((2 << m_bits_per_sample) - 1);
   }

//   remove before flight!
//   m_min_sample_value = 0;
//   m_max_sample_value = 2047;

   // determine image type
   switch( m_photometric_interpretation )
   {
      case GEOTIFF_WHITE_IS_ZERO:
         switch( m_bits_per_sample )
         {
            case 1:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_1BIT;
               m_image_type_description = "BiLevel Image with White = Zero";
            m_new_error_description = "This is a 1-bit Tiff which is not currently supported";
               break;
            case 4:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description =
                  "16-Shade Grayscale Image with White = Zero";
            m_new_error_description = "This is a 4-bit Tiff which is not currently supported";
               break;
            case 8:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_256_GRAYSCALE;
               m_image_type_description =
                  "256-Shade Grayscale Image with White = Zero";
               break;
            case 16:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE;
               m_image_type_description =
                  "16-Bit Grayscale Image with White = Zero";
               break;
            default:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "Image type is unknown.";
               m_new_error_description = "Image type is unknown.";
               return FAILURE;            // others not supported
         }
         break;
      case GEOTIFF_BLACK_IS_ZERO:
         switch( m_bits_per_sample )
         {
            case 1:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_1BIT;
               m_image_type_description = "BiLevel Image with Black = Zero";
            m_new_error_description = "This is a 1-bit Tiff which is not currently supported";
               break;
            case 4:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description =
                  "16-Shade Grayscale Image with Black = Zero";
            m_new_error_description = "This is a 4-bit Tiff which is not currently supported";
               break;
            case 8:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_256_GRAYSCALE;
               m_image_type_description =
                  "256-Shade Grayscale Image with Black = Zero";
               break;
            case 16:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE;
               m_image_type_description =
                  "16-Bit Grayscale Image with Black = Zero";
               break;
            case 32:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_32BIT_FLOAT;
               m_image_type_description =
                  "32-Bit Float Elevation Data";
            m_has_elevation_data = TRUE;
               break;
            default:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "Image type is unknown.";
               m_new_error_description = "Image type is unknown.";
               return FAILURE;            // others not supported
         }
         break;
      case GEOTIFF_RGB:
     case GEOTIFF_YCBCR:
         switch( m_bits_per_sample )
         {
            case 8:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_24BIT_COLOR;
               m_image_type_description = "24-Bit RGB Image";
               break;
            case 16:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_48BIT_COLOR;
               m_image_type_description = "48-Bit RGB Image";
               break;
            default:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "Image type is unknown.";
               m_new_error_description = "Image type unknown.";
               return FAILURE;                      // only 8-bit supported
         }
         break;
      case GEOTIFF_RGB_PALETTE:
         switch( m_bits_per_sample )
         {
            case 4:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "16-Color Palette Image";
            m_new_error_description = "This is a 4-bit Tiff which is not currently supported";
               break;
            case 8:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_256_COLOR;
               m_image_type_description = "256-Color Palette Image";
               break;
            default:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "Image type is unknown.";
               m_new_error_description = "Image type unknown.";
               return FAILURE;                      // only 8-bit supported
         }
         break;
      case GEOTIFF_CMYK:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_24BIT_COLOR;
               m_image_type_description = "24-Bit RGB Image";
            break;
      default:
         m_image_type_supported = FALSE;
         m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
         m_image_type_description = "Image type is unknown.";
         m_new_error_description = "Image type is unknown.";
         return FAILURE;
   }

   // check for case of large strips
   // in this case we try to change to lots of small strips
   // for more efficient reading
   if( m_image_type_supported &&
       m_compression_scheme == GEOTIFF_NO_COMPRESSION &&
       m_rows_per_strip > 5 &&
       m_max_strip_byte_count > 1000000 )
   {
      int strip_byte_counts_tag_index, strip_offsets_tag_index;
      int bytes_per_pixel;
      unsigned int istrip, inewstrip, irow, row_offset;
      unsigned int strip_byte_count, strip_offset;
      unsigned int new_rows_per_strip;
      unsigned int new_num_strips;
      unsigned int new_max_strip_byte_count;
      CTiffTag new_strip_byte_counts_tag;
      CTiffTag new_strip_offsets_tag;

      new_rows_per_strip = 1;
      new_num_strips = m_image_length;
    strip_offset = 0;


      switch( m_image_type )
      {
         case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
         case GEOTIFF_IMAGE_TYPE_256_COLOR:
            bytes_per_pixel = 1;
            break;
         case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
            bytes_per_pixel = 2;
            break;
         case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
            bytes_per_pixel = 3;
            break;
         default:
            goto DONE;
      }
      new_max_strip_byte_count = m_image_width * bytes_per_pixel;

      // find strip byte count and offsets tags
      find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index );
      find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index );

      // create new strip byte counts tag
      new_strip_byte_counts_tag.m_defined = TRUE;
      new_strip_byte_counts_tag.m_tag_id = GEOTIFF_STRIP_BYTE_COUNTS_TAG;
      new_strip_byte_counts_tag.m_type = GEOTIFF_LONG;
      new_strip_byte_counts_tag.m_count = new_num_strips;
      new_strip_byte_counts_tag.m_value_offset =
         m_tags[strip_byte_counts_tag_index].m_value_offset;
      new_strip_byte_counts_tag.m_tag_name =
         m_tags[strip_byte_counts_tag_index].m_tag_name;
      new_strip_byte_counts_tag.m_type_name = "LONG";

      new_strip_byte_counts_tag.m_long_values =
      (unsigned int*)MEM_malloc( new_num_strips*sizeof(unsigned int) );

      // create new strip offsets tag
      new_strip_offsets_tag.m_defined = TRUE;
      new_strip_offsets_tag.m_tag_id = GEOTIFF_STRIP_OFFSETS_TAG;
      new_strip_offsets_tag.m_type = GEOTIFF_LONG;
      new_strip_offsets_tag.m_count = new_num_strips;
      new_strip_offsets_tag.m_value_offset =
         m_tags[strip_offsets_tag_index].m_value_offset;
      new_strip_offsets_tag.m_tag_name =
         m_tags[strip_offsets_tag_index].m_tag_name;
      new_strip_offsets_tag.m_type_name = "LONG";
      new_strip_offsets_tag.m_long_values =
      (unsigned int*)MEM_malloc( new_num_strips*sizeof(unsigned int) );

      // fill in new strip byte counts and offsets
      inewstrip = 0;
      for( istrip = 0; istrip < m_num_strips; istrip++ )
      {
         // Get the strip offset
         GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset );

         // Get the strip byte count
         GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count );

         for( irow = 0; irow < m_rows_per_strip; irow++ )
         {
            row_offset = strip_offset + irow * new_max_strip_byte_count;
            new_strip_byte_counts_tag.m_long_values[inewstrip] =
               new_max_strip_byte_count;
            new_strip_offsets_tag.m_long_values[inewstrip] =
               row_offset;
            inewstrip++;
            if( inewstrip == new_num_strips )
               break;
         }
      }

      // assign the value name strings
      new_strip_byte_counts_tag.m_value_name =
         get_tag_value_name( new_strip_byte_counts_tag );
      new_strip_offsets_tag.m_value_name =
         get_tag_value_name( new_strip_offsets_tag );

      // now replace the old byte count and offset tags with the new ones
      m_tags[strip_byte_counts_tag_index].clear( );
      m_tags[strip_byte_counts_tag_index] = new_strip_byte_counts_tag;
      new_strip_byte_counts_tag.m_long_values = NULL;
      new_strip_byte_counts_tag.clear( );

      m_tags[strip_offsets_tag_index].clear( );
      m_tags[strip_offsets_tag_index] = new_strip_offsets_tag;
      new_strip_offsets_tag.m_long_values = NULL;
      new_strip_offsets_tag.clear( );

      // set the new values
      m_rows_per_strip = new_rows_per_strip;
      m_num_strips = new_num_strips;
      m_max_strip_byte_count = new_max_strip_byte_count;
   }

DONE:
   return SUCCESS;
}
// end of inspect_tags

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::read_geokey_directory( )
{
   // this function finds the geokey directory, if present, and
   // reads the geokey directory and all geokeys
   // returns SUCCESS or FAILURE

   int i, tag_index, value_index;
   int j, itag, offset, count;
   const int STR_LEN = 50;
   char string[STR_LEN];

   //TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);

   // find the index of the geokey directory if present
   if( find_tag( GEOTIFF_GEOKEY_DIRECTORY_TAG, tag_index ) == FAILURE )
      return SUCCESS;   // if geokey directory is not present,
                        // return SUCCESS anyway

   // verify that tag data is of type GEOTIFF_SHORT
   if( m_tags[tag_index].m_type != GEOTIFF_SHORT )
   {
      m_new_error_description = "GeoKeyDirectoryTag tag is invalid data type.";
      return FAILURE;
   }

   // verify that tag contains at least enough data for the geokey directory
   // header
   if( m_tags[tag_index].m_count < 4 )
   {
      m_new_error_description =
         "GeoKeyDirectoryTag tag does not contain geokey directory header.";
      return FAILURE;
   }

   // get geokey directory header info from tag array
   m_geokey_directory_version = m_tags[tag_index].m_short_values[0];
   m_geokey_directory_revision = m_tags[tag_index].m_short_values[1];
   m_geokey_directory_minor_revision = m_tags[tag_index].m_short_values[2];
   m_num_geokeys = m_tags[tag_index].m_short_values[3];

   // if number of geokeys is zero, return SUCCESS
   if( m_num_geokeys == 0 )
      return SUCCESS;

   // see if the tag has enough data for the directory header and all the
   // geokey entries
   if( (int)m_tags[tag_index].m_count < 4 * ( m_num_geokeys + 1 ) )
   {
      m_new_error_description =
         "GeoKeyDirectoryTag tag has incorrect data count.";
      return FAILURE;
   }

   // allocate memory for geokeys
   m_geokeys = new CGeoKey[m_num_geokeys];

   // index into the tag data array
   value_index = 4;

   // read each geokey entry from the tag array data
   for( i = 0; i < m_num_geokeys; i++ )
   {
      m_geokeys[i].clear( );
      m_geokeys[i].m_geokey_id = m_tags[tag_index].m_short_values[value_index];
      value_index++;
      m_geokeys[i].m_tiff_tag_location =
         m_tags[tag_index].m_short_values[value_index];
      value_index++;
      m_geokeys[i].m_count = m_tags[tag_index].m_short_values[value_index];
      value_index++;
      m_geokeys[i].m_value_offset =
         m_tags[tag_index].m_short_values[value_index];
      value_index++;

      // check for data contained in value offset (m_tiff_tag_location = 0)
      if( m_geokeys[i].m_tiff_tag_location == 0 )
      {
         // data is a single unsigned short contained in value_offset
         m_geokeys[i].m_count = 1;
         m_geokeys[i].m_type = GEOTIFF_SHORT;
         m_geokeys[i].m_short_values =
            (unsigned short*)MEM_malloc( sizeof(unsigned short) );
         m_geokeys[i].m_short_values[0] = m_geokeys[i].m_value_offset;
      }
      else
      {
         // data is contained in the tag indicated by tiff tag location

         // check that indicated tag exists by finding its index itag
         itag = -1;
         for( j = 0; j < m_num_tags; j++ )
         {
            if( m_tags[j].m_tag_id == m_geokeys[i].m_tiff_tag_location )
            {
               itag = j;
               break;
            }
         }
         if( itag == -1 )
         {
            sprintf_s( string, STR_LEN, "Geokey %i references non-existant tag.", i );
            m_new_error_description = string;
            return FAILURE;
         }

         // offset, and count are used for shorthand
         offset = m_geokeys[i].m_value_offset;
         count = m_geokeys[i].m_count;

         // check for count compatibility
         if( (unsigned int)(offset + count) > m_tags[itag].m_count )
         {
            sprintf_s( string, STR_LEN, "Geokey %i has invalid data count", i );
            m_new_error_description = string;
            return FAILURE;
         }

         // data type is the type of the tag data
         m_geokeys[i].m_type = m_tags[itag].m_type;

         // allocate memory for data and copy from tag array
         switch( m_geokeys[i].m_type )
         {
            case GEOTIFF_ASCII:             // null-terminated string
               m_geokeys[i].m_ascii_values = (char*)MEM_malloc( count );
               // copy data from tag at value offset
               for( j = 0; j < count; j++ )
                  m_geokeys[i].m_ascii_values[j] =
                     m_tags[itag].m_ascii_values[offset+j];
               // add terminating null
               m_geokeys[i].m_ascii_values[count-1] = NULL;
               break;
            case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
               m_geokeys[i].m_short_values =
                  (unsigned short*)MEM_malloc( count * sizeof(unsigned short) );
               // copy data from tag at value offset
               for( j = 0; j < count; j++ )
                  m_geokeys[i].m_short_values[j] =
                  m_tags[itag].m_short_values[offset+j];
               break;
            case GEOTIFF_DOUBLE:            // 8-byte double value
               m_geokeys[i].m_double_values =
                  (double*)MEM_malloc( count * sizeof(double) );
               // copy data from tag at value offset
               for( j = 0; j < count; j++ )
                  m_geokeys[i].m_double_values[j] =
                     m_tags[itag].m_double_values[offset+j];
               break;
            default:
               // data type is unknown - don't read values
               break;
         }

      }

      // assign names to geokey, type, and value
      m_geokeys[i].m_geokey_name = get_geokey_name( m_geokeys[i].m_geokey_id );
      m_geokeys[i].m_type_name = get_type_name( m_geokeys[i].m_type );
      m_geokeys[i].m_value_name = get_geokey_value_name( m_geokeys[i] );

      // geokey is now completely defined
      m_geokeys[i].m_defined = TRUE;

   }

   return SUCCESS;
}
// end of read_geokey_directory

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::find_geokey( unsigned short geokey_id, int &index )
{
   // finds the index of the specified geokey_id in the geokey array
   // returns SUCCESS or FAILURE

   for( index = 0; index < m_num_geokeys; index++ )
      if( m_geokeys[index].m_geokey_id == geokey_id )
        return SUCCESS;

   index = -1;
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

#pragma intrinsic(memcpy,memset)


int __fastcall CGeoTiff::decompress_packbits( int compressed_size,
                                               BYTE* compressed,
                                               int decompressed_size,
                                               BYTE* decompressed)
{
   // this function decompresses an array of bytes using the packbits algorithm
   // compressed_size is the number of bytes in the compressed array - not all
   // of the compressed bytes will necessarily be used.
   // decompressed_size is the number of bytes expected from the
   // decompression -  normally determind by the number of rows in a strip of
   // data
   // the decompressed data is stored in the decompressed array
   // returns SUCCESS or FAILURE

   size_t count;
   int sbyte;

   BYTE *pComp = compressed;
   BYTE *pCompEnd = pComp + compressed_size;
   BYTE *pDecomp = decompressed;
   BYTE *pDecompEnd = pDecomp + decompressed_size;

   while( pComp < pCompEnd )



   {
      // get a signed byte from the compressed array and increment index
      sbyte = (int)*(signed char *)pComp++;     // Oh, I love this line (need it to get sign bit extension)


      if( sbyte == -128 )
      {
         if( ++pComp >= pCompEnd)



            return FAILURE;

         continue;
      }

      if( sbyte >= 0)
      {
         // if sbyte is positive or zero, we copy the next sbyte+1 literally
         count = sbyte + 1;

         // check for overflow
         if( pComp + count > pCompEnd)


            return FAILURE;
         if( pDecomp + count > pDecompEnd)
            count = pDecompEnd - pDecomp;


         // copy the data
         memcpy(pDecomp, pComp, count);

         // increment indices
         pComp += count;
         pDecomp += count;

         // check for done
         if( pDecomp == pDecompEnd)
            return SUCCESS;

         continue;
      }


      // otherwise, if sbyte is negative, we duplicate the next 1-sbyte times
      count = 1 - sbyte;

      // check for overflow
      if( pComp >= pCompEnd)


         return FAILURE;
      if( pDecomp + count > pDecompEnd)
         count = pDecompEnd - pDecomp;


      // duplicate the data
      memset(pDecomp, *pComp++, count);

      // increment indices
      pDecomp += count;


      // check for done
      if( pDecomp == pDecompEnd)
         return SUCCESS;



   }

   //   ERR_report("Error reading packbits block");
//   ASSERT(0);
   // if we run out of compressed bytes before finishing, return FAILURE
   m_new_error_description = "Error in packbits compressed image data";
   return FAILURE;
}

// ********************************************************************************************

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::decompress_lzw( int compressed_size,
                               unsigned char *compressed,
                               int decompressed_size,
                               unsigned char *decompressed )
{
   // this function decompresses an array of bytes using the TIFF 6.0 variant
   // of LZW (compression tag 5).  Same contract as decompress_packbits:
   // compressed_size is the number of bytes available - not all of them will
   // necessarily be used; decompressed_size is the number of bytes expected,
   // and decoding stops as soon as that many have been produced.
   // returns SUCCESS or FAILURE
   //
   // Until 2026-08-27 this function was a copy of decompress_packbits with
   // the error strings renamed - it never decoded LZW.  Nothing reached it,
   // because the compression tag rejected LZW before any reader ran.
   //
   // Codes are packed most-significant-bit first (fill order 2 is rejected at
   // load time, so there is no bit-reversed case to handle here).  Widths run
   // 9..12 bits and step up ONE CODE EARLY - the off-by-one in the TIFF 6.0
   // pseudocode that every encoder in the wild reproduces.  Codes 256 and 257
   // are Clear and EndOfInformation; 258 up are string table entries.

   const int clear_code = 256;
   const int eoi_code = 257;
   const int first_entry = 258;
   const int max_entries = 4096;

   // the string table: each entry is a prefix code plus one suffix byte, so a
   // string is walked backwards down the prefix chain and emitted reversed
   short prefix[max_entries];
   unsigned char suffix[max_entries];
   unsigned char reversed[max_entries];

   int i, code, old_code, code_width, next_entry;
   int walk, nbytes;
   unsigned int bit_pos, bit_end, byte_index, accum;
   unsigned char first_char;
   unsigned char *decomp = decompressed;
   unsigned char *decomp_end = decomp + decompressed_size;

   if( compressed_size <= 0 || decompressed_size <= 0 )
   {
      m_new_error_description = "Error in LZW compressed image data";
      return FAILURE;
   }

   for( i = 0; i < 256; i++ )
   {
      prefix[i] = -1;
      suffix[i] = (unsigned char)i;
   }

   bit_pos = 0;
   bit_end = (unsigned int)compressed_size * 8;

   code_width = 9;
   next_entry = first_entry;
   old_code = -1;
   first_char = 0;

   while( bit_pos + (unsigned int)code_width <= bit_end )
   {
      // read code_width bits, most significant bit first
      byte_index = bit_pos >> 3;
      accum = (unsigned int)compressed[byte_index] << 16;
      accum |= (unsigned int)compressed[byte_index+1] << 8;
      if( byte_index + 2 < (unsigned int)compressed_size )
         accum |= (unsigned int)compressed[byte_index+2];
      code = (int)( ( accum >> ( 24 - ( bit_pos & 7 ) - code_width ) ) &
                    ( ( 1u << code_width ) - 1 ) );
      bit_pos += (unsigned int)code_width;

      if( code == eoi_code )
         break;

      if( code == clear_code )
      {
         code_width = 9;
         next_entry = first_entry;
         old_code = -1;
         continue;
      }

      if( old_code == -1 )
      {
         // first code after a Clear must be a literal
         if( code >= 256 )
         {
            m_new_error_description = "Error in LZW compressed image data";
            return FAILURE;
         }
         if( decomp >= decomp_end )
            return SUCCESS;
         *decomp++ = (unsigned char)code;
         if( decomp == decomp_end )
            return SUCCESS;
         old_code = code;
         first_char = (unsigned char)code;
         continue;
      }

      if( code < next_entry )
      {
         // the code is in the table: emit its string, then add old_code plus
         // that string's first character as the next entry
         walk = code;
      }
      else if( code == next_entry && next_entry < max_entries )
      {
         // the KwKwK case: the encoder used an entry it had only just added,
         // so the string is old_code's string plus its own first character
         walk = -1;
      }
      else
      {
         m_new_error_description = "Error in LZW compressed image data";
         return FAILURE;
      }

      if( walk >= 0 )
      {
         nbytes = 0;
         while( walk >= 0 && nbytes < max_entries )
         {
            reversed[nbytes++] = suffix[walk];
            walk = prefix[walk];
         }
         if( walk >= 0 )
         {
            m_new_error_description = "Error in LZW compressed image data";
            return FAILURE;                   // corrupt prefix chain
         }
         first_char = reversed[nbytes-1];
      }
      else
      {
         nbytes = 0;
         walk = old_code;
         while( walk >= 0 && nbytes < max_entries )
         {
            reversed[nbytes++] = suffix[walk];
            walk = prefix[walk];
         }
         if( walk >= 0 || nbytes >= max_entries )
         {
            m_new_error_description = "Error in LZW compressed image data";
            return FAILURE;
         }
         first_char = reversed[nbytes-1];
         // prepend (in reversed order, append) the leading character
         for( i = nbytes; i > 0; i-- )
            reversed[i] = reversed[i-1];
         reversed[0] = first_char;
         nbytes++;
      }

      for( i = nbytes - 1; i >= 0; i-- )
      {
         if( decomp >= decomp_end )
            return SUCCESS;                   // caller wanted only this much
         *decomp++ = reversed[i];
      }

      // add old_code + first character of what we just emitted
      if( next_entry < max_entries )
      {
         prefix[next_entry] = (short)old_code;
         suffix[next_entry] = first_char;
         next_entry++;
      }

      old_code = code;

      // "early change": the width steps up one code before the table is full
      if( next_entry + 1 >= ( 1 << code_width ) && code_width < 12 )
         code_width++;

      if( decomp == decomp_end )
         return SUCCESS;
   }

   // ran out of codes: SUCCESS only if we produced everything asked for
   if( decomp == decomp_end )
      return SUCCESS;

   m_new_error_description = "Error in LZW compressed image data";
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::undo_horizontal_predictor( unsigned char *data, int size,
                                         int row_bytes )
{
   // undoes TIFF predictor 2 (horizontal differencing) in place.  Each sample
   // was stored as its difference from the sample one PIXEL to its left, so
   // the stride is samples-per-pixel and each row restarts.
   // returns SUCCESS or FAILURE

   int row_start, i, spp;

   if( row_bytes <= 0 )
      return FAILURE;

   spp = (int)m_samples_per_pixel;
   if( spp <= 0 )
      return FAILURE;

   if( m_bits_per_sample == 8 )
   {
      for( row_start = 0; row_start < size; row_start += row_bytes )
      {
         int row_len = __min( row_bytes, size - row_start );
         unsigned char *row = data + row_start;
         for( i = spp; i < row_len; i++ )
            row[i] = (unsigned char)( row[i] + row[i-spp] );
      }
      return SUCCESS;
   }

   if( m_bits_per_sample == 16 )
   {
      // differencing is over 16-bit samples; the bytes are in file order, so
      // reassemble each sample with the file's byte order and write it back
      int stride = spp * 2;
      for( row_start = 0; row_start < size; row_start += row_bytes )
      {
         int row_len = __min( row_bytes, size - row_start );
         unsigned char *row = data + row_start;
         for( i = stride; i + 1 < row_len; i += 2 )
         {
            unsigned int prev, cur;
            if( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               prev = ( (unsigned int)row[i-stride] << 8 ) | row[i-stride+1];
               cur  = ( (unsigned int)row[i] << 8 ) | row[i+1];
               cur = ( cur + prev ) & 0xFFFF;
               row[i]   = (unsigned char)( cur >> 8 );
               row[i+1] = (unsigned char)( cur & 0xFF );
            }
            else
            {
               prev = ( (unsigned int)row[i-stride+1] << 8 ) | row[i-stride];
               cur  = ( (unsigned int)row[i+1] << 8 ) | row[i];
               cur = ( cur + prev ) & 0xFFFF;
               row[i+1] = (unsigned char)( cur >> 8 );
               row[i]   = (unsigned char)( cur & 0xFF );
            }
         }
      }
      return SUCCESS;
   }

   return FAILURE;                             // predictor 2 needs 8 or 16 bpp
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::decompress_strip( int compressed_size,
                                unsigned char *compressed,
                                int decompressed_size,
                                unsigned char *decompressed )
{
   // one entry point for every reader below: pick the codec, then undo the
   // predictor.  With PackBits and predictor 1 - everything this reader
   // handled before LZW was enabled - it is decompress_packbits and nothing
   // else, byte for byte.
   // returns SUCCESS or FAILURE

   int row_bytes;

   switch( m_compression_scheme )
   {
      case GEOTIFF_PACKBITS:
         if( decompress_packbits( compressed_size, compressed,
                                  decompressed_size, decompressed ) != SUCCESS )
            return FAILURE;
         break;
      case GEOTIFF_LZW:
         if( decompress_lzw( compressed_size, compressed,
                             decompressed_size, decompressed ) != SUCCESS )
            return FAILURE;
         break;
      default:
         return FAILURE;
   }

   if( m_predictor == 2 )
   {
      // a row of the strip, or of the tile when the image is tiled
      row_bytes = (int)( m_image_is_tiled ? m_tile_width : m_image_width ) *
                  (int)m_samples_per_pixel * ( (int)m_bits_per_sample / 8 );
      if( undo_horizontal_predictor( decompressed, decompressed_size,
                                     row_bytes ) != SUCCESS )
      {
         m_new_error_description = "Predictor could not be undone";
         return FAILURE;
      }
   }

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_grayscale_as_rgb_image( unsigned char *img, IImageLibCallback *callback )
{
   // this function reads a grayscale (8-bit monochrome) image into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int pixel_index, decompressed_size, num_remaining_bytes;
   unsigned int strip_offset, strip_byte_count;
   unsigned char *strip_data, *decompressed_strip_data;
   CUtil util;

   strip_data = NULL;
   decompressed_strip_data = NULL;
   decompressed_size = 0;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
     if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   pixel_index = 0;

   // loop through each strip
   for( istrip = 0; istrip < (int)m_num_strips; istrip++ )
   {
      double percent = 100.0 * (double) istrip / (double) m_num_strips;
      CString label = "Loading Image";
      if (!util.send_user_update_and_continue(callback, percent, label))
      {
         m_new_error_description = "Operation canceled by user";
         goto FAIL;
      }
      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;


      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
      {
        m_new_error_description = "File read error";
        goto FAIL;
     }

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // copy strip data to pixel array
            for( i = 0; i < (int)strip_byte_count; i++ )
            {
               img[pixel_index*3+0] = strip_data[i];
               img[pixel_index*3+1] = strip_data[i];
               img[pixel_index*3+2] = strip_data[i];
               pixel_index += 3;
               if( pixel_index >= m_num_pixels*3 )
               goto DONE;
            }
            break;
         case GEOTIFF_CCITT_1D:
          m_new_error_description = "This file uses CCITT compression which is not supported";
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            if( m_compression_scheme == GEOTIFF_PACKBITS ||
                m_compression_scheme == GEOTIFF_LZW )
            {
               num_remaining_bytes = (m_num_pixels - pixel_index) *
                                     m_samples_per_pixel;
               decompressed_size = __min( (int)(m_image_width *
                                   m_rows_per_strip * m_samples_per_pixel),
                                   num_remaining_bytes );
               if( decompress_strip( (int)strip_byte_count, strip_data,
                   decompressed_size, decompressed_strip_data ) != SUCCESS )
            {
             m_new_error_description = "Error in packbits compressed data";
                   goto FAIL;
            }
            }
            for( i = 0; (unsigned int) i < decompressed_size; i++ )
            {
               img[pixel_index*3+0] = decompressed_strip_data[i];
               img[pixel_index*3+1] = decompressed_strip_data[i];
               img[pixel_index*3+2] = decompressed_strip_data[i];
               pixel_index += 3;
               if( pixel_index >= m_num_pixels*3 )
               goto DONE;
            }
            break;
         default:
          m_new_error_description = "This file uses an unsupported compression scheme";
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
   // deallocate strip data array
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   // check for reversed grayscale
   if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
   {
      for( i = 0; i < (int)m_num_pixels*3; i++ )
     {
         img[i] = (BYTE) (255 - img[i]);
     }
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_8bit_grayscale_as_rgb_image

// ********************************************************************************************
// ********************************************************************************************
/*
int CGeoTiff::get_8bit_grayscale_as_palette_image( unsigned char *img, IImageLibCallback *callback )
{
   // this function reads a grayscale (8-bit monochrome) image into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   int pixel_index, decompressed_size, num_remaining_bytes;
   unsigned int strip_offset, strip_byte_count;
   unsigned char *strip_data, *decompressed_strip_data;
   CUtil util;

   strip_data = NULL;
   decompressed_strip_data = NULL;
   decompressed_size = 0;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
     if (decompressed_strip_data == NULL)
      {
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   pixel_index = 0;

   // loop through each strip
   for( istrip = 0; istrip < (int)m_num_strips; istrip++ )
   {
      double percent = 100.0 * (double) istrip / (double) m_num_strips;
      CString label = "Loading Image";
      if (!util.send_user_update_and_continue(callback, percent, label))
      {
         m_new_error_description = "Operation canceled by user";
         goto FAIL;
      }
      // get the strip offset
      switch( m_tags[strip_offsets_tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            strip_offset =
               m_tags[strip_offsets_tag_index].m_short_values[istrip];
            break;
         case GEOTIFF_LONG:
            strip_offset =
               m_tags[strip_offsets_tag_index].m_long_values[istrip];
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }
      // get the strip byte count
      switch( m_tags[strip_byte_counts_tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            strip_byte_count =
               m_tags[strip_byte_counts_tag_index].m_short_values[istrip];
            break;
         case GEOTIFF_LONG:
            strip_byte_count =
               m_tags[strip_byte_counts_tag_index].m_long_values[istrip];
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }

      // set the file pointer to the row offset
      if( fseek( m_file_ptr, strip_offset, SEEK_SET ) != 0 )
     {
        m_new_error_description = "File seek error";
        goto FAIL;
     }

      // read the strip
      if( gtfRead( strip_data, strip_byte_count, 1, m_file_ptr ) != 1 )
     {
        m_new_error_description = "File read error";
        goto FAIL;
     }

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // copy strip data to pixel array
            for( i = 0; i < (int)strip_byte_count; i++ )
            {
               img[pixel_index*3+0] = strip_data[i];
               img[pixel_index*3+1] = strip_data[i];
               img[pixel_index*3+2] = strip_data[i];
               pixel_index += 3;
               if( pixel_index >= (int)m_num_pixels*3 )
               goto DONE;
            }
            break;
         case GEOTIFF_CCITT_1D:
          m_new_error_description = "This file uses CCITT compression which is not supported";
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            if( m_compression_scheme == GEOTIFF_PACKBITS ||
                m_compression_scheme == GEOTIFF_LZW )
            {
               num_remaining_bytes = (m_num_pixels - pixel_index) *
                                     m_samples_per_pixel;
               decompressed_size = __min( (int)(m_image_width *
                                   m_rows_per_strip * m_samples_per_pixel),
                                   num_remaining_bytes );
               if( decompress_strip( (int)strip_byte_count, strip_data,
                   decompressed_size, decompressed_strip_data ) != SUCCESS )
            {
             m_new_error_description = "Error in packbits compressed data";
                   goto FAIL;
            }
            }
            for( i = 0; i < decompressed_size; i++ )
            {
               img[pixel_index*3+0] = decompressed_strip_data[i];
               img[pixel_index*3+1] = decompressed_strip_data[i];
               img[pixel_index*3+2] = decompressed_strip_data[i];
               pixel_index += 3;
               if( pixel_index >= (int)m_num_pixels*3 )
               goto DONE;
            }
            break;
         default:
          m_new_error_description = "This file uses an unsupported compression scheme";
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
   // deallocate strip data array
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   // check for reversed grayscale
   if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
   {
      for( i = 0; i < (int)m_num_pixels*3; i++ )
     {
         img[i] = (BYTE) (255 - img[i]);
     }
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_8bit_grayscale_as_palette_image
*/
// ********************************************************************************************
// ********************************************************************************************

/*
int CGeoTiff::get_8bit_grayscale_as_palette_image(
      CColorQuantizer &color_quantizer, unsigned char *indices )
{
   // this function returns the palette indices for the image pixels in the
   // argument indices based on the values in the color_quantizer argument
   // returns SUCCESS or FAILURE

   int i, j, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   int pixel_index, decompressed_size, num_remaining_bytes;
   int diff;
   unsigned int strip_offset, strip_byte_count;
   unsigned int color_value;
   unsigned char red, green, blue;
   unsigned char *strip_data, *decompressed_strip_data;
   double value;
   unsigned char cross_reference[256];

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index )
       != SUCCESS ) goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index )
       != SUCCESS ) goto FAIL;

   // allocate data for strips
   strip_data = NULL;
//   strip_data = new unsigned char[m_max_strip_byte_count];
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );

   // if compressed, allocate memory for a decompressed strip
   decompressed_strip_data = NULL;
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
//      decompressed_strip_data =
//         new unsigned char[m_image_width * m_rows_per_strip *
//                           m_samples_per_pixel];
      decompressed_strip_data =
         (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip *
                           m_samples_per_pixel );
   }

   // if necessary, create cross-reference table
   // by finding nearest grayscale entry
   if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
   {
      for( i = 0; i < 256; i++ )
      {
         cross_reference[i] = 0;
         diff = 256;
         for( j = 0; j < color_quantizer.m_num_colors; j++ )
         {
            red = color_quantizer.m_red[j];
            green = color_quantizer.m_green[j];
            blue = color_quantizer.m_blue[j];
            if( green == red && blue == red )
            {
               if( abs( i - red ) < diff )
               {
                  cross_reference[i] = j;
                  diff = abs( i - red );
                  if( diff == 0 )
                     break;
               }
            }
         }
      }
   }

   pixel_index = 0;

   // loop through each strip
   for( istrip = 0; istrip < (int)m_num_strips; istrip++ )
   {
      // get the strip offset
      switch( m_tags[strip_offsets_tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            strip_offset =
               m_tags[strip_offsets_tag_index].m_short_values[istrip];
            break;
         case GEOTIFF_LONG:
            strip_offset =
               m_tags[strip_offsets_tag_index].m_long_values[istrip];
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }
      // get the strip byte count
      switch( m_tags[strip_byte_counts_tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            strip_byte_count =
               m_tags[strip_byte_counts_tag_index].m_short_values[istrip];
            break;
         case GEOTIFF_LONG:
            strip_byte_count =
               m_tags[strip_byte_counts_tag_index].m_long_values[istrip];
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }

      // set the file pointer to the row offset
      if( fseek( m_file_ptr, strip_offset, SEEK_SET ) != 0 ) goto FAIL;

      // read the strip
      if( fread( strip_data, strip_byte_count, 1, m_file_ptr ) != 1 ) goto FAIL;

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // copy strip data to pixel array
            for( i = 0; i < (int)strip_byte_count; i++ )
            {
               red = strip_data[i];
               // check for reversed grayscale
               if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                  red = 255 - red;
               if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
               {
                  value = (double)(red) * (color_quantizer.m_num_colors-1) /
                          255.0 + 0.5;
                  indices[pixel_index] = (unsigned char) floor( value );
               }
               else
               {
                  if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                     indices[pixel_index] =
                     cross_reference[red];
                  else
                  {
                     green = blue = red;
                     color_value = (red >> 3) << 10;
                     color_value |= (green >> 3) << 5;
                     color_value |= (blue >> 3);
                     indices[pixel_index] =
                        color_quantizer.m_histogram_indices[color_value];
                  }
               }
               pixel_index++;
               if( pixel_index >= (int)m_num_pixels ) goto DONE;
            }
            break;
         case GEOTIFF_CCITT_1D:
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            if( m_compression_scheme == GEOTIFF_PACKBITS ||
                m_compression_scheme == GEOTIFF_LZW )
            {
               num_remaining_bytes =
                  (m_num_pixels - pixel_index) * m_samples_per_pixel;
               decompressed_size =
                  __min( (int)(m_image_width * m_rows_per_strip *
                  m_samples_per_pixel), num_remaining_bytes );
               if( decompress_strip( (int)strip_byte_count, strip_data,
                   decompressed_size, decompressed_strip_data ) != SUCCESS )
                   goto FAIL;
            }
            for( i = 0; i < decompressed_size; i++ )
            {
               red = decompressed_strip_data[i];
               // check for reversed grayscale
               if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                  red = 255 - red;
               if( color_quantizer.m_method == COLOR_QUANTIZE_GRAYSCALE )
               {
                  value = (double)(red) * (color_quantizer.m_num_colors-1) /
                          255.0 + 0.5;
                  indices[pixel_index] = (unsigned char) floor( value );
               }
               else
               {
                  if( color_quantizer.m_method == COLOR_QUANTIZE_PALETTE )
                     indices[pixel_index] =
                     cross_reference[red];
                  else
                  {
                     green = blue = red;
                     color_value = (red >> 3) << 10;
                     color_value |= (green >> 3) << 5;
                     color_value |= (blue >> 3);
                     indices[pixel_index] =
                        color_quantizer.m_histogram_indices[color_value];
                  }
               }
               pixel_index++;
               if( pixel_index >= (int)m_num_pixels ) goto DONE;
            }
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
//   delete [] strip_data;
   MEM_free( strip_data );
   strip_data = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
//      delete [] decompressed_strip_data;
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
//      delete [] strip_data;
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
//      delete [] decompressed_strip_data;
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
*/
// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_16bit_grayscale_as_rgb_image( unsigned char *img, IImageLibCallback *callback )
{
   // this function reads a 16-bit grayscale image into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int pixel_index, decompressed_size, num_remaining_bytes;
   unsigned int strip_offset, strip_byte_count;
   unsigned char *strip_data, *decompressed_strip_data;
   unsigned char byte1, byte2;
   unsigned short *strip_16bit_data;
   double fraction;
   CUtil util;

   strip_data = NULL;
   decompressed_strip_data = NULL;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( 2 * m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   pixel_index = 0;

   // loop through each strip
   for( istrip = 0; istrip < (int)m_num_strips; istrip++ )
   {
      double percent = 100.0 * (double) istrip / (double) m_num_strips;
      CString label = "Loading Image";
      if (!util.send_user_update_and_continue(callback, percent, label))
      {
         m_new_error_description = "Operation canceled by user";
         goto FAIL;
      }
      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
        goto FAIL;

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // swap byte order if necessary
            if( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               for( i = 0; i < (int)strip_byte_count; i += 2 )
               {
                  byte1 = strip_data[i];
                  byte2 = strip_data[i+1];
                  strip_data[i] = byte2;
                  strip_data[i+1] = byte1;
               }
            }
            // copy strip data to pixel array
            strip_16bit_data = (unsigned short*)strip_data;
            for( i = 0; i < (int)strip_byte_count/2; i++ )
            {
               fraction = (double)(strip_16bit_data[i]-m_min_sample_value)/
                  (m_max_sample_value-m_min_sample_value);
               if( fraction < 0.0 )
                  fraction = 0.0;
               if( fraction > 1.0 )
                  fraction = 1.0;
               img[pixel_index*3+0] = (unsigned char)(255*fraction+0.5);
               img[pixel_index*3+1] = (unsigned char)(255*fraction+0.5);
               img[pixel_index*3+2] = (unsigned char)(255*fraction+0.5);
               pixel_index +=3;
               if( pixel_index >= m_num_pixels*3 )
               goto DONE;
            }
            break;
         case GEOTIFF_CCITT_1D:
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            num_remaining_bytes = 2 * (m_num_pixels - pixel_index) *
                                  m_samples_per_pixel;
            decompressed_size = __min( (int)(2 * m_image_width *
                                m_rows_per_strip * m_samples_per_pixel),
                                num_remaining_bytes );
            if( decompress_strip( (int)strip_byte_count, strip_data,
                decompressed_size, decompressed_strip_data ) != SUCCESS )
                goto FAIL;
            // swap byte order if necessary
            if( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               for( i = 0; i < (int)decompressed_size; i += 2 )
               {
                  byte1 = decompressed_strip_data[i];
                  byte2 = decompressed_strip_data[i+1];
                  decompressed_strip_data[i] = byte2;
                  decompressed_strip_data[i+1] = byte1;
               }
            }
            strip_16bit_data = (unsigned short*)decompressed_strip_data;
            for( i = 0; (unsigned int) i < decompressed_size/2; i++ )
            {
               fraction = (double)(strip_16bit_data[i]-m_min_sample_value)/
                  (m_max_sample_value-m_min_sample_value);
               if( fraction < 0.0 )
                  fraction = 0.0;
               if( fraction > 1.0 )
                  fraction = 1.0;
               img[pixel_index*3+0] = (unsigned char)(255*fraction+0.5);
               img[pixel_index*3+1] = (unsigned char)(255*fraction+0.5);
               img[pixel_index*3+2] = (unsigned char)(255*fraction+0.5);
               pixel_index += 3;
               if( pixel_index >= m_num_pixels*3 )
               goto DONE;
            }
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
   // deallocate strip data array
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   // check for reversed grayscale
   if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
   {
      for( i = 0; i < (int)m_num_pixels*3; i++ )
         img[i] = (BYTE) (255 - img[i]);
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_16bit_grayscale_as_rgb_image

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_24bit_rgb_as_rgb_image( unsigned char *img, IImageLibCallback *callback )
{
   // this function reads a 24-bit rgb image into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int pixel_index, ibyte, decompressed_size, num_remaining_bytes;
   unsigned int strip_offset, strip_byte_count, psize;
   unsigned char *strip_data, *decompressed_strip_data;
   CUtil util;

   strip_data = NULL;
   decompressed_strip_data = NULL;
   decompressed_size = 0;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   pixel_index = 0;

   // loop through each strip
   for( istrip = 0; istrip < (int)m_num_strips; istrip++ )
   {
      double percent = 100.0 * (double) istrip / (double) m_num_strips;
      CString label = "Loading Image";
      if (!util.send_user_update_and_continue(callback, percent, label))
      {
         m_new_error_description = "Operation canceled by user";
         goto FAIL;
      }
      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
        goto FAIL;

     psize = strip_byte_count / 3;

      // select planar configuration - either chunky (rgbrgbrgb) or
      // planar (rrrgggbbb) format
      switch( m_planar_configuration )
      {
         case GEOTIFF_CHUNKY_FORMAT:
            switch( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
                  ibyte = 0;
                  while( ibyte < (int)strip_byte_count )
                  {
                     img[pixel_index*3+0] = strip_data[ibyte];
                     ibyte++;
                     img[pixel_index*3+1] = strip_data[ibyte];
                     ibyte++;
                     img[pixel_index*3+2] = strip_data[ibyte];
                     ibyte++;
                     pixel_index += 3;
                     if( pixel_index >= m_num_pixels*3 )
                   break;
                  }
                  break;
               case GEOTIFF_CCITT_1D:
                  goto FAIL;                     // not yet supported
               case GEOTIFF_LZW:                 // LZW compression scheme
               case GEOTIFF_PACKBITS:            // PackBits compression scheme
                  // decompress data
                  if( m_compression_scheme == GEOTIFF_PACKBITS ||
                      m_compression_scheme == GEOTIFF_LZW )
                  {
                     num_remaining_bytes = (m_num_pixels - pixel_index) *
                                           m_samples_per_pixel;
                     decompressed_size = __min( (int)(m_image_width *
                        m_rows_per_strip * m_samples_per_pixel),
                        num_remaining_bytes );
                     if( decompress_strip( (int)strip_byte_count, strip_data,
                         decompressed_size, decompressed_strip_data ) != SUCCESS )
                   goto FAIL;
                  }
                  ibyte = 0;
                  while( ibyte < decompressed_size )
                  {
                     img[pixel_index*3+0] = decompressed_strip_data[ibyte];
                     ibyte++;
                     img[pixel_index*3+1] = decompressed_strip_data[ibyte];
                     ibyte++;
                     img[pixel_index*3+2] = decompressed_strip_data[ibyte];
                     ibyte++;
                     pixel_index += 3;
                     if( pixel_index >= m_num_pixels*3 )
                   break;
                  }
                  break;
               default:
                  ASSERT( FALSE );
                  goto FAIL;
            }
            break;

         case GEOTIFF_PLANAR_FORMAT:
            switch( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
                  ibyte = 0;
                  while( ibyte < (int)psize )
                  {
                     img[pixel_index*3+0] = strip_data[ibyte];
                     ibyte++;
                     img[pixel_index*3+1] = strip_data[ibyte+psize];
                     ibyte++;
                     img[pixel_index*3+2] = strip_data[ibyte+psize+psize];
                     ibyte++;
                     pixel_index += 3;
                     if( pixel_index >= m_num_pixels*3 )
                   break;
                  }
                  break;
               case GEOTIFF_CCITT_1D:
                  goto FAIL;                     // not yet supported
               case GEOTIFF_LZW:                 // LZW compression scheme
               case GEOTIFF_PACKBITS:            // PackBits compression scheme
                  // decompress data
                  if( m_compression_scheme == GEOTIFF_PACKBITS ||
                      m_compression_scheme == GEOTIFF_LZW )
                  {
                     num_remaining_bytes = (m_num_pixels - pixel_index) *
                                           m_samples_per_pixel;
                     decompressed_size = __min( (int)(m_image_width *
                        m_rows_per_strip * m_samples_per_pixel),
                        num_remaining_bytes );
                     if( decompress_strip( (int)strip_byte_count, strip_data,
                         decompressed_size, decompressed_strip_data ) != SUCCESS )
                   goto FAIL;
                  }
                  ibyte = 0;
              psize = decompressed_size / 3;
                  while( ibyte < (int) psize )
                  {
                     img[pixel_index*3+0] = decompressed_strip_data[ibyte];
                     ibyte++;
                     img[pixel_index*3+1] = decompressed_strip_data[ibyte+psize];
                     ibyte++;
                     img[pixel_index*3+2] = decompressed_strip_data[ibyte+psize];
                     ibyte++;
                     pixel_index += 3;
                     if( pixel_index >= m_num_pixels*3 )
                   break;
                  }
                  break;
               default:
                  ASSERT( FALSE );
                  goto FAIL;
            }
            break;

         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

//   delete [] strip_data;
   MEM_free( strip_data );
   strip_data = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
//      delete [] decompressed_strip_data;
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_24bit_rgb_as_rgb_image

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_palette_as_rgb_image( unsigned char *img, IImageLibCallback *callback )
{
   // this function reads an 8-bit palette image into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int pixel_index, ibyte, decompressed_size, num_remaining_bytes;
   unsigned int strip_offset, strip_byte_count, palette_index;
   unsigned char *strip_data, *decompressed_strip_data;
   CUtil util;

   strip_data = NULL;
   decompressed_strip_data = NULL;
   decompressed_size = 0;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   pixel_index = 0;

   // loop through each strip
   for( istrip = 0; istrip < (int)m_num_strips; istrip++ )
   {
      double percent = 100.0 * (double) istrip / (double) m_num_strips;
      CString label = "Loading Image";
      if (!util.send_user_update_and_continue(callback, percent, label))
      {
         m_new_error_description = "Operation canceled by user";
         goto FAIL;
      }
      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
        goto FAIL;

      // select planar configuration - either chunky (rgbrgbrgb) or
      // planar (rrrgggbbb) format
      switch( m_planar_configuration )
      {
         case GEOTIFF_CHUNKY_FORMAT:
            switch( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
                  ibyte = 0;
                  while( ibyte < (int)strip_byte_count )
                  {
                     palette_index = strip_data[ibyte];
                     if( (int)palette_index >= m_num_color_map_values )
                        goto FAIL;
                     ibyte++;
                     img[pixel_index*3+0] = (unsigned char)(m_red_color_map[palette_index]/256);
                     img[pixel_index*3+1] = (unsigned char)(m_green_color_map[palette_index]/256);
                     img[pixel_index*3+2] = (unsigned char)(m_blue_color_map[palette_index]/256);
                     pixel_index += 3;
                     if( pixel_index >= m_num_pixels*3 )
                   break;
                  }
                  break;
               case GEOTIFF_CCITT_1D:
                  goto FAIL;                     // not yet supported
               case GEOTIFF_LZW:                 // LZW compression scheme
               case GEOTIFF_PACKBITS:            // PackBits compression scheme
                  // decompress data
                  if( m_compression_scheme == GEOTIFF_PACKBITS ||
                      m_compression_scheme == GEOTIFF_LZW )
                  {
                     num_remaining_bytes =
                        (m_num_pixels - pixel_index) * m_samples_per_pixel;
                     decompressed_size =
                        __min( (int)(m_image_width * m_rows_per_strip *
                               m_samples_per_pixel), num_remaining_bytes );
                     if( decompress_strip( (int)strip_byte_count, strip_data,
                         decompressed_size, decompressed_strip_data ) != SUCCESS )
                   goto FAIL;
                  }
                  ibyte = 0;
                  while( ibyte < decompressed_size )
                  {
                     palette_index = decompressed_strip_data[ibyte];
                     if( (int)palette_index >= m_num_color_map_values )
                        goto FAIL;
                     ibyte++;
                     img[pixel_index*3+0] = (unsigned char)(m_red_color_map[palette_index]/256);
                     img[pixel_index*3+1] = (unsigned char)(m_green_color_map[palette_index]/256);
                     img[pixel_index*3+2] = (unsigned char)(m_blue_color_map[palette_index]/256);
                     pixel_index += 3;
                     if( pixel_index >= m_num_pixels*3 )
                   break;
                  }
                  break;
               default:
                  ASSERT( FALSE );
                  goto FAIL;
            }
            break;

         case GEOTIFF_PLANAR_FORMAT:
            goto FAIL;                                  // not yet supported

         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

   MEM_free( strip_data );
   strip_data = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_8bit_palette_as_rgb_image

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_grayscale_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *img, IImageLibCallback *callback )
{
   // this function reads a grayscale (8-bit monochrome) subimage into the
   // argument red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count;
   unsigned char red, *strip_data, *decompressed_strip_data;
   int rslt;
   CUtil util;
#ifndef FULL_STRIP_READ
   unsigned pos;
#endif
   unsigned width;

   strip_data = NULL;
   decompressed_strip_data = NULL;

   if (m_compression_scheme == GEOTIFF_JPEG)
   {
      rslt = get_24bit_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
      return rslt;
   }

   if (m_image_is_tiled)
   {
      rslt = get_tiled_24bit_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
      return rslt;
   }

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip offsets tag";
      goto FAIL;
   }

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip byte count tag";
      goto FAIL;
   }

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error allocating memory";
      goto FAIL;
   }

   width = max_hpix - min_hpix + 1;

   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error allocating memory";
         goto FAIL;
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip != end_strip)  // avoid divide by zero
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }

      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
      {
         m_new_error_description = "Invalid tag type";
         goto FAIL;
      }

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
      {
         m_new_error_description = "Invalid tag type";
         goto FAIL;
      }

      // if the image is uncompressed only read as much of the strip as needed
#ifndef FULL_STRIP_READ // Only if reading partial strips
      if ( (m_compression_scheme == GEOTIFF_NO_COMPRESSION ) && (m_rows_per_strip == 1))
      {
         pos = strip_offset + min_hpix;

         // Read the strip
         if ( gtfRead( pos, strip_data, width ) != SUCCESS )
         {
            m_new_error_description = "Error reading image file";
            goto FAIL;
         }
      }
      else
#endif
      {
         // read the strip
         if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
         {
            m_new_error_description = "Error reading image file";
            goto FAIL;
         }
      }
      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // copy strip data to pixel array
#ifndef FULL_STRIP_READ
          if (m_rows_per_strip == 1)
             hpix = min_hpix;
#endif
            for( i = 0; i < (int)strip_byte_count; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
               {
                  red = strip_data[i];
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = (BYTE) (255 - red);
                  img[subimage_pixel_index*3+0] = red;
                  img[subimage_pixel_index*3+1] = red;
                  img[subimage_pixel_index*3+2] = red;
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
                 goto DONE;
               }
               ipixel++;
            }
            break;
         case GEOTIFF_CCITT_1D:
         m_new_error_description = "Unsupported compression scheme";
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            num_remaining_bytes =
               (m_num_pixels - ipixel) * m_samples_per_pixel;
            decompressed_size =
               __min( (int)(m_image_width * m_rows_per_strip *
                  m_samples_per_pixel), num_remaining_bytes );
            if( decompress_strip( (int)strip_byte_count, strip_data, decompressed_size, decompressed_strip_data ) != SUCCESS )
         {
            m_new_error_description = "Error decompressing packbits";
                goto FAIL;
         }
            for( i = 0; (unsigned int) i < decompressed_size; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
               {
                  red = decompressed_strip_data[i];
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = (BYTE) (255 - red);
                  img[subimage_pixel_index*3+0] = red;
                  img[subimage_pixel_index*3+1] = red;
                  img[subimage_pixel_index*3+2] = red;
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
                 goto DONE;
               }
               ipixel++;
            }
            break;
         default:
            ASSERT( FALSE );
         m_new_error_description = "Unsupported compression scheme";
            goto FAIL;
      }

   }

DONE:
   util.clear_callback(callback);

   // deallocate strip data array
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   util.clear_callback(callback);

   if ( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if ( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
// end of get_8bit_grayscale_as_rgb_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_grayscale_as_palette_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *img, IImageLibCallback *callback )
{
   // this function reads a grayscale (8-bit monochrome) subimage into the
   // argument red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count;
   unsigned char red, *strip_data, *decompressed_strip_data;
   CUtil util;
#ifndef FULL_STRIP_READ
   unsigned pos;
#endif
   unsigned width;

   strip_data = NULL;
   decompressed_strip_data = NULL;
/*
   if (m_compression_scheme == GEOTIFF_JPEG)
   {
      rslt = get_24bit_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
      return rslt;
   }

   if (m_image_is_tiled)
   {
      rslt = get_tiled_24bit_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
      return rslt;
   }
*/
   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip offsets tag";
      goto FAIL;
   }

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip byte count tag";
      goto FAIL;
   }

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error allocating memory";
      goto FAIL;
   }

   width = max_hpix - min_hpix + 1;

   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error allocating memory";
         goto FAIL;
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip != end_strip)  // avoid divide by zero
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }

      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
      {
         m_new_error_description = "Invalid tag type";
         goto FAIL;
      }

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
      {
         m_new_error_description = "Invalid tag type";
         goto FAIL;
      }

      // if the image is uncompressed only read as much of the strip as needed
#ifndef FULL_STRIP_READ
      if ( (m_compression_scheme == GEOTIFF_NO_COMPRESSION ) && (m_rows_per_strip == 1))
      {
         pos = strip_offset + min_hpix;

         // Read the strip
         if ( gtfRead( pos, strip_data, width ) != SUCCESS )
         {
            m_new_error_description = "Error reading image file";
            goto FAIL;
         }
      }
      else
#endif
      {
         // Read the strip
         if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
         {
            m_new_error_description = "Error reading image file";
            goto FAIL;
         }
      }
      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // copy strip data to pixel array
#ifndef FULL_STRIP_READ
          if (m_rows_per_strip == 1)
             hpix = min_hpix;
#endif
            for( i = 0; i < (int)strip_byte_count; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
               {
                  red = strip_data[i];
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = (BYTE) (255 - red);
                  img[subimage_pixel_index] = red;
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
                 goto DONE;
               }
               ipixel++;
            }
            break;
         case GEOTIFF_CCITT_1D:
         m_new_error_description = "Unsupported compression scheme";
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            num_remaining_bytes =
               (m_num_pixels - ipixel) * m_samples_per_pixel;
            decompressed_size =
               __min( (int)(m_image_width * m_rows_per_strip *
                  m_samples_per_pixel), num_remaining_bytes );
            if( decompress_strip( (int)strip_byte_count, strip_data, decompressed_size, decompressed_strip_data ) != SUCCESS )
         {
            m_new_error_description = "Error decompressing packbits";
                goto FAIL;
         }
            for( i = 0; (unsigned int) i < decompressed_size; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
               {
                  red = decompressed_strip_data[i];
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = (BYTE) (255 - red);
                  img[subimage_pixel_index] = red;
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
                 goto DONE;
               }
               ipixel++;
            }
            break;
         default:
            ASSERT( FALSE );
         m_new_error_description = "Unsupported compression scheme";
            goto FAIL;
      }

   }

DONE:
   util.clear_callback(callback);

   // deallocate strip data array
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   util.clear_callback(callback);

   if ( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if ( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
// end of get_8bit_grayscale_as_palette_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_palette_as_palette_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                       unsigned char *indices, IImageLibCallback *callback )
{
   // this function returns the palette indices for the subimage pixels in the
   // argument indices based on the values in the color_quantizer argument
   // returns SUCCESS or FAILURE

   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int ibyte, decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count, palette_index;
   unsigned char *strip_data, *decompressed_strip_data;
   CUtil util;


   strip_data = decompressed_strip_data = NULL;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index )
       != SUCCESS ) goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index )
       != SUCCESS ) goto FAIL;

   // allocate data for strips
   strip_data = NULL;
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );

   // if compressed, allocate memory for a decompressed strip
   decompressed_strip_data = NULL;
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data =
         (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip *
                           m_samples_per_pixel );
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip != end_strip)  // avoid divide by zero
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }
      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
        goto FAIL;

      // select planar configuration - either chunky (rgbrgbrgb) or
      // planar (rrrgggbbb) format
      switch ( m_planar_configuration )
      {
         case GEOTIFF_PLANAR_FORMAT:
         case GEOTIFF_CHUNKY_FORMAT:
            switch ( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
                  if ( m_rows_per_strip == 1 )
                  {
                     ibyte = min_hpix;
                     for ( hpix = min_hpix; hpix <= max_hpix; hpix++ )
                     {
                        palette_index = strip_data[ibyte];
                  indices[subimage_pixel_index] = (BYTE) palette_index;
                        subimage_pixel_index++;
                     }
                  }
                  else
                  {
                     ibyte = 0;
                     while( ibyte < (int)strip_byte_count )
                     {
                        palette_index = strip_data[ibyte];
                        if( (int)palette_index >= m_num_color_map_values )
                           goto FAIL;
                        ibyte++;
                        if( hpix >= min_hpix && hpix <= max_hpix &&
                            vpix >= min_vpix && vpix <= max_vpix )
                        {
                     indices[subimage_pixel_index] = (BYTE) palette_index;
                           subimage_pixel_index++;
                        }
                        hpix++;
                        if( hpix == (int)m_image_width )
                        {
                           hpix = 0;
                           vpix++;
                           if ( vpix > max_vpix )
                        goto DONE;
                        }
                        ipixel++;
                     }
                  }
                  break;
               case GEOTIFF_CCITT_1D:
                  goto FAIL;                     // not yet supported
               case GEOTIFF_LZW:                 // LZW compression scheme
               case GEOTIFF_PACKBITS:            // PackBits compression scheme
                  // decompress data
                  num_remaining_bytes =
                     (m_num_pixels - ipixel) * m_samples_per_pixel;
                  decompressed_size =
                     __min( (int)(m_image_width * m_rows_per_strip *
                     m_samples_per_pixel), num_remaining_bytes );
                  if( decompress_strip( (int)strip_byte_count, strip_data,
                      decompressed_size, decompressed_strip_data ) !=
                      SUCCESS ) goto FAIL;

                  if( m_rows_per_strip == 1 )
                  {
                     ibyte = min_hpix;
                     for( hpix = min_hpix; hpix <= max_hpix; hpix++ )
                     {
                        palette_index = decompressed_strip_data[hpix];
                  indices[subimage_pixel_index] = (BYTE) palette_index;
                        subimage_pixel_index++;
                     }
                  }
                  else
                  {
                     ibyte = 0;
                     while( ibyte < decompressed_size )
                     {
                        palette_index = decompressed_strip_data[ibyte];
                        if( (int)palette_index >= m_num_color_map_values )
                           goto FAIL;
                        ibyte++;
                        if( hpix >= min_hpix && hpix <= max_hpix &&
                            vpix >= min_vpix && vpix <= max_vpix )
                        {
                     indices[subimage_pixel_index] = (BYTE) palette_index;
                            subimage_pixel_index++;
                        }
                        hpix++;
                        if ( hpix == (int)m_image_width )
                        {
                           hpix = 0;
                           vpix++;
                           if ( vpix > max_vpix )
                        goto DONE;
                        }
                        ipixel++;
                     }
                  }
                  break;
               default:
                  ASSERT( FALSE );
                  goto FAIL;
            }
            break;

//         case GEOTIFF_PLANAR_FORMAT:
//            goto FAIL;                           // not yet supported

         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
//   delete [] strip_data;
   MEM_free( strip_data );
   strip_data = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
// end of get_8bit_palette_as_palette_subimage

// ********************************************************************************************
// ********************************************************************************************

/*
int CGeoTiff::get_strip_data( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array)
{
   if (m_image_is_tiled)
      return FAILURE;




}
*/

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_1bit_image_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                  unsigned char *img, IImageLibCallback *callback )
{
   // this function reads a 1-bit subimage into the
   // argument red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count;
   unsigned char red, *strip_data, *decompressed_strip_data;
   int rslt;
   CUtil util;

   strip_data = NULL;
   decompressed_strip_data = NULL;

   if (m_image_is_tiled)
   {
      rslt = get_tiled_24bit_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
      return rslt;
   }

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip offsets tag";
      goto FAIL;
   }

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip byte count tag";
      goto FAIL;
   }

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error allocating memory";
         goto FAIL;
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip != end_strip)  // avoid divide by zero
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }

      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
      {
         m_new_error_description = "Invalid tag type";
         goto FAIL;
      }

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
      {
         m_new_error_description = "Invalid tag type";
         goto FAIL;
      }

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
        goto FAIL;

     switch( m_compression_scheme )
     {
       case GEOTIFF_NO_COMPRESSION:
         // copy strip data to pixel array
         for( i = 0; i < (int)strip_byte_count; i++ )
         {
            int bt, k;
            BYTE dat, clr;

            for (k=0; k<8; k++)
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                  vpix >= min_vpix && vpix <= max_vpix )
               {
                  dat = strip_data[i];
                  bt = dat & (0x80 >> k);
                  if (bt > 0)
                     clr = 255;
                  else
                     clr = 0;
                  img[subimage_pixel_index*3+0] = clr;
                  img[subimage_pixel_index*3+1] = clr;
                  img[subimage_pixel_index*3+2] = clr;
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                 hpix = 0;
                 vpix++;
                 if( vpix > max_vpix )
                    goto DONE;
               }
               ipixel++;
            }
         }
         break;
       case GEOTIFF_CCITT_1D:
         goto FAIL;                          // not yet supported
       case GEOTIFF_LZW:                 // LZW compression scheme
       case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // copy decompressed strip data to pixel array
         // decompress data
         num_remaining_bytes = (m_num_pixels - ipixel) * m_samples_per_pixel;
         decompressed_size = __min( (int)(m_image_width * m_rows_per_strip * m_samples_per_pixel), num_remaining_bytes );
         if( decompress_strip( (int)strip_byte_count, strip_data, decompressed_size, decompressed_strip_data ) != SUCCESS )
            goto FAIL;
         for( i = 0; (unsigned int) i < decompressed_size; i++ )
         {
            if( hpix >= min_hpix && hpix <= max_hpix &&
               vpix >= min_vpix && vpix <= max_vpix )
            {
               red = decompressed_strip_data[i];
               // check for reversed grayscale
               if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                  red = (BYTE) (255 - red);
               img[subimage_pixel_index*3+0] = red;
               img[subimage_pixel_index*3+1] = red;
               img[subimage_pixel_index*3+2] = red;
               subimage_pixel_index++;
            }
            hpix++;
            if( hpix == (int)m_image_width )
            {
              hpix = 0;
              vpix++;
              if( vpix > max_vpix )
                 goto DONE;
            }
            ipixel++;
         }
         break;
       default:
         ASSERT( FALSE );
         goto FAIL;
     }
   }

DONE:
   // deallocate strip data array
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
// end of get_1bit_image_as_rgb_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_16bit_grayscale_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                      unsigned char *img, IImageLibCallback *callback )
{
   // this function reads a 16-bit grayscale subimage into the
   // argument red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count;
   unsigned char red, *strip_data, *decompressed_strip_data;
   unsigned char byte1, byte2;
   unsigned short *strip_16bit_data;
   double fraction;
//   int clr, maxval;
   int rslt;
   CUtil util;

   strip_data = NULL;
   decompressed_strip_data = NULL;
   strip_16bit_data = NULL;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip offsets tag";
      goto FAIL;
   }

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip counts tag";
      goto FAIL;
   }

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count * 2 );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error allocating memory";
      goto FAIL;
   }


   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( 2 * m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error allocating memory";
         goto FAIL;
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

//maxval = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip != end_strip)  // avoid divide by zero
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }

      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
      {
         m_new_error_description = "File Read Error";
         goto FAIL;
      }

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // swap byte order if necessary
            if ( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               for( i = 0; i < (int)strip_byte_count; i += 2 )
               {
                  byte1 = strip_data[i];
                  byte2 = strip_data[i+1];
                  strip_data[i] = byte2;
                  strip_data[i+1] = byte1;
               }
            }
            // copy strip data to pixel array
            strip_16bit_data = (unsigned short*) strip_data;
            for( i = 0; i < (int)strip_byte_count/2; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix && vpix >= min_vpix && vpix <= max_vpix )
               {
                  fraction = (double)(strip_16bit_data[i]-m_min_sample_value) / (m_max_sample_value-m_min_sample_value);
                  if( fraction < 0.0 )
                     fraction = 0.0;
                  if( fraction > 1.0 )
                     fraction = 1.0;
                  red = (unsigned char)(255*fraction+0.5);
//                clr = strip_16bit_data[i];
//                if (maxval < clr)
//                   maxval = clr;
//                clr /= 6;
//                if (clr > 255)
//                   clr = 255;
//                red = (BYTE) clr;
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = (BYTE) (255 - red);
                  img[subimage_pixel_index*3+0] = red;
                  img[subimage_pixel_index*3+1] = red;
                  img[subimage_pixel_index*3+2] = red;
                  subimage_pixel_index++;
               }
               hpix++;
               if ( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
                     goto DONE;
               }
               ipixel++;
            }
            break;
         case GEOTIFF_CCITT_1D:
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            num_remaining_bytes = 2 * (m_num_pixels - ipixel) * m_samples_per_pixel;
            decompressed_size = __min( (int)(2 * m_image_width * m_rows_per_strip *
                   m_samples_per_pixel), num_remaining_bytes );
            rslt = decompress_strip( (int)strip_byte_count, strip_data, decompressed_size, decompressed_strip_data );
            if (rslt != SUCCESS )
            {
               m_new_error_description = "Error decompressing packbits";
               goto FAIL;
            }
            // swap byte order if necessary
            if( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               for( i = 0; i < (int)decompressed_size; i += 2 )
               {
                  byte1 = decompressed_strip_data[i];
                  byte2 = decompressed_strip_data[i+1];
                  decompressed_strip_data[i] = byte2;
                  decompressed_strip_data[i+1] = byte1;
               }
            }
            strip_16bit_data = (unsigned short*) decompressed_strip_data;
            for( i = 0; (unsigned int) i < decompressed_size/2; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix && vpix >= min_vpix && vpix <= max_vpix )
               {
                  fraction = (double)(strip_16bit_data[i]-m_min_sample_value)/
                           (m_max_sample_value-m_min_sample_value);
                  if ( fraction < 0.0 )
                     fraction = 0.0;
                  if ( fraction > 1.0 )
                     fraction = 1.0;
                  red = (unsigned char)(255*fraction+0.5);
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = (BYTE) (255 - red);
                  img[subimage_pixel_index*3+0] = red;
                  img[subimage_pixel_index*3+1] = red;
                  img[subimage_pixel_index*3+2] = red;
                  subimage_pixel_index++;
               }
               hpix++;
               if ( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if ( vpix > max_vpix )
                     goto DONE;
               }
               ipixel++;
            }
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
   // deallocate strip data array
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
// end of get_16bit_grayscale_as_rgb_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_grayscale_as_8bit_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                      unsigned char *img, IImageLibCallback *callback )
{
   // this function reads a 8-bit grayscale subimage into the img pixel array
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count;
   unsigned char *strip_data, *decompressed_strip_data, clr;
   int maxval, rslt;
   CUtil util;

   strip_data = NULL;
   decompressed_strip_data = NULL;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip offsets tag";
      goto FAIL;
   }

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip counts tag";
      goto FAIL;
   }

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count);
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error allocating memory";
      goto FAIL;
   }


   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error allocating memory";
         goto FAIL;
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

maxval = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip != end_strip)  // avoid divide by zero
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }

      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
      {
         m_new_error_description = "File Read Error";
         goto FAIL;
      }

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // check for reversed grayscale
            if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
               for( i = 0; i < (int)strip_byte_count/2; i++ )
                  strip_data[i] = 255 - strip_data[i];

            // copy strip data to pixel array
//          memcpy(img, strip_data, strip_byte_count);

            for( i = 0; i < (int)strip_byte_count; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix && vpix >= min_vpix && vpix <= max_vpix )
               {
                  clr = strip_data[i];
                  // check for reversed grayscale
                  if ( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     clr = 255 - clr;
                  img[subimage_pixel_index] = clr;
                  subimage_pixel_index++;
               }
               hpix++;
               if ( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
                     goto DONE;
               }
               ipixel++;
            }
            break;
         case GEOTIFF_CCITT_1D:
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            num_remaining_bytes = (m_num_pixels - ipixel) * m_samples_per_pixel;
            decompressed_size = __min( (int)(m_image_width * m_rows_per_strip *
                   m_samples_per_pixel), num_remaining_bytes );
            rslt = decompress_strip( (int)strip_byte_count, strip_data, decompressed_size, decompressed_strip_data );
            if (rslt != SUCCESS )
            {
               m_new_error_description = "Error decompressing packbits";
               goto FAIL;
            }
            strip_data = (unsigned char*) decompressed_strip_data;
            for( i = 0; (unsigned int) i < decompressed_size; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix && vpix >= min_vpix && vpix <= max_vpix )
               {
                  clr = strip_data[i];
                  // check for reversed grayscale
                  if ( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     clr = 255 - clr;
                  img[subimage_pixel_index] = clr;
                  subimage_pixel_index++;
               }
               hpix++;
               if ( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if ( vpix > max_vpix )
                     goto DONE;
               }
               ipixel++;
            }
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
   // deallocate strip data array
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
// end of get_8bit_grayscale_as_8bit_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_16bit_grayscale_as_16bit_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                      unsigned short *img, IImageLibCallback *callback )
{
   // this function reads a 16-bit grayscale subimage into the img pixel array
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count;
   unsigned char *strip_data, *decompressed_strip_data;
   unsigned char byte1, byte2;
   unsigned short *strip_16bit_data, clr;
   int maxval, rslt;
   CUtil util;

   strip_data = NULL;
   decompressed_strip_data = NULL;
   strip_16bit_data = NULL;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip offsets tag";
      goto FAIL;
   }

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
   {
      m_new_error_description = "Error finding strip counts tag";
      goto FAIL;
   }

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count * 2);
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error allocating memory";
      goto FAIL;
   }


   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( 2 * m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error allocating memory";
         goto FAIL;
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

maxval = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip != end_strip)  // avoid divide by zero
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }

      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Read the strip
      if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
      {
         m_new_error_description = "File Read Error";
         goto FAIL;
      }

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // swap byte order if necessary
            if ( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               for( i = 0; i < (int)strip_byte_count; i += 2 )
               {
                  byte1 = strip_data[i];
                  byte2 = strip_data[i+1];
                  strip_data[i] = byte2;
                  strip_data[i+1] = byte1;
               }
            }
            // check for reversed grayscale
            if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
               for( i = 0; i < (int)strip_byte_count/2; i++ )
                  strip_data[i] = 65535 - strip_data[i];

            // copy strip data to pixel array
//          memcpy(img, strip_data, strip_byte_count);

            strip_16bit_data = (unsigned short*) strip_data;
            for( i = 0; i < (int)strip_byte_count/2; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix && vpix >= min_vpix && vpix <= max_vpix )
               {
                  clr = strip_16bit_data[i];
                  // check for reversed grayscale
                  if ( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     clr = 65535 - clr;
                  img[subimage_pixel_index] = clr;
                  subimage_pixel_index++;
               }
               hpix++;
               if ( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
                     goto DONE;
               }
               ipixel++;
            }
            break;
         case GEOTIFF_CCITT_1D:
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                 // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            num_remaining_bytes = 2 * (m_num_pixels - ipixel) * m_samples_per_pixel;
            decompressed_size = __min( (int)(2 * m_image_width * m_rows_per_strip *
                   m_samples_per_pixel), num_remaining_bytes );
            rslt = decompress_strip( (int)strip_byte_count, strip_data, decompressed_size, decompressed_strip_data );
            if (rslt != SUCCESS )
            {
               m_new_error_description = "Error decompressing packbits";
               goto FAIL;
            }
            // swap byte order if necessary
            if( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               for( i = 0; i < (int)decompressed_size; i += 2 )
               {
                  byte1 = decompressed_strip_data[i];
                  byte2 = decompressed_strip_data[i+1];
                  decompressed_strip_data[i] = byte2;
                  decompressed_strip_data[i+1] = byte1;
               }
            }
            strip_16bit_data = (unsigned short*) decompressed_strip_data;
            for( i = 0; (unsigned int) i < decompressed_size/2; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix && vpix >= min_vpix && vpix <= max_vpix )
               {
                  clr = strip_16bit_data[i];
                  // check for reversed grayscale
                  if ( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     clr = 65535 - clr;
                  img[subimage_pixel_index] = clr;
                  subimage_pixel_index++;
               }
               hpix++;
               if ( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if ( vpix > max_vpix )
                     goto DONE;
               }
               ipixel++;
            }
            break;
         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
   // deallocate strip data array
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}
// end of get_16bit_grayscale_as_16bit_subimage

// ********************************************************************************************
// ********************************************************************************************

// this function reads a 24-bit rgb subimage into the argument
// red, green, and blue pixel arrays
// returns SUCCESS or FAILURE

int CGeoTiff::get_24bit_rgb_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                 unsigned char *img, IImageLibCallback *callback )
{
   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int ibyte, decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count, psize, cur_max_byte_count;
   unsigned char red, green, blue, *strip_data, *decompressed_strip_data;
   int kx, ky, inpos, outpos;
   int iwidth, iheight, strip_cnt;

   unsigned char *jred, *jgrn, *jblu;
   CJpeg jpeg;
   int width, height, rslt, maxpos;
   CString error;
   int dummy;
   CUtil util;

   jred = jgrn = jblu = NULL;
   strip_data = NULL;
   decompressed_strip_data = NULL;
   decompressed_size = 0;

   if (m_image_is_tiled)
   {
      rslt = get_tiled_24bit_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
      return rslt;
   }

   if ( m_planar_configuration == GEOTIFF_PLANAR_FORMAT)
   {
      rslt = get_24bit_planar_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
      return rslt;
   }

   // find strip offsets tag
   if ( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if ( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   width = max_hpix - min_hpix + 1;
   height = max_vpix - min_vpix + 1;

   iwidth = width;
   iheight = height;

   maxpos = width * height;

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   cur_max_byte_count = m_max_strip_byte_count;

   if (m_compression_scheme == GEOTIFF_JPEG)
   {
      // alloc memory for jpeg tiffs
      jred = (unsigned char*) MEM_malloc( width * m_rows_per_strip * 2);
      jgrn = (unsigned char*) MEM_malloc( width * m_rows_per_strip * 2);
      jblu = (unsigned char*) MEM_malloc( width * m_rows_per_strip * 2);
      if (jred == NULL || jgrn == NULL || jblu == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }
   else if ( m_compression_scheme == GEOTIFF_PACKBITS ||
             m_compression_scheme == GEOTIFF_LZW )
   {
      // if compressed, allocate memory for a decompressed strip
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   outpos = 0;

   strip_cnt = end_strip - start_strip + 1;

   // loop through each strip
   for ( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip < end_strip)
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }

      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Set the file pointer to the row offset
      if ( gtfSeek( strip_offset ) != SUCCESS )
         goto FAIL;

      if (m_compression_scheme == GEOTIFF_JPEG)
      {
         int yoff, yrange;
         BOOL rgb_colorspace = FALSE;

         if (m_photometric_interpretation == GEOTIFF_RGB)
            rgb_colorspace = TRUE;

         width = iwidth;
         yoff = min_vpix % m_rows_per_strip;
         if (istrip > start_strip)
            yoff = 0;

         yrange = m_rows_per_strip - yoff;

         if (m_jpeg_table_len > 0)
            jpeg.set_jpeg_tables(m_jpeg_table, m_jpeg_table_len);

#if defined MAPPEDFILE_FILE_INPUT
         rslt = jpeg.get_jpeg_bitstream_image(
            ( m_iobMappedFileIOBufDesc._base != NULL ) ? &m_iobMappedFileIOBufDesc : m_file_ptr,
            min_hpix, yoff, width, yrange, rgb_colorspace, jred, jgrn, jblu, error );
         if ( m_iobMappedFileIOBufDesc._base == NULL )
            m_iobMappedFileIOBufDesc._ptr = ftell( m_file_ptr ) + m_iobMappedFileIOBufDesc._base;
#elif defined READFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
         rslt = jpeg.get_jpeg_bitstream_image( &m_iobMappedFileIOBufDesc,
            min_hpix, yoff, width, yrange, rgb_colorspace, jred, jgrn, jblu, error );
#else
         rslt = jpeg.get_jpeg_bitstream_image(m_file_ptr, min_hpix, yoff, width, yrange, rgb_colorspace,
                                 jred, jgrn, jblu, error);
         m_qwFilePosition = _ftelli64( m_file_ptr );   // Resync
#endif

         inpos = 0;
         for (ky=0; ky<yrange; ky++)
         {
            for (kx=0; kx < iwidth; kx++)
            {
               img[outpos*3+0] = jred[inpos];
               img[outpos*3+1] = jgrn[inpos];
               img[outpos*3+2] = jblu[inpos];
               inpos++;
               outpos++;
               if (outpos >= maxpos)
                  goto DONE;

            }
         }
      }
      else
      {
         // something other than JPEG compression

         // check for bad max byte count
         if (strip_byte_count > cur_max_byte_count)
         {
            cur_max_byte_count = strip_byte_count;
            strip_data = (unsigned char*) realloc(strip_data, cur_max_byte_count);
         }

         // read the strip
         if ( gtfRead( strip_data, strip_byte_count ) != SUCCESS )
           goto FAIL;

         psize = strip_byte_count / 3;

         // select planar configuration - either chunky (rgbrgbrgb) or
         // planar (rrrgggbbb) format
         switch( m_planar_configuration )
         {
          case GEOTIFF_CHUNKY_FORMAT:
            switch( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
                 ibyte = 0;
                 // copy strip data to pixel array
                 while( ibyte < (int)strip_byte_count )
                 {
                   red = strip_data[ibyte];
                   ibyte++;
                   green = strip_data[ibyte];
                   ibyte++;
                   blue = strip_data[ibyte];
                   ibyte++;
                   if( hpix >= min_hpix && hpix <= max_hpix &&
                      vpix >= min_vpix && vpix <= max_vpix )
                   {
                     img[subimage_pixel_index*3+0] = red;
                     img[subimage_pixel_index*3+1] = green;
                     img[subimage_pixel_index*3+2] = blue;
                     subimage_pixel_index++;
                   }
                   hpix++;
                   if( hpix == (int)m_image_width )
                   {
                     hpix = 0;
                     vpix++;
                     if ( vpix > max_vpix )
                        goto DONE;
                   }
                   ipixel++;
                 }
                 break;
               case GEOTIFF_CCITT_1D:
                 goto FAIL;                     // not yet supported
               case GEOTIFF_LZW:                 // LZW compression scheme
               case GEOTIFF_PACKBITS:            // PackBits compression scheme
                 // decompress data
                   num_remaining_bytes =
                     (m_num_pixels - ipixel) * m_samples_per_pixel;
                   decompressed_size =
                     __min( (int)(m_image_width * m_rows_per_strip *
                     m_samples_per_pixel), num_remaining_bytes );
                   if ( decompress_strip( (int)strip_byte_count, strip_data,
                      decompressed_size, decompressed_strip_data ) != SUCCESS )
//                    goto FAIL;
                     dummy = 0;
                 ibyte = 0;
                 // copy strip data to pixel array
                 while( ibyte < (int)decompressed_size )
                 {
                   red = decompressed_strip_data[ibyte];
                   ibyte++;
                   green = decompressed_strip_data[ibyte];
                   ibyte++;
                   blue = decompressed_strip_data[ibyte];
                   ibyte++;
                   if( hpix >= min_hpix && hpix <= max_hpix &&
                      vpix >= min_vpix && vpix <= max_vpix )
                   {
                     img[subimage_pixel_index*3+0] = red;
                     img[subimage_pixel_index*3+1] = green;
                     img[subimage_pixel_index*3+2] = blue;
                     subimage_pixel_index++;
                   }
                   hpix++;
                   if( hpix == (int)m_image_width )
                   {
                     hpix = 0;
                     vpix++;
                     if( vpix > max_vpix )
                        goto DONE;
                   }
                   ipixel++;
                 }
                 break;
               default:
                 ASSERT( FALSE );
                 goto FAIL;
            }
            break;

          case GEOTIFF_PLANAR_FORMAT:
             return FAILURE;

          default:
            ASSERT( FALSE );
            goto FAIL;
         }
      }
   }


DONE:
   if (strip_data != NULL)
      MEM_free( strip_data );
   strip_data = NULL;

   if (jred != NULL)
      MEM_free(jred);
   if (jgrn != NULL)
      MEM_free(jgrn);
   if (jblu != NULL)
      MEM_free(jblu);

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if (jred != NULL)
      MEM_free(jred);
   if (jgrn != NULL)
      MEM_free(jgrn);
   if (jblu != NULL)
      MEM_free(jblu);

   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_24bit_rgb_as_rgb_subimage

// ********************************************************************************************
// ********************************************************************************************

// this function reads a 48-bit rgb subimage into the argument
// red, green, and blue pixel arrays, 48 bit space is mapped into 24 bit space
// returns SUCCESS or FAILURE

int CGeoTiff::get_48bit_rgb_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                 unsigned char *img, IImageLibCallback *callback )
{
   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   int ibyte;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count, psize, cur_max_byte_count;
   unsigned short red, green, blue, *strip_data, *buf;
   int outpos;
   int iwidth, iheight, strip_cnt;
   int width, height, maxpos;
   CString error;
   CUtil util;

   strip_data = buf = NULL;

   if (m_compression_scheme != GEOTIFF_NO_COMPRESSION)
   {
      m_new_error_description = "Only uncompressed 48-bit RGB images are supported";
      return FAILURE;
   }

   if (m_image_is_tiled)
   {
//    rslt = get_tiled_48bit_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
//    return rslt;
      return FAILURE;
   }

   if ( m_planar_configuration == GEOTIFF_PLANAR_FORMAT)
   {
//    rslt = get_48bit_planar_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
//    return rslt;
      return FAILURE;
   }

   // find strip offsets tag
   if ( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if ( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   width = max_hpix - min_hpix + 1;
   height = max_vpix - min_vpix + 1;

   iwidth = width;
   iheight = height;

   maxpos = width * height;

   // allocate data for strips
   strip_data = (unsigned short*)MEM_malloc( m_max_strip_byte_count * sizeof(unsigned short) );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   // allocate data for 16-bit buffer
   buf = (unsigned short*)MEM_malloc( maxpos * 3 * sizeof(unsigned short) );
   if (buf == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   cur_max_byte_count = m_max_strip_byte_count;

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   outpos = 0;

   strip_cnt = end_strip - start_strip + 1;

   // loop through each strip
   for ( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip < end_strip)
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }

      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Set the file pointer to the row offset
      if ( gtfSeek( strip_offset ) != SUCCESS )
         goto FAIL;

      // check for bad max byte count
      if (strip_byte_count > cur_max_byte_count)
      {
         cur_max_byte_count = strip_byte_count;
         strip_data = (unsigned short*) realloc(strip_data, cur_max_byte_count);
      }

      // read the strip
      if ( gtfRead( (BYTE*) strip_data, strip_byte_count ) != SUCCESS )
        goto FAIL;

      psize = strip_byte_count / 3;

      // select planar configuration - either chunky (rgbrgbrgb) or
      // planar (rrrgggbbb) format
      switch( m_planar_configuration )
      {
       case GEOTIFF_CHUNKY_FORMAT:
         switch( m_compression_scheme )
         {
            case GEOTIFF_NO_COMPRESSION:
              ibyte = 0;
              // copy strip data to pixel array
              while( ibyte < (int)strip_byte_count/2 )
              {
                red = strip_data[ibyte];
                ibyte++;
                green = strip_data[ibyte];
                ibyte++;
                blue = strip_data[ibyte];
                ibyte++;
                if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
                {
                  buf[subimage_pixel_index*3+0] = red;
                  buf[subimage_pixel_index*3+1] = green;
                  buf[subimage_pixel_index*3+2] = blue;
                  subimage_pixel_index++;
                }
                hpix++;
                if( hpix == (int)m_image_width )
                {
                  hpix = 0;
                  vpix++;
                  if ( vpix > max_vpix )
                     goto DONE;
                }
                ipixel++;
              }
              break;
            default:
              ASSERT( FALSE );
              goto FAIL;
         }
         break;

       case GEOTIFF_PLANAR_FORMAT:
          return FAILURE;

       default:
         ASSERT( FALSE );
         goto FAIL;
      }
   }


DONE:
   int maxval, k, size;
   double factor;

   size = maxpos * 3;

    if ( m_byte_order == GEOTIFF_BIG_ENDIAN )
    {
       for ( k = 0; k < size; k++ )
         byteswap_2(&(buf[k]));
    }

   maxval = 0;
   for (k=0; k<size; k++)
      if (buf[k] > maxval)
         maxval = buf[k];

   if (maxval == 0)
      factor = 1.0;
   else
      factor = 255.0 / (double) maxval;

   for (k=0; k<size; k++)
      img[k] = (BYTE) ((double) buf[k] * factor);

   if (strip_data != NULL)
      MEM_free( strip_data );
   strip_data = NULL;

   if (buf != NULL)
      MEM_free(buf);
   buf = NULL;

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if ( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if ( buf != NULL )
   {
      MEM_free( buf );
      buf = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_48bit_rgb_as_rgb_subimage

// ********************************************************************************************
// ********************************************************************************************

// this function reads a 24-bit rgb subimage into the argument
// red, green, and blue pixel arrays
// returns SUCCESS or FAILURE

int CGeoTiff::get_24bit_planar_rgb_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                 unsigned char *img, IImageLibCallback *callback )
{
   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int ibyte, decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count, psize;
   unsigned char red, green, blue, *strip_data, *decompressed_strip_data;
   int kx, ky, inpos, outpos, subx, suby;
   int iwidth, iheight, strip_cnt, img_size;;

   unsigned char *jred, *jgrn, *jblu;
   CJpeg jpeg;
   int width, height, rslt, maxpos, plane;
   CString error;
   int dummy, data_cnt;;
   CUtil util;

   if ( m_planar_configuration != GEOTIFF_PLANAR_FORMAT)
   {
      ASSERT(0);
      m_new_error_description = "Function is only for planar images";
      return FAILURE;
   }

   img_size = (max_hpix - min_hpix + 1) * (max_vpix - min_vpix + 1) * 3;

   jred = jgrn = jblu = NULL;
   strip_data = NULL;
   decompressed_strip_data = NULL;
   decompressed_size = 0;

   if (m_image_is_tiled)
   {
      rslt = get_tiled_24bit_rgb_as_rgb_subimage(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
      return rslt;
   }

   // find strip offsets tag
   if ( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if ( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   width = max_hpix - min_hpix + 1;
   height = max_vpix - min_vpix + 1;

   iwidth = width;
   iheight = height;

   maxpos = width * height;

   // allocate data for strips

   data_cnt = m_max_strip_byte_count;
   if (data_cnt < (int) m_image_width)
      data_cnt = m_image_width;

   strip_data = (unsigned char*)MEM_malloc( data_cnt );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   if (m_compression_scheme == GEOTIFF_JPEG)
   {
      // alloc memory for jpeg tiffs
      jred = (unsigned char*) MEM_malloc( width * m_rows_per_strip * 2);
      jgrn = (unsigned char*) MEM_malloc( width * m_rows_per_strip * 2);
      jblu = (unsigned char*) MEM_malloc( width * m_rows_per_strip * 2);
      if (jred == NULL || jgrn == NULL || jblu == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }
   else if ( m_compression_scheme == GEOTIFF_PACKBITS ||
             m_compression_scheme == GEOTIFF_LZW )
   {
      // if compressed, allocate memory for a decompressed strip
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   outpos = 0;

   strip_cnt = end_strip - start_strip + 1;

   for (plane=0; plane < 3; plane++)
   {
      vpix = start_strip * m_rows_per_strip;
      ipixel = vpix * m_image_width + hpix;
      subimage_pixel_index = 0;
      // loop through each strip
      for ( istrip = start_strip; istrip <= end_strip; istrip++ )
      {
         hpix = 0;
         if ((callback != NULL) && (start_strip < end_strip))
         {
            double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
            percent *= (double) plane * 33.3333333;
            CString label = "Loading Image";
            if (!util.send_user_update_and_continue(callback, percent, label))
            {
               m_new_error_description = "Operation canceled by user";
               goto FAIL;
            }
         }

         // Get the strip offset
         if ( GetTagIndexUIntValue( istrip+(plane*m_num_strips), strip_offsets_tag_index, strip_offset ) != SUCCESS )
            goto FAIL;

         // Get the strip byte count
         if ( GetTagIndexUIntValue( istrip+(plane*m_num_strips), strip_byte_counts_tag_index, strip_byte_count ) != SUCCESS )
            goto FAIL;

         // Set the file pointer to the row offset
         if ( gtfSeek( strip_offset ) != SUCCESS )
            goto FAIL;

         if (m_compression_scheme == GEOTIFF_JPEG)
         {
            int yoff, yrange;
            BOOL rgb_colorspace = FALSE;

            if (m_photometric_interpretation == GEOTIFF_RGB)
               rgb_colorspace = TRUE;

            width = iwidth;
            yoff = min_vpix % m_rows_per_strip;
            if (istrip > start_strip)
               yoff = 0;

            yrange = m_rows_per_strip - yoff;

            if (m_jpeg_table_len > 0)
               jpeg.set_jpeg_tables(m_jpeg_table, m_jpeg_table_len);

   #if defined MAPPEDFILE_FILE_INPUT
            rslt = jpeg.get_jpeg_bitstream_image(
            ( m_iobMappedFileIOBufDesc._base != NULL ) ? &m_iobMappedFileIOBufDesc : m_file_ptr,
            min_hpix, yoff, width, yrange, rgb_colorspace, jred, jgrn, jblu, error );
          if ( m_iobMappedFileIOBufDesc._base == NULL )
            m_iobMappedFileIOBufDesc._ptr = ftell( m_file_ptr ) + m_iobMappedFileIOBufDesc._base;
   #elif defined READFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
            rslt = jpeg.get_jpeg_bitstream_image( &m_iobMappedFileIOBufDesc,
            min_hpix, yoff, width, yrange, rgb_colorspace, jred, jgrn, jblu, error );
   #else
            rslt = jpeg.get_jpeg_bitstream_image(m_file_ptr, min_hpix, yoff, width, yrange, rgb_colorspace,
                                    jred, jgrn, jblu, error);
          m_qwFilePosition = _ftelli64( m_file_ptr );   // Resync
   #endif

            inpos = 0;
            for (ky=0; ky<yrange; ky++)
            {
               for (kx=0; kx < iwidth; kx++)
               {
                  img[outpos*3+0] = jred[inpos];
                  img[outpos*3+1] = jgrn[inpos];
                  img[outpos*3+2] = jblu[inpos];
                  inpos++;
                  outpos++;
                  if (outpos >= maxpos)
                     goto DONE;

               }
            }
         }
         else
         {
            // something other than JPEG compression

            // read the strip
            if ( gtfRead( strip_data, strip_byte_count ) != SUCCESS )
              goto FAIL;

            psize = strip_byte_count / 3;

            switch( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
                 ibyte = 0;
                 // copy strip data to pixel array
//               while ( ibyte < (int)strip_byte_count )
                  for (suby=0; suby < (int) m_rows_per_strip; suby++)
                 {
                     hpix = 0;
                     for (subx=0; subx < (int) m_image_width; subx++)
                     {
                         if ( hpix >= min_hpix && hpix <= max_hpix &&
                            vpix >= min_vpix && vpix <= max_vpix )
                         {
                            if (img_size > (subimage_pixel_index*3+plane))
                              img[subimage_pixel_index*3+plane] = strip_data[ibyte];
                           subimage_pixel_index++;
                         }
                         hpix++;
                        ipixel++;
                         ibyte++;
                     }
                     vpix++;
                 }
                 break;
               case GEOTIFF_CCITT_1D:
                 goto FAIL;                     // not yet supported
               case GEOTIFF_LZW:                 // LZW compression scheme
               case GEOTIFF_PACKBITS:            // PackBits compression scheme
                 // decompress data
                 if( m_compression_scheme == GEOTIFF_PACKBITS ||
                     m_compression_scheme == GEOTIFF_LZW )
                 {
                   num_remaining_bytes =
                     (m_num_pixels - ipixel) * m_samples_per_pixel;
                   decompressed_size =
                     __min( (int)(m_image_width * m_rows_per_strip *
                     m_samples_per_pixel), num_remaining_bytes );
                   if ( decompress_strip( (int)strip_byte_count, strip_data,
                      decompressed_size, decompressed_strip_data ) != SUCCESS )
   //                    goto FAIL;
                     dummy = 0;
                 }
                 ibyte = 0;
                 // copy strip data to pixel array
                 while( ibyte < (int)decompressed_size )
                 {
                   red = decompressed_strip_data[ibyte];
                   ibyte++;
                   green = decompressed_strip_data[ibyte];
                   ibyte++;
                   blue = decompressed_strip_data[ibyte];
                   ibyte++;
                   if( hpix >= min_hpix && hpix <= max_hpix &&
                      vpix >= min_vpix && vpix <= max_vpix )
                   {
                     img[subimage_pixel_index*3+0] = red;
                     img[subimage_pixel_index*3+1] = green;
                     img[subimage_pixel_index*3+2] = blue;
                     subimage_pixel_index++;
                   }
                   hpix++;
                   if( hpix == (int)m_image_width )
                   {
                     hpix = 0;
                     vpix++;
                     if( vpix > max_vpix )
                        goto DONE;
                   }
                   ipixel++;
                 }
                 break;
               default:
                 ASSERT( FALSE );
                 goto FAIL;
            }
         }
      }
      int dum = 0;
   }


DONE:
   if (strip_data != NULL)
      MEM_free( strip_data );
   strip_data = NULL;

   if (jred != NULL)
      MEM_free(jred);
   if (jgrn != NULL)
      MEM_free(jgrn);
   if (jblu != NULL)
      MEM_free(jblu);

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if (jred != NULL)
      MEM_free(jred);
   if (jgrn != NULL)
      MEM_free(jgrn);
   if (jblu != NULL)
      MEM_free(jblu);

   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_24bit_planar_rgb_as_rgb_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_multiband_chunky_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix, IImageLibCallback *callback )
{
   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int ibyte, decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count, psize;
   unsigned char *strip_data, *decompressed_strip_data, byte1, byte2;
   unsigned short *strip_data_16, tshort;
   int outpos, k, subx, suby, rslt;
   int iwidth, iheight, strip_cnt;

   unsigned char *jred, *jgrn, *jblu;
   CJpeg jpeg;
   int width, height, maxpos;
   CString error;
   int dummy;
   CUtil util;

   jred = jgrn = jblu = NULL;
   strip_data = NULL;
   strip_data_16 = NULL;
   decompressed_strip_data = NULL;
   decompressed_size = 0;

   if ( m_planar_configuration == GEOTIFF_PLANAR_FORMAT)
   {
      ASSERT(0);
      m_new_error_description = "Function is only for chunky images";
      return FAILURE;
   }

   // find strip offsets tag
   if ( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if ( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   width = max_hpix - min_hpix + 1;
   height = max_vpix - min_vpix + 1;

   iwidth = width;
   iheight = height;

   maxpos = width * height;

   // allocate data for strips

   if (m_bits_per_sample == 8)
   {
      strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
      if (strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }
   else if (m_bits_per_sample == 16)
   {
      strip_data_16 = (unsigned short*)MEM_malloc( m_max_strip_byte_count );
      if (strip_data_16 == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }
   else
   {
         ASSERT(0);
         m_new_error_description = "Unsupported bit depth";
         goto FAIL;
   }

   if (m_compression_scheme == GEOTIFF_JPEG)
   {
      ASSERT(0);
      m_new_error_description = "JPEG compressed multispectral chunky files are not supported";
      return FAILURE;
   }
   else if ( m_compression_scheme == GEOTIFF_PACKBITS ||
             m_compression_scheme == GEOTIFF_LZW )
   {
      // if compressed, allocate memory for a decompressed strip
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   outpos = 0;

   strip_cnt = end_strip - start_strip + 1;

   // loop through each strip
   for ( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip < end_strip)
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }

      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

      // Set the file pointer to the row offset
      if ( gtfSeek( strip_offset ) != SUCCESS )
         goto FAIL;

      // something other than JPEG compression

      // read the strip
      if (m_bits_per_sample == 8)
         rslt = gtfRead( strip_data, strip_byte_count );
      else
         rslt = gtfRead( (unsigned char*) strip_data_16, strip_byte_count );
      if (rslt != SUCCESS )
        goto FAIL;

      psize = strip_byte_count / 3;

      // select planar configuration - either chunky (rgbrgbrgb) or
      // planar (rrrgggbbb) format
      switch( m_planar_configuration )
      {
       case GEOTIFF_CHUNKY_FORMAT:
         switch( m_compression_scheme )
         {
            case GEOTIFF_NO_COMPRESSION:
               ibyte = 0;
               // copy strip data to pixel array
               for (suby=0; suby < (int) m_rows_per_strip; suby++)
               {
                  hpix = 0;
                  for (subx=0; subx < (int) m_image_width; subx++)
                  {
                     if ( hpix >= min_hpix && hpix <= max_hpix &&
                         vpix >= min_vpix && vpix <= max_vpix )
                     {
                        for (k=0; k<m_samples_per_pixel; k++)
                        {
                           if (m_bits_per_sample == 8)
                              m_image->m_band[k].m_img16bit[subimage_pixel_index] = strip_data[ibyte+k];
                           else
                           {
                              tshort = strip_data_16[ibyte+k];
                              if ( m_byte_order == GEOTIFF_BIG_ENDIAN )
                              {
                                 byte1 = tshort >> 8;
                                 byte2 = tshort & 255;
                                 tshort = byte2;
                                 tshort = tshort << 8;
                                 tshort += byte1;
                              }
                              m_image->m_band[k].m_img16bit[subimage_pixel_index] = tshort;
                           }
                        }
                        subimage_pixel_index++;
                     }
                     hpix++;
                     ibyte += m_samples_per_pixel;
                  }
                  vpix++;
               }
              break;
            case GEOTIFF_CCITT_1D:
              goto FAIL;                     // not yet supported
            case GEOTIFF_LZW:                 // LZW compression scheme
            case GEOTIFF_PACKBITS:            // PackBits compression scheme
              // decompress data
              if( m_compression_scheme == GEOTIFF_PACKBITS ||
                  m_compression_scheme == GEOTIFF_LZW )
              {
                num_remaining_bytes =
                  (m_num_pixels - ipixel) * m_samples_per_pixel;
                decompressed_size =
                  __min( (int)(m_image_width * m_rows_per_strip *
                  m_samples_per_pixel), num_remaining_bytes );
                if ( decompress_strip( (int)strip_byte_count, strip_data,
                   decompressed_size, decompressed_strip_data ) != SUCCESS )
//                    goto FAIL;
                  dummy = 0;
              }
              ibyte = 0;
               // copy strip data to pixel array
               for (suby=0; suby < (int) m_rows_per_strip; suby++)
               {
                  hpix = 0;
                  for (subx=0; subx < (int) m_image_width; subx++)
                  {
                     if ( hpix >= min_hpix && hpix <= max_hpix &&
                         vpix >= min_vpix && vpix <= max_vpix )
                     {
                        for (k=0; k<m_samples_per_pixel; k++)
                        {
                           m_image->m_band[k].m_img16bit[subimage_pixel_index] = decompressed_strip_data[ibyte+k];
                        }
                        subimage_pixel_index++;
                     }
                     hpix++;
                     ibyte += m_samples_per_pixel;
                  }
                  vpix++;
               }
              break;
            default:
              ASSERT( FALSE );
              goto FAIL;
         }
         break;

      }
   }

   if (strip_data != NULL)
      MEM_free( strip_data );
   strip_data = NULL;
   if (strip_data_16 != NULL)
      MEM_free( strip_data_16 );
   strip_data_16 = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( strip_data_16 != NULL )
   {
      MEM_free( strip_data_16 );
      strip_data_16 = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_multiband_chunky_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_multiband_planar_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix, IImageLibCallback *callback )
{
   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   int ibyte, decompressed_size;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count, psize;
   unsigned char *strip_data, *decompressed_strip_data, byte1, byte2;
   unsigned short *strip_data_16, tshort;
   int outpos, subx, suby;
   int iwidth, iheight, strip_cnt, img_size;;
   int width, height, plane;
   CString error;
   int dummy, data_cnt, band_cnt, rslt;
   CUtil util;

   if ( m_planar_configuration != GEOTIFF_PLANAR_FORMAT)
   {
      ASSERT(0);
      m_new_error_description = "Function is only for planar images";
      return FAILURE;
   }

   img_size = (max_hpix - min_hpix + 1) * (max_vpix - min_vpix + 1);

   strip_data = NULL;
   strip_data_16 = NULL;
   decompressed_strip_data = NULL;
   decompressed_size = 0;

   // find strip offsets tag
   if ( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   if ( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   width = max_hpix - min_hpix + 1;
   height = max_vpix - min_vpix + 1;

   iwidth = width;
   iheight = height;

   // allocate data for strips

   data_cnt = m_max_strip_byte_count;
   if (data_cnt < (int) m_image_width)
      data_cnt = m_image_width;

   if (m_bits_per_sample == 8)
   {
      strip_data = (unsigned char*)MEM_malloc( data_cnt );
      if (strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }
   else if (m_bits_per_sample == 16)
   {
      strip_data_16 = (unsigned short*)MEM_malloc( data_cnt );
      if (strip_data_16 == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }
   else
   {
         ASSERT(0);
         m_new_error_description = "Unsupported bit depth";
         goto FAIL;
   }

   if (( m_compression_scheme == GEOTIFF_PACKBITS ||
         m_compression_scheme == GEOTIFF_LZW ) && (m_bits_per_sample == 16))
   {
      ASSERT(0);
      m_new_error_description = "Compression unsupported for bit depth of 16";
      goto FAIL;
   }

   if (m_compression_scheme == GEOTIFF_JPEG)
   {
      m_new_error_description = "JPEG compressed planar files not supported";
      goto FAIL;
   }
   else if ( m_compression_scheme == GEOTIFF_PACKBITS ||
             m_compression_scheme == GEOTIFF_LZW )
   {
      // if compressed, allocate memory for a decompressed strip
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip * m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   outpos = 0;

   strip_cnt = end_strip - start_strip + 1;

   band_cnt = m_samples_per_pixel;

   for (plane=0; plane < band_cnt; plane++)
   {
      vpix = start_strip * m_rows_per_strip;
      ipixel = vpix * m_image_width + hpix;
      subimage_pixel_index = 0;
      // loop through each strip
      for ( istrip = start_strip; istrip <= end_strip; istrip++ )
      {
         hpix = 0;
         if ((callback != NULL) && (start_strip < end_strip))
         {
            double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
            percent *= (double) plane * 33.3333333;
            CString label = "Loading Image";
            if (!util.send_user_update_and_continue(callback, percent, label))
            {
               m_new_error_description = "Operation canceled by user";
               goto FAIL;
            }
         }

         // Get the strip offset
         if ( GetTagIndexUIntValue( istrip+(plane*m_num_strips), strip_offsets_tag_index, strip_offset ) != SUCCESS )
            goto FAIL;

         // Get the strip byte count
         if ( GetTagIndexUIntValue( istrip+(plane*m_num_strips), strip_byte_counts_tag_index, strip_byte_count ) != SUCCESS )
            goto FAIL;

         // Set the file pointer to the row offset
         if ( gtfSeek( strip_offset ) != SUCCESS )
            goto FAIL;

         // read the strip
         if (m_bits_per_sample == 8)
            rslt = gtfRead( strip_data, strip_byte_count );
         else
            rslt = gtfRead( (unsigned char*) strip_data_16, strip_byte_count );

         if (rslt != SUCCESS)
            goto FAIL;

         psize = strip_byte_count / band_cnt;

         switch( m_compression_scheme )
         {
            case GEOTIFF_LZW:                 // LZW compression scheme
            case GEOTIFF_PACKBITS:            // PackBits compression scheme
              // decompress data
              if ( m_compression_scheme == GEOTIFF_PACKBITS ||
                   m_compression_scheme == GEOTIFF_LZW )
              {
                decompressed_size = m_image_width * m_rows_per_strip;
                if ( decompress_strip( (int)strip_byte_count, strip_data,
                   decompressed_size, decompressed_strip_data ) != SUCCESS )
//                    goto FAIL;
                  dummy = 0;  // don't fail because of one bad strip
              }
              ibyte = 0;
              // copy strip data to pixel array
               for (suby=0; suby < (int) m_rows_per_strip; suby++)
               {
                  hpix = 0;
                  for (subx=0; subx < (int) m_image_width; subx++)
                  {
                     if ( hpix >= min_hpix && hpix <= max_hpix &&
                     vpix >= min_vpix && vpix <= max_vpix )
                     {
                        m_image->m_band[plane].m_img16bit[subimage_pixel_index] = decompressed_strip_data[ibyte];
                        subimage_pixel_index++;
                     }
                     hpix++;
                     ibyte++;
                  }
                  vpix++;
               }

              break;

            case GEOTIFF_NO_COMPRESSION:
               ibyte = 0;
               // copy strip data to pixel array
               for (suby=0; suby < (int) m_rows_per_strip; suby++)
               {
                  hpix = 0;
                  for (subx=0; subx < (int) m_image_width; subx++)
                  {
                     if ( hpix >= min_hpix && hpix <= max_hpix &&
                     vpix >= min_vpix && vpix <= max_vpix )
                     {

                        if (m_bits_per_sample == 8)
                           m_image->m_band[plane].m_img16bit[subimage_pixel_index] = strip_data[ibyte];
                        else
                        {
                           tshort = strip_data_16[ibyte];
                           if ( m_byte_order == GEOTIFF_BIG_ENDIAN )
                           {
                              byte1 = tshort >> 8;
                              byte2 = tshort & 255;
                              tshort = byte2;
                              tshort = tshort << 8;
                              tshort += byte1;
                           }
                           m_image->m_band[plane].m_img16bit[subimage_pixel_index] = tshort;
                        }
                        subimage_pixel_index++;
                     }
                     hpix++;
                     ibyte++;
                  }
                  vpix++;
               }
               break;
            case GEOTIFF_CCITT_1D:
              goto FAIL;                     // not yet supported

            default:
              ASSERT( FALSE );
              goto FAIL;
         }
      }
      int dum = 0;
   }

   if (strip_data != NULL)
      MEM_free( strip_data );
   strip_data = NULL;
   if (strip_data_16 != NULL)
      MEM_free( strip_data_16 );
   strip_data_16 = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_multiband_planar_subimage

// ********************************************************************************************
// ********************************************************************************************

// this function reads a tiled 24-bit rgb subimage into the argument
// red, green, and blue pixel arrays
// returns SUCCESS or FAILURE

int CGeoTiff::get_tiled_24bit_rgb_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                      unsigned char *img, IImageLibCallback *callback )
{
   int strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned char *strip_data, *decompressed_strip_data;
   int iwidth, iheight, image_hpix;

   unsigned char *jred, *jgrn, *jblu;
   CJpeg jpeg;
   int width, height, maxpos;
   CString error;
   int tile_row, tile_col, tile_hpix, tile_vpix, tile, tile_index;
   int image_vpix;
   int min_tile_row, max_tile_row, min_tile_col, max_tile_col;
   int subimage_hpix, subimage_vpix, subimage_index, subimage_width;
   int tile_num, tile_cnt;
   CUtil util;


   jred = jgrn = jblu = NULL;
   strip_data = NULL;
   decompressed_strip_data = NULL;

   // find tile offsets tag
   if ( find_tag( GEOTIFF_TILE_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find tile byte counts tag
   if ( find_tag( GEOTIFF_TILE_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
      goto FAIL;

   width = max_hpix - min_hpix + 1;
   height = max_vpix - min_vpix + 1;

   iwidth = width;
   iheight = height;

   maxpos = width * height;

   // allocate data for strips

   //   strip_data = new unsigned char[m_max_strip_byte_count];
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   subimage_width = max_hpix - min_hpix + 1;

   if (m_compression_scheme == GEOTIFF_JPEG)
   {
      // alloc memory for jpeg tiffs
      jred = (unsigned char*) MEM_malloc( m_tile_width * m_tile_length );
      jgrn = (unsigned char*) MEM_malloc( m_tile_width * m_tile_length );
      jblu = (unsigned char*) MEM_malloc( m_tile_width * m_tile_length );
      if (jred == NULL || jgrn == NULL || jblu == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }
   else if ( m_compression_scheme == GEOTIFF_PACKBITS ||
             m_compression_scheme == GEOTIFF_LZW )
   {
      // if compressed, allocate memory for a decompressed strip
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_tile_width * m_tile_length * m_samples_per_pixel * 2 );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   // determine range of tile rows and columns in subimage
   min_tile_row = min_vpix / m_tile_length;
   max_tile_row = max_vpix / m_tile_length;
   min_tile_col = min_hpix / m_tile_width;
   max_tile_col = max_hpix / m_tile_width;

   tile_num = (max_tile_row - min_tile_row + 1) * (max_tile_col - min_tile_col + 1);
   tile_cnt = 0;

   // loop through tiles covering the subimage
   for( tile_row = min_tile_row; tile_row <= max_tile_row; tile_row++ )
   {
      for( tile_col = min_tile_col; tile_col <= max_tile_col; tile_col++ )
      {
         tile_cnt++;
         double percent = (double) tile_cnt / (double) tile_num;
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }

         tile = m_num_tiles_across * tile_row + tile_col;

         if( get_tile_as_rgb_image( tile, m_tile_red_array, m_tile_green_array,
                              m_tile_blue_array ) != SUCCESS )
            goto FAIL;

         // copy data from tile
         for( tile_vpix = 0; tile_vpix < (int)m_tile_length; tile_vpix++ )
         {
            image_vpix = tile_row * m_tile_length + tile_vpix;
            if( image_vpix < min_vpix )
               continue;
            if( image_vpix > max_vpix )
               break;
            for( tile_hpix = 0; tile_hpix < (int)m_tile_width; tile_hpix++ )
            {
               image_hpix = tile_col * m_tile_width + tile_hpix;
               if( image_hpix < min_hpix )
                  continue;
               if( image_hpix > max_hpix )
                  break;
               tile_index = tile_vpix * m_tile_width + tile_hpix;
               subimage_hpix = image_hpix - min_hpix;
               subimage_vpix = image_vpix - min_vpix;
               subimage_index = subimage_vpix * subimage_width + subimage_hpix;
               img[subimage_index*3+0] = m_tile_red_array[tile_index];
               img[subimage_index*3+1] = m_tile_green_array[tile_index];
               img[subimage_index*3+2] = m_tile_blue_array[tile_index];
               tile_index++;
            }
         }
      }
   }

   if (strip_data != NULL)
      MEM_free( strip_data );
   strip_data = NULL;

   if (jred != NULL)
      MEM_free(jred);
   if (jgrn != NULL)
      MEM_free(jgrn);
   if (jblu != NULL)
      MEM_free(jblu);

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if (jred != NULL)
      MEM_free(jred);
   if (jgrn != NULL)
      MEM_free(jgrn);
   if (jblu != NULL)
      MEM_free(jblu);

   if ( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if ( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_24bit_tiled_rgb_as_rgb_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_palette_as_rgb_subimage( int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                                    unsigned char *img, IImageLibCallback *callback )
{
   // this function reads an 8-bit palette image into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   unsigned int ibyte, decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count, palette_index;
   unsigned char *strip_data, *decompressed_strip_data;
   int rslt, size;
   CUtil util;
#ifndef FULL_STRIP_READ
   unsigned int pos;
#endif
   unsigned int width, height;


   strip_data = NULL;
   decompressed_strip_data = NULL;
   decompressed_size = 0;

   // find strip offsets tag
   rslt = find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index );
   if (rslt != SUCCESS )
      goto FAIL;

   // find strip byte counts tag
   rslt = find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index );
   if (rslt != SUCCESS )
      goto FAIL;

   // allocate data for strips
   strip_data = (unsigned char*)MEM_malloc( m_max_strip_byte_count );
   if (strip_data == NULL)
   {
      ASSERT(0);
      m_new_error_description = "Error Allocating Memory";
      goto FAIL;
   }

   // if compressed, allocate memory for a decompressed strip
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data = (unsigned char*)MEM_malloc( m_image_width * m_rows_per_strip *  m_samples_per_pixel );
      if (decompressed_strip_data == NULL)
      {
         ASSERT(0);
         m_new_error_description = "Error Allocating Memory";
         goto FAIL;
      }
   }

   width = max_hpix - min_hpix + 1;
   height = max_vpix - min_vpix + 1;
   size = width * height;

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for ( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      if (start_strip < end_strip)
      {
         double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
         percent *= 100.0;
         CString label = "Loading Image";
         if (!util.send_user_update_and_continue(callback, percent, label))
         {
            m_new_error_description = "Operation canceled by user";
            goto FAIL;
         }
      }
      // Get the strip offset
      if ( GetTagIndexUIntValue( istrip, strip_offsets_tag_index, strip_offset )
         != SUCCESS )
         goto FAIL;

      // Get the strip byte count
      if ( GetTagIndexUIntValue( istrip, strip_byte_counts_tag_index, strip_byte_count )
         != SUCCESS )
         goto FAIL;

#ifndef FULL_STRIP_READ
     if ((m_compression_scheme == GEOTIFF_NO_COMPRESSION) && (m_rows_per_strip == 1))
     {
        pos = strip_offset + min_hpix;

        // Read the strip
        if ( gtfRead( pos, strip_data, width ) != SUCCESS )
           goto FAIL;
     }
     else
#endif
     {
        // Read the strip
        if ( gtfRead( strip_offset, strip_data, strip_byte_count ) != SUCCESS )
           goto FAIL;
     }

      // select planar configuration - either chunky (rgbrgbrgb) or
      // planar (rrrgggbbb) format
      switch( m_planar_configuration )
      {
         case GEOTIFF_CHUNKY_FORMAT:
            switch( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
#ifndef FULL_STRIP_READ
               if (m_rows_per_strip == 1)
                 hpix = min_hpix;
#endif
                  ibyte = 0;
                  while( ibyte < (int)strip_byte_count )
                  {
                     palette_index = strip_data[ibyte];
                     if ( (int)palette_index >= m_num_color_map_values )
                        goto FAIL;
                     ibyte++;
                     if ( hpix >= min_hpix && hpix <= max_hpix && vpix >= min_vpix && vpix <= max_vpix )
                     {
//                 ASSERT(subimage_pixel_index < size);
                   if (subimage_pixel_index < size)
                   {
                     img[subimage_pixel_index*3+0] = (unsigned char)(m_red_color_map[palette_index]/256);
                     img[subimage_pixel_index*3+1] = (unsigned char)(m_green_color_map[palette_index]/256);
                     img[subimage_pixel_index*3+2] = (unsigned char)(m_blue_color_map[palette_index]/256);
                     if ((img[subimage_pixel_index*3+0] == 0) && (img[subimage_pixel_index*3+1] == 0) &&
                        (img[subimage_pixel_index*3+2] == 0))
                     {
                        // make it not true black for transparency reasons
                        img[subimage_pixel_index*3+0] = 1;
                        img[subimage_pixel_index*3+1] = 1;
                        img[subimage_pixel_index*3+2] = 1;
                     }
                     subimage_pixel_index++;
                   }
                     }
                     hpix++;
                     if( hpix == (int)m_image_width )
                     {
                        hpix = 0;
                        vpix++;
                        if( vpix > max_vpix )
                     goto DONE;
                     }
                     ipixel++;
                  }
                  break;
               case GEOTIFF_CCITT_1D:
                  goto FAIL;                      // not yet supported
               case GEOTIFF_LZW:                 // LZW compression scheme
               case GEOTIFF_PACKBITS:             // PackBits compression scheme
                  // decompress data
try
{
                  if( m_compression_scheme == GEOTIFF_PACKBITS ||
                      m_compression_scheme == GEOTIFF_LZW )
                  {
                     num_remaining_bytes = (m_num_pixels - ipixel) * m_samples_per_pixel;
                     decompressed_size = __min( (int)(m_image_width * m_rows_per_strip *
                               m_samples_per_pixel), num_remaining_bytes );
                     rslt = decompress_strip( (int)strip_byte_count, strip_data,
                         decompressed_size, decompressed_strip_data );
//              if (rslt != SUCCESS )
//                 goto FAIL;
                  }

                  ibyte = hpix = min_hpix;
                  while( ibyte < decompressed_size )
                  {
                     if ( hpix <= max_hpix)
                     {
                   if (hpix >= min_hpix && vpix >= min_vpix && vpix <= max_vpix )
                   {
                     palette_index = decompressed_strip_data[ibyte];
                     if ( (int)palette_index >= m_num_color_map_values )
                        goto FAIL;

//                    ASSERT(subimage_pixel_index < size);
                      if (subimage_pixel_index < size)
                      {
                        img[subimage_pixel_index*3+0] = (unsigned char)(m_red_color_map[palette_index]/256);
                        img[subimage_pixel_index*3+1] = (unsigned char)(m_green_color_map[palette_index]/256);
                        img[subimage_pixel_index*3+2] = (unsigned char)(m_blue_color_map[palette_index]/256);
                        subimage_pixel_index++;
                      }
                   }

                        ibyte++;
                        hpix++;
                     }
                     else
                     {
                        hpix = min_hpix;
                        vpix++;
                        ipixel += m_image_width;
                        ibyte += (m_image_width - max_hpix - 1) + min_hpix; // jump to end of row + jump to start of next row
                        if ( vpix > max_vpix )
                           goto DONE;
                     }
                  }
}
   catch(COleException *e)
   {
      e->Delete();
      m_cumulative_error_description = "Unable to use ImageLib.dll";
      return FAILURE;
   }
   catch(CMemoryException *e)
   {
      e->Delete();
      m_cumulative_error_description = "Unable to use ImageLib.dll";
      return FAILURE;
   }
   catch(...)
   {
      m_cumulative_error_description = "Unable to use ImageLib.dll";
      return FAILURE;
   }
                  break;
               default:
                  ASSERT( FALSE );
                  goto FAIL;
            }
            break;

         case GEOTIFF_PLANAR_FORMAT:
            goto FAIL;                                     // not yet supported

         default:
            ASSERT( FALSE );
            goto FAIL;
      }

   }

DONE:
//   delete [] strip_data;
   MEM_free( strip_data );
   strip_data = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      MEM_free( strip_data );
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      MEM_free( decompressed_strip_data );
      decompressed_strip_data = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}
// end of get_8bit_palette_as_rgb_subimage

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::calculate_histogram(IImageLibCallback *callback)
{
   // this function calculates the histogram for the image
   // in a 32K color space
   // returns SUCCESS or FAILURE

   int hpix, vpix, rslt;
   unsigned int color_value;
   unsigned char *img = NULL;
   CUtil util;

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
      return FAILURE;

   // Allocate the histogram if necessary
   if ( m_puiHistogram == nullptr )
   {
      m_apuiHistogram.reset( new UINT[ 32768 ] );
      m_puiHistogram = reinterpret_cast< UINT(*)[32768] >( m_apuiHistogram.get() );
   }

   // Clear the histogram
   ZeroMemory( m_puiHistogram, sizeof(*m_puiHistogram) );

   if (m_image_is_tiled)
   {
      rslt = calculate_histogram_for_tiled_images(callback);
      return rslt;
   }

   // allocate data for one row
   img = (unsigned char*)MEM_malloc( m_image_width * 3);
   if ( img == NULL )
   {
      ASSERT(0);
      m_new_error_description = "Error allocating memory";
      goto FAIL;
   }

   // loop through each row
   for( vpix = 0; vpix < (int)m_image_length; vpix++ )
   {
      double percent = (double) (vpix) / (double) (m_image_length);
      percent *= 100.0;
      CString label = "Computing Histogram";
      if (!util.send_user_update_and_continue(callback, percent, label))
      {
         m_new_error_description = "Operation canceled by user";
         goto FAIL;
      }
      // get the row
      if( get_rgb_subimage( 0, vpix, m_image_width, 1, img, NULL ) != SUCCESS )
         goto FAIL;

      for( hpix = 0; hpix < (int)m_image_width; hpix++ )
      {
         color_value = ( img[hpix*3+0] >> 3) << 10;
         color_value |= (img[hpix*3+1] >> 3) << 5;
         color_value |= (img[hpix*3+2] >> 3);
         (*m_puiHistogram)[color_value]++;
      }
   }

   // free row memory
   MEM_free( img );
   img = NULL;

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   // free row memory
   if( img != NULL )
   {
      MEM_free( img );
      img = NULL;
   }
   util.clear_callback(callback);
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::calculate_histogram_for_tiled_images( IImageLibCallback *callback )
{
   // this function calculates the histogram for the image
   // in a 32K color space
   // returns SUCCESS or FAILURE

   int j, k, rslt;
   unsigned char *buf_red = NULL, *buf_grn = NULL, *buf_blu = NULL;
   BYTE red, grn, blu;
   int pos, max_item, max_pix;
   CUtil util;

   // return if no file loaded or there was an error loading the tiff file
   if ( !m_file_loaded || m_error )
      return FAILURE;

   max_item = m_num_tiles_down * m_num_tiles_across;
   max_pix = m_tile_width * m_tile_length;

   buf_red = (BYTE*) MEM_malloc(max_pix);
   buf_grn = (BYTE*) MEM_malloc(max_pix);
   buf_blu = (BYTE*) MEM_malloc(max_pix);
   if ( buf_red == NULL || buf_grn == NULL || buf_blu == NULL )
      goto FAIL;

   for (k=0; k<max_item; k++)
   {
      double percent = (double) (k) / (double) (max_item);
      percent *= 100.0;
      CString label = "Computing Histogram";
      if (!util.send_user_update_and_continue(callback, percent, label))
      {
         m_new_error_description = "Operation canceled by user";
         goto FAIL;
      }
      rslt = get_tile_as_rgb_image( k, buf_red, buf_grn, buf_blu);
      if (rslt != SUCCESS)
           goto FAIL;

      for (j=0; j<max_pix; j++)
      {
         // clip to 5 bits
         red = buf_red[j];
         grn = buf_grn[j];
         blu = buf_blu[j];
         red = (BYTE) (red >> 3);
         grn = (BYTE) (grn >> 3);
         blu = (BYTE) (blu >> 3);
         pos = (red << 10) + (grn << 5) + blu;
         (*m_puiHistogram)[pos] += 1;
      }
   }

   if (buf_red != NULL)
      MEM_free(buf_red);
   if (buf_grn != NULL)
      MEM_free(buf_grn);
   if (buf_blu != NULL)
      MEM_free(buf_blu);

   util.clear_callback(callback);
   return SUCCESS;

FAIL:
   if (buf_red != NULL)
      MEM_free(buf_red);
   if (buf_grn != NULL)
      MEM_free(buf_grn);
   if (buf_blu != NULL)
      MEM_free(buf_blu);

   util.clear_callback(callback);
   return FAILURE;
}
// end of calculate_histogram_for_tiled_images

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_tile_as_rgb_image( int itile, BYTE *red_array, BYTE *green_array, BYTE *blue_array )
{
   // this function returns a tile as an rgb image
   int rslt;

   // select image type and call appropriate function
   switch( m_image_type )
   {
      case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
         return FAILURE;

      case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
        if (m_samples_per_pixel == 3)
        {
           if (m_planar_configuration == GEOTIFF_PLANAR_FORMAT)
            rslt = get_24bit_planar_rgb_tile_as_rgb_image( itile, red_array, green_array, blue_array );
           else
            rslt = get_24bit_rgb_tile_as_rgb_image( itile, red_array, green_array, blue_array );
        }
        else
         rslt = get_8bit_grayscale_tile_as_rgb_image( itile, red_array, green_array, blue_array );
        if (rslt != SUCCESS )
            return FAILURE;
         break;

      case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
         if( get_16bit_grayscale_tile_as_rgb_image( itile,
            red_array, green_array, blue_array ) != SUCCESS )
            return FAILURE;
         break;

      case GEOTIFF_IMAGE_TYPE_256_COLOR:
         if( get_8bit_palette_tile_as_rgb_image( itile,
            red_array, green_array, blue_array ) != SUCCESS )
            return FAILURE;
         break;

      case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
        if (m_planar_configuration == GEOTIFF_PLANAR_FORMAT)
          rslt = get_24bit_planar_rgb_tile_as_rgb_image( itile, red_array, green_array, blue_array );
        else
          rslt = get_24bit_rgb_tile_as_rgb_image( itile, red_array, green_array, blue_array );
         if ( rslt != SUCCESS )
            return FAILURE;
         break;

         default:                           // should always be one of above
            ASSERT( FALSE );
            return FAILURE;
   }

   return SUCCESS;
}
// end of get_tile_as_rgb_image

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_grayscale_tile_as_rgb_image( int itile, BYTE *red_array, BYTE *green_array, BYTE *blue_array )
{
   // this function reads a 8-bit grayscale image tile into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i;
   unsigned int pixel_index, decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   BYTE *tile_data = NULL, *decompressed_tile_data = NULL;
   CJpeg jpeg;
   int width, height, rslt;
   CString error_msg;
   BOOL rgb_colorspace = FALSE;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // Get the tile offset and byte count
   if ( GetTagTileValues( itile, tile_offset, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // allocate memory to hold the tile data
   tile_data = NULL;
   tile_data = (unsigned char*)MEM_malloc( tile_byte_count  );
   if( tile_data == NULL )
   {
      ASSERT(0);
      m_new_error_description = "Error allocating memory";
      goto FAIL;
   }

   // Read the tile
   if ( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // copy tile data to pixel array
         pixel_index = 0;
         for( i = 0; i < (int)tile_byte_count; i++ )
         {
            red_array[pixel_index] = tile_data[i];
            pixel_index++;
         }
         break;
      case GEOTIFF_LZW:                 // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
         decompressed_tile_data = (unsigned char*)MEM_malloc( m_tile_width * m_tile_length );
       if (decompressed_tile_data == NULL)
         {
            ASSERT(0);
            m_new_error_description = "Error allocating memory";
            goto FAIL;
         }
         num_remaining_bytes = m_tile_width * m_tile_length;
         decompressed_size = m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
             goto FAIL;
         // copy decompressed strip data to pixel array
         pixel_index = 0;
         for( i = 0; (unsigned int) i < decompressed_size; i++ )
         {
            red_array[pixel_index] = decompressed_tile_data[i];
            pixel_index++;
         }
         break;
     case GEOTIFF_JPEG:                 // Jpeg compression scheme
         // set the file pointer to the tile offset
         if( gtfSeek( tile_offset ) != SUCCESS )
            goto FAIL;

         if (m_jpeg_table_len > 0)
            jpeg.set_jpeg_tables(m_jpeg_table, m_jpeg_table_len);

         width = m_tile_width;
         height = m_tile_length;
#ifdef MAPPEDFILE_FILE_INPUT
         FILE* pf;
         pf = ( m_iobMappedFileIOBufDesc._base != NULL )
            ? &m_iobMappedFileIOBufDesc : m_file_ptr;
         rslt = jpeg.check_jpeg_file( pf, error_msg );
         if ( rslt != SUCCESS )
         {
            m_new_error_description = "Found bad Jpeg stream";
            goto FAIL;
         }
         rslt = jpeg.get_jpeg_bitstream_image( pf, 0, 0, width, height,
            rgb_colorspace, red_array, green_array, blue_array, error_msg);
         if ( m_iobMappedFileIOBufDesc._base == NULL )
            m_iobMappedFileIOBufDesc._ptr = ftell( m_file_ptr ) + m_iobMappedFileIOBufDesc._base;
#elif defined READFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
         rslt = jpeg.check_jpeg_file( &m_iobMappedFileIOBufDesc, error_msg );
         if ( rslt != SUCCESS )
         {
            m_new_error_description = "Found bad Jpeg stream";
            goto FAIL;
         }
         rslt = jpeg.get_jpeg_bitstream_image( &m_iobMappedFileIOBufDesc,
                                 0, 0, width, height, rgb_colorspace,
                                 red_array, green_array, blue_array, error_msg );
#else
         rslt = jpeg.check_jpeg_file(m_file_ptr, error_msg);
         if (rslt != SUCCESS)
         {
            m_new_error_description = "Found bad Jpeg stream";
            goto FAIL;
         }
         rslt = jpeg.get_jpeg_bitstream_image(m_file_ptr, 0, 0, width, height, rgb_colorspace,
                                 red_array, green_array, blue_array, error_msg);

         m_qwFilePosition = _ftelli64( m_file_ptr );   // Recapture file position
#endif
         break;
      default:
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   // check for reversed grayscale
   if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
   {
      for( i = 0; i < (int)(m_tile_width*m_tile_length); i++ )
         red_array[i] = (BYTE) (255 - red_array[i]);
   }

   // copy red array data to green and blue arrays
   for( i = 0; i < (int)(m_tile_width*m_tile_length); i++ )
      green_array[i] = blue_array[i] = red_array[i];

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}
// end of get_8bit_grayscale_tile_as_rgb_image

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_16bit_grayscale_tile_as_rgb_image( int itile, BYTE *red_array, BYTE *green_array, BYTE *blue_array )
{
   // this function reads a 16-bit grayscale image tile into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i;
   unsigned int pixel_index, decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   BYTE *tile_data = NULL;
   BYTE *decompressed_tile_data = NULL;
   BYTE byte1, byte2;
   unsigned short *tile_16bit_data;
   double fraction;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // Get the tile offset and byte count
   if ( GetTagTileValues( itile, tile_offset, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // Allocate memory to hold the tile data
   tile_data = NULL;
   tile_data = (PBYTE) MEM_malloc( tile_byte_count  );
   if( tile_data == NULL )
      goto FAIL;

   // Read the tile
   if ( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // swap byte order if necessary
         if( m_byte_order == GEOTIFF_BIG_ENDIAN )
         {
            for( i = 0; i < (int)tile_byte_count; i += 2 )
            {
               byte1 = tile_data[i];
               byte2 = tile_data[i+1];
               tile_data[i] = byte2;
               tile_data[i+1] = byte1;
            }
         }
         // copy tile data to pixel array
         tile_16bit_data = (unsigned short*)tile_data;
         pixel_index = 0;
         for( i = 0; i < (int)tile_byte_count/2; i++ )
         {
            fraction = (double)(tile_16bit_data[i]-m_min_sample_value)/
               (m_max_sample_value-m_min_sample_value);
            if( fraction < 0.0 )
               fraction = 0.0;
            if( fraction > 1.0 )
               fraction = 1.0;
            red_array[pixel_index] = (unsigned char)(255*fraction+0.5);
            pixel_index++;
         }
         break;
      case GEOTIFF_LZW:                 // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
         decompressed_tile_data =
            (unsigned char*)MEM_malloc( 2 * m_tile_width * m_tile_length );
         if ( decompressed_tile_data == NULL )
         {
            ASSERT(0);
            m_new_error_description = "Error Allocating Memory";
            goto FAIL;
         }
         num_remaining_bytes = 2 * m_tile_width * m_tile_length;
         decompressed_size = 2 * m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data, decompressed_size, decompressed_tile_data ) != SUCCESS )
         {
            m_new_error_description = "Error decompressing packbits";
            goto FAIL;
         }
         // swap byte order if necessary
         if( m_byte_order == GEOTIFF_BIG_ENDIAN )
         {
            for( i = 0; i < (int)decompressed_size; i += 2 )
            {
               byte1 = decompressed_tile_data[i];
               byte2 = decompressed_tile_data[i+1];
               decompressed_tile_data[i] = byte2;
               decompressed_tile_data[i+1] = byte1;
            }
         }
         // copy decompressed strip data to pixel array
         tile_16bit_data = (unsigned short*)decompressed_tile_data;
         pixel_index = 0;
         for( i = 0; (unsigned int) i < decompressed_size/2; i++ )
         {
            fraction = (double)(tile_16bit_data[i]-m_min_sample_value)/
               (m_max_sample_value-m_min_sample_value);
            if( fraction < 0.0 )
               fraction = 0.0;
            if( fraction > 1.0 )
               fraction = 1.0;
            red_array[pixel_index] = (unsigned char)(255*fraction+0.5);
            pixel_index++;
         }
         break;
      default:
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   // check for reversed grayscale
   if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
   {
      for( i = 0; i < (int)(m_tile_width*m_tile_length); i++ )
         red_array[i] = (BYTE) (255 - red_array[i]);
   }

   // copy red array data to green and blue arrays
   for( i = 0; i < (int)(m_tile_width*m_tile_length); i++ )
      green_array[i] = blue_array[i] = red_array[i];

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}
// end of get_16bit_grayscale_tile_as_rgb_image

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_grayscale_tile_as_8bit( int itile, unsigned char *img )
{
   // this function reads a 8-bit grayscale image tile into the img pixel array
   // returns SUCCESS or FAILURE

   int i;
   unsigned int decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   BYTE *tile_data = NULL;
   BYTE *decompressed_tile_data = NULL;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // Get the tile offset and byte count
   if ( GetTagTileValues( itile, tile_offset, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // Allocate memory to hold the tile data
   tile_data = NULL;
   tile_data = (PBYTE) MEM_malloc( tile_byte_count  );
   if( tile_data == NULL )
      goto FAIL;

   // Read the tile
   if ( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // copy tile data to pixel array
       memcpy(img, tile_data, tile_byte_count);
         break;

      case GEOTIFF_LZW:                 // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
         decompressed_tile_data =
            (unsigned char*)MEM_malloc( m_tile_width * m_tile_length );
         if ( decompressed_tile_data == NULL )
         {
            ASSERT(0);
            m_new_error_description = "Error Allocating Memory";
            goto FAIL;
         }
         num_remaining_bytes = m_tile_width * m_tile_length;
         decompressed_size = m_tile_width * m_tile_length;
         // decompress data
         if ( decompress_strip( (int)tile_byte_count, tile_data, decompressed_size, decompressed_tile_data ) != SUCCESS )
         {
            m_new_error_description = "Error decompressing packbits";
            goto FAIL;
         }
         // copy decompressed strip data to pixel array
       memcpy(img, tile_data, tile_byte_count);
         break;
      default:
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   // check for reversed grayscale
   if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
   {
      for( i = 0; i < (int)(m_tile_width*m_tile_length); i++ )
         img[i] = 255 - img[i];
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}
// end of get_8bit_grayscale_tile_as_8bit

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_16bit_grayscale_tile_as_16bit( int itile, unsigned short *img )
{
   // this function reads a 16-bit grayscale image tile into the img pixel array
   // returns SUCCESS or FAILURE

   int i;
   unsigned int decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   BYTE *tile_data = NULL;
   BYTE *decompressed_tile_data = NULL;
   BYTE byte1, byte2;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // Get the tile offset and byte count
   if ( GetTagTileValues( itile, tile_offset, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // Allocate memory to hold the tile data
   tile_data = NULL;
   tile_data = (PBYTE) MEM_malloc( tile_byte_count  );
   if( tile_data == NULL )
      goto FAIL;

   // Read the tile
   if ( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // swap byte order if necessary
         if( m_byte_order == GEOTIFF_BIG_ENDIAN )
         {
            for( i = 0; i < (int)tile_byte_count; i += 2 )
            {
               byte1 = tile_data[i];
               byte2 = tile_data[i+1];
               tile_data[i] = byte2;
               tile_data[i+1] = byte1;
            }
         }
         // copy tile data to pixel array
       memcpy(img, tile_data, tile_byte_count);
         break;

      case GEOTIFF_LZW:                 // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
         decompressed_tile_data =
            (unsigned char*)MEM_malloc( 2 * m_tile_width * m_tile_length );
         if ( decompressed_tile_data == NULL )
         {
            ASSERT(0);
            m_new_error_description = "Error Allocating Memory";
            goto FAIL;
         }
         num_remaining_bytes = 2 * m_tile_width * m_tile_length;
         decompressed_size = 2 * m_tile_width * m_tile_length;
         // decompress data
         if ( decompress_strip( (int)tile_byte_count, tile_data, decompressed_size, decompressed_tile_data ) != SUCCESS )
         {
            m_new_error_description = "Error decompressing packbits";
            goto FAIL;
         }
         // swap byte order if necessary
         if( m_byte_order == GEOTIFF_BIG_ENDIAN )
         {
            for( i = 0; i < (int)decompressed_size; i += 2 )
            {
               byte1 = decompressed_tile_data[i];
               byte2 = decompressed_tile_data[i+1];
               decompressed_tile_data[i] = byte2;
               decompressed_tile_data[i+1] = byte1;
            }
         }
         // copy decompressed strip data to pixel array
       memcpy(img, tile_data, tile_byte_count);
         break;
      default:
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   // check for reversed grayscale
   if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
   {
      for( i = 0; i < (int)(m_tile_width*m_tile_length); i++ )
         img[i] = 65535 - img[i];
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}
// end of get_16bit_grayscale_tile_as_16bit

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_24bit_rgb_tile_as_rgb_image( int itile, BYTE *red_array, BYTE *green_array, BYTE *blue_array )
{
   // this function reads a tiled 24-bit rgb image tile into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, pix_count;
   unsigned int pixel_index, decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   BYTE *tile_data = NULL, *decompressed_tile_data = NULL;
   CString error_msg;
   CJpeg jpeg;
   int rslt, width, height;
   BOOL rgb_colorspace = FALSE;

   if (m_photometric_interpretation == GEOTIFF_RGB)
      rgb_colorspace = TRUE;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
   {
      m_new_error_description = "Invalid tile index";
      goto FAIL;
   }

   // Get the tile offset and byte count
   if ( GetTagTileValues( itile, tile_offset, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // allocate memory to hold the tile data
   tile_data = NULL;
   tile_data = (unsigned char*)MEM_malloc( tile_byte_count  );
   if( tile_data == NULL )
   {
      ASSERT(0);
      m_new_error_description = "Error allocating memory";
      goto FAIL;
   }

   // Read the tile
   if( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
   {
      m_new_error_description = "Error in file read";
      goto FAIL;
   }

   pix_count = m_tile_width * m_tile_length;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // copy tile data to pixel array
         pixel_index = 0;
         for( i = 0; i < (int)tile_byte_count; i+=3 )
         {
         red_array[pixel_index] = tile_data[i];
         green_array[pixel_index] = tile_data[i+1];
         blue_array[pixel_index] = tile_data[i+2];
           pixel_index++;
         }
         break;

      case GEOTIFF_LZW:                 // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
         decompressed_tile_data =
            (unsigned char*)MEM_malloc( 3 * m_tile_width * m_tile_length );
         num_remaining_bytes = 3 * m_tile_width * m_tile_length;
         decompressed_size = 3 * m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
         {
            m_new_error_description = "Error decompressing packbits";
            goto FAIL;
         }
         // copy decompressed strip data to pixel array
         pixel_index = 0;
         for( i = 0; (unsigned int) i < decompressed_size; i+= 3 )
         {
            red_array[pixel_index] = decompressed_tile_data[i];
            green_array[pixel_index] = decompressed_tile_data[i+1];
            blue_array[pixel_index] = decompressed_tile_data[i+2];
            pixel_index++;
         }
         break;

      case GEOTIFF_JPEG:                 // Jpeg compression scheme
         // set the file pointer to the tile offset
         if( gtfSeek( tile_offset ) != SUCCESS )
            goto FAIL;

         if (m_jpeg_table_len > 0)
            jpeg.set_jpeg_tables(m_jpeg_table, m_jpeg_table_len);

         width = m_tile_width;
         height = m_tile_length;
#ifdef MAPPEDFILE_FILE_INPUT
         FILE* pf;
         pf = ( m_iobMappedFileIOBufDesc._base != NULL )
            ? &m_iobMappedFileIOBufDesc : m_file_ptr;
         rslt = jpeg.check_jpeg_file( pf, error_msg );
         if ( rslt != SUCCESS )
         {
            m_new_error_description = "Found bad Jpeg stream";
            goto FAIL;
         }
         rslt = jpeg.get_jpeg_bitstream_image( pf, 0, 0, width, height,
            rgb_colorspace, red_array, green_array, blue_array, error_msg);
         if ( m_iobMappedFileIOBufDesc._base == NULL )
            m_iobMappedFileIOBufDesc._ptr = ftell( m_file_ptr ) + m_iobMappedFileIOBufDesc._base;
#elif defined READFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
         rslt = jpeg.check_jpeg_file( &m_iobMappedFileIOBufDesc, error_msg );
         if ( rslt != SUCCESS )
         {
            m_new_error_description = "Found bad Jpeg stream";
            goto FAIL;
         }
         rslt = jpeg.get_jpeg_bitstream_image( &m_iobMappedFileIOBufDesc,
                                 0, 0, width, height, rgb_colorspace,
                                 red_array, green_array, blue_array, error_msg );
#else
         rslt = jpeg.check_jpeg_file(m_file_ptr, error_msg);
         if (rslt != SUCCESS)
         {
            m_new_error_description = "Found bad Jpeg stream";
            goto FAIL;
         }
         rslt = jpeg.get_jpeg_bitstream_image(m_file_ptr, 0, 0, width, height, rgb_colorspace,
                                 red_array, green_array, blue_array, error_msg);

         m_qwFilePosition = _ftelli64( m_file_ptr );   // Recapture file position
#endif
         break;

      default:
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}
// end of get_24bit_rgb_tile_as_rgb_image

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_24bit_planar_rgb_tile_as_rgb_image( int itile, BYTE *red_array, BYTE *green_array, BYTE *blue_array )
{
   // this function reads a tiled 24-bit rgb image tile into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, k, nTile;
   unsigned int pixel_index, decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   BYTE *tile_data = NULL, *decompressed_tile_data = NULL;
   CString error_msg;
   CJpeg jpeg;
   int rslt, width, height;
   BOOL rgb_colorspace = FALSE;

   if (m_photometric_interpretation == GEOTIFF_RGB)
      rgb_colorspace = TRUE;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
   {
      m_new_error_description = "Invalid tile index";
      goto FAIL;
   }

   for (k=0; k<3; k++)
   {
      nTile = itile + (k * m_num_tiles);

      // Get the tile offset and byte count
      if ( GetTagTileValues( nTile, tile_offset, tile_byte_count ) != SUCCESS )
        goto FAIL;

      // allocate memory to hold the tile data
      if (tile_data != NULL)
         MEM_free(tile_data);
      tile_data = (unsigned char*)MEM_malloc( tile_byte_count  );
      if( tile_data == NULL )
      {
         ASSERT(0);
         m_new_error_description = "Error allocating memory";
         goto FAIL;
      }

      // Read the tile
      if( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
      {
         m_new_error_description = "Error in file read";
         goto FAIL;
      }

      int pix_count = m_tile_width * m_tile_length;

      // check compression scheme
      switch( m_compression_scheme )
      {
        case GEOTIFF_NO_COMPRESSION:
          // copy tile data to pixel array
          pixel_index = 0;
          for( i = 0; i < (int)tile_byte_count; i++ )
          {
             if (k == 0)
               red_array[pixel_index] = tile_data[i];
             else if (k == 1)
               green_array[pixel_index] = tile_data[i];
             else
               blue_array[pixel_index] = tile_data[i];
             pixel_index++;
          }
          break;

        case GEOTIFF_LZW:                 // LZW compression scheme
        case GEOTIFF_PACKBITS:                 // PackBits compression scheme
          // allocate memory for decompressed data
          decompressed_tile_data = NULL;
          decompressed_tile_data =
            (unsigned char*)MEM_malloc( 3 * m_tile_width * m_tile_length );
          num_remaining_bytes = 3 * m_tile_width * m_tile_length;
          decompressed_size = 3 * m_tile_width * m_tile_length;
          // decompress data
          if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
            {
               m_new_error_description = "Error decompressing packbits";
               goto FAIL;
            }
          // copy decompressed strip data to pixel array
          pixel_index = 0;
          for( i = 0; (unsigned int) i < decompressed_size; i+= 3 )
          {
             if (k == 0)
               red_array[pixel_index] = decompressed_tile_data[i];
             else if (k == 1)
               green_array[pixel_index] = decompressed_tile_data[i];
             else
               blue_array[pixel_index] = decompressed_tile_data[i];
             pixel_index++;
          }
          break;

        case GEOTIFF_JPEG:                 // Jpeg compression scheme
            // set the file pointer to the tile offset
            if( gtfSeek( tile_offset ) != SUCCESS )
               goto FAIL;

            if (m_jpeg_table_len > 0)
               jpeg.set_jpeg_tables(m_jpeg_table, m_jpeg_table_len);

            width = m_tile_width;
            height = m_tile_length;
   #ifdef MAPPEDFILE_FILE_INPUT
          FILE* pf;
          pf = ( m_iobMappedFileIOBufDesc._base != NULL )
            ? &m_iobMappedFileIOBufDesc : m_file_ptr;
            rslt = jpeg.check_jpeg_file( pf, error_msg );
            if ( rslt != SUCCESS )
            {
               m_new_error_description = "Found bad Jpeg stream";
               goto FAIL;
            }
            rslt = jpeg.get_jpeg_bitstream_image( pf, 0, 0, width, height,
            rgb_colorspace, red_array, green_array, blue_array, error_msg);
          if ( m_iobMappedFileIOBufDesc._base == NULL )
            m_iobMappedFileIOBufDesc._ptr = ftell( m_file_ptr ) + m_iobMappedFileIOBufDesc._base;
   #elif defined READFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
            rslt = jpeg.check_jpeg_file( &m_iobMappedFileIOBufDesc, error_msg );
            if ( rslt != SUCCESS )
            {
               m_new_error_description = "Found bad Jpeg stream";
               goto FAIL;
            }
            rslt = jpeg.get_jpeg_bitstream_image( &m_iobMappedFileIOBufDesc,
                            0, 0, width, height, rgb_colorspace,
                                    red_array, green_array, blue_array, error_msg );
   #else
            rslt = jpeg.check_jpeg_file(m_file_ptr, error_msg);
            if (rslt != SUCCESS)
            {
               m_new_error_description = "Found bad Jpeg stream";
               goto FAIL;
            }
            rslt = jpeg.get_jpeg_bitstream_image(m_file_ptr, 0, 0, width, height, rgb_colorspace,
                                    red_array, green_array, blue_array, error_msg);

          m_qwFilePosition = _ftelli64( m_file_ptr );   // Recapture file position
   #endif
          break;

        default:
          ASSERT( FALSE );
          goto FAIL;
      }
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}
// end of get_24bit_planar_rgb_tile_as_rgb_image

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::get_8bit_palette_tile_as_rgb_image(int itile, BYTE *red_array, BYTE *green_array, BYTE *blue_array)
{
   // this function reads a 8-bit palette image tile into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i;
   unsigned int decompressed_size, num_remaining_bytes;
   int palette_index;
   unsigned int tile_offset, tile_byte_count;
   BYTE *tile_data = NULL, *decompressed_tile_data = NULL;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // Get the tile offset and byte count
   if ( GetTagTileValues( itile, tile_offset, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // allocate memory to hold the tile data
   tile_data = NULL;
   tile_data = (unsigned char*)MEM_malloc( tile_byte_count  );
   if( tile_data == NULL )
      goto FAIL;

   // Read the tile
   if ( gtfRead( tile_offset, tile_data, tile_byte_count ) != SUCCESS )
      goto FAIL;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // copy tile data to pixel array
         for( i = 0; i < (int)tile_byte_count; i++ )
         {
            palette_index = tile_data[i];
            if( (int)palette_index >= m_num_color_map_values )
               goto FAIL;
            red_array[i] =
               (unsigned char)(m_red_color_map[palette_index]/256);
            green_array[i] =
               (unsigned char)(m_green_color_map[palette_index]/256);
            blue_array[i] =
               (unsigned char)(m_blue_color_map[palette_index]/256);
         }
         break;
      case GEOTIFF_LZW:                 // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
         decompressed_tile_data =
            (unsigned char*)MEM_malloc( m_tile_width * m_tile_length );
         num_remaining_bytes = m_tile_width * m_tile_length;
         decompressed_size = m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
             goto FAIL;
         // copy decompressed strip data to pixel array
         for( i = 0; i < (int)m_num_tile_pixels; i++ )
         {
            palette_index = decompressed_tile_data[i];
            if( (int)palette_index >= m_num_color_map_values )
               goto FAIL;
            red_array[i] =
               (unsigned char)(m_red_color_map[palette_index]/256);
            green_array[i] =
               (unsigned char)(m_green_color_map[palette_index]/256);
            blue_array[i] =
               (unsigned char)(m_blue_color_map[palette_index]/256);
         }
         break;
      default:
         ASSERT( FALSE );
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      MEM_free( tile_data );
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      MEM_free( decompressed_tile_data );
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}
// end of get_8bit_palette_tile_as_rgb_image

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::read_string( char *string, int length )
{
   // reads a string of specified length into the specified buffer
   // terminating character is converted from '|' to a null
   // returns SUCCESS or FAILURE
   int i;

   if ( gtfRead( (PBYTE) string, length ) == SUCCESS )
   {
      for( i = length-1; i >= 0; i-- )
      {
         if( string[i] == '|' )
         {
            string[i] = NULL;
            break;
         }
      }
      // guarantee null terminated
      string[length-1] = NULL;
      return SUCCESS;
   }
   else
      return FAILURE;
}

int CGeoTiff::read_signed_short( short &value )
{
   // reads a signed short into the argument
   // returns SUCCESS or FAILURE
   INT iResult;

   switch( m_byte_order )
   {
      case GEOTIFF_LITTLE_ENDIAN:
         // read two bytes straight into argument
         iResult = gtfRead( (PBYTE) &value, sizeof(short) );
         break;

      case GEOTIFF_BIG_ENDIAN:
         iResult = gtfRead( (PBYTE) &value, sizeof(short) );

         // Reverse byte order
         reverse_byte_order( (PCHAR) &value, sizeof(short) );
         break;

      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
      default:
         iResult = FAILURE;
   }
   return iResult;
}



// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::read_unsigned_short( unsigned short &value )
{
   // reads an unshort into the argument
   // returns SUCCESS or FAILURE

   INT iResult;

   switch( m_byte_order )
   {
      case GEOTIFF_LITTLE_ENDIAN:
         // read two bytes straight into argument
         iResult = gtfRead( (PBYTE) &value, sizeof(unsigned short) );
         break;

      case GEOTIFF_BIG_ENDIAN:
         iResult = gtfRead( (PBYTE) &value, sizeof(unsigned short) );
         reverse_byte_order( (PCHAR) &value, sizeof(unsigned short) );
         break;

      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
      default:
         iResult = FAILURE;
   }
   return iResult;
}


// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::read_signed_int( int &value )
{
   // reads a signed integer into the argument
   // returns SUCCESS or FAILURE
   INT iResult;

   switch( m_byte_order )
   {
      case GEOTIFF_LITTLE_ENDIAN:
         // Read four bytes straight into argument
         iResult = gtfRead( (PBYTE) &value, sizeof(INT) );
         break;

      case GEOTIFF_BIG_ENDIAN:
         iResult = gtfRead( (PBYTE) &value, sizeof(INT) );
         // Reverse byte order
         reverse_byte_order( (PCHAR) &value, sizeof(INT) );
         break;

      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
      default:
         iResult = FAILURE;

   }
   return iResult;
}



// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::read_unsigned_int( UINT& value )
{
   // reads an unsigned integer into the argument
   // returns SUCCESS or FAILURE
   INT iResult;

   switch( m_byte_order )
   {
      case GEOTIFF_LITTLE_ENDIAN:
         // Read four bytes straight into argument
         iResult = gtfRead( (PBYTE) &value, sizeof(UINT) );
         break;

      case GEOTIFF_BIG_ENDIAN:
         iResult = gtfRead( (PBYTE) &value, sizeof(UINT) );
         // Reverse byte order
         reverse_byte_order( (PCHAR) &value, sizeof(UINT) );
         break;

      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
      default:
         iResult = FAILURE;

   }
   return iResult;
}



// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::read_float( FLOAT& value )
{
   // reads a float into the argument
   // returns SUCCESS or FAILURE
   INT iResult;

   switch( m_byte_order )
   {
      case GEOTIFF_LITTLE_ENDIAN:
         // Read four bytes straight into argument
         iResult = gtfRead( (PBYTE) &value, sizeof(FLOAT) );
         break;

      case GEOTIFF_BIG_ENDIAN:
         iResult = gtfRead( (PBYTE) &value, sizeof(FLOAT) );
         // Reverse byte order
         reverse_byte_order( (PCHAR) &value, sizeof(FLOAT) );
         break;

      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
      default:
         iResult = FAILURE;

   }
   return iResult;
}

// ********************************************************************************************
// ********************************************************************************************

int CGeoTiff::read_double( double& value )
{
   // reads a double into the argument
   // returns SUCCESS or FAILURE
   int iResult;

   switch( m_byte_order )
   {
      case GEOTIFF_LITTLE_ENDIAN:
         // Read 8 bytes straight into argument
         iResult = gtfRead( (PBYTE) &value, sizeof(DOUBLE) );
         break;

      case GEOTIFF_BIG_ENDIAN:
         iResult = gtfRead( (PBYTE) &value, sizeof(DOUBLE) );

         // Reverse byte order
         reverse_byte_order( (PCHAR) &value, sizeof(DOUBLE) );
         break;

      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
      default:
         iResult = FAILURE;
         break;         // FAILURE

  }

  return iResult;
}

// ********************************************************************************************
// ********************************************************************************************

void CGeoTiff::reverse_byte_order( char *buffer, int num_bytes )
{
   // this function reverses the byte order for the buffer argument
   // num_bytes specifies how many bytes there are in the buffer

   int i, j;
   char temp;

   for( i = 0, j = num_bytes-1; i < num_bytes/2; i++, j-- )
   {
      temp = buffer[i];
      buffer[i] = buffer[j];
      buffer[j] = temp;
   }
}

// ********************************************************************************************
// ********************************************************************************************

CString CGeoTiff::get_tag_name( unsigned short tag_id )
{
   // returns the name of the tag with id tag_id

   CString tag_name;

   switch( tag_id )
   {
      case GEOTIFF_NEW_SUBFILE_TYPE_TAG:
         tag_name = "NewSubfileType";
         break;
      case GEOTIFF_SUBFILE_TYPE_TAG:
         tag_name = "SubfileType";
         break;
      case GEOTIFF_IMAGE_WIDTH_TAG:
         tag_name = "ImageWidth";
         break;
      case GEOTIFF_IMAGE_LENGTH_TAG:
         tag_name = "ImageLength";
         break;
      case GEOTIFF_BITS_PER_SAMPLE_TAG:
         tag_name = "BitsPerSample";
         break;
      case GEOTIFF_COMPRESSION_TAG:
         tag_name = "Compression";
         break;
      case GEOTIFF_PHOTOMETRIC_INTERPRETATION_TAG:
         tag_name = "PhotometricInterpretation";
         break;
      case GEOTIFF_THRESHHOLDING_TAG:
         tag_name = "Threshholding";
         break;
      case GEOTIFF_CELL_WIDTH_TAG:
         tag_name = "CellWidth";
         break;
      case GEOTIFF_CELL_LENGTH_TAG:
         tag_name = "CellLength";
         break;
      case GEOTIFF_FILL_ORDER_TAG:
         tag_name = "FillOrder";
         break;
      case GEOTIFF_DOCUMENT_NAME_TAG:
         tag_name = "DocumentName";
         break;
      case GEOTIFF_IMAGE_DESCRIPTION_TAG:
         tag_name = "ImageDescription";
         break;
      case GEOTIFF_MAKE_TAG:
         tag_name = "Make";
         break;
      case GEOTIFF_MODEL_TAG:
         tag_name = "Model";
         break;
      case GEOTIFF_STRIP_OFFSETS_TAG:
         tag_name = "StripOffsets";
         break;
      case GEOTIFF_ORIENTATION_TAG:
         tag_name = "Orientation";
         break;
      case GEOTIFF_SAMPLES_PER_PIXEL_TAG:
         tag_name = "SamplesPerPixel";
         break;
      case GEOTIFF_ROWS_PER_STRIP_TAG:
         tag_name = "RowsPerStrip";
         break;
      case GEOTIFF_STRIP_BYTE_COUNTS_TAG:
         tag_name = "StripByteCounts";
         break;
      case GEOTIFF_MIN_SAMPLE_VALUE_TAG:
         tag_name = "MinSampleValue";
         break;
      case GEOTIFF_MAX_SAMPLE_VALUE_TAG:
         tag_name = "MaxSampleValue";
         break;
      case GEOTIFF_X_RESOLUTION_TAG:
         tag_name = "XResolution";
         break;
      case GEOTIFF_Y_RESOLUTION_TAG:
         tag_name = "YResolution";
         break;
      case GEOTIFF_PLANAR_CONFIGURATION_TAG:
         tag_name = "PlanarConfiguration";
         break;
      case GEOTIFF_PAGE_NAME_TAG:
         tag_name = "PageName";
         break;
      case GEOTIFF_X_POSITION_TAG:
         tag_name = "XPosition";
         break;
      case GEOTIFF_Y_POSITION_TAG:
         tag_name = "YPosition";
         break;
      case GEOTIFF_FREE_OFFSETS_TAG:
         tag_name = "FreeOffsets";
         break;
      case GEOTIFF_FREE_BYTE_COUNTS_TAG:
         tag_name = "FreeByteCounts";
         break;
      case GEOTIFF_GRAY_RESPONSE_UNIT_TAG:
         tag_name = "GrayResponseUnit";
         break;
      case GEOTIFF_GRAY_RESPONSE_CURVE_TAG:
         tag_name = "GrayResponseCurve";
         break;
      case GEOTIFF_T4_OPTIONS_TAG:
         tag_name = "T4Options";
         break;
      case GEOTIFF_T6_OPTIONS_TAG:
         tag_name = "T6Options";
         break;
      case GEOTIFF_RESOLUTION_UNIT_TAG:
         tag_name = "ResolutionUnit";
         break;
      case GEOTIFF_PAGE_NUMBER_TAG:
         tag_name = "PageNumber";
         break;
      case GEOTIFF_TRANSFER_FUNCTION_TAG:
         tag_name = "TransferFunction";
         break;
      case GEOTIFF_SOFTWARE_TAG:
         tag_name = "Software";
         break;
      case GEOTIFF_DATE_TIME_TAG:
         tag_name = "DateTime";
         break;
      case GEOTIFF_ARTIST_TAG:
         tag_name = "Artist";
         break;
      case GEOTIFF_HOST_COMPUTER_TAG:
         tag_name = "HostComputer";
         break;
      case GEOTIFF_PREDICTOR_TAG:
         tag_name = "Predictor";
         break;
      case GEOTIFF_WHITE_POINT_TAG:
         tag_name = "WhitePoint";
         break;
      case GEOTIFF_PRIMARY_CHROMATICITIES_TAG:
         tag_name = "PrimaryChromaticities";
         break;
      case GEOTIFF_COLOR_MAP_TAG:
         tag_name = "ColorMap";
         break;
      case GEOTIFF_HALFTONE_HINTS_TAG:
         tag_name = "HalftoneHints";
         break;
      case GEOTIFF_TILE_WIDTH_TAG:
         tag_name = "TileWidth";
         break;
      case GEOTIFF_TILE_LENGTH_TAG:
         tag_name = "TileLength";
         break;
      case GEOTIFF_TILE_OFFSETS_TAG:
         tag_name = "TileOffsets";
         break;
      case GEOTIFF_TILE_BYTE_COUNTS_TAG:
         tag_name = "TileByteCounts";
         break;
      case GEOTIFF_INK_SET_TAG:
         tag_name = "InkSet";
         break;
      case GEOTIFF_INK_NAMES_TAG:
         tag_name = "InkNames";
         break;
      case GEOTIFF_NUMBER_OF_INKS_TAG:
         tag_name = "NumberOfInks";
         break;
      case GEOTIFF_DOT_RANGE_TAG:
         tag_name = "DotRange";
         break;
      case GEOTIFF_TARGET_PRINTER_TAG:
         tag_name = "TargetPrinter";
         break;
      case GEOTIFF_EXTRA_SAMPLES_TAG:
         tag_name = "ExtraSamples";
         break;
      case GEOTIFF_SAMPLE_FORMAT_TAG:
         tag_name = "SampleFormat";
         break;
      case GEOTIFF_S_MIN_SAMPLE_VALUE_TAG:
         tag_name = "SMinSampleValue";
         break;
      case GEOTIFF_S_MAX_SAMPLE_VALUE_TAG:
         tag_name = "SMaxSampleValue";
         break;
      case GEOTIFF_TRANSFER_RANGE_TAG:
         tag_name = "TransferRange";
         break;
      case GEOTIFF_JPEG_TABLES_TAG:
         tag_name = "JPEGTables";
         break;
      case GEOTIFF_JPEG_PROC_TAG:
         tag_name = "JPEGProc";
         break;
      case GEOTIFF_JPEG_INTERCHANGE_FORMAT_TAG:
         tag_name = "JPEGInterchangeFormat";
         break;
      case GEOTIFF_JPEG_INTERCHANGE_FORMAT_LNGTH_TAG:
         tag_name = "JPEGInterchangeFormatLngth";
         break;
      case GEOTIFF_JPEG_RESTART_INTERVAL_TAG:
         tag_name = "JPEGRestartInterval";
         break;
      case GEOTIFF_JPEG_LOSSLESS_PREDICTORS_TAG:
         tag_name = "JPEGLosslessPredictors";
         break;
      case GEOTIFF_JPEG_POINT_TRANSFORMS_TAG:
         tag_name = "JPEGPointTransforms";
         break;
      case GEOTIFF_JPEG_Q_TABLES_TAG:
         tag_name = "JPEGQTables";
         break;
      case GEOTIFF_JPEG_DC_TABLES_TAG:
         tag_name = "JPEGDCTables";
         break;
      case GEOTIFF_JPEC_AC_TABLES_TAG:
         tag_name = "JPEGACTables";
         break;
      case GEOTIFF_YCBCR_COEFFICIENTS_TAG:
         tag_name = "YCbCrCoefficients";
         break;
      case GEOTIFF_YCBCR_SUB_SAMPLING_TAG:
         tag_name = "YCbCrSubSampling";
         break;
      case GEOTIFF_YCBCR_POSITIONING_TAG:
         tag_name = "YCbCrPositioning";
         break;
      case GEOTIFF_REFERENCE_BLACK_WHITE_TAG:
         tag_name = "ReferenceBlackWhite";
         break;
      case GEOTIFF_XML_PACKET_TAG:
         tag_name = "XMLPacketTag";
         break;
      case GEOTIFF_COPYRIGHT_TAG:
         tag_name = "Copyright";
         break;
      case GEOTIFF_MODEL_PIXEL_SCALE_TAG:
         tag_name = "ModelPixelScaleTag";
         break;
      case GEOTIFF_MODEL_TRANSFORMATION_TAG:
         tag_name = "ModelTransformationTag";
         break;
      case GEOTIFF_MODEL_TIEPOINT_TAG:
         tag_name = "ModelTiepointTag";
         break;
      case GEOTIFF_GEOKEY_DIRECTORY_TAG :
         tag_name = "GeoKeyDirectoryTag";
         break;
      case GEOTIFF_GEO_DOUBLE_PARAMS_TAG:
         tag_name = "GeoDoubleParamsTag";
         break;
      case GEOTIFF_GEO_ASCII_PARAMS_TAG:
         tag_name = "GeoAsciiParamsTag";
         break;
      case GEOTIFF_INTERGRAPH_MATRIX_TAG:
         tag_name = "IntergraphMatrixTag";
         break;
      case GEOTIFF_IPTC_NAA_CHUNK:
         tag_name = "IPTC_NAA_ChunkTag";
         break;
      case GEOTIFF_PHOTOSHOP_CHUNK:
         tag_name = "PhotoshopChunkTag";
         break;
      case GEOTIFF_EXIF_IFD:
         tag_name = "EXIF_IFD_Tag";
         break;
      default:
         tag_name.Format( "Unknown: %u", tag_id );
         break;
   }

   return tag_name;
}

// ********************************************************************************************
// ********************************************************************************************

CString CGeoTiff::get_type_name( unsigned short type )
{
   // returns the name of the specified tag data type

   CString type_name;

   switch( type )
   {
      case GEOTIFF_BYTE:              // 8-bit unsigned integer
         type_name = "BYTE";
         break;
      case GEOTIFF_ASCII:             // null-terminated string
         type_name = "ASCII";
         break;
      case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
         type_name = "SHORT";
        break;
      case GEOTIFF_LONG:              // unsigned long (32-bit) integer
         type_name = "LONG";
         break;
      case GEOTIFF_RATIONAL:          // num/denom pair of unsigned longs
         type_name = "RATIONAL";
         break;
      case GEOTIFF_SBYTE:             // 8-bit signed integer
         type_name = "SBYTE";
         break;
      case GEOTIFF_UNDEFINED_TYPE:         // 8-bit undefined data type
         type_name = "UNDEFINED TYPE";
         break;
      case GEOTIFF_SSHORT:            // signed short (16-bit) integer
         type_name = "SSHORT";
         break;
      case GEOTIFF_SLONG:             // signed long (32-bit) integer
         type_name = "SLONG";
         break;
      case GEOTIFF_SRATIONAL:         // num/denom pair of signed longs
         type_name = "SRATIONAL";
         break;
      case GEOTIFF_FLOAT:             // 4-byte float value
         type_name = "FLOAT";
         break;
      case GEOTIFF_DOUBLE:            // 8-byte double value
         type_name = "DOUBLE";
         break;
      default:
         type_name.Format( "UNKNOWN: %u", type );
         break;
   }

   return type_name;
}

// ********************************************************************************************
// ********************************************************************************************

CString CGeoTiff::get_tag_value_name( const CTiffTag &tag )
{
   // returns the name of the tag's value if it has a name
   // or it's formatted numeric values otherwise

   unsigned short i, imax;
   const int STR_LEN = 100;
   char string[STR_LEN];
   CString value_name;

   //TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);

   switch( tag.m_tag_id )
   {
      case GEOTIFF_SUBFILE_TYPE_TAG:         // SubfileType
        if (tag.m_short_values == NULL)  // sanity check
           return value_name;
         switch( tag.m_short_values[0] )
         {
            case 1:
               value_name = "Full-Resolution Image";
               break;
            case 2:
               value_name = "Reduced-Resolution Image";
               break;
            case 3:
               value_name = "Single Page of Multi-Page Document";
               break;
            default:
            if (tag.m_short_values != NULL)
               value_name.Format( "Unknown: %u", tag.m_short_values[0] );
               break;
         }
         break;

      case GEOTIFF_COMPRESSION_TAG:           // Compression
        if (tag.m_short_values == NULL)  // sanity check
           return value_name;
         switch( tag.m_short_values[0] )
         {
            case 1:
               value_name = "Uncompressed";
               break;
            case 2:
               value_name = "CCITT 1D";
               break;
            case 3:
               value_name = "Group 3 Fax";
               break;
            case 4:
               value_name = "Group 4 Fax";
               break;
            case 5:
               value_name = "LZW";
               break;
            case 6:
            case 7:
               value_name = "JPEG";
               break;
            case 32773:
               value_name = "PackBits";
               break;
            default:
               value_name.Format( "Unknown: %u", tag.m_short_values[0] );
               break;
         }
         break;

      case GEOTIFF_PHOTOMETRIC_INTERPRETATION_TAG:  // PhotometricInterpretation
        if (tag.m_short_values == NULL)  // sanity check
           return value_name;
         switch( tag.m_short_values[0] )
         {
            case 0:
               value_name = "WhiteIsZero";
               break;
            case 1:
               value_name = "BlackIsZero";
               break;
            case 2:
               value_name = "RGB";
               break;
            case 3:
               value_name = "RGB Palette";
               break;
            case 4:
               value_name = "Transparency Mask";
               break;
            case 5:
               value_name = "CMYK";
               break;
            case 6:
               value_name = "YCbCr";
               break;
            case 7:
               value_name = "CIELab";
               break;
            default:
               value_name.Format( "Unknown: %u", tag.m_short_values[0] );
               break;
         }
         break;

      case GEOTIFF_THRESHHOLDING_TAG:         // Threshholding
        if (tag.m_short_values == NULL)  // sanity check
           return value_name;
         switch( tag.m_short_values[0] )
         {
            case 1:
               value_name = "No Dithering or Halftoning Has Been Applied";
               break;
            case 2:
               value_name = "An Ordered Dither or Halftone Has Been Applied";
               break;
            case 3:
               value_name =
               "A Randomized Process Such As Error Diffusion Has Been Applied";
               break;
            default:
               value_name.Format( "Unknown: %u", tag.m_short_values[0] );
               break;
         }
         break;

      case GEOTIFF_PLANAR_CONFIGURATION_TAG:   // PlanarConfiguration
        if (tag.m_short_values == NULL)  // sanity check
           return value_name;
         switch( tag.m_short_values[0] )
         {
            case 1:
               value_name = "Chunky Format";
               break;
            case 2:
               value_name = "Planar Format";
               break;
            default:
               value_name.Format( "Unknown: %u", tag.m_short_values[0] );
               break;
         }
         break;

      case GEOTIFF_RESOLUTION_UNIT_TAG:       // ResolutionUnit
        if (tag.m_short_values == NULL)  // sanity check
           return value_name;
         switch( tag.m_short_values[0] )
         {
            case 1:
               value_name = "None";
               break;
            case 2:
               value_name = "Inch";
               break;
            case 3:
               value_name = "Centimeter";
               break;
            default:
               value_name.Format( "Unknown: %u", tag.m_short_values[0] );
               break;
         }
         break;

      default:                            // format data values
         switch( tag.m_type )
         {
            case GEOTIFF_BYTE:              // 8-bit unsigned integer
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%u  ", tag.m_byte_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
               break;
            case GEOTIFF_ASCII:             // null-terminated string
               value_name = "\"";
               value_name += tag.m_ascii_values;
               value_name += "\"";
               break;
            case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%u  ", tag.m_short_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
              break;
            case GEOTIFF_LONG:              // unsigned long (32-bit) integer
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%i  ", tag.m_long_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
               break;
            case GEOTIFF_RATIONAL:          // num/denom pair of unsigned longs
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%lf  ", tag.m_double_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
               break;
            case GEOTIFF_SBYTE:             // 8-bit signed integer
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%i  ", tag.m_sbyte_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
               break;
            case GEOTIFF_UNDEFINED_TYPE:         // 8-bit undefined data type
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%i  ", tag.m_undefined_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
               break;
            case GEOTIFF_SSHORT:            // signed short (16-bit) integer
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%i  ", tag.m_sshort_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
               break;
            case GEOTIFF_SLONG:             // signed long (32-bit) integer
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%i  ", tag.m_slong_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
               break;
            case GEOTIFF_SRATIONAL:         // num/denom pair of signed longs
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%lf  ", tag.m_double_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
               break;
            case GEOTIFF_FLOAT:             // 4-byte float value
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%f  ", tag.m_float_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
               break;
            case GEOTIFF_DOUBLE:            // 8-byte double value
               value_name = "";
               imax = (unsigned short) __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%lf  ", tag.m_double_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count )
               value_name += "...";
               break;
            default:
               value_name.Format( "Unknown: %u", tag.m_type );
               break;
         }
         break;
   }

   return value_name;
}

// ********************************************************************************************
// ********************************************************************************************

CString CGeoTiff::get_geokey_name( unsigned short geokey_id )
{
   // returns the name of the geokey with id geokey_id

   CString geokey_name;

   switch( geokey_id )
   {
      case GEOTIFF_GT_MODEL_TYPE_GEOKEY:
         geokey_name = "GTModelTypeGeoKey";
         break;
      case GEOTIFF_GT_RASTER_TYPE_GEOKEY:
         geokey_name = "GTRasterTypeGeoKey";
         break;
      case GEOTIFF_GT_CITATION_GEOKEY:
         geokey_name = "GTCitationGeoKey";
         break;
      case GEOTIFF_GEOGRAPHIC_TYPE_GEOKEY:
         geokey_name = "GeographicTypeGeoKey";
         break;
      case GEOTIFF_GEOG_CITATION_GEOKEY:
         geokey_name = "GeogCitationGeoKey";
         break;
      case GEOTIFF_GEOG_GEODETIC_DATUM_GEOKEY:
         geokey_name = "GeogGeodeticDatumGeoKey";
         break;
      case GEOTIFF_GEOG_PRIME_MERIDIAN_GEOKEY:
         geokey_name = "GeogPrimeMeridianGeoKey";
         break;
      case GEOTIFF_GEOG_LINEAR_UNITS_GEOKEY:
         geokey_name = "GeogLinearUnitsGeoKey";
         break;
      case GEOTIFF_GEOG_LINEAR_UNIT_SIZE_GEOKEY:
         geokey_name = "GeogLinearUnitSizeGeoKey";
         break;
      case GEOTIFF_GEOG_ANGULAR_UNITS_GEOKEY:
         geokey_name = "GeogAngularUnitsGeoKey";
         break;
      case GEOTIFF_GEOG_ANGULAR_UNIT_SIZE_GEOKEY:
         geokey_name = "GeogAngularUnitSizeGeoKey";
         break;
      case GEOTIFF_GEOG_ELLIPSOID_GEOKEY:
         geokey_name = "GeogEllipsoidGeoKey";
         break;
      case GEOTIFF_GEOG_SEMI_MAJOR_AXIS_GEOKEY:
         geokey_name = "GeogSemiMajorAxisGeoKey";
         break;
      case GEOTIFF_GEOG_SEMI_MINOR_AXIS_GEOKEY:
         geokey_name = "GeogSemiMinorAxisGeoKey";
         break;
      case GEOTIFF_GEOG_INV_FLATTENING_GEOKEY:
         geokey_name = "GeogInvFlatteningGeoKey";
         break;
      case GEOTIFF_GEOG_AZIMUTH_UNITS_GEOKEY:
         geokey_name = "GeogAzimuthInitsGeoKey";
         break;
      case GEOTIFF_GEOG_PRIME_MERIDIAN_LONG_GEOKEY:
         geokey_name = "GeogPrimeMeridianLongGeoKey";
         break;
      case GEOTIFF_PROJECTED_CS_TYPE_GEOKEY:
         geokey_name = "ProjectedCSTypeGeoKey";
         break;
      case GEOTIFF_PCS_CITATION_GEOKEY:
         geokey_name = "PCSCitationGeoKey";
         break;
      case GEOTIFF_PROJECTION_GEOKEY:
         geokey_name = "ProjectionGeoKey";
         break;
      case GEOTIFF_PROJ_COORD_TRANS_GEOKEY:
         geokey_name = "ProjCoordTransGeoKey";
         break;
      case GEOTIFF_PROJ_LINEAR_UNITS_GEOKEY:
         geokey_name = "ProjLinearUnitsGeoKey";
         break;
      case GEOTIFF_PROJ_LINEAR_UNIT_SIZE_GEOKEY:
         geokey_name = "ProjLinearUnitSizeGeoKey";
         break;
      case GEOTIFF_PROJ_STD_PARALLEL_1_GEOKEY:
         geokey_name = "ProjStdParallel1GeoKey";
         break;
      case GEOTIFF_PROJ_STD_PARALLEL_2_GEOKEY:
         geokey_name = "ProjStdParallel2GeoKey";
         break;
      case GEOTIFF_PROJ_NAT_ORIGIN_LONG_GEOKEY:
         geokey_name = "ProjNatOriginLongGeoKey";
         break;
      case GEOTIFF_PROJ_NAT_ORIGIN_LAT_GEOKEY:
         geokey_name = "ProjNatOriginLatGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_EASTING_GEOKEY:
         geokey_name = "ProjFalseEastingGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_NORTHING_GEOKEY:
         geokey_name = "ProjFalseNorthingGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_ORIGIN_LONG_GEOKEY:
         geokey_name = "ProjFalseOriginLongGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_ORIGIN_LAT_GEOKEY:
         geokey_name = "ProjFalseOriginLatGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_ORIGIN_EASTING_GEOKEY:
         geokey_name = "ProjFalseOriginEastingGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_ORIGIN_NORTHING_GEOKEY:
         geokey_name = "ProjFalseOriginNorthingGeoKey";
         break;
      case GEOTIFF_PROJ_CENTER_LONG_GEOKEY:
         geokey_name = "ProjCenterLongGeoKey";
         break;
      case GEOTIFF_PROJ_CENTER_LAT_GEOKEY:
         geokey_name = "ProjCenterLatGeoKey";
         break;
      case GEOTIFF_PROJ_CENTER_EASTING_GEOKEY:
         geokey_name = "ProjCenterEastingGeoKey";
         break;
      case GEOTIFF_PROJ_CENTER_NORTHING_GEOKEY:
         geokey_name = "ProjCenterNorthingGeoKey";
         break;
      case GEOTIFF_PROJ_SCALE_AT_NAT_ORIGIN_GEOKEY:
         geokey_name = "ProjScaleAtNatOriginGeoKey";
         break;
      case GEOTIFF_PROJ_SCALE_AT_CENTER_GEOKEY:
         geokey_name = "ProjScaleAtCenterGeoKey";
         break;
      case GEOTIFF_PROJ_AZIMUTH_ANGLE_GEOKEY:
         geokey_name = "ProjAzimuthAngleGeoKey";
         break;
      case GEOTIFF_PROJ_STRAIGHT_VERT_POLE_LONG_GEOKEY:
         geokey_name = "ProjStraightVertPoleLongGeoKey";
         break;
      case GEOTIFF_VERTICAL_CS_TYPE_GEOKEY:
         geokey_name = "VertCSTypeGeoKey";
         break;
      case GEOTIFF_VERTICAL_CITATION_GEOKEY:
         geokey_name = "VertCitationGeoKey";
         break;
      case GEOTIFF_VERTICAL_DATUM_GEOKEY:
         geokey_name = "VerticalDatumGeoKey";
         break;
      case GEOTIFF_VERTICAL_UNITS_GEOKEY:
         geokey_name = "VerticalUnitsGeoKey";
         break;
      case GEOTIFF_UNDEFINED:
         geokey_name = "Undefined (0)";
         break;
      case GEOTIFF_USER_DEFINED:
         geokey_name = "User-Defined (32767)";
         break;
      default:
         geokey_name.Format( "Unknown: %u", geokey_id );
         break;
   }

   return geokey_name;
}

// ********************************************************************************************
// ********************************************************************************************

CString CGeoTiff::get_geokey_value_name( const CGeoKey &geokey )
{
   // returns the name of the geokey's value if it has a name
   // or it's formatted numeric values otherwise

   int index;
   unsigned short i, imax;
   CString value_name;
   const int STR_LEN = 100;
   char string[STR_LEN];

   //TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);

   switch( geokey.m_geokey_id )
   {
      case GEOTIFF_GT_MODEL_TYPE_GEOKEY:        // GTModelTypeGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         switch( geokey.m_short_values[0] )
         {
            case GEOTIFF_MODEL_TYPE_PROJECTED:
               value_name = "ModelTypeProjected";
               break;
            case GEOTIFF_MODEL_TYPE_GEOGRAPHIC:
               value_name = "ModelTypeGeographic";
               break;
            case GEOTIFF_MODEL_TYPE_GEOCENTRIC:
               value_name = "ModelTypeGeocentric";
               break;
            case GEOTIFF_UNDEFINED:
               value_name = "Undefined (0)";
               break;
            case GEOTIFF_USER_DEFINED:
               value_name = "User-Defined (32767)";
               break;
            default:
               value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
               break;
         }
         break;

      case GEOTIFF_GT_RASTER_TYPE_GEOKEY:          // GTRasterTypeGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         switch( geokey.m_short_values[0] )
         {
            case GEOTIFF_RASTER_PIXEL_IS_AREA:
               value_name = "RasterPixelIsArea";
               break;
            case GEOTIFF_RASTER_PIXEL_IS_POINT:
               value_name = "RasterPixelIsPoint";
               break;
            case GEOTIFF_UNDEFINED:
               value_name = "Undefined (0)";
               break;
            case GEOTIFF_USER_DEFINED:
               value_name = "User-Defined (32767)";
               break;
            default:
               value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
               break;
         }
         break;

      case GEOTIFF_GEOGRAPHIC_TYPE_GEOKEY:    // GeographicTypeGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
        index = -1;
         for( i = 0; i < geotiff_num_geographic_type_names; i++ )
         {
            if( geokey.m_short_values[0] ==
                geotiff_geographic_type_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_geographic_type_names[index].name;
         break;

      case GEOTIFF_GEOG_GEODETIC_DATUM_GEOKEY:    // GeogGeodeticDatumGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_datum_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_datum_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_datum_names[index].name;
         break;

      case GEOTIFF_GEOG_PRIME_MERIDIAN_GEOKEY:   // GeogPrimeMeridianGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_prime_meridian_names; i++ )
         {
            if( geokey.m_short_values[0] ==
                geotiff_prime_meridian_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_prime_meridian_names[index].name;
         break;

      case GEOTIFF_GEOG_LINEAR_UNITS_GEOKEY:    // GeogLinearUnitsGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_linear_units_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_linear_units_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_linear_units_names[index].name;
         break;

      case GEOTIFF_GEOG_ANGULAR_UNITS_GEOKEY:   // GeogAngularUnitsGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_angular_units_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_angular_units_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_angular_units_names[index].name;
         break;

      case GEOTIFF_GEOG_ELLIPSOID_GEOKEY:    // GeogEllipsoidGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_ellipse_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_ellipse_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_ellipse_names[index].name;
         break;

      case GEOTIFF_GEOG_AZIMUTH_UNITS_GEOKEY:    // GeogAzimuthUnitsGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_angular_units_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_angular_units_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_angular_units_names[index].name;
         break;

      case GEOTIFF_PROJECTED_CS_TYPE_GEOKEY:      // ProjectedCSTypeGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_pcs_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_pcs_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_pcs_names[index].name;
         break;

      case GEOTIFF_PROJECTION_GEOKEY:       // ProjectionGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_proj_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_proj_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_proj_names[index].name;
         break;

      case GEOTIFF_PROJ_COORD_TRANS_GEOKEY:     // ProjCoordTransGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_ct_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_ct_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_ct_names[index].name;
         break;

      case GEOTIFF_PROJ_LINEAR_UNITS_GEOKEY:    // ProjLinearUnitsGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_linear_units_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_linear_units_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_linear_units_names[index].name;
         break;

      case GEOTIFF_VERTICAL_CS_TYPE_GEOKEY:     // VerticalCSTypeGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_vert_cs_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_vert_cs_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_vert_cs_names[index].name;
         break;

      case GEOTIFF_VERTICAL_UNITS_GEOKEY:      // VerticalUnitsGeoKey
        if (geokey.m_short_values == NULL)  // sanity check
           return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_linear_units_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_linear_units_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name.Format( "Unknown: %u", geokey.m_short_values[0] );
         else
            value_name = geotiff_linear_units_names[index].name;
         break;

      default:                              // format data values
         switch( geokey.m_type )
         {
            case GEOTIFF_ASCII:             // null-terminated string
               value_name = "\"";
               value_name += geokey.m_ascii_values;
               value_name += "\"";
               break;
            case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
               value_name = "";
               imax = (unsigned short) __min( 100, geokey.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%u  ", geokey.m_short_values[i] );
                  value_name += string;
               }
               if( imax < geokey.m_count )
               value_name += "...";
              break;
            case GEOTIFF_DOUBLE:            // 8-byte double value
               value_name = "";
               imax = (unsigned short) __min( 100, geokey.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STR_LEN, "%lf  ", geokey.m_double_values[i] );
                  value_name += string;
               }
               if( imax < geokey.m_count )
               value_name += "...";
               break;
            default:
               value_name.Format( "Unknown: %u", geokey.m_type );
               break;
         }
         break;
   }

   return value_name;
}

// ********************************************************************************************
// ********************************************************************************************

CColorQuantizer::CColorQuantizer( )
{
   // constructor

   m_histogram = NULL;
   m_histogram_indices = NULL;
   m_red = NULL;
   m_green = NULL;
   m_blue = NULL;

   // clear to initial state
   clear( );
}

// ********************************************************************************************
// ********************************************************************************************

CColorQuantizer::~CColorQuantizer( )
{
   // destructor

   // clear to initial state
   clear( );
}

// ********************************************************************************************
// ********************************************************************************************

void CColorQuantizer::clear( )
{
   // clears to initial state

   m_defined = FALSE;
   m_method = 0;

   if( m_histogram != NULL )
   {
      MEM_free( m_histogram );
      m_histogram = NULL;
   }

   if( m_histogram_indices != NULL )
   {
      MEM_free( m_histogram_indices );
      m_histogram_indices = NULL;
   }

   if( m_red != NULL )
   {
      MEM_free( m_red );
      m_red = NULL;
   }

   if( m_green != NULL )
   {
      MEM_free( m_green );
      m_green = NULL;
   }

   if( m_blue != NULL )
   {
      MEM_free( m_blue );
      m_blue = NULL;
   }
}

// ********************************************************************************************
// ********************************************************************************************

int CColorQuantizer::set_colors(int num_colors, BYTE *red, BYTE *green, BYTE *blue, CString & error_msg)
{
   int i;

   clear();

   m_red = (unsigned char*)MEM_malloc( num_colors );
   m_green = (unsigned char*)MEM_malloc( num_colors );
   m_blue = (unsigned char*)MEM_malloc( num_colors );
   if ( m_red == NULL || m_green == NULL || m_blue == NULL )
   {
      ASSERT(0);
      error_msg = "Error allocating memory for storing colors.";
      clear();
      return FAILURE;
   }

   m_num_colors = num_colors;
   m_defined = TRUE;

   for ( i = 0; i < num_colors; i++ )
   {
      m_red[i] = red[i];
      m_green[i] = green[i];
      m_blue[i] = blue[i];
   }

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int CColorQuantizer::select_colors(
                                    CImageMap& image_map,
                                    int num_colors,
                                    unsigned char *red, unsigned char *green,
                                    unsigned char *blue,
                                    CString &error_message, IImageLibCallback *callback )
{
   // this function selects a color quantization method and color palette
   // for the list of tiff files in the file_cstring_array argument
   // num_colors argument specifies number of colors to be used
   // red, green, and blue arrays are filled with palette colors
   // returns SUCCESS or FAILURE

   int i, iunique, num_unique_colors, num_grayscale;
   BOOL all_grayscale, all_palette, all_grayscale_or_palette;
   double value, delta;
   CImageMap::GeoTiffMapIter gtfi;
   CString image_type_description;
   unsigned short photometric_interpretation, bits_per_sample;
   unsigned char val;
   unsigned char unique_colors_red[256];
   unsigned char unique_colors_green[256];
   unsigned char unique_colors_blue[256];
   unsigned char file_color_map_red[256];
   unsigned char file_color_map_green[256];
   unsigned char file_color_map_blue[256];
   unsigned int *file_histogram = NULL;

   error_message = "";

   // clear to initial state
   clear( );

   // check for empty file list
   if ( image_map.m_gtfmGeoTiffMap.empty() )
   {
      error_message.Format( "File list is empty." );
      goto FAIL;
   }

   // number of colors can be from 2 to 256
   if( num_colors < 2 || num_colors > 256 )
   {
      error_message.Format( "Number of colors is not in range 2 to 256." );
      goto FAIL;
   }

   // loop through the tiff files and determine method
   all_grayscale = TRUE;
   all_palette = TRUE;
   all_grayscale_or_palette = TRUE;
   num_unique_colors = 0;

   for ( gtfi = image_map.m_gtfmGeoTiffMap.begin();
      gtfi != image_map.m_gtfmGeoTiffMap.end(); gtfi++ )
   {
      // At this point, the geotiff is already loaded
      CGeoTiff& geotiff = *gtfi->second;
      if ( ! geotiff.GetGeoTiffReferenced() )
         continue;      // Inactive entry

      geotiff.get_image_format( photometric_interpretation, bits_per_sample );

      switch( photometric_interpretation )
      {
         case GEOTIFF_WHITE_IS_ZERO:
         case GEOTIFF_BLACK_IS_ZERO:
            switch( bits_per_sample )
            {
               case 1:                       // not yet implemented
                  error_message.Format( "Unsupported image format in file %s",
                                        (LPCSTR)gtfi->first );
                  goto FAIL;
               case 4:                       // not yet implemented
                  error_message.Format( "Unsupported image format in file %s",
                                        (LPCSTR)gtfi->first );
                  goto FAIL;
               case 8:
                  all_palette = FALSE;       // 8-bit grayscale
                  break;
               case 16:
                  all_palette = FALSE;       // 16-bit grayscale
                  break;
               default:                      // all others not supported
                  error_message.Format( "Unsupported image format in file %s",
                                        (LPCSTR)gtfi->first );
                  goto FAIL;
            }
            break;
         case GEOTIFF_RGB:                   // 24-bit rgb
      case GEOTIFF_YCBCR:
          all_grayscale = FALSE;
            all_palette = FALSE;
            all_grayscale_or_palette = FALSE;
            break;
         case GEOTIFF_RGB_PALETTE:           // 8-bit palette
            all_grayscale = FALSE;
            // get the file's color map
            geotiff.get_color_map( file_color_map_red, file_color_map_green,
                                   file_color_map_blue );
            // scan file color map for unique colors
            for( i = 0; i < 256; i++ )
            {
               // loop through unique colors to see if it is a duplicate
               for( iunique = 0; iunique < num_unique_colors; iunique++ )
               {
                  if( file_color_map_red[i] == unique_colors_red[iunique] &&
                      file_color_map_green[i] == unique_colors_green[iunique] &&
                      file_color_map_blue[i] == unique_colors_blue[iunique] )
                      goto DUPLICATE;
               }
               // the color is unique, so see if we have room for it
               if( num_unique_colors == num_colors )
               {
                  // too many unique colors to
                  all_palette = FALSE;
                  all_grayscale_or_palette = FALSE;
                  goto DETERMINE_METHOD;
               }
               // add new unique color
               unique_colors_red[num_unique_colors] = file_color_map_red[i];
               unique_colors_green[num_unique_colors] = file_color_map_green[i];
               unique_colors_blue[num_unique_colors] = file_color_map_blue[i];
               num_unique_colors++;
               if( num_colors - num_unique_colors < 64 )
                  all_grayscale_or_palette = FALSE;
               continue;
DUPLICATE:     ;
            }
            break;
         default:            // all others not supported at present
            error_message.Format( "Unsupported image format in file %s",
                                  (LPCSTR)gtfi->first );
            goto FAIL;
      }
   }

DETERMINE_METHOD:
   // determine method

   // allocate memory for storing colors internally
   m_red = (unsigned char*)MEM_malloc( num_colors );
   m_green = (unsigned char*)MEM_malloc( num_colors );
   m_blue = (unsigned char*)MEM_malloc( num_colors );
   if( m_red == NULL || m_green == NULL || m_blue == NULL )
   {
      ASSERT(0);
      error_message = "Error allocating memory for storing colors.";
      goto FAIL;
   }

   // check for all grayscale images
   if( all_grayscale )
   {
      m_method = COLOR_QUANTIZE_GRAYSCALE;
      // create grayscale palette
      for( i = 0; i < num_colors; i++ )
      {
         value = (double)(i) / (num_colors-1) * 255.0 + 0.5;
         red[i] = green[i] = blue[i] = (unsigned char) floor( value );
         m_red[i] = m_green[i] = m_blue[i] = red[i];
      }
      m_num_colors = num_colors;
      m_defined = TRUE;
      return SUCCESS;
   }

   // check for all palette images
   if( all_palette )
   {
      m_method = COLOR_QUANTIZE_PALETTE;
      // create palette from unique colors
      for( i = 0; i < num_unique_colors; i++ )
      {
         red[i] = unique_colors_red[i];
         green[i] = unique_colors_green[i];
         blue[i] = unique_colors_blue[i];
         m_red[i] = red[i];
         m_green[i] = green[i];
         m_blue[i] = blue[i];
      }
      // set any excess colors to last unique color
      for( i = num_unique_colors; i < num_colors; i++ )
      {
         red[i] = red[num_unique_colors-1];
         green[i] = green[num_unique_colors-1];
         blue[i] = blue[num_unique_colors-1];
         m_red[i] = red[num_unique_colors-1];
         m_green[i] = green[num_unique_colors-1];
         m_blue[i] = blue[num_unique_colors-1];
      }
      m_num_colors = num_colors;
      m_defined = TRUE;
      return SUCCESS;
   }

   // check for all grayscale or palette images
   if( all_grayscale_or_palette )
   {
      m_method = COLOR_QUANTIZE_PALETTE;
      // create palette from unique colors
      for( i = 0; i < num_unique_colors; i++ )
      {
         red[i] = unique_colors_red[i];
         green[i] = unique_colors_green[i];
         blue[i] = unique_colors_blue[i];
         m_red[i] = red[i];
         m_green[i] = green[i];
         m_blue[i] = blue[i];
      }
      // use remainder of palette for compressed grayscale
      num_grayscale = num_colors - num_unique_colors;
      delta = 255.0 / (num_grayscale-1);
      value = 0.0;
      val = 0;
      for( i = num_unique_colors; i < num_colors; i++ )
      {
         red[i] = val;
         green[i] = val;
         blue[i] = val;
         m_red[i] = val;
         m_green[i] = val;
         m_blue[i] = val;
         value += delta;
         val = (unsigned char)(value + 0.5);
      }
      m_num_colors = num_colors;
      m_defined = TRUE;
      return SUCCESS;
   }

   // if none of the above, we use the histogram method
   m_method = COLOR_QUANTIZE_HISTOGRAM;

   // allocate memory for histograms and histogram indices
   if ( m_histogram == NULL )
      m_histogram = (unsigned int*) MEM_malloc( 32768 * sizeof(*m_histogram) );
   file_histogram = (unsigned int*)MEM_malloc( 32768 * sizeof(*file_histogram) );
   if ( m_histogram_indices == NULL )
      m_histogram_indices = (unsigned char*)MEM_malloc( 32768 * sizeof(*m_histogram_indices) );
   if( m_histogram == NULL || file_histogram == NULL || m_histogram_indices == NULL )
   {
      ASSERT(0);
      error_message = "Error allocating memory for histogram.";
      goto FAIL;
   }

   // initialize histogram to zero
   ZeroMemory( m_histogram, 32768 * sizeof(*m_histogram) );
   ZeroMemory( m_histogram_indices, 32768 * sizeof(*m_histogram_indices) );

   // form cumulative histogram for all files
   for ( gtfi = image_map.m_gtfmGeoTiffMap.begin();
      gtfi != image_map.m_gtfmGeoTiffMap.end(); gtfi++ )
   {
      // At this point, the geotiff is already loaded
      CGeoTiff& geotiff = *gtfi->second;
      if ( ! geotiff.GetGeoTiffReferenced() )
         continue;      // Not in use

      if( geotiff.get_histogram( file_histogram, callback ) != SUCCESS )
      {
         error_message.Format( "Error getting histogram from  file %s",
                               (LPCSTR)gtfi->first );
         goto FAIL;
      }

      // add file histogram to cumulative histogram
      for( i = 0; i < 32768; i++ )
         m_histogram[i] += file_histogram[i];
   }

   // use median cut method to select palette colors
   if( median_cut( m_histogram, num_colors, red, green, blue,
       m_histogram_indices ) != SUCCESS )
   {
      error_message.Format( "Error in function median_cut" );
      goto FAIL;
   }

   // copy palette colors to internal storage
   for( i = 0; i < num_colors; i++ )
   {
      m_red[i] = red[i];
      m_green[i] = green[i];
      m_blue[i] = blue[i];
   }

   // deallocate memory for file histogram
   MEM_free( file_histogram );
   file_histogram = NULL;

   m_num_colors = num_colors;
   m_defined = TRUE;
   return SUCCESS;

FAIL:
   if( file_histogram != NULL )
   {
      MEM_free( file_histogram );
      file_histogram = NULL;
   }

   clear( );
   return FAILURE;
}


// ********************************************************************************************
// ********************************************************************************************

CTiepointTransform::CTiepointTransform( )
{
   m_defined = FALSE;
   m_num_tiepoints = 0;
   m_num_rows = 0;
   m_aprox0 = 1.e-12;
   m_tiepoint_x = NULL;
   m_tiepoint_y = NULL;
   m_tiepoint_latitude = NULL;
   m_tiepoint_longitude = NULL;
   m_x_coef = NULL;
   m_y_coef = NULL;
   m_latitude_coef = NULL;
   m_longitude_coef = NULL;

   clear( );
}

// ********************************************************************************************
// ********************************************************************************************

CTiepointTransform::~CTiepointTransform( )
{
   clear( );
}

// ********************************************************************************************
// ********************************************************************************************

void CTiepointTransform::clear( )
{
   // this function clears the object
   m_defined = FALSE;
   m_num_tiepoints = 0;
   m_num_rows = 0;

   // deallocate memory
   if( m_tiepoint_x != NULL )
   {
      MEM_free( m_tiepoint_x );
      m_tiepoint_x = NULL;
   }

   if( m_tiepoint_y != NULL )
   {
      MEM_free( m_tiepoint_y );
      m_tiepoint_y = NULL;
   }

   if( m_tiepoint_latitude != NULL )
   {
      MEM_free( m_tiepoint_latitude );
      m_tiepoint_latitude = NULL;
   }

   if( m_tiepoint_longitude != NULL )
   {
      MEM_free( m_tiepoint_longitude );
      m_tiepoint_longitude = NULL;
   }

   if( m_x_coef != NULL )
   {
      MEM_free( m_x_coef );
      m_x_coef = NULL;
   }

   if( m_y_coef != NULL )
   {
      MEM_free( m_y_coef );
      m_y_coef = NULL;
   }

   if( m_latitude_coef != NULL )
   {
      MEM_free( m_latitude_coef );
      m_latitude_coef = NULL;
   }

   if( m_longitude_coef != NULL )
   {
      MEM_free( m_longitude_coef );
      m_longitude_coef = NULL;
   }
}

// ********************************************************************************************
// ********************************************************************************************

int CTiepointTransform::define_tiepoints( int num_tiepoints, double *x,
   double *y, double *latitude, double *longitude )
{
   // this function is used to define the tiepoints
   int i, j, index;
   double dx, dy, r_sqrd;
   double *tmp_matrix = NULL;
   double *fwd_matrix = NULL;
   double *inv_matrix = NULL;

   // first clear the object
   clear( );

   // must be at least 3 tiepoints
   if( num_tiepoints < 3 )
      goto FAIL;

   m_num_tiepoints = num_tiepoints;
   m_num_rows = m_num_tiepoints + 3;

   // allocate memory for tiepoints and transformation matrices
   m_tiepoint_x = (double*)MEM_malloc( m_num_tiepoints * sizeof(double) );
   if( m_tiepoint_x == NULL )
      goto FAIL;
   m_tiepoint_y = (double*)MEM_malloc( m_num_tiepoints * sizeof(double) );
   if( m_tiepoint_y == NULL )
      goto FAIL;
   m_tiepoint_latitude =
      (double*)MEM_malloc( m_num_tiepoints * sizeof(double) );
   if( m_tiepoint_latitude == NULL )
      goto FAIL;
   m_tiepoint_longitude =
      (double*)MEM_malloc( m_num_tiepoints * sizeof(double) );
   if( m_tiepoint_longitude == NULL )
      goto FAIL;

   m_x_coef = (double*)MEM_malloc( m_num_rows * sizeof(double) );
   if( m_x_coef == NULL )
      goto FAIL;
   m_y_coef = (double*)MEM_malloc( m_num_rows * sizeof(double) );
   if( m_y_coef == NULL )
      goto FAIL;
   m_latitude_coef = (double*)MEM_malloc( m_num_rows * sizeof(double) );
   if( m_latitude_coef == NULL )
      goto FAIL;
   m_longitude_coef = (double*)MEM_malloc( m_num_rows * sizeof(double) );
   if( m_longitude_coef == NULL )
      goto FAIL;

   // copy tiepoints
   for( i = 0; i < m_num_tiepoints; i++ )
   {
      m_tiepoint_x[i] = x[i];
      m_tiepoint_y[i] = y[i];
      m_tiepoint_latitude[i] = latitude[i];
      m_tiepoint_longitude[i] = longitude[i];
   }

   // allocate temporary memory for calculating matrices
   fwd_matrix =
      (double*)MEM_malloc( m_num_rows * m_num_rows * sizeof(double) );
   if( fwd_matrix == NULL )
      goto FAIL;
   inv_matrix =
      (double*)MEM_malloc( m_num_rows * m_num_rows * sizeof(double) );
   if( inv_matrix == NULL )
      goto FAIL;
   tmp_matrix =
      (double*)MEM_malloc( m_num_rows * m_num_rows * sizeof(double) );
   if( tmp_matrix == NULL )
      goto FAIL;

   // calculate inverse of inverse transformation matrix
   index = 0;
   for( i = 0; i < m_num_tiepoints; i++ )
   {
      for( j = 0; j < m_num_tiepoints; j++ )
      {
         if( j == i )
         {
            tmp_matrix[index] = 0.0;
         }
         else
         {
            dx = m_tiepoint_x[j] - m_tiepoint_x[i];
            dy = m_tiepoint_y[j] - m_tiepoint_y[i];
            r_sqrd = dx*dx + dy*dy;
            // check for coincident points
            if( r_sqrd < m_aprox0 )
               goto FAIL;
            tmp_matrix[index] = r_sqrd * log( r_sqrd );
         }
         index++;
      }

      tmp_matrix[index] = 1.0;
      index++;
      tmp_matrix[index] = m_tiepoint_x[i];
      index++;
      tmp_matrix[index] = m_tiepoint_y[i];
      index++;
   }

   i = m_num_tiepoints;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = 1.0;
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   i = m_num_tiepoints + 1;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_x[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   i = m_num_tiepoints + 2;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_y[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   // now invert the temporary matrix to obtain the inverse transform matrix
   // which allows calculation of latitude and longitude from x and y
   if( invert_matrix( tmp_matrix, m_num_rows, inv_matrix ) != SUCCESS )
      goto FAIL;

   // now calculate the coefficients for latitude and longitude
   index = 0;
   for( i = 0; i < m_num_rows; i++ )
   {
      m_latitude_coef[i] = 0.0;
      m_longitude_coef[i] = 0.0;

      for( j = 0; j < m_num_tiepoints; j++ )
      {
         m_latitude_coef[i] += inv_matrix[index] * m_tiepoint_latitude[j];
         m_longitude_coef[i] += inv_matrix[index] * m_tiepoint_longitude[j];
         index++;
      }
      index += 3;
   }


   // calculate inverse of forward transformation matrix
   index = 0;
   for( i = 0; i < m_num_tiepoints; i++ )
   {
      for( j = 0; j < m_num_tiepoints; j++ )
      {
         if( j == i )
         {
            tmp_matrix[index] = 0.0;
         }
         else
         {
            dx = m_tiepoint_longitude[j] - m_tiepoint_longitude[i];
            dy = m_tiepoint_latitude[j] - m_tiepoint_latitude[i];
            r_sqrd = dx*dx + dy*dy;
            // check for coincident points
            if( r_sqrd < m_aprox0 )
               goto FAIL;
            tmp_matrix[index] = r_sqrd * log( r_sqrd );
         }
         index++;
      }

      tmp_matrix[index] = 1.0;
      index++;
      tmp_matrix[index] = m_tiepoint_longitude[i];
      index++;
      tmp_matrix[index] = m_tiepoint_latitude[i];
      index++;
   }

   i = m_num_tiepoints;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = 1.0;
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   i = m_num_tiepoints + 1;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_longitude[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   i = m_num_tiepoints + 2;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_latitude[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   // now invert the temporary matrix to obtain the inverse transform matrix
   // which allows calculation of latitude and longitude from x and y
   if( invert_matrix( tmp_matrix, m_num_rows, fwd_matrix ) != SUCCESS )
      goto FAIL;

   // now calculate the coefficients for x and y
   index = 0;
   for( i = 0; i < m_num_rows; i++ )
   {
      m_x_coef[i] = 0.0;
      m_y_coef[i] = 0.0;

      for( j = 0; j < m_num_tiepoints; j++ )
      {
         m_x_coef[i] += fwd_matrix[index] * m_tiepoint_x[j];
         m_y_coef[i] += fwd_matrix[index] * m_tiepoint_y[j];
         index++;
      }
      index += 3;
   }

   // deallocate temporary matrices
   MEM_free( tmp_matrix );
   MEM_free( fwd_matrix );
   MEM_free( inv_matrix );

   m_defined = TRUE;
   return SUCCESS;

FAIL:
   if( tmp_matrix != NULL )
      MEM_free( tmp_matrix );
   if( fwd_matrix != NULL )
      MEM_free( fwd_matrix );
   if( inv_matrix != NULL )
      MEM_free( inv_matrix );
   clear( );
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CTiepointTransform::invert_matrix( double *matrix, int n,
                                       double *inverse_matrix )
{
   // this function inverts the input matrix, which is destroyed
   // returns SUCCESS or FAILURE

   int i, j, index;
   int *index_array = NULL;
   double *col = NULL;

   if( n <= 0 )
      goto FAIL;

   if( n == 1 )
   {
      if( fabs( matrix[0]) < m_aprox0 )
         goto FAIL;
      inverse_matrix[0] = 1.0 / matrix[0];
      goto SUCCEED;
   }

   // allocate memory for index
   index_array = (int*)MEM_malloc( n * sizeof(int) );
   if( index_array == NULL )
      goto FAIL;

   // perform LU decomposition, return if error
   if( decompose_lu( matrix, n, index_array ) != SUCCESS )
      goto FAIL;

   // allocate memory for one column
   col = (double*)MEM_malloc( n * sizeof(double) );
   if( col == NULL )
      goto FAIL;

   // calculate inverse column by column using back substitution
   for( j = 0; j < n; j++ )
   {
      for( i = 0; i < n; i++ )
         col[i] = 0.0;
      col[j] = 1.0;
      back_substitute_lu( matrix, n, index_array, col );
      for( i = 0; i < n; i++ )
      {
         index = i*n + j;
         inverse_matrix[index] = col[i];
      }
   }

SUCCEED:
   if( index_array != NULL )
   {
      MEM_free( index_array );
      index = NULL;
   }
   if( col != NULL )
   {
      MEM_free( col );
      col = NULL;
   }
   return SUCCESS;

FAIL:
   if( index_array != NULL )
   {
      MEM_free( index_array );
      index = NULL;
   }
   if( col != NULL )
   {
      MEM_free( col );
      col = NULL;
   }
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CTiepointTransform::decompose_lu( double *a, int n, int *indx )
{
   // performs LU decomposition on matrix in a
   // returns SUCCESS or FAILURE
   int i,imax,j,k;
   int index, index1, index2;
   double d, big,dum,sum,temp;
   double *scratch;

   // allocate scratch space
   scratch = (double*)MEM_malloc( n * sizeof(double) );
   if( scratch == NULL )
      goto FAIL;

   d=1.0;

   for( i = 0; i < n; i++ )   // find largest absolute value in each row of a
   {
      big = 0.0;
      for( j = 0; j < n; j++ )
      {
         index = n*i+j;
         if( (temp=fabs(a[index])) > big )
            big=temp;
      }
      if ( big == 0.0 )
         goto FAIL;
      scratch[i]=1.0/big; // scratch now contains largest abs value in each row
   }

   for( j = 0; j < n; j++ )
   {
      for( i = 0; i < j; i++ )
      {
         index = n*i+j;
         sum=a[index];
         for ( k = 0; k < i; k++ )
         {
            index1 = n*i+k;
            index2 = n*k+j;
            sum -= a[index1]*a[index2];
         }
         a[index]=sum;
      }

      big=0.0;
      imax = 0;

      for( i = j; i < n; i++ )
      {
         index = n*i+j;
         sum=a[index];
         for( k = 0; k < j; k++ )
         {
            index1 = n*i+k;
            index2 = n*k+j;
            sum -= a[index1]*a[index2];
         }
         a[index]=sum;
         if ( (dum=scratch[i]*fabs(sum)) >= big)
         {
            big=dum;
            imax=i;
         }
      }

      if( j != imax )
      {
         for( k = 0; k < n; k++)
         {
            index1 = n*imax+k;
            dum=a[index1];
            index2 = n*j+k;
            a[index1]=a[index2];
            a[index2]=dum;
         }
         d = -(d);
         scratch[imax]=scratch[j];
      }

      indx[j]=imax;
      index1 = n*j+j;
      if( a[index1] == 0.0 )
         a[index1] = m_aprox0;
      if (j != n - 1)
      {
         dum=1.0/(a[index1]);
         for( i= j + 1; i < n; i++ )
         {
            index = n*i+j;
            a[index] *= dum;
         }
      }
   }

   if( scratch != NULL )
   {
      MEM_free( scratch );
      scratch = NULL;
   }
   return SUCCESS;

FAIL:
   if( scratch != NULL )
   {
      MEM_free( scratch );
      scratch = NULL;
   }
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

void CTiepointTransform::back_substitute_lu( double *a, int n, int *indx,
                                             double *b )
{
   int i,ii=-1,ip,j;
   int index;
   double sum;

   for( i = 0; i < n; i++ )
   {
      ip=indx[i];
      sum=b[ip];
      b[ip]=b[i];
      if( ii >= 0 )
         for( j = ii; j <= i-1; j++ )
         {
            index = n*i+j;
            sum -= a[index]*b[j];
         }
      else
         if( sum )
            ii=i;
      b[i]=sum;
   }

   for( i = n - 1; i >= 0; i-- )
   {
      sum=b[i];
      for( j = i+1; j < n; j++ )
      {
         index = n*i+j;
         sum -= a[index]*b[j];
      }
      index = n*i+i;
      b[i]=sum/a[index];
   }
}

// ********************************************************************************************
// ********************************************************************************************

int CTiepointTransform::inv_transform( double x, double y,
                                       double &latitude, double &longitude )
{
   // performs inverse transform: returns longitude and latitude corresponding
   // to the arguments x and y
   // returns SUCCESS or FAILURE

   int i;
   double r_sqrd, dx, dy;

   // check for defined transformation
   if( !m_defined )
      goto FAIL;

   longitude = 0.0;
   latitude = 0.0;

   for( i = 0; i < m_num_tiepoints; i++ )
   {
      dx = x - m_tiepoint_x[i];
      dy = y - m_tiepoint_y[i];
      r_sqrd = dx*dx + dy*dy;
      if( r_sqrd < m_aprox0 )
      {
         latitude = m_tiepoint_latitude[i];
         longitude = m_tiepoint_longitude[i];
         goto SUCCEED;
      }
      latitude += r_sqrd * log( r_sqrd ) * m_latitude_coef[i];
      longitude += r_sqrd * log( r_sqrd ) * m_longitude_coef[i];
   }

   latitude += m_latitude_coef[i];
   longitude += m_longitude_coef[i];

   latitude += x * m_latitude_coef[i+1];
   longitude += x * m_longitude_coef[i+1];

   latitude += y * m_latitude_coef[i+2];
   longitude += y * m_longitude_coef[i+2];

SUCCEED:
   return SUCCESS;

FAIL:
   return FAILURE;
}

// ********************************************************************************************
// ********************************************************************************************

int CTiepointTransform::fwd_transform( double latitude, double longitude,
                                       double &x, double &y )
{
   // performs forward transform: returns x and y corresponding
   // to the arguments longitude and latitude
   // returns SUCCESS or FAILURE

   int i;
   double r_sqrd, d_latitude, d_longitude;

   // check for defined transformation
   if( !m_defined )
      goto FAIL;

   x = 0.0;
   y = 0.0;

   for( i = 0; i < m_num_tiepoints; i++ )
   {
      d_latitude = latitude - m_tiepoint_latitude[i];
      d_longitude = longitude - m_tiepoint_longitude[i];
      r_sqrd = d_latitude*d_latitude + d_longitude*d_longitude;
      if( r_sqrd < m_aprox0 )
      {
         x = m_tiepoint_x[i];
         y = m_tiepoint_y[i];
         goto SUCCEED;
      }
      x += r_sqrd * log( r_sqrd ) * m_x_coef[i];
      y += r_sqrd * log( r_sqrd ) * m_y_coef[i];
   }

   x += m_x_coef[i];
   y += m_y_coef[i];

   x += longitude * m_x_coef[i+1];
   y += longitude * m_y_coef[i+1];

   x += latitude * m_x_coef[i+2];
   y += latitude * m_y_coef[i+2];

SUCCEED:
   return SUCCESS;

FAIL:
   return FAILURE;
}



// ********************************************************************************************
// ********************************************************************************************

//
// SetExpandedLastView()
//
// Save size of last view used (expanded by 50%)
//
VOID CGeoTiff::SetExpandedLastView( DOUBLE dULLat, DOUBLE dULLon, DOUBLE dLRLat, DOUBLE dLRLon )
{
   // Correct for dateline wrap
   if ( dULLon > dLRLon )
      dLRLon += 360.0;

   m_dExpandedLastView[0] = dULLat;
   m_dExpandedLastView[1] = dULLon;
   m_dExpandedLastView[2] = dLRLat;
   m_dExpandedLastView[3] = dLRLon;
}



// ********************************************************************************************
// ********************************************************************************************

//
// GetExpandedLastView()
//
// Retrieve size of expanded last view and image
//
VOID CGeoTiff::GetExpandedBounds(
         DOUBLE& dViewULLat, DOUBLE& dViewULLon,
         DOUBLE& dViewLRLat, DOUBLE& dViewLRLon,
         DOUBLE& dImageULLat, DOUBLE& dImageULLon,
         DOUBLE& dImageLRLat, DOUBLE& dImageLRLon )
{
   static const DOUBLE
      dViewExpansion = 1.1,   // Go 110% beyond view
      dViewA = 1.0 + dViewExpansion,
      dViewB = -dViewExpansion,
      dImageExpansion = 0.5,  // Go 50% beyond image
      dImageA = 1.0 + dImageExpansion,
      dImageB = -dImageExpansion;

   // Get expanded view limits
   dViewULLat = ( dViewA * m_dExpandedLastView[0] ) + ( dViewB * m_dExpandedLastView[2] );
   dViewULLon = ( dViewA * m_dExpandedLastView[1] ) + ( dViewB * m_dExpandedLastView[3] );
   dViewLRLat = ( dViewA * m_dExpandedLastView[2] ) + ( dViewB * m_dExpandedLastView[0] );
   dViewLRLon = ( dViewA * m_dExpandedLastView[3] ) + ( dViewB * m_dExpandedLastView[1] );

   // Clip the latitudes (the longitude was already dewrapped)
   if ( dViewULLat > +90.0 )
      dViewULLat = +90.0;
   if ( dViewLRLat < -90.0 )
      dViewLRLat = -90.0;


   // Get expanded image limits
   DOUBLE dULLat, dULLon, dLRLat, dLRLon;     // Temp storage
   inv_transform( 0, 0, dULLat, dULLon );
   inv_transform( m_image_width - 1, m_image_length - 1, dLRLat, dLRLon );

   // Remove dateline wrap
   if ( dULLon > dLRLon )
      dLRLon += 360.0;

   dImageULLat = ( dImageA * dULLat ) + ( dImageB * dLRLat );
   dImageULLon = ( dImageA * dULLon ) + ( dImageB * dLRLon );
   dImageLRLat = ( dImageA * dLRLat ) + ( dImageB * dULLat );
   dImageLRLon = ( dImageA * dLRLon ) + ( dImageB * dLRLon );

   // Clip the latitudes
   if ( dImageULLat > +90.0 )
      dImageULLat = +90.0;
   if ( dImageLRLat < -90.0 )
      dImageLRLat = -90.0;
}



// ********************************************************************************************
// ********************************************************************************************

//
// gtfOpen() - open file
//
INT CGeoTiff::gtfOpen( LPCSTR pszFilename )
{
   if ( m_file_ptr != NULL )
   {
      ClearFile();
      FIL_close( m_file_ptr );
   }

   // Attempt to open file
   m_tiff_file_name = pszFilename;

//   m_file_ptr = FIL_open( m_tiff_file_name, "rbs" );

   // Check whether opened ok
   do
   {
      if ( 0 == FIL_open( &m_file_ptr, m_tiff_file_name, "rbS" )
         && m_file_ptr != NULL )
      {
#if defined READFILE_FILE_INPUT
         // Build a pseudo I/O buffer descriptor
         ZeroMemory( &m_iobMappedFileIOBufDesc, sizeof(m_iobMappedFileIOBufDesc) );
         m_iobMappedFileIOBufDesc._flag = SPECIAL_IOBUF_FLAG; // Flag for fread() substitutes

         m_hGeoTiffFile = CreateFile( m_tiff_file_name, GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL );

         if ( m_hGeoTiffFile != INVALID_HANDLE_VALUE )
         {
            m_iobMappedFileIOBufDesc._tmpfname = (PCHAR) &m_ovlpOverlapped;   // Pointer to OVERLAPPED
            m_iobMappedFileIOBufDesc._file = (INT) m_hGeoTiffFile;   // File handle

            m_ovlpOverlapped.Offset = m_ovlpOverlapped.OffsetHigh = 0;  // Beginning of file

            if ( m_ovlpOverlapped.hEvent == NULL )
               m_ovlpOverlapped.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );   // Event for input completion
            ASSERT( m_ovlpOverlapped.hEvent != NULL );

            m_iobMappedFileIOBufDesc._tmpfname = (PCHAR) &m_ovlpOverlapped;

            break;                     // Ready to go
         }

#elif defined LLFILE_FILE_INPUT     // Low-level file I/O test
         // Build a pseudo I/O buffer descriptor
         ZeroMemory( &m_iobMappedFileIOBufDesc, sizeof(m_iobMappedFileIOBufDesc) );
         m_iobMappedFileIOBufDesc._flag = SPECIAL_IOBUF_FLAG; // Flag for fread() substitutes

         m_hGeoTiffFile = _open( m_tiff_file_name, _O_BINARY | _O_SEQUENTIAL | _O_RDONLY );
         if ( m_hGeoTiffFile != -1 )
         {
            m_iobMappedFileIOBufDesc._file = m_hGeoTiffFile;   // File handle
            m_iobMappedFileIOBufDesc._ptr = m_iobMappedFileIOBufDesc._base;
            break;                     // Ready
         }

#elif defined MAPPEDFILE_FILE_INPUT
         // Build a pseudo I/O buffer descriptor
         ZeroMemory( &m_iobMappedFileIOBufDesc, sizeof(m_iobMappedFileIOBufDesc) );
         m_iobMappedFileIOBufDesc._flag = SPECIAL_IOBUF_FLAG; // Flag for fread() substitutes

         // If the file is either remote or a CDROM drive (slow), set to back up to
         // the system paging file
         do
         {
            if ( m_tiff_file_name.Find( _T(":\\") ) == 1 ) // Assume network if not "x:\" format
            {
               UINT uiType = GetDriveType( m_tiff_file_name.Left(3) );
               if ( uiType != DRIVE_REMOTE && uiType != DRIVE_CDROM )
                  break;      // Fast local drive, don't use system paging file
            }
            m_iobMappedFileIOBufDesc._cnt = dwSystemPageSize;
         } while ( FALSE );

         m_hGeoTiffFile = CreateFile( m_tiff_file_name, GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, /* FILE_ATTRIBUTE_NORMAL | */FILE_FLAG_SEQUENTIAL_SCAN, NULL );

         if ( m_hGeoTiffFile != INVALID_HANDLE_VALUE )
         {
            m_iobMappedFileIOBufDesc._bufsiz = GetFileSize( m_hGeoTiffFile, NULL );
            if ( (UINT) m_iobMappedFileIOBufDesc._bufsiz >= 0x4000000UL )
            {
               ClearFile();         // Don't try to map files over 1GB
               goto FileOpened;
            }

            // Create a file mapping object
            m_hFileMapping = CreateFileMapping( m_hGeoTiffFile, NULL, PAGE_WRITECOPY, 0,0, NULL );

            // If mapping object was created, map the file
            if ( m_hFileMapping != INVALID_HANDLE_VALUE )
            {
SetupFileMapping:
            m_iobMappedFileIOBufDesc._base =
               (PCHAR) MapViewOfFile( m_hFileMapping, FILE_MAP_COPY, 0,0, 0 );

            if  ( m_iobMappedFileIOBufDesc._base != NULL )
               goto FileOpened;        // Mapped ok

            if ( GetLastError() == ERROR_NOT_ENOUGH_MEMORY )
            {
               if ( m_pCallingImageMap != NULL )
               {
                  if ( m_pCallingImageMap->InactiveGeoTiffCleanup() )
                     goto SetupFileMapping;  // Retry if something is cleaned up
               }
               ClearFile();         // Don't map
               goto FileOpened;     // Use fread's if not enough mapping space
            }

            }  // CreateFileMapping() succeeded
         }  // CreateFile() succeeded

         // Some part of mapping operation failed
         ClearFile();

#else // Must be standard fread() I/O
         break;
#endif
      }  // FIL_fopen ok

      m_error = TRUE;
      m_cumulative_error_description = "Error opening file: ";
      m_cumulative_error_description += pszFilename;
      return FAILURE;

   } while ( FALSE ); // One-shot do

#ifdef MAPPEDFILE_FILE_INPUT
FileOpened:    // Input file opened
#endif

   gtfSeek( 0L );                                        // Mark at beginning
   return SUCCESS;

}  // End of gtfOpen()

//
// gtfSeek() - input file seek
//
// This routine provides for file seeking only when necessary.  Seeking seems
// to flush network file caches
//

INT CGeoTiff::gtfSeek( fpos_t qwFilePosition )
{
#ifdef MAPPEDFILE_FILE_INPUT
   INT64 iPositionChange = qwFilePosition  - (INT64) ( m_iobMappedFileIOBufDesc._ptr - m_iobMappedFileIOBufDesc._base );
   if ( iPositionChange != 0 )
   {
      // If no mapped file access, must do real seek
      if ( m_iobMappedFileIOBufDesc._base == NULL )
      {
         // If within 100 bytes, read rather than seek
         if ( iPositionChange > 0 && iPositionChange <= 100 )
         {
            BYTE bTmp[100];
            if ( fread( bTmp, (size_t) iPositionChange / sizeof(BYTE), 1, m_file_ptr ) != 1 )
               return FAILURE;
         }
         else
         {
            fpos_t p = qwFilePosition;
            if ( fsetpos( m_file_ptr, &p ) != 0 )
               return FAILURE;
         }
      }

      // Update the pseudo I/O buffer descriptor with the current position
      m_iobMappedFileIOBufDesc._ptr = m_iobMappedFileIOBufDesc._base + qwFilePosition;
   }

#elif defined LLFILE_FILE_INPUT
   INT iPositionChange = (INT) ( qwFilePosition  - ( m_iobMappedFileIOBufDesc._ptr - m_iobMappedFileIOBufDesc._base ) );
   if ( iPositionChange != 0 )
   {
      // If within 100 bytes, read rather than seek
      if ( iPositionChange > 0 && iPositionChange <= 100 )
      {
         BYTE bTmp[100];
         if ( _read( m_iobMappedFileIOBufDesc._file, bTmp, iPositionChange / sizeof(BYTE) )
            != iPositionChange / sizeof(BYTE) )
            return FAILURE;
      }
      else if ( _lseek( m_iobMappedFileIOBufDesc._file, (LONG) qwFilePosition, SEEK_SET ) == -1 )
         return FAILURE;

      // Update the pseudo I/O buffer descriptor with the current position
      m_iobMappedFileIOBufDesc._ptr = m_iobMappedFileIOBufDesc._base + qwFilePosition;
   }

#elif defined READFILE_FILE_INPUT
   m_ovlpOverlapped.Offset = qwFilePosition & 0xFFFFFFFF;
   m_ovlpOverlapped.OffsetHigh = qwFilePosition >> 32;

#else
   INT iPositionChange = (INT) ( qwFilePosition - m_qwFilePosition );
   if ( iPositionChange != 0 )
   {
      // If within 100 bytes, read rather than seek
      if ( iPositionChange > 0 && iPositionChange <= 100 )
      {
         BYTE bTmp[100];
         if ( fread( bTmp, iPositionChange / sizeof(BYTE), 1, m_file_ptr ) != 1 )
            return FAILURE;
      }
      else if ( _fseeki64( m_file_ptr, qwFilePosition, SEEK_SET ) != 0 )
         return FAILURE;

      m_qwFilePosition = qwFilePosition;
   }
#endif

   return SUCCESS;
}

//
// gtfRead() - input file read
//
INT CGeoTiff::gtfRead( DWORD dwFilePosition, PBYTE pbBuffer, DWORD dwByteCount )
{
   INT iResult = gtfSeek( dwFilePosition );
   if ( iResult == SUCCESS )
      iResult = gtfRead( pbBuffer, dwByteCount );
   return iResult;
}

#ifdef MAPPEDFILE_FILE_INPUT
#pragma auto_inline( off )

static VOID RewriteByte( PCHAR pchIn, PCHAR pchOut )
{
   *pchOut = *pchIn;             // Will make target page dirty
}

#endif

INT CGeoTiff::gtfRead( PBYTE pbBuffer, DWORD dwByteCount )
{
#ifdef MAPPEDFILE_FILE_INPUT
   // Memory copy if mapped file access
   if ( m_iobMappedFileIOBufDesc._base != NULL )
   {
      // Don't go beyond the file
      fpos_t qwPos = m_iobMappedFileIOBufDesc._ptr - m_iobMappedFileIOBufDesc._base;
      if ( qwPos > (UINT) m_iobMappedFileIOBufDesc._bufsiz )
         dwByteCount = 0;
      else if ( qwPos + dwByteCount > (UINT) m_iobMappedFileIOBufDesc._bufsiz )
         dwByteCount = (DWORD) ( (UINT) m_iobMappedFileIOBufDesc._bufsiz - qwPos );

      // Catch any network read error
      __try
      {
         memcpy( pbBuffer, m_iobMappedFileIOBufDesc._ptr, dwByteCount * sizeof(BYTE) );

         // If not local hard drive, mark dirty to force local paging
         if ( m_iobMappedFileIOBufDesc._cnt > 0 )
         {
            // Tickle each page in the range
            for ( PCHAR pch = m_iobMappedFileIOBufDesc._ptr;
               pch < m_iobMappedFileIOBufDesc._ptr + ( dwByteCount * sizeof(BYTE) );
               pch += m_iobMappedFileIOBufDesc._cnt )
            {
               RewriteByte( pch, pch );   // Rewrite one byte in each page
            }
         }
      }
      __except(
         ( GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
            || GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ) ?
         EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH )
      {
         return FAILURE;      // Assume nothing copied
      }
   }
   else  // No mapped file access
   {
      if ( fread( pbBuffer, dwByteCount, sizeof(BYTE), m_file_ptr ) != 1 )
         return FAILURE;
   }

   m_iobMappedFileIOBufDesc._ptr += dwByteCount * sizeof(BYTE);

#elif defined READFILE_FILE_INPUT || defined LLFILE_FILE_INPUT
   if ( jpeg_fread( pbBuffer, dwByteCount, &m_iobMappedFileIOBufDesc )
      != dwByteCount )
      return FAILURE;

#else    // No mapped file support
   if ( fread( pbBuffer, dwByteCount, sizeof(BYTE), m_file_ptr ) != 1 )
      return FAILURE;

   m_qwFilePosition += dwByteCount * sizeof(BYTE);
#endif

   return SUCCESS;
}


// ********************************************************************************************
// ********************************************************************************************

