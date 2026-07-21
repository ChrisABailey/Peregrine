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

#include "nitf.h"
#include "rpf_lcl.h"
#include "rpf_util.h"
#include "cdb_base.h"

#define READ_AUINT_2(fp, field_length, ptr)            \
   if (read_auint_2(fp, field_length, ptr) != SUCCESS)  \
      return FAILURE;
#define READ_AUINT_4(fp, field_length, ptr)            \
   if (read_auint_4(fp, field_length, ptr) != SUCCESS)  \
      return FAILURE;

// ----------------------------------------------------------------

int NTF_file_header::read(FILE* fp, BOOL* rpf_header_present, 
   RPF_endianness* rpf_endian, RPF_header_section_excluding_endianness* rpf_hdr)
{
   *rpf_header_present = FALSE;

   FREAD(m_file_type_and_version, 1, 9, fp);
   m_file_type_and_version[9] = '\0';

	FSEEK(fp, 6, SEEK_CUR);	// skip unused fields

	FREAD(m_originating_station_id, 1, 10, fp);
	m_originating_station_id[10] = '\0';

   FREAD(m_file_date_and_time, 1, 14, fp); // strip trailing
   m_file_date_and_time[14] = '\0';

   FREAD(m_file_title, 1, 80, fp); // strip trailing
   m_file_title[80] = '\0';

   FSEEK(fp, 161, SEEK_CUR); // skip junk

   FREAD(m_file_security_downgrade, 1, 6, fp);
   m_file_security_downgrade[6] = '\0';
   if (strcmp(m_file_security_downgrade, "999998") == 0)
   {
      FSEEK(fp, 40, SEEK_CUR);
   }

   FSEEK(fp, 74, SEEK_CUR); // skip junk

   READ_AUINT_2(fp, 3, &m_number_of_images);
   int image;
   for (image=0; image<m_number_of_images; image++)
   {
       FSEEK(fp, 16, SEEK_CUR); // skip junk
   }

   READ_AUINT_2(fp, 3, &m_number_of_symbols);
   int symbol;
   for (symbol=0; symbol<m_number_of_symbols; symbol++)
   {
       FSEEK(fp, 10, SEEK_CUR); // skip junk
   }

   READ_AUINT_2(fp, 3, &m_number_of_labels);
   int label;
   for (label=0; label<m_number_of_labels; label++)
   {
       FSEEK(fp, 7, SEEK_CUR); // skip junk
   }

   READ_AUINT_2(fp, 3, &m_number_of_text_files);
   int text_file;
   for (text_file=0; text_file<m_number_of_text_files; text_file++)
   {
       FSEEK(fp, 9, SEEK_CUR); // skip junk
   }

   READ_AUINT_2(fp, 3, &m_number_of_data_extensions);
   if (m_number_of_data_extensions > 0)
   {
      int data_extension;
      for (data_extension=0; data_extension<m_number_of_data_extensions; 
         data_extension++)
      {
         FSEEK(fp, 13, SEEK_CUR);
      }
   }

   READ_AUINT_2(fp, 3, &m_number_of_reserved_extensions);
   int reserved;
   for (reserved=0; reserved<m_number_of_reserved_extensions; reserved++)
   {
       FSEEK(fp, 11, SEEK_CUR); // skip junk
   }

   READ_AUINT_4(fp, 5, &m_user_defined_header_data_length);

   long pos_at_start_of_user_defined_header_data = ftell(fp);
   if (pos_at_start_of_user_defined_header_data == -1)
   {
      // ftell failed
      return FAILURE;
   }

   /*
    *  read user defined header data (if any)
    */
   if (m_user_defined_header_data_length != 0)
   {
      READ_AUINT_2(fp, 3, &m_user_defined_header_overflow);

      long curr_pos = ftell(fp);
      if (curr_pos == -1)
      {
         // ftell failed
         return FAILURE;
      }

      char extension_type_identifier[6+1];
      UINT4 length;
      while ((UINT4)curr_pos < 
         ((UINT4)pos_at_start_of_user_defined_header_data + 
          m_user_defined_header_data_length))
      {
         long pos_at_start_of_reg_tag = ftell(fp);
         if (pos_at_start_of_reg_tag == -1)
         {
            // ftell failed
            return FAILURE;
         }

         FREAD(extension_type_identifier, 1, 6, fp);
         extension_type_identifier[6] = '\0';
         READ_AUINT_4(fp, 5, &length);

         /*
          *  if it's the RPF header extension, read it
          */
         if (strcmp(extension_type_identifier, "RPFHDR") == 0)
         {
            if (rpf_endian->read(fp) != SUCCESS)
            {
               // read failed
               return FAILURE;
            }
            if (rpf_hdr->read(fp, rpf_endian->little_endian()) != SUCCESS)
            {
               // read failed
               return FAILURE;
            }

            *rpf_header_present = TRUE;
         }

         if (fseek(fp, pos_at_start_of_reg_tag+11+length, SEEK_SET) != 0)
         {
            // fseek failed
            return FAILURE;
         }

         curr_pos = ftell(fp);
      }

      /*
       *  NOTE: this does not handle overflow into an other record
       */
   }

   return SUCCESS;
}

