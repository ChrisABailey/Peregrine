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

#include "tables.h"
#include "vpfrcset.h"
#include "vpfdb.h"
#include "variant.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//------------------------------------------------------------------------------
//------------------------- VPFLibraryAttributeTable ---------------------------
//------------------------------------------------------------------------------
//VPFLibraryAttributeTable::VPFLibraryAttributeTable(VPFDatabase* db)
//: VPFRecordset(db)
//{
//}

// open from a path
VPFLibraryAttributeTable::VPFLibraryAttributeTable(const CString &path)
: VPFRecordset(path)
{
}

//------------------------ ~VPFLibraryAttributeTable ---------------------------
VPFLibraryAttributeTable::~VPFLibraryAttributeTable()
{
}

//----------------------------------- open -------------------------------------
void VPFLibraryAttributeTable::open()
{
   VPFRecordset::open(_T("lat"));
}

//----------------------------- on_set_position --------------------------------
void VPFLibraryAttributeTable::on_set_position()
{
   VPFVariant *value;

   value = get_field_value(_T("library_name"));
   m_library_name = value->m_text;

   value = get_field_value(_T("xmin"));
   if (value->is_float_short())
      m_bounds.ll.lon = value->m_float_short;
   else
      m_bounds.ll.lon = value->m_float_long;

   value = get_field_value(_T("xmax"));
   if (value->is_float_short())
      m_bounds.ur.lon = value->m_float_short;
   else
      m_bounds.ur.lon = value->m_float_long;


   value = get_field_value(_T("ymin"));
   if (value->is_float_short())
      m_bounds.ll.lat = value->m_float_short;
   else
      m_bounds.ll.lat = value->m_float_long;

   value = get_field_value(_T("ymax"));
   if (value->is_float_short())
      m_bounds.ur.lat = value->m_float_short;
   else
      m_bounds.ur.lat = value->m_float_long;
}

//--------------------------------- on_open ------------------------------------
void VPFLibraryAttributeTable::on_open()
{
   // The filename must be "lat"
   verify_filename(_T("lat"));

   // Verify that the fields are what they should be
   // Field Name          FieldType    KeyType
   // 0     ID              I            U
   // 1     LIBRARY_NAME    T/L/M/N,8    P
   // 2     XMIN            F/R          N
   // 3     YMIN            F/R          N
   // 4     XMAX            F/R          N
   // 5     YMAX            F/R          N

//   verify_field("id", VPF_INT_LONG, 0, VPF_KEY_UNIQUE);//x-x-x-x-x
// VVOD has this as VPF_KEY_UNIQUE, VMap0 has this as VPF_KEY_PRIMARY

//   verify_field("library_name",
//      VPF_TEXT | VPF_LATIN1_TEXT | VPF_FULL_LATIN_TEXT | VPF_MULTI_LINGUAL_TEXT,
//      8,
//      VPF_KEY_PRIMARY);
// VVOD has this as VPF_KEY_PRIMARY, VMap0 has this as VPF_KEY_NON_UNIQUE

   verify_field(_T("xmin"), VPF_FLOAT_LONG | VPF_FLOAT_SHORT, 0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("ymin"), VPF_FLOAT_LONG | VPF_FLOAT_SHORT, 0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("xmax"), VPF_FLOAT_LONG | VPF_FLOAT_SHORT, 0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("ymax"), VPF_FLOAT_LONG | VPF_FLOAT_SHORT, 0, VPF_KEY_NON_UNIQUE);
}

//------------------------------ get_class_name --------------------------------
const TCHAR * VPFLibraryAttributeTable::get_class_name()
{
   return _T("VPFLibraryAttributeTable");
}

//------------------------------- library_name ---------------------------------
CString VPFLibraryAttributeTable::library_name()
{
   return m_library_name;
}

//---------------------------------- bounds ------------------------------------
d_geo_rect_t VPFLibraryAttributeTable::bounds()
{
   return m_bounds;
}


//------------------------------------------------------------------------------
//-------------------------- VPFDatabaseHeaderTable ----------------------------
//------------------------------------------------------------------------------
//VPFDatabaseHeaderTable::VPFDatabaseHeaderTable(VPFDatabase* db)
//: VPFRecordset(db)
//{
//}

VPFDatabaseHeaderTable::VPFDatabaseHeaderTable(const CString& path_to_table)
: VPFRecordset(path_to_table)
{
}

//------------------------- ~VPFDatabaseHeaderTable ----------------------------
VPFDatabaseHeaderTable::~VPFDatabaseHeaderTable()
{
}

//----------------------------------- open -------------------------------------
int VPFDatabaseHeaderTable::open()
{
   return VPFRecordset::open(_T("dht"));
}

//----------------------------- on_set_position --------------------------------
void VPFDatabaseHeaderTable::on_set_position()
{
   VPFVariant *data;

	data = this->get_field_value(_T("database_name"));
	m_database_name = data->m_text;
}

//--------------------------------- on_open ------------------------------------
void VPFDatabaseHeaderTable::on_open()
{
   verify_filename(_T("dht"));

   verify_field(_T("vpf_version"),    VPF_TEXT,      10, VPF_KEY_NON_UNIQUE);
   verify_field(_T("database_name"),  VPF_TEXT,       8, VPF_KEY_NON_UNIQUE);
   verify_field(_T("database_desc"),  VPF_TEXT,     100, VPF_KEY_NON_UNIQUE);
   verify_field(_T("media_standard"), VPF_TEXT,      20, VPF_KEY_NON_UNIQUE);
   verify_field(_T("originator"),     VPF_TEXT,      50, VPF_KEY_NON_UNIQUE);
   verify_field(_T("addressee"),      VPF_TEXT,     100, VPF_KEY_NON_UNIQUE);
   verify_field(_T("media_volumes"),  VPF_TEXT,       1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("seq_numbers"),    VPF_TEXT,       1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("num_data_sets"),  VPF_TEXT,       1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("media_volumes"),  VPF_TEXT,      -1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("seq_numbers"),    VPF_TEXT,      -1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("num_data_sets"),  VPF_TEXT,      -1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("security_class"), VPF_TEXT,       1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("downgrading"),    VPF_TEXT,       3, VPF_KEY_NON_UNIQUE);
   verify_field(_T("downgrade_date"), VPF_DATE_TIME,  0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("releasability"),  VPF_TEXT,      20, VPF_KEY_NON_UNIQUE);
   verify_field(_T("other_std_name"), VPF_TEXT,      50, VPF_KEY_NON_UNIQUE, false);
   verify_field(_T("other_std_date"), VPF_DATE_TIME,  0, VPF_KEY_NON_UNIQUE, false);
   verify_field(_T("other_std_ver"),  VPF_TEXT,      20, VPF_KEY_NON_UNIQUE, false);
   verify_field(_T("transmittal_id"), VPF_TEXT,      -1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("transmittal_id"), VPF_TEXT,       1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("edition_number"), VPF_TEXT,      10, VPF_KEY_NON_UNIQUE);
   verify_field(_T("edition_date"),   VPF_DATE_TIME,  0, VPF_KEY_NON_UNIQUE);
}

//------------------------------ get_class_name --------------------------------
const TCHAR * VPFDatabaseHeaderTable::get_class_name()
{
   return _T("VPFDatabaseHeaderTable");
}

// VPFLibraryHeaderTable implementation
//------------------------------------------------------------------------------
VPFLibraryHeaderTable::VPFLibraryHeaderTable(VPFLibrary* lib)
: VPFRecordset(lib), m_library(lib)
{
}

VPFLibraryHeaderTable::VPFLibraryHeaderTable(const CString& path_to_table)
: VPFRecordset(path_to_table)
{
}


VPFLibraryHeaderTable::~VPFLibraryHeaderTable()
{
}

//----------------------------- on_set_position --------------------------------
void VPFLibraryHeaderTable::on_set_position()
{
   VPFVariant *data;

   data = get_field_value(_T("product_type"));
   m_product_type = data->m_text;

   data = get_field_value(_T("library_name"));
   m_library_name = data->m_text;

   data = get_field_value(_T("description"));
   m_library_description = data->m_text;

   data = get_field_value(_T("source_series"));
   m_source_series = data->m_text;

   data = get_field_value(_T("source_id"));
   m_source_id = data->m_text;

   data = get_field_value(_T("source_name"));
   m_source_name = data->m_text;

   data = get_field_value(_T("edition_number"));
	if (data)
		m_edition_number = data->m_text;
}

//--------------------------------- on_open ------------------------------------
void VPFLibraryHeaderTable::on_open()
{
   verify_filename(_T("lht"));

   verify_field(_T("id"),					VPF_INT_LONG,    0, VPF_KEY_PRIMARY);
   verify_field(_T("product_type"),		VPF_TEXT,       12, VPF_KEY_NON_UNIQUE);
   verify_field(_T("library_name"),		VPF_TEXT,       12, VPF_KEY_NON_UNIQUE);
   verify_field(_T("description"),	   VPF_TEXT,      100, VPF_KEY_NON_UNIQUE);
   
	verify_field(_T("data_struct_code"), VPF_TEXT,		  1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("scale"),				VPF_INT_LONG,    0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("source_series"),   VPF_TEXT,		 15, VPF_KEY_NON_UNIQUE);
   verify_field(_T("source_id"),			VPF_TEXT,       30, VPF_KEY_NON_UNIQUE);
	verify_field(_T("source_edition"),  VPF_TEXT,       20, VPF_KEY_NON_UNIQUE);
	verify_field(_T("source_name"),		VPF_TEXT,      100, VPF_KEY_NON_UNIQUE);

   verify_field(_T("source_date"),		VPF_DATE_TIME,   0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("security_class"),	VPF_TEXT,        1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("downgrading"),		VPF_TEXT,        3, VPF_KEY_NON_UNIQUE);
   verify_field(_T("downgrading_date"),VPF_DATE_TIME,   0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("releasability"),	VPF_TEXT,       20, VPF_KEY_NON_UNIQUE);
}

//------------------------------ get_class_name --------------------------------
const TCHAR * VPFLibraryHeaderTable::get_class_name()
{
   return _T("VPFLibraryHeaderTable");
}


//----------------------------------- open -------------------------------------
int VPFLibraryHeaderTable::open()
{
   return VPFRecordset::open(_T("\\lht"));
}


//------------------------------------------------------------------------------
//-------------------------- VPFDataQualityTable ----------------------------
//------------------------------------------------------------------------------

VPFDataQualityTable::VPFDataQualityTable(const CString& path_to_table)
: VPFRecordset(path_to_table)
{
}


VPFDataQualityTable::~VPFDataQualityTable()
{
}

//----------------------------- on_set_position --------------------------------
void VPFDataQualityTable::on_set_position()
{
   VPFVariant *data;

   data = get_field_value(_T("revision_date"));
	if (data)
	   m_revision_date = data->m_text;
}


//----------------------------------- open -------------------------------------
int VPFDataQualityTable::open()
{
   return VPFRecordset::open(_T("\\dqt"));
}



//------------------------------------------------------------------------------
//------------------------ VPFBoundingRectangleTable ---------------------------
//------------------------------------------------------------------------------
//VPFBoundingRectangleTable::VPFBoundingRectangleTable(VPFDatabase* db)
//: VPFRecordset(db)
//{
//}

VPFBoundingRectangleTable::VPFBoundingRectangleTable(VPFLibrary* library)
: VPFRecordset(library)
{
}

VPFBoundingRectangleTable::VPFBoundingRectangleTable(const CString& path_to_table)
: VPFRecordset(path_to_table)
{
}



//------------------------ ~VPFBoundingRectangleTable --------------------------
VPFBoundingRectangleTable::~VPFBoundingRectangleTable()
{
}

//----------------------------- on_set_position --------------------------------
void VPFBoundingRectangleTable::on_set_position()
{
   VPFVariant *value;

   value = get_field_value(_T("xmin"));
   if (value->is_float_short())
      m_bounds.ll.lon = value->m_float_short;
   else
      m_bounds.ll.lon = value->m_float_long;

   value = get_field_value(_T("xmax"));
   if (value->is_float_short())
      m_bounds.ur.lon = value->m_float_short;
   else
      m_bounds.ur.lon = value->m_float_long;


   value = get_field_value(_T("ymin"));
   if (value->is_float_short())
      m_bounds.ll.lat = value->m_float_short;
   else
      m_bounds.ll.lat = value->m_float_long;

   value = get_field_value(_T("ymax"));
   if (value->is_float_short())
      m_bounds.ur.lat = value->m_float_short;
   else
      m_bounds.ur.lat = value->m_float_long;
}

//--------------------------------- on_open ------------------------------------
void VPFBoundingRectangleTable::on_open()
{
   // Don't call verify_filename(), because this particular class
   // can encapsulate an Edge Bounding Rectangle Table (ebr) or
   // a Face Bounding Rectangle Table (fbr)

   verify_field(_T("id"),   VPF_INT_LONG,                     0, VPF_KEY_PRIMARY);
   verify_field(_T("xmin"), VPF_FLOAT_LONG | VPF_FLOAT_SHORT, 0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("ymin"), VPF_FLOAT_LONG | VPF_FLOAT_SHORT, 0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("xmax"), VPF_FLOAT_LONG | VPF_FLOAT_SHORT, 0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("ymax"), VPF_FLOAT_LONG | VPF_FLOAT_SHORT, 0, VPF_KEY_NON_UNIQUE);
}

//------------------------------ get_class_name --------------------------------
const TCHAR * VPFBoundingRectangleTable::get_class_name()
{
   return _T("VPFBoundingRectangleTable");
}

//---------------------------------- bounds ------------------------------------
d_geo_rect_t VPFBoundingRectangleTable::bounds(void)
{
   return m_bounds;
}

//------------------------------------ ll --------------------------------------
d_geo_t VPFBoundingRectangleTable::ll()
{
   return m_bounds.ll;
}

//------------------------------------ ur --------------------------------------
d_geo_t VPFBoundingRectangleTable::ur()
{
   return m_bounds.ur;
}

//----------------------------------- xmin -------------------------------------
degrees_t VPFBoundingRectangleTable::min_lon()
{
   return m_bounds.ll.lon;
}

//----------------------------------- xmax -------------------------------------
degrees_t VPFBoundingRectangleTable::max_lon()
{
   return m_bounds.ur.lon;
}

//----------------------------------- ymin -------------------------------------
degrees_t VPFBoundingRectangleTable::min_lat()
{
   return m_bounds.ll.lat;
}

//----------------------------------- ymax -------------------------------------
degrees_t VPFBoundingRectangleTable::max_lat()
{
   return m_bounds.ur.lat;
}


//------------------------------------------------------------------------------
//------------------------ VPFCoverageAttributeTable ---------------------------
//------------------------------------------------------------------------------
VPFCoverageAttributeTable::VPFCoverageAttributeTable(VPFLibrary* lib)
: VPFRecordset(lib), m_library(lib)
{
}

VPFCoverageAttributeTable::VPFCoverageAttributeTable(const CString& path_to_table)
: VPFRecordset(path_to_table)
{
}


//------------------------ ~VPFCoverageAttributeTable ---------------------------
VPFCoverageAttributeTable::~VPFCoverageAttributeTable()
{
}

//----------------------------------- open -------------------------------------
int VPFCoverageAttributeTable::open()
{
   return VPFRecordset::open(/*m_library->name() + */"cat");
}

//----------------------------- on_set_position --------------------------------
void VPFCoverageAttributeTable::on_set_position()
{
   VPFVariant *value;

   value = get_field_value(_T("coverage_name"));
   m_coverage_name = value->m_text;

   value = get_field_value(_T("description"));
   m_description = value->m_text;

   value = get_field_value(_T("level"));

	// dvl :: 00/5/11
	// Set value based on type of the variant.  VVOD uses a long int 
	// for topology while vmap uses a short int.
   m_topology_level = (value->is_int_long()) ? value->m_int_long : value->m_int_short;
}

//--------------------------------- on_open ------------------------------------
void VPFCoverageAttributeTable::on_open()
{
   // The filename must be "cat"
   verify_filename(_T("cat"));

   // Verify that the fields are what they should be
   // Field Name          FieldType    KeyType
   // 0     ID              I            U
   // 1     COVERAGE_NAME   T,8          P
   // 2     DESCRIPTION     T,*          N
   // 3     LEVEL           I            N

//x-x-x-x-x These appear to be different in VVOD and VMAP
//   verify_field("id",            VPF_INT_LONG,  0, VPF_KEY_UNIQUE);
//   verify_field("coverage_name", VPF_TEXT,      8, VPF_KEY_PRIMARY);
   verify_field(_T("description"),   VPF_TEXT,     -1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("description"),   VPF_TEXT,      0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("level"),         VPF_INT_LONG,  0, VPF_KEY_NON_UNIQUE);
}

//------------------------------ get_class_name --------------------------------
const TCHAR * VPFCoverageAttributeTable::get_class_name()
{
   return _T("VPFCoverageAttributeTable");
}

//------------------------------ coverage_name ---------------------------------
CString VPFCoverageAttributeTable::coverage_name()
{
   return m_coverage_name;
}

//------------------------------- description ----------------------------------
CString VPFCoverageAttributeTable::description()
{
   return m_description;
}

//------------------------------ topology_level --------------------------------
int VPFCoverageAttributeTable::topology_level()
{
   return m_topology_level;
}


//------------------------------------------------------------------------------
//---------------------------- VPFNarrativeTable -------------------------------
//------------------------------------------------------------------------------
VPFNarrativeTable::VPFNarrativeTable(CString table_name,
   VPFRecordset* main_table)
: VPFRecordset(main_table->m_dbpath)
{
}

//--------------------------- ~VPFNarrativeTable -------------------------------
VPFNarrativeTable::~VPFNarrativeTable()
{
}


//------------------------------------------------------------------------------
//----------------------------- VPFFeatureTable --------------------------------
//------------------------------------------------------------------------------
VPFFeatureTable::VPFFeatureTable(VPFCoverage* cov, CString name)
: VPFRecordset(cov->get_library()), //cov->get_database()),
  m_coverage(cov)
{
   m_name = name;
}

//----------------------------- ~VPFFeatureTable -------------------------------
VPFFeatureTable::~VPFFeatureTable()
{
}

//#include "err.h"
short VPFFeatureTable::get_field_value_short(int num_rows, int TileLinkField)
{
	ASSERT(is_open());

	// 1.  Move to start of record
   if (is_eof())
   {
//      ERR_report("Cannot perform VPFRecordset move operation.  Recordset is at EOF.");

      return FAILURE;
   }

   if (is_bof())
   {
//      ERR_report("Cannot perform VPFRecordset move operation.  Recordset is at BOF.");

      return FAILURE;
   }

   m_current += num_rows;

   // If we're above the upper limit, set current
   // to INT_MAX and seek to the end of the file
   if (m_current >= m_record_count)
   {
      m_current = INT_MAX;
      m_current_file_pos = m_file_ptr + m_file_size - 1;
   }
   // If we're below the lower limit, set current to -1
   // and seek to the beginning of the data area
   else if (m_current < 0)
   {
      m_current = -1;
		m_current_file_pos = m_start_file_pos;
   }
   // Else seek to the record
   else
   {
      seek_to_record(m_current);
   }

	// 2. Skip to start of TileLinkField
	for(int i=0;i<TileLinkField;i++)
	{
		if (m_fields[i].m_type == VPF_TRIPLET_ID)
		{
			char triplet_id = *(char *)m_current_file_pos;
			m_current_file_pos += sizeof(char);

			int mask[] = { 12, 48, 192 };
			
			for (int i = 1; i < 4; ++i)
			{
				int val = (triplet_id & mask[i-1]) >> (i << 1);
				if (val == 3)
					val = 4;
				m_current_file_pos += val;
			}
		}
		else if (m_fields[i].m_length >= 0)
			m_current_file_pos += m_fields[i].m_length;
		else
		{
			const VPFType type = m_fields[i].m_type;
			int length = *(long int *)m_current_file_pos;
			m_current_file_pos += sizeof(long int);
			
			if (type == VPF_3COORD_SHORT_FLOAT)
				length *= 12;
			else if (type == VPF_2COORD_SHORT_FLOAT)
				length *=  8;
			else if (type == VPF_3COORD_LONG_FLOAT)
				length *= 24;
			else if (type == VPF_2COORD_LONG_FLOAT)
				length *= 16;

			m_current_file_pos += length;
		}
	}

	// 3.  Return value
	return *(short *)m_current_file_pos;
}

//----------------------------------- open -------------------------------------
int VPFFeatureTable::open()
{
	int RetVal = VPFRecordset::open(/*m_coverage->get_library_name() + "\\" +*/
      m_coverage->name() + _T("\\") + m_name);
	
   // DVL :: 20020924
   //
   // I am finding many tiles now that do NOT have their feature tables sorted
   // by tile ID.  So far, the feature tables hold the tile_id's in a 
   // contiguous group, but I doubt this will remain true after a VDU 
   // update.  There are problems in DNC 17 with updates, so we will have
   // to see what happens.
   //
   // Again, the index table entry to the database will probably fix this, 
   // but that remains unsupported.  ALL the code below is simply making
   // a simple index into the feature table.  This will not handle non-
   // contiguous tile feature groups
	
	
   // Assemble a list of the tile starting locations in the table
	
   // it may be an empty file.  The move here may not be necessary, but it is cheap		
	m_current = 0;
	
   // It may be a joined feature table (no tile_id entry)
   const VPFFieldInfo * fi = get_field_info(_T("tile_id"));
	
   if (!is_eof() && (fi != NULL))
   {
      // We have a feature table that can be indexed by tile_id
		
      // get the tile_id field ordinal for access speed
      int TileLinkField = fi->m_ordinal_position;
      
		
      // Scan the file for beginning tile entries.  Add an entry to the map
      // for each new tile and warn if duplicates are found during debug.
      
      // get the first entry in the file
		int CurrentTile = get_field_value_short(0, TileLinkField);
//      m_TilePosition.SetAt(CurrentTile, m_current);
      m_TilePosition.insert(std::map<int,int>::value_type(CurrentTile, m_current));

		
      int LastPos, NextTile;
      const int TotalRecords = get_record_count();
      const int JumpDist = 20; // Arbitrary count.  Can we make it adaptive?
		
		m_current++;
		
      while (m_current < TotalRecords)
      {
         LastPos = m_current;
			
         // Look for the possibility to skip <some arbitarary count> rows
         // save the last position in case we go too far
         if (LastPos + JumpDist < TotalRecords)
         {
				NextTile = get_field_value_short(JumpDist, TileLinkField);
				
				
				
            if (NextTile == CurrentTile)
               continue;
				
            // JumpDist was too far, slow search to 1 for next record.
				m_current = LastPos;
            for (int i = 0; i<JumpDist; i++)
            {
					NextTile = get_field_value_short(0, TileLinkField);
					
               if (NextTile != CurrentTile)
                  break;
					
               m_current++;
            }
         }
			else
				NextTile = get_field_value_short(0, TileLinkField);
			
         if (CurrentTile != NextTile)
         {
            CurrentTile = NextTile;
//            ASSERT(m_TilePosition.Lookup(CurrentTile, temp) == false);
            //m_TilePosition.SetAt(CurrentTile, m_current);
            m_TilePosition.insert(std::map<int,int>::value_type(CurrentTile, m_current));
         }
			
			m_current++;
      }
   }
	
   move_first();
   return RetVal;
}

//--------------------------------- on_open ------------------------------------
void VPFFeatureTable::on_open()
{
}

//------------------------------ get_class_name --------------------------------
const TCHAR * VPFFeatureTable::get_class_name()
{
   return _T("VPFFeatureTable");
}

//----------------------------- on_set_position --------------------------------
void VPFFeatureTable::on_set_position()
{
}

//------------------------- get_attribute_info_array ---------------------------
//const CArray<VPFFieldInfo, VPFFieldInfo&> const * VPFFeatureTable::get_attribute_info_array()
std::vector<VPFFieldInfo> const * VPFFeatureTable::get_attribute_info_array()
{
   return &m_fields;
}

/*
//---------------------------- get_attribute_list ------------------------------
CList<VPFVariant, VPFVariant&>* VPFFeatureTable::get_attribute_list()
{
   CList<VPFVariant, VPFVariant&>* list = new CList<VPFVariant, VPFVariant&>;

   for (int index = 0; index < m_fields.size(); index++)
   {
      // DVL :: No modification, CList makes its own copy of the returned VPFVariant&
      // since the list is declared to receive items by value.  However, this may
      // change with the implementation of CList.
		VPFVariant variant;
		variant = *(get_field_value(index));
      list->AddTail(const_cast<VPFVariant&>(variant));
   }

   return list;
}
*/

//------------------------------------------------------------------------------
//------------------------ VPFFeatureClassSchemaTable --------------------------
//------------------------------------------------------------------------------
VPFFeatureClassSchemaTable::VPFFeatureClassSchemaTable(VPFCoverage* cov)
: VPFRecordset(cov->get_library()), //cov->get_database()), 
  m_coverage(cov)
{
   m_feature_class.Empty();
   m_table1.Empty();
   m_table1_key.Empty();
   m_table2.Empty();
   m_table2_key.Empty();
}

//----------------------- ~VPFFeatureClassSchemaTable --------------------------
VPFFeatureClassSchemaTable::~VPFFeatureClassSchemaTable()
{
}

//----------------------------------- open -------------------------------------
void VPFFeatureClassSchemaTable::open()
{
   VPFRecordset::open(/*m_coverage->get_library_name() + "\\" +*/
      m_coverage->name() + _T("\\fcs"));
}

//-------------------------- get_feature_class_list ----------------------------
int VPFFeatureClassSchemaTable::get_feature_class_list(CStringList &list)
{
   // DVL :: 00/09/14
   // This function is improper in nature.  The only information that can be 
   // gleamed from the returned list is that those classes are represented in this
   // table.  The list is neither guaranteed to be complete nor in order.

   int return_value = SUCCESS;

   for (int i = 0; i < get_record_count(); i++)
   {
      set_absolute_position(i);

      const bool already_present_in_list = (NULL != list.Find(m_feature_class));

      if (!already_present_in_list)
         list.AddTail(m_feature_class);
   }

   if (list.GetCount() < 1)
      return_value = FAILURE;

   return return_value;
}

//--------------------------------- on_open ------------------------------------
void VPFFeatureClassSchemaTable::on_open()
{
   verify_filename(_T("fcs"));

   const int type_combo =
      VPF_TEXT | VPF_LATIN1_TEXT | VPF_FULL_LATIN_TEXT | VPF_MULTI_LINGUAL_TEXT;

   verify_field(_T("id"),            VPF_INT_LONG, 0, VPF_KEY_PRIMARY);
   verify_field(_T("feature_class"), type_combo,   8, VPF_KEY_NON_UNIQUE);
   verify_field(_T("table1"),        type_combo,  12, VPF_KEY_NON_UNIQUE);
   verify_field(_T("table1_key"),    type_combo,   0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("table2"),        type_combo,  12, VPF_KEY_NON_UNIQUE);
   verify_field(_T("table2_key"),    type_combo,   0, VPF_KEY_NON_UNIQUE);
}

//----------------------------- on_set_position --------------------------------
void VPFFeatureClassSchemaTable::on_set_position()
{
   VPFVariant *value;

   value = get_field_value(_T("feature_class"));
   m_feature_class = value->m_text;

   value = get_field_value(_T("table1"));
   m_table1 = value->m_text;

   value = get_field_value(_T("table1_key"));
   m_table1_key = value->m_text;

   value = get_field_value(_T("table2"));
   m_table2 = value->m_text;

   value = get_field_value(_T("table2_key"));
   m_table2_key = value->m_text;
}

//------------------------------ get_class_name --------------------------------
const TCHAR * VPFFeatureClassSchemaTable::get_class_name()
{
   return _T("VPFFeatureClassSchemaTable");
}

//------------------------------------------------------------------------------
//---------------------- VPFFeatureClassAttributeTable -------------------------
//------------------------------------------------------------------------------
VPFFeatureClassAttributeTable::VPFFeatureClassAttributeTable(VPFCoverage* cov)
: VPFRecordset(cov->get_library()) //cov->get_database())
   ,m_coverage(cov)
{

}

//---------------------- ~VPFFeatureClassAttributeTable ------------------------
VPFFeatureClassAttributeTable::~VPFFeatureClassAttributeTable()
{
}

//----------------------------- on_set_position --------------------------------
void VPFFeatureClassAttributeTable::on_set_position()
{
   m_feature_class_name = get_field_value(_T("fclass"))->m_text;

   m_feature_class_type = get_field_value(_T("type"))->m_text[0];

   m_feature_class_desc = get_field_value(_T("descr"))->m_text;
}

//--------------------------------- on_open ------------------------------------
void VPFFeatureClassAttributeTable::on_open()
{
   verify_filename(_T("fca"));

   const int all_text_types =
      VPF_TEXT | VPF_LATIN1_TEXT | VPF_FULL_LATIN_TEXT | VPF_MULTI_LINGUAL_TEXT;

   verify_field(_T("id"),     VPF_INT_LONG,    0, VPF_KEY_PRIMARY);
   verify_field(_T("fclass"), all_text_types,  8, VPF_KEY_UNIQUE);
   verify_field(_T("type"),   VPF_TEXT,        1, VPF_KEY_NON_UNIQUE);
   verify_field(_T("descr"),  all_text_types, -1, VPF_KEY_NON_UNIQUE);
}

//------------------------------ get_class_name --------------------------------
const TCHAR * VPFFeatureClassAttributeTable::get_class_name()
{
   return _T("VPFFeatureClassAttributeTable");
}

//-------------------------- get_feature_class_list ----------------------------
int VPFFeatureClassAttributeTable::get_feature_class_list(CStringList &list)
{
   // DVL :: 00/09/14
   // The feature class attribute table contains a list of the feature classes
   // included in the coverage within which the file is contained
   //      i.e.
   //          "C:\VMap\VMAPLV0\NOAMER\BND\fca" has feature classes for boundaries

   int return_value = SUCCESS;

   if (! this->is_open())
   {
      //if (m_coverage->get_library()->name() != "rference")
      {
         // These should both always be present, but we may need to check for their
         // existence.  Other coverages may also be present.  They are in the libref, 
         // but also in the rference coverage

         list.AddTail(_T("libref"));
         list.AddTail(_T("libreft"));
      }
/*      else
      {
         // testing to load this in rference library
         list.AddTail("polbnda");
      }
*/
   }
   else
   {
      const int size = get_record_count();

      for (int ct=0; ct<size; ct++)
      {
         set_absolute_position(ct);

         list.AddTail(m_feature_class_name);
      }

      if (list.GetCount() < 1)
         return_value = FAILURE;
   }

   return return_value;
}

//----------------------------------- open -------------------------------------
int VPFFeatureClassAttributeTable::open()
{
   return VPFRecordset::open(/*m_coverage->get_library_name() + "\\" +*/
      m_coverage->name() + _T("\\fca"));
}

//---------------------------- feature_class_name ------------------------------
CString VPFFeatureClassAttributeTable::feature_class_name()
{
   return m_feature_class_name;
}

//------------------------ feature_class_description ---------------------------
CString VPFFeatureClassAttributeTable::feature_class_description()
{
   return m_feature_class_desc;
}

//------------------------------------------------------------------------------
//--------------------------- VPFFeatureIndexTable -----------------------------
//------------------------------------------------------------------------------
VPFFeatureIndexTable::VPFFeatureIndexTable(VPFCoverage* cov)
: VPFRecordset(cov->get_library()), //cov->get_database()),
  m_coverage(cov)
{
}

//-------------------------- ~VPFFeatureIndexTable -----------------------------
VPFFeatureIndexTable::~VPFFeatureIndexTable()
{
}

//----------------------------------- open -------------------------------------
void VPFFeatureIndexTable::open(const CString &table_name)
{
   VPFRecordset::open(/*m_coverage->get_library_name() + "\\" +*/
      m_coverage->name() + _T("\\") + table_name + _T(".fit"));
}

//----------------------------- on_set_position --------------------------------
void VPFFeatureIndexTable::on_set_position()
{
   VPFVariant *value;

   value = get_field_value(_T("prim_id"));
   m_primitive_id = value->m_int_long;

   value = get_field_value(_T("tile_id"));
   m_tile_id = value->m_int_short;

   value = get_field_value(_T("fc_id"));
   m_feature_class_id = value->m_int_long - 1;// convert to a 0-based index

   value = get_field_value(_T("feature_id"));
   m_feature_id = value->m_int_long;
}

//--------------------------------- on_open ------------------------------------
void VPFFeatureIndexTable::on_open()
{
   verify_field(_T("id"),         VPF_INT_LONG,  0, VPF_KEY_PRIMARY);
   verify_field(_T("prim_id"),    VPF_INT_LONG,  0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("tile_id"),    VPF_INT_SHORT, 0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("fc_id"),      VPF_INT_LONG,  0, VPF_KEY_NON_UNIQUE);
   verify_field(_T("feature_id"), VPF_INT_LONG,  0, VPF_KEY_NON_UNIQUE);
}

//------------------------------ get_class_name --------------------------------
const TCHAR * VPFFeatureIndexTable::get_class_name()
{
   return _T("VPFFeatureIndexTable");
}

//----------------------------- get_feature_class_id ---------------------------
long VPFFeatureIndexTable::get_feature_class_id() const
{
   return m_feature_class_id;
}

//------------------------------ get_feature_id --------------------------------
long VPFFeatureIndexTable::get_feature_id() const
{
   return m_feature_id;
}




//------------------------------------------------------------------------------
//-------------------------------VPFJoinTable  ---------------------------------
//------------------------------------------------------------------------------


//-------------------------------VPFJoinTable  ---------------------------------
VPFJoinTable::VPFJoinTable(const CString &TableName, 
         const CString &FirstField, const CString & /*SecondField*/) 
   : VPFRecordset(TableName.Left(TableName.ReverseFind('\\')+1)),
      m_isopen(false)

{
   // DVL :: 03-22-2001
   // I should make a note here on how this function works as I keep
   // confusing myself. It is important to note that we are creating the
   // join table on having read that it is needed in the fcs, rather than
   // when its link to the primitive file is declared.  This means that
   // The LeftField being passed in here is the field name in the join
   // table whose value is needed in the primitive table.
   //
   // It would be more proper to do this in 2 steps, on the first, the
   // link from the feature table to the join table is established (note
   // that I have the "search" column fixed as column 1 (int or short)).
   // The second step should establish the link from the join table to the
   // primitive table.  We get 1 field name from each of the 2 entries
   // in the fcs, i.e. 
   //    feature table to join table entry         id -> utill.lft_id
   //    join table to primitive table entry       edg_id -> id

   // The dividing of the fname is because VPFRecordSet expects to get the
   // path and name separately
   int count = TableName.GetLength();
   int Start = TableName.ReverseFind('\\');
   m_isopen = VPFRecordset::open(TableName.Right(count-Start-1)) == SUCCESS;

   if (!m_isopen)
      return;

   const VPFFieldInfo* fi;

   // See comment above, leave 2 step initialization of the join table
   // for later if this poses a problem,... 
   //
   //fi = get_field_info(FirstField);
   //if (!fi)
   //{
   //   ASSERT(false); // We should not get here!
   //   close();
   //   return;
   //}
   //m_LeftFieldID = fi->m_ordinal_position;
   m_LeftFieldID=1;

   // First field matching RightField ID is due to comment above!
   fi = get_field_info(FirstField);
   if (!fi)
   {
      ASSERT(false); // We should not get here!
      close();
      return;
   }
   m_RightFieldID = fi->m_ordinal_position;

   m_isopen = true;
}


bool VPFJoinTable::BeginSearch(int TileID)
{
   // Open the join table here and move to the first matching record
   // of the tile.

   // return false   : no records found or table not found
   // return true    : table is at first record

   if (!is_open())
      return (false);

   if (is_eof())
      move_first();

   m_SearchTile = TileID;

   const VPFVariant* Temp;
   Temp = get_field_value(2);
   int TempVal = (Temp->is_int_long()) ? Temp->m_int_long : Temp->m_int_short;

   if (TempVal == m_SearchTile)
   {
      Temp = get_field_value(m_LeftFieldID);
      m_SearchValue = (Temp->is_int_long()) ? 
                  Temp->m_int_long : Temp->m_int_short;

      // Otherwise, we are already pointed at the next feature of this tile
      return true;
   }

   // Start the search for the first record at the beginning of the file.

   // OPTIMIZATION NEEDED
   // We should not really move to the beginning of the file every time!
   move_first();
   m_SearchValue = -1;

   // First, move to the current tile.
   while (!is_eof())
   {
      // The structure of the if is uneven on purpose to take advantage
      // of a short circuit on the second one.  We only want to get to the
      // else if in the case that the value is_int_long().  Otherwise,
      // the loop is continued if the value is_in_short() but didn't match
      Temp = get_field_value(2);
      TempVal = (Temp->is_int_long()) ? Temp->m_int_long : Temp->m_int_short;
      if (TempVal==m_SearchTile)
      {
         Temp = get_field_value(m_LeftFieldID);
         m_SearchValue = (Temp->is_int_long()) ? 
                  Temp->m_int_long : Temp->m_int_short;

         return true;
      }

      move_next();
   }

   return false;
}


bool VPFJoinTable::GetNextEntry(int& FeatureID, int& RightValue)
{
   // return value   : True - RightValue holds the next matching record#
   //                : False - done processing for "m_m_SearchValue"

   // Repeatedly Call this function to get the next "2nd table" ID
   // return false/0 : no more records found
   // return (+) int : record number in primitive table

   // No reads past EOF!
   if (is_eof())
      return false;


   const VPFVariant* Temp;
   int   TempValue;

   Temp = get_field_value(m_LeftFieldID);
   TempValue = (Temp->is_int_long()) ? Temp->m_int_long : Temp->m_int_short;

   if (m_SearchValue == -1)
   {
      ASSERT(false);
   }
   else if (TempValue != m_SearchValue)
   {
      // Check for a FeatureID change
      FeatureID = TempValue;
      m_SearchValue = TempValue;
      return false;
   }

   FeatureID = TempValue;

   // Check for a TileID change
   Temp = get_field_value(2);
   TempValue = (Temp->is_int_long()) ? Temp->m_int_long : Temp->m_int_short;

   if (TempValue !=m_SearchTile)
      return false;

   // Get the value for the primitive (right field)
   Temp = get_field_value(m_RightFieldID);
   RightValue = (Temp->is_int_long()) ? Temp->m_int_long : Temp->m_int_short;

   // Set the file to point at the next record
   move_next();

   return true;
}


int VPFJoinTable::Delineation()
{
   switch(m_file_name.Right(3)[0])
   {
   case 'p': return 1;
   
   case 'l': return 2;

   case 'a': return 3;

   case 't': return 0;

   default:
      // Undefined table!  Add it to the proper place above!
      ASSERT(false);
      return(0);
   }
}

bool VPFJoinTable::GetTileEntries(const int& TileID, 
                                  int& StartRecord, int& Count)
{
   // Search the join table for entries relating to the supplied TileID
   //
   // Return true / false on entries being found / not found
   // Return the starting record number in the feature table
   // return the number of related entries in the feature table

   if (!is_open())
      return (false);

   for (int passes=0; passes<2; passes++)
   {
      // Move to the beginning of the file to begin the search (since we are
      // not yet using a thematic index file on the join table.  Watch for eof
      if (is_eof())   
         move_first();

      // Initialization
      Count=0;
      StartRecord=-1;
      int Position=0;
      int LastFeatureID=-1;

      // Search for the first matching record.
      while (!is_eof())
      {  
         VPFVariant* Temp;
         int iFeature, iTileID;

         Temp = get_field_value(2); // tile_id position in the file
         iTileID = (Temp->is_int_long()) ? Temp->m_int_long : Temp->m_int_short;

         if (iTileID == TileID)
         {
            // This record relates to the tile we are looking for

            Temp = get_field_value(1); // Feature_id position in the file
            iFeature = (Temp->is_int_long()) ? Temp->m_int_long : Temp->m_int_short;

            if (StartRecord==-1)
            {
               // Set the StartRecord THE FIRST TIME the TileID is found
               StartRecord = iFeature;
            }

            if (iFeature != LastFeatureID)
            {
               LastFeatureID = iFeature;
               Count++;
            }
         }

         move_next();
      
         Position++; // Start Counting at 0;
      }

      if (Count>0)
         break;
   }

   return (Count>0);
}
