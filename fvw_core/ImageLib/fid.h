// Copyright (c) 1994-2010 Georgia Tech Research Corporation, Atlanta, GA
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

// fid.h

#pragma once

#ifdef _WIN32
#include <afxtempl.h>
#else
#include "fv_mfc_containers.h"
#endif
#include <map>

#ifndef SUCCESS
#  define SUCCESS 0
#endif
#ifndef FAILURE
#  define FAILURE -1
#endif


#define FID_FILE_SENTINAL  "FalconView Image Data"
#define GID_FILE_SENTINAL  "GeoRect Image Data"
#define FID_CS_TYPE        "Projected CS Type"
#define FID_CS_TYPE_STR    "Projected CS Type Text"
#define FID_HIST1	         "Histogram1"
#define FID_HIST2	         "Histogram2"
#define FID_HIST3	         "Histogram3"
#define FID_HIST4	         "Histogram4"
#define FID_HIST5	         "Histogram5"
#define FID_HIST6	         "Histogram6"
#define FID_HIST7	         "Histogram7"
#define FID_HIST8	         "Histogram8"
#define FID_DATA_SCALE     "ImageDataScale"
#define FID_DATA_OFFSET    "ImageDataOffset"
#define FID_CONTRAST       "Contrast Values"
#define FID_POSITION       "Position Values"
#define FORMULA_NAME       "Formula Name"
#define RED_FORMULA        "Red Formula"
#define GRN_FORMULA        "Grn Formula"
#define BLU_FORMULA        "Blu Formula"


// ****************************************************************
// ****************************************************************

class CFid
{
protected:
	CString m_filename;
   typedef std::map< CString, CString > FidMap;
	FidMap m_list;

   virtual LPCSTR GetSentinalString() const { return FID_FILE_SENTINAL; }
   int ReadFile();

public:
	int open( const CString& filename );
	int save() const;
	int save_as( const CString& filename );
	int get_projected_cs_type(int *cs_type, CString & cs_type_str) const;
	int set_projected_cs_type(int cs_type, const CString& cs_type_str);
	int get_histogram( PUINT hist) const;  // 256 luminance values
	int set_histogram( const PUINT hist);  // 256 luminance values
	int get_contrast(int *minval, int *ctrval, int *maxval) const;
	int set_contrast(int minval, int ctrval, int maxval);
	int get_position(int *startx, int *starty, double *zoom) const;
	int set_position(int startx, int starty, double zoom);
#if 0
	int get_tile_offsets(int *cnt, int **offset);  
	int set_tile_offsets(int cnt, int *offset);
#endif
	int remove_key( const CString& key );
	int get_key( const CString& key, CString& value) const;

}; // class CFid

// ****************************************************************
// ****************************************************************

class CGid : public CFid
{
public:
	int open( const CString& filename );
	int get_formula( const CString& filename, CString& form_name, CString& red_form, CString& grn_form, CString& blu_form );
	int set_formula( const CString& filename, const CString& form_name,
                   const CString& red_form, const CString& grn_form, const CString& blu_form );
	int get_contrast( const CString& filename, int *minval, int *ctrval, int *maxval);
	int set_contrast( const CString& filename, int minval, int ctrval, int maxval);
	int get_position( const CString& filename, int *startx, int *starty, double *zoom);
	int set_position( const CString& filename, int startx, int starty, double zoom);

	int open_gid_file_for_read( const CString& filename );
	int open_gid_file_for_write( const CString& filename );

private:
   virtual LPCSTR GetSentinalString() const { return GID_FILE_SENTINAL; }
	int make_gid_name( const CString& filename, CString& gidname, BOOL old_style = FALSE ) const;
}; // CGid

// End of fid.h
