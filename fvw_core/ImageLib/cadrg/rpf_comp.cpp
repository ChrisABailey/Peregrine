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

#include "file.h"
#include "rpf_defs.h"
#include "rpf_comp.h"
#include "rpf_lcl.h"
#include <math.h>

// ------------------------------------------------------------
// header section 
// ------------------------------------------------------------

int RPF_endianness::read(FILE* fp)
{
   fread(&m_endian_indicator, 1, 1, fp);

   /*
    *  set the little_endian indicator
    */
   if (m_endian_indicator == RPF_LITTLE_ENDIAN)
      m_little_endian = TRUE;
   else if (m_endian_indicator == RPF_BIG_ENDIAN)
      m_little_endian = FALSE;
   else
   {
      // invalid endian indicator
      return FAILURE;
   }

   return SUCCESS;
}

int RPF_header_section_excluding_endianness::read(FILE* fp, 
   BOOL little_endian)
{
   GET_UINT_2(fp, &m_header_section_length, little_endian);
   READ_RPF_STRING(m_file_name, fp);
   GET_UINT_1(fp, &m_new_replace_update_indicator);
   READ_RPF_STRING(m_governing_specification_number, fp);
   READ_RPF_STRING(m_governing_specification_date, fp);
   READ_RPF_STRING(m_security_classification, fp);
   READ_RPF_STRING(m_security_country_international_code, fp);
   READ_RPF_STRING(m_security_release_marking, fp);
   GET_UINT_4(fp, &m_location_section_location, little_endian);

   return SUCCESS;
}

int RPF_header_section::read(FILE* fp)
{
   if (m_endianness.read(fp) != SUCCESS || 
      m_hdr.read(fp, m_endianness.little_endian()) != SUCCESS)
   {
      // read failed
      return FAILURE;
   }

   return SUCCESS;
}

// ------------------------------------------------------------
// location section
// ------------------------------------------------------------

int RPF_component_location_record::read(FILE* fp, BOOL little_endian)
{
   GET_UINT_2(fp, &m_component_id, little_endian);
   GET_UINT_4(fp, &m_component_length, little_endian);
   GET_UINT_4(fp, &m_component_location, little_endian);

   return SUCCESS;
}

int RPF_location_section::read(FILE* fp, BOOL little_endian)
{
   long position_at_start_of_location_section;

   position_at_start_of_location_section = ftell(fp);
   if (position_at_start_of_location_section == -1)
   {
      // ftell failed
      return FAILURE;
   }
   
   GET_UINT_2(fp, &m_location_section_length, little_endian);
   GET_UINT_4(fp, &m_component_location_table_offset, little_endian);
   GET_UINT_2(fp, &m_number_of_component_location_records, little_endian);
   GET_UINT_2(fp, &m_component_location_record_length, little_endian);
   GET_UINT_4(fp, &m_component_aggregate_length, little_endian);

   if (m_number_of_component_location_records > 0)
   {
      if (allocate_table(m_number_of_component_location_records) != SUCCESS)
      {
         // allocate_table failed
         return FAILURE;
      }

      /*
       *  seek to the beginning of the component location table
       */
      FSEEK(fp, 
         position_at_start_of_location_section + m_component_location_table_offset,
         SEEK_SET);

      long pos;
      int i;
      for (i=0; i<m_number_of_component_location_records; i++)
      {
         pos = ftell(fp);
         if (pos == -1)
         {
            // ftell failed
            return FAILURE;
         }

         if (m_component_location_table[i].read(fp, little_endian) != SUCCESS)
         {
            // location table read failed
            return FAILURE;
         }

         /*
          *  seek for backwards compatibility
          */
         FSEEK(fp, pos + m_component_location_record_length, SEEK_SET);
      }
   }
   else 
      return FAILURE;

   return SUCCESS;
}

int RPF_location_section::allocate_table(UINT2 num_comp_location_recs)
{
   m_component_location_table = 
      new RPF_component_location_record[num_comp_location_recs];
   if (m_component_location_table == NULL)
     return FAILURE;
   else
     return SUCCESS;
}

void RPF_location_section::deallocate_table(void)
{
   if (m_component_location_table != NULL)
   {
      delete[] m_component_location_table;
      m_component_location_table = NULL;
   }
}

// ------------------------------------------------------------
// coverage section
// ------------------------------------------------------------

int RPF_coverage_section::read(FILE* fp, BOOL little_endian)
{
   GET_REAL_8(fp, &m_nw_lat, little_endian);
   GET_REAL_8(fp, &m_nw_lon, little_endian);
   GET_REAL_8(fp, &m_sw_lat, little_endian);
   GET_REAL_8(fp, &m_sw_lon, little_endian);
   GET_REAL_8(fp, &m_ne_lat, little_endian);
   GET_REAL_8(fp, &m_ne_lon, little_endian);
   GET_REAL_8(fp, &m_se_lat, little_endian);
   GET_REAL_8(fp, &m_se_lon, little_endian);
   GET_REAL_8(fp, &m_north_south_resolution, little_endian);
   GET_REAL_8(fp, &m_east_west_resolution, little_endian);
   GET_REAL_8(fp, &m_latitude_interval, little_endian);
   GET_REAL_8(fp, &m_longitude_interval, little_endian);

   return SUCCESS;
}

// ------------------------------------------------------------
// compression section
// ------------------------------------------------------------

// --------------------------------------------------------------------

int RPF_compression_section_subheader::read(FILE* fp, BOOL little_endian)
{
   GET_UINT_2(fp, &m_compression_algorithm_id, little_endian);
   GET_UINT_2(fp, &m_number_of_compression_lookup_offset_records, 
      little_endian);
   GET_UINT_2(fp, &m_number_of_compression_parameter_offset_records, 
      little_endian);

   return SUCCESS;
}

int RPF_compression_lookup_offset_record::read(FILE* fp, 
   BOOL little_endian)
{
   GET_UINT_2(fp, &m_compression_lookup_table_id, little_endian);
   GET_UINT_4(fp, &m_number_of_compression_lookup_records, little_endian);
   GET_UINT_2(fp, &m_number_of_values_per_compression_lookup_record, 
      little_endian);
   GET_UINT_2(fp, &m_compression_lookup_value_bit_length, little_endian);
   GET_UINT_4(fp, &m_compression_lookup_table_offset, little_endian);

   return SUCCESS;
}

int RPF_compression_lookup_offset_record_array::create(UINT2 num_recs)
{
   m_recs = new RPF_compression_lookup_offset_record[num_recs];
   if (m_recs == NULL)
   {
       // memory allocation error
       return FAILURE;
   }

   return SUCCESS;
}

void RPF_compression_lookup_offset_record_array::destroy(void)
{
   if (m_recs != NULL)
   {
      delete[] m_recs;
      m_recs = NULL;
   }
}

// --------------------------------------------------------------------

int RPF_compression_lookup_table::create(UINT4 num_lookup_records, 
   UINT4 values_per_record, UINT2 bits_per_value)
{
   double num_bytes = 
      (double) num_lookup_records *
      (double) values_per_record *
      ((double)bits_per_value/8.0);

   m_num_bytes = (size_t) num_bytes;
 
   m_bits = new unsigned char[m_num_bytes];
   if (m_bits == NULL)
   {
      // memory allocation error
      return FAILURE;
   } 

   m_num_recs = num_lookup_records;
   m_values_per_record = values_per_record;
   m_bits_per_value = bits_per_value;

   return SUCCESS;
}

void RPF_compression_lookup_table::destroy(void)
{
   if (m_bits != NULL)
   {
      delete [] m_bits;
      m_bits = NULL;
   }
}

int RPF_compression_lookup_table::read(FILE* fp, BOOL little_endian)
{
   fread(m_bits, 1, m_num_bytes, fp);

   // should make sure that m_num_bytes matches the size in the file

   return SUCCESS;
}

int RPF_compression_lookup_table_array::create(UINT2 num_tables)
{
   m_tables = new RPF_compression_lookup_table[num_tables];
   if (m_tables == NULL)
   {
       // memory allocation error
       return FAILURE;
   }

   m_num_tables = num_tables;

   return SUCCESS;
}

void RPF_compression_lookup_table_array::destroy(void)
{
   if (m_tables != NULL)
   {
      delete[] m_tables;
      m_tables = NULL;
      m_num_tables = 0;
   }
}

// --------------------------------------------------------------------

int RPF_compression_lookup_subsection::read(FILE* fp, BOOL little_endian,
   int num_compression_lookup_offset_records)
{
   long pos_at_start;

   pos_at_start = ftell(fp);
   if (pos_at_start == -1)
   {
      // ftell failed
      return FAILURE;
   }

   // make sure to set number of records member
   m_num_lookup_offset_records = num_compression_lookup_offset_records;

   if (m_compression_lookup_offset_recs.create(num_compression_lookup_offset_records)
      != SUCCESS)
   {
      // allocation failed
      return FAILURE;
   }
   
   if (m_compression_lookup_table_array.create(num_compression_lookup_offset_records)
      != SUCCESS)
   {
      // allocation failed
      return FAILURE;
   }

   GET_UINT_4(fp, &m_compression_lookup_offset_table_offset, little_endian);
   GET_UINT_2(fp, &m_compression_lookup_table_offset_record_length, little_endian);

   FSEEK(fp, pos_at_start+m_compression_lookup_offset_table_offset, SEEK_SET);

   int i;
   long rec_pos;
   for (i=0 ; i<num_compression_lookup_offset_records; i++)
   {
      rec_pos = ftell(fp);
      if (rec_pos == -1)
      {
         // ftell failed
         return FAILURE;
      }

      if (m_compression_lookup_offset_recs.m_recs[i].read(fp, little_endian) != 
         SUCCESS)
      {
         // read failed
         return FAILURE;
      }

      /*
       *  seek - for backwards compatibility
       */
      FSEEK(fp, rec_pos+m_compression_lookup_table_offset_record_length, 
         SEEK_SET);
   }

   /*
    *  read the compression lookup tables
    */
   RPF_compression_lookup_offset_record* rec;
   for (i=0; i<num_compression_lookup_offset_records; i++)
   {
      rec = &m_compression_lookup_offset_recs.m_recs[i];
      if (m_compression_lookup_table_array.m_tables[i].create(
         rec->m_number_of_compression_lookup_records,
         rec->m_number_of_values_per_compression_lookup_record,
         rec->m_compression_lookup_value_bit_length) != SUCCESS)
      {
         // create failed
         return FAILURE;
      }    

      /*
       * need to seek here
       */
      FSEEK(fp, pos_at_start+rec->m_compression_lookup_table_offset, SEEK_SET);

      // NOTE: no need for the little_endian param here
      if (m_compression_lookup_table_array.m_tables[i].read(fp, little_endian) 
         != SUCCESS)
      {
         // read failed
         return FAILURE;
      }
   }

   return SUCCESS;
}

// ------------------------------------------------------------
// color/grayscale section
// ------------------------------------------------------------

// ------------------------------------------------------------

int RPF_color_grayscale_section_subheader::read(FILE* fp, 
   BOOL little_endian)
{
   GET_UINT_1(fp, &m_number_of_color_offset_records);
   GET_UINT_1(fp, &m_number_of_color_converter_offset_records);

   READ_RPF_STRING(m_external_color_file_name, fp);

   return SUCCESS;
}

// ------------------------------------------------------------

int RPF_color_offset_record::read(FILE* fp, BOOL little_endian)
{
   GET_UINT_2(fp, &m_color_table_id, little_endian);
   GET_UINT_4(fp, &m_number_of_color_records, little_endian);
   GET_UINT_1(fp, &m_color_element_length);
   GET_UINT_2(fp, &m_histogram_record_length, little_endian);
   GET_UINT_4(fp, &m_color_table_offset, little_endian);
   GET_UINT_4(fp, &m_histogram_table_offset, little_endian);

   return SUCCESS;
}

int RPF_colormap_offset_array::create(UINT1 num_recs)
{
   m_recs = new RPF_color_offset_record[num_recs];
   if (m_recs == NULL)
   {
      // memory allocation error
      return FAILURE;
   }

   m_num_recs = num_recs;

   return SUCCESS;
}

void RPF_colormap_offset_array::destroy(void)
{
   if (m_recs != NULL)
   {
      delete[] m_recs;
      m_recs = NULL;
      m_num_recs = 0;
   }
}

// ------------------------------------------------------------

int RPF_color_table_and_histogram::create(UINT2 table_id, UINT4 num_recs, 
   UINT1 color_element_length)
{
   m_table_id = table_id;

   m_color_elems = new unsigned char[num_recs*color_element_length];
   if (m_color_elems == NULL)
   {
      // memory allocation error
      return FAILURE;
   }

   m_num_color_records = num_recs;
   m_color_element_length = color_element_length;

   return SUCCESS;
}

void RPF_color_table_and_histogram::destroy(void)
{
   if (m_color_elems != NULL)
   {
      delete [] m_color_elems;
      m_color_elems = NULL;
   }

   m_num_color_records = 0;
   m_color_element_length = 0;

   if (m_histogram != NULL)
   {
      delete [] m_histogram;
      m_histogram = NULL;
   }
}

int RPF_color_table_and_histogram::read(FILE* fp, BOOL little_endian)
{
   if (fread(m_color_elems, m_color_element_length, m_num_color_records, 
      fp) != m_num_color_records)
   {
      // fread failed
      return FAILURE;
   }

   return SUCCESS;
}

// ------------------------------------------------------------

int RPF_color_table_array::create(UINT1 num_tables)
{
   m_tables = new RPF_color_table_and_histogram[num_tables];
   if (m_tables == NULL)
   {
       // memory allocation error
       return FAILURE;
   }

   m_num_tables = num_tables;

   return SUCCESS;
}

void RPF_color_table_array::destroy(void)
{
   if (m_tables != NULL)
   {
      delete[] m_tables;
      m_tables = NULL;
      m_num_tables = 0;
   }
}

// ------------------------------------------------------------

int RPF_colormap_subsection::create(UINT1 num_offset_records)
{
   if (m_cm_offset_recs.create(num_offset_records) != SUCCESS)
   {
      // create failed
      return FAILURE;
   }
   if (m_cm_array.create(num_offset_records) != SUCCESS)
   {
      // create failed
      return FAILURE;
   }

   m_num_recs = num_offset_records;

   return SUCCESS;
}

int RPF_colormap_subsection::read(FILE* fp, BOOL little_endian)
{
   long pos_at_start_of_colormap_subsection = ftell(fp);
   if (pos_at_start_of_colormap_subsection == -1)
   {
      // ftell failed
      return FAILURE;
   }

   GET_UINT_4(fp, &m_colormap_offset_table_offset, little_endian);
   GET_UINT_2(fp, &m_color_offset_record_length, little_endian);

   /*
    *  seek to the color offset records
    */
   FSEEK(fp, pos_at_start_of_colormap_subsection + m_colormap_offset_table_offset, 
      SEEK_SET);

   // read the offset records
   long pos;
   UINT1 rec;
   for (rec=0; rec<m_num_recs; rec++)
   {
      pos = ftell(fp);
      if (pos == -1)
      {
         // ftell failed
         return FAILURE;
      }

      if (m_cm_offset_recs.m_recs[rec].read(fp, little_endian) != SUCCESS)
      {
         // read failed
         return FAILURE;
      }

      /*
       *  seek to the end of the record
       */
      FSEEK(fp, pos + m_color_offset_record_length, SEEK_SET);
   }

   /*
    *  read the color tables and histograms
    */
   UINT1 table;
   RPF_color_offset_record* ptr;
   for (table=0; table<m_num_recs; table++)
   {
      ptr = &m_cm_offset_recs.m_recs[table];

      if (m_cm_array.m_tables[table].create(ptr->m_color_table_id, 
         ptr->m_number_of_color_records, ptr->m_color_element_length) != SUCCESS)
      {
         // create failed
         return FAILURE;
      }

      /*
       *  seek to color table
       */
      if (fseek(fp, 
        pos_at_start_of_colormap_subsection+ptr->m_color_table_offset,
        SEEK_SET) != 0)
      {
         // fseek failed
         return FAILURE;
      }

      if (m_cm_array.m_tables[table].read(fp, little_endian) != SUCCESS)
      {
         // read failed
         return FAILURE;
      }

      //
      // read the corresponding histogram (if any)
      //
      if (ptr->m_histogram_table_offset != RPF_UINT_4_NULL_VALUE)
      {
         //
         // allocate the histogram elements
         //
         {
            UINT4* histo_ptr = 
               new UINT4[ptr->m_number_of_color_records];
            if (histo_ptr == NULL)
            {
               // mem error
               return FAILURE;
            }

            m_cm_array.m_tables[table].m_histogram = histo_ptr;
         }

         /*
          *  seek to histogram table
          */
         if (fseek(fp, 
           pos_at_start_of_colormap_subsection+ptr->m_histogram_table_offset,
           SEEK_SET) != 0)
         {
            // fseek failed
            return FAILURE;
         }

         //
         // read the histogram elements
         //
         for (UINT1 i=0; i < ptr->m_number_of_color_records; i++)
         {
            GET_UINT_4(fp, &m_cm_array.m_tables[table].m_histogram[i], 
               little_endian);

            if (ptr->m_histogram_record_length > 4)
            {
               if (fseek(fp, ptr->m_histogram_record_length-4, 
                  SEEK_CUR) != 0)
               {
                  // fseek failed
                  return FAILURE;
               }
            }
         }
      }
   }

   return SUCCESS;
}

// ------------------------------------------------------------
// ------------------------------------------------------------

int RPF_image_description_subheader::read(FILE* fp , BOOL little_endian)
{
   GET_UINT_2(fp, &m_number_of_spectral_groups, little_endian);
   GET_UINT_2(fp, &m_number_of_subframe_tables, little_endian);
   GET_UINT_2(fp, &m_number_of_spectral_band_tables, little_endian);
   GET_UINT_2(fp, &m_number_of_spectral_band_lines_per_image_row, 
      little_endian);
   GET_UINT_2(fp, &m_number_of_subframes_in_east_west_direction, 
      little_endian); 
   GET_UINT_2(fp, &m_number_of_subframes_in_north_south_direction, 
      little_endian); 
   GET_UINT_4(fp, &m_number_of_output_columns_per_subframe, little_endian); 
   GET_UINT_4(fp, &m_number_of_output_rows_per_subframe, little_endian); 
   GET_UINT_4(fp, &m_subframe_mask_table_offset, little_endian); 
   GET_UINT_4(fp, &m_transparency_mask_table_offset, little_endian); 

   return SUCCESS;
}

// ------------------------------------------------------------

int RPF_mask_subheader::read_transparent_pixel_code(FILE* fp)
{
   if (fread(m_transparent_output_pixel_code, 1, m_length_in_bytes, fp) !=
      m_length_in_bytes)
   {
      // fread failed
      return FAILURE;
   }

   return SUCCESS;
}

int RPF_mask_subheader::read(FILE* fp, BOOL little_endian)
{
   GET_UINT_2(fp, &m_subframe_sequence_record_length, little_endian);
   GET_UINT_2(fp, &m_transparency_sequence_record_length, little_endian);

   GET_UINT_2(fp, &m_transparent_output_pixel_code_length, little_endian);

   m_length_in_bytes = m_transparent_output_pixel_code_length/8 +
     (m_transparent_output_pixel_code_length%8 == 0 ? 0 : 1);

   if (m_transparent_output_pixel_code_length != 0)
   {
      if (allocate_transparent_pixel_code(m_length_in_bytes) != SUCCESS)
      {
         // allocate_transparent_pixel_code failed
         return FAILURE;
      }

      if (read_transparent_pixel_code(fp) != SUCCESS)
      {
         // read_transparent_pixel_code failed
         return FAILURE;
      }
   }

   return SUCCESS;
}

int RPF_mask_subheader::allocate_transparent_pixel_code(size_t length_in_bytes)
{
   m_transparent_output_pixel_code = 
      new unsigned char[length_in_bytes];
   if (m_transparent_output_pixel_code == NULL)
   {
      // memory allocation error
      return FAILURE;
   }

   return SUCCESS;
}

void RPF_mask_subheader::deallocate_transparent_pixel_code(void)
{
   if (m_transparent_output_pixel_code != NULL)
   {
      delete m_transparent_output_pixel_code;
      m_transparent_output_pixel_code = NULL;
   }
}

// ------------------------------------------------------------

int RPF_mask_table::create(UINT2 num_spectral_groups, 
   UINT4 num_subframes_east_west, UINT4 num_subframes_north_south,
   UINT2 sequence_record_length)
{
   size_t num_recs = 
      num_spectral_groups * num_subframes_east_west * num_subframes_north_south;
   m_subframe_offsets = new UINT4[num_recs];

   if (m_subframe_offsets == NULL)
   {
      // memory allocation error
      return FAILURE;
   }

   m_num_spectral_groups = num_spectral_groups;
   m_subframes_east_west = num_subframes_east_west;
   m_subframes_north_south = num_subframes_north_south;
   m_sequence_record_length = sequence_record_length;

   return SUCCESS;
}

void RPF_mask_table::destroy(void)
{
   if (m_subframe_offsets != NULL)
   {
      delete m_subframe_offsets;
      m_subframe_offsets = NULL;
   }
}

int RPF_mask_table::read(FILE* fp, BOOL little_endian)
{
   UINT2 group;
   UINT4 row, col;
   BOOL need_to_seek;
   UINT2 amount_to_seek;

   need_to_seek = (m_sequence_record_length != 4) ? TRUE : FALSE;
   if (need_to_seek)
      amount_to_seek = m_sequence_record_length - 4;

   UINT4* ptr = m_subframe_offsets;
   for (group=0; group<m_num_spectral_groups; group++)
   {
      for (row=0; row<m_subframes_north_south; row++)
      {
         for (col=0; col<m_subframes_east_west; col++)
         {
            GET_UINT_4(fp, ptr, little_endian);
            ptr++;

            /*
             *  seek to the end of the record if needed
             */
            if (need_to_seek)
            {
               if (fseek(fp, amount_to_seek, SEEK_CUR) != 0)
               {
                  // fseek failed
                  return FAILURE;
               }
            }
         }
      }
   }

   return SUCCESS;
}

// ------------------------------------------------------------

int RPF_mask_subsection::read(FILE* fp, BOOL little_endian,
   UINT4 subframe_mask_table_offset, UINT4 transparency_mask_table_offset,
   UINT2 number_of_spectral_groups, UINT2 num_subframes_east_west,
   UINT2 num_subframes_north_south)
{
   long pos_at_start_of_mask_subsection;

   pos_at_start_of_mask_subsection = ftell(fp);
   if (pos_at_start_of_mask_subsection == -1)
   {
      // ftell failed
      return FAILURE;
   }

   if (m_subhdr.read(fp, little_endian) != SUCCESS)
   {
      // read failed
      return FAILURE;
   }

   if (subframe_mask_table_offset != RPF_UINT_4_NULL_VALUE &&
      m_subhdr.m_subframe_sequence_record_length != 0)
   {
      /*
       *  seek to the subframe mask table
       */
      if (fseek(fp, pos_at_start_of_mask_subsection+subframe_mask_table_offset,
         SEEK_SET) != 0)
      {
         // fseek failed
         return FAILURE;
      }

      if (m_subframe_mask_table.create(number_of_spectral_groups, 
         num_subframes_east_west, num_subframes_north_south, 
         m_subhdr.m_subframe_sequence_record_length) != SUCCESS)
      {
         // create failed
         return FAILURE;
      }

      if (m_subframe_mask_table.read(fp, little_endian) != SUCCESS)
      {
         // read failed
         return FAILURE;
      }
   }

   if (transparency_mask_table_offset != RPF_UINT_4_NULL_VALUE &&
      m_subhdr.m_transparency_sequence_record_length != 0)
   {
      /*
       *  seek to the subframe mask table
       */
      if (fseek(fp, 
         pos_at_start_of_mask_subsection + transparency_mask_table_offset,
         SEEK_SET) != 0)
      {
         // fseek failed
         return FAILURE;
      }

      if (m_transparency_mask_table.create(number_of_spectral_groups, 
         num_subframes_east_west, num_subframes_north_south, 
         m_subhdr.m_transparency_sequence_record_length) != SUCCESS)
      {
         // create failed
         return FAILURE;
      }

      if (m_transparency_mask_table.read(fp, little_endian) != SUCCESS)
      {
         // read failed
         return FAILURE;
      }
   }

   return SUCCESS;
}

// ------------------------------------------------------------

int RPF_image_display_parameters_subheader::read(FILE* fp, 
   BOOL little_endian)
{
   GET_UINT_4(fp, &m_number_of_image_rows, little_endian);
   GET_UINT_4(fp, &m_number_of_image_codes_per_row, little_endian);
   GET_UINT_1(fp, &m_image_code_bit_length);

   return SUCCESS;
}

// ------------------------------------------------------------

int RPF_spectral_band_table::create(UINT4 num_image_rows, 
   UINT4 num_image_codes_per_row, UINT1 image_code_bit_length)
{
   size_t num_bytes; 

   /*
    *  NOTE: an image_code_bit_length of 0 indicates a variable-length
    *  image code.  This case is not handled.
    */ 

   num_bytes = (size_t) ceil((num_image_rows*num_image_codes_per_row) * 
      ((double)image_code_bit_length/8.0));

   m_num_bytes = num_bytes;

   m_image_codes = new unsigned char[num_bytes];
   if (m_image_codes == NULL)
   {
      // memory allocation error
      return FAILURE;
   }

   m_num_image_rows = num_image_rows;
   m_num_image_codes_per_row = num_image_codes_per_row;
   m_image_code_bit_length = image_code_bit_length;

   return SUCCESS;
}

void RPF_spectral_band_table::destroy(void)
{
   if (m_image_codes != NULL)
   {
      delete [] m_image_codes;
      m_image_codes = NULL;

      // set other vars to 0 ?
   }
}

// create must have been called to do this
int RPF_spectral_band_table::read(FILE* fp, BOOL little_endian)
{
   if (fread(m_image_codes, 1, m_num_bytes, fp) != m_num_bytes)
   {
      // fread failed
      return FAILURE;
   }

   return SUCCESS;
}
