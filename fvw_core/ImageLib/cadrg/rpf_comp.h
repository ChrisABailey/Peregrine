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



#ifndef RPF_COMP_H
#define RPF_COMP_H 1

/*
 *  This file defines RPF file components, i.e. the building blocks 
 *  of which RPF files are constructed.
 */

#include "rpf_util.h"

// ----------------------------------------------------------------------

class RPF_element
{

public:

   virtual ~RPF_element(void) {}
};

// ---------------------------------------------------------------
// header section 
// ---------------------------------------------------------------

class RPF_endianness
{
private:
   unsigned char m_endian_indicator;
   BOOL m_little_endian;

public:
   int read(FILE* fp);
   BOOL little_endian(void) const { return m_little_endian; }
};

class RPF_header_section_excluding_endianness : public RPF_element
{
public:
   RPF_header_section_excluding_endianness() :
         m_file_name(12),m_governing_specification_number(15),
         m_governing_specification_date(8), m_security_classification(1),
         m_security_country_international_code(2), m_security_release_marking(2) {}

   UINT2 m_header_section_length;
   rpf_string m_file_name;
   UINT1 m_new_replace_update_indicator;
   rpf_string m_governing_specification_number;
   rpf_string m_governing_specification_date;
   rpf_string m_security_classification;
   rpf_string m_security_country_international_code;
   rpf_string m_security_release_marking;
   UINT4 m_location_section_location;

   int read(FILE* fp, BOOL little_endian);
};

class RPF_header_section : public RPF_element
{

private:

   RPF_endianness m_endianness;
   RPF_header_section_excluding_endianness m_hdr;

protected:

   /*
    *  These return const pointers so that read can not be called on these 
    *  directly (i.e. you must call read_header)
    */
   const RPF_endianness* get_endianness(void) const 
      { return &m_endianness; }
   const RPF_header_section_excluding_endianness* get_hdr(void) const
      { return &m_hdr; }

public:

   RPF_header_section() {}
   virtual ~RPF_header_section() {}

   int read(FILE* fp);

   BOOL little_endian(void) const { return m_endianness.little_endian(); }
};

// ---------------------------------------------------------------
// location section
// ---------------------------------------------------------------

class RPF_component_location_record : public RPF_element
{

public:

   UINT2 m_component_id;
   UINT4 m_component_length;
   UINT4 m_component_location;

public:

   int read(FILE* fp, BOOL little_endian);
};

class RPF_location_section : public RPF_element
{

protected:

   int allocate_table(UINT2 num_comp_location_recs);
   void deallocate_table(void);

public:

   UINT2 m_location_section_length;
   UINT4 m_component_location_table_offset;
   UINT2 m_number_of_component_location_records;
   UINT2 m_component_location_record_length;
   UINT4 m_component_aggregate_length;
   RPF_component_location_record* m_component_location_table;

public:

   RPF_location_section(void)
   {
      m_component_location_table = NULL;
   }
   virtual ~RPF_location_section(void)
   {
      deallocate_table();
   }

   int read(FILE* fp, BOOL little_endian);
};

// ---------------------------------------------------------------
// coverage section
// ---------------------------------------------------------------

class RPF_coverage_section : public RPF_element
{

public:

   FLOAT8 m_nw_lat;
   FLOAT8 m_nw_lon;
   FLOAT8 m_sw_lat;
   FLOAT8 m_sw_lon;
   FLOAT8 m_ne_lat;
   FLOAT8 m_ne_lon;
   FLOAT8 m_se_lat;
   FLOAT8 m_se_lon;
   FLOAT8 m_north_south_resolution;
   FLOAT8 m_east_west_resolution;
   FLOAT8 m_latitude_interval;
   FLOAT8 m_longitude_interval;

public:

   int read(FILE* fp, BOOL little_endian);
};

// ---------------------------------------------------------------
// compression section
// ---------------------------------------------------------------

// ----------------------------------------------------------------

class RPF_compression_section_subheader : public RPF_element
{

public:

   UINT2 m_compression_algorithm_id;
   UINT2 m_number_of_compression_lookup_offset_records;
   UINT2 m_number_of_compression_parameter_offset_records;

public:

   int read(FILE* fp, BOOL little_endian);
};

class RPF_compression_lookup_offset_record : public RPF_element
{

public:

   UINT2 m_compression_lookup_table_id;
   UINT4 m_number_of_compression_lookup_records;
   UINT2 m_number_of_values_per_compression_lookup_record;
   UINT2 m_compression_lookup_value_bit_length;
   UINT4 m_compression_lookup_table_offset;

public:

   int read(FILE* fp, BOOL little_endian);
};

class RPF_compression_lookup_offset_record_array
{

public:

   RPF_compression_lookup_offset_record* m_recs;

public:

   RPF_compression_lookup_offset_record_array(void)
   {
      m_recs = NULL;
   }
   virtual ~RPF_compression_lookup_offset_record_array(void)
   {
      destroy();
   }

   int create(UINT2 num_recs);
   void destroy(void);
};

// ----------------------------------------------------------------

class RPF_compression_lookup_table : public RPF_element
{

public:

   UINT4 m_num_recs;
   UINT4 m_values_per_record;
   UINT2 m_bits_per_value;

   size_t m_num_bytes;

   unsigned char* m_bits;

   int create(UINT4 num_lookup_records, UINT4 values_per_record, 
      UINT2 bits_per_value);
   void destroy(void);

public:

   RPF_compression_lookup_table(void)
   {
      m_bits = NULL;

      m_num_recs = 0;
      m_values_per_record = 0;
      m_bits_per_value = 0;
      m_num_bytes = 0;
   }
   virtual ~RPF_compression_lookup_table(void)
   {
      destroy();
   }

   int read(FILE* fp, BOOL little_endian);
};

class RPF_compression_lookup_table_array
{

protected:

   UINT2 m_num_tables;

public:

   RPF_compression_lookup_table* m_tables;

public:

   RPF_compression_lookup_table_array(void)
   {
      m_tables = NULL;
      m_num_tables = 0;
   }
   virtual ~RPF_compression_lookup_table_array(void)
   {
      destroy();
   }

   int create(UINT2 num_tables);
   void destroy(void);
};

// ----------------------------------------------------------------

class RPF_compression_lookup_subsection : public RPF_element
{

private:

   UINT2 m_num_lookup_offset_records;

protected:

public:

   UINT4 m_compression_lookup_offset_table_offset;
   UINT2 m_compression_lookup_table_offset_record_length;
   RPF_compression_lookup_offset_record_array m_compression_lookup_offset_recs;
   RPF_compression_lookup_table_array m_compression_lookup_table_array;

public:

   RPF_compression_lookup_subsection(void)
   {
   }
   virtual ~RPF_compression_lookup_subsection(void)
   {
   }

   int read(FILE* fp, BOOL little_endian, 
      int num_compression_lookup_offset_records);
};

class RPF_compression_parameter_offset_record : public RPF_element
{

public:

   UINT2 m_compression_parameter_id;
   UINT4 m_compression_parameter_record_offset;

public:

   int read(FILE* fp, BOOL little_endian);
};

class RPF_compression_parameter_subsection : public RPF_element
{

protected:

   int allocate_compression_parameter_offset_table(void);
   int deallocate_compression_parameter_offset_table(void);
   int allocate_compression_parameter_record(void);
   int deallocate_compression_parameter_record(void);

public:

   UINT4 m_compression_parameter_offset_table_offset;
   UINT2 m_compression_parameter_offset_record_length;
   RPF_compression_parameter_offset_record* m_compression_parameter_offset_table;
   BYTE* m_compression_parameter_record;

public:

   RPF_compression_parameter_subsection(void)
   {
      m_compression_parameter_offset_table = NULL;
      m_compression_parameter_record = NULL;
   }
   virtual ~RPF_compression_parameter_subsection(void)
   {
      deallocate_compression_parameter_offset_table();
      deallocate_compression_parameter_record();
   }

   int read(FILE* fp, BOOL little_endian);
};

// ---------------------------------------------------------------
// color/grayscale section 
// ---------------------------------------------------------------

class RPF_color_grayscale_section_subheader : public RPF_element
{

public:

   UINT1 m_number_of_color_offset_records;
   UINT1 m_number_of_color_converter_offset_records;
   rpf_string m_external_color_file_name;

public:

   RPF_color_grayscale_section_subheader(void): m_external_color_file_name(12) {}

   int read(FILE* fp, BOOL little_endian);
};

// ---------------------------------------------------------------

class RPF_color_offset_record : public RPF_element
{

public:

   UINT2 m_color_table_id;
   UINT4 m_number_of_color_records;
   UINT1 m_color_element_length;
   UINT2 m_histogram_record_length;
   UINT4 m_color_table_offset;
   UINT4 m_histogram_table_offset;

public:

   RPF_color_offset_record(void) {}

   int read(FILE* fp, BOOL little_endian);
};

class RPF_colormap_offset_array
{

public:

   UINT1 m_num_recs;

public:

   RPF_color_offset_record* m_recs;
   
public:

   RPF_colormap_offset_array(void) 
   {
      m_recs = NULL;
      m_num_recs = 0;
   }
   ~RPF_colormap_offset_array(void) 
   {
      destroy();
   }

   int create(UINT1 num_recs);
   void destroy(void);
};

// ---------------------------------------------------------------

class RPF_color_table_and_histogram : public RPF_element
{

public:

   UINT2 m_table_id;
   unsigned char* m_color_elems;
   UINT4 m_num_color_records;
   UINT1 m_color_element_length;
   UINT4* m_histogram;
 
public:

   RPF_color_table_and_histogram(void) 
   {
      m_table_id = 0;

      m_color_elems = NULL;
      m_num_color_records = 0;
      m_color_element_length = 0;

      m_histogram = NULL;
   }
   virtual ~RPF_color_table_and_histogram(void)
   {
      destroy();
   }

   int read(FILE* fp, BOOL little_endian);

   int create(UINT2 table_id, UINT4 num_recs, UINT1 color_element_length);
   void destroy();
};

class RPF_color_table_array
{

public:

   RPF_color_table_and_histogram* m_tables;
   UINT1 m_num_tables;

public:

   RPF_color_table_array(void) 
   {
      m_tables = NULL;
      m_num_tables = 0;
   }
   ~RPF_color_table_array(void)
   {
      destroy();
   }

   int create(UINT1 num_tables);
   void destroy();
};

// ---------------------------------------------------------------

class RPF_colormap_subsection : public RPF_element
{

public:

   // file elements
   UINT4 m_colormap_offset_table_offset;
   UINT2 m_color_offset_record_length;
   RPF_colormap_offset_array m_cm_offset_recs;
   RPF_color_table_array m_cm_array;

   // non-file elements
   UINT1 m_num_recs;

public:

   RPF_colormap_subsection(void)
   {
      m_num_recs = 0;
   }

   int read(FILE* fp, BOOL little_endian);

   int create(UINT1 num_offset_records);
};

// ---------------------------------------------------------------
// image section
// ---------------------------------------------------------------

class RPF_image_description_subheader : public RPF_element
{

public:

   /*
    *  image description subheader
    */
   UINT2 m_number_of_spectral_groups;
   UINT2 m_number_of_subframe_tables;
   UINT2 m_number_of_spectral_band_tables;
   UINT2 m_number_of_spectral_band_lines_per_image_row;
   UINT2 m_number_of_subframes_in_east_west_direction;
   UINT2 m_number_of_subframes_in_north_south_direction;
   UINT4 m_number_of_output_columns_per_subframe;
   UINT4 m_number_of_output_rows_per_subframe;
   UINT4 m_subframe_mask_table_offset;
   UINT4 m_transparency_mask_table_offset;

public:

   int read(FILE* fp, BOOL little_endian);
};

// ---------------------------------------------------------------

class RPF_mask_subheader : public RPF_element
{

public:

   UINT2 m_subframe_sequence_record_length;
   UINT2 m_transparency_sequence_record_length;
   UINT2 m_transparent_output_pixel_code_length;
   unsigned char* m_transparent_output_pixel_code;

protected:

   size_t m_length_in_bytes;

   int allocate_transparent_pixel_code(size_t length_in_bytes);
   void deallocate_transparent_pixel_code(void);
   int read_transparent_pixel_code(FILE* fp);

public:
   
   RPF_mask_subheader(void)
   {
      m_transparent_output_pixel_code = NULL;
      m_transparent_output_pixel_code_length = 0;
   }
   virtual ~RPF_mask_subheader(void)
   {
      deallocate_transparent_pixel_code();
   }

   int read(FILE* fp, BOOL little_endian);
};

// ---------------------------------------------------------------

class RPF_mask_table : public RPF_element
{

public:

   UINT2 m_num_spectral_groups;
   UINT4 m_subframes_east_west;
   UINT4 m_subframes_north_south;
   UINT2 m_sequence_record_length;
   UINT4* m_subframe_offsets;

public:

   RPF_mask_table(void) 
   {
      m_subframe_offsets = NULL;
      m_num_spectral_groups = 0;
      m_subframes_east_west = 0;
      m_subframes_north_south = 0;
      m_sequence_record_length = 0;
   }
   virtual ~RPF_mask_table(void) 
   {
      destroy();
   }
  
   int read(FILE* fp, BOOL little_endian);

   int create(UINT2 num_spectral_groups, UINT4 num_subframes_east_west,
      UINT4 num_subframes_north_south, UINT2 sequence_record_length);
   void destroy(void);
};

// ---------------------------------------------------------------

class RPF_mask_subsection : public RPF_element
{

protected:

#if 0
   BOOL m_subframe_sequence_mask_table_present(void)
   { 
      return ((m_subframe_sequence_record_length == RPF_UINT_2_NULL_VALUE) ?
        FALSE : TRUE); 
   }
   BOOL m_transparency_sequence_mask_table_present(void)
   { 
      return ((m_transparency_sequence_record_length == RPF_UINT_2_NULL_VALUE) ?
        FALSE : TRUE); 
   }
#endif

public:

   RPF_mask_subheader m_subhdr;

   /*
    *  subframe mask table
    */
   RPF_mask_table m_subframe_mask_table;

   /*
    *  transparency mask table
    */
   RPF_mask_table m_transparency_mask_table;

public:

   RPF_mask_subsection(void) {}
   virtual ~RPF_mask_subsection(void) {}

   int read(FILE* fp, BOOL little_endian, 
      UINT4 subframe_mask_table_offset, UINT4 transparancy_mask_table_offset,
      UINT2 number_of_spectral_groups, UINT2 num_subframes_east_west,
      UINT2 num_subframes_north_south);
};

// ---------------------------------------------------------------

class RPF_image_display_parameters_subheader : public RPF_element
{

public:

   UINT4 m_number_of_image_rows;
   UINT4 m_number_of_image_codes_per_row;
   UINT1 m_image_code_bit_length;  // a multiple of 4

public:

   RPF_image_display_parameters_subheader(void) {}
   virtual ~RPF_image_display_parameters_subheader(void) {}

   int read(FILE* fp, BOOL little_endian);
};

// ---------------------------------------------------------------

class RPF_spectral_band_table : public RPF_element
{

public:

   UINT4 m_num_image_rows;
   UINT4 m_num_image_codes_per_row;
   UINT1 m_image_code_bit_length;


public:

   size_t m_num_bytes;  // number of bytes allocated in m_image_codes

   unsigned char* m_image_codes;

public:

   RPF_spectral_band_table(void)
   {
      m_image_codes = NULL;
      m_num_image_rows = 0;
      m_num_image_codes_per_row = 0;
      m_image_code_bit_length = 0;

      m_num_bytes = 0;
   }

   virtual ~RPF_spectral_band_table(void)
   {
      destroy();
   }

   int read(FILE* fp, BOOL little_endian);

   int create(UINT4 num_image_rows, UINT4 num_image_codes_per_row,
      UINT1 image_code_bit_length);
   void destroy(void);
};

#endif
