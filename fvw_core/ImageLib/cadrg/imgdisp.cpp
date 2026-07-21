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



#include "stdafx.h"
#include "imgdisp.h"
#include "cdb_base.h"
#include "RpfZoneScales.h"
#include "polar_utils.h"
#include <float.h>

// ****************************************************************
// ****************************************************************

RPFRenderer::~RPFRenderer()
{
   while (m_listCibHistogramEntries.size())
   {
      delete m_listCibHistogramEntries.back();
      m_listCibHistogramEntries.pop_back();
   }
}

// ****************************************************************
// ****************************************************************

/*
 *  Compute a pixel position in a frame as described in CADRG standard.
 *
 *  Note: pixel_row_in_frame and pixel_column_in_frame are computed based on 
 *  having the frame origin at its upper left corner
 */
int RPFRenderer::get_pixel_position_in_frame(CString source, double dScale,
   MapScaleUnitsEnum eScaleUnits, CArcZone primary_zone, double pixel_latitude, 
	double pixel_longitude, int* pixel_row_in_frame, int* pixel_column_in_frame)
{
   double north_south_pix_const;
   const double f_w = (double)CADRG_FRAME_WIDTH_IN_PIX;  // frame width in pix
   const double f_h = (double)CADRG_FRAME_HEIGHT_IN_PIX; // frame height in pix

   if (source.Compare("CIB") && source.Compare("CADRG"))
   {
      // invalid source
      return FAILURE;
   }

	north_south_pix_const = primary_zone.calc_n_s_pix_const(dScale, eScaleUnits);

   double zone_bound_lat = primary_zone.get_actual_equatorward_zone_extent(dScale, eScaleUnits);

   int frame_row_within_zone = (int)
      ((pixel_latitude-zone_bound_lat)/90.0 * (north_south_pix_const/f_h));

   /*
    *  use the frame row within the zone to figure out the latitude of the
    *  northwest corner of the frame
    */
   double frame_upper_left_lat = 
     (90.0/north_south_pix_const) * f_h * (frame_row_within_zone+1) + 
        zone_bound_lat; 

   *pixel_row_in_frame = (int) 
      ((frame_upper_left_lat - pixel_latitude) * (north_south_pix_const/90.0));


   /*
    *  now figure out the pixel column
    */

   double east_west_pix_const = primary_zone.calc_e_w_pix_const(dScale, eScaleUnits);
   
   int frame_column_in_zone = (int)
      ( ((pixel_longitude+180.0)/360.0) * (east_west_pix_const/f_w) );

   double frame_upper_left_lon =
      (360.0/east_west_pix_const) * f_w * frame_column_in_zone - 180.0;

   *pixel_column_in_frame = (int)
      ( (pixel_longitude-frame_upper_left_lon)/360.0 * east_west_pix_const );

   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

void RPFRenderer::calculate_stretch_lut(UINT4 *histogram, unsigned char *orig_lut,
																  unsigned char *lut) 
{
	int hist[216];
	int x, k, ndx;
	int cnt;
	double fsum, finc;
	int hi_ndx, lo_ndx;
	int new_lut[216];
	int cib_lut[216];
	BOOL notdone;
	int min_value = 100; 

	// copy the input histogram to the local one
	memcpy(hist, histogram, 216 * sizeof(int));

	// copy the orig_lut to the lut in case there is a problem
	memcpy(lut, orig_lut, 216 * 3 * sizeof(unsigned char));

	cnt = 0;

	// calculate the total count
	for (k=0; k<216; k++)
		if (hist[k] > min_value)
			cnt += hist[k];

	min_value = cnt / 100;

	// get the cib palette
	for (k=0; k<216; k++)
		cib_lut[k] = (int) orig_lut[k*3];

	// jim's contrast stretching routine
	hi_ndx = 0;
	lo_ndx = 215;

	// ignore the top and bottom 1 percent of the value range
	ndx = 0;
	cnt = 0;
	notdone = TRUE;
	while (notdone)
	{
		cnt += hist[ndx];
		if (cnt > min_value)
			notdone = FALSE;
		ndx++;
		if (ndx > 215)
			notdone = FALSE;
	}
	lo_ndx = ndx;

	ndx = 215;
	cnt = 0;
	notdone = TRUE;
	while (notdone)
	{
		cnt += hist[ndx];
		if (cnt > min_value)
			notdone = FALSE;
		ndx--;
		if (ndx <= lo_ndx)
			notdone = FALSE;
	}
	hi_ndx = ndx;


	// calculate the new lut
	for (x=0; x<=lo_ndx; x++)
		new_lut[x] = 0;
	for (x=215; x>=hi_ndx; x--)
		new_lut[x] = 255;

	// jim's contrast stretching routine

	// make luts before lowest valuse 0
	for (x=0; x<=lo_ndx; x++)
		new_lut[x] = 0;
	// make luts above highest value 255
	for (x=215; x>=hi_ndx; x--)
		new_lut[x] = 255;

	// set the middle indexs to a stretched value
	finc = 255.0 / (double) (hi_ndx - lo_ndx);
	fsum = 0.0;
	for (x=lo_ndx; x < hi_ndx; x++)
	{
		fsum += finc;
		if (fsum > 255)
			fsum = 255;
		new_lut[x] = (int) fsum;
	}

	// create the lut
	for (k=0; k<216; k++)
	{
		lut[(k*3)+0] = (char) new_lut[k];
		lut[(k*3)+1] = (char) new_lut[k];
		lut[(k*3)+2] = (char) new_lut[k];
	}
}

// ****************************************************************
// ****************************************************************

void RPFRenderer::calculate_bright_contrast(double *bright, double *contrast, int *midval)
{
	int k, total, total2, curval, minval, maxval;
	unsigned int thresh;

	// find the maximum value
	total = 0;
	for (k=0; k<216; k++)
	{
		curval = m_composite_histogram[k];
		total += curval;
	}

//	thresh = (int) ((double) total / 10000.0);

	thresh = 100;

	// find the lowest value above threshold
	k = 1;
	while ((k<216) && (m_composite_histogram[k] < thresh))
		k++;
	minval = k;
	k = 215;
	while ((k>=0) && (m_composite_histogram[k] < thresh))
		k--;
	maxval = k;

	// apply contrast stretch as a test
	// find center value
	total2 = total / 2;
	total = 0;
	k = 0;
	while (total < total2)
	{
		curval = m_composite_histogram[k];
		total += curval;
		k++;
	}

	k--;
	*midval = k;
	k = 108 - (minval + maxval) / 2;
	*bright = (double) k / 108.0;

//	k = 215 - (maxval - minval);
	k = 108 - *midval;
	*contrast = (double) k / 108.0;
}

// ****************************************************************
// ****************************************************************

int RPFRenderer::get_frame_image(CString filename, BOOL is_cib, BYTE *img)
{
//	RPFHelper rpf_helper;
	FILE *frame_fp = NULL;
	int start_tile_x, start_tile_y, end_tile_x, end_tile_y;
	int endx, endy, rslt, sf_index, x, y, tx, ty, ip, op;
	int startx, starty, width, height;
	BYTE *frame_buf;

	frame_buf = (BYTE*) malloc(256 * 256 * 3);

	startx = starty = 0;
	width = height = 1536;

	endx = startx + width - 1;
	endy = starty + height - 1;

	start_tile_x = startx / 256;
	start_tile_y = starty / 256;


	end_tile_x = endx / 256;
	end_tile_y = endy / 256;

	cadrg_cib_base cadrg;

	// Open the file now, if has not already  been opened
   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
	fopen_s(&frame_fp, filename, "rb");
	if (frame_fp == NULL)
	{
		std::string strError("Error opening RPF frame: ");
		strError += filename;
		free(frame_buf);
		return FAILURE;
	}

	if (cadrg.read(frame_fp) != SUCCESS)
	{
		std::string strError("Invalid CADRG frame: ");
		strError += filename;
		free(frame_buf);
		fclose(frame_fp);

		return FAILURE;
	}

	//
	// Since the file is already open and subframes may have already been
	// read from it, make sure to seek to the beginning of the file.
	//
	if (fseek(frame_fp, 0, SEEK_SET) != 0)
	{
		// fseek failed
		free(frame_buf);
		fclose(frame_fp);
		return FAILURE;
	}

	for (y=start_tile_y; y<=end_tile_y; y++)
	{
		for (x = start_tile_x; x <= end_tile_x; x++)
		{
			sf_index = (y * 6) + x;
			rslt = read_and_display_subframe(frame_fp, is_cib, sf_index, cadrg, frame_buf);
			if (rslt != SUCCESS)
			{
				// read_and_display_subframes failed
//				free(frame_buf);
//				fclose(frame_fp);
//				AfxMessageBox("Error in -- read_and_display_subframe()");
//				return FAILURE;
				continue;
			}
			// copy frame_buf to image_buf
			for (ty = 0; ty < 256; ty++)
			{
				op = ((y - start_tile_y) * 256 * width) + (ty * width) + ((x - start_tile_x) * 256);
				ip = (255 - ty) * 256;
				op *= 3;
				for (tx = 0; tx < 256; tx++)
				{
					img[op+0] = m_lut[frame_buf[ip]*3+0];
					img[op+1] = m_lut[frame_buf[ip]*3+1];
					img[op+2] = m_lut[frame_buf[ip]*3+2];
					ip++;
					op += 3;
				}
			}
		}
	}

	free(frame_buf);
	if (frame_fp != NULL)
		fclose(frame_fp);

	return SUCCESS;
}
// end of get_frame_image

// ****************************************************************
// ****************************************************************

int RPFRenderer::get_rgb_image(CString filename, BOOL is_cib, int startx, int starty, int width, int height, BYTE *img_data)
{
	BYTE *img;
	int x, y, inp, outp, rslt;

	rslt = SUCCESS;

	if ((startx >= 1536) || (starty >= 1536))
	{
		FillMemory(img_data, width * height * 3, 0);
		return SUCCESS;
	}

	img = (BYTE*) malloc(1536 * 1536 * 3);
	if (img == NULL)
	{
		ASSERT(0);
		return FAILURE;
	}

	rslt = get_frame_image(filename, is_cib, img);
	if (rslt != SUCCESS)
	{
		free(img);
		ASSERT(0);
		return FAILURE;
	}
	outp = 0;
	for (y=starty; y<starty+height; y++)
	{
		for (x=startx; x<startx+width; x++)
		{
			inp = (y * 1536) + x;
			inp *= 3;
			if ((y<1536) && (x<1536))
				memcpy(&(img_data[outp]), &(img[inp]), 3);
			else
				FillMemory(&(img_data[outp]), 3, 0);
			outp += 3;
		}
	}

	free(img);
	return rslt;
}
// end of get_rgb_image

// ****************************************************************
// ****************************************************************

int RPFRenderer::read_and_display_subframe(FILE* fp, BOOL is_cib, int subframe_index, cadrg_cib_base &cadrg, BYTE *frame_buf)
{
   /*
    *  Determine if the subframe is present.  If not, return SUCCESS.
    */
   BOOL sf_present;
   UINT4 offset;
   int rslt;
   CString source = "CADRG";
   if (is_cib)
	   source = "CIB";

   rslt = cadrg.subframe_present(subframe_index, &sf_present, &offset);
   if (rslt != SUCCESS)
   {
      // subframe_present failed
	  AfxMessageBox("subframe_present failed");
      return FAILURE;
   }

//   if (!sf_present)
//   {
//      // subframe not present
//	  AfxMessageBox("subframe not present");
//      return SUCCESS;
//   }

   /*
    *  extract the primary color table for the source - note that they are 
    *  different formats for CADRG and CIB
    */
   RPF_color_table_and_histogram* table;
   BOOL found;
   rslt = get_primary_lut(cadrg, source, m_lut, table, &found);
   if (rslt != SUCCESS)
   {
      // get_primary_lut failed
 	  AfxMessageBox("get_primary_lut failed");
     return FAILURE;
   }
   if (found == FALSE)
   {
      // primary lut not found
 	  AfxMessageBox("primary lut not found");
      return FAILURE;
   }

   /*
   *  There are 217 lut entries instead of 216 when there is transparency.
   *  In that case ignore the one dealing with transparency (i.e. the 
   *  last one).
   */
   m_num_lut_entries = (table->m_num_color_records > 216) ? 216 : table->m_num_color_records;

   /*
    *  determine if there is a compression section in the subframe
    *  by searching the location section
    */
   BOOL compression_section_present = FALSE;
   int compression_id = 0;  // dummy value
   {
      const UINT2 num_components = cadrg.get_location()->m_number_of_component_location_records;
      const RPF_component_location_record* component_table = cadrg.get_location()->m_component_location_table;
      int i;
      for (i=0; i<num_components; i++)
      {
         if (component_table[i].m_component_id == 
            ID_COMPRESSION_SECTION_SUBHEADER)
         {
            compression_section_present = TRUE;

            /*
             *  assign the compression algorithm id too since the compression
             *  section is present
             */
            compression_id = cadrg.get_compr_subhdr()->m_compression_algorithm_id;
            break;
         }
      }
   }

   rslt = get_uncompressed_image(&cadrg, subframe_index, fp, compression_section_present, compression_id, CADRG_SF_WIDTH, 
								CADRG_SF_HEIGHT, frame_buf);
   if (rslt != SUCCESS)
   {
      // read_uncompress_and_display_image failed
      return FAILURE;
   }
 
   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int RPFRenderer::get_primary_lut(cadrg_cib_base& frame, CString source, unsigned char lut[256*3], 
								 RPF_color_table_and_histogram*& table,  BOOL* found)
{
   if (get_primary_lut_table(frame, source, table, found) != SUCCESS)
   {
      // get_primary_lut_table
      return FAILURE;
   }

   if (!(*found))
   {
      return SUCCESS;
   }

   if (extract_lut_from_table(table, source, lut) != SUCCESS)
   {
      // extract_lut_from_table
      return FAILURE;
   }

   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int RPFRenderer::get_primary_lut_table(cadrg_cib_base& frame, CString source, 
   RPF_color_table_and_histogram*& table, BOOL* found)
{
   UINT1 num_luts;
   const RPF_colormap_subsection* color_info;
   {
      const RPF_color_grayscale_section_subheader* const subhead =
         frame.get_color_section_subheader();
      num_luts = subhead->m_number_of_color_offset_records;
      color_info = frame.get_colormap_subsection();
   }

   *found = FALSE;
   for (UINT1 i=0; i<num_luts; i++)
   {
      /*
       *  find the primary color table for the source
       */

      const RPF_color_offset_record* table_record = 
         &color_info->m_cm_offset_recs.m_recs[i];
      table = &color_info->m_cm_array.m_tables[i];

      if (table->m_num_color_records != 216 && table->m_num_color_records != 217)
         continue;

      if (!source.Compare("CADRG"))
      {
         if (table_record->m_color_table_id == RPF_COLOR_TABLE_RGBM)
         {
            *found = TRUE;
            break;
         }
      }
      else if (!source.Compare("CIB"))
      {
         if (table_record->m_color_table_id == RPF_COLOR_TABLE_MONO)
         {
            *found = TRUE;
            break;
         }
      }
      else
      {
         // invalid source
 		  AfxMessageBox("invalid source in get_primary_lut_table");
         return FAILURE;
      }
   }

   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int RPFRenderer::extract_lut_from_table(RPF_color_table_and_histogram*& table,
   CString source, unsigned char lut[256*3])
{
   if (!source.Compare("CADRG"))
   {
      for (UINT4 j=0; j<table->m_num_color_records; j++)
      {
         lut[j*3] = table->m_color_elems[table->m_color_element_length*j];
         lut[j*3+1] = table->m_color_elems[table->m_color_element_length*j+1];
         lut[j*3+2] = table->m_color_elems[table->m_color_element_length*j+2];
      }
   }
   else if (!source.Compare("CIB"))
   {
      for (UINT4 j=0; j<table->m_num_color_records; j++)
      {
         lut[j*3] = table->m_color_elems[table->m_color_element_length*j];
         lut[j*3+1] = table->m_color_elems[table->m_color_element_length*j];
         lut[j*3+2] = table->m_color_elems[table->m_color_element_length*j];
      }
   }
   else
   {
      // invalid source
 		AfxMessageBox("invalid source in extract_lut_from_table");
      return FAILURE;
   }

   return SUCCESS;
}


// ****************************************************************
// ****************************************************************

// compression id is valid only if compressed is TRUE
int RPFRenderer::get_uncompressed_image(cadrg_cib_base* cdb, int subframe_index, FILE* fp, BOOL compressed, 
										int compression_id, UINT4 image_width, UINT4 image_height, unsigned char* image)
{
   RPF_spectral_band_table subframe;

   /*
    *  read the (possibly compressed) subframe
    */
   if (cdb->read_subframe(fp, subframe_index, &subframe, 
      cdb->little_endian()) != SUCCESS)
   {
      // read_subframe failed
//  		AfxMessageBox("read_subframe failed");
     return FAILURE;
   }

   /*
    * if uncompressed
    */
   if (!compressed)
   {
      /*
       *  it would be better here to optimize so that uncompressed images
       *  wouldn't have to be copied, but ...
       */
      memcpy(image, subframe.m_image_codes, image_width*image_height);
   }
   else if (compression_id == RPF_COMPRESSION_VQ)
   {
      /*
       *  decompress the subframe
       */
      RPF_compression_lookup_table_array* lu = &cdb->get_compr_lookup()->m_compression_lookup_table_array;
      if (cdb->vq_decompress_spectral_band_table(image,
         subframe.m_image_codes, lu->m_tables[0].m_bits, 
         lu->m_tables[1].m_bits, lu->m_tables[2].m_bits, 
         lu->m_tables[3].m_bits) != SUCCESS)
      {
         // decompress failed
   		AfxMessageBox("decompress failed");
        return FAILURE;
      }
   }
   else
   {
      // unsupported compression method
    AfxMessageBox("unsupported compression method");
     return FAILURE;
   }

   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

#define DOUBLE2INT(d, i) {double t = ((d) + 6755399441055744.0); i = *((int *)(&t));} 

int RPFRenderer::resize_pixmap(const int old_width, const int old_height, 
   const unsigned char* const old_image, const int new_width, const int new_height, 
   unsigned char* const new_image, const int new_padded_width,
   const int new_start_row, const int new_end_row)
{
   int row,      /* row in the resized pixmap */
       col;      /* column in the resized pixmap */
   int old_row,  /* row in the original pixmap which corresponds to row */
       old_col;  /* column in the original pixmap which corresponds to column */

   // the old height/new height ratio
   const double height_factor = (double)old_height/(double)new_height;
   // the old width/new width ratio
   const double width_factor = (double)old_width/(double)new_width;

   // set rounding mode to round down
   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   UINT orig_control;
   _controlfp_s(&orig_control, _RC_DOWN, _MCW_RC);

   /*  
    *  for each pixel of the resized pixmap, determine which pixel in the
    *  original pixmap it "maps" to and use it.
    */
   for (row=new_start_row; row<=new_end_row; ++row) 
   {
      /*
       *  determine which row in the original pixmap "maps" to this row
       *  in the original pixmap
       */ 
      DOUBLE2INT(row*height_factor, old_row);

      ATLASSERT(old_row < old_height);

		int new_image_index = (row-new_start_row)*new_padded_width;
		const int old_row_times_old_width = old_row * old_width;
		double current_col = 0.0;
      for (col=0; col<new_width; col++) 
      {
         /*
          *  determine which column in the original pixmap "maps" to this 
          *  column in the original pixmap
          */ 
         DOUBLE2INT(current_col, old_col);
			current_col += width_factor;

         //ASSERT(old_col < old_width);

         new_image[new_image_index] = old_image[old_row_times_old_width+old_col];

			++new_image_index;
      }

      /*
       *  fill the pad bytes with BITMAP_BLACK
       */

		new_image_index = (row-new_start_row)*new_padded_width+new_width;
      for (col=new_width; col<new_padded_width; ++col)
      {
         new_image[new_image_index] = BITMAP_BLACK;
			++new_image_index;
      }
   }

   // restore orignal rounding mode
   UINT ui;
   _controlfp_s(&ui, orig_control, 0xfffff);

   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

BOOL RPFHelper::lon_in_range(double left_lon, double right_lon, double point_lon)
{
   if (left_lon <= right_lon)
   {
      if (point_lon < left_lon || right_lon < point_lon)
         return FALSE;
   }
   else
   {
      if (point_lon < left_lon && point_lon > right_lon)
         return FALSE;
   }

   return TRUE;    /* point in bounds */
} 

// ****************************************************************
// ****************************************************************

BOOL RPFHelper::region_intersect(double ll_A_lat, double ll_A_lon,
   double ur_A_lat, double ur_A_lon,
   double ll_B_lat, double ll_B_lon,
   double ur_B_lat, double ur_B_lon)
{
   if (ur_A_lat <= ll_B_lat)   /* region A completely below B */
      return FALSE;

   if (ll_A_lat >= ur_B_lat)   /* region A completely above B */
      return FALSE;

   /* if either region goes around the world, intersection exists */
   if (ll_A_lon == ur_A_lon || ll_B_lon == ur_B_lon)
      return TRUE;

   /* if right edge of region B is in region A */
   if (lon_in_range(ll_A_lon, ur_A_lon, ur_B_lon) == TRUE) 
      return TRUE;

   /* if left edge of region B is in region A */
   if (lon_in_range(ll_A_lon, ur_A_lon, ll_B_lon) == TRUE) 
      return TRUE;

   /* if right edge of region A is in region B */ 
   if (lon_in_range(ll_B_lon, ur_B_lon, ur_A_lon) == TRUE) 
      return TRUE;

   /* if left edge of region A is in region B */
   if (lon_in_range(ll_B_lon, ur_B_lon, ll_A_lon) == TRUE)
      return TRUE;

   return FALSE;
}

// ****************************************************************
// ****************************************************************

BOOL RPFHelper::region_enclose(double ll_A_lat, double ll_A_lon,
   double ur_A_lat, double ur_A_lon,
   double ll_B_lat, double ll_B_lon,
   double ur_B_lat, double ur_B_lon)
{
   /* region B completely below A */
   if (ur_B_lat <= ll_A_lat)
      return FALSE;

   /* region B completely above A */
   if (ll_B_lat >= ur_A_lat)
      return FALSE;

   /* if northern edge of B goes outside of A */
   if (ur_B_lat > ur_A_lat)
      return FALSE;

   /* if southern edge of B goes outside of A */
   if (ll_B_lat < ll_A_lat)
      return FALSE;

   /* if right edge of region B is outside of region A */
   if (lon_in_range(ll_A_lon, ur_A_lon, ur_B_lon) == FALSE) 
      return FALSE;

   /* if left edge of region B is outside of region A */
   if (lon_in_range(ll_A_lon, ur_A_lon, ll_B_lon) == FALSE) 
      return FALSE;

   return TRUE;
}

// ****************************************************************
// ****************************************************************

BOOL RPFHelper::is_east_of(double a, double b)
{
	double diff;

   /* (a>=b) and in same hemisphere, 0<=diff<= half_world 
      (a<=b) and in same hemisphere, 0>=diff>=-half_world
      (a>b) and in Opp hemispheres,  0< diff<= around_world 
      (a<b) and in Opp hemispheres,  0> diff>=-around_world */

   diff = a - b;

   if (diff < 0.0) /* convert diff to eastward distance from b to a */
      diff += 360.0;

   if (0.0 < diff && diff <= 180.0)
      return TRUE;

   return FALSE;
}

// ****************************************************************
// ****************************************************************

