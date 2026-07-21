// Copyright (c) 1994-2011 Georgia Tech Research Corporation, Atlanta, GA
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

#include "vpfdb.h"
#include "vpfrcset.h"
#include "variant.h"
#include "tables.h"
#ifdef _WIN32
#include <io.h>
#include "MdsUtilities.h"
#else
#include <unistd.h>   // access(), via _taccess in fv_compat.h
#endif

#include "lst_iter.h"

#include <algorithm>


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

bool lon_in_range(double left_lon, double right_lon, double point_lon)
{
   if (left_lon <= right_lon)
   {
      if (point_lon < left_lon || right_lon < point_lon)
         return false;
   }
   else // if (left_lon > right_lon)   dvl :: redundant check
   {
      if (point_lon < left_lon && point_lon > right_lon)
         return false;
   }

   return true;  
} 


bool in_bounds(double ll_lat, double ll_lon, double ur_lat, double ur_lon,
   double p_lat,  double p_lon)
{
   // north/south reject 
   if (p_lat < ll_lat || p_lat > ur_lat)
      return false;

   // east/west reject
   return lon_in_range(ll_lon, ur_lon, p_lon);
}

bool in_bounds_degrees(d_geo_t ll, d_geo_t ur, d_geo_t p)
{
   return in_bounds(ll.lat, ll.lon, ur.lat, ur.lon, p.lat, p.lon);
}


bool FIL_access(const TCHAR *path, int mode)
{
	ATLASSERT(mode == FIL_EXISTS);

   if (_taccess(path, mode) == 0)
		return true;
	return false;
}

#ifdef _WIN32  // write-side helpers: used only by the copy/export path
               // (vpf_tree.cpp), which is Windows-only in this port.
int FIL_create_directory(const TCHAR *directory)
{
   // returns FALSE for failure
   if (CreateAllDirectories(_bstr_t(directory)) != NO_ERROR)
      return FAILURE;
            
   return SUCCESS;
}

int FIL_set_permissions(const TCHAR *filename, int permissions)
{
   long int attributes;
   
   attributes = GetFileAttributes(filename);
   if (attributes == 0xffffffff)
      return FAILURE;
   
   if ( (permissions & FIL_WRITE_OK) == 0)
   {
      attributes |= FILE_ATTRIBUTE_READONLY;
      if (SetFileAttributes(filename, attributes) == FALSE)
         return FAILURE;         
   }

   if ( (permissions & FIL_READ_OK) == 0)
      return FAILURE;

   return SUCCESS;
}


#ifndef DIR_SLASH
#define DIR_SLASH '\\'
#endif

int FIL_is_dir_empty(const TCHAR *dir_name, bool *empty)
{
   HANDLE handle = INVALID_HANDLE_VALUE;
   //FIL_find_data_t data;
	WIN32_FIND_DATA data;

   TCHAR file_spec[MAX_PATH];

	// assume it's empty
   *empty = true;

   // assumes dir is valid
   _stprintf_s(file_spec, MAX_PATH, _T("%s%c*.*"), dir_name, DIR_SLASH);

   bool match_found = true;
	if (FindFirstFile(CString(dir_name) + _T("\\*.*"), &data) == INVALID_HANDLE_VALUE )
   //if (FIL_find_first(file_spec, &match_found, &handle, &data) != SUCCESS)
   {
		match_found = false;
      return SUCCESS;
   }

   int ret_val;
   bool no_more_files;
   do
   {
      if (_tcscmp(data.cFileName, _T(".")) != 0 &&
          _tcscmp(data.cFileName, _T("..")) != 0)
      {
         *empty = false;
         break;
      }

      if (::FindNextFile(handle, &data) == 0)
	   {
			if (::GetLastError() == ERROR_NO_MORE_FILES)
			{
				no_more_files = true;
				return SUCCESS;
			}
			else
			{
				ret_val = FAILURE;
			}
		}
		no_more_files = false;


   } while (ret_val == SUCCESS && !no_more_files);

   if (ret_val == FAILURE)
   {
      FindClose(handle);
      return FAILURE;
   }

   if (FindClose(handle) != TRUE)
      return FAILURE;

   return SUCCESS;
}
#endif  // _WIN32 (write-side helpers)


/*
//------------------------------------------------------------------------------
//------------------------------- VPFException ---------------------------------
//------------------------------------------------------------------------------
VPFException::VPFException()
{
}

//------------------------------ ~VPFException ---------------------------------
VPFException::~VPFException()
{
}
*/

//------------------------------------------------------------------------------
//------------------------------- VPFDatabase ----------------------------------
//------------------------------------------------------------------------------
VPFDatabase::VPFDatabase(CString name)//(bool multi_library) : m_multi_library(multi_library)
: m_name(name), m_isopen(false)
{
   m_version.Empty();
   m_libraries.RemoveAll();
   m_paths_to_root.RemoveAll();
   m_library_position = NULL;
}

//------------------------------- ~VPFDatabase ---------------------------------
VPFDatabase::~VPFDatabase()
{
   if (!m_libraries.IsEmpty())
   {
      POSITION pos = m_libraries.GetStartPosition();
      CString key;
      VPFLibrary* lib;
      
      do {
         m_libraries.GetNextAssoc(pos, key, lib);
         delete lib;
      } while (pos);
   }

   m_paths_to_root.RemoveAll();
}

//----------------------------------- open -------------------------------------
bool VPFDatabase::open(const CString& path)
{
   // NOTE: The following line MUST happen first.  The calls to
   // VPFRecordset::open() depend on the database path being set.
   CString path_to_root = path + _T("\\");

	// Get the library names on this database path
	VPFLibraryNames lib_names( path_to_root );
	lib_names.read_directories();

   // Loop through each name in the VPFLibraryNames collection
	// and setup the list of VPFLibrary's

	CString *pLibraryName;
	const int size = lib_names.get_count();

   m_libraries.InitHashTable(size);
   for (int i = 0; i < size; i++)
   {
		// get the next library name
		pLibraryName = lib_names[i];
		ASSERT( pLibraryName );

		// if the library was already added, don't bother
		if (library_exists( *pLibraryName ))
			continue;

      // delay opening of lib to first use of the lib.  It will be opened in 
      // VPFDatabase::get_library_from_name()
		// VPFLibrary *pLib = new VPFLibrary(this, *pLibraryName);

		// add the newly discovered library to the map (as NULL for now)
      m_libraries.SetAt(*pLibraryName, NULL);
   }

   m_isopen = (m_libraries.GetCount() > 0);

   return m_isopen;
}


//----------------------------------- statics -------------------------------------
// check if the directory "dir" exists on this "path"

bool find_directory(const CString &path, const CString &dir )
{
   CString complete_path = path + dir ;

   // 4 => read attribute
   return FIL_access(complete_path, FIL_READ_OK);
}

/*
// This is a multiple-library version of open
bool VPFDatabase::open(void)
{
   CString database = m_name;
   CString database_complete_path;
   CString library;

   m_isopen = false;
   bool reference_lib_found = false;

   // LOOP here for each library

   DataSourceList* sources = COV_get_data_source_list();
	if (sources == NULL)
   {
      ERR_report("COV_get_data_source_list() == NULL");
      return false;
   }

   DataSource* current_source = sources->get_first();
   m_libraries.InitHashTable(137);
   m_paths_to_root.InitHashTable(137);

   // Iterating data sources
   while (current_source != NULL)
   {
      if (current_source->get_on_line())
      {
         database_complete_path = current_source->get_data_path_with_backslash();
			
         // Check if the database exist in the map data path
         if (  find_directory(database_complete_path, database) 
				|| find_directory(database_complete_path+"vpf\\", database))
         {
            // VMap is a special case i the fvw directory structure since the name the 
            // user program gives is NOT the directory name the db's are stored under.
            // They are under a general heading of "vmap\vmaplv0, etc,..." in fvw\data\...

				if ( find_directory(database_complete_path+"vpf\\", database) )
					database_complete_path += "vpf\\" + database;
				else
					database_complete_path += database;

            // NOTE: This path need to be stored on a library basis
            CString path_to_root(database_complete_path + "\\");

				// Get the VPF library names under the database root path
				VPFLibraryNames lib_names( database_complete_path);
				lib_names.read_directories();

            // Loop through the records in the VPFLibraryNames collection 
            CString *pLibraryName;

				const int size = lib_names.get_count();

				for (int i = 0; i < size; i++)
            {
					pLibraryName = lib_names[i];
					ASSERT( pLibraryName );

					// don't add if it already exists in database
					if ( library_exists( *pLibraryName ) )
						continue;

               if ( *pLibraryName == "rference" || *pLibraryName == "browse" ) 
					{
                  reference_lib_found = true;      
               }

               m_libraries.SetAt(*pLibraryName, NULL);
               m_paths_to_root.SetAt(*pLibraryName, path_to_root);
            }         

            if (m_libraries.GetCount() > 0) 
               m_isopen = true;

         } // end if the database exist in the map data path

      } // end if online()

      // get the next data_source in source list, libraries belonging to this 
      // database may exist among several paths (sources)
      current_source = sources->get_next();

   } // end while iterating data sources

   return m_isopen;
}
*/

//---------------------------------- close -------------------------------------
void VPFDatabase::close()
{
   m_isopen = false;
}

//--------------------------------- get_name -----------------------------------
CString VPFDatabase::get_name() const
{
   return m_name;
}

//------------------------------- get_version ----------------------------------
CString VPFDatabase::get_version() const
{
   return m_version;
}

//--------------------------------- is_open ------------------------------------
bool VPFDatabase::is_open() const
{
   return m_isopen;
}

//--------------------------------- get_path -----------------------------------
CString VPFDatabase::get_path(const CString &library_name)
{
	CString lib_name (library_name);
   CString path;
   return (m_paths_to_root.Lookup(lib_name, path)) ? path : _T("");
}

bool VPFDatabase::is_multi_library()
{
   return m_multi_library;
}


// Iterator methods to access library list elements
VPFLibrary* VPFDatabase::get_first_library()
{
   VPFLibrary* lib=NULL;
   CString temp;

   // get head position
   m_library_position = m_libraries.GetStartPosition();

   // if there is an element, return it and save the next position
   if (m_library_position != NULL) 
     m_libraries.GetNextAssoc(m_library_position, temp, lib);
   
   return lib;
}

VPFLibrary* VPFDatabase::get_next_library()
{
   VPFLibrary* lib=NULL;
   CString temp;

   // if there is a next element, return it and save the next position
   if (m_library_position != NULL) 
     m_libraries.GetNextAssoc(m_library_position, temp, lib);
   
   return lib;
}


VPFLibrary* VPFDatabase::get_library_from_name( const TCHAR *library_name )
{
	CString lib_name(library_name);

   // Iterate to each library in database
   VPFLibrary *library=NULL;
   CString path;
   
   if (m_libraries.Lookup(lib_name, library)) // entry exists
   {
      if (!library) // null check
      {
         // attempt to open the library -- this is the first request for it.
         m_paths_to_root.Lookup(lib_name, path);
         library = new VPFLibrary(path, lib_name);

         // If the library was opened, enter it into the storage list for
         // later use.
         if (library && library->is_open())
            m_libraries.SetAt(lib_name, library);
      }
   }

   // If a NULL is returned, it means either the library could not be opened
   // in this database, or the library does not belong to this database.  
   //
   // That leaves the question, can a library be repeated across databases?
   // I think it is Yes (i.e. DNC Browse library).  So we need to be asking for
   // the database+library assigned to the tile, not just guessing which databse 
   // it will be in
   return library;
}


//------------------------------- get_library ----------------------------------
const CStringList* VPFDatabase::get_library_names() const
{
   CStringList *return_value = new CStringList;
   POSITION pos = m_libraries.GetStartPosition();

   VPFLibrary* library;
   CString name;

   while (pos)
   {
      m_libraries.GetNextAssoc(pos, name, library);
      return_value->AddTail(name);
   }

   return return_value;
}

//---------------------------- get_library_bounds ------------------------------
const d_geo_rect_t* VPFDatabase::get_library_bounds(CString name)
{
   name.MakeLower();

   VPFLibrary *lib = get_library_from_name(name);

   if (lib)
   {
      return lib->bounds();
   }

   return NULL;
}
//------------------------------- open_library ---------------------------------
VPFLibrary* VPFDatabase::open_library(CString name)
{
   CString LibPath;

   name.MakeLower();
   
   int pos = name.ReverseFind('\\');
   if(pos != -1)
   {
      // Name and path were found, check on the short name
      LibPath = name.Left(pos+1);
      name = name.Right(name.GetLength()-pos-1);
   }

   VPFLibrary* retval = get_library_from_name(name);

   if (!retval && pos!=-1)
   {
      // The user passed in a name and path and the library named was not 
      // located in the held lists.  Add it for lookup

      // The library is not already opened, add it to the list of 
      // possible libraries
      m_libraries.SetAt(name, NULL);
      m_paths_to_root.SetAt(name, LibPath);

      retval = get_library_from_name(name);
      if (!retval)
         m_libraries.RemoveKey(name);
   }
   //else The library was previously opened, just return the pointer to it
   // or was not found and is NULL

   // This may still be NULL, user should check it.
   return retval;

}

bool VPFDatabase::library_exists(CString name)
{
   VPFLibrary* temp;
   name.MakeLower();

   return (m_libraries.Lookup(name, temp) == TRUE);
}


//------------------------------------------------------------------------------
//-------------------------------- VPFLibrary ----------------------------------
//------------------------------------------------------------------------------
VPFLibrary::VPFLibrary(CString lib_path, VPFLibraryAttributeTable* lat)
: m_path(lib_path), m_tileref_fbr(NULL), m_tileref_fac(NULL), m_tileref_aft(NULL)
   , m_isopen(false)
{
   ASSERT(lat);
   if (!lat) 
      return;

   // Build the list of coverages in this library
   VPFCoverageAttributeTable coverage_attribute_table(this);

   if (coverage_attribute_table.open() != SUCCESS)
   {
      CString Error;
      Error.Format(_T("Unable to open database at (%s)"), (LPCSTR)m_path);
      WriteToLogFile(_bstr_t(Error));

      return;
   }

   m_name   = lat->library_name();
   m_bounds = lat->bounds();

   //const bool tileref_coverage_exists = (SUCCESS == FIL_access(m_path + m_name +
   //         "\\tileref", FIL_EXISTS));

   // Create the tile bounds list 
   build_tile_bounds_list();

   // Loop through the records in the Coverage Attribute
   // Table and setup a list of the coverages

   const int size = coverage_attribute_table.get_record_count();
   for (int i = 0; i < size; i++)
   {
      coverage_attribute_table.set_absolute_position(i);

      VPFCoverage *new_cov = new VPFCoverage(this, &coverage_attribute_table);

      if (new_cov)
         m_coverages.push_back(new_cov);
   }

   m_coverage_position = NULL;

   m_isopen = m_coverages.size() > 0;
}


VPFLibrary::VPFLibrary(CString lib_path, CString lib_name)
: m_path(lib_path), m_tileref_fbr(NULL), m_tileref_fac(NULL), m_tileref_aft(NULL)
   , m_isopen(false)
{
   ASSERT(!lib_name.IsEmpty());
   if (lib_name.IsEmpty()) 
      return;

   // Build the list of coverages in this library

   m_name = lib_name;

   VPFCoverageAttributeTable coverage_attribute_table(this);

   if (coverage_attribute_table.open() != SUCCESS)
   {
      CString Error;
      Error.Format(_T("Unable to open database at (%s)"),(LPCSTR)m_path);
      WriteToLogFile(_bstr_t(Error));

      return;
   }

   //m_bounds = lat->bounds();

   //const bool tileref_coverage_exists = (SUCCESS == FIL_access(m_path + m_name 
   //         + "\\tileref", FIL_EXISTS));

   // Create the tile bounds list 
   build_tile_bounds_list();

   // Loop through the records in the Coverage Attribute
   // Table and setup a list of the coverages

   const int size = coverage_attribute_table.get_record_count();
   for (int i = 0; i < size; i++)
   {
      coverage_attribute_table.set_absolute_position(i);

      VPFCoverage *new_cov = new VPFCoverage(this, &coverage_attribute_table);

      if (new_cov)
         m_coverages.push_back(new_cov);
   }

   m_coverage_position = NULL;

   m_isopen = m_coverages.size() > 0;
}

//---------------------------------- bounds ------------------------------------
int VPFLibrary::build_tile_bounds_list(/* bool tileref_coverage_exists */) 
{
   const bool tileref_coverage_exists = (true == FIL_access(
      m_path + m_name + _T("\\tileref"), FIL_EXISTS));

   if (tileref_coverage_exists)
   {
      VPFTileBounds tile_bounds;
      VPFVariant *value;

      // Create the tables
      m_tileref_fbr = new VPFBoundingRectangleTable(this);
      m_tileref_fac = new VPFRecordset(this);
      m_tileref_aft = new VPFRecordset(this);

      // Open the tables
      m_tileref_fbr->open( _T("\\tileref\\fbr"));
      m_tileref_fac->open( _T("\\tileref\\fac"));
      m_tileref_aft->open( _T("\\tileref\\tileref.aft"));

		int rec_pos;
		const int size_aft = m_tileref_aft->get_record_count();
		for (int i = 0; i < size_aft; i++)
		{
			m_tileref_aft->set_absolute_position(i);

			// set the tile name
			value = m_tileref_aft->get_field_value(_T("tile_name"));
			_tcscpy_s(tile_bounds.tile_name, 64, value->m_text); TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);

			// set the tile id
			value = m_tileref_aft->get_field_value(_T("fac_id"));
		
			if ( value->m_int_long > 0)
			{
				rec_pos = value->m_int_long - 1;
            ASSERT((int)((short)rec_pos) == rec_pos); // verify range
				tile_bounds.tile_id = (short)rec_pos;// convert to a 0-based index

				m_tileref_fbr->set_absolute_position(rec_pos);
				tile_bounds.m_geo_rect.ll = m_tileref_fbr->bounds().ll;
				tile_bounds.m_geo_rect.ur = m_tileref_fbr->bounds().ur;

				//m_tile_bounds.AddTail(tile_bounds);
            m_tile_bounds.push_back(tile_bounds);
			}
		}
   }
   else
   {
      // If there is no TILEREF coverage, then the library is untiled.
      // For the puposes of clean code, we will consider an untiled library
      // to be a tiled library with only one tile.  Therefore, we need to
      // create a tile_bounds object whose path is empty and whose bounds
      // are the bounds of the library.  This allows the rest of the program
      // to treat an untiled coverage no differently than a tiled coverage.
      VPFTileBounds tile_bounds;

      tile_bounds.m_geo_rect.ll = m_bounds.ll;
      tile_bounds.m_geo_rect.ur = m_bounds.ur;

      tile_bounds.tile_name[0] = '\0';

      //m_tile_bounds.AddTail(tile_bounds);
      m_tile_bounds.push_back(tile_bounds);
   }

   return SUCCESS;
}

//------------------------------- ~VPFLibrary ----------------------------------
VPFLibrary::~VPFLibrary()
{
   if (m_tileref_fbr)
      delete m_tileref_fbr;

   if (m_tileref_fac)
      delete m_tileref_fac;

   if (m_tileref_aft)
      delete m_tileref_aft;

  VPFCoverage* pe = NULL;
  std::vector<VPFCoverage*>::iterator it;
  
  for (it = m_coverages.begin(); it != m_coverages.end(); it++)
  {
    pe = static_cast<VPFCoverage*>(*it);
    
    delete pe;
  }
  
  m_coverages.clear();
}

//----------------------------------- name -------------------------------------
CString VPFLibrary::name() const
{
   return m_name;
}

//---------------------------------- bounds ------------------------------------
const d_geo_rect_t* VPFLibrary::bounds()
{
   return &m_bounds;
}

//------------------------------ get_tile_path ---------------------------------
CString VPFLibrary::get_tile_path(degrees_t lat, degrees_t lon)
{
   VPFTileBounds tile_bounds;

   if (SUCCESS == get_tile_bounds_object(lat, lon, tile_bounds))
   {
      const CString return_value = tile_bounds.tile_name;
      return return_value;
   }
   else
   {
      const CString return_value = _T("");
      return return_value;
   }
}

//------------------------------- get_tile_id ----------------------------------
short VPFLibrary::get_tile_id(degrees_t lat, degrees_t lon)
{
   VPFTileBounds tile_bounds;

   if (SUCCESS == get_tile_bounds_object(lat, lon, tile_bounds))
      return tile_bounds.tile_id;
   else
      return 0;
}

//----------------------------- get_tile_bounds --------------------------------
d_geo_rect_t VPFLibrary::get_tile_bounds(degrees_t lat, degrees_t lon)
{
   VPFTileBounds tile_bounds;
   tile_bounds.m_geo_rect.ll.lat = 0;
   tile_bounds.m_geo_rect.ll.lon = 0;
   tile_bounds.m_geo_rect.ur.lat = 0;
   tile_bounds.m_geo_rect.ur.lon = 0;

   get_tile_bounds_object(lat, lon, tile_bounds);

   return tile_bounds.m_geo_rect;
}

//-------------------------- get_tile_bounds_object ----------------------------
int VPFLibrary::get_tile_bounds_object(degrees_t lat, degrees_t lon,
   VPFTileBounds& bounds_object)
{
   d_geo_t  point;

   point.lat = lat;
   point.lon = lon;

   std::vector<VPFTileBounds>::iterator it;

	for (it = m_tile_bounds.begin(); it != m_tile_bounds.end(); ++it)
   {
      bounds_object = *it;
      if ( in_bounds_degrees(bounds_object.m_geo_rect.ll, bounds_object.m_geo_rect.ur, point) )
         // NOT an exhaustive search.  As soon as we find a matching one, exit.
         return SUCCESS;
   }

   return FAILURE;
}

//------------------------- get_tile_boundaries_list ----------------------------
const std::vector<VPFTileBounds> *VPFLibrary::get_tile_boundaries_list()
{
   // Should return the iterator from here instead of the list
   return &m_tile_bounds;
}

//--------------------------------- get_path -----------------------------------
CString VPFLibrary::get_path() const
{
   return m_path + m_name + '\\';
}

int VPFLibrary::set_path(const CString &path) 
{
   ASSERT(path != _T(""));

   m_path = path;

   return SUCCESS;
}

// Iterator methods to access library list elements
VPFCoverage* VPFLibrary::get_first_coverage()
{
   // get head position
   if ( m_coverages.size() > 0 )
      return m_coverages[m_coverage_position++];

   return NULL;
}

VPFCoverage* VPFLibrary::get_next_coverage()
{
   int size = m_coverages.size();
   // if there is a next element, return it and save the next position
   if ( size > 0 && m_coverage_position < size)
      return m_coverages[m_coverage_position++];
   
   return NULL;
}


//------------------------------- get_coverage_names ----------------------------------
const CStringList* VPFLibrary::get_coverage_names() const
{
   CStringList *return_value = new CStringList;

   VPFCoverage* coverage = NULL;

   for (size_t i = 0; i < m_coverages.size(); i++)
   {
      coverage = m_coverages[i];

      return_value->AddTail(coverage->name());
   }

   return return_value;
}

//------------------------------ open_coverage ---------------------------------
VPFCoverage* VPFLibrary::open_coverage(CString name)
{
   VPFCoverage *current;

   name.MakeLower();

   for (size_t i = 0; i < m_coverages.size(); i++)
   {
      current = m_coverages[i];

      if (current->name() == name)
         return current;
   }

   return NULL;
}


//------------------------------------------------------------------------------
//------------------------------- VPFDatabaseNames ----------------------------------
//------------------------------------------------------------------------------
    
VPFDatabaseNames::VPFDatabaseNames(const CString path ) : m_path(path), m_position(0)
{
	ASSERT( !path.IsEmpty() );
}

// remove any existing library name
VPFDatabaseNames::~VPFDatabaseNames()
{
	reset();
}

int VPFDatabaseNames::reset()
{
	m_list.clear();
   
   m_list_size = m_list.size();

	m_position = 0;

   return SUCCESS;
}

// add a new db name
int VPFDatabaseNames::Add( const CString& name )
{
	m_list.push_back(name);
	
   m_list_size = m_list.size();   

   return SUCCESS;
}

// add a new db name
int VPFDatabaseNames::Remove( const CString& name )
{
    std::vector<CString>::iterator it = std::find( m_list.begin(), m_list.end(), name );

    if( m_list.end() == it )
        return FAILURE;
    m_list.erase( it );

    m_list_size = m_list.size();

    return SUCCESS;
}



// Read the directories in this VPF database and store them in a container
int VPFDatabaseNames::read_directories()
{
	// Walk all non-hidden, non-folder files in the folder 
	WIN32_FIND_DATA find_data;
	BOOL b = TRUE;
	HANDLE h;

	CString Name;

	for (h = ::FindFirstFile(m_path + _T("\\*.*"), &find_data);
			b && h != INVALID_HANDLE_VALUE;
			b = ::FindNextFile(h, &find_data) )
	{
		if (	!(find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)    )
		{
			CString s(find_data.cFileName);
			s.MakeLower();

			if (s == _T(".") || s == _T(".."))
				continue;   // skip current and parent folder's

			if ( find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				// if the name is a directory, check first if the directory has
				// a coverage attribute table (cat) file.
				
				// Check the lht to and get the library name and verfiy that the name
				// exists in the m_path memeber

				if ( FIL_access(m_path + _T("\\") + s + _T("\\dht"), FIL_EXISTS) == true )
				{
					if ( s.GetLength() > 0 )
						m_list.push_back(s);
				}
			}
		}
	}
	::FindClose(h);

   m_list_size = m_list.size();

	return SUCCESS;
}

CString* VPFDatabaseNames::get_first()  
{
   // get head position
   if ( m_list_size > 0  && m_position < m_list_size )
      return &m_list[m_position++]; 
  
   return NULL;
}

CString* VPFDatabaseNames::get_next()
{
   // if there is a next element, return it and save the next position
   if ( m_list_size > 0  && m_position < m_list_size )
      return &m_list[m_position++]; 
   
   return NULL;
}   

//------------------------------------------------------------------------------
//------------------------------- VPFLibraryNames ----------------------------------
//------------------------------------------------------------------------------
    
VPFLibraryNames::VPFLibraryNames(const CString path ) : m_path(path), m_position(0)
{
   m_list_size = 0;;

	ASSERT( !path.IsEmpty() );
}

// remove any existing library name
VPFLibraryNames::~VPFLibraryNames()
{
	reset();
}

// Read the directories in this VPF database and store them in a container
int VPFLibraryNames::read_directories()
{
	// Walk all non-hidden, non-folder files in the folder 
	WIN32_FIND_DATA find_data;
	BOOL b = TRUE;
	HANDLE h;

	for (h = ::FindFirstFile(m_path + _T("\\*.*"), &find_data);
			b && h != INVALID_HANDLE_VALUE;
			b = ::FindNextFile(h, &find_data) )
	{
		if (	!(find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)    )
		{
			CString s(find_data.cFileName);
			s.MakeLower();

			if (s == _T(".") || s == _T(".."))
				continue;   // skip current and parent folder's

			if ( find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				// if the name is a directory, check first if the directory has
				// a coverage attribute table (cat) file.
				
				// Check the lht to and get the library name and verfiy that the name
				// exists in the m_path member

				if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
				{
					if ( s.GetLength() > 0 )
						m_list.push_back(s);
				}
			}
		}
	}
	::FindClose(h);

   m_list_size = m_list.size();

	return SUCCESS;
}


// Read the directories in this VPF database and store them in a container
int VPFLibraryNames::read_directories(long product_id)
{
	// Walk all non-hidden, non-folder files in the folder 
	WIN32_FIND_DATA find_data;
	BOOL b = TRUE;
	HANDLE h;

	for (h = ::FindFirstFile(m_path + _T("\\*.*"), &find_data);
			b && h != INVALID_HANDLE_VALUE;
			b = ::FindNextFile(h, &find_data) )
	{
		if (	!(find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)    )
		{
			CString s(find_data.cFileName);
			s.MakeLower();

			if (s == _T(".") || s == _T(".."))
				continue;   // skip current and parent folder's

			if ( find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				switch ( product_id )
				{
				case 1: // Vmap0 Reference
					if ( s == CString( _T("rference") ) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\lht"), FIL_EXISTS) == true )
                  {
						   add_string(s);

						   return SUCCESS;
                  }
					}
					break;

				case 2: // Vmap 0
					if ( s == CString( _T("rference") ) )
						continue;

					// if the name is a directory, check first if the directory has
					// a coverage attribute table (cat) file.
					
					// Check the lht to and get the library name and verfiy that the name
					// exists in the m_path member

					if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
					{
						add_string(s);
					}

					break;

				case 3: // Vmap 1
               if ( s == CString( _T("rference") ) )
						continue;

					if ( s.Left(3) == CString(_T("lib")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;

            case 4: // Dnc Browse
					if ( s == CString( _T("browse") ) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\lht"), FIL_EXISTS) == true )
                  {
						   add_string(s);

						   return SUCCESS;
                  }
					}
					break;

				case 5: // Dnc General
					if ( s == CString( _T("browse") ) )
						continue;
					
					if ( s.Left(1) == CString(_T("g")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;

				case 6: // Dnc Harbor
					if ( s == CString( _T("browse") ) )
						continue;

					if ( s.Left(1) == CString(_T("h")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;

				case 7: // Dnc Approach
					if ( s == CString( _T("browse") ) )
						continue;

					if ( s.Left(1) == CString(_T("a")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;

				case 8: // Dnc Coastal
					if ( s == CString( _T("browse") ) )
						continue;

					if ( s.Left(1) == CString(_T("c")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;

				case 9: // WVS 250K
					if ( s == CString(_T("wvs250k")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;

				case 10: // WVS 1M
					if ( s == CString(_T("wvs001m")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;


				case 11: // WVS 3M
					if ( s == CString(_T("wvs003m")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;


				case 12: // WVS 12M
					if ( s == CString(_T("wvs012m")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;


				case 13: // WVS 40M
					if ( s == CString(_T("wvs040m")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;


				case 14: // WVS 120M
					if ( s == CString(_T("wvs120m")) )
					{
						if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
						{
							add_string(s);
						}
					}

					break;


				case 16: // vvod
					if ( FIL_access(m_path + _T("\\") + s + _T("\\cat"), FIL_EXISTS) == true )
					{
						add_string(s);
					}

					break;
				}
			}
		}
	}

   m_list_size = m_list.size();

	::FindClose(h);

	return SUCCESS;
}


int VPFLibraryNames::add_string(CString s)
{
	if ( s.GetLength() == 0 )
      return FAILURE;

	m_list.push_back(s);

   m_list_size = m_list.size();

	return SUCCESS;
}


int VPFLibraryNames::reset()
{
   m_list.clear();

   m_position = 0;

   m_list_size = m_list.size();

   return SUCCESS;
}

CString* VPFLibraryNames::get_first()  
{
   // get head position
   if ( m_list_size > 0  && m_position < m_list_size )
      return &m_list[m_position++]; 
  
   return NULL;
}

CString* VPFLibraryNames::get_next()
{
   // get head position
   if ( m_list_size > 0  && m_position < m_list_size )
      return &m_list[m_position++]; 
  
   return NULL;
}   



//------------------------------------------------------------------------------
//------------------------------- VPFCoverageNames ----------------------------------
//------------------------------------------------------------------------------
    
VPFCoverageNames::VPFCoverageNames(const CString path ) : m_path(path), m_position(0)
{
   m_list_size = 0;;

	ASSERT( !path.IsEmpty() );
}

// remove any existing library name
VPFCoverageNames::~VPFCoverageNames()
{
	reset();	
}

// Read the directories in this VPF database and store them in a container
int VPFCoverageNames::read_directories()
{
	// open the coverage attributes table
	VPFCoverageAttributeTable cat(m_path+"\\");
	
	if (cat.open() != SUCCESS)
	{
	   // there is no need to log an error in this case
	   return FAILURE;
	}

	CString Name;
	const int cat_size = cat.get_record_count();

	for (int i = 0; i < cat_size; i++ )
	{
		cat.set_absolute_position(i);
		Name = cat.coverage_name();

		ASSERT( Name.GetLength() > 0 );
		if ( Name.GetLength() == 0)
		{
//			ERR_report("read_directories() failed");
			// make sure to remove any added element
			reset();
			return FAILURE;
		}

	   m_list.push_back(Name);
	}

   m_list_size = m_list.size();
		
	return SUCCESS;
}

int VPFCoverageNames::reset()
{
	m_list.clear();

   m_list_size = m_list.size();

   m_position = 0;

   return SUCCESS;
}

CString* VPFCoverageNames::get_first()  
{
   // get head position
   if ( m_list_size > 0  && m_position < m_list_size )
      return &m_list[m_position++]; 
  
   return NULL;
}

CString* VPFCoverageNames::get_next()
{
   // get head position
   if ( m_list_size > 0  && m_position < m_list_size )
      return &m_list[m_position++]; 
  
   return NULL;
}   

//------------------------------------------------------------------------------
//------------------------------- VPFCoverage ----------------------------------
//------------------------------------------------------------------------------
VPFCoverage::VPFCoverage(VPFLibrary* lib, VPFCoverageAttributeTable* cat)
{
   ASSERT(lib);
   ASSERT(cat);

   m_library         = lib;
   m_name            = cat->coverage_name();
   m_desc            = cat->description();
   m_topology_level  = cat->topology_level();

   m_edg_fit = NULL;
   m_fac_fit = NULL;
   m_end_fit = NULL;
   m_cnd_fit = NULL;
   m_txt_fit = NULL;

   create_feature_class_list();

   // DVL
   // These inadvertently open the primitive files, but doesn't close them.  We
   // are not using this table yet so I wil not fix it now
   //
   // DVL 2002/06/04
   // In fact, we do not use this method of access into the feature tables 
   // anymore, so this does not matter.  We will be using the "fit" classes for
   // other tables, so don't remove them though.
   //
   //open_feature_index_tables();
}

//------------------------------- ~VPFCoverage ---------------------------------
VPFCoverage::~VPFCoverage()
{
   // Made into local vars -- only used for a short time
   //delete m_fca;
   //delete m_fcs;

   std::vector<VPFFeatureClass*>::iterator it;

   for (it = m_feature_classes.begin(); it != m_feature_classes.end(); it++)
   {
      delete *it;
   }

   m_feature_classes.clear();


   delete m_edg_fit;
   delete m_fac_fit;
   delete m_end_fit;
   delete m_cnd_fit;
   delete m_txt_fit;
}

//------------------------ create_feature_class_list ---------------------------
void VPFCoverage::create_feature_class_list()
{
   CStringList list;

   //
   // Coverages are not required to have a Feature Class atribute table which lists
   // the feature classes contained in the class.  The order of the feature classes
   // here is the proper order of them in the various tables as the feature class
   // is referred to by an ordinal number
   //
   // The thematic coverages (bnd, hydro, obs,... ) do have a fca, tileref, etc. do not
   //
   // 

   VPFFeatureClassSchemaTable    fcs(this);
   VPFFeatureClassAttributeTable fca(this);
   fca.open();
   fcs.open();

   if (fca.is_open() || m_name==_T("libref") )
   {
      fca.get_feature_class_list(list);

      POSITION next = list.GetHeadPosition();
      while (next)
      {
         CString feature_class_name = list.GetNext(next);

         //if (feature_class_name.Left(2) != _T("dq"))
         {
            // DVL :: 03/21/2001
            // We are not yet supporting the data quality coverages.  Do
            // not load the tables yet
            VPFFeatureClass* new_feature_class=NULL;
            new_feature_class = 
                     new VPFFeatureClass(this, &fcs, feature_class_name);

            if (new_feature_class->is_open())
            {
               m_feature_classes.push_back(new_feature_class);
            }
            else
            {
               // It is possible for a feature class to be named in the fca
               // or a table named in the fcs, that DOES NOT EXIST!  I don't
               // think this is correct in the standard, so flag an error

               CString msg;
               msg.Format(_T("Files associated with feature table ")
                        _T("(%s%s\\%s) missing"),
                        (LPCSTR)m_library->get_path(), (LPCSTR)m_name,
                        (LPCSTR)feature_class_name);
//               ERR_report(msg);

               delete new_feature_class;
            }
         }
      }
   }
}



const std::vector<VPFFeatureClass*>* VPFCoverage::get_feature_class_list()
{
   return &m_feature_classes;
}


//------------------------ open_feature_index_tables ---------------------------
void VPFCoverage::open_feature_index_tables()
{
   // Attempt to open each of the possible primitive tables for the coverage

   m_edg_fit = new VPFFeatureIndexTable(this);   
   m_edg_fit->open(_T("edg"));

   if (! m_edg_fit->is_open())
   {
      delete m_edg_fit;
      m_edg_fit = NULL;
   }


   m_fac_fit = new VPFFeatureIndexTable(this);  
   m_fac_fit->open(_T("fac"));

   if (! m_fac_fit->is_open())
   {
      delete m_fac_fit;
      m_fac_fit = NULL;
   }


   m_end_fit = new VPFFeatureIndexTable(this);  
   m_end_fit->open(_T("end"));

   if (! m_end_fit->is_open())
   {
      delete m_end_fit;
      m_end_fit = NULL;
   }
   

   m_cnd_fit = new VPFFeatureIndexTable(this);  
   m_cnd_fit->open(_T("cnd"));

   if (! m_cnd_fit->is_open())
   {
      delete m_cnd_fit;
      m_cnd_fit = NULL;
   }

   m_txt_fit = new VPFFeatureIndexTable(this);  
   m_txt_fit->open(_T("txt"));
   if (! m_txt_fit->is_open())
   {
      delete m_txt_fit;
      m_txt_fit = NULL;
   }
}

//----------------------------------- name -------------------------------------
CString VPFCoverage::name() const
{
   return m_name;
}

//------------------------------- get_database ---------------------------------
//VPFDatabase* VPFCoverage::get_database() const
//{
// ASSERT(m_library);
//
// return m_library->get_database();
//}

//------------------------------- get_database ---------------------------------
VPFLibrary* VPFCoverage::get_library() const
{
   ASSERT(m_library);

   return m_library;
}

//------------------------------ get_tile_path ---------------------------------
CString VPFCoverage::get_tile_path(degrees_t lat, degrees_t lon)
{
   ASSERT(m_library);

   return m_library->get_tile_path(lat, lon);
}

//------------------------------- get_tile_id ----------------------------------
short VPFCoverage::get_tile_id(degrees_t lat, degrees_t lon)
{
   return m_library->get_tile_id(lat, lon);
}

//----------------------------- get_tile_bounds --------------------------------
d_geo_rect_t VPFCoverage::get_tile_bounds(degrees_t lat, degrees_t lon)
{
   return m_library->get_tile_bounds(lat, lon);
}

//--------------------------------- get_path -----------------------------------
CString VPFCoverage::get_path() const
{
   ASSERT(m_library);

   return m_library->get_path() + m_name + _T("\\");
}

//----------------------------- get_library_name -------------------------------
CString VPFCoverage::get_library_name() const
{
   return m_library->name();
}

//--------------------- get_feature_table_for_primitive ------------------------
VPFFeatureTable* VPFCoverage::get_feature_table_for_primitive(
   const CString& primitive_type, int tile_id, int primitive_id)
{
   VPFFeatureIndexTable* fit   = NULL;

   // This function is written to require that any vpf product implemented MUST
   // have a feature index table (.fit) for all primitives -- which is not required
   // by the vpf spec; although, it is strongly encouraged.

   // figure out what type of primitive it is
   // go to the appropriate feature index table
   if (primitive_type == _T("VPFIcon"))
      fit = m_end_fit;
   else if (primitive_type == _T("VPFEdge"))
      fit = m_edg_fit;
   else if (primitive_type == _T("VPFFace"))
      fit = m_fac_fit;
   else 
      return NULL;

   // DVL :: 10/25/2000
   //
   // Though this case will not exist once we are loading features via their entries
   // in the feature tables (All features possible to come here have an entry in a 
   // proper feature table -- so the feature table WILL exist),... 
   //
   // We have to check now, because we are loading and displaying ALL primitives that
   // are found.  This means that not all primitives are actually features.  A good
   // example are the edges that make up a ring/face.  They are not features themselves.
   if (!fit)
      return NULL;

   // get the feature class and the id from the feature table
   CString filter;
   filter.Format(_T("prim_id = %d AND tile_id = %d"), primitive_id, tile_id);

   if(fit->find(VPFRecordset::VPF_FIRST, filter))
   {
      // DVL : this function is looking for a feature table.  Return false
      // if it is not found.

      const int id_in_feature_table = fit->get_feature_id() - 1;// convert to 0-based index
      const int feature_class_id    = fit->get_feature_class_id();
      //.ecr.todo. is feature_class_id 0-based or 1-based??

      // get a handle to the feature table from the feature class
//      const POSITION pos = m_feature_classes.FindIndex(feature_class_id);

      const VPFFeatureClass* feature_class = m_feature_classes[feature_class_id];

      
      ASSERT(feature_class != NULL);

      VPFFeatureTable* feature_table = feature_class->m_feature_table;
      ASSERT(feature_table != NULL);

      // seek to the id
      feature_table->set_absolute_position(id_in_feature_table);

      return feature_table;
   }
   else
      return NULL;
}

int	VPFCoverage::get_topology_level() const
{
	//Simple accessor function

	return (m_topology_level);

}


// Method in support of overlay using CMapSymbol (known feature class and id)
CString VPFCoverage::get_primitive_info(VPFFeatureClass* FeatureClass, int FeatureID, bool Verbose)
{
   VPFFeatureTable* feature_table = FeatureClass->m_feature_table;
	
   if (!feature_table)
   {
      CString temp;
      temp.Format(_T("Feature table for feature class (%s) not found!"), (LPCSTR)FeatureClass->GetName());
      return temp;
   }
	
   feature_table->set_absolute_position(FeatureID);

   // read in the attributes and re-format into a CString
   CString info_str;
   CString txt;

   int num_fields = feature_table->num_fields();
   for(int i=0;i<num_fields;i++)
	{
		VPFVariant *info = feature_table->get_attribute_at(i);
		VPFFieldInfo &field_info = feature_table->get_attribute_info_at(i);

		if (Verbose)
      {
         #ifndef _DEBUG
            // For the release version, there are some fields that the user does not need.
            // This used to be done by removing the first 1, and last 2 fields, which is where
            // these fields usually are, but the database is inconsistent

            if (field_info.m_name.CompareNoCase(_T("tile_id"))==0 ||
               field_info.m_name.CompareNoCase(_T("id"))==0 ||
               field_info.m_name.CompareNoCase(_T("fac_id"))==0 ||
               field_info.m_name.CompareNoCase(_T("edg_id"))==0 ||
               field_info.m_name.CompareNoCase(_T("end_id"))==0 ||
               field_info.m_name.CompareNoCase(_T("cnd_id"))==0 ||
               field_info.m_name.CompareNoCase(_T("txt_id"))==0
               )
               continue;
         #endif

         if (field_info.m_value_description_table.IsEmpty())
            info_str += (field_info.m_desc + _T(" (") + field_info.m_name + _T(") : "));
         else
            info_str += (field_info.m_desc + _T(" index (") + field_info.m_name + _T(") : "));
      }
		else
      {
         // No formatting, sending to geosym
			info_str += field_info.m_name + _T("=");
      }

		if (info->is_text())
      {
         if (Verbose && 
               ( _tcsicmp(info->m_text, _T("UNK")) == 0
               || _tcsicmp(info->m_text, _T("NULL")) == 0 ) )
            info_str += _T("Unknown\n");
         else
            info_str += info->m_text + _T("\n");
      }
		else if (info->is_float_short())
		{
			txt.Format(_T("%f\n"), info->m_float_short);
         if (Verbose && txt.Find(_T("QNAN")) != -1)
            info_str += _T("Unknown\n");
         else
			   info_str += txt;
		}
		else if (info->is_float_long())
		{
			txt.Format(_T("%f\n"), info->m_float_long);
         if (Verbose && txt.Find(_T("QNAN")) != -1)
            info_str += _T("Unknown\n");
         else
			   info_str += txt;
		}
		else if (info->is_int_short())
		{
         if (Verbose && (info->m_int_short == -32768 || info->m_int_short == 32767))
         {
            info_str += _T("Unknown\n");
         }
         else
         {
            txt.Format(_T("%d\n"), info->m_int_short);
   			info_str += txt;
         }
		}
		else if (info->is_int_long())
		{
         if (Verbose && (info->m_int_long == -32768 || 
            info->m_int_long == 32767 ||
            info->m_int_long == INT_MIN ||
            info->m_int_long == INT_MAX))
         {
            info_str += _T("Unknown\n");
         }
         else
         {
            txt.Format(_T("%d\n"), info->m_int_long);
   			info_str += txt;
         }
		}
		else if (info->is_date_time())
		{
         // TODO: 
#ifdef _WIN32
			info_str += CString(info->m_date_time.Format(info_str.GetBuffer(info_str.GetLength()), VAR_DATEVALUEONLY)) + _T("\n");
#else
			// POSIX: no CComDATE::Format; this is a diagnostic dump, so an
			// ISO date carries the same information.
			{
				CString date_str;
				date_str.Format(_T("%04d-%02d-%02d\n"), info->m_date_time.GetYear(),
					info->m_date_time.GetMonth(), info->m_date_time.GetDay());
				info_str += date_str;
			}
#endif
		}
		else
			info_str += _T("<placeholder>");
	}

   return info_str;
}

//------------------------------------------------------------------------------
//----------------------------- VPFFeatureClass --------------------------------
//------------------------------------------------------------------------------
VPFFeatureClass::VPFFeatureClass(VPFCoverage* cov,
            VPFFeatureClassSchemaTable* fcs, const CString& name)
   : m_feature_table(NULL), m_name(name), m_coverage(cov), m_join_table(NULL),
      m_isopen(false), LinkField("")
{
   // loop through the fcs and find the relations pertaining to the
   // feature class with name same as m_name
   VPFVariant *value;
   m_name.MakeLower(); // Just in case,... 

   // This may not be the proper way to get the filename and open the
   // actual feature table for this feature class.  However, the name
   // is here and is convenient.  I actually think that the filename
   // should be constructed from the specs and the information in the
   // fca table.

   for (fcs->move_first(); !fcs->is_eof(); fcs->move_next())
   {
      CString FeatueTableName, JoinTableName;

      value = fcs->get_field_value(_T("feature_class"));

      if (value->m_text == m_name)
      {
         CString table1, table2, table1_key, table2_key;

         // get table1
         value = fcs->get_field_value(_T("table1"));
         table1 = value->m_text;
         table1.MakeLower();

         // Get the stored name for the feature table for this feature class
         // as listed in the fcs.  We will open the feature table as soon
         // as we know the name -- though this does not really help the logic
         // in this function or loop (IT IS NOT NECESSARY TO OPEN THIS FILE
         // HERE -- IT IS JUST CONVENIENT SINCE THE NAME IS PRESENT)
         const CString dot          = table1.Right(4);

         if (dot[0] == '.') // table1 is at least a file name
         {
            const CString last_2_chars = table1.Right(2);

            if ((m_feature_table == NULL) && (last_2_chars.Compare(_T("ft")) == 0))
            {
               // no feature table open and table1_is_feature_table

               FeatueTableName = table1;
               m_feature_table = new VPFFeatureTable(cov, FeatueTableName);
               m_feature_table->open();
            }
            else if ((m_join_table == NULL) && (last_2_chars.Compare(_T("jt")) == 0))
            {
               // Kludge :: We have a problem processing the NOTES Join Table
               // since it does not represent all entries in the feature table,
               // only selected entries.  Until we support it, skip it for now.
               if (table1.Right(3) == _T("njt"))
                  continue;
               if (table1.Right(3) == _T("rat"))
                  continue;

               // DVL : temp removal of area join table support
               //if (table1.Right(3) == "ajt")
               //   continue;

               // no join table open and table1_is_join_table
               JoinTableName = table1;
            }
            else
            {
               // We are only interested in links OUT of this table, 
               // AND
               // We have not found the table entry yet,... 
               continue;
            }
         }

         // At this point, we have the feature table, and are on an entry pertaining to 
         // this feature class.  First, determine if it is a link to a primitive file,...
         if (FeatueTableName == table1)
         {
            // Now we are looking for join table entries and links into the 
            // primitive files.

            // get table2
            value = fcs->get_field_value(_T("table2"));
            table2 = value->m_text;

            // get table1_key
            value = fcs->get_field_value(_T("table1_key"));
            table1_key = value->m_text;

            if ((table2 == _T("txt")) || (table2 == _T("end")) || (table2 == _T("cnd")) ||
               (table2 == _T("edg")) || (table2 == _T("fac")))
            {
               LinkField = table1_key;

               // Unsupported table join found in fcs
               ASSERT(m_name == table1.Left(table1.Find('.')));

               // Found what we are looking for
               break;
            }
            else if (table2.Right(2)==_T("jt"))
            {
               LinkField = table1_key;

               // We still need the link from the join table to the primitive table
               if (m_join_table)
                  // We already have it
                  break;
            }

            // DVL :: UNUSED AT THE MOMENT, PROBABLY SHOULD BE, BUT PREDICTABLY=="ID"
            // This will be the id field in the primitive table -- always
            // get table2_key
            //value = fcs->get_field_value("table2_key");
            //table2_key = value->m_text;
         }
         // or to a join table
         else if (JoinTableName == table1)
         {        
            // Now we are looking for join table entries and links into the 
            // primitive files.

            // get table2
            value = fcs->get_field_value(_T("table2"));
            table2 = value->m_text;

            // get table1_key
            value = fcs->get_field_value(_T("table1_key"));
            table1_key = value->m_text;

            if ((table2 == _T("txt")) || (table2 == _T("end")) || (table2 == _T("cnd")) ||
               (table2 == _T("edg")) || (table2 == _T("fac")))
            {
               PrimField = table1_key;
            }
            else
            {
               // We are looking for the link into the primitive table from
               // the join table.  We found a like to somewhere else
               continue;
            }

            // DVL :: UNUSED AT THE MOMENT, PROBABLY SHOULD BE, BUT PREDICTABLY=="ID"
            // This will be the id field in the primitive table -- always

            // get table2_key
            value = fcs->get_field_value(_T("table2_key"));
            table2_key = value->m_text;

            ASSERT(m_join_table == NULL);
            m_join_table = new VPFJoinTable(cov->get_path()+JoinTableName,
               PrimField, _T(""));

            // We still need the link from the feature table to the join table
            if (!LinkField.IsEmpty())
               break;
        }
      }
   }


   if ((m_name.Find('.', 0)!=-1) && (LinkField.IsEmpty()))
   {
      ASSERT(false);
      CString Str;
      Str.Format(_T("Error detected in VPF Feature Class Schema table.\n")
                 _T("%s\\%s\nNo primitive LinkField provided"), 
                 (LPCSTR)cov->get_path(), (LPCSTR)m_name);

//      ERR_report(Str);
   }

   // Set delineation of the feature class (symbol types), to be used
   // by GeoSym to note the type of symbol being searched for

   switch(m_name[name.GetLength()-1])
   {
   case 't':
      m_delineation = 0;
      break;

   case 'p':
   case 'c':
      m_delineation = 1;
      break;

   case 'l':
   case 'e':    // dqline.lft
   case 'f':    // libreft,  may be several things,.... need to check extension
      m_delineation = 2;
      break;

   case 'a':
      m_delineation = 3;
      break;

   default:
      ASSERT(false);
      m_delineation = -1;
   }

   m_isopen = true;
}

//----------------------------- ~VPFFeatureClass -------------------------------
VPFFeatureClass::~VPFFeatureClass()
{
   delete m_feature_table;
   delete m_join_table;
}


const CString& VPFFeatureClass::GetName()
{
   return(m_name);
}



//------------------------------- description ----------------------------------
CString VPFFeatureClass::description()
{
   return (m_description);

/*


   //dvl :: 00/9/14
   
   //The feature class attribute table really belongs to the coverage and not to
   //the 5 or 10 feature classes it contains.  This is move up to the coverage
   //level to reduce the number of copies of the tables.

   try
   {
      VPFFeatureClassAttributeTable* fca = m_coverage->GetFeatureClassAttributeTable();

      if (fca->is_open())  // should already be open
      {
         for (fca->move_first(); !fca->is_eof(); fca->move_next())
         {
            if (fca->feature_class_name() == m_name)
               return (fca->feature_class_description());
         }
      }
      else
      {
         // tileref, rference do no have a fca
         return (m_name);
      }
   }
   catch(VPFException *e)
   {
      delete e;
   }

   return ("Unknown");
*/   
}

