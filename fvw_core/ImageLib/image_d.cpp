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

// image_d.cpp



#include "stdafx.h"
#include "image_d.h"
#include "common.h"

// ********************************************************************************************
// ********************************************************************************************

C_band::C_band()
{
	m_8bit = FALSE;
#if 0
   m_img8bit = NULL;
#endif
	m_img16bit = NULL;
	m_hist = NULL;
	m_image_width = 0;
	m_image_height = 0;
	m_bpp = 0;		
	m_abpp = 0;		
	m_label = "";
	m_pct_red = 0.0;
	m_pct_grn = 0.0;
	m_pct_blu = 0.0;	
}


// ********************************************************************************************
// ********************************************************************************************

C_band::~C_band()
{
#if 0
   delete m_img8bit;
#endif
	delete m_img16bit;
	delete m_hist;
}

// ********************************************************************************************
// ********************************************************************************************

C_group_item::C_group_item()
{
//	m_pStream = NULL; // Automatic
	m_width = 0;
	m_height = 0;
	m_ul_lat = 0.0;
	m_ul_lon = 0.0;
	m_lr_lat = 0.0;
	m_lr_lon = 0.0;
}

// ********************************************************************************************
// ********************************************************************************************

C_group_item::~C_group_item()
{
}

// ********************************************************************************************
// ********************************************************************************************

int C_group_item::xy2geo(int x, int y, double *lat, double *lon)
{
	int rslt;

	rslt = m_transform.inv_transform((double) x, (double) y, *lat, *lon);
	return rslt;
}

// ********************************************************************************************
// ********************************************************************************************

int C_group_item::geo2xy(double lat, double lon, int *x, int *y)
{
	int rslt;
	double dx, dy;

	rslt = m_transform.fwd_transform(lat, lon, dx, dy);
	*x = (int) (dx + 0.5);
	*y = (int) (dy + 0.5);
	return rslt;
}

// ********************************************************************************************
// ********************************************************************************************

C_image::C_image()
{
//	m_fp = NULL;					// file pointer to image file
	m_initialized = FALSE;
	m_color_type = 0;			// whether mono, rgb, etc.
	m_image_width = 0;			// width of image
	m_image_height = 0;			// height of image
	m_bpp = 0;					// storage bits per pixel
	m_abpp = 0;					// actual bits per pixel (e.g. 11 of 16 bits actually used)
	m_lut = NULL;				// look up table for paletted images stored in RGB format
	m_band = NULL;
	m_num_bands = 0;
	m_target_width = 0;
	m_target_height = 0;
   m_lut_cnt = 0;
	m_err_code = 0;				// numerical code for error that may have occurred in process
	m_is_tiled = FALSE;			// is the image tiled
	m_tile_size_x = 0;			// tile width
	m_tile_size_y = 0;			// tile height
	m_tile_cnt_x = 0;			// number of horizontal tiles
	m_tile_cnt_y = 0;			// number of vertical tiles
	m_has_transparency = FALSE;	// whether the image has transparent pixels
	m_trans_pixel_code[0] = 0; // transparent color, for paletted images first byte is use, else RGB
	m_trans_pixel_code[1] = 0; // transparent color, for paletted images first byte is use, else RGB
	m_trans_pixel_code[2] = 0; // transparent color, for paletted images first byte is use, else RGB
	m_trans_pix_code_len = 0;   // Transparent Output pixel code length
	m_histogram = NULL;	// size = 32768
	m_freq_red = NULL;  // size = 256
	m_freq_grn = NULL;  // size = 256
	m_freq_blu = NULL;  // size = 256
	m_tile_index = NULL;
	m_num_tile_index = 0;
	m_pixel_size = 0.0;
	m_ul_lat = 0.0;
	m_ul_lon = 0.0;
	m_ur_lat = 0.0;
	m_ur_lon = 0.0;
	m_lr_lat = 0.0;
	m_lr_lon = 0.0;
	m_ll_lat = 0.0;
	m_ll_lon = 0.0;

	m_buf_red = NULL;
	m_buf_grn = NULL;
	m_buf_blu = NULL;
	m_buf_start_row = -1;
	m_georeferenced = FALSE;
	m_globally_georeferenced = FALSE;

	m_old_width = 0;
	m_old_height = 0;
	m_old_ul_lat = 0.0;
	m_old_ul_lon = 0.0;
	m_old_lr_lat = 0.0;
	m_old_lr_lon = 0.0;
	m_old_startx = -999;
	m_old_starty = -999;
	m_old_factor = 0.0;

	m_image_type_supported = FALSE;
#ifdef _DEBUG // Debug members
	m_min_latitude = 0.0;
	m_max_latitude = 0.0;
	m_min_longitude = 0.0;
	m_max_longitude = 0.0;
	m_delta_latitude = 0.0;
	m_delta_longitude = 0.0;
#endif
//	m_gcp_num = 0;
	m_image_type = 0;
	m_geodata_present = FALSE;
	m_geodata_supported = FALSE;
	m_buf_end_row = -1;
	m_img_offset_x = 0;
	m_img_offset_y = 0;
	m_row_bytes = 0;
	m_is_color = FALSE;
	m_trans_pixel_code[0] = 0;
	m_trans_pixel_code[1] = 0;
	m_trans_pixel_code[2] = 0;
	m_paletted = FALSE;
	m_pal_img = NULL;
}


C_image::~C_image()
{
	try
	{
	//	if (m_fp != NULL)
	//		fclose(m_fp);
		if (m_histogram != NULL)
			free(m_histogram);	
		if (m_freq_red != NULL)
			free(m_freq_red);	
		if (m_freq_grn != NULL)
			free(m_freq_grn);	
		if (m_freq_blu != NULL)
			free(m_freq_blu);	
		if (m_lut != NULL)
			free(m_lut);

		if (m_band != NULL)
			delete [] m_band;

		if (m_buf_red)
		{
			free(m_buf_red);
			m_buf_red = NULL;
		}
		if (m_buf_grn)
		{
			free(m_buf_grn);
			m_buf_grn = NULL;
		}
		if (m_buf_blu)
		{
			free(m_buf_blu);
			m_buf_blu = NULL;
		}
      while ( !m_gcp_list.IsEmpty() )
         delete m_gcp_list.RemoveHead();
	}
	catch(...)
	{
		ASSERT(0);
	}

}


// ********************************************************************************************
// ********************************************************************************************

INT C_image::GetHistogram( PUINT puiHistogram, PUINT puiFRed, PUINT puiFGreen, PUINT puiFBlue )
{
   if ( m_apbImageData.get() == NULL
      || m_image_width <= 0 || m_image_height <= 0
      || puiHistogram == NULL || puiFRed == 0
      || puiFGreen == NULL || puiFBlue == 0 )
      return FAILURE;

   ZeroMemory( puiHistogram, 32768 * sizeof(UINT) );
   ZeroMemory( puiFRed, 256 * sizeof(UINT) );
   ZeroMemory( puiFGreen, 256 * sizeof(UINT) );
   ZeroMemory( puiFBlue, 256 * sizeof(UINT) );

   UINT cPixels = m_image_width * m_image_height;
   RGBTriplet* prgb = (RGBTriplet*) m_apbImageData.get();
   for ( UINT k = 0; k < cPixels; k++ )
   {
      UINT
         uiRed = prgb[ k ].red,
         uiGreen = prgb[ k ].green,
         uiBlue = prgb[ k ].blue;

      if ( uiRed != 0 || uiGreen != 0 || uiBlue != 0 )
      {
         puiFRed[ uiRed ]++;
         puiFGreen[ uiGreen ]++;
         puiFBlue[ uiBlue ]++;

         UINT uiPos = ( ( ( ( uiRed >> 3 ) << 5 ) | ( uiGreen >> 3 ) ) << 5 ) | ( uiBlue >> 3 );
		   ASSERT( uiPos < 32768 );
		   puiHistogram[ uiPos ]++;
      }
   }

   return SUCCESS;
}




// ********************************************************************************************
// ********************************************************************************************

C_nitf_image::C_nitf_image()
{
	m_num_colors = 0;
	m_resolution = 0;
	m_err_num = 0;
	m_image_rep = 0;
	m_image_cat = 0;
	m_image_mode = 0;
	m_has_geo = FALSE;
	m_has_mask = FALSE;
	m_ul_lat = 0.0;
	m_ul_lon = 0.0;
	m_ur_lat = 0.0;
	m_ur_lon = 0.0;
	m_ll_lat = 0.0;
	m_ll_lon = 0.0;
	m_lr_lat = 0.0;
	m_lr_lon = 0.0;
	m_image_offset_x = 0;
	m_image_offset_y = 0;
	m_image_data = NULL;
	m_image_len = 0;
	m_image_start = 0;
	m_trans_pix_red = 0;
	m_trans_pix_grn = 0;
	m_trans_pix_blu = 0;
	m_back_pix_red = 0;
	m_back_pix_grn = 0;
	m_back_pix_blu = 0;
	m_buf_red = NULL;
	m_buf_grn = NULL;
	m_buf_blu = NULL;
	m_buf_start_row = -1;
	m_buf_end_row = -1;
	m_tredACCPOB.m_pACCPOS = NULL;
	m_pixel_type = 0;
	m_coord_type = 0;
	m_blocked_image_data_offset = 0;
	m_hblock_cnt = 0;
	m_vblock_cnt = 0;
	m_block_pix_wide = 0;
	m_block_pix_high = 0;
	m_has_tile_mask = FALSE;
	m_tile_mask_cnt = 0;
	m_tile_mask = NULL;
}


// ********************************************************************************************
// ********************************************************************************************

C_nitf_image::~C_nitf_image()
{
#if 0
	if (m_tredACCPOB.m_pACCPOS != NULL)
		delete [] m_tredACCPOB.m_pACCPOS;
#endif
}

// ********************************************************************************************
// ********************************************************************************************

C_bitmap::C_bitmap()
{
	m_width = 0;
	m_height = 0;
	m_num_bands = 0;
	m_img = NULL;
}

// ********************************************************************************************
// ********************************************************************************************

C_bitmap::~C_bitmap()
{
	if (m_img != NULL)
		free(m_img);
}

// ********************************************************************************************
// ********************************************************************************************

int C_bitmap::initialize(int width, int height, int numbands)
{
	int size;
	if (m_img != NULL)
		free(m_img);

	// check ranges
	if ((width < 1) || (width > 100000) || (height < 1) || (height > 100000))
		return FAILURE;
	if ((numbands < 1) || (numbands > 10))
		return FAILURE;

	size = width * height * numbands;

	m_img = (unsigned short*) malloc(size * sizeof(unsigned short));
	if (m_img == NULL)
		return FAILURE;

	m_width = width;
	m_height = height;
	m_num_bands = numbands;

	return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int C_bitmap::get_at(int x, int y, int bandnum, unsigned short *value)
{
	int size, ndx;

	if ((m_img == NULL) || (m_width < 1) || (m_height < 1) || (m_num_bands < 1))
		return FAILURE;

	if ((x < 0) || (x >= m_width) || (y < 0) || (y >= m_height))
		return FAILURE;

	size = m_width * m_height;
	ndx = (bandnum * size) + (y * m_width) + x;
	*value = m_img[ndx];

	return SUCCESS;
}
// ********************************************************************************************
// ********************************************************************************************

int C_bitmap::set_at(int x, int y, int bandnum, unsigned short value)
{
	int size, ndx;

	if ((m_img == NULL) || (m_width < 1) || (m_height < 1) || (m_num_bands < 1))
		return FAILURE;

	if ((x < 0) || (x >= m_width) || (y < 0) || (y >= m_height) || (bandnum < 0) || (bandnum >= m_num_bands))
		return FAILURE;

	size = m_width * m_height;
	ndx = (bandnum * size) + (y * m_width) + x;
	m_img[ndx] = value;

	return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

int C_bitmap::fill(int bandnum, unsigned short value)
{
	int size, ndx;

	if ((m_img == NULL) || (m_width < 1) || (m_height < 1) || (m_num_bands < 1))
		return FAILURE;

	if ((bandnum < 0) || (bandnum >= m_num_bands))
		return FAILURE;

	size = m_width * m_height;

	for (ndx=0; ndx<size; ndx++)
		m_img[ndx] = value;

	return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

// End of image_d.cpp
