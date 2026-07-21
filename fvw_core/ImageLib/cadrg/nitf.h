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



#ifndef NITF_H
#define NITF_H 1

#include "rpf_comp.h"

// ------------------------------------------------------------

class NTF_registered_tagged_record
{

public:

   char m_unique_extension_type_id[6+1];
   UINT4 m_length;

protected:

   virtual int read_user_defined_data(FILE* fp) = 0;

public:

#if 0
   virtual int read(FILE* fp)
   {
      FREAD(m_unique_extension_type_id, 1, 6, fp);
      m_unique_extension_type_if[6] = '\0';
      READ_AUINT4(fp, 5, &m_length);

      if (read_user_defined_data(fp) != SUCCESS)
      {
         ERR_report("read_user_defined_data failed");
         return FAILURE;
      }

      return SUCCESS;
   }
#endif

};

class NTF_rpf_tagged_record
{
};

// ------------------------------------------------------------

class NTF_file_header
{

public:

   char m_file_type_and_version[9+1];
	char m_originating_station_id[10+1];
   char m_file_date_and_time[14+1];
   char m_file_title[80+1];
   char m_file_security_downgrade[6+1];
   UINT2 m_number_of_images;
   UINT2 m_number_of_symbols;
   UINT2 m_number_of_labels;
   UINT2 m_number_of_text_files;
   UINT2 m_number_of_data_extensions;
   UINT2 m_number_of_reserved_extensions;

   UINT4 m_user_defined_header_data_length;
   UINT2 m_user_defined_header_overflow;

public:

   NTF_file_header() {}
   ~NTF_file_header(void)
   {
       // deallocate
   }
   int read(FILE* fp, BOOL* rpf_header_present,  
      RPF_endianness* rpf_endian, 
      RPF_header_section_excluding_endianness* rpf_hdr);
};

#endif
