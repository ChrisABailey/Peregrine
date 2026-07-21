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

// geotiff2.cpp



// 15-Sep-2005  (RAC)  Use file-specific temp file names for overview builds

#include "stdafx.h"
#include "common.h"
#include "defines.h"
#include "mem.h"
#include "geotiff.h"
#include <math.h>
//#include "ProgressDlg.h"
#include "util.h"
#ifdef _WIN32
#include "ComErrorObject.h"
#endif
#include "geotrans.h"

#undef FAST_SUBSAMPLE
#ifndef NO_FAST_SUBSAMPLE
   #define FAST_SUBSAMPLE
#endif

static inline
void byteswap_4(void* in, void* out)
{
   ((unsigned char*)out)[0] = ((unsigned char*)in)[3];
   ((unsigned char*)out)[1] = ((unsigned char*)in)[2];
   ((unsigned char*)out)[2] = ((unsigned char*)in)[1];
   ((unsigned char*)out)[3] = ((unsigned char*)in)[0];
}

///////////////////////////////////////////////////////////////////////////////////
///    New functions added to CGeoTiff for GeoRectification program
///////////////////////////////////////////////////////////////////////////////////

// ****************************************************************
// ****************************************************************

int CGeoTiff::get_32bit_elevation_data( int image_offset_x, int image_offset_y, // pixel offsets into the image
										int width, int height,				// size of the image to be return in arrays
										float *elev, CString & error_msg, IImageLibCallback *callback )
{
	int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
	int ibyte;
	int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
	unsigned int strip_offset, strip_byte_count, psize;
	float *strip_data, fdat;
	int outpos, maxpos;
	int iwidth, iheight, strip_cnt;
	CString error;
	int min_hpix, max_hpix, min_vpix, max_vpix;
	CUtil util;
//	FILE *fp = NULL;

	strip_data = NULL;

	if (m_image_is_tiled)
	{
		ASSERT(0);
		return FAILURE;
//		rslt = get_tiled_32bit_elevation_data(min_hpix, min_vpix, max_hpix, max_vpix, img, callback);
//		return rslt;
	}

	if ( m_planar_configuration == GEOTIFF_PLANAR_FORMAT)
	{
		ASSERT(0);
		return FAILURE;
	}

	// find strip offsets tag
	if ( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
		goto FAIL;

	// find strip byte counts tag
	if ( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
		goto FAIL;

	min_hpix = image_offset_x;
	min_vpix = image_offset_y;
	max_hpix = image_offset_x + width - 1;
	max_vpix = image_offset_y + height - 1;

	iwidth = width;
	iheight = height;

	maxpos = width * height;

	// allocate data for strips

	strip_byte_count = m_image_width * m_rows_per_strip * 4;

	strip_data = (float*) MEM_malloc( strip_byte_count );
	if (strip_data == NULL)
	{
		ASSERT(0);
		m_new_error_description = "Error Allocating Memory";
		goto FAIL;
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

//	fp = fopen(m_tiff_file_name, "rb");
//	if (fp == NULL)
//		return FAILURE;

	// loop through each strip
	for ( istrip = start_strip; istrip <= end_strip; istrip++ )
	{
		if (start_strip < end_strip)
		{
			double percent = (double) (istrip-start_strip) / (double) (end_strip-start_strip);
			percent *= 100.0;
			CString label = "Loading Elevation Data";
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
//	  fseek(fp, strip_offset, SEEK_SET);

		// something other than JPEG compression

		// read the strip
		if ( gtfRead( (unsigned char*) strip_data, strip_byte_count ) != SUCCESS )
		  goto FAIL;

//	  if (fread((unsigned char*) strip_data, strip_byte_count, 1, fp) < 1)
//		  goto FAIL;

		psize = strip_byte_count / 4;

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
				  while( ibyte < (int)strip_byte_count / 4)
				  {
					 if( hpix >= min_hpix && hpix <= max_hpix &&
						 vpix >= min_vpix && vpix <= max_vpix )
					 {
						 try
						 {
							 ASSERT(ibyte < (int) strip_byte_count);
							 ASSERT(subimage_pixel_index < maxpos);
							fdat = strip_data[ibyte];
							if (m_byte_order == 2)
								byteswap_4(&fdat, &(elev[subimage_pixel_index]));
							else
								elev[subimage_pixel_index] = fdat;
							subimage_pixel_index++;
						 }
						 catch(...)
						 {
							 ASSERT(0);
						 }
					 }
					 ibyte++;
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
			   case GEOTIFF_PACKBITS:            // PackBits compression scheme
				  goto FAIL;                     // not yet supported
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
//	if (fp != NULL)
//		fclose(fp);
	if (strip_data != NULL)
		MEM_free( strip_data );
	strip_data = NULL;

	util.clear_callback(callback);
	return SUCCESS;

FAIL:
//	if (fp != NULL)
//		fclose(fp);
	if ( strip_data != NULL )
	{
		MEM_free( strip_data );
		strip_data = NULL;
	}
	util.clear_callback(callback);
	return FAILURE;
}
// end of get_32bit_elevation_data

// ********************************************************************************************
// ********************************************************************************************

// ****************************************************************
// ****************************************************************

int CGeoTiff::get_supersampled_rgb_subimage( double factor,	// the degree of oversampling
											int image_offset_x, int image_offset_y, // pixel offsets into the image
											int width, int height,						  // size of the image to be return in arrays
											unsigned char *img, IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage, subsampled according to the sampling argument
   // the sampling argument indicates the fraction of the image's rows and
   // columns that are to be retained in the subsampled image. a sampling of 3
   // indicates that every third row and column should be retained
   // returns SUCCESS or FAILURE

   int row, col, index, icol, irow, iwidth, rslt;
   unsigned int twidth;
   unsigned char *img_row;
   CUtil util;

   if (m_image_is_tiled)
   {
	   rslt = get_supersampled_tiled_rgb_subimage(factor, image_offset_x, image_offset_y, width, height, img, callback);
	   return rslt;
   }

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
		return FAILURE;

   // return error if image type is not supported
   if( !m_image_type_supported )
		return FAILURE;

   // return FAILURE if sampling is less than 1
   if ( factor < 1.0 )
		return FAILURE;

   // allocate arrays to hold one row of data
   img_row = NULL;
   img_row = (unsigned char*)MEM_malloc( width*3 );
   if( img_row == NULL )
		goto FAIL;

	index = 0;
	iwidth = image_offset_x+(int) ((double) width / factor)-1;
	if ((unsigned int) iwidth > m_image_width)
		iwidth = m_image_width -1;
   for( row = 0; row < height; row++ )
   {
		double percent = (double) row / (double) height;
		percent *= 100.0;
		CString label = "Loading Image";
		if (!util.send_user_update_and_continue(callback, percent, label))
		{
			m_new_error_description = "Operation canceled by user";
			goto FAIL;
		}

		irow = (int) ((double) row / factor);
		irow += image_offset_y;
		if ((unsigned int) irow < m_image_length)
		{
			switch( m_image_type )
			{
				case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
					goto FAIL;
				case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
					 if (m_samples_per_pixel == 3)
						 rslt = get_24bit_rgb_as_rgb_subimage( image_offset_x, irow, iwidth, irow, img_row, NULL );
					 else
						rslt =  get_8bit_grayscale_as_rgb_subimage( image_offset_x, irow, iwidth, irow, img_row, NULL);
					if (rslt != SUCCESS )
						 goto FAIL;
					break;
				case GEOTIFF_IMAGE_TYPE_256_COLOR:
					rslt = get_8bit_palette_as_rgb_subimage( image_offset_x, irow, iwidth, irow, img_row, NULL );
					if (rslt != SUCCESS )
						 goto FAIL;
					break;
				case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:

//					rslt = get_24bit_rgb_as_rgb_subimage( image_offset_x, irow, iwidth, irow,
//						 red_row, green_row, blue_row );
					twidth = image_offset_x+width-1;
					if (twidth > m_image_width-1)
						twidth = m_image_width-1;
					rslt = get_rgb_subimage( image_offset_x, irow, width, 1, img_row, NULL );
					if (rslt != SUCCESS )
						 goto FAIL;
					break;
				default:                           // should always be one of above
					ASSERT( FALSE );
					goto FAIL;
			}
		}

		for( col = 0; col < width; col++ )
		{
			icol = col;
			icol = (int) ((double) icol / factor);
			if ((icol <= iwidth) && ((unsigned int) irow < m_image_length))
			{
				img[index*3+0] = img_row[icol*3+0];
				img[index*3+1] = img_row[icol*3+1];
				img[index*3+2] = img_row[icol*3+2];
			}
			else
			{
				img[index*3+0] = 0;
				img[index*3+1] = 0;
				img[index*3+2] = 0;
			}

			index++;
		}
   }

	util.clear_callback(callback);

   // deallocate memory
   MEM_free( img_row );

   return SUCCESS;

FAIL:
	util.clear_callback(callback);

   // deallocate memory
   if( img_row != NULL )
	   MEM_free( img_row );
   return FAILURE;
}
// end of get_supersampled_rgb_subimage

// ****************************************************************
// ****************************************************************

int CGeoTiff::get_supersampled_tiled_rgb_subimage( double factor,	// the degree of oversampling
												int image_offset_x, int image_offset_y, // pixel offsets into the image
												int width, int height,			// size of the image to be return in arrays
												unsigned char *img, IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage, subsampled according to the sampling argument
   // the sampling argument indicates the fraction of the image's rows and
   // columns that are to be retained in the subsampled image. a sampling of 3
   // indicates that every third row and column should be retained
   // returns SUCCESS or FAILURE

	int row, col, index, icol, irow, iwidth, iheight, size, rslt;
	BYTE *bufimg;
	int inpos, outpos;
	CUtil util;

	// return if no file loaded or there was an error loading the tiff file
	if( !m_file_loaded || m_error )
		return FAILURE;

	// return error if image type is not supported
	if( !m_image_type_supported )
		return FAILURE;

	// return FAILURE if sampling is less than 1
	if ( factor < 1.0 )
		return FAILURE;


	index = 0;
//	iwidth = image_offset_x+(int) ((double) width / factor)-1;
	iwidth = (int) ((double) width / factor)-1;
	if ((unsigned int) iwidth > m_image_width)
		iwidth = m_image_width -1;
//	iheight = image_offset_y+(int) ((double) height / factor)-1;
	iheight = (int) ((double) height / factor)-1;
	if ((unsigned int) iheight > m_image_length)
		iheight = m_image_length -1;

	size = iwidth * iheight;

	// allocate arrays to hold sub image
	bufimg = (BYTE*) MEM_malloc(size*3);
	if (bufimg == NULL)
		return FAILURE;

	// get the smaller image
	switch( m_image_type )
	{
		case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
			goto FAIL;
		case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
			 if (m_samples_per_pixel == 3)
				 rslt = get_24bit_rgb_as_rgb_subimage( image_offset_x, image_offset_y,
										image_offset_x+iwidth-1, image_offset_y+iheight-1,
										bufimg, callback);
			 else
				rslt =  get_8bit_grayscale_as_rgb_subimage( image_offset_x, image_offset_y,
										image_offset_x+iwidth-1, image_offset_y+iheight-1,
										bufimg, callback);
			if (rslt != SUCCESS )
				 goto FAIL;
			break;
		case GEOTIFF_IMAGE_TYPE_256_COLOR:
			rslt = get_8bit_palette_as_rgb_subimage(  image_offset_x, image_offset_y,
												image_offset_x+iwidth-1, image_offset_y+iheight-1,
												bufimg, callback);
			if (rslt != SUCCESS )
				 goto FAIL;
			break;
		case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
			rslt = get_24bit_rgb_as_rgb_subimage(  image_offset_x, image_offset_y,
												image_offset_x+iwidth-1, image_offset_y+iheight-1,
												bufimg, callback);
			if (rslt != SUCCESS )
				 goto FAIL;
			break;
		default:                           // should always be one of above
			ASSERT( FALSE );
			goto FAIL;
	}

	// fill the image buffer
	for( row = 0; row < height; row++ )
	{
		irow = (int) ((double) row / factor);
		for (col = 0; col < width; col++)
		{
			icol = col;
			icol = (int) ((double) icol / factor);
			inpos = (irow * iwidth) + icol;
			outpos = (row * width) + col;
			if (inpos >= size)
				continue;
			img[outpos*3+0] = bufimg[inpos*3+0];
			img[outpos*3+1] = bufimg[inpos*3+1];
			img[outpos*3+2] = bufimg[inpos*3+2];
		}
	}

	util.clear_callback(callback);

	// deallocate memory
	if (bufimg)
		MEM_free( bufimg );

   return SUCCESS;

FAIL:
	util.clear_callback(callback);

	// deallocate memory
	if (bufimg)
		MEM_free( bufimg );

   return FAILURE;
}
// end of get_supersampled_tiled_rgb_subimage

// ****************************************************************
// ****************************************************************

int CGeoTiff::get_subsampled_tiled_rgb_subimage( int sampling,				// the degree of oversampling
									int image_offset_x, int image_offset_y, // pixel offsets into the image
									int width, int height,					// size of the image to be return in arrays
									unsigned char *img,
									IImageLibCallback *callback )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage, subsampled according to the sampling argument
   // the sampling argument indicates the fraction of the image's rows and
   // columns that are to be retained in the subsampled image. a sampling of 3
   // indicates that every third row and column should be retained
   // returns SUCCESS or FAILURE

	int index, iwidth, iheight, size, tile_num, tile_cnt;
	BYTE *red, *grn, *blu;
	unsigned int x, y, ix, iy;
	int inpos, outpos, maxpos;
	int subimage_min_x, subimage_max_x, subimage_min_y, subimage_max_y;
	int subimage_ul_x, subimage_ul_y, subimage_lr_x, subimage_lr_y;
	int dummy;
	CUtil util;

	// return if no file loaded or there was an error loading the tiff file
	if( !m_file_loaded || m_error )
		return FAILURE;

	// return error if image type is not supported
	if( !m_image_type_supported )
		return FAILURE;

	// return FAILURE if sampling is less than 1
	if ( sampling < 1 )
		return FAILURE;

	subimage_min_x = 0;
	subimage_min_y = 0;
	subimage_max_x = width * sampling;
	subimage_max_y = height * sampling;
	subimage_ul_x = image_offset_x;
	subimage_ul_y = image_offset_y;
	subimage_lr_x = subimage_ul_x + subimage_max_x - 1;
	subimage_lr_y = subimage_ul_y + subimage_max_y - 1;

	index = 0;
	iwidth = image_offset_x+ (width * sampling)-1;
	if ((unsigned int) iwidth > m_image_width)
		iwidth = m_image_width -1;
	iheight = image_offset_y+ (height * sampling)-1;
	if ((unsigned int) iheight > m_image_length)
		iheight = m_image_length -1;

	iwidth = width * sampling;
	iheight = height * sampling;

	size = m_tile_width * m_tile_length;

	// allocate arrays to hold a tile of data
	red = (BYTE*) MEM_malloc(size);
	grn = (BYTE*) MEM_malloc(size);
	blu = (BYTE*) MEM_malloc(size);

	if (!red || !grn || !blu)
		goto FAIL;

	unsigned int min_tile_row, min_tile_col, max_tile_row, max_tile_col;
	unsigned int tile_row, tile_col, tile;
	int imax_hpix, imax_vpix;
	int imgx, imgy, imgx_samp, imgy_samp;
	int image_x, image_y;

	imax_hpix = image_offset_x + (width * sampling);
	imax_vpix = image_offset_y + (height * sampling);

	// determine range of tile rows and columns in subimage
	min_tile_row = image_offset_y / m_tile_length;
	max_tile_row = (imax_vpix / m_tile_length);
	min_tile_col = image_offset_x / m_tile_width;
	max_tile_col = (imax_hpix / m_tile_width);

	if (max_tile_row >= m_num_tiles_down)
		max_tile_row = m_num_tiles_down -1;
	if (max_tile_col >= m_num_tiles_across)
		max_tile_col = m_num_tiles_across -1;

	dummy = 0;

	maxpos = width * height;

	tile_num = (max_tile_row - min_tile_row) * (max_tile_col - min_tile_col);
	tile_cnt = 0;

	// loop through tiles covering the subimage
	for( tile_row = min_tile_row; tile_row <= max_tile_row; tile_row++ )
	{
		for( tile_col = min_tile_col; tile_col <= max_tile_col; tile_col++ )
		{
			double percent = (double) tile_cnt / (double) tile_num;
			percent *= 100.0;
			CString label = "Loading Image";
			if (!util.send_user_update_and_continue(callback, percent, label))
			{
				m_new_error_description = "Operation canceled by user";
				goto FAIL;
			}
			tile_cnt++;

			tile = m_num_tiles_across * tile_row + tile_col;

			if ( get_tile_as_rgb_image( tile, red, grn, blu) != SUCCESS )
				goto FAIL;

			for (y=0; y<m_tile_length; y+=sampling)
			{
				image_y = (tile_row * m_tile_length) + y;
				iy = y / sampling;
				imgy = (tile_row * m_tile_length / sampling) + iy;
				imgy_samp = imgy * sampling;
				if ((image_y >= subimage_ul_y) && (image_y < subimage_lr_y) && (image_y < (int) m_image_length))
				{
					for (x=0; x<m_tile_width; x+=sampling)
					{
						image_x = (tile_col * m_tile_width) + x;
						ix = x / sampling;
						imgx = (tile_col * m_tile_width / sampling) + ix;
						imgx_samp = imgx * sampling;

						if ((image_x >= subimage_ul_x) && (image_x < subimage_lr_x) && (image_x < (int) m_image_width))
						{
							outpos = (((image_y - subimage_ul_y)/ sampling) * width) + (image_x - subimage_ul_x)/sampling;
//							outpos = ((imgy - image_offset_y) * width) + (imgx - image_offset_x);
							inpos = (y * m_tile_width) + x;
							if ((outpos >= 0) && (outpos < maxpos))
							{
								img[outpos*3+0] = red[inpos];
								img[outpos*3+1] = grn[inpos];
								img[outpos*3+2] = blu[inpos];
							}
						}
						else
							dummy = 1;
					}
				}
				else
					dummy = 1;
			}
		}
	}

	util.clear_callback(callback);

	// deallocate memory
	if (red)
		MEM_free( red );
	if (grn)
		MEM_free( grn );
	if (blu)
		MEM_free( blu );

   return SUCCESS;

FAIL:
	util.clear_callback(callback);
	// deallocate memory
	if (red)
		MEM_free( red );
	if (grn)
		MEM_free( grn );
	if (blu)
		MEM_free( blu );

   return FAILURE;
}
// end of get_subsampled_tiled_rgb_subimage

// ****************************************************************
// ****************************************************************

int CGeoTiff::get_subsampled_rgb_subimage2( int sampling,
                         int image_offset_x, int image_offset_y, // pixel offsets into the image
								 int width, int height,						  // size of the image to be return in arrays
                         unsigned char *img, IImageLibCallback *callback )
{
	// this function returns the pixel colors in the argument arrays for the
	// specified subimage, subsampled according to the sampling argument
	// the sampling argument indicates the fraction of the image's rows and
	// columns that are to be retained in the subsampled image. a sampling of 3
	// indicates that every third row and column should be retained
	// returns SUCCESS or FAILURE

	int row, col, index, icol, irow, iwidth, iheight;
	unsigned char *img_row = NULL;
	int rslt;
	CUtil util;

   if (m_image_is_tiled)
   {
	   rslt = get_subsampled_tiled_rgb_subimage(sampling, image_offset_x, image_offset_y,
											width, height, img, callback);
	   return rslt;
   }

	// return if no file loaded or there was an error loading the tiff file
	if( !m_file_loaded || m_error )
		return FAILURE;

	// return error if image type is not supported
	if( !m_image_type_supported )
		return FAILURE;

	// return failure if subimage is invalid
	if( image_offset_x < 0 || image_offset_x >= (int)m_image_width )
		return FAILURE;
	if( image_offset_y < 0 || image_offset_y >= (int)m_image_length )
		return FAILURE;

	// return FAILURE if sampling is less than 1
	if( sampling < 1 )
		return FAILURE;

	if (m_image_type == GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED)
		goto FAIL;

	// allocate arrays to hold one row of data
	img_row = NULL;

	iwidth = image_offset_x + (width * sampling);
	if ((unsigned int) iwidth > m_image_width)
		iwidth = m_image_width;
	iheight = image_offset_y + (height * sampling);
	if ((unsigned int) iheight > m_image_length)
		iheight = m_image_length;

   img_row = (unsigned char*)MEM_malloc( iwidth * 3);
   if ( img_row == NULL )
	   goto FAIL;

	index = 0;
	for( row = 0; row < height; row++ )
	{
		double percent = (double) row / (double) height;
		percent *= 100.0;
		CString label = "Loading Image";
		if (!util.send_user_update_and_continue(callback, percent, label))
		{
			m_new_error_description = "Operation canceled by user";
			goto FAIL;
		}
		irow = image_offset_y + (row * sampling);
		if (irow < iheight)
		{
			// select image type and call appropriate function
			switch( m_image_type )
			{
				case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
					 if (m_samples_per_pixel == 3)
						 rslt = get_24bit_rgb_as_rgb_subimage( image_offset_x, irow, iwidth-1, irow, img_row, callback) ;
					 else
						rslt = get_8bit_grayscale_as_rgb_subimage( image_offset_x, irow, iwidth-1, irow, img_row, callback );
					if (rslt != SUCCESS )
					{
						m_new_error_description = "Error loading subimage";
						goto FAIL;
					}
					break;
				case GEOTIFF_IMAGE_TYPE_256_COLOR:
					rslt = get_8bit_palette_as_rgb_subimage( image_offset_x, irow, iwidth-1, irow, img_row, callback );
					if (rslt != SUCCESS )
						goto FAIL;
					break;
				case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
//					rslt = get_24bit_rgb_as_rgb_subimage( image_offset_x, irow, iwidth-1,
//												irow, red_row, green_row, blue_row );
					rslt = get_rgb_subimage( image_offset_x, irow, width * sampling, 1, img_row, callback );
					if (rslt != SUCCESS )
						goto FAIL;
					break;
			}
		}
		for( col = 0; col < width; col++ )
		{
			icol = col * sampling;
			if ((icol < (iwidth - image_offset_x)) && (irow < iheight))
			{
				img[index*3+0]   = img_row[icol*3+0];
				img[index*3+1] = img_row[icol*3+1];
				img[index*3+2]  = img_row[icol*3+2];
			}
			else
			{
				img[index*3+0] = 0;
				img[index*3+1] = 0;
				img[index*3+2] = 0;
			}
			index++;
		}
	}

	util.clear_callback(callback);
   // deallocate memory
   MEM_free( img_row );

   return SUCCESS;

FAIL:
	util.clear_callback(callback);
   // deallocate memory
   if ( img_row != NULL )
		MEM_free( img_row );
   return FAILURE;
}
// end of get_subsampled_rgb_subimage2

// ****************************************************************
// ****************************************************************

BOOL CGeoTiff::is_image_tiled()
{
	return m_image_is_tiled;
}

// ****************************************************************
// ****************************************************************

void CGeoTiff::get_tile_size(int *width, int *height)
{
	*width = m_tile_width;
	*height = m_tile_length;
}

// ****************************************************************
// ****************************************************************

int CGeoTiff::compute_histogram2( int *hist, unsigned long *freq_red, unsigned long *freq_grn,
								 unsigned long *freq_blu, IImageLibCallback *callback)
{
	int j, k, red, grn, blu, rslt;
	BYTE *buf_red, *buf_grn, *buf_blu, *buf_img;
	int max_item, max_pix, pos, vpix;
	int trans_inc;
	CUtil util;

	buf_red = NULL;
	buf_grn = NULL;
	buf_blu = NULL;
	buf_img = NULL;

	trans_inc = (m_image_width * m_image_length) / 100;

	// clear histogram
	for( j = 0; j < 32768; j++ )
		hist[j] = 0;

	// clear frequencies
	for (j=0; j < 256; j++)
	{
		freq_red[j] = 0;
		freq_grn[j] = 0;
		freq_blu[j] = 0;
	}

	if (m_image_is_tiled)
	{
		max_item = m_num_tiles_down * m_num_tiles_across;
		max_pix = m_tile_width * m_tile_length;

		buf_red = (BYTE*) MEM_malloc(max_pix);
		buf_grn = (BYTE*) MEM_malloc(max_pix);
		buf_blu = (BYTE*) MEM_malloc(max_pix);
		if ( buf_red == NULL || buf_grn == NULL || buf_blu == NULL )
			goto FAIL;

		for (k=0; k<max_item; k++)
		{
			double percent = (double) k / (double) max_item;
			percent *= 100.0;
			CString label = "Loading Image";
			if (!util.send_user_update_and_continue(callback, percent, label))
			{
				rslt = FAILURE;
				m_new_error_description = "Operation canceled by user";
				goto FAIL;
			}

			rslt = get_tile_as_rgb_image( k, buf_red, buf_grn, buf_blu);
			if (rslt != SUCCESS)
			{
				m_new_error_description = "Error reading image tile";
				goto FAIL;
			}

			for (j=0; j<max_pix; j++)
			{
				// clip to 5 bits
				red = buf_red[j];
				grn = buf_grn[j];
				blu = buf_blu[j];
				red = (int) (red >> 3);
				grn = (int) (grn >> 3);
				blu = (int) (blu >> 3);
				pos = (red << 10) + (grn << 5) + blu;
				hist[pos] += 1;
				freq_red[red]++;
				freq_grn[grn]++;
				freq_blu[blu]++;
			}
		}

		// if there is a transparent color, make sure is shows up in the histogram
//		if (m_has_transparent_color)
		{
			// clip to 5 bits
			red = GetRValue(m_transparent_color);
			grn = GetGValue(m_transparent_color);
			blu = GetBValue(m_transparent_color);
			red = (int) (red >> 3);
			grn = (int) (grn >> 3);
			blu = (int) (blu >> 3);
			pos = (red << 10) + (grn << 5) + blu;
			hist[pos] += trans_inc;
			freq_red[red] += trans_inc;
			freq_grn[grn] += trans_inc;
			freq_blu[blu] += trans_inc;
		}

		util.clear_callback(callback);
		MEM_free(buf_red);
		MEM_free(buf_grn);
		MEM_free(buf_blu);

		return SUCCESS;
	}

	// striped image
	// allocate data for one row
	buf_img = (unsigned char*)MEM_malloc( m_image_width * 3);
	if ( buf_img == NULL )
		goto FAIL;

	// loop through each row
	for ( vpix = 0; vpix < (int)m_image_length; vpix++ )
	{
		double percent = (double) vpix / (double) m_image_length;
		percent *= 100.0;
		CString label = "Loading Image";
		if (!util.send_user_update_and_continue(callback, percent, label))
		{
			rslt = FAILURE;
			m_new_error_description = "Operation canceled by user";
			goto FAIL;
		}

		// get the row
		rslt = get_rgb_subimage( 0, vpix, m_image_width, 1, buf_img, callback);
		if (rslt != SUCCESS )
			goto FAIL;

		for ( j = 0; j < (int)m_image_width; j++ )
		{
			// clip to 5 bits
			red = buf_img[j*3+0];
			grn = buf_img[j*3+1];
			blu = buf_img[j*3+2];
			red = (int) (red >> 3);
			grn = (int) (grn >> 3);
			blu = (int) (blu >> 3);
			pos = (red << 10) + (grn << 5) + blu;
			hist[pos] += 1;
			freq_red[red]++;
			freq_grn[grn]++;
			freq_blu[blu]++;
		}
   }


	// if there is a transparent color, make sure is shows up in the histogram
//	if (m_has_transparent_color)
	{
		// clip to 5 bits
		red = GetRValue(m_transparent_color);
		grn = GetGValue(m_transparent_color);
		blu = GetBValue(m_transparent_color);
		red = (int) (red >> 3);
		grn = (int) (grn >> 3);
		blu = (int) (blu >> 3);
		pos = (red << 10) + (grn << 5) + blu;
		hist[pos] += trans_inc;
		freq_red[red] += trans_inc;
		freq_grn[grn] += trans_inc;
		freq_blu[blu] += trans_inc;
	}

	util.clear_callback(callback);
   // free row memory
   if (buf_red != NULL)
	   MEM_free(buf_red);
   if (buf_grn != NULL)
	   MEM_free(buf_grn);
   if (buf_blu != NULL)
	   MEM_free(buf_blu);
   if (buf_img != NULL)
	   MEM_free(buf_img);

   return SUCCESS;

FAIL:
	util.clear_callback(callback);
	if (buf_red != NULL)
		MEM_free(buf_red);
	if (buf_grn != NULL)
		MEM_free(buf_grn);
	if (buf_blu != NULL)
		MEM_free(buf_blu);
	if (buf_img != NULL)
		MEM_free(buf_img);

   return FAILURE;

}
// end of compute_histogram2

// ****************************************************************
// ****************************************************************

// get a copy of geotiff geokeys

int CGeoTiff::get_geokeys(int *numgeokeys, CGeoKey **geokeys)
{
	int i, j;

	*numgeokeys = 0;

	if (m_num_geokeys < 1)
		return FAILURE;

	*geokeys = (CGeoKey*) MEM_malloc(m_num_geokeys * sizeof(CGeoKey));
	*numgeokeys = m_num_geokeys;

	for ( i = 0; i < m_num_geokeys; i++ )
	{
		m_geokeys[i].clear( );
		geokeys[i]->m_geokey_id = m_geokeys[i].m_geokey_id;
		geokeys[i]->m_tiff_tag_location = m_geokeys[i].m_tiff_tag_location;
		geokeys[i]->m_count = m_geokeys[i].m_count;
		geokeys[i]->m_value_offset = m_geokeys[i].m_value_offset;

		// check for data contained in value offset (m_tiff_tag_location = 0)
		if ( m_geokeys[i].m_tiff_tag_location == 0 )
		{
			// data is a single unsigned short contained in value_offset
			geokeys[i]->m_count = 1;
			geokeys[i]->m_type = GEOTIFF_SHORT;
			geokeys[i]->m_short_values = (unsigned short*)MEM_malloc( sizeof(unsigned short) );
			geokeys[i]->m_short_values[0] = m_geokeys[i].m_short_values[0];
		}
		else
		{
			// data is contained in the tag indicated by tiff tag location

			int count = m_geokeys[i].m_count;

			// data type is the type of the tag data
			geokeys[i]->m_type = m_geokeys[i].m_type;

			// allocate memory for data and copy from tag array
			switch( m_geokeys[i].m_type )
			{
				case GEOTIFF_ASCII:             // null-terminated string
					//               m_geokeys[i].m_ascii_values = new char[count];
					geokeys[i]->m_ascii_values = (char*)MEM_malloc( count );
					// copy data from tag at value offset
					for ( j = 0; j < count; j++ )
						geokeys[i]->m_ascii_values[j] = m_geokeys[i].m_ascii_values[j];
					// add terminating null
					geokeys[i]->m_ascii_values[count-1] = NULL;
					break;
				case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
					geokeys[i]->m_short_values = (unsigned short*)MEM_malloc( count * sizeof(unsigned short) );
					// copy data from tag at value offset
					for ( j = 0; j < count; j++ )
						geokeys[i]->m_short_values[j] = m_geokeys[i].m_short_values[j];
					break;
				case GEOTIFF_DOUBLE:            // 8-byte double value
					//               m_geokeys[i].m_double_values = new double[count];
					geokeys[i]->m_double_values = (double*)MEM_malloc( count * sizeof(double) );
					// copy data from tag at value offset
					for ( j = 0; j < count; j++ )
						geokeys[i]->m_double_values[j] = m_geokeys[i].m_double_values[j];
					break;
				default:
					// data type is unknown - don't read values
					break;
			}

		}

		// assign names to geokey, type, and value
		geokeys[i]->m_geokey_name = m_geokeys[i].m_geokey_name;
		geokeys[i]->m_type_name = m_geokeys[i].m_type_name;
		geokeys[i]->m_value_name = m_geokeys[i].m_value_name;

		// geokey is now completely defined
		geokeys[i]->m_defined = TRUE;

	}
	return SUCCESS;
}
// end of get_geokeys

// ****************************************************************
// ****************************************************************

/*
// get a copy of the tif tags

int CGeoTiff::get_tags(CTiffTag **tag, int *numtags)
{
	int i;

	*numtags = 0;

	if (m_num_tags < 1)
		return FAILURE;

//	ASSERT(*tag = NULL);

	if (*tag != NULL)
		return FAILURE;

	*tag = new CTiffTag[m_num_tags];
	*numtags = m_num_tags;

	for ( i = 0; i < m_num_tags; i++ )
	{
//		init_tag((*tag)[i]);
		copy_tag((*tag)[i], m_tags[i]);
	}
	return SUCCESS;
}
// end of get_tags
*/

// ****************************************************************
// ****************************************************************

// get a copy of the tif tags

int CGeoTiff::get_tags(CTiffTag *tag)
{
	int i;

	if (m_num_tags < 1)
		return FAILURE;

	if (tag == NULL)
		return FAILURE;

	for ( i = 0; i < m_num_tags; i++ )
	{
//		init_tag((*tag)[i]);
		copy_tag(tag[i], &m_tags[i]);
	}
	return SUCCESS;
}
// end of get_tags


// ****************************************************************
// ****************************************************************
//

int CGeoTiff::copy_tag( CTiffTag &tag, CTiffTag *src_tag )
{
   int data_size_in_bytes;
   unsigned int k;

   // clear the tag
   tag.clear( );

   tag.m_tag_id = src_tag->m_tag_id;
   tag.m_type	= src_tag->m_type;
   tag.m_count	= src_tag->m_count;

   // allocate memory for data and determine data size
   switch( tag.m_type )
   {

      case GEOTIFF_BYTE:              // 8-bit unsigned integer
         data_size_in_bytes = 1;
         tag.m_byte_values = (unsigned char*)MEM_malloc( tag.m_count );
		 for (k=0; k<tag.m_count; k++)
			 tag.m_byte_values[k] = src_tag->m_byte_values[k];
         break;
      case GEOTIFF_ASCII:              // 8-bit unsigned integer
         data_size_in_bytes = 1;
         tag.m_ascii_values = (char*)MEM_malloc( tag.m_count );
		 for (k=0; k<tag.m_count; k++)
			 tag.m_ascii_values[k] = src_tag->m_ascii_values[k];
         break;
      case GEOTIFF_SHORT:              // 8-bit unsigned integer
         data_size_in_bytes = 2;
         tag.m_short_values = (unsigned short*)MEM_malloc( tag.m_count * sizeof(unsigned short) );
		 for (k=0; k<tag.m_count; k++)
			 tag.m_short_values[k] = src_tag->m_short_values[k];
         break;
      case GEOTIFF_LONG:              // 8-bit unsigned integer
         data_size_in_bytes = 4;
         tag.m_long_values = (unsigned int*)MEM_malloc( tag.m_count * sizeof(unsigned int) );
		 for (k=0; k<tag.m_count; k++)
			 tag.m_long_values[k] = src_tag->m_long_values[k];
         break;
      case GEOTIFF_RATIONAL:              // 8-bit unsigned integer
         data_size_in_bytes = 8;
         tag.m_rational_numerator_values = (unsigned int*)MEM_malloc( tag.m_count * sizeof(unsigned int) );
         tag.m_rational_denominator_values = (unsigned int*)MEM_malloc( tag.m_count * sizeof(unsigned int) );
		 for (k=0; k<tag.m_count; k++)
		 {
			 tag.m_rational_numerator_values[k] = src_tag->m_rational_numerator_values[k];
			 tag.m_rational_denominator_values[k] = src_tag->m_rational_denominator_values[k];
		 }
         break;
      case GEOTIFF_SBYTE:              // 8-bit unsigned integer
         data_size_in_bytes = 1;
         tag.m_sbyte_values = (char*)MEM_malloc( tag.m_count );
		 for (k=0; k<tag.m_count; k++)
			 tag.m_sbyte_values[k] = src_tag->m_sbyte_values[k];
         break;
      case GEOTIFF_UNDEFINED_TYPE:              // 8-bit unsigned integer
         data_size_in_bytes = 1;
         tag.m_undefined_values = (char*)MEM_malloc( tag.m_count );
		 for (k=0; k<tag.m_count; k++)
			 tag.m_undefined_values[k] = src_tag->m_undefined_values[k];
         break;
      case GEOTIFF_SSHORT:              // 8-bit unsigned integer
         data_size_in_bytes = 2;
         tag.m_sshort_values = (short*)MEM_malloc( tag.m_count * sizeof(short) );
		 for (k=0; k<tag.m_count; k++)
			 tag.m_sshort_values[k] = src_tag->m_sshort_values[k];
         break;
      case GEOTIFF_SLONG:              // 8-bit unsigned integer
         data_size_in_bytes = 4;
         tag.m_slong_values = (int*)MEM_malloc( tag.m_count * sizeof(int) );
		 for (k=0; k<tag.m_count; k++)
			 tag.m_slong_values[k] = src_tag->m_slong_values[k];
         break;
      case GEOTIFF_SRATIONAL:              // 8-bit unsigned integer
         data_size_in_bytes = 8;
         tag.m_srational_numerator_values = (int*)MEM_malloc( tag.m_count * sizeof(int) );
         tag.m_srational_denominator_values = (int*)MEM_malloc( tag.m_count * sizeof(int) );
         tag.m_double_values = (double*)MEM_malloc( tag.m_count * sizeof(double) );
		 for (k=0; k<tag.m_count; k++)
		 {
			 tag.m_srational_numerator_values[k] = src_tag->m_srational_numerator_values[k];
			 tag.m_srational_denominator_values[k] = src_tag->m_srational_denominator_values[k];
			 tag.m_double_values[k] = src_tag->m_double_values[k];
		 }
         break;
      case GEOTIFF_FLOAT:              // 8-bit unsigned integer
         data_size_in_bytes = 4;
         tag.m_float_values = (float*)MEM_malloc( tag.m_count * sizeof(float) );
		 for (k=0; k<tag.m_count; k++)
			 tag.m_float_values[k] = src_tag->m_float_values[k];
         break;
      case GEOTIFF_DOUBLE:              // 8-bit unsigned integer
         data_size_in_bytes = 8;
         tag.m_double_values = (double*)MEM_malloc( tag.m_count * sizeof(double) );
		 for (k=0; k<tag.m_count; k++)
			 tag.m_double_values[k] = src_tag->m_double_values[k];
         break;

      default:
         // data type is unknown - don't read values
         break;
   }

   tag.m_tag_name = src_tag->m_tag_name;
   tag.m_type_name = src_tag->m_type_name;
   tag.m_value_name = src_tag->m_value_name;
   tag.m_defined = TRUE;

   // return SUCCESS
   return SUCCESS;
}
// end of copy_tag

// ****************************************************************
// ****************************************************************

void CGeoTiff::init_tag( CTiffTag &tag )
{
   tag.m_defined = FALSE;
   tag.m_tag_id = 0;
   tag.m_type = 0;
   tag.m_count = 0;
   tag.m_value_offset = 0;
//   tag.m_tag_name = "";
//   tag.m_type_name = "";
//   tag.m_value_name = "";

   // set all array pointers to NULL
   tag.m_byte_values = NULL;
   tag.m_ascii_values = NULL;
   tag.m_short_values = NULL;
   tag.m_long_values = NULL;
   tag.m_rational_numerator_values = NULL;
   tag.m_rational_denominator_values = NULL;
   tag.m_sbyte_values = NULL;
   tag.m_undefined_values = NULL;
   tag.m_sshort_values = NULL;
   tag.m_slong_values = NULL;
   tag.m_srational_numerator_values = NULL;
   tag.m_srational_denominator_values = NULL;
   tag.m_float_values = NULL;
   tag.m_double_values = NULL;
}

// ****************************************************************
// ****************************************************************

int CGeoTiff::get_date_time( int *year, int *month, int *day, int *hour, int *minute, int *second, CString & datestr )
{
	int ndx, rslt, k, mon;
	CString sdate, tstr, monthstr;
	char buf[21];

	rslt = find_geokey(GEOTIFF_DATE_TIME_TAG, ndx);
	if (rslt != SUCCESS)
		return rslt;

	if (m_tags[ndx].m_tag_id != GEOTIFF_DATE_TIME_TAG)
		return FAILURE;

	if (m_tags[ndx].m_count != 20)
		return FAILURE;

	for (k=0; k< (int) m_tags[ndx].m_count; k++)
		buf[k] = m_tags[ndx].m_ascii_values[k];

	buf[19] = '\0';

	sdate = buf;
	datestr = sdate;

	tstr = sdate.Left(4);
	*year = atoi(tstr);
	tstr = sdate.Mid(5, 2);
	*month = atoi(tstr);
	tstr = sdate.Mid(8, 2);
	*day = atoi(tstr);
	tstr = sdate.Mid(11, 2);
	*hour = atoi(tstr);
	tstr = sdate.Mid(14, 2);
	*minute = atoi(tstr);
	tstr = sdate.Mid(17, 2);
	*second = atoi(tstr);

	monthstr = "UNK";
	mon = *month;
	if (mon == 1)
		monthstr = "Jan";
	else if (mon == 2)
		monthstr = "Feb";
	else if (mon == 3)
		monthstr = "Mar";
	else if (mon == 4)
		monthstr = "Apr";
	else if (mon == 5)
		monthstr = "May";
	else if (mon == 6)
		monthstr = "Jun";
	else if (mon == 7)
		monthstr = "Jul";
	else if (mon == 8)
		monthstr = "Aug";
	else if (mon == 9)
		monthstr = "Sep";
	else if (mon == 10)
		monthstr = "Oct";
	else if (mon == 11)
		monthstr = "Nov";
	else if (mon == 12)
		monthstr = "Dec";

	sdate = monthstr;

	return SUCCESS;
}
// end of get_date_time

// ****************************************************************
// ****************************************************************

int CGeoTiff::geotiff_datum_to_string(int datum_num, CString & datum_str)
{
	int rslt = SUCCESS;

	switch(datum_num)
	{
		case  GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1927 : datum_str = "NAS-C"; break;
        case  GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1983 : datum_str = "NAR-C"; break;
        case  GEOTIFF_DATUM_WGS72 : datum_str = "W72"; break;
        case  GEOTIFF_DATUM_WGS84 : datum_str = "W84"; break;
        case  GEOTIFF_DATUME_AIRY_1830 : datum_str = "OGB-M"; break;  // best guess
        case  GEOTIFF_DATUME_AIRY_MODIFIED1849 : datum_str = "IRL"; break; // best guess
        case  GEOTIFF_DATUME_AUSTRALIAN_NATIONAL_SPHEROID : datum_str = "AUA"; break;  // best guess
        case  GEOTIFF_DATUME_BESSEL_1841 : datum_str = "TOY-M"; break;  // best guess
        case  GEOTIFF_DATUME_BESSEL_MODIFIED : datum_str = "TOY-M"; break;  // best guess
        case  GEOTIFF_DATUME_BESSEL_NAMIBIA : datum_str = "SCK"; break;  // best guess
        case  GEOTIFF_DATUME_CLARKE_1858 : datum_str = "NAS-C"; break;  // best guess
        case  GEOTIFF_DATUME_CLARKE_1866 : datum_str = "NAS-C"; break;  // best guess
        case  GEOTIFF_DATUME_CLARKE_1866_MICHIGAN : datum_str = "NAS-A"; break;  // best guess
        case  GEOTIFF_DATUME_CLARKE_1880_BENOIT : datum_str = "ADI-M"; break;  // best tuess
        case  GEOTIFF_DATUME_CLARKE_1880_IGN : datum_str = "IBE"; break;  // best guess
        case  GEOTIFF_DATUME_CLARKE_1880_RGS : datum_str = "W84"; break;
        case  GEOTIFF_DATUME_CLARKE_1880_ARC : datum_str = "ARF-M"; break;  // best guess
        case  GEOTIFF_DATUME_CLARKE_1880_SGA_1922 : datum_str = "ARF-M"; break;  // best guess
        case  GEOTIFF_DATUME_EVEREST_1830_1937_ADJUSTMENT : datum_str = "IND-B"; break;  // best guess
        case  GEOTIFF_DATUME_EVEREST_1830_1967_DEFINITION : datum_str = "IND-I"; break;  // best guess
        case  GEOTIFF_DATUME_EVEREST1830_1975_DEFINITION : datum_str = "IND-I"; break;  // best guess
        case  GEOTIFF_DATUME_EVEREST_1830_MODIFIED : datum_str = "IND-I"; break;  // best guess
        case  GEOTIFF_DATUME_GRS_1980 : datum_str = "NAR-C"; break;  // best guess
        case  GEOTIFF_DATUME_HELMERT_1906 : datum_str = "OEG"; break;  // best guess
        case  GEOTIFF_DATUME_INDONESIAN_NATIONAL_SPHEROID : datum_str = "IDN"; break;
        case  GEOTIFF_DATUME_INTERNATIONAL_1924 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_INTERNATIONAL_1967 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_KRASSOWSKY_1960 : datum_str = "PUK"; break;  // best guess
        case  GEOTIFF_DATUME_NWL9D : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_NWL10D : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_PLESSIS_1817 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_STRUVE_1860 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_WAR_OFFICE : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_WGS_84 : datum_str = "W84"; break;
        case  GEOTIFF_DATUME_GEM10C : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_OSU86F : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_OSU91A : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_CLARKE_1880 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUME_SPHERE : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_ADINDAN : datum_str = "ADI-M"; break;
        case  GEOTIFF_DATUM_AUSTRALIAN_GEODETIC_DATUM_1966 : datum_str = "AUA"; break;
        case  GEOTIFF_DATUM_AUSTRALIAN_GEODETIC_DATUM_1984 : datum_str = "AUG"; break;
        case  GEOTIFF_DATUM_AIN_EL_ABD_1970 : datum_str = "AIN-A"; break;
        case  GEOTIFF_DATUM_AFGOOYE : datum_str = "AFG"; break;
        case  GEOTIFF_DATUM_AGADEZ : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_LISBON : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_ARATU : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_ARC_1950 : datum_str = "ARF-M"; break;
        case  GEOTIFF_DATUM_ARC_1960 : datum_str = "ARS-M"; break;
        case  GEOTIFF_DATUM_BATAVIA : datum_str = "BAT"; break;
        case  GEOTIFF_DATUM_BARBADOS : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_BEDUARAM : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_BEIJING_1954 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_RESEAU_NATIONAL_BELGE_1950 : datum_str = "W84"; break;
        case  GEOTIFF_DATUM_BERMUDA_1957 : datum_str = "BER"; break;
        case  GEOTIFF_DATUM_BERN_1898 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_BOGOTA : datum_str = "BOO"; break;
        case  GEOTIFF_DATUM_BUKIT_RIMPAH : datum_str = "BUR"; break;
        case  GEOTIFF_DATUM_CAMACUPA : datum_str = "W84"; break;
        case  GEOTIFF_DATUM_CAMPO_INCHAUSPE : datum_str = "CAI"; break;
        case  GEOTIFF_DATUM_CAPE : datum_str = "CAP"; break;
        case  GEOTIFF_DATUM_CARTHAGE : datum_str = "CGE"; break;
        case  GEOTIFF_DATUM_CHUA : datum_str = "CHU"; break;
        case  GEOTIFF_DATUM_CORREGO_ALEGRE : datum_str = "COA"; break;
        case  GEOTIFF_DATUM_COTE_D_IVOIRE : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_DEIR_EZ_ZOR : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_DOUALA : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_EGYPT_1907 : datum_str = "OEG"; break;
        case  GEOTIFF_DATUM_EUROPEAN_DATUM_1950 : datum_str = "EUR"; break;
        case  GEOTIFF_DATUM_EUROPEAN_DATUM_1987 : datum_str = "EUS"; break;  // closest
        case  GEOTIFF_DATUM_FAHUD : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_GANDAJIKA_1970 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_GAROUA : datum_str = "W84"; break;
        case  GEOTIFF_DATUM_GUYANE_FRANCAISE : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_HU_TZU_SHAN : datum_str = "HTN"; break;
        case  GEOTIFF_DATUM_HUNGARIAN_DATUM_1972 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_INDONESIAN_DATUM_1974 : datum_str = "IDN"; break;
        case  GEOTIFF_DATUM_INDIAN_1954 : datum_str = "INF-A"; break;
        case  GEOTIFF_DATUM_INDIAN_1975 : datum_str = "INH-A"; break;
        case  GEOTIFF_DATUM_JAMAICA_1875 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_JAMAICA_1969 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_KALIANPUR : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_KANDAWALA : datum_str = "KAN"; break;
        case  GEOTIFF_DATUM_KERTAU : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_KUWAIT_OIL_COMPANY : datum_str = "KEA"; break;
        case  GEOTIFF_DATUM_LA_CANOA : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_PROVISIONAL_S_AMERICAN_DATUM_1956 : datum_str = "PRP-M"; break;
        case  GEOTIFF_DATUM_LAKE : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_LEIGON : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_LIBERIA_1964 : datum_str = "LIB"; break;
        case  GEOTIFF_DATUM_LOME : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_LUZON_1911 : datum_str = "LUZ-A"; break;
        case  GEOTIFF_DATUM_HITO_XVIII_1963 : datum_str = "HIT"; break;
        case  GEOTIFF_DATUM_HERAT_NORTH : datum_str = "HEN"; break;
        case  GEOTIFF_DATUM_MAHE_1971 : datum_str = "MIK"; break;
        case  GEOTIFF_DATUM_MAKASSAR : datum_str = "MAS"; break;
        case  GEOTIFF_DATUM_EUROPEAN_REFERENCE_SYSTEM_1989 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_MALONGO_1987 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_MANOCA : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_MERCHICH : datum_str = "MER"; break;
        case  GEOTIFF_DATUM_MASSAWA : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_MINNA : datum_str = "MIN-A"; break;
        case  GEOTIFF_DATUM_MHAST : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_MONTE_MARIO : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_M_PORALOKO : datum_str = "MPO"; break;
        case  GEOTIFF_DATUM_NAD_MICHIGAN : datum_str = "NAS-A"; break;  // best guess
        case  GEOTIFF_DATUM_NAHRWAN_1967 : datum_str = "NAH-C"; break;  // best guess
        case  GEOTIFF_DATUM_NAPARIMA_1972 : datum_str = "NAP"; break;
        case  GEOTIFF_DATUM_NEW_ZEALAND_GEODETIC_DATUM_1949 : datum_str = "GEO"; break;
        case  GEOTIFF_DATUM_NGO_1948 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_DATUM_73 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_NOUVELLE_TRIANGULATION_FRANCAISE : datum_str = "W84"; break;
        case  GEOTIFF_DATUM_NSWC_9Z_2 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_OSGB_1936 : datum_str = "OGB-M"; break;
        case  GEOTIFF_DATUM_OSGB_1970_SN : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_OS_SN_1980 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_PADANG_1884 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_PALESTINE_1923 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_POINTE_NOIRE : datum_str = "PTN"; break;
        case  GEOTIFF_DATUM_GEOCENTRIC_DATUM_OF_AUSTRALIA_1994 : datum_str = "AUG"; break;
        case  GEOTIFF_DATUM_PULKOVO_1942 : datum_str = "PUK"; break;
        case  GEOTIFF_DATUM_QATAR : datum_str = "QAT"; break;
        case  GEOTIFF_DATUM_QATAR_1948 : datum_str = "QAT"; break;
        case  GEOTIFF_DATUM_QORNOQ : datum_str = "QUO"; break;
        case  GEOTIFF_DATUM_LOMA_QUINTANA : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_AMERSFOORT : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_RT38 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_SOUTH_AMERICAN_DATUM_1969 : datum_str = "SAN-M"; break;
        case  GEOTIFF_DATUM_SAPPER_HILL_1943 : datum_str = "SAP"; break;
        case  GEOTIFF_DATUM_SCHWARZECK : datum_str = "SCK"; break;
        case  GEOTIFF_DATUM_SEGORA : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_SERINDUNG : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_SUDAN : datum_str = "ADI-B"; break;  // best guess
        case  GEOTIFF_DATUM_TANANARIVE_1925 : datum_str = "TAN"; break;
        case  GEOTIFF_DATUM_TIMBALAI_1948 : datum_str = "TIL"; break;
        case  GEOTIFF_DATUM_TM65 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_TM75 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_TOKYO : datum_str = "TOY-M"; break;
        case  GEOTIFF_DATUM_TRINIDAD_1903 : datum_str = "NAP"; break; // best guess
        case  GEOTIFF_DATUM_TRUCIAL_COAST_1948 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_VOIROL_1875 : datum_str = "VOI"; break;
        case  GEOTIFF_DATUM_VOIROL_UNIFIE_1960 : datum_str = "VOR"; break;
        case  GEOTIFF_DATUM_BERN_1938 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_NORD_SAHARA_1959 : datum_str = "NSD"; break;
        case  GEOTIFF_DATUM_STOCKHOLM_1938 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_YACARE : datum_str = "YAC"; break;
        case  GEOTIFF_DATUM_YOFF : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_ZANDERIJ : datum_str = "ZAN"; break;
        case  GEOTIFF_DATUM_MILITAR_GEOGRAPHISCHE_INSTITUT : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_RESEAU_NATIONAL_BELGE_1972 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_DEUTSCHE_HAUPTDREIECKSNETZ : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_CONAKRY_1905 : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_WGS72_TRANSIT_BROADCAST_EPHEMERIS : datum_str = "W72"; break;  // best guess
        case  GEOTIFF_DATUM_ANCIENNE_TRIANGULATION_FRANCAISE : datum_str = "W84"; rslt = FAILURE; break;
        case  GEOTIFF_DATUM_NORD_DE_GUERRE : datum_str = "W84"; rslt = FAILURE; break;
		default: datum_str = "W84"; rslt = FAILURE; break;
	}

	return rslt;
}
// end of geotiff_datum_to_string

// ****************************************************************
// *************************************************************
// *************************************************************

// max_width and max_height are the buffer size
// width and height are the size of the actual thumbnail which might be smaller in one dimension

//int CGeoTiff::get_thumbnail(int max_width, int max_height, int *width, int *height, BYTE *red, BYTE *grn, BYTE *blu, CString &err_msg)
int CGeoTiff::get_thumbnail(int max_width, int max_height, int *width, int *height, BYTE *img,
							CString &err_msg, IImageLibCallback *callback)
{
	BYTE *bred, *bgrn, *bblu;
	BYTE *rimg;
	double factor, hscale, vscale, scale, mscale, percent;
	int bwidth, bheight, size, off, off2, x, y, k, row, col, pos, bpos, rslt;
	int r, g, b;
	CWnd *wnd;
	CUtil util;

	if (m_image_is_tiled)
	{
		rslt = get_tiled_thumbnail(max_width, max_height, width, height, img, err_msg, callback);
		return rslt;
	}

	wnd = AfxGetMainWnd();


	m_buf_end_row = -1;

	// find the factor
	hscale = (double) max_width / (double) m_image_width;
	vscale = (double) max_height / (double) m_image_length;
	scale = hscale;
	if (vscale < scale)
		scale = vscale;

	*width = (int) ((double) m_image_width * scale);
	*height = (int) ((double) m_image_length * scale);
	if (scale < 1.0)
	{
		factor = 1.0;
		while (factor > scale)
			factor /= 2.0;
		factor *= 2.0;
		mscale = factor / scale;
		bwidth = (int) ((double) m_image_width * factor);
		bheight = (int) ((double) m_image_length * factor);
		size = bwidth * bheight;
		bred = (BYTE*) malloc(size);
		bgrn = (BYTE*) malloc(size);
		bblu = (BYTE*) malloc(size);
		rimg = (BYTE*) malloc(m_image_width * 3);

		if (!bred || !bgrn || !bblu || !rimg)
			goto FAIL;

		off = (int) (1.0 / factor);
		off2 = off / 2;
		pos = 0;
		for (y=0; y<bheight; y++)
		{
			row = y * off;
			percent = 100.0 * (double) y / (double) bheight;
			CString label = "Creating Overview File";
			if (!util.send_user_update_and_continue(callback, percent, label))
			{
				err_msg = "Operation canceled by user";
				goto FAIL;
			}
			rslt = get_bufferred_rgb_row(row, rimg, err_msg);
			pos = y * bwidth;
			for (x=0; x<bwidth; x++)
			{
				col = x * off;
				if ((col < off2) || (col >= (int) m_image_width - off2) || (off2 < 1))
				{
					bred[pos] = rimg[col*3+0];
					bgrn[pos] = rimg[col*3+1];
					bblu[pos] = rimg[col*3+2];
				}
				else
				{
					r = 0;
					for (k=-off2; k<off2; k++)
						r += rimg[(col+k)*3+0];
					r /= off;
					g = 0;
					for (k=-off2; k<off2; k++)
						g += rimg[(col+k)*3+1];
					g /= off;
					b = 0;
					for (k=-off2; k<off2; k++)
						b += rimg[(col+k)*3+2];
					b /= off;
					bred[pos] = (BYTE) r;
					bgrn[pos] = (BYTE) g;
					bblu[pos] = (BYTE) b;
				}
				pos++;
			}
		}
		pos = 0;
		// map the buffer into the thumbnail
		for (y=0; y< *height; y++)
		{
			row = (int) ((double) y * mscale);
			for (x=0; x < *width; x++)
			{
				col = (int) ((double) x * mscale);
				bpos = (row * bwidth) + col;
//				red[pos] = bred[bpos];
//				grn[pos] = bgrn[bpos];
//				blu[pos] = bblu[bpos];
				img[pos*3+0] = bred[bpos];
				img[pos*3+1] = bgrn[bpos];
				img[pos*3+2] = bblu[bpos];
				pos++;
			}
		}
		free(bred);
		free(bgrn);
		free(bblu);
		free(rimg);
	}
	else
		return FAILURE;  // for not, implement super thumbnail later

	util.clear_callback(callback);
	return SUCCESS;

FAIL:
	util.clear_callback(callback);
	if (bred)
		free(bred);
	if (bgrn)
		free(bgrn);
	if (bblu)
		free(bblu);
	if (rimg)
		free(rimg);

	return FAILURE;
}
// end of get_thumbnail

// *************************************************************
// *************************************************************

// max_width and max_height are the buffer size
// width and height are the size of the actual thumbnail which might be smaller in one dimension

//int CGeoTiff::get_tiled_thumbnail(int max_width, int max_height, int *width, int *height, BYTE *red, BYTE *grn, BYTE *blu, CString &err_msg)
int CGeoTiff::get_tiled_thumbnail(int max_width, int max_height, int *width, int *height, BYTE *img, CString &err_msg, IImageLibCallback *callback)
{
	BYTE *bred, *bgrn, *bblu;
	BYTE *tred, *tgrn, *tblu;
	double factor, hscale, vscale, scale, mscale, percent;
	int bwidth, bheight, size, x, y, row, col, pos, bpos, rslt;
	int tile_width, tile_height, tile_size, tile_cnt_x, tile_cnt_y, chunk_size;
	int tile_row, tile_col, tile, srcpos, destpos, imgwidth, imgheight, samp, samp2;
	int scntx, scnty, buf_tile_width, buf_tile_height, tile_cnt, bsize, r, g, b, k;
	CUtil util;

	bred = bgrn = bblu = tred = tgrn = tblu = NULL;
	rslt = SUCCESS;

	tile_width = m_tile_width;
	tile_height = m_tile_length;
	tile_size = tile_width * tile_height;
	tile_cnt_x = m_num_tiles_across;
	tile_cnt_y = m_num_tiles_down;
	tile_cnt = tile_cnt_x * tile_cnt_y;
	imgwidth = m_image_width;
	imgheight = m_image_length;

	chunk_size = tile_size * tile_cnt_x;

	// find the factor
	hscale = (double) max_width / (double) m_image_width;
	vscale = (double) max_height / (double) m_image_length;
	scale = hscale;
	if (vscale < scale)
		scale = vscale;

	*width = (int) ((double) m_image_width * scale);
	*height = (int) ((double) m_image_length * scale);
	if (scale < 1.0)
	{
		factor = 1.0;
		while (factor > scale)
			factor /= 2.0;
		factor *= 2.0;
		samp = (int) (1.0 / factor);
		samp2 = samp / 2;
		scntx = imgwidth / samp;
		scnty = imgheight / samp;
		mscale = factor / scale;
		bwidth = (int) ((double) m_image_width * factor);
		bheight = (int) ((double) m_image_length * factor);
		bsize = bwidth * bheight;
		buf_tile_width = (int) ((double) tile_width * factor);
		buf_tile_height = (int) ((double) tile_width * factor);
		size = bwidth * bheight;
		bred = (BYTE*) malloc(size);
		bgrn = (BYTE*) malloc(size);
		bblu = (BYTE*) malloc(size);
		tred = (BYTE*) malloc(tile_size);
		tgrn = (BYTE*) malloc(tile_size);
		tblu = (BYTE*) malloc(tile_size);

		if (!bred || !bgrn || !bblu || !tred || !tgrn || !tblu)
		{
			ASSERT(0);
			rslt = FAILURE;
			err_msg = "Error Allocating Memory";
			goto END;
		}

		for (tile_row=0; tile_row<tile_cnt_y; tile_row++)
		{
			for (tile_col=0; tile_col<tile_cnt_x; tile_col++)
			{
				tile = m_num_tiles_across * tile_row + tile_col;
				percent = 100.0 * (double) tile / (double) tile_cnt;
				CString label = "Creating Overview File";
				if (!util.send_user_update_and_continue(callback, percent, label))
				{
					rslt = FAILURE;
					err_msg = "Operation canceled by user";
					goto END;
				}

				rslt = get_tile_as_rgb_image( tile, tred, tgrn, tblu);
				if (rslt == FAILURE)
					return rslt;
				srcpos = 0;
				for (y=0; y<buf_tile_height; y++)
				{
					if (((tile_row * buf_tile_height) + y) >= bheight)
						continue;
					for (x=0; x<buf_tile_width; x++)
					{
						if (((tile_col * buf_tile_width) + x) >= bwidth)
							continue;
						destpos = (((tile_row * buf_tile_height) + y) * bwidth) + (tile_col * buf_tile_width) + x;
						ASSERT(destpos < bsize);
						srcpos = (y * tile_width * samp) + (x * samp);
//						bred[destpos] = tred[srcpos];
//						bgrn[destpos] = tgrn[srcpos];
//						bblu[destpos] = tblu[srcpos];
						if ((srcpos < samp2) || (srcpos >= (int) m_image_width - samp2))
						{
							bred[destpos] = tred[srcpos];
							bgrn[destpos] = tgrn[srcpos];
							bblu[destpos] = tblu[srcpos];
						}
						else
						{
							r = 0;
							for (k=-samp2; k<samp2; k++)
								r += tred[srcpos+k];
							r /= samp;
							g = 0;
							for (k=-samp2; k<samp2; k++)
								g += tgrn[srcpos+k];
							g /= samp;
							b = 0;
							for (k=-samp2; k<samp2; k++)
								b += tblu[srcpos+k];
							b /= samp;
							bred[destpos] = (BYTE) r;
							bgrn[destpos] = (BYTE) g;
							bblu[destpos] = (BYTE) b;
						}
					}
				}
			}
		}

/*
// test code only, make a jpeg of the intermediate image
{
	CString msg;
	CJpeg jpeg;
	BYTE *img = (BYTE*) malloc(bsize*3);
	pos = 0;
	for (y=0; y<bsize; y++)
	{
		img[pos+0] = bred[y];
		img[pos+1] = bred[y];
		img[pos+2] = bred[y];
		pos += 3;
	}
	CString name = m_image->m_filename.Left(m_filename.GetLength()-6);
	name += "_tn.jpg";
	rslt = jpeg.write_jpeg_file( name, bwidth, bheight, img, 75, msg);
}
*/
		pos = 0;
		// map the buffer into the thumbnail
		for (y=0; y< *height; y++)
		{
			row = (int) ((double) y * mscale);
			for (x=0; x < *width; x++)
			{
				col = (int) ((double) x * mscale);
				bpos = (row * bwidth) + col;
				pos = (y * (*width)) + x;
				img[pos*3+0] = bred[bpos];
				img[pos*3+1] = bgrn[bpos];
				img[pos*3+2] = bblu[bpos];
			}
		}
	}
	else
	{
		err_msg = "Thumbnails are only valid when the are smaller than the original image";
		rslt = FAILURE;
		goto END;  // for not, implement super thumbnail later
	}

END:
	util.clear_callback(callback);
	if (bred)
		free(bred);
	if (bgrn)
		free(bgrn);
	if (bblu)
		free(bblu);
	if (tred)
		free(tred);
	if (tgrn)
		free(tgrn);
	if (bblu)
		free(tblu);

	return rslt;
}
// end of get_tiled_thumbnail

// *************************************************************
// *************************************************************

#define BUF_SIZE 12000000

int CGeoTiff::get_bufferred_rgb_row(int row, BYTE *img, CString &err_msg)
{
	int num_rows, size, rslt, ndx_row, buf_pos;
	int row_bytes;

	ndx_row = row - m_buf_start_row;
	buf_pos = ndx_row * m_image_width * 3;
	if ((row >= m_buf_start_row) && (row <= m_buf_end_row))
	{
		memcpy(img, m_buf_img + buf_pos, m_image_width * 3);
		return SUCCESS;
	}

	// load buffer
	if (m_buf_img)
	{
		free(m_buf_img);
		m_buf_img = NULL;
	}

      switch( m_image_type )
      {
         case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
            ASSERT( FALSE );
            return FAILURE;

         case GEOTIFF_IMAGE_TYPE_1BIT:
			 row_bytes = m_image_width / 8;
            break;

         case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
			 if (m_samples_per_pixel == 3)
				 row_bytes = m_image_width * 3;
			 else
				 row_bytes = m_image_width;
            break;

         case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
			 row_bytes = m_image_width;
            break;

         case GEOTIFF_IMAGE_TYPE_256_COLOR:
				row_bytes = m_image_width;
            break;

         case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
			row_bytes = m_image_width * 3;
            break;

         default:                           // should always be one of above
            ASSERT( FALSE );
            return FAILURE;
      }

	if (row_bytes < 1)
	{
		ASSERT(0);
		return FAILURE;
	}

	num_rows = BUF_SIZE / row_bytes;
	m_buf_start_row = row;
	m_buf_end_row = row + num_rows - 1;
	if (m_buf_end_row >= (int) m_image_length)
	{
		m_buf_end_row = m_image_length - 1;
		num_rows = m_buf_end_row - m_buf_start_row + 1;
	}

	size = row_bytes * num_rows;
	m_buf_img = (BYTE*) malloc(m_image_width * num_rows * 3);
	if (m_buf_img == NULL)
	{
		return FAILURE;
	}

	rslt = get_rgb_subblock(m_buf_start_row, num_rows, m_buf_img, err_msg);

	return SUCCESS;
}
// end of get_bufferred_rgb_row

// *************************************************************
// *************************************************************

int CGeoTiff::get_rgb_subblock(int starty, int height, BYTE *img, CString &err_msg)
{
	int size, rslt;
	int min_hpix, max_hpix, min_vpix, max_vpix;

	rslt = SUCCESS;

	size = m_image_width * height;

	min_hpix = 0;
	max_hpix = m_image_width -1;
	min_vpix = starty;
	max_vpix = starty + height - 1;

	rslt = get_rgb_subimage( min_hpix, min_vpix, m_image_width, height, img, NULL);
	if (rslt != SUCCESS)
		err_msg = "Error reading image file";

	return rslt;
}
// end of get_rgb_subblock

// *************************************************************
// ****************************************************************

int CGeoTiff::get_tiff_tags( CString & tagstr, int *err_code, CString & error_msg )
{
	CString tstr;
	CString document_name, image_description, image_size, image_format, scanner_make_and_model;
	CString image_resolution, software, creation_date_and_time, creator, copyright_notice;
	int rslt;

	rslt = get_documentation_strings( document_name, image_description, image_size, image_format,
										scanner_make_and_model, image_resolution, software,
										creation_date_and_time, creator, copyright_notice );
	if (rslt != SUCCESS)
	{
		*err_code = 1;
		error_msg = "Error getting documentation string";
		return FAILURE;
	}

	tagstr = "";
	if (m_photometric_interpretation > 0)
	{
		tstr.Format("PhotometricInterpretation: %d", m_photometric_interpretation);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_compression_scheme > 0)
	{
		tstr.Format("Compression: %d", m_compression_scheme);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_image_width > 0)
	{
		tstr.Format("ImageWidth: %d", m_image_width);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_image_length > 0)
	{
		tstr.Format("ImageLength: %d", m_image_length);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_resolution_unit > 0)
	{
		tstr.Format("ResolutionUnit: %d", m_resolution_unit);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_x_resolution > 0)
	{
		tstr.Format("XResolution: %f", m_x_resolution);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_y_resolution > 0)
	{
		tstr.Format("YResolution: %f", m_y_resolution);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_bits_per_sample > 0)
	{
		tstr.Format("BitsPerSample: %d", m_bits_per_sample);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (creator.GetLength() > 0)
	{
		tstr.Format("Artist: %s", (LPCSTR)creator );
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (copyright_notice.GetLength() > 0)
	{
		tstr.Format("Copyright: %s", (LPCSTR)copyright_notice );
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (creation_date_and_time.GetLength() > 0)
	{
		tstr.Format("DateTime: %s", (LPCSTR)creation_date_and_time );
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (scanner_make_and_model.GetLength() > 0)
	{
		tstr.Format("Make: %s", (LPCSTR)scanner_make_and_model );
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (software.GetLength() > 0)
	{
		tstr.Format("Software: %s", (LPCSTR)software );
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (document_name.GetLength() > 0)
	{
		tstr.Format("DocumentName: %s", (LPCSTR)document_name );
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (image_description.GetLength() > 0)
	{
		tstr.Format("ImageDescription: %s", (LPCSTR)image_description );
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_model_tie_point_present)
	{
		tstr.Format("ModelTiepointX: %f", m_geodata.m_model_tie_point_x[0]);
		tagstr += tstr;
		tagstr += "\r\n";
		tstr.Format("ModelTiepointY: %f", m_geodata.m_model_tie_point_y[0]);
		tagstr += tstr;
		tagstr += "\r\n";
		tstr.Format("ModelTiepointZ: %f", m_geodata.m_model_tie_point_z[0]);
		tagstr += tstr;
		tagstr += "\r\n";
		tstr.Format("ModelTiepointXpix: %f", m_geodata.m_model_tie_point_hpix[0]);
		tagstr += tstr;
		tagstr += "\r\n";
		tstr.Format("ModelTiepointYpix: %f", m_geodata.m_model_tie_point_vpix[0]);
		tagstr += tstr;
		tagstr += "\r\n";
		tstr.Format("ModelTiepointZpix: %f", m_geodata.m_model_tie_point_zpix[0]);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_model_pixel_scale_present)
	{
		tstr.Format("ModelPixelScaleX: %f", m_geodata.m_dLScaleX );
		tagstr += tstr;
		tagstr += "\r\n";
		tstr.Format("ModelPixelScaleY: %f", m_geodata.m_dPScaleY );
		tagstr += tstr;
		tagstr += "\r\n";
		tstr.Format("ModelPixelScaleZ: %f", m_geodata.m_dHScaleZ );
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_gt_model_type_present)
	{
		tstr.Format("GTModelTypeGeoKey: %d", m_geodata.m_gt_model_type);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_gt_raster_type_present)
	{
		tstr.Format("GTRasterTypeGeoKey: %d", m_geodata.m_gt_raster_type);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_gt_citation_present)
	{
		tstr.Format("GTCitationGeoKey: %s", (LPCSTR)m_geodata.m_gt_citation );
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_geographic_type_present)
	{
		tstr.Format("GeographicTypeGeoKey: %d", m_geodata.m_geographic_type);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_geog_geodetic_datum_present)
	{
		tstr.Format("GeogGeodeticDatumGeoKey: %d", m_geodata.m_geog_geodetic_datum);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_geog_prime_meridian_present)
	{
		tstr.Format("GeogPrimeMeridianGeoKey: %d", m_geodata.m_geog_prime_meridian);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_geog_prime_meridian_long_present)
	{
		tstr.Format("GeogPrimeMeridianLongGeoKey: %f", m_geodata.m_geog_prime_meridian_long);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geog_linear_units_present)
	{
		tstr.Format("GeogLinearUnitsGeoKey: %d", m_linear_units);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geog_linear_unit_size_present)
	{
		tstr.Format("GeogLinearUnitSizeGeoKey : %f", m_geog_linear_unit_size);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geog_angular_units_present)
	{
		tstr.Format("GeogAngularUnitsGeoKey: %d", m_geog_angular_units);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geog_angular_unit_size_present)
	{
		tstr.Format("GeogAngularUnitSizeGeoKey: %d", m_geog_angular_unit_size);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geog_azimuth_units_present)
	{
		tstr.Format("GeogAzimuthUnitsGeoKey: %d", m_geog_azimuth_units);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_projected_cs_type_present)
	{
		tstr.Format("ProjectedCSTypeGeoKey: %d", m_geodata.m_projected_cs_type);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_pcs_citation_present)
	{
		tstr.Format("PCSCitationGeoKey: %s", (LPCSTR)m_geodata.m_pcs_citation );
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_projection_present)
	{
		tstr.Format("ProjectionGeoKey: %d", m_geodata.m_projection);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_coord_trans_present)
	{
		tstr.Format("ProjCoordTransGeoKey: %d", m_geodata.m_proj_coord_trans);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_geog_ellipsoid_present)
	{
		tstr.Format("GeogEllipsoidGeoKey: %d", m_geodata.m_geog_ellipsoid);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_geog_semi_major_axis_present)
	{
		tstr.Format("GeogSemiMajorAxisGeoKey: %f", m_geodata.m_geog_semi_major_axis);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_geog_semi_minor_axis_present)
	{
		tstr.Format("GeogSemiMinorAxisGeoKey: %f", m_geodata.m_geog_semi_minor_axis);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_geog_inv_flattening_present)
	{
		tstr.Format("GeogInvFlatteningGeoKey: %f", m_geodata.m_geog_inv_flattening);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_std_parallel_1_present)
	{
		tstr.Format("ProjStdParallel1GeoKey: %f", m_geodata.m_proj_std_parallel_1);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_std_parallel_2_present)
	{
		tstr.Format("ProjStdParallel2GeoKey: %f", m_geodata.m_proj_std_parallel_2);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_nat_origin_long_present)
	{
		tstr.Format("ProjNatOriginLongGeoKey: %f", m_geodata.m_proj_nat_origin_long);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_nat_origin_lat_present)
	{
		tstr.Format("ProjNatOriginLatGeoKey: %f", m_geodata.m_proj_nat_origin_lat);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_false_easting_present)
	{
		tstr.Format("ProjFalseEastingGeoKey: %f", m_geodata.m_proj_false_easting);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_false_northing_present)
	{
		tstr.Format("ProjFalseNorthingGeoKey: %f", m_geodata.m_proj_false_northing);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_false_origin_long_present)
	{
		tstr.Format("ProjFalseOriginLongGeoKey: %f", m_geodata.m_proj_false_origin_long);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_false_origin_lat_present)
	{
		tstr.Format("ProjFalseOriginLatGeoKey: %f", m_geodata.m_proj_false_origin_lat);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_false_origin_easting_present)
	{
		tstr.Format("ProjFalseOriginEastingGeoKey: %f", m_geodata.m_proj_false_origin_easting);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_false_origin_northing_present)
	{
		tstr.Format("ProjFalseOriginNorthingGeoKey: %f", m_geodata.m_proj_false_origin_northing);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_center_long_present)
	{
		tstr.Format("ProjCenterLongGeoKey: %f", m_geodata.m_proj_center_long);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_center_lat_present)
	{
		tstr.Format("ProjCenterLatGeoKey: %f", m_geodata.m_proj_center_lat);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_center_easting_present)
	{
		tstr.Format("ProjCenterEastingGeoKey: %f", m_geodata.m_proj_center_easting);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_center_northing_present)
	{
		tstr.Format("ProjFalseOriginNorthingGeoKey: %f", m_geodata.m_proj_center_northing);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_scale_at_nat_origin_present)
	{
		tstr.Format("ProjScaleAtNatOriginGeoKey: %f", m_geodata.m_proj_scale_at_nat_origin);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_scale_at_center_present)
	{
		tstr.Format("ProjScaleAtCenterGeoKey: %f", m_geodata.m_proj_scale_at_center);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_azimuth_angle_present)
	{
		tstr.Format("ProjAzimuthAngleGeoKey: %f", m_geodata.m_proj_azimuth_angle);
		tagstr += tstr;
		tagstr += "\r\n";
	}
	if (m_geodata.m_proj_straight_vert_pole_long_present)
	{
		tstr.Format("ProjStraightVertPoleLongGeoKey: %d", m_geodata.m_proj_straight_vert_pole_long);
		tagstr += tstr;
		tagstr += "\r\n";
	}


	return SUCCESS;
}
// end of get_tiff_tags

// *************************************************************
// *************************************************************

int CGeoTiff::get_subsampled_rgb_subimage( double factor,
										int image_offset_x, int image_offset_y, // pixel offsets into the image
										int width, int height,						  // size of the image to be return in arrays
										BYTE *img, CString & error_msg, IImageLibCallback *callback )
{
	// this function returns the pixel colors in the argument arrays for the
	// specified subimage, subsampled according to the sampling argument
	// the sampling argument indicates the fraction of the image's rows and
	// columns that are to be retained in the subsampled image. a sampling of 3
	// indicates that every third row and column should be retained
	// returns SUCCESS or FAILURE

	int row, col, index, icol, irow, iwidth, iheight;
	unsigned char *img_row;
	int rslt, sampling, min_hpix, max_hpix, min_vpix, max_vpix;
	int k, n;
   size_t size, needed_memsize, sys_memsize;
	double percent;
	CUtil util;
	BYTE *bufimg;

//	CWnd *wnd = AfxGetMainWnd();

	bufimg = NULL;

	if (factor > 1.0)
	{
		rslt = get_supersampled_rgb_subimage(factor, image_offset_x, image_offset_y, width, height, img, error_msg, callback);
		return rslt;
	}

	if (factor == 1)
	{
		rslt = get_rgb_subimage( image_offset_x, image_offset_y, width, height, img, callback );
		return rslt;
	}

	if (m_image_is_tiled)
	{
		sampling = (int) (1.0 / factor);
		rslt = get_subsampled_tiled_rgb_subimage(sampling, image_offset_x, image_offset_y,
											width, height, img, callback);
		return rslt;
	}

	min_hpix = image_offset_x;
	min_vpix = image_offset_y;
	max_hpix = min_hpix + width - 1;
	max_vpix = min_vpix + height - 1;

	sampling = (int) (1.0 / factor);

	// return failure if subimage is invalid
	if ( image_offset_x < 0 || image_offset_x >= (int) m_image_width )
		return FAILURE;
	if ( image_offset_y < 0 || image_offset_y >= (int) m_image_length )
		return FAILURE;

	// return FAILURE if sampling is less than 1
	if ( sampling < 1 )
		return FAILURE;

	iwidth = width * sampling;
	iheight = height * sampling;
	size = iwidth * iheight;
	needed_memsize = size * 3;

	// find system memory size
	MEMORYSTATUS memstat;
	GlobalMemoryStatus(&memstat);
	sys_memsize = memstat.dwAvailPhys;

	if (needed_memsize < (sys_memsize / 2))
//	if (FALSE)
	{
		bufimg = (BYTE*) malloc(needed_memsize);
		if (bufimg == NULL)
			return FAILURE;
		max_hpix = min_hpix + width * sampling - 1;
		max_vpix = min_vpix + height * sampling - 1;
		rslt = get_rgb_subimage( min_hpix, min_vpix, width * sampling, height * sampling, bufimg, callback);
		if (rslt != SUCCESS)
		{
			free(bufimg);
			return rslt;
		}
		util.remap_image(width, height, img, iwidth, iheight, bufimg);
		free(bufimg);
		return SUCCESS;
	}


	// allocate arrays to hold one row of data
	img_row = NULL;

	iwidth = image_offset_x + (width * sampling);
	if ( iwidth > (int) m_image_width)
		iwidth = m_image_width;
	iheight = image_offset_y + (height * sampling);
	if ( iheight > (int) m_image_length)
		iheight = m_image_length;

	img_row = (unsigned char*)malloc( iwidth * 3);
	if ( img_row == NULL )
		goto FAIL;

	bufimg = (BYTE*) malloc(iwidth*3);

	index = 0;
	for ( row = 0; row < height; row++ )
	{
		percent = 100.0 * (double) row / (double) height;
		CString label = "Loading Image";
		if (!util.send_user_update_and_continue(callback, percent, label))
		{
			error_msg = "Operation canceled by user";
			goto FAIL;
		}

		irow = image_offset_y + (row * sampling);
		if (irow < iheight)
		{
			rslt = get_rgb_subimage(image_offset_x, irow, width * sampling, 1, bufimg, NULL);
			for (k=0; k<iwidth; k++)
			{
				n = k*3;
				img_row[n+0] = bufimg[n+0];
				img_row[n+1] = bufimg[n+1];
				img_row[n+2] = bufimg[n+2];
			}
		}

		for ( col = 0; col < width; col++ )
		{
			icol = col * sampling;
			if ((icol < (iwidth - image_offset_x)) && (irow < iheight))
			{
				img[index*3+0] = img_row[icol*3+0];
				img[index*3+1] = img_row[icol*3+1];
				img[index*3+2] = img_row[icol*3+2];
			}
			else
			{
				img[index*3+0] = 0;
				img[index*3+1] = 0;
				img[index*3+2] = 0;
			}
			index++;
		}
	}

	util.clear_callback(callback);

	// deallocate memory
	free( img_row );
	free(bufimg);

	return SUCCESS;

FAIL:
	// deallocate memory
	if ( img_row != NULL )
		free( img_row );
	if ( bufimg != NULL )
		free( bufimg );

	return FAILURE;
}
// end of get_subsampled_rgb_subimage

// ****************************************************************
// ****************************************************************

int CGeoTiff::get_supersampled_rgb_subimage( double factor,	// the degree of oversampling
									int image_offset_x, int image_offset_y, // pixel offsets into the image
									int width, int height,						  // size of the image to be return in arrays
									BYTE *img, CString & error_msg, IImageLibCallback *callback )
{
	int iwidth, iheight, rslt, size;
	BYTE *img_src;
	int ulx, uly, lrx, lry;
	CUtil util;

	ulx = image_offset_x;
	uly = image_offset_y;
	iwidth = (int) ((double) width / factor)-1;
	if ( iwidth+ulx > (int) m_image_width)
		iwidth = m_image_width - ulx + 1;

	iheight = (int) ((double) height / factor)-1;
	if ( iheight+uly > (int) m_image_length)
		iheight = m_image_length - uly + 1;

	lrx = ulx + iwidth - 1;
	lry = uly + iheight - 1;

	size = iwidth * iheight;

	// allocate arrays to hold one row of data
	img_src = NULL;
	img_src = (BYTE*) malloc( size * 3);
	if ( img_src == NULL )
	{
		ASSERT(0);
		error_msg = "Error Allocating Memory";
		goto FAIL;
	}

	rslt = get_rgb_subimage(ulx, uly, iwidth, iheight, img_src, callback);
//	rslt = get_rgb_subimage(ulx, uly, iwidth, iheight, img_src, &err, error_msg);

	rslt = util.remap_image_interp(width, height, img, iwidth, iheight, img_src);

   // deallocate memory
   free( img_src );

   return SUCCESS;

FAIL:
   // deallocate memory
   if ( img_src != NULL )
	   free( img_src );
   return FAILURE;
}
// end of get_supersampled_rgb_subimage

// ****************************************************************
// ****************************************************************

CString CGeoTiff::get_info_string(void)
{
	CStringArray cstring_array;
	CString tstr, tstr2, txt, info_text;
	INT_PTR k, kmax;

//	print_image_info( cstring_array );
	info_text = get_info();
	print_tags( cstring_array );
	print_geokeys( cstring_array );
	kmax = cstring_array.GetUpperBound( );
//	info_text = "";
	for ( k = 0; k <= kmax; k++ )
	{
		tstr = cstring_array.GetAt( k );
		info_text += tstr;
		info_text += "\r\n";
	}

	return info_text;
}
// end of get_info_string(void)


// ****************************************************************
// *************************************************************

// computes image histogram
// image file must be loaded

int CGeoTiff::compute_histogram(IImageLibCallback *callback)
{
	CString file_name, image_type_description, error_message;
	int k, x, y, rslt, pos;
	int tile_cnt, tile_inc;
	CString tstr;
//	int err;
	BYTE *buf_red, *buf_grn, *buf_blu, *img;
	unsigned int red, grn, blu;
	BYTE bred, bgrn, bblu;
	double percent;
	CWnd *wnd;
	CUtil util;

	wnd = AfxGetMainWnd();

	buf_red = buf_grn = buf_blu = img = NULL;

	// int the frequency arrays
	if (m_image->m_freq_red)
		free(m_image->m_freq_red);
	if (m_image->m_freq_grn)
		free(m_image->m_freq_grn);
	if (m_image->m_freq_blu)
		free(m_image->m_freq_blu);
	m_image->m_freq_red = (unsigned int*) calloc(256, sizeof(unsigned int));
	m_image->m_freq_grn = (unsigned int*) calloc(256, sizeof(unsigned int));
	m_image->m_freq_blu = (unsigned int*) calloc(256, sizeof(unsigned int));
	if (!m_image->m_freq_red || !m_image->m_freq_grn || !m_image->m_freq_blu)
	{
		m_image->m_err_msg = "Memory allocation error";
		goto FAIL;
	}

	// initialize the histogram
	if (m_image->m_histogram)
		free(m_image->m_histogram);
	m_image->m_histogram = (unsigned int*) calloc(32768, sizeof(unsigned int));
	if (m_image->m_histogram == NULL)
	{
		m_image->m_err_msg = "Memory allocation error";
		goto FAIL;
	}

	tile_cnt = m_image->m_tile_cnt_x * m_image->m_tile_cnt_y;
	tile_inc = tile_cnt / 20;
	tile_inc++;
//	if (tile_cnt > 0)
	if (m_image->m_is_tiled)
	{
		int tile_size = m_image->m_tile_size_x * m_image->m_tile_size_y;
		buf_red = (BYTE*) malloc(tile_size);
		buf_grn = (BYTE*) malloc(tile_size);
		buf_blu = (BYTE*) malloc(tile_size);
		if (!buf_red || !buf_grn || !buf_blu)
			goto FAIL;

		for (k=0; k<tile_cnt; k+=tile_inc)
		{
			percent = 100.0 * (double) k / (double) tile_cnt;
			CString label = "Loading Image";
			if (!util.send_user_update_and_continue(callback, percent, label))
			{
				m_new_error_description = "Operation canceled by user";
				goto FAIL;
			}

//			CProgressDisplay::set_progress_bar("Computing Histogram...", percent);
			rslt = get_tile_as_rgb_image(k, buf_red, buf_grn, buf_blu);
			if (rslt != SUCCESS)
			{
				m_new_error_description = "Error reading image file";
				goto FAIL;
			}
			for (x=0; x<tile_size; x++)
			{
				bred = buf_red[x];
				bgrn = buf_grn[x];
				bblu = buf_blu[x];
				if ((bred == 0) && (bgrn == 0) && (bblu == 0))
					continue;
				(m_image->m_freq_red[bred])++;
				(m_image->m_freq_grn[bgrn])++;
				(m_image->m_freq_blu[bblu])++;
				red = (unsigned int) (bred >> 3);
				grn = (unsigned int) (bgrn >> 3);
				blu = (unsigned int) (bblu >> 3);
				pos = (red << 10) + (grn << 5) + blu;
				ASSERT(pos < 32768);
				m_image->m_histogram[pos] += 1;
			}
		}
		free(buf_red);
		free(buf_grn);
		free(buf_blu);
		buf_red = buf_grn = buf_blu = NULL;
	}
	else
	{
		// untiled
		ASSERT(m_image->m_image_width > 0);
		ASSERT(m_image->m_image_height > 0);
		img = (BYTE*) malloc(m_image->m_image_width * 3);
		if (img == NULL)
		{
			m_image->m_err_msg = "Memory allocation error";
			goto FAIL;
		}
		tile_inc = m_image->m_image_height / 20;
		tile_inc++;
		for (y=0; y<m_image->m_image_height; y+=tile_inc)
		{
			percent = 100.0 * (double) y / (double) m_image->m_image_height;
			CString label = "Computing Histogram";
			if (!util.send_user_update_and_continue(callback, percent, label))
			{
				m_new_error_description = "Operation canceled by user";
				goto FAIL;
			}
			ASSERT(m_image->m_image_width > 0);
			rslt = get_rgb_subimage(0, y, m_image->m_image_width, 1, img, callback);

			if (rslt != SUCCESS)
				goto FAIL;

			for (x=0; x<m_image->m_image_width; x++)
			{
				bred = img[(x*3)+0];
				bgrn = img[(x*3)+1];
				bblu = img[(x*3)+2];
				if ((bred == 0) && (bgrn == 0) && (bblu == 0))
					continue;	// do not include pure black in histogram
				(m_image->m_freq_red[bred])++;
				(m_image->m_freq_grn[bgrn])++;
				(m_image->m_freq_blu[bblu])++;
				// clip to 5 bits
				red = (unsigned int) (bred >> 3);
				grn = (unsigned int) (bgrn >> 3);
				blu = (unsigned int) (bblu >> 3);
				pos = (red << 10) + (grn << 5) + blu;
				ASSERT(pos < 32768);
				m_image->m_histogram[pos] += 1;
			}
		}
	}

/*
	// if there is a transparent color, make sure is shows up in the histogram
//	if (data->m_has_transparent_color)
	{
		// clip to 5 bits
		red = GetRValue(data->m_transparent_color);
		grn = GetGValue(data->m_transparent_color);
		blu = GetBValue(data->m_transparent_color);
		red = (int) (red >> 3);
		grn = (int) (grn >> 3);
		blu = (int) (blu >> 3);
		pos = (red << 10) + (grn << 5) + blu;
		(m_image->m_histogram[pos]) += trans_inc;
		(m_image->m_freq_red[red]) += trans_inc;
		(m_image->m_freq_grn[grn]) += trans_inc;
		(m_image->m_freq_blu[blu]) += trans_inc;
	}
*/
	if (buf_red)
		free(buf_red);
	if (buf_grn)
		free(buf_grn);
	if (buf_blu)
		free(buf_blu);
	if (img)
		free(img);

	util.clear_callback(callback);
	return SUCCESS;

FAIL:
	// free any allocated memory
	if (buf_red)
		free(buf_red);
	if (buf_grn)
		free(buf_grn);
	if (buf_blu)
		free(buf_blu);
	if (img)
		free(img);

	if (m_image->m_histogram)
	{
		free(m_image->m_histogram);
		m_image->m_histogram = NULL;
	}
	if (m_image->m_freq_red)
	{
		free(m_image->m_freq_red);
		m_image->m_freq_red = NULL;
	}
	if (m_image->m_freq_grn)
	{
		free(m_image->m_freq_grn);
		m_image->m_freq_grn = NULL;
	}
	if (m_image->m_freq_blu)
	{
		free(m_image->m_freq_blu);
		m_image->m_freq_blu = NULL;
	}

	util.clear_callback(callback);
	return FAILURE;
}
// end of compute_histogram

// *************************************************************
// *************************************************************

// computes image histogram
// image file must be loaded

int CGeoTiff::compute_16bit_value_range(unsigned short *minval, unsigned short *maxval, IImageLibCallback *callback)
{
	CString file_name, image_type_description, error_message;
	int k, x, y, rslt;
	int tile_cnt, tile_inc, min_val, max_val;
	CString tstr;
	unsigned short *img;
//	int err;
	double percent;
	CWnd *wnd;
	CUtil util;

	wnd = AfxGetMainWnd();

	img = NULL;

	min_val = 65535;
	max_val = 0;

	tile_cnt = m_image->m_tile_cnt_x * m_image->m_tile_cnt_y;
	tile_inc = tile_cnt / 20;
	tile_inc++;
//	if (tile_cnt > 0)
	if (m_image->m_is_tiled)
	{
		int tile_size = m_image->m_tile_size_x * m_image->m_tile_size_y;
		img = (unsigned short*) malloc(tile_size* sizeof(unsigned short));
		if (!img)
			goto FAIL;

		for (k=0; k<tile_cnt; k+=tile_inc)
		{
			percent = 100.0 * (double) k / (double) tile_cnt;
			CString label = "Computing Value Range of Image";
			if (!util.send_user_update_and_continue(callback, percent, label))
			{
				m_new_error_description = "Operation canceled by user";
				goto FAIL;
			}

//			CProgressDisplay::set_progress_bar("Computing Histogram...", percent);
			rslt = get_16bit_grayscale_tile_as_16bit( k, img );
			if (rslt != SUCCESS)
			{
				m_new_error_description = "Error reading image file";
				goto FAIL;
			}
			for (x=0; x<tile_size; x++)
			{
				if (min_val > img[x])
					min_val = img[x];
				if (max_val < img[x])
					max_val = img[x];
			}
		}
		free(img);
		img = NULL;
	}
	else
	{
		// untiled
		ASSERT(m_image->m_image_width > 0);
		ASSERT(m_image->m_image_height > 0);
		img = (unsigned short*) malloc(m_image->m_image_width * sizeof(unsigned short));
		if (img == NULL)
		{
			m_image->m_err_msg = "Memory allocation error";
			goto FAIL;
		}
		tile_inc = m_image->m_image_height / 20;
		tile_inc++;
		for (y=0; y<m_image->m_image_height; y+=tile_inc)
		{
			percent = 100.0 * (double) y / (double) m_image->m_image_height;
			CString label = "Computing Value Range of Image";
			if (!util.send_user_update_and_continue(callback, percent, label))
			{
				m_new_error_description = "Operation canceled by user";
				goto FAIL;
			}
			ASSERT(m_image->m_image_width > 0);
			rslt = get_16bit_grayscale_subimage( 0, y, m_image->m_image_width, 1, img, callback);

			if (rslt != SUCCESS)
				goto FAIL;

			for (x=0; x<m_image->m_image_width; x++)
			{
				if (min_val > img[x])
					min_val = img[x];
				if (max_val < img[x])
					max_val = img[x];
			}
		}
	}

	if (img)
		free(img);

	*minval = min_val;
	*maxval = max_val;
	util.clear_callback(callback);
	return SUCCESS;

FAIL:
	// free any allocated memory
	if (img)
		free(img);


	util.clear_callback(callback);
	return FAILURE;
}
// end of compute_16bit_value_range

// *************************************************************
// ****************************************************************

int CGeoTiff::read_fid_file(CString filename)
{
	FILE *f = NULL;
   const int BUF2_LEN = 100;
	char buf[200], buf2[BUF2_LEN];
	CString fname;
	int pos;
	int cs_type;

	pos = filename.ReverseFind('.');
	fname = filename.Left(pos);
	fname += ".fid";

	if ( 0 != fopen_s(&f, fname, "rt")
      || f == NULL )
		return FALSE;

	// read the sentinel string
	fgets(buf, 200, f);
	if (strncmp(buf, "FalconView Image Data", 21))
	{
		fclose(f);
		return FALSE;
	}

	while (!feof(f))
	{
		// read the next line
		fgets(buf, 200, f);
		buf[24] = '\0';
		if (!strncmp(buf, "Projected CS Type: ", 19))
		{
			cs_type = atoi(buf+19);

			m_geodata.m_projected_cs_type = (unsigned short) cs_type;
			m_geodata.m_projected_cs_type_present = TRUE;
			m_geodata.m_gt_model_type = GEOTIFF_MODEL_TYPE_PROJECTED;
			m_geodata.m_gt_model_type_present = TRUE;
		}

		if (!strncmp(buf, "Units: ", 7))
		{
			strcpy_s(buf2, BUF2_LEN, buf+7);
			if (strncmp(buf2, "US FEET", 7))
				m_linear_units = 1;
			if (strncmp(buf2, "METERS", 6))
				m_linear_units = 2;
			if (strncmp(buf2, "INTL FEET", 9))
				m_linear_units = 5;
		}
	}


	// close file
	fclose(f);


	return TRUE;
}
// end of read_fid_file

// ****************************************************************
// ****************************************************************

BOOL CGeoTiff::read_world_file(CString filename, double *lat_per_pix, double *lon_per_pix,
							   double *lat, double *lon, CString &error_msg)
{
	double rot;
	FILE *f = NULL;
	char buf[200];
	CString fname, ext;
	int len, pos;

	len = filename.GetLength();
	pos = filename.ReverseFind('.');
	fname = filename.Left(pos);
	fname += ".tfw";

	if ( 0 != fopen_s(&f, fname, "rt")
	   || f == NULL )
		return FALSE;

	// read size of pixel in x direction
	fgets(buf, 200, f);
	*lon_per_pix = atof(buf);

	// read two rotation terms
	fgets(buf, 200, f);
	rot = atof(buf);
	if (rot != 0.0)
	{
		error_msg = "Cannot display rotated images";
		fclose(f);
		return FALSE;
	}
	fgets(buf, 200, f);
	rot = atof(buf);
	if (rot != 0.0)
	{
		error_msg = "Cannot display rotated images";
		fclose(f);
		return FALSE;
	}

	// read size of pixel in y direction
	fgets(buf, 200, f);
	*lat_per_pix = atof(buf);

	// read lon of upper left corner
	fgets(buf, 200, f);
	*lon = atof(buf);

	// read lat of upper left corner
	fgets(buf, 200, f);
	*lat = atof(buf);

	// close file
	fclose(f);

	return TRUE;
}
// end of read_world_file

// ****************************************************************
// ****************************************************************

int CGeoTiff::get_filled_rgb_subimage(int fill_width, int fill_height,
								   int min_hpix, int min_vpix, int max_hpix, int max_vpix,
								   BYTE *red, BYTE *grn, BYTE *blu)
{
	int tmax_hpix, tmax_vpix, tile_size, j, k, pos, fpos, x, y;
	int width, height, rslt;
	BYTE *img = NULL;

	tmax_hpix = max_hpix;
	tmax_vpix = max_vpix;
	if (tmax_hpix >= (int) m_image_width)
		tmax_hpix = m_image_width - 1;
	if (tmax_vpix >= (int) m_image_length)
		tmax_vpix = m_image_length - 1;

	tile_size = fill_width * fill_height;
	img = (BYTE*) malloc(tile_size * 3);
	if (img == NULL)
		return FAILURE;

	// fill the buffer with black
	for (k=0; k<tile_size; k++)
	{
		red[k] = 0;
		grn[k] = 0;
		blu[k] = 0;
	}

	width = tmax_hpix - min_hpix + 1;
	height = tmax_vpix - min_vpix + 1;
	rslt = get_rgb_subimage(min_hpix, min_vpix, width, height, img, NULL);
	if (rslt == FAILURE)
		goto FAIL;

	for (y=0; y<height; y++)
	{
		j = y * fill_width;
		k = y * width;
		for (x=0; x<width; x++)
		{
			pos = k + x;
			pos *= 3;
			fpos = j + x;
			red[fpos] = img[pos+0];
			grn[fpos] = img[pos+1];
			blu[fpos] = img[pos+2];
		}
	}

	free(img);
	return SUCCESS;

FAIL:
	if (img)
		free(img);
	return FAILURE;
}
// end of get_filled_rgb_subimage

// ****************************************************************
// ****************************************************************


// create a tiled jpeg-encoded tiff file with half the dimensions of the NITF file
//
//  1. Read each tile of the currently loaded image and create jpegs tiles of 1/2 and 1/4 size
//  2. While creating the jpeg tiles keep track of the relative offsets for each tile
//  3. Write the tiff file header
//  4. Calculate the size of the first image so that the first IFD call contain the offset of the
//     second IFD.
//  5. Write the first image tags
//  6. Copy the first image data to the file
//  7. Write the second image tags
//  8. Copy the second image data to the file


int CGeoTiff::create_multi_image_tiff_overview( PINT piErr, CString& csErrorMsg, IImageLibCallback *callback)
{
   static const INT MAX_LEVELS = 16;            // Maybe real big
   static const INT MAX_COPY_BYTES = 0x200000;  // 2MB
	PBYTE tred, tgrn, tblu;
	double percent;
	int rslt;
	int tile_width, tile_height, num_tile_pixels, tile_cnt_x, tile_cnt_y;
	int tile_row, tile_col, tile, imgwidth, imgheight;
	int tile_cnt;
	int i;
	int itemnum, itemcnt, length;
	CUtil util;
	CString jpeg_name, overview_name, base_name, csTempOverviewFilespec;
	int num_tiles;
	FILE *fp, *jfp = NULL, *subfp[ MAX_LEVELS ];
	int num_tags = 13;
	CString image_description, software;
	int tile_index;
   INT cSubImgBytes;
   BytePtr apbSubImg, apbWork;
	PBYTE sub_img;
#ifdef FAST_SUBSAMPLE
   // NOTE (port 2026-07-15): auto_ptr<UINT> -> unique_ptr<UINT[]> (auto_ptr
   // removed in C++17; these all hold UINT[] and were scalar-deleting array
   // memory). .get()/.reset() usage is unchanged; no ownership copies here.
   std::unique_ptr< UINT[] > apuiSubImg;
#endif
	int ifd_offset;
	int num_levels, size, k;
	unsigned short ushort, tag_id, data_type;
	unsigned int uint, count, data_value, data_offset, image_offset;
	CString acsSubFilespecs[ MAX_LEVELS ];
	CJpeg jpeg;
	int num_uncompressed_bytes;
	int num_compressed_bytes, samp;
	int sub_tile_width, sub_tile_height, sub_width, sub_height, level;
	unsigned int sub_size;

   std::unique_ptr< UINT[] > aapuiSubTileByteCounts[ MAX_LEVELS ];
	PUINT sub_tile_byte_counts[ MAX_LEVELS ];
#ifndef FAST_SUBSAMPLE
	int j, x, y;
   int r, g, b, sx, sy, sj, sub_ndx;
	BOOL true_black;
#endif
	int key_filesum;
   INT iResult = FAILURE;     // Reasonable assumption

	if ( m_image == NULL )
   {
      *piErr = 4;
      csErrorMsg = _T("m_image is NULL");
		return FAILURE;
   }

	tile_cnt = 1;
	level = 0;

	// calc the number of levels needs to get to 512
	size = m_image_width;
	num_levels = 1;
	while ( size > 512 && num_levels < MAX_LEVELS )
	{
		num_levels++;
		size /= 2;
	}
	if ( m_image_length > m_image_width )
	{
		size = m_image_length;
		num_levels = 1;
		while (size > 512 && num_levels < MAX_LEVELS )
		{
			num_levels++;
			size /= 2;
		}
	}

	// calculate the 1 MB filesum of original file
	rslt = util.calc_file_1m_sum( m_tiff_file_name, &key_filesum, csErrorMsg );
	if ( rslt != SUCCESS )
	{
		*piErr = 3;
		csErrorMsg = _T("Error getting file size code");
		return FAILURE;
	}

	image_description.Format( _T("NumLevels=%d, Overview Image for %s, SrcMsum=%d,"),
      num_levels, (LPCSTR)m_tiff_file_name, key_filesum );
	software = _T("ImageLib.dll");

   // Get common base filsespec for all temporary files
   util.calc_overview_temp_base_file( m_tiff_file_name, base_name );
	jpeg_name = base_name + _T(".tmp");

	if (m_image_is_tiled)
	{
		tile_width = m_image->m_tile_size_x;
		tile_height = m_image->m_tile_size_y;
		num_tile_pixels = tile_width * tile_height;
		tile_cnt_x = m_image->m_tile_cnt_x;
		tile_cnt_y = m_image->m_tile_cnt_y;
		tile_cnt = tile_cnt_x * tile_cnt_y;
		imgwidth = m_image->m_image_width;
		imgheight = m_image->m_image_height;
	}
	else
	{
		// calculate tile sizes for non-tiled images
		tile_width = 1024;
		tile_height = 1024;
		num_tile_pixels = tile_width * tile_height;
		imgwidth = m_image_width;
		imgheight = m_image_length;
      tile_cnt_x = 1 + ( ( imgwidth - 1 ) / tile_width );
		tile_cnt_y = 1 + ( ( imgheight - 1 ) / tile_height );
	}
	num_tiles = tile_cnt_x * tile_cnt_y;

	sub_tile_width = tile_width / 2;
	sub_tile_height = tile_height / 2;

   cSubImgBytes = sub_tile_width * sub_tile_height * 3;
   apbSubImg = BytePtr( new BYTE[ cSubImgBytes ]  );
	sub_img = apbSubImg.get();

	// Generate the subfile names and open the files
	for ( k = 0; k < num_levels; k++ )
	{
      acsSubFilespecs[ k ].Format( _T("%s_%d.tmp"), (LPCSTR)base_name, k );
		if ( 0 != fopen_s( &(subfp[ k ]), acsSubFilespecs[ k ], "w+b" )
		   || subfp[ k ] == NULL )
		{
			*piErr = 4;
			csErrorMsg.Format( _T("Error opening file: %s"), (LPCSTR)acsSubFilespecs[ k ] );
			goto Exit;
		}
      aapuiSubTileByteCounts[ k ] = std::unique_ptr< UINT[] >( new UINT[ num_tiles ] );
		sub_tile_byte_counts[ k ] = aapuiSubTileByteCounts[ k ].get();
	}

	// restore the initial sub_tile sizes
	sub_tile_width = tile_width / 2;
	sub_tile_height = tile_height / 2;
	sub_width = imgwidth / 2;
	sub_height = imgheight / 2;

	num_tiles = tile_cnt_x * tile_cnt_y;
	num_uncompressed_bytes = num_tile_pixels;

   apbWork = BytePtr( new BYTE[ 3 * num_tile_pixels ] );
	tred = apbWork.get();
	tgrn = tred + num_tile_pixels;
	tblu = tgrn + num_tile_pixels;

#ifdef FAST_SUBSAMPLE
   apuiSubImg = std::unique_ptr< UINT[] >( new UINT[ 4 * num_tile_pixels ] ); // Subsampling accumulations & counts
#endif
	// note that the last low and column of tiles may need to be padded with zeroes
	tile_index = 0;
	itemnum = tile_cnt_y * tile_cnt_x;
	itemcnt = 0;
#ifdef TIMING_TEST
   TRACE( _T("CGeoTiff::create_multi_image_tiff_overview: tile rows = %d, tile cols = %d\n"),
      tile_cnt_y, tile_cnt_x );
   LARGE_INTEGER liFileStart, liFreq, liGetImageTotal, liSubsampleTotal, liJpegTotal;
   QueryPerformanceFrequency( &liFreq );
   QueryPerformanceCounter( &liFileStart );
   liGetImageTotal.QuadPart = liSubsampleTotal.QuadPart = liJpegTotal.QuadPart = 0;
#endif
	for (tile_row=0; tile_row<tile_cnt_y; tile_row++)
	{
		for (tile_col=0; tile_col<tile_cnt_x; tile_col++)
		{
#ifdef TIMING_TEST
         LARGE_INTEGER liTileStart, liSubsampleTime, liJpegTime, liCopyTime;
         liSubsampleTime.QuadPart = liJpegTime.QuadPart = liCopyTime.QuadPart = 0;
         QueryPerformanceCounter( &liTileStart );
#endif
			tile = m_image->m_tile_cnt_x * tile_row + tile_col;
			percent = 100.0 * (double) tile / (double) tile_cnt;
			if (m_image_is_tiled)
			{
				rslt = get_tile_as_rgb_image( tile, tred, tgrn, tblu);
				if ( rslt != SUCCESS )
				{
					*piErr = 5;
					csErrorMsg = _T("Error getting image tile");
					goto Exit;
				}
			}
			else
			{
				int min_hpix, max_hpix, min_vpix, max_vpix;

				min_hpix = tile_col * tile_width;
				max_hpix = min_hpix + tile_width - 1;
				min_vpix = tile_row * tile_height;
				max_vpix = min_vpix + tile_height - 1;

				rslt = get_filled_rgb_subimage(tile_width, tile_height, min_hpix, min_vpix, max_hpix, max_vpix,
												tred, tgrn, tblu);
				if ( rslt != SUCCESS )
            {
               *piErr = 5;
               csErrorMsg = _T("Error get_filled_rgb_subimage");
					goto Exit;
            }
			}
#ifdef TIMING_TEST
         LARGE_INTEGER liGetImageEnd;
         QueryPerformanceCounter( &liGetImageEnd );
#endif
#ifdef FAST_SUBSAMPLE
         PUINT
            puiSubImgRed = apuiSubImg.get(),
            puiSubImgGrn = puiSubImgRed + num_tile_pixels,
            puiSubImgBlu = puiSubImgGrn + num_tile_pixels,
            puiSubImgCounts = puiSubImgBlu + num_tile_pixels;  // Counts trail RGB data

         for ( i = 0; i < num_tile_pixels; i++ )
         {
            puiSubImgRed[ i ] = tred[ i ];   // Extend byte to UINT
            puiSubImgGrn[ i ] = tgrn[ i ];
            puiSubImgBlu[ i ] = tblu[ i ];
            puiSubImgCounts[ i ] = ( ( tred[ i ] == 0 ) && ( tgrn[ i ] == 0 ) && ( tblu[ i ] == 0 ) )
               ? 0 : 1;       // Count if not transparent (all black)
         }
#endif
			samp = 2;
			for ( level = 0; level < num_levels; level++ )
			{
#ifdef TIMING_TEST
            LARGE_INTEGER li;
            QueryPerformanceCounter( &li );
            liSubsampleTime.QuadPart -= li.QuadPart;
#endif
#ifdef FAST_SUBSAMPLE
            UINT uiInRowLength = 2 * sub_tile_width;
            for ( INT iRowOut = 0; iRowOut < sub_tile_height; iRowOut++ )
            {
               // Precompute the row starting addresses
               PUINT
                  puiRedRowOut = puiSubImgRed + ( iRowOut * sub_tile_width ),
                  puiGrnRowOut = puiRedRowOut + num_tile_pixels,
                  puiBluRowOut = puiGrnRowOut + num_tile_pixels,
                  puiCountsRowOut = puiBluRowOut + num_tile_pixels,
                  puiRedRowIn1 = puiSubImgRed + ( 2 * uiInRowLength * iRowOut ),   // Base input row
                  puiRedRowIn2 = puiRedRowIn1 + uiInRowLength, // Next row
                  puiGrnRowIn1 = puiRedRowIn1 + num_tile_pixels,
                  puiGrnRowIn2 = puiGrnRowIn1 + uiInRowLength,
                  puiBluRowIn1 = puiGrnRowIn1 + num_tile_pixels,
                  puiBluRowIn2 = puiBluRowIn1 + uiInRowLength,
                  puiCountsRowIn1 = puiBluRowIn1 + num_tile_pixels,
                  puiCountsRowIn2 = puiCountsRowIn1 + uiInRowLength;
               INT iSubImgIndex = 3 * iRowOut * sub_tile_width;

               for ( INT iColIn = 0, iColOut = 0;
                  iColOut < sub_tile_width;
                  iSubImgIndex += 3, iColIn += 2, iColOut += 1 )
               {
                  UINT uiRed, uiGrn, uiBlu, uiCount;

                  puiRedRowOut[ iColOut ] = uiRed =
                     puiRedRowIn1[ iColIn + 0 ]
                     + puiRedRowIn1[ iColIn + 1 ]
                     + puiRedRowIn2[ iColIn + 0 ]
                     + puiRedRowIn2[ iColIn + 1 ];
                  puiGrnRowOut[ iColOut ] = uiGrn =
                     puiGrnRowIn1[ iColIn + 0 ]
                     + puiGrnRowIn1[ iColIn + 1 ]
                     + puiGrnRowIn2[ iColIn + 0 ]
                     + puiGrnRowIn2[ iColIn + 1 ];
                  puiBluRowOut[ iColOut ] = uiBlu =
                     puiBluRowIn1[ iColIn + 0 ]
                     + puiBluRowIn1[ iColIn + 1 ]
                     + puiBluRowIn2[ iColIn + 0 ]
                     + puiBluRowIn2[ iColIn + 1 ];

                  if ( ( puiCountsRowOut[ iColOut ] = uiCount =
                     puiCountsRowIn1[ iColIn + 0 ]
                     + puiCountsRowIn1[ iColIn + 1 ]
                     + puiCountsRowIn2[ iColIn + 0 ]
                     + puiCountsRowIn2[ iColIn + 1 ] ) == 0 )   // All black pixels
                  {
                     // All pixel values are zero
                     sub_img[ iSubImgIndex + 0 ] = sub_img[ iSubImgIndex + 1 ]
                        = sub_img[ iSubImgIndex + 2 ] = 0;
                  }
                  else
                  {
                     // RGB values are rounded high to insure that even a single original value of 1
                     // will produce a non-zero output.
                     sub_img[ iSubImgIndex + 0 ] = ( puiRedRowOut[ iColOut ] + uiCount - 1 ) / uiCount;
                     sub_img[ iSubImgIndex + 1 ] = ( puiGrnRowOut[ iColOut ] + uiCount - 1 ) / uiCount;
                     sub_img[ iSubImgIndex + 2 ] = ( puiBluRowOut[ iColOut ] + uiCount - 1 ) / uiCount;
                  }  // Non-black pixel

               }  // Column loop
            }  // Row loop
#endif
#ifndef FAST_SUBSAMPLE
            // subsample the tile image for the sub2 image
				for (y=0; y<sub_tile_height; y++)
				{
					for (x=0; x<sub_tile_width; x++)
					{
						j = (y * samp * tile_width) + x * samp;
						sub_ndx = (y * sub_tile_width) + x;
						sub_ndx *= 3;
//						sub_img[sub_ndx+0] = tred[j];
//						sub_img[sub_ndx+1] = tgrn[j];
//						sub_img[sub_ndx+2] = tblu[j];
						// interpolate
						true_black = FALSE;
						r = g = b = 0;
						for (sy=0; sy<samp; sy++)
						{
							for (sx=0; sx<samp; sx++)
							{
								sj = j + (sy * tile_width) + sx;
								r += tred[sj];
								g += tgrn[sj];
								b += tblu[sj];
								if ((tred[sj] == 0) && (tgrn[sj] == 0) && (tblu[sj] == 0))
									true_black = TRUE;
							}
						}
						sub_img[sub_ndx+0] = (BYTE) (r / (samp * samp));
						sub_img[sub_ndx+1] = (BYTE) (g / (samp * samp));
						sub_img[sub_ndx+2] = (BYTE) (b / (samp * samp));
						if (!true_black)
						{
							if (sub_img[sub_ndx+0] == 0)
								sub_img[sub_ndx+0] = 1;
							if (sub_img[sub_ndx+1] == 0)
								sub_img[sub_ndx+1] = 1;
							if (sub_img[sub_ndx+2] == 0)
								sub_img[sub_ndx+2] = 1;
						}
					}
				}
#endif
#ifdef TIMING_TEST
            QueryPerformanceCounter( &li );
            liSubsampleTime.QuadPart += li.QuadPart;
            liJpegTime.QuadPart -= li.QuadPart;
#endif
				// write the tile out to a jpeg file for sub2
				rslt = jpeg.write_jpeg_file( jpeg_name, sub_tile_width, sub_tile_height, sub_img, 80, csErrorMsg );
				if ( rslt != SUCCESS )
            {
               *piErr = 5;
					goto Exit;
            }
#ifdef TIMING_TEST
            QueryPerformanceCounter( &li );
            liJpegTime.QuadPart += li.QuadPart;
            liCopyTime.QuadPart -= li.QuadPart;
#endif

            // Add the jpeg image to the sub2 file
				if ( 0 != fopen_s( &jfp, jpeg_name, "rb" )
				   || jfp == NULL )
				{
					*piErr = 5;
					csErrorMsg.Format( _T("Error reading file: %s"), (LPCSTR)jpeg_name );
					goto Exit;
				}
				num_compressed_bytes = (INT) fread( sub_img, sizeof(BYTE), cSubImgBytes, jfp );
				if ( num_compressed_bytes <= 0 )
            {
               csErrorMsg = _T("fread(jpeg) failed");
               *piErr = 4;
					goto Exit;
            }
				sub_tile_byte_counts[level][tile_index] = num_compressed_bytes;

				if ( fwrite( sub_img, sizeof(BYTE), num_compressed_bytes, subfp[level] )
               != num_compressed_bytes )
            {
               csErrorMsg = _T("fwrite(subimg) failed");
               *piErr = 4;
               goto Exit;
            }
				fclose( jfp ); jfp = NULL;
#ifdef TIMING_TEST
            QueryPerformanceCounter( &li );
            liCopyTime.QuadPart += li.QuadPart;
#endif

				samp *= 2;
				sub_tile_width /= 2;
				sub_tile_height /= 2;

			}  // Level loop

			sub_tile_width = tile_width / 2;
			sub_tile_height = tile_height / 2;

			tile_index++;

			itemcnt++;

#ifdef TIMING_TEST
         LARGE_INTEGER liTileEnd;
         QueryPerformanceCounter( &liTileEnd );
         TRACE( _T("  Tile row %d, column %d, get_image time = %.3f, tile time = %.3f,\n"
            "    subsample time = %.3f, jpeg time = %.3f, copy time = %.3f\n"),
            tile_row, tile_col,
            ( liGetImageEnd.QuadPart - liTileStart.QuadPart ) / (DOUBLE) liFreq.QuadPart,
            ( liTileEnd.QuadPart - liTileStart.QuadPart ) / (DOUBLE) liFreq.QuadPart,
            (DOUBLE) liSubsampleTime.QuadPart / liFreq.QuadPart,
            (DOUBLE) liJpegTime.QuadPart / liFreq.QuadPart,
            (DOUBLE) liCopyTime.QuadPart / liFreq.QuadPart );
         liGetImageTotal.QuadPart += liGetImageEnd.QuadPart - liTileStart.QuadPart;
         liSubsampleTotal.QuadPart += liSubsampleTime.QuadPart;
         liJpegTotal.QuadPart += liJpegTime.QuadPart;
#endif

         double percent = 100.0 * (double) itemcnt / (double) itemnum;
			CString label = "Creating Overview File";
			if (!util.send_user_update_and_continue(callback, percent, label))
			{
				*piErr = USER_ABORT;
				csErrorMsg = "Operation canceled by user";
            TRACE( _T("Overview build for %s aborted\n"), m_tiff_file_name );
				goto Exit;
			}

		}  // Tile column loop
	}  // Tile row loop
#ifdef TIMING_TEST
   LARGE_INTEGER liFileEnd;
   QueryPerformanceCounter( &liFileEnd );
   #ifdef _DEBUG
      TRACE( _T("  Overview end, total time = %.3f, GetImage = %.3f, subsample = %.3f, jpeg = %.3f\n"),
         ( liFileEnd.QuadPart - liFileStart.QuadPart ) / (DOUBLE) liFreq.QuadPart,
         (DOUBLE) liGetImageTotal.QuadPart / liFreq.QuadPart,
         (DOUBLE) liSubsampleTotal.QuadPart / liFreq.QuadPart,
         (DOUBLE) liJpegTotal.QuadPart / liFreq.QuadPart );
   #else
      WCHAR wchWork[500];
      swprintf( wchWork, L"  Overview end, total time = %.3f, GetImage = %.3f, subsample = %.3f, jpeg = %.3f",
         ( liFileEnd.QuadPart - liFileStart.QuadPart ) / (DOUBLE) liFreq.QuadPart,
         (DOUBLE) liGetImageTotal.QuadPart / liFreq.QuadPart,
         (DOUBLE) liSubsampleTotal.QuadPart / liFreq.QuadPart,
         (DOUBLE) liJpegTotal.QuadPart / liFreq.QuadPart );
      WriteToLogFile( wchWork, L"ImageLibTest.log", TRUE );
   #endif
#endif

	// *****************************************************
	// Write the tiff file
	// *****************************************************

	// create the file
	rslt = util.calc_overview_write_name( m_tiff_file_name, overview_name, csErrorMsg );
	if ( rslt != SUCCESS )
   {
      *piErr = 4;
		goto Exit;
   }

   csTempOverviewFilespec = overview_name + _T(".tmp");
	if ( 0 != fopen_s( &fp, csTempOverviewFilespec, "wb" )
	   || fp == NULL )
	{
		*piErr = 4;
		csErrorMsg.Format( _T("Error opening file - %s - for writing"), (LPCSTR)csTempOverviewFilespec );
		goto Exit;
	}

	////////////////////////////////////////////////
	// Write the file header
	////////////////////////////////////////////////
	// bytes 0-1
	// write byte order string
	fwrite( "II", 1, 2, fp );

	// bytes 2-3
	// write 42 to identify as a tiff file
	ushort = 42;
	fwrite( &ushort, 2, 1, fp );

	// bytes 4-7
	// write offset of image file directory
	uint = 8;
	fwrite( &uint, 4, 1, fp );

	ifd_offset = 8;

   apbWork = BytePtr( new BYTE[ MAX_COPY_BYTES ] );  // For file copy

	for ( level = 0; level < num_levels; level++ )
	{
		// calculate data offset for writing data outside of image file directory
		data_offset = ifd_offset;    // IFP header
		data_offset += sizeof( unsigned short );    // tag count
		data_offset += num_tags * 12;   // 15 tags at 12 bytes each
		data_offset += sizeof( unsigned int );    // terminating zero

		// calculate image offset
		image_offset = data_offset;   // start at data offset
		image_offset += 3 * sizeof( unsigned short );   // bits per sample
		image_offset += num_tiles * sizeof( unsigned int ); // tile offsets
		image_offset += num_tiles * sizeof( unsigned int ); // tile bytes
		image_offset += image_description.GetLength( ) + 1; // image description
		image_offset += software.GetLength( ) + 1; // software

		// compute the offset of the next IFD
		ifd_offset = image_offset;
      sub_size = ftell( subfp[ level ] );    // After last write
		ifd_offset += sub_size;

		// image file directory
		// bytes 8-9
		// write number of tags
		ushort = (unsigned short) num_tags;
		fwrite( &ushort, 2, 1, fp );

		// bytes 10-21
		// write image width tag
		tag_id = 256;
		data_type = 3;
		count = 1;
		data_value = sub_width;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 22-33
		// write image length tag
		tag_id = 257;
		data_type = 3;
		count = 1;
		data_value = sub_height;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 34-45
		// write bits per sample tag
		tag_id = 258;
		data_type = 3;
		count = 3;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_offset, 4, 1, fp );
		data_offset += count * sizeof( unsigned short );

		// bytes 46-57
		// write compression tag
		tag_id = 259;
		data_type = 3;
		count = 1;
		data_value = 7;        // jpeg
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 58-69
		// write photometric interpretation tag
		tag_id = 262;
		data_type = 3;
		count = 1;
		data_value = 6;  // YCbCr
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 70-81
		// write image description tag
		tag_id = 270;
		data_type = 2;
		count = image_description.GetLength( ) + 1;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_offset, 4, 1, fp );
		data_offset += count * sizeof( unsigned char );

		// bytes 82-93
		// write samples per pixel tag
		tag_id = 277;
		data_type = 3;
		count = 1;
		data_value = 3;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 84-105
		// write planar configuration tag
		tag_id = 284;
		data_type = 3;
		count = 1;
		data_value = 1;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 106-117
		// write software tag
		tag_id = 305;
		data_type = 2;
		count = software.GetLength( ) + 1;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_offset, 4, 1, fp );
		data_offset += count * sizeof( unsigned char );

		// bytes 118-129
		// write tile width tag
		tag_id = 322;
		data_type = 3;
		count = 1;
		data_value = sub_tile_width;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 130-141
		// write tile length tag
		tag_id = 323;
		data_type = 3;
		count = 1;
		data_value = sub_tile_height;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 142-153
		// write tile offsets tag
		tag_id = 324;
		data_type = 4;
		count = num_tiles;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_offset, 4, 1, fp );
		data_offset += count * sizeof( unsigned int );

		// bytes 154-165
		// write tile byte count tag
		tag_id = 325;
		data_type = 4;
		count = num_tiles;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_offset, 4, 1, fp );
		data_offset += count * sizeof( unsigned int );

		// bytes 166-169
		// write terminating zero
		if (level >= num_levels - 1)
			uint = 0;
		else
			uint = ifd_offset;
		fwrite( &uint, 4, 1, fp );

		// write bits per sample values
		ushort = 8;
		fwrite( &ushort, 2, 1, fp );
		fwrite( &ushort, 2, 1, fp );
		fwrite( &ushort, 2, 1, fp );

		// write image description
		length = image_description.GetLength( ) + 1;  // include terminating null
		fwrite( image_description.GetBuffer( length ), length, 1, fp );

		// write software
		length = software.GetLength( ) + 1;  // include terminating null
		fwrite( software.GetBuffer( length ), length, 1, fp );

		// write out tile offsets
		uint = image_offset;
		for( i = 0; i < num_tiles; i++ )
		{
			fwrite( &uint, 4, 1, fp );
			uint += sub_tile_byte_counts[level][i];
		}

		// write out tile byte counts
		for( i = 0; i < num_tiles; i++ )
			fwrite( &sub_tile_byte_counts[level][i], 4, 1, fp );


      rewind( subfp[ level ] );
      for ( INT cRemaining = sub_size; cRemaining > 0; cRemaining -= MAX_COPY_BYTES )
      {
         size_t cCopyBytes = cRemaining;
         if ( cCopyBytes > MAX_COPY_BYTES )
            cCopyBytes = MAX_COPY_BYTES;
         fread( apbWork.get(), sizeof(BYTE), cCopyBytes, subfp[ level ] );
         fwrite( apbWork.get(), sizeof(BYTE), cCopyBytes, fp );
      }

		sub_tile_width /= 2;
		sub_tile_height /=2;
		sub_width /= 2;
		sub_height /= 2;
		image_description.Format( _T("Level=%d, Overview Image for %s"), level, (LPCSTR)m_tiff_file_name );
	}

	fclose( fp );     // Tentative output file
   DeleteFile( overview_name );      // Make sure nothing aready exists
   MoveFile( csTempOverviewFilespec, overview_name ); // Give it its proper name

   iResult = SUCCESS;
   *piErr = 0;
   csErrorMsg = _T("");

Exit:
   // Clean up the subfiles
	for ( k = 0; k < num_levels; k++ )
	{
      fclose( subfp[ k ] );
		DeleteFile( acsSubFilespecs[ k ] );
	}
   if ( jfp != NULL )
      fclose( jfp );       // Possible jpeg file
   DeleteFile( jpeg_name );

	util.clear_callback(callback);
	return iResult;
}
// end of create_multi_image_tiff_overview



// ****************************************************************
// ****************************************************************

int CGeoTiff::check_for_usgs_cropping()
{
   // this function checks the .tif file specified by file_path for being a
   // supported geotiff file (USGS DRG, DOQ) and if so returns the bounding
   // latitude and longitude, file size, scale, and series
   // returns SUCCESS if the file is supported or FAILURE otherwise

   int error_code;
   CString image_type_description, error_message;
   CString tiff_file_path, path_copy;
   CGeoData geodata;
   CString image_description, software;
   CString datum_string;
   CFile file;
   CGeoTrans geotrans;
   int rslt;
   double ll_lat, ll_lon, ur_lat, ur_lon;

   // get the geodata structure
   if ( get_geodata( geodata, error_code, error_message ) != SUCCESS )
      goto NOT_SUPPORTED;
   // determine datum string
   switch( geodata.m_geog_geodetic_datum )
   {
      case GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1927:
         datum_string = "NAS-C";
         break;
      case GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1983:
         datum_string = "NAR-C";
         break;
      case GEOTIFF_DATUM_WGS72:
         datum_string = "W72";
         break;
      case GEOTIFF_DATUM_WGS84:
         datum_string = "W84";
         break;
      default:
	      goto NOT_SUPPORTED;
   }

	// get the image description
	if ( get_image_description( image_description ) != SUCCESS )
		image_description = "";
	image_description.MakeUpper( );

	// get software string
	if ( get_software( software ) != SUCCESS )
		software = "";
	software.MakeUpper( );

	// check for USGS DRG *************************************************
	if ( image_description.Find( "USGS GEOTIFF DRG" ) == -1 )
		goto CHECK_DOQ;

	// determine scale and coverage from file name
	rslt = get_drg_coverage( m_raw_filename );
	if ( rslt != SUCCESS )
		goto CHECK_DOQ;

	// convert datum for coverage lat/lon's to WGS84
	geotrans.convert_datum( m_cropped_ll_lat, m_cropped_ll_lon, ll_lat, ll_lon, datum_string, "W84" );
	geotrans.convert_datum( m_cropped_ur_lat, m_cropped_ur_lon, ur_lat, ur_lon, datum_string, "W84" );
	m_cropped_ll_lat = ll_lat;
	m_cropped_ll_lon = ll_lon;
	m_cropped_ur_lat = ur_lat;
	m_cropped_ur_lon = ur_lon;

   // verify scale in image description
   if( image_description.Find( "USGS GEOTIFF DRG 1:250000" ) != -1 )
   {
      m_scale = ONE_TO_250K;
      if( m_scale != m_scale_from_file_name )
		  goto CHECK_DOQ;
      return SUCCESS;
   }

   if( image_description.Find( "USGS GEOTIFF DRG 1:100000" ) != -1 )
   {
      m_scale = ONE_TO_100K;
      if( m_scale != m_scale_from_file_name )
		  goto CHECK_DOQ;
      return SUCCESS;
   }

   if( image_description.Find( "USGS GEOTIFF DRG 1:63360" ) != -1 )
   {
      m_scale = ONE_TO_63360;
      if( m_scale != m_scale_from_file_name )
		  goto CHECK_DOQ;
      return SUCCESS;
   }

   if( image_description.Find( "USGS GEOTIFF DRG 1:30000" ) != -1 )
   {
      m_scale = ONE_TO_30K;
      if( m_scale != m_scale_from_file_name )
		  goto CHECK_DOQ;
      return SUCCESS;
   }

   if( image_description.Find( "USGS GEOTIFF DRG 1:25000" ) != -1 )
   {
      m_scale = ONE_TO_25K;
      if( m_scale != m_scale_from_file_name )
		  goto CHECK_DOQ;
      return SUCCESS;
   }

   if( image_description.Find( "USGS GEOTIFF DRG 1:24000" ) != -1 )
   {
      m_scale = ONE_TO_24K;
      if( m_scale != m_scale_from_file_name )
		  goto CHECK_DOQ;
      return SUCCESS;
   }

   if( image_description.Find( "USGS GEOTIFF DRG 1:20000" ) != -1 )
   {
      m_scale = ONE_TO_20K;
      if( m_scale != m_scale_from_file_name )
		  goto CHECK_DOQ;
      return SUCCESS;
   }

   goto CHECK_DOQ;

   // check for USGS 1 meter DOQ **********************************************
CHECK_DOQ:
   m_scale = NULL_SCALE;

   if( image_description.Find( "USGS GEOTIFF DIGITAL ORTHOIMAGE" ) == -1 &&
       image_description.Find( "USGS GEOTIFF DOQ" ) == -1 )
      goto CHECK_OTHER;

   // check meters per pixel scale (nominal 1.0) in x and y directions
   if ( geodata.m_dLScaleX != geodata.m_dPScaleY
         || geodata.m_dLScaleX != 1.0 )
      goto NOT_SUPPORTED;

	// determine coverage from file name
	rslt = get_doq_coverage( m_raw_filename );
	if (rslt != SUCCESS)
		goto CHECK_OTHER;

	// convert datum for coverage lat/lon's
	geotrans.convert_datum( m_cropped_ll_lat, m_cropped_ll_lon, ll_lat, ll_lon, datum_string, "W84" );
	geotrans.convert_datum( m_cropped_ur_lat, m_cropped_ur_lon, ur_lat, ur_lon, datum_string, "W84" );
	m_cropped_ll_lat = ll_lat;
	m_cropped_ll_lon = ll_lon;
	m_cropped_ur_lat = ur_lat;
	m_cropped_ur_lon = ur_lon;

	return SUCCESS;

CHECK_OTHER:

	// drop through to failure

NOT_SUPPORTED:
	return FAILURE;
}
// end of check_for_usgs_cropping

// ****************************************************************
// ****************************************************************

int CGeoTiff::get_drg_coverage( CString tiff_file_name )
{
   // this function determines the scale and lat/lon coverage of a usgs drg
   // file based on the name of the file
   // returns SUCCESS or FAILURE

   int length, ilat, ilon, i100, i10, i1;
   int secondary_cell_lat, secondary_cell_lon;
   double primary_cell_width, primary_cell_height;
   double primary_cell_lat, primary_cell_lon;
   char c, string[2], primary_cell_c1, primary_cell_c2;
   double ll_lat, ll_lon, ur_lat, ur_lon;

   // make the file name uppercase
   tiff_file_name.MakeUpper( );

   // make sure that the file name is 12 characters long
   length = tiff_file_name.GetLength( );
   if( length != 12 )
      goto FAIL;

   // check first character to determine scale
   c = tiff_file_name.GetAt( 0 );
   switch( c )
   {
      case 'C':                        // USGS 1:250K DRG  2deg x 1deg
         m_scale = ONE_TO_250K;
         primary_cell_width = 2.0;
         primary_cell_height = 1.0;
         break;
      case 'F':                        // USGS 1:100K DRG  1deg x 0.5deg
         m_scale = ONE_TO_100K;
         primary_cell_width = 1.0;
         primary_cell_height = 0.5;
         break;
      case 'I':                        // USGS 1:63360 DRG (Alaska)  22.5'x15'
         m_scale = ONE_TO_63360;
         primary_cell_width = 0.375;
         primary_cell_height = 0.25;
         break;
      case 'J':                        // USGS 1:30K DRG
         // only 2 of these 1:30000 scale "J" files exist and they don't fit
         // the standard naming convention - so we just hardwire the corners
         // of the two known files, which are islands near Puerto Rico
         if( tiff_file_name.Find( "J18065C2" ) >= 0 )    // known file
         {                          // "CULEBRA AND ADJACENT ISLANDS"
            m_scale = ONE_TO_30K;
            ll_lat = 18.266666;
            ll_lon = -65.4;
            ur_lat = 18.366666;
            ur_lon = -65.216666;
            return SUCCESS;
         }
         if( tiff_file_name.Find( "J18065A3" ) >= 0 )    // known file
         {                          // "ISLA DE VIEQUES"
            m_scale = ONE_TO_30K;
            ll_lat = 18.066666;
            ll_lon = -65.583333;
            ur_lat = 18.175;
            ur_lon = -65.266666;
            return SUCCESS;
         }
         // otherwise the file is unknown
         goto FAIL;
      case 'K':                        // USGS 1:25K DRG  15'x7.5'
         m_scale = ONE_TO_25K;
         primary_cell_width = 0.25;
         primary_cell_height = 0.125;
         break;
      case 'L':                        // USGS 1:25K DRG  7.5'x7.5'
         m_scale = ONE_TO_25K;
         primary_cell_width = 0.125;
         primary_cell_height = 0.125;
         break;
      case 'O':                        // USGS 1:24K DRG  7.5'x7.5'
         m_scale = ONE_TO_24K;
         primary_cell_width = 0.125;
         primary_cell_height = 0.125;
         break;
      case 'P':                        // USGS 1:24K DRG  7.5'x7.5'
         m_scale = ONE_TO_24K;
         primary_cell_width = 0.125;
         primary_cell_height = 0.125;
         break;
      case 'R':                        // USGS 1:20K DRG  7.5'x7.5'
         m_scale = ONE_TO_20K;
         primary_cell_width = 0.125;
         primary_cell_height = 0.125;
         break;
      default:
         goto FAIL;
   }

   // determine secondary cell latitude
   //TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   string[1] = NULL;
   string[0] = tiff_file_name.GetAt( 1 );
   if( sscanf_s( string, "%i", &i10 ) != 1 )
      goto FAIL;
   string[0] = tiff_file_name.GetAt( 2 );
   if( sscanf_s( string, "%i", &i1 ) != 1 )
      goto FAIL;
   secondary_cell_lat = 10*i10 + i1;
   if( secondary_cell_lat >= 90 )
      goto FAIL;

   // determine secondary cell longitude (west is positive 0 to +360 deg)
   string[0] = tiff_file_name.GetAt( 3 );
   if( sscanf_s( string, "%i", &i100 ) != 1 )
      goto FAIL;
   string[0] = tiff_file_name.GetAt( 4 );
   if( sscanf_s( string, "%i", &i10 ) != 1 )
      goto FAIL;
   string[0] = tiff_file_name.GetAt( 5 );
   if( sscanf_s( string, "%i", &i1 ) != 1 )
      goto FAIL;
   secondary_cell_lon = 100*i100 + 10*i10 + i1;
   if( secondary_cell_lat >= 360 )
      goto FAIL;

   // determine primary cell
   primary_cell_c1 = tiff_file_name.GetAt( 6 );
   primary_cell_c2 = tiff_file_name.GetAt( 7 );

   switch( primary_cell_c1 )
   {
      case 'A':
         ilat = 0;
         break;
      case 'B':
         ilat = 1;
         break;
      case 'C':
         ilat = 2;
         break;
      case 'D':
         ilat = 3;
         break;
      case 'E':
         ilat = 4;
         break;
      case 'F':
         ilat = 5;
         break;
      case 'G':
         ilat = 6;
         break;
      case 'H':
         ilat = 7;
         break;
      default:
         goto FAIL;
   }

   switch( primary_cell_c2 )
   {
      case '1':
         ilon = 0;
         break;
      case '2':
         ilon = 1;
         break;
      case '3':
         ilon = 2;
         break;
      case '4':
         ilon = 3;
         break;
      case '5':
         ilon = 4;
         break;
      case '6':
         ilon = 5;
         break;
      case '7':
         ilon = 6;
         break;
      case '8':
         ilon = 7;
         break;
      default:
         goto FAIL;
   }

   // find the lat/lon of the primary cell lower right corner
   primary_cell_lat = secondary_cell_lat + ilat * 0.125;
   primary_cell_lon = secondary_cell_lon + ilon * 0.125;

   // calculate lower left and upper right corner lat/lon's
   ll_lat = primary_cell_lat;
   ll_lon = primary_cell_lon + primary_cell_width;

   ur_lat = primary_cell_lat + primary_cell_height;
   ur_lon = primary_cell_lon;

   // now convert to east positive 0 to 180, west negative 0 to -180
   ll_lon = -ll_lon;
   if( ll_lon < -180.0 )
      ll_lon += 360.0;
   ur_lon = -ur_lon;
   if( ur_lon < -180.0 )
      ur_lon += 360.0;

	m_cropped_ll_lat = ll_lat;
	m_cropped_ll_lon = ll_lon;
	m_cropped_ur_lat = ur_lat;
	m_cropped_ur_lon = ur_lon;
	m_is_cropped = TRUE;
	m_scale_from_file_name = m_scale;

	return SUCCESS;

FAIL:
   return FAILURE;
}
// end of get_drg_coverage

// ****************************************************************
// ****************************************************************

int CGeoTiff::get_doq_coverage( CString tiff_file_name )
{
   // this function determines the lat/lon coverage of a usgs doq
   // file based on the name of the file
   // returns SUCCESS or FAILURE

   int length, ilat, ilon, i100, i10, i1;
   int secondary_cell_lat, secondary_cell_lon;
   double primary_cell_lat, primary_cell_lon;
   char string[2], primary_cell_c1, primary_cell_c2, primary_cell_c3;
   double ll_lat, ll_lon, ur_lat, ur_lon;

   // make the file name uppercase
   tiff_file_name.MakeUpper( );

   // make sure that the file name is 12 characters long
   length = tiff_file_name.GetLength( );
   if( length != 12 )
      goto FAIL;

   // determine secondary cell latitude
   //TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   string[1] = NULL;
   string[0] = tiff_file_name.GetAt( 0 );
   if( sscanf_s( string, "%i", &i10 ) != 1 )
      goto FAIL;
   string[0] = tiff_file_name.GetAt( 1 );
   if( sscanf_s( string, "%i", &i1 ) != 1 )
      goto FAIL;
   secondary_cell_lat = 10*i10 + i1;
   if( secondary_cell_lat >= 90 )
      goto FAIL;

   // determine secondary cell longitude (west is positive 0 to +360 deg)
   string[0] = tiff_file_name.GetAt( 2 );
   if( sscanf_s( string, "%i", &i100 ) != 1 )
      goto FAIL;
   string[0] = tiff_file_name.GetAt( 3 );
   if( sscanf_s( string, "%i", &i10 ) != 1 )
      goto FAIL;
   string[0] = tiff_file_name.GetAt( 4 );
   if( sscanf_s( string, "%i", &i1 ) != 1 )
      goto FAIL;
   secondary_cell_lon = 100*i100 + 10*i10 + i1;
   if( secondary_cell_lat >= 360 )
      goto FAIL;

   // determine primary cell
   primary_cell_c1 = tiff_file_name.GetAt( 5 );
   primary_cell_c2 = tiff_file_name.GetAt( 6 );
   primary_cell_c3 = tiff_file_name.GetAt( 7 );

   switch( primary_cell_c1 )
   {
      case 'A':
         ilat = 0;
         break;
      case 'B':
         ilat = 1;
         break;
      case 'C':
         ilat = 2;
         break;
      case 'D':
         ilat = 3;
         break;
      case 'E':
         ilat = 4;
         break;
      case 'F':
         ilat = 5;
         break;
      case 'G':
         ilat = 6;
         break;
      case 'H':
         ilat = 7;
         break;
      default:
         goto FAIL;
   }

   switch( primary_cell_c2 )
   {
      case '1':
         ilon = 0;
         break;
      case '2':
         ilon = 1;
         break;
      case '3':
         ilon = 2;
         break;
      case '4':
         ilon = 3;
         break;
      case '5':
         ilon = 4;
         break;
      case '6':
         ilon = 5;
         break;
      case '7':
         ilon = 6;
         break;
      case '8':
         ilon = 7;
         break;
      default:
         goto FAIL;
   }

   // find the lat/lon of the primary cell lower right corner
   primary_cell_lat = secondary_cell_lat + ilat * 0.125;
   primary_cell_lon = secondary_cell_lon + ilon * 0.125;

   // now adjust for the required quarter quad
   switch( primary_cell_c3 )
   {
      case '1':
         primary_cell_lat += 0.0625;
         primary_cell_lon += 0.0625;
         break;
      case '2':
         primary_cell_lat += 0.0625;
         break;
      case '3':
         break;
      case '4':
         primary_cell_lon += 0.0625;
         break;
      case '5':
         primary_cell_lat += 0.0625;
         primary_cell_lon += 0.0625;
         break;
      case '6':
         primary_cell_lat += 0.0625;
         break;
      case '7':
         break;
      case '8':
         primary_cell_lon += 0.0625;
         break;
      default:
         goto FAIL;
   }

   // calculate lower left and upper right corner lat/lon's
   ll_lat = primary_cell_lat;
   ll_lon = primary_cell_lon + 0.0625;

   ur_lat = primary_cell_lat + 0.0625;
   ur_lon = primary_cell_lon;

   // now convert to east positive 0 to 180, west negative 0 to -180
   ll_lon = -ll_lon;
   if( ll_lon < -180.0 )
      ll_lon += 360.0;
   ur_lon = -ur_lon;
   if( ur_lon < -180.0 )
      ur_lon += 360.0;

	m_cropped_ll_lat = ll_lat;
	m_cropped_ll_lon = ll_lon;
	m_cropped_ur_lat = ur_lat;
	m_cropped_ur_lon = ur_lon;
	m_is_cropped = TRUE;
	m_scale_from_file_name = m_scale;

	return SUCCESS;

FAIL:
	return FAILURE;
}
// end of get_doq_coverage

// ****************************************************************
// ****************************************************************

