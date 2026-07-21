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



#include "stdafx.h"

#include "cdb_base.h"
#include "rpf_lcl.h"
#include "imgdisp.h"


// ------------------------------------------------------------------
// ------------------------------------------------------------------

#define CADRG_COMPRESSED_SF_WIDTH    64
#define CADRG_COMPRESSED_SF_HEIGHT   64

// ------------------------------------------------------------------
// ------------------------------------------------------------------

int nitf_rpf_file::read_header(FILE* fp)
{
   char str[4+1];
   if (fread(str, 1, 4, fp) != 4)
   {
      // fread failed
      return FAILURE;
   }
   str[4] = '\0';

   /*
    *  be sure to reset the file pointer to the start of the file
    */
   if (fseek(fp, 0, SEEK_SET) != 0)
   {
      // fseek failed
      return FAILURE;
   }

   /*
    *  if the first 4 bytes of the file are "NITF", then it is wrapped in
    *  an NITF header
    */
   if (strcmp(str, "NITF") == 0)
   {
      BOOL rpf_header_present;

      if (m_nitf_header.read(fp, &rpf_header_present, &m_endianness, &m_hdr)
         != SUCCESS)
      {
         // read failed
         return FAILURE;
      }

      if (!rpf_header_present)
      {
         // rpf header not present
         return FAILURE;
      }
   }
   else
   {
      if (m_endianness.read(fp) != SUCCESS)
      {
         // read failed
         return FAILURE;
      }
      if (m_hdr.read(fp, m_endianness.little_endian()) != SUCCESS)
      {
         // read failed
         return FAILURE;
      }
   }

   return SUCCESS;
}

// ------------------------------------------------------------------

int cadrg_cib_base::vq_decompress_spectral_band_table(unsigned char* img,
  const unsigned char* image_codes, 
  const unsigned char* compr_lookup_table1, 
  const unsigned char* compr_lookup_table2,
  const unsigned char* compr_lookup_table3,
  const unsigned char* compr_lookup_table4)
{
   const UINT4* const lookup_table1 = (UINT4*) compr_lookup_table1;
   const UINT4* const lookup_table2 = (UINT4*) compr_lookup_table2;
   const UINT4* const lookup_table3 = (UINT4*) compr_lookup_table3;
   const UINT4* const lookup_table4 = (UINT4*) compr_lookup_table4;

   unsigned char* img_ptr;  // was 'register' (storage class removed in C++17)

   const unsigned int img_width = CADRG_SF_WIDTH;
   const unsigned int img_width_x_2 = CADRG_SF_WIDTH*2;
   const unsigned int img_width_x_3 = CADRG_SF_WIDTH*3;

   /*
    *  set img_ptr to point to the last 4 rows in the pixmap
    */
   img_ptr = img + img_width*(CADRG_SF_HEIGHT-4);

   /*
    *  extract and decompress all image codes into a pixmap
    */
   int row, col;
   for (row=0; row<CADRG_COMPRESSED_SF_HEIGHT; ++row)
   { 
      for (col=0; col<CADRG_COMPRESSED_SF_WIDTH; col += 2)
      {
         /*
          *  extract 2 12-bit codes (into code1 and code2)
          */
         const UINT4 code_1 = (*image_codes << 4) | ((*(image_codes+1)) >> 4);
         const UINT4 code_2 = ((*(image_codes+1) & 0x0F) << 8) | (*(image_codes + 2));

         /*
          *  For a particular image code, there is an entry in each of the 
          *  four tables giving a 4-pixel row value corresponding to the
          *  image code.  The decompressed pixels from the four compression
          *  tables can be combined to form a 4x4 block of pixels.
          */

         /*
          *  fill the 4x4 pixel block in the image for code 1
          */
         (*(UINT4*)img_ptr) = lookup_table4[code_1];
         (*(UINT4*)(img_ptr + img_width)) = lookup_table3[code_1];
         (*(UINT4*)(img_ptr + img_width_x_2)) = lookup_table2[code_1];
         (*(UINT4*)(img_ptr + img_width_x_3)) = lookup_table1[code_1];

         /*
          *  fill the 4x4 pixel block in the image for code 2
          */
         (*(UINT4*)(img_ptr + 4)) = lookup_table4[code_2];
         (*(UINT4*)(img_ptr + img_width + 4)) = lookup_table3[code_2];
         (*(UINT4*)(img_ptr + img_width_x_2 + 4)) = lookup_table2[code_2];
         (*(UINT4*)(img_ptr + img_width_x_3 + 4)) = lookup_table1[code_2];

         img_ptr += 8;
         image_codes += 3;
      }

      img_ptr -= img_width*5;

   }

   return SUCCESS;
}

// ------------------------------------------------------------------
// ------------------------------------------------------------------

int cadrg_cib_base::read_subframe(FILE* fp, int subframe_index, 
   RPF_spectral_band_table* subframe, BOOL little_endian)
{
   UINT4 spatial_data_offset;
   BOOL offset_from_spatial_data_read = FALSE;
   UINT4 offset_from_spatial_data;
   UINT4 subframe_offset;

   /*
    *  see if the indicated subframe is present and get its offset if possible
    */
   BOOL sf_present;
   subframe_present(subframe_index, &sf_present, &offset_from_spatial_data);
   if (!sf_present)
   {
      // subframe not present
//    	AfxMessageBox("subframe not present in read_subframe()");
      return FAILURE;
   }

   /*
    * see whether the offset was read
    */
   offset_from_spatial_data_read = 
      (offset_from_spatial_data != RPF_UINT_4_NULL_VALUE) ? TRUE : FALSE;

   /*
    *  find the spatial data offset
    */
   BOOL spatial_data_offset_found = FALSE;
   int rec;
   for (rec=0; rec<get_location()->m_number_of_component_location_records;
      rec++)
   {
      if (get_location()->m_component_location_table[rec].m_component_id ==
         ID_SPATIAL_DATA_SUBSECTION)
      {
         spatial_data_offset = 
           get_location()->m_component_location_table[rec].m_component_location;
         spatial_data_offset_found = TRUE;
         break;
      }
   }
   if (!spatial_data_offset_found)
   {
      // spatial data offset not found
     	AfxMessageBox("spatial data offset not found in read_subframe()");
     return FAILURE;
   }

   UINT4 num_image_rows = get_img_disp_params()->m_number_of_image_rows;
   UINT4 num_image_codes_per_row = 
      get_img_disp_params()->m_number_of_image_codes_per_row;
   UINT1 image_code_bit_length = 
      get_img_disp_params()->m_image_code_bit_length;

   /*
    *  NOTE: an image code bit length of 0 indicates a variable length bit
    *  code which is not supported
    */
   if (image_code_bit_length == 0)
   {
      // var. length bit code
     	AfxMessageBox("var bit code in read_subframe()");
     return FAILURE;
   }

   if (subframe->create(num_image_rows, num_image_codes_per_row,
      image_code_bit_length) != SUCCESS)
   {
      // create failed
    	AfxMessageBox("create failed in read_subframe()");
     return FAILURE;
   }

   /*
    *  calculate the subframe offset
    */
   if (offset_from_spatial_data_read)
   {
      subframe_offset = spatial_data_offset + offset_from_spatial_data;
   }
   else
   {
      subframe_offset = 
         spatial_data_offset + subframe_index*(UINT4)(subframe->m_num_bytes);
   }

   if (fseek(fp, subframe_offset, SEEK_SET) != 0)
   {
      // fseek failed
   	AfxMessageBox("fseek failed in read_subframe()");
      return FAILURE;
   }

   if (subframe->read(fp, little_endian) != SUCCESS)
   {
      // read failed
   	AfxMessageBox("read failed in read_subframe()");
     return FAILURE;
   }

   return SUCCESS;
}

// ------------------------------------------------------------------
// ------------------------------------------------------------------

/*
 *  Note: if there is no subframe mask table, this function does NOT figure 
 *  out the offset.  It sets subframe_present to TRUE and sets offset to
 *  RPF_UINT_4_NULL_VALUE.
 */
int cadrg_cib_base::subframe_present(int subframe_index, BOOL* subframe_present,
   UINT4* offset)
{
   if (subframe_index >= CADRG_FRAME_SIZE_IN_SF)
   {
      // invalid subframe index
      return FAILURE;
   }

   /*
    *  if the subframe mask table is present, then get its offset
    */
   if (get_img_subhdr()->m_subframe_mask_table_offset != RPF_UINT_4_NULL_VALUE
      && get_mask_subsection()->m_subhdr.m_subframe_sequence_record_length != 0)
   {
      *offset=
         get_mask_subsection()->m_subframe_mask_table.m_subframe_offsets[subframe_index]; 

      *subframe_present = (*offset == RPF_UINT_4_NULL_VALUE) ? FALSE : TRUE;
   }
   else
   {
      *subframe_present = TRUE;
      *offset = RPF_UINT_4_NULL_VALUE;
   }
      
   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int cadrg_cib_base::transparency_present(int subframe_index, 
   BOOL* transparency_present)
{
   if (subframe_index >= CADRG_FRAME_SIZE_IN_SF)
   {
      // invalid subframe index
      return FAILURE;
   }

   /*
    *  if the subframe mask table is present, then get its offset
    */
   if ((get_img_subhdr()->m_transparency_mask_table_offset != 
         RPF_UINT_4_NULL_VALUE)
      && 
         (get_mask_subsection()->m_subhdr.m_transparency_sequence_record_length 
        != 0))
   {
      UINT4 offset=
         get_mask_subsection()->m_transparency_mask_table.m_subframe_offsets[subframe_index]; 

      *transparency_present = (offset == RPF_UINT_4_NULL_VALUE) ? FALSE : TRUE;
   }
   else
   {
      *transparency_present = FALSE;
   }
      
   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int cadrg_cib_base::read_components(FILE* fp)
{
   int i;
   RPF_component_id id;
   UINT4 location;

   for (i=0; i<get_location()->m_number_of_component_location_records; i++)
   {
      id = get_location()->m_component_location_table[i].m_component_id;
      location = 
         get_location()->m_component_location_table[i].m_component_location;

      if (id == ID_COVERAGE_SECTION_SUBHEADER)
      {
         FSEEK(fp, location, SEEK_SET);
         if (get_coverage()->read(fp, little_endian()) != SUCCESS)
         {
            // read_coverage_section failed
            return FAILURE;
         }
      }
      else if (id == ID_COMPRESSION_SECTION_SUBHEADER)
      {
         FSEEK(fp, location, SEEK_SET);
         if (get_compr_subhdr()->read(fp, little_endian()) != SUCCESS)
         {
            // read_compression_section_subheader failed
            return FAILURE;
         }
      }
      else if (id == ID_COMPRESSION_LOOKUP_SUBSECTION)
      {
         FSEEK(fp, location, SEEK_SET);
         if (get_compr_lookup()->read(fp, little_endian(), 
            get_compr_subhdr()->m_number_of_compression_lookup_offset_records)
               != SUCCESS)
         {
            // read_compression_lookup_subsection failed
            return FAILURE;
         }
      }
      else if (id == ID_IMAGE_DESCRIPTION_SUBHEADER)
      {
         FSEEK(fp, location, SEEK_SET);
         if (get_img_subhdr()->read(fp, little_endian()) != SUCCESS)
         {
            // read_compression_lookup_subsection failed
            return FAILURE;
         }
      }
      else if (id == ID_COLOR_GRAYSCALE_SECTION_SUBHEADER)
      {
         FSEEK(fp, location, SEEK_SET);
         if (get_color_section_subheader()->read(fp, little_endian()) != SUCCESS)
         {
            // read_color_grayscale_section_subheader failed
            return FAILURE;
         }
      }
      else if (id == ID_COLORMAP_SUBSECTION)
      {
         FSEEK(fp, location, SEEK_SET);

         /*
          *  create
          */
         UINT1 num_recs = 
            get_color_section_subheader()->m_number_of_color_offset_records;
         if (get_colormap_subsection()->create(num_recs) != SUCCESS)
         {
            // create failed
            return FAILURE;
         }
         if (get_colormap_subsection()->read(fp, little_endian()) != SUCCESS)
         {
            // read_colormap_subsection failed
            return FAILURE;
         }
      }
      else if (id == ID_IMAGE_DISPLAY_PARAMETERS_SUBHEADER)
      {
         FSEEK(fp, location, SEEK_SET);
         if (get_img_disp_params()->read(fp, little_endian()) != SUCCESS)
         {
            // read_image_display_parameters_subheader failed
            return FAILURE;
         }
      }
      else if (id == ID_MASK_SUBSECTION)
      {
         FSEEK(fp, location, SEEK_SET);
         const RPF_image_description_subheader* hdr = get_img_subhdr();
         if (get_mask_subsection()->read(fp, little_endian(),
            hdr->m_subframe_mask_table_offset, hdr->m_transparency_mask_table_offset,
            hdr->m_number_of_spectral_groups,
            hdr->m_number_of_subframes_in_east_west_direction,
            hdr->m_number_of_subframes_in_north_south_direction) != SUCCESS)
         {
            // read_mask_subsection failed
            return FAILURE;
         }
      }
   }

   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int cadrg_cib_base::read(FILE* fp)
{
   if (read_header(fp) != SUCCESS)
   {
      // read_header failed
      return FAILURE;
   }

   /*
    *  use the location section location to skip to find the location section
    */
   FSEEK(fp, (long) get_hdr()->m_location_section_location, SEEK_SET);
   if (get_location()->read(fp, get_endianness()->little_endian()) != SUCCESS)
   {
      // read_location_section failed
      return FAILURE;
   }

   if (read_components(fp) != SUCCESS)
   {
      // read_components failed
      return FAILURE;
   }

   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

