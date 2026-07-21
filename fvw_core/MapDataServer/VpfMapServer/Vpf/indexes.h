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

#if !defined(INDEXES_H__8DC93C8E_8651_11D3_8663_00105A9B4838__INCLUDED_)
#define INDEXES_H__8DC93C8E_8651_11D3_8663_00105A9B4838__INCLUDED_

//#include "geo_tool_d.h" // for degrees_t & d_geo_rect_t

#include "vpf_d.h"

// Forward Declarations
class VPFRecordset;
struct VPFFieldInfo;

//------------------------------------------------------------------------------
//----------------------------- VPFSpatialIndex --------------------------------
//------------------------------------------------------------------------------
// Spatial indexes are not required in a VPF database, but they are strongly
// recommended.  They provide the ability to very quickly retrieve primitive
// data based on geographic position without having to scan through all the
// primitive tables. (see MIL-STD-2407 section 5.4.2)
//------------------------------------------------------------------------------
class VPFSpatialIndex
{
//--- Construction ----------------------------------------
public:
   VPFSpatialIndex              (VPFRecordset* main_table);
   virtual ~VPFSpatialIndex     ();

//--- Public Methods --------------------------------------
public:
   // Given a bounding box, these functions will return a list
   // of the primitives that lie within or on the borders.
   int get_primitives_in_rect   (d_geo_rect_t x);
   int get_primitives_in_rect   (d_geo_t ll, d_geo_t ur);
   int get_primitives_in_rect   (degrees_t x1, degrees_t y1,
                                 degrees_t x2, degrees_t y2);

   // Given a point and a distance from that point, these functions
   // will return a list of the primitives that lie within the bounds
   // of the described circle .
   int get_primitives_for_point (degrees_t x, degrees_t y, long threshold = 0);
   int get_primitives_for_point (d_geo_t pt, long threshold = 0);

   bool is_open() const   {return m_isopen;}
   
//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   CFile m_file;           // the file on disk that represents this index

   DWORD m_pos_bin_data;   // file position that starts the bin data section

   int32_t  m_num_primitives; // the number of primitives indexed in this file
   int32_t  m_num_nodes;      // the number of nodes in the data tree

   d_geo_rect_t m_bounding_rect; // the geographic extent of the items indexed

   bool m_isopen;
};


//------------------------------------------------------------------------------
//-------------------------- VPFVariableLengthIndex ----------------------------
//------------------------------------------------------------------------------
// If a VPFRecordset contains any variable length data (text or coordinates),
// then there must be an index for that file.  The index specifies the number
// of records in the recordset, and the byte-offset and byte-length for each
// record in the recordset. (see MIL-STD-2407 section 5.4.1.3)
//------------------------------------------------------------------------------
class VPFVariableLengthIndex
{
//--- Construction ----------------------------------------
public:
   VPFVariableLengthIndex(VPFRecordset* main_table);
   virtual ~VPFVariableLengthIndex();


//--- Public Methods --------------------------------------
public:
   long  get_record_count        ();
   DWORD get_filepos_for_record  (int record_num);
   bool is_open() const   {return m_isopen;}

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   HANDLE m_file_handle;
	HANDLE m_file_mapping_handle;
	BYTE *m_file_ptr;

   int32_t  m_record_count;   // the number of entries in the index; also the
                           // number of records in the main file

   bool m_isopen;
};


//------------------------------------------------------------------------------
//----------------------------- VPFThematicIndex -------------------------------
//------------------------------------------------------------------------------
// "A thematic index may be created for any column in a table.  There are two
// types of indexes, depending on the data content in a column: an inverted list
// thematic index or a bit array thematic index.
//
// For categorical data or data with few distinct values, such as soil polygons
// where numerous polygons are assigned soil class designations from a
// relatively small number of classes, an inverted list is used. One entry in
// the index file is created for each distinct value in the column;
// correspondingly, a list of table record ids is stored with the value.
//
// If the data in a column is all unique, especially in the case of an index for
// character strings, a bit array can be stored for each unique byte/character
// in the column. Each bit in the bit array represents a row in the indexed
// table. An ‘ON’ bit at a particular position means that the corresponding row
// in the table contains a specific byte/character pattern."
//
// -- MIL-STD-2407, Section 5.4.3
//------------------------------------------------------------------------------
class VPFThematicIndex
{
   enum VPFThematicIndexType
   {
      VPF_INVERTED_LIST_INDEX,
      VPF_BIT_ARRAY_INDEX
   };

//--- Construction ----------------------------------------
public:
   VPFThematicIndex(VPFFieldInfo* field_info);
   virtual ~VPFThematicIndex();
   bool is_open() const   {return m_isopen;}

//--- Public Methods --------------------------------------
public:
   // returns the name of the table that this file indexes
   CString table_indexed();

   // returns the name of the column that this file indexes
   CString column_indexed();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   CFile   m_file; // the file on disk that represents this index

   int32_t m_full_header_size;   // the size (in bytes) of the header PLUS
                              // the size of the directory

   int32_t m_num_values_indexed; // number of distinct values in the main
                              // table for the value being indexed here
   int32_t m_num_rows_indexed;   // number of rows which are listed in this index

   CString m_table_name;      // table this file indexes
   CString m_column;          // column this file indexes

   char    m_type_of_value_in_dir;
   long    m_size_of_value_in_dir; // the byte-size of the value listed int
                                   // the directory; this value is used in the
                                   // algorithm that allows you to jump directly
                                   // to an entry in the directory


   long    m_num_dir_entries;
   char    m_data_element_type;
   char    m_data_type_indexed;
   bool    m_sorted;

   VPFThematicIndexType m_index_type;

   bool m_isopen;
};

#endif // !defined(INDEXES_H__8DC93C8E_8651_11D3_8663_00105A9B4838__INCLUDED_)