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
#include "indexes.h"
#include "vpfrcset.h"
#include "vpfdb.h"
#include "variant.h"
#ifdef _WIN32
#include "ComErrorObject.h"
#endif  // POSIX: fv_compat provides the error macros

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// file position that starts the bin array section
const DWORD POS_BIN_ARRAY = 24;

//------------------------------------------------------------------------------
//----------------------------- VPFSpatialIndex --------------------------------
//------------------------------------------------------------------------------
VPFSpatialIndex::VPFSpatialIndex(VPFRecordset* main_table) : m_isopen(false)
{
   CString index_path;

   // Determine what type of primitive table the main table is, and set
   // the spatial index path accordingly
   {
      //"For each primitive (face, edge, entity node, connected node, and text),
      // there can exist a spatial index file:  FSI, ESI, NSI, CSI, or TSI."
      // -- MIL-STD-2407, Section F.4.1

      bool main_table_is_primitive_table = true;
      CString main_table_path = main_table->get_file_path();
      CString main_table_name = main_table->get_file_name();

      main_table_path.MakeLower();
      main_table_name.MakeLower();

      index_path = main_table_path.Left(main_table_path.GetLength() - 3);

      if (main_table_name == _T("fac"))
         index_path += "fsi";
      else if (main_table_name == _T("edg"))
         index_path += "esi";
      else if (main_table_name == _T("end"))
         index_path += "nsi";
      else if (main_table_name == _T("cnd"))
         index_path += "csi";
      else if (main_table_name == _T("txt"))
         index_path += "tsi";
      else
         main_table_is_primitive_table = false;

      if (!main_table_is_primitive_table)
      {
         CString Error = _T("A spatial index is only allowed for primitive tables.\n")
                 _T("Table ")+main_table_path+_T(" is not a primitive table.");
         WriteToLogFile(_bstr_t(Error));
         return;
      }
   }

   // Now open the index file
   if (!m_file.Open(index_path, CFile::modeRead | CFile::shareDenyWrite))
   {
      ASSERT(false);
      return;
   }

   //
   // Read in the header and set up appropriate data members
   //

   m_file.Read(&m_num_primitives, 4);

   // The latitude and longitude values the bounding box for the index file
   // are stored in 4-byte floats, and the FalconView standard geographic
   // points are 8-byte doubles.  Therefore we have to read the data into a
   // float and then assign that value to the double that we store.
   {
      float  value;
      m_file.Read(&value, 4);    m_bounding_rect.ll.lon = value;
      m_file.Read(&value, 4);    m_bounding_rect.ll.lat = value;
      m_file.Read(&value, 4);    m_bounding_rect.ur.lon = value;
      m_file.Read(&value, 4);    m_bounding_rect.ur.lat = value;
   }

   m_file.Read(&m_num_nodes, 4);

   m_pos_bin_data = POS_BIN_ARRAY + (m_num_nodes * 8);

   m_isopen = true;
}

//---------------------------- ~VPFSpatialIndex --------------------------------
VPFSpatialIndex::~VPFSpatialIndex()
{
}

//-------------------------- get_primitives_in_rect ----------------------------
int VPFSpatialIndex::get_primitives_in_rect(d_geo_rect_t rect)
{
   return get_primitives_in_rect(rect.ll.lon, rect.ll.lat, rect.ur.lon, rect.ur.lat);
}

//-------------------------- get_primitives_in_rect ----------------------------
int VPFSpatialIndex::get_primitives_in_rect(d_geo_t ll, d_geo_t ur)
{
   return get_primitives_in_rect(ll.lon, ll.lat, ur.lon, ur.lat);
}

//-------------------------- get_primitives_in_rect ----------------------------
int VPFSpatialIndex::get_primitives_in_rect(degrees_t /*x1*/, degrees_t /*y1*/, degrees_t /*x2*/, degrees_t /*y2*/)
{
   //.ecr.todo. Complete this function
   return FAILURE;
}

//-----------------------   get_primitives_for_point ---------------------------
int VPFSpatialIndex::get_primitives_for_point(degrees_t /*x*/, degrees_t /*y*/, long /*threshold*/)
{
   //.ecr.todo. Complete this function
   return FAILURE;
}

//-----------------------   get_primitives_for_point ---------------------------
int VPFSpatialIndex::get_primitives_for_point(d_geo_t pt, long threshold)
{
   return get_primitives_for_point(pt.lon, pt.lat, threshold);
}


//------------------------------------------------------------------------------
//-------------------------- VPFVariableLengthIndex ----------------------------
//------------------------------------------------------------------------------
VPFVariableLengthIndex::VPFVariableLengthIndex(VPFRecordset* main_table) : m_isopen(false)
{
/*
   From MIL-STD-2407, Section 5.3.1.2 "Reserved table names and extensions":

   Any table that contains variable-length records must have a
   variable-length index associated with it. The index file shall
   have the same file name as the table, except that the last
   character will end with "X." For example, a variable-length
   record road line table, ROAD.LFT, would have a variable-length
   index ROAD.LFX. The one exception to this convention is for the
   FCS, whose variable-length index shall be named FCZ.

*/
   CString index_path;
   CString main_table_path = main_table->get_file_path();
   CString main_table_name = main_table->get_file_name();

   main_table_path.MakeLower();
   main_table_name.MakeLower();

   if (main_table_name == _T("fcs"))
   {
      index_path = main_table_path;
      index_path.SetAt(index_path.GetLength() - 1, 'z');
   }
   else
   {
      index_path = main_table_path;
      index_path.SetAt(index_path.GetLength() - 1, 'x');
   }

   m_file_handle = CreateFile(index_path, GENERIC_READ, FILE_SHARE_READ,
		                     NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); 

	if (m_file_handle == INVALID_HANDLE_VALUE)
	{
//		ERR_report("Invalid file handle");
		return;
	}

	// create the file mapping
	m_file_mapping_handle = CreateFileMapping(m_file_handle, 
		NULL, PAGE_READONLY, 0, 0, NULL);

	// retrieve a pointer to the file
	m_file_ptr = (BYTE *)MapViewOfFile(m_file_mapping_handle,
		FILE_MAP_READ, 0, 0, 0);

	// 4-byte on-disk count; see the note in VPFRecordset::read_in_header.
	m_record_count = *(int32_t *)m_file_ptr;

   m_isopen = true;
}

//------------------------- ~VPFVariableLengthIndex ----------------------------
VPFVariableLengthIndex::~VPFVariableLengthIndex()
{
	if (m_isopen)
   {
	   UnmapViewOfFile(m_file_ptr);
	   CloseHandle(m_file_mapping_handle);
	   CloseHandle(m_file_handle);
   }
}


//----------------------------- get_record_count -------------------------------
long VPFVariableLengthIndex::get_record_count()
{
   return m_record_count;
}

//-------------------------- get_filepos_for_record ----------------------------
DWORD VPFVariableLengthIndex::get_filepos_for_record(int record_num)
{
	ASSERT(record_num < m_record_count);

	if (record_num < m_record_count)
		return *(DWORD *)(m_file_ptr + 8 + (record_num << 3));

   return 0;
}

//------------------------------------------------------------------------------
//----------------------------- VPFThematicIndex -------------------------------
//------------------------------------------------------------------------------
VPFThematicIndex::VPFThematicIndex(VPFFieldInfo* /*field_info*/) : m_isopen(false)
{
   char header[60];
   memset(header, 0, 60);

   // given a field_info struct, we should be able to get our filename from
   // it and determine how to open the file on disk

   CString index_path;

   if (!m_file.Open(index_path, CFile::modeRead | CFile::shareDenyWrite))
   {
      ASSERT(false);
      return;
   }

   //
   // Read in the header and set up appropriate data members
   //
   m_file.Read(&header, 60);

   memcpy(&m_full_header_size,    &header[0], 4);

   memcpy(&m_num_values_indexed, &header[4], 4);

   memcpy(&m_num_rows_indexed,   &header[8], 4);

   switch (header[12])
   {
      case 'I':
      case 'i':
         m_index_type = VPF_INVERTED_LIST_INDEX; break;
      case 'B':
      case 'b':
         m_index_type = VPF_BIT_ARRAY_INDEX;     break;
      default:
         ASSERT(false);
   }

   m_type_of_value_in_dir = header[13];
   switch (m_type_of_value_in_dir)
   {
      case 'I':
      case 'i': m_size_of_value_in_dir = 4; break;
      case 'T':
      case 't': m_size_of_value_in_dir = 1; break;
      case 'S':
      case 's': m_size_of_value_in_dir = 2; break;
      case 'F':
      case 'f': m_size_of_value_in_dir = 4; break;
      case 'R':
      case 'r': m_size_of_value_in_dir = 8; break;
   }


   memcpy(m_table_name.GetBuffer(12), &header[19], 12);
   m_table_name.ReleaseBuffer();
   m_table_name.TrimRight();

   memcpy(m_column.GetBuffer(25), &header[31], 25);
   m_column.ReleaseBuffer();
   m_column.TrimRight();

   m_sorted = (header[56] == 'S');

   //
   // Read in directory entries, if present
   //



   m_isopen = true;

}

//---------------------------- ~VPFThematicIndex -------------------------------
VPFThematicIndex::~VPFThematicIndex()
{
}

//------------------------------ table_indexed ---------------------------------
CString VPFThematicIndex::table_indexed()
{
   return m_table_name;
}

//------------------------------ column_indexed --------------------------------
CString VPFThematicIndex::column_indexed()
{
   return m_column;
}
