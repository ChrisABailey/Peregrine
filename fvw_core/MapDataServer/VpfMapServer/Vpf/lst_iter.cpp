// Copyright (c) 1994-2011,2014 Georgia Tech Research Corporation, Atlanta, GA
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



// lst_iter.cpp : implementation file
// created by Robert 12/10/99

#include "stdafx.h"

#include "lst_iter.h"
#include "vpf_d.h"
#include "variant.h" 
#include "tables.h"  

#include <algorithm> 

 
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char BASED_CODE THIS_FILE[] = __FILE__;
#endif


/////////////////////////////////////////////////////////////////////////////
// VPF_tile_bounds_list_iterator
    
   
// Constructor
VPF_tile_bounds_list_iterator::VPF_tile_bounds_list_iterator(const CString& path_to_library)
: m_path_to_library( path_to_library), m_position(0), m_list_size(0)
{
   m_pExtern_tile_bounds_list = NULL;

   m_external_list = false;
}

VPF_tile_bounds_list_iterator::VPF_tile_bounds_list_iterator(
   const std::vector<VPFTileBounds>* tile_list ) :
   m_pExtern_tile_bounds_list( const_cast<std::vector<VPFTileBounds>*>(tile_list) ), m_position(0)
{  
   m_external_list = true;
   m_list_size = m_pExtern_tile_bounds_list->size();
}

// Default Constructor
VPF_tile_bounds_list_iterator::VPF_tile_bounds_list_iterator() 
: m_position(0), m_external_list(false), m_list_size(0)
{
   m_pExtern_tile_bounds_list = NULL;
}


VPF_tile_bounds_list_iterator::~VPF_tile_bounds_list_iterator()
{
   reset();
}

int VPF_tile_bounds_list_iterator::reset()
{
   // initialize get_next() data source pointer to NULL       
   m_position = 0;

   m_tile_bounds_list.clear();

   m_list_size = m_tile_bounds_list.size();

   return SUCCESS;
}

VPFTileBounds* VPF_tile_bounds_list_iterator::get_first()  
{
   // get head position
   if ( m_list_size > 0 )
      return &m_tile_bounds_list[m_position++];

   return NULL;
}

VPFTileBounds* VPF_tile_bounds_list_iterator::get_next()
{
   VPFTileBounds* pBounds = NULL;

   // if there is a next element, return it and save the next position
   if (m_list_size > 0 && m_position < m_list_size ) 
      pBounds = &m_tile_bounds_list[m_position++];
   
   return pBounds;
}   

VPFTileBounds* VPF_tile_bounds_list_iterator::get_current()
{
   VPFTileBounds* pBounds = NULL;

   // if there is a next element, return it and save the next position
   if (m_list_size > 0 && m_position < m_list_size ) 
      pBounds = &m_tile_bounds_list[m_position];
   
   return NULL;
}   


//---------------------------------- bounds ------------------------------------
int VPF_tile_bounds_list_iterator::build_tile_bounds_list() 
{
   const bool tileref_coverage_exists = (true == FIL_access(
      m_path_to_library + _T("\\tileref"), FIL_EXISTS));

   if (tileref_coverage_exists)
   {
      CString tileref_path(m_path_to_library + _T("\\tileref"));

      VPFTileBounds tile_bounds;
      VPFVariant *value;

      // Create the tables
      VPFBoundingRectangleTable* tileref_fbr = new VPFBoundingRectangleTable(m_path_to_library);
      VPFRecordset* tileref_fac = new VPFRecordset(m_path_to_library);
      VPFRecordset* tileref_aft = new VPFRecordset(m_path_to_library);

      // Open the tables
      tileref_fbr->open( _T("\\tileref\\fbr"));
      tileref_fac->open( _T("\\tileref\\fac"));
      tileref_aft->open( _T("\\tileref\\tileref.aft"));

      int rec_pos;
      const int size_aft = tileref_aft->get_record_count();
      for (int i = 0; i < size_aft; i++)
      {
         tileref_aft->set_absolute_position(i);

         // set the tile name
         value = tileref_aft->get_field_value(_T("tile_name"));
         _tcscpy_s(tile_bounds.tile_name, 64, value->m_text);
         // TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed.\n"), __LINE__, __FILE__);

         //ATLTRACE( _T("Tile name [%d] = %s\n"), i, tile_bounds.tile_name);

         // set the tile id
         value = tileref_aft->get_field_value(_T("fac_id"));
      
         if ( value->m_int_long > 0)
         {
            rec_pos = value->m_int_long - 1;
            ASSERT((int)((short)rec_pos) == rec_pos); // Just checking in case something is too big
            tile_bounds.tile_id = (short)rec_pos;// convert to a 0-based index

            tileref_fbr->set_absolute_position(rec_pos);
            tile_bounds.m_geo_rect = tileref_fbr->bounds();

            m_tile_bounds_list.push_back(tile_bounds);
         }
      }

      // clean up
      delete tileref_fbr;
      delete tileref_fac;
      delete tileref_aft;

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

      tile_bounds.m_geo_rect.ll.lat = -90.0;
      tile_bounds.m_geo_rect.ll.lon = -180.0;
      tile_bounds.m_geo_rect.ur.lat = 90.0;
      tile_bounds.m_geo_rect.ur.lon = 180.0;

      tile_bounds.tile_name[0] = '\0';

      //m_tile_bounds_list->AddTail(tile_bounds);
      m_tile_bounds_list.push_back(tile_bounds);
   }

   // remove any duplicates
   m_tile_bounds_list.erase( std::unique(m_tile_bounds_list.begin(),
      m_tile_bounds_list.end()), m_tile_bounds_list.end() );

   m_list_size = m_tile_bounds_list.size();

   return SUCCESS;
}

