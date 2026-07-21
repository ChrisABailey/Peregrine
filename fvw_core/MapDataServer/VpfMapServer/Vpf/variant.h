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

#if !defined(VARIANT_H__89E52557_8652_11D3_8663_00105A9B4838__INCLUDED_)
#define VARIANT_H__89E52557_8652_11D3_8663_00105A9B4838__INCLUDED_

#include "vpf_d.h"

typedef struct _3coord_float_
{
	float lon;
	float lat;
	float alt;
} coord3_float_t;

typedef struct _3coord_double_
{
	double lon;
	double lat;
	double alt;
} coord3_double_t;

typedef struct _2coord_float_
{
	float lon;
	float lat;
} coord2_float_t;

typedef struct _2coord_double_
{
	double lon;
	double lat;
} coord2_double_t;

//------------------------------------------------------------------------------
//-------------------------------- VPFVariant ----------------------------------
//------------------------------------------------------------------------------
// A VPFVariant represents all the types of data that can be a field in a VPF
// database table.  This way, you can generically get the value in a recordset
// for a given field (like a CDaoRecordset gets a COleVariant).  The VPFType
// enum allows for a common way to describe the different types available, and
// allows for very strict type checking.
//------------------------------------------------------------------------------
class VPFVariant
{
//--- Construction ----------------------------------------
public:
   VPFVariant();
   virtual ~VPFVariant();
   VPFVariant(const VPFVariant &val);
   VPFVariant &operator=(const VPFVariant &val);

//--- Public Methods --------------------------------------
public:
   bool is_text() const;
   bool is_float_short() const;
   bool is_float_long() const;
   bool is_int_short() const;
   bool is_int_long() const;
   bool is_coords() const;
   bool is_date_time() const;
   bool is_triplet_id() const;
   bool is_null() const;
   void remove_coords_list(void);

//--- Private Methods -------------------------------------
private:


//--- Member Data -----------------------------------------
// Normally you would make member data private, but in this
// case we're making a glorified struct more than we're
// defining a class, so we'll give access to the internals.
//---------------------------------------------------------
public:
   VPFType m_type;

   // I attempted to make the following a union, so as to minimize
   // storage space, but I would have had to make the CStrings and
   // the COleDateTime pointers to those objects, not the objects
   // themselves.  This introduced the problem of memory management
   // of globally allocated memory, overriding operator=, etc.
   // Due to the complexity involved, I opted to make the object
   // take up more space, but manage its own memory.
	//
	// Made an anonymous union from the remaining nonproblematic types -- George
   CString       m_text;
   
	coord3_float_t *m_3coord_float_coords;
	coord3_double_t *m_3coord_double_coords;
	coord2_float_t *m_2coord_float_coords;
	coord2_double_t *m_2coord_double_coords;

	int m_num_coords;
   COleDateTime  m_date_time;

	union
	{
	float         m_float_short;
   double        m_float_long;
   short         m_int_short;
   long          m_int_long;
   VPFTripletId  m_triplet_id;
	};
};

#endif // !defined(VARIANT_H__89E52557_8652_11D3_8663_00105A9B4838__INCLUDED_)