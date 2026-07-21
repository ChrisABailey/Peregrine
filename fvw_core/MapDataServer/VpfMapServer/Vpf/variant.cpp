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
#include "variant.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


//------------------------------------------------------------------------------
//-------------------------------- VPFVariant ----------------------------------
//------------------------------------------------------------------------------
VPFVariant::VPFVariant()
{
   m_type = VPF_NULL;

	m_3coord_float_coords = NULL;
	m_3coord_double_coords = NULL;
	m_2coord_float_coords = NULL;
	m_2coord_double_coords = NULL;
}

//-------------------------------- VPFVariant ----------------------------------
VPFVariant::VPFVariant(const VPFVariant &val)
{
   m_type = val.m_type;

	m_3coord_float_coords = NULL;
	m_3coord_double_coords = NULL;
	m_2coord_float_coords = NULL;
	m_2coord_double_coords = NULL;

   switch (m_type)
   {
      case VPF_NULL:
         break;

      case VPF_FLOAT_SHORT:
         m_float_short = val.m_float_short;
         break;

      case VPF_FLOAT_LONG:
         m_float_long = val.m_float_long;
         break;

      case VPF_INT_SHORT:
         m_int_short = val.m_int_short;
         break;

      case VPF_INT_LONG:
         m_int_long = val.m_int_long;
         break;

      case VPF_TEXT:
      case VPF_LATIN1_TEXT:
      case VPF_FULL_LATIN_TEXT:
      case VPF_MULTI_LINGUAL_TEXT:
         m_text = val.m_text;
         break;

      case VPF_2COORD_SHORT_FLOAT:

			m_2coord_float_coords = (coord2_float_t *)
				malloc(sizeof(coord2_float_t) * val.m_num_coords);
			memcpy(m_2coord_float_coords, val.m_2coord_float_coords,
				sizeof(coord2_float_t) * val.m_num_coords);
			m_num_coords = val.m_num_coords;
			break;

      case VPF_3COORD_SHORT_FLOAT:

			m_3coord_float_coords = (coord3_float_t *)
				malloc(sizeof(coord3_float_t) * val.m_num_coords);
			memcpy(m_3coord_float_coords, val.m_3coord_float_coords,
				sizeof(coord3_float_t) * val.m_num_coords);
			m_num_coords = val.m_num_coords;
			break;

      case VPF_2COORD_LONG_FLOAT:

			m_2coord_double_coords = (coord2_double_t *)
				malloc(sizeof(coord2_double_t) * val.m_num_coords);
			memcpy(m_2coord_double_coords, val.m_2coord_double_coords,
				sizeof(coord2_double_t) * val.m_num_coords);
			m_num_coords = val.m_num_coords;
			break;

      case VPF_3COORD_LONG_FLOAT:
         
			m_3coord_double_coords = (coord3_double_t *)
				malloc(sizeof(coord3_double_t) * val.m_num_coords);
			memcpy(m_3coord_double_coords, val.m_3coord_double_coords,
				sizeof(coord3_double_t) * val.m_num_coords);
			m_num_coords = val.m_num_coords;

         break;

      case VPF_TRIPLET_ID:
         m_triplet_id.id      = val.m_triplet_id.id;
         m_triplet_id.tile_id = val.m_triplet_id.tile_id;
         m_triplet_id.ext_id  = val.m_triplet_id.ext_id;
         break;

      default:
         break;
   }
}

//------------------------------- ~VPFVariant ----------------------------------
VPFVariant::~VPFVariant()
{
   remove_coords_list();
}


//-------------------------------- remove_coords_list---------------------------
void VPFVariant::remove_coords_list(void)
{
	if (m_3coord_float_coords)
	{
		free(m_3coord_float_coords);
		m_3coord_float_coords = NULL;
	}
	if (m_3coord_double_coords)
	{
		free(m_3coord_double_coords);
		m_3coord_double_coords = NULL;
	}
	if (m_2coord_double_coords)
	{
		free(m_3coord_double_coords);
		m_2coord_double_coords = NULL;
	}
	if (m_2coord_float_coords)
	{
		free(m_2coord_float_coords);
		m_2coord_float_coords = NULL;
	}
}



//-------------------------------- operator= -----------------------------------
VPFVariant& VPFVariant::operator=(const VPFVariant &val)
{
   if (this == &val) return const_cast<VPFVariant&>(val); // self assignment check to prevent coord list damage

   remove_coords_list();

   m_type = val.m_type;

   switch (m_type)
   {
      case VPF_NULL:
         break;

      case VPF_FLOAT_SHORT:
         m_float_short = val.m_float_short;
         break;

      case VPF_FLOAT_LONG:
         m_float_long = val.m_float_long;
         break;

      case VPF_INT_SHORT:
         m_int_short = val.m_int_short;
         break;

      case VPF_INT_LONG:
         m_int_long = val.m_int_long;
         break;

      case VPF_TEXT:
      case VPF_LATIN1_TEXT:
      case VPF_FULL_LATIN_TEXT:
      case VPF_MULTI_LINGUAL_TEXT:
         m_text = val.m_text;
         break;

      case VPF_2COORD_SHORT_FLOAT:

			m_2coord_float_coords = (coord2_float_t *)
				malloc(sizeof(coord2_float_t) * val.m_num_coords);
			memcpy(m_2coord_float_coords, val.m_2coord_float_coords,
				sizeof(coord2_float_t) * val.m_num_coords);
			m_num_coords = val.m_num_coords;
			break;

      case VPF_3COORD_SHORT_FLOAT:

			m_3coord_float_coords = (coord3_float_t *)
				malloc(sizeof(coord3_float_t) * val.m_num_coords);
			memcpy(m_3coord_float_coords, val.m_3coord_float_coords,
				sizeof(coord3_float_t) * val.m_num_coords);
			m_num_coords = val.m_num_coords;
			break;

      case VPF_2COORD_LONG_FLOAT:

			m_2coord_double_coords = (coord2_double_t *)
				malloc(sizeof(coord2_double_t) * val.m_num_coords);
			memcpy(m_2coord_double_coords, val.m_2coord_double_coords,
				sizeof(coord2_double_t) * val.m_num_coords);
			m_num_coords = val.m_num_coords;
			break;

      case VPF_3COORD_LONG_FLOAT:

			m_3coord_double_coords = (coord3_double_t *)
				malloc(sizeof(coord3_double_t) * val.m_num_coords);
			memcpy(m_3coord_double_coords, val.m_3coord_double_coords,
				sizeof(coord3_double_t) * val.m_num_coords);
			m_num_coords = val.m_num_coords;

         break;

      case VPF_TRIPLET_ID:
         m_triplet_id.id      = val.m_triplet_id.id;
         m_triplet_id.tile_id = val.m_triplet_id.tile_id;
         m_triplet_id.ext_id  = val.m_triplet_id.ext_id;
         break;

      default:
         break;
   }

   return *this;
}

//--------------------------------- is_text ------------------------------------
bool VPFVariant::is_text() const
{
   switch (m_type)
   {
      case VPF_TEXT:
      case VPF_LATIN1_TEXT:
      case VPF_FULL_LATIN_TEXT:
      case VPF_MULTI_LINGUAL_TEXT:
         return true;

      default:
         return false;
   }

}

//------------------------------ is_float_short --------------------------------
bool VPFVariant::is_float_short() const
{
   return (m_type == VPF_FLOAT_SHORT);
}

//------------------------------ is_float_long ---------------------------------
bool VPFVariant::is_float_long() const
{
   return (m_type == VPF_FLOAT_LONG);
}

//------------------------------- is_int_short ---------------------------------
bool VPFVariant::is_int_short() const
{
   return (m_type == VPF_INT_SHORT);
}

//------------------------------- is_int_long ----------------------------------
bool VPFVariant::is_int_long() const
{
   return (m_type == VPF_INT_LONG);
}

//-------------------------------- is_coords -----------------------------------
bool VPFVariant::is_coords() const
{
   switch (m_type)
   {
      case VPF_2COORD_SHORT_FLOAT:
      case VPF_2COORD_LONG_FLOAT:
      case VPF_3COORD_SHORT_FLOAT:
      case VPF_3COORD_LONG_FLOAT:
         return true;

      default:
         return false;
   }
}

//------------------------------- is_date_time ---------------------------------
bool VPFVariant::is_date_time() const
{
   return (m_type == VPF_DATE_TIME);
}

//------------------------------- is_date_time ---------------------------------
bool VPFVariant::is_triplet_id() const
{
   return (m_type == VPF_TRIPLET_ID);
}

//--------------------------------- is_null ------------------------------------
bool VPFVariant::is_null() const
{
   return (m_type == VPF_NULL);
}

