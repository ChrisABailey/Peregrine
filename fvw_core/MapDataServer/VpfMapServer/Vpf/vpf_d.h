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

#if !defined(VPF_DEFS_H__89E52558_8652_11D3_8663_00105A9B4838__INCLUDED_)
#define VPF_DEFS_H__89E52558_8652_11D3_8663_00105A9B4838__INCLUDED_

#ifdef _WIN32
#include <atlstr.h>	// for CString
#else
#include "fv_compat.h"   // POSIX port (2026-07-19)
#include "fv_cstring.h"  // CString
#endif

#ifdef _WIN32
#include "comdate.h"
#else
#include "fv_oledatetime.h"  // COleDateTime emulation (POSIX)
#endif

#include <stdint.h>   // int32_t: on-disk field widths (see indexes.h)

#include <vector>
#include <algorithm>

#ifdef _WIN32
typedef CComDATE COleDateTime;
#endif  // POSIX: fv_oledatetime.h defines COleDateTime directly

typedef double degrees_t;

typedef struct d_geo_t {
	double lat;
	double lon;
} d_geo_t;


// geo coordinate rect in degrees
typedef struct d_geo_rect_t
{
   d_geo_t ll; // lower-left corner
   d_geo_t ur; // upper-right corner
} d_geo_rect_t;

// Associates a tile name with a bounding rect
class VPFTileBounds
{
public:
   short tile_id;
   TCHAR tile_name[64];
	d_geo_rect_t m_geo_rect;

   bool operator==(const VPFTileBounds &t) const
	{
      CString s1(tile_name);
      CString s2(t.tile_name);

	   return s1 == s2;
	}
};


#define SUCCESS  0
#define FAILURE -1

#define FIL_READ_OK   4
#define FIL_WRITE_OK  2
#define FIL_EXISTS    0



bool lon_in_range(double left_lon, double right_lon, double point_lon);
bool in_bounds(double ll_lat, double ll_lon, double ur_lat, double ur_lon,
   double p_lat,  double p_lon);
bool in_bounds_degrees(d_geo_t ll, d_geo_t ur, d_geo_t p);


// VPFType contains all the possible VPF field types as defined
// in MIL-STD-2407 Section 5.3.1.1 (see Table 10)
enum VPFType
{
   VPF_NULL               = 0x00,  // X

   VPF_2COORD_SHORT_FLOAT = 0x01,  // C
   VPF_2COORD_LONG_FLOAT  = 0x02,  // B
   VPF_3COORD_SHORT_FLOAT = 0x04,  // Z
   VPF_3COORD_LONG_FLOAT  = 0x08,  // Y

   VPF_TEXT               = 0x10,  // T
   VPF_LATIN1_TEXT        = 0x11,  // L
   VPF_FULL_LATIN_TEXT    = 0x12,  // N
   VPF_MULTI_LINGUAL_TEXT = 0x14,  // M

   VPF_FLOAT_SHORT        = 0x18,  // F
   VPF_FLOAT_LONG         = 0x20,  // R
   VPF_INT_SHORT          = 0x21,  // S
   VPF_INT_LONG           = 0x22,  // I

   VPF_DATE_TIME          = 0x24,  // D

   VPF_TRIPLET_ID         = 0x28   // K
};
/*
// Associates a tile name with a bounding rect
struct VPFTileBounds : public d_geo_rect_t
{
   short tile_id;
   char tile_name[64];
};
*/

bool FIL_access(const TCHAR *path, int mode);
int FIL_create_directory(const TCHAR *directory);
int FIL_set_permissions(const TCHAR *filename, int permissions);
int FIL_is_dir_empty(const TCHAR* dir_name, bool* empty);


#pragma pack (push, 2)
struct VPFTripletId
{
   long id;       // 32 bit maximum possible
   short tile_id; // 16 bit maximum possible
   long ext_id;   // 32 bit maximum possible
};
#pragma pack (pop)

enum VPFKeyType
{
   VPF_KEY_PRIMARY,
   VPF_KEY_UNIQUE,
   VPF_KEY_NON_UNIQUE
};

const TCHAR * VPFKeyType_to_String(const int type);
const TCHAR * VPFType_to_String   (const int type);

int VPF_copy_elements(const CString& db_name, const CString& lib_name, const CString& tile_name, long tile_id,
							 const CString& source, const CString& target);
int VPF_delete_elements(const CString& db_name, const CString& lib_name, const CString& tile_name, long tile_id,
								CString target);

#endif // !defined(VPF_DEFS_H__89E52558_8652_11D3_8663_00105A9B4838__INCLUDED_)

