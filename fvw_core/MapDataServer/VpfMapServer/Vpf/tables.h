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

#if !defined(TABLES_H__54E720F0_8652_11D3_8663_00105A9B4838__INCLUDED_)
#define TABLES_H__54E720F0_8652_11D3_8663_00105A9B4838__INCLUDED_

#include "vpfrcset.h"

// Forward Declarations
class VPFDatabase;
class VPFCoverage;
class VPFFeatureClassSchemaTable;

//------------------------------------------------------------------------------
//------------------------------ VPFNarrativeTable -----------------------------
//------------------------------------------------------------------------------
// The narrative table is a table that is used to provide "other" data about
// a table or field. The VPF spec contains this table as a way for a certain
// product specification to provide data in a standard VPF table that the
// creators of VPF could not forsee, or did not think was generic enought to
// include in the VPF spec. Products may use this table in any way they see
// fit, as long as the table complies with the specs for a VPF table.
//------------------------------------------------------------------------------
class VPFNarrativeTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
   VPFNarrativeTable(CString table_name, VPFRecordset* main_table);
   virtual ~VPFNarrativeTable();

//--- Public Methods --------------------------------------
public:

//--- Protected Methods -----------------------------------
protected:

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
};


//------------------------------------------------------------------------------
//------------------------- VPFLibraryAttributeTable ---------------------------
//------------------------------------------------------------------------------
// <This is a general overview of the class and its usage.>
//------------------------------------------------------------------------------
class VPFLibraryAttributeTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
   //VPFLibraryAttributeTable(VPFDatabase* db);
	VPFLibraryAttributeTable(const CString &path);

   virtual ~VPFLibraryAttributeTable();

//--- Public Methods --------------------------------------
public:
   void           open        ();
   CString        library_name();
   d_geo_rect_t   bounds      ();

//--- Protected Methods -----------------------------------
protected:
   virtual void on_set_position();
   virtual void on_open();
   virtual const TCHAR *get_class_name();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   CString        m_library_name;
   d_geo_rect_t   m_bounds;
};


//------------------------------------------------------------------------------
//------------------------ VPFCoverageAttributeTable ---------------------------
//------------------------------------------------------------------------------
// <This is a general overview of the class and its usage.>
//------------------------------------------------------------------------------
class VPFCoverageAttributeTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
   VPFCoverageAttributeTable(VPFLibrary* lib);
	VPFCoverageAttributeTable(const CString& path_to_table);
   virtual ~VPFCoverageAttributeTable();

//--- Public Methods --------------------------------------
public:
   int            open          ();
   CString        coverage_name ();
   CString        description   ();
   int            topology_level();

//--- Protected Methods -----------------------------------
protected:
   virtual void on_set_position();
   virtual void on_open();
   virtual const TCHAR *get_class_name();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   CString m_coverage_name;
   CString m_description;
   int     m_topology_level;

   VPFLibrary *m_library;
};


//------------------------------------------------------------------------------
//-------------------------- VPFDatabaseHeaderTable ----------------------------
//------------------------------------------------------------------------------
// <This is a general overview of the class and its usage.>
//------------------------------------------------------------------------------
class VPFDatabaseHeaderTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
   VPFDatabaseHeaderTable(VPFDatabase* db);
	VPFDatabaseHeaderTable(const CString& path_to_table);
   virtual ~VPFDatabaseHeaderTable();

//--- Public Methods --------------------------------------
public:
   int open();

	CString get_database_name() { return m_database_name; }

//--- Protected Methods -----------------------------------
protected:
   virtual void on_set_position();
   virtual void on_open();
   virtual const TCHAR *get_class_name();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
	CString m_database_name;
};

//------------------------------------------------------------------------------
//-------------------------- VPFLibraryHeaderTable ----------------------------
//------------------------------------------------------------------------------
// <This is a general overview of the class and its usage.>
//------------------------------------------------------------------------------
class VPFLibraryHeaderTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
   VPFLibraryHeaderTable(VPFLibrary* pLibrary);
	VPFLibraryHeaderTable(const CString& path_to_table);
   virtual ~VPFLibraryHeaderTable();

//--- Public Methods --------------------------------------
public:
   int open();

	CString get_product_type() { return m_product_type; }
	CString get_library_name() { return m_library_name; }
	CString get_library_description() { return m_library_description; }
	CString get_source_series() { return m_source_series; }
	CString get_source_id() { return m_source_id; }
	CString get_source_name() { return m_source_name; }
	CString get_edition_number() { return m_edition_number; }

//--- Protected Methods -----------------------------------
protected:
   virtual void on_set_position();
   virtual void on_open();
   virtual const TCHAR *get_class_name();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   VPFLibrary *m_library;


	CString m_product_type;
	CString m_library_name;
	CString m_library_description;
	CString m_source_series;
	CString m_source_id;
	CString m_source_name;
	CString m_edition_number;
};


class VPFDataQualityTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
	VPFDataQualityTable(const CString& path_to_table);
   virtual ~VPFDataQualityTable();

//--- Public Methods --------------------------------------
public:
   int open();

	CString get_revision_date()
	{
		return m_revision_date;
	}

//--- Protected Methods -----------------------------------
protected:
   virtual void on_set_position();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   VPFLibrary *m_library;

	CString m_revision_date;
};


//------------------------------------------------------------------------------
//------------------------ VPFBoundingRectangleTable ---------------------------
//------------------------------------------------------------------------------
// VPF has two types of bounding rectangle tables: the Face Bounding Rectangle
// table, and the Edge Bounding Rectangle table. They both conform to the same
// specification for a bounding rectangle table. The table maintains a 1-to-1
// relationship with the appropriate primitive table. e.g. Record #14 in the
// Edge Bounding Rectangle table describes the bounding box for the fourteenth
// record in the Edge table.
//------------------------------------------------------------------------------
class VPFBoundingRectangleTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
   VPFBoundingRectangleTable(VPFDatabase* db);
   VPFBoundingRectangleTable(VPFLibrary* library);
	VPFBoundingRectangleTable(const CString& path_to_table);

   virtual ~VPFBoundingRectangleTable();

//--- Public Methods --------------------------------------
public:
   d_geo_rect_t   bounds(void);
   d_geo_t        ll       ();
   d_geo_t        ur       ();
   degrees_t      min_lon  ();
   degrees_t      max_lon  ();
   degrees_t      min_lat  ();
   degrees_t      max_lat  ();

//--- Protected Methods -----------------------------------
protected:
   virtual void         on_set_position   ();
   virtual void         on_open           ();
   virtual const TCHAR * get_class_name    ();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   d_geo_rect_t m_bounds;
};


//------------------------------------------------------------------------------
//----------------------------- VPFFeatureTable --------------------------------
//------------------------------------------------------------------------------
// <This is a general overview of the class and its usage.>
//------------------------------------------------------------------------------
class VPFFeatureTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
   VPFFeatureTable(VPFCoverage* cov, CString name);
   VPFFeatureTable(VPFLibrary* library);
   virtual ~VPFFeatureTable();

//--- Public Methods --------------------------------------
public:
   int open();
   std::vector<VPFFieldInfo> const * get_attribute_info_array();
//  CList<VPFVariant, VPFVariant&>* get_attribute_list();

	// instead of constructing a CList of VPFVariant structs, the caller of 
	// get_attribute_list can simply can these methods to retrieve a pointer
	// to the requested VPFVariant -- George
	int num_fields() { return m_fields.size(); }
	VPFVariant *get_attribute_at(int i) { return m_record_contents.data[i]; }
	VPFFieldInfo& get_attribute_info_at(int i) { return m_fields[i]; }

   const int get_tile_start_feature(int TileID, int& StartFeature)
   {
      std::map<int, int>::iterator it = m_TilePosition.find(TileID);

      if ( it == m_TilePosition.end() )
         return FALSE;

      StartFeature = m_TilePosition[TileID];

      return TRUE;
   }

//--- Protected Methods -----------------------------------
protected:
   virtual void on_set_position();
   virtual void on_open();
   virtual const TCHAR *get_class_name();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   CString      m_name;
   VPFCoverage* m_coverage;
//   CMap<int, int, int, int> m_TilePosition;   // List of tiles in the table;
   std::map<int, int> m_TilePosition;   // List of tiles in the table;

	short get_field_value_short(int num_rows, int index);

public:
};


//------------------------------------------------------------------------------
//------------------------ VPFFeatureClassSchemaTable --------------------------
//------------------------------------------------------------------------------
// The Feature Class Schema is a required table in the coverage directory that
// describes the feature classes in the coverage.
//------------------------------------------------------------------------------
class VPFFeatureClassSchemaTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
   VPFFeatureClassSchemaTable(VPFCoverage* cov);
   void VPFFeatureClassSchema(VPFLibrary* library);
   virtual ~VPFFeatureClassSchemaTable();

//--- Public Methods --------------------------------------
public:
   int get_feature_class_list(CStringList &list);

   void open();

//--- Protected Methods -----------------------------------
protected:
   virtual void on_set_position();
   virtual void on_open();
   virtual const TCHAR *get_class_name();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   VPFCoverage* m_coverage;
   CString      m_feature_class;
   CString      m_table1;
   CString      m_table1_key;
   CString      m_table2;
   CString      m_table2_key;
};


//------------------------------------------------------------------------------
//---------------------- VPFFeatureClassAttributeTable -------------------------
//------------------------------------------------------------------------------
// The Feature Class Attribute table is an optional table in the VPF spec.  It
// is required to support the use of Feature Index tables.
//------------------------------------------------------------------------------
class VPFFeatureClassAttributeTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
   VPFFeatureClassAttributeTable(VPFCoverage* cov);
   virtual ~VPFFeatureClassAttributeTable();

//--- Public Methods --------------------------------------
public:
   int open();
   CString feature_class_name();
   CString feature_class_description();
   int get_feature_class_list(CStringList &list);

//--- Protected Methods -----------------------------------
protected:
   virtual void on_set_position();
   virtual void on_open();
   virtual const TCHAR *get_class_name();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   VPFCoverage* m_coverage;
   CString      m_feature_class_name;
   CString      m_feature_class_desc;
   TCHAR        m_feature_class_type;
};

//------------------------------------------------------------------------------
//--------------------------- VPFFeatureIndexTable -----------------------------
//------------------------------------------------------------------------------
// see MIL-STD-2407 section 5.4.3.1
//------------------------------------------------------------------------------
class VPFFeatureIndexTable : public VPFRecordset
{
//--- Construction ----------------------------------------
public:
   VPFFeatureIndexTable(VPFCoverage* cov);
   virtual ~VPFFeatureIndexTable();

//--- Public Methods --------------------------------------
public:
   void open(const CString &table_name);

   long get_feature_class_id() const;
   long get_feature_id() const;

//--- Private Methods -------------------------------------
private:
   virtual void on_set_position();
   virtual void on_open();
   virtual const TCHAR *get_class_name();

//--- Member Data -----------------------------------------
private:
   VPFCoverage* m_coverage;

   long m_primitive_id;
   long m_tile_id;
   long m_feature_class_id;
   long m_feature_id;
};


//------------------------------------------------------------------------------
//--------------------------- VPFJoinTable -------------------------------------
//------------------------------------------------------------------------------
// Usage:
// 1) create an empty table using the constructor.  Provide the Table path/name
//    and the field names.  FirstField
//
// Next, open a table providing the column names found in the the fcs
//
//

class VPFJoinTable : public VPFRecordset
{
public: 
   // table_name  :: Full path+name to the join table
   // FirstField  :: "left" or "search" lookup value
   // SecondField :: "right" or "joined"
   VPFJoinTable(const CString &TableName, const CString &FirstField, 
            const CString &SecondField);

   // TileID      :: Tile to find in join table
   bool BeginSearch(int TileID);

   // Repeatedly Call to get the next ID, Value is SearchValue
   bool GetNextEntry(int& FeatureID, int& RightValue); 

   int  Delineation(); // returns general feature delineation type (p/l/a/t)

   bool GetTileEntries(const int& TileID, int& StartRecord, int& Count);

   bool is_open() {return m_isopen;}

private:

   int   m_LeftFieldID;    // field order#, faster than using the field name
   int   m_RightFieldID;   // field order#

   int   m_SearchValue;    // key value (in LeftField)
   int   m_SearchTile;     // Tile to look on for feature
   bool  m_isopen;
};

#endif // !defined(TABLES_H__54E720F0_8652_11D3_8663_00105A9B4838__INCLUDED_)
