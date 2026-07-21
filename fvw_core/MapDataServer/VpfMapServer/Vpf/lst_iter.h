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


#ifndef TILE_LIST_ITERATOR_H
#define TILE_LIST_ITERATOR_H 1

#include "vpf_d.h"


class VPF_tile_bounds_list_iterator
{
private:
	CString m_path_to_library;		// path to the VPF library
	
   std::vector<VPFTileBounds> m_tile_bounds_list;
   std::vector<VPFTileBounds> *m_pExtern_tile_bounds_list;
	
   //POSITION m_position;               // position in list
   int m_position;               // position in list

   int m_list_size;

	bool m_external_list;

public:
   // Constructor
   VPF_tile_bounds_list_iterator(const std::vector<VPFTileBounds>* tile_list); 

   // Constructor
   VPF_tile_bounds_list_iterator(const CString& path_to_library); 

   // Default Constructor
   VPF_tile_bounds_list_iterator(); 


   // Destructor  private,  
   virtual ~VPF_tile_bounds_list_iterator();

   // Re-initialize list from INI file.  Old data sources are destroyed.
   int reset();

	void set_library_path( const CString& library_path ) { m_path_to_library = library_path; }

   // Returns the first data source in the list or NULL if the list is
   // empty.  The next data source returned by get_next() is set to the
   // second data source in the list (or NULL if the list has one element).
   VPFTileBounds* get_first(); 

   // Returns the next data source in the list or NULL if the next position
   // is at the end of the list.
   VPFTileBounds* get_next();

   // Returns the current data source in the list or NULL if the current position
   // is at the end of the list.
   VPFTileBounds* get_current();

   //get number of items in list
   int get_count(void) {return m_tile_bounds_list.size();} 

	// Get the tiles 
	int build_tile_bounds_list();  // (was MSVC-only extra qualification)
};


#endif  // TILE_LIST_ITERATOR_H