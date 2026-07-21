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

#if !defined(VPFDB_H__89E52559_8652_11D3_8663_00105A9B4838__INCLUDED_)
#define VPFDB_H__89E52559_8652_11D3_8663_00105A9B4838__INCLUDED_

//#include "common.h"     // for degrees_t
//#include "fvwutil.h"    // for C_3d_vertex
#include "vpf_d.h"

// Forward Declarations
class VPFRecordset;
class VPFDatabase;
class VPFLibrary;
class VPFCoverage;
class VPFLibraryAttributeTable;
class VPFBoundingRectangleTable;
class VPFCoverageAttributeTable;
class VPFFeatureClassSchemaTable;
class VPFFeatureClassAttributeTable;
class VPFFeatureTable;
class VPFFeatureIndexTable;
class VPFJoinTable;

/*
//------------------------------------------------------------------------------
//------------------------------- VPFException ---------------------------------
//------------------------------------------------------------------------------
// Just as CDaoRecordsets throw CDaoException objects, VPFRecordsets will throw
// VPFException objects in error situations.  This allows for a great deal of
// symmetry between the CDaoRecordset and VPFRecordset objects.
//------------------------------------------------------------------------------
class VPFException
{
//--- Construction ----------------------------------------
public:
   VPFException();
   virtual ~VPFException();

//--- Public Methods --------------------------------------
public:

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
public:
   CString m_error_message;
   int     m_error_type;
};
*/

//------------------------------------------------------------------------------
//----------------------------- VPFFeatureClass --------------------------------
//------------------------------------------------------------------------------
// This class contains all the data that defines a feature class in VPF.  It is
// the mechanism that controls the feature indexes and returns feature attribute
// data to the overlay for display.
//------------------------------------------------------------------------------
class VPFFeatureClass
{
//--- Construction ----------------------------------------
public:
   VPFFeatureClass(VPFCoverage* cov, VPFFeatureClassSchemaTable* fcs,
      const CString& name);
   virtual ~VPFFeatureClass();

//--- Public Methods --------------------------------------
public:
   const CString& GetName();  // (was MSVC-only extra qualification)
   CString        description();
   int            delineation() {return m_delineation;}
   VPFCoverage*   GetCoverage() {return m_coverage;}

   //.ecr.todo. this is public because VPFCoverage::get_primitive_info() needs
   // to access its data and I don't currently have time to design an access
   // scheme into VPFFeatureClass to handle it.
   VPFFeatureTable*  m_feature_table;  // feature table for this feature class
   VPFJoinTable*     m_join_table;     // Uses a join table to the primitives

   bool is_open() const { return m_isopen;}

//--- Protected Methods -----------------------------------
protected:

//--- Private Methods -------------------------------------
private:
   //
   // We don't want anyone to be able to create an
   // empty feature class or copy a feature class.
   //
   VPFFeatureClass() {ASSERT(false);}
   VPFFeatureClass(const VPFFeatureClass & /*fc*/){ASSERT(false);}
   VPFFeatureClass& operator=(VPFFeatureClass &fc){ASSERT(false); return fc;}

//--- Member Data -----------------------------------------
private:
   CString        m_name;           // the feature class name
   CString        m_description;    // "verbose name"
   VPFCoverage*   m_coverage;       //pointer to the containing coverage
   int            m_delineation;    // text/point/line/area 0..3

   //VPFFeatureClassAttributeTable* m_fca;

   bool           m_isopen;

public:
   CString        LinkField;  // Field name in feature table to go "out"
                              // with to either the join table or the primitives

   CString        PrimField;  // When using a join table, then 
};


//------------------------------------------------------------------------------
//------------------------------- VPFCoverage ----------------------------------
//------------------------------------------------------------------------------
// <This is a general overview of the class and its usage.>
//------------------------------------------------------------------------------
class VPFCoverage
{
//--- Construction ----------------------------------------
public:
   VPFCoverage(VPFLibrary *lib, VPFCoverageAttributeTable *cat);
   virtual ~VPFCoverage();

//--- Public Methods --------------------------------------
public:
   CString        name              () const;
   //VPFDatabase*   get_database      () const;
   VPFLibrary*    get_library       () const;

   CString        get_tile_path     (degrees_t lat, degrees_t lon);
   short          get_tile_id       (degrees_t lat, degrees_t lon);
   d_geo_rect_t   get_tile_bounds   (degrees_t lat, degrees_t lon);
   CString        get_path          () const;
   CString        get_library_name  () const;
   
   // VVOD had used this one until before it was changed to use CMapSymbol
   // CString get_primitive_info(const CString& primitive_type, int tile_id, int primitive_id);

   // faster method
   static CString        get_primitive_info(VPFFeatureClass* FeatureClass, int FeatureID, bool Verbose=true);
   
   VPFFeatureTable* get_feature_table_for_primitive(const CString& primitive_type,
      int tile_id, int primitive_id);

	int				get_topology_level() const;
   //VPFFeatureClassAttributeTable*   GetFeatureClassAttributeTable(void) const;
   const std::vector<VPFFeatureClass*>* get_feature_class_list(void);

   
//--- Protected Methods -----------------------------------
protected:
   void           create_feature_class_list();
   void           open_feature_index_tables();

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   VPFLibrary *m_library;     // the library to which this coverage belongs

   // only used in 1 function, just make these local vars
   //VPFFeatureClassSchemaTable    *m_fcs;
   //VPFFeatureClassAttributeTable *m_fca;

   CString     m_name;
   CString     m_desc;
   int         m_topology_level;

   //CList<VPFFeatureClass*, VPFFeatureClass*> m_feature_classes;
   std::vector<VPFFeatureClass*> m_feature_classes;

   VPFFeatureIndexTable* m_edg_fit;
   VPFFeatureIndexTable* m_fac_fit;
   VPFFeatureIndexTable* m_end_fit;
   VPFFeatureIndexTable* m_cnd_fit;
   VPFFeatureIndexTable* m_txt_fit;
};


//------------------------------------------------------------------------------
//------------------------------- VPFLibraryNames ----------------------------------
//------------------------------------------------------------------------------
// THis class will be used to create objects that will get the library directory
// names from a vpf database's given path
//------------------------------------------------------------------------------

class VPFCoverageNames
{
private:

   std::vector<CString> m_list;
   int m_position;               // position in list

   int m_list_size;
   
	CString m_path;

public:
   // Constructor
   VPFCoverageNames(const CString path); 

   // Destructor  private,  
   virtual ~VPFCoverageNames();

	// Read the library names in the database directory
	int read_directories();

   // Reset list.
   int reset();

   // Returns the first data source in the list or NULL if the list is
   // empty.  The next data source returned by get_next() is set to the
   // second data source in the list (or NULL if the list has one element).
   CString* get_first(); 

   // Returns the next data source in the list or NULL if the next position
   // is at the end of the list.
   CString* get_next();

   //get number of items in list
   int get_count(void) {return m_list.size();} 

	CString * operator [](int index);
};

inline CString * VPFCoverageNames::operator [](int index)
{
	if ( (index < 0) || (index > get_count()-1) )
	{
		ASSERT( 0 );
		return NULL;
	}

	return &m_list[index];
}


//------------------------------------------------------------------------------
//------------------------------- VPFLibraryNames ----------------------------------
//------------------------------------------------------------------------------
// THis class will be used to create objects that will get the library directory
// names from a vpf database's given path
//------------------------------------------------------------------------------

class VPFLibraryNames
{
private:
   std::vector<CString> m_list;
   int m_position;               // position in list

   int m_list_size;

   CString m_path;

   long  m_product_id;
public:
   // Constructor
   VPFLibraryNames(const CString path); 

   // Destructor  private,  
   virtual ~VPFLibraryNames();

	// Read the library names in the database directory
	int read_directories();

	// Read the library names in the database directory that belongs to product id
	int read_directories(long product_id);

   // Reset list.
   int reset();

	// 
	int add_string(CString s);	

   // Returns the first data source in the list or NULL if the list is
   // empty.  The next data source returned by get_next() is set to the
   // second data source in the list (or NULL if the list has one element).
   CString* get_first(); 

   // Returns the next data source in the list or NULL if the next position
   // is at the end of the list.
   CString* get_next();

   //get number of items in list
   int get_count(void) {return m_list.size();} 

	CString * operator [](int index);
};

inline CString * VPFLibraryNames::operator [](int index)
{
	if ( (index < 0) || (index > get_count()-1) )
	{
		ASSERT( 0 );
		return NULL;
	}

	return &m_list[index];
}


//------------------------------------------------------------------------------
//-------------------------------- VPFLibrary ----------------------------------
//------------------------------------------------------------------------------
// In a VPF database, there can be (but usually aren't) multiple libraries,
// each with information that is specific to the library but not the whole
// database.  By definition, each library must share a common tiling scheme,
// stored in the "TILEREF" coverage.  This class gives access to all the data
// and functions specific to the Library, and gives access to the database
// object as well.
//------------------------------------------------------------------------------
class VPFLibrary
{
//--- Construction ----------------------------------------
public:
   // The constructor assumes that "lat" is set to an appropriate record
   VPFLibrary  (CString lib_path, VPFLibraryAttributeTable* lat);
  
	// This constructor does not depened on the lat
	VPFLibrary  (CString lib_path, CString lib_name);

   virtual ~VPFLibrary  ();

//--- Public Methods --------------------------------------
public:
         CString        name              () const;
   const d_geo_rect_t*  bounds            ();
         CString        get_tile_path     (degrees_t lat, degrees_t lon);
         short          get_tile_id       (degrees_t lat, degrees_t lon);
         d_geo_rect_t   get_tile_bounds   (degrees_t lat, degrees_t lon);
         CString        get_path          () const;
         int            set_path          ( const CString &path );

         bool           is_open() const   {return m_isopen;}

   VPFCoverage* open_coverage(CString name);
	VPFCoverage* get_first_coverage();
	VPFCoverage* get_next_coverage();

	const CStringList* get_coverage_names() const;


   int build_tile_bounds_list( /*bool tileref_coverage_exists*/ );
   const std::vector<VPFTileBounds> *get_tile_boundaries_list();
   
//--- Protected Methods -----------------------------------
protected:

//--- Private Methods -------------------------------------
private:
   VPFLibrary();
   int get_tile_bounds_object(degrees_t lat, degrees_t lon,
      VPFTileBounds& bounds_object);

//--- Member Data -----------------------------------------
private:
   CString                     m_name;
   CString                     m_path;
   d_geo_rect_t                m_bounds;
   //VPFDatabase                *m_database;    // the database to which this library belongs
   VPFBoundingRectangleTable  *m_tileref_fbr; // TILEREF Face Bounding Rectangle Table
   VPFRecordset               *m_tileref_fac; // TILEREF Face Primitive Table
   VPFRecordset               *m_tileref_aft; // TILEREF Tile Reference Area Feature Table

   std::vector<VPFTileBounds> m_tile_bounds;
   std::vector<VPFCoverage*> m_coverages;
   int                       m_coverage_position;	// to let clients iterate through
   //CList<VPFCoverage*, VPFCoverage*> m_coverages;
	//POSITION m_coverage_position;					// to let clients iterate through
															// available coverages
   bool m_isopen;
};


//------------------------------------------------------------------------------
//------------------------------- VPFDatabase ----------------------------------
//------------------------------------------------------------------------------
// Represents all information about a VPF database that the VPFRecordset class
// would need.
//------------------------------------------------------------------------------
class VPFDatabase
{
//--- Construction ----------------------------------------
public:
   VPFDatabase(CString name);
   virtual ~VPFDatabase();

//--- Public Methods --------------------------------------
public:
         bool           open                 (const CString& path);
//         bool           open                 (void);
         void           close                ();
         CString        get_name             () const;
         CString        get_version          () const;
         bool           is_open              () const;

         CString        get_path             (const CString &library_name);
         bool           is_multi_library     (void);

   const d_geo_rect_t*  get_library_bounds   (CString name);
   const CStringList*   get_library_names    () const;
         VPFLibrary*    open_library         (CString name);
			bool		      library_exists       (CString name);
         
         VPFLibrary*    get_first_library    ();
         VPFLibrary*    get_next_library     ();
         VPFLibrary*    get_library_from_name(const TCHAR* library_name);

//--- Private Methods -------------------------------------
private:

//--- Member Data -----------------------------------------
private:
   bool        m_isopen;
   CString     m_name;    // databse name (e.g. "vmaplv0", "vvod")

   // This is probably not needed!
   bool        m_multi_library;

   CString     m_version;

   // Changing library storage to be a map instead of a list to provide for 
   // faster lookups and db controlled opening of the libs "on the fly"
   CMap<CString, LPCTSTR, VPFLibrary*, VPFLibrary*> m_libraries;
   POSITION m_library_position;

   // Parallel to the m_libraries map holding root paths
   CMap<CString, LPCTSTR, CString, CString&> m_paths_to_root;
};

//------------------------------------------------------------------------------
//------------------------------- VPFLibraryNames ------------------------------
//------------------------------------------------------------------------------
// This class will be used to create objects that will get an enumeration of all 
// the vpf databases that are on a given path
//------------------------------------------------------------------------------

class VPFDatabaseNames
{
private:
   std::vector<CString> m_list;
   int m_position;               // position in list

   int m_list_size;
   CString m_path;

public:
   // Constructor
   VPFDatabaseNames(const CString path); 

   // Destructor  private,  
   virtual ~VPFDatabaseNames();

	// add a new db name
	int Add( const CString& name );

	// add a new db name
	int Remove( const CString& name );

	// Read the library names in the database directory
	int read_directories();

   // Reset list.
   int reset();

   // Returns the first data source in the list or NULL if the list is
   // empty.  The next data source returned by get_next() is set to the
   // second data source in the list (or NULL if the list has one element).
   CString* get_first(); 

   // Returns the next data source in the list or NULL if the next position
   // is at the end of the list.
   CString* get_next();

   //get number of items in list
   int get_count(void) {return m_list.size();} 

	CString *operator [](int index);
};


inline CString *VPFDatabaseNames::operator [](int index)
{
	if ( (index < 0) || (index > get_count()-1) )
	{
		ASSERT( 0 );
		return NULL;
	}

	return &m_list[index];
}


#endif // !defined(VPFDB_H__89E52559_8652_11D3_8663_00105A9B4838__INCLUDED_)