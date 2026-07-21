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



#ifndef CDB_BASE_H
#define CDB_BASE_H 1

/*
 *  This file contains classes that represent a RPF file
 */

#include <stdio.h>
#include "nitf.h"
#include "rpf_defs.h"
#include "rpf_comp.h"

// ---------------------------------------------------------------------

class nitf_rpf_file
{

private:

   NTF_file_header m_nitf_header;
   RPF_endianness m_endianness;
   RPF_header_section_excluding_endianness m_hdr;

public:

   /*
    *  These return const pointers so that you can not call read on these 
    *  directly (i.e. you must call read_header - because these reads can
    *  not be done separately).
    */
   const NTF_file_header* get_nitf_hdr(void) const 
      { return &m_nitf_header; }
   const RPF_endianness* get_endianness(void) const 
      { return &m_endianness; }
   const RPF_header_section_excluding_endianness* get_hdr(void) const
      { return &m_hdr; }

public:

   nitf_rpf_file() {}
   virtual ~nitf_rpf_file() {}

   virtual int read_header(FILE* fp);

   BOOL little_endian(void) const 
   { 
      return m_endianness.little_endian(); 
   }
};


// ---------------------------------------------------------------

class cadrg_cib_base : public nitf_rpf_file
{

protected:

   RPF_location_section m_location;
   RPF_coverage_section m_coverage;

   /*
    *  compression subsections
    */
   RPF_compression_section_subheader m_compr_subhdr;
   RPF_compression_lookup_subsection m_compr_lookup;

   /*
    *  color section
    */
   RPF_color_grayscale_section_subheader m_color_section_subhdr;
   RPF_colormap_subsection m_colormap_subsection;

   /*
    *  image section
    */
   RPF_image_description_subheader m_img_subhdr;
   RPF_mask_subsection m_mask_subsection;
   RPF_image_display_parameters_subheader m_img_disp_params;

public:

   /*
    *  file components
    */
   RPF_location_section* get_location(void) { return &m_location; }
   RPF_coverage_section* get_coverage(void) { return &m_coverage; }

   /*
    *  compression subsections
    */
   RPF_compression_section_subheader* get_compr_subhdr(void) 
      { return &m_compr_subhdr; }
   RPF_compression_lookup_subsection* get_compr_lookup(void) 
      { return &m_compr_lookup; }

   /*
    *  color section
    */
   RPF_color_grayscale_section_subheader*
      get_color_section_subheader(void)
   {
      return &m_color_section_subhdr;
   }
   RPF_colormap_subsection* get_colormap_subsection(void)
   {
      return &m_colormap_subsection;
   }

   /*
    *  image section
    */
   RPF_image_description_subheader* get_img_subhdr(void)
      { return &m_img_subhdr; }
   RPF_mask_subsection* get_mask_subsection(void)
      { return &m_mask_subsection; }
   RPF_image_display_parameters_subheader* get_img_disp_params(void)
      { return &m_img_disp_params; }


public:

   cadrg_cib_base() {}
   virtual ~cadrg_cib_base() {}

   int vq_decompress_spectral_band_table(unsigned char* img,
      const unsigned char* image_codes, 
      const unsigned char* compr_lookup_table1, 
      const unsigned char* compr_lookup_table2,
      const unsigned char* compr_lookup_table3,
      const unsigned char* compr_lookup_table4);

   // reads compression codes
   virtual int read_subframe(FILE* fp, int subframe_index, 
      RPF_spectral_band_table* subframe, BOOL little_endian);

   // new functions (i.e. non-overrides) 
   virtual int subframe_present(int subframe_index, 
      BOOL* subframe_present, UINT4* offset);
   virtual int transparency_present(int transparencye_index, 
      BOOL* transparencye_present);

   int read_components(FILE* fp);

   virtual int read(FILE* fp);
};

#endif