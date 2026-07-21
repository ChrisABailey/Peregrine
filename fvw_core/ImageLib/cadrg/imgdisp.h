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



// disp.h
//

#pragma once

#define BITMAP_BLACK 217

#define CADRG_SF_WIDTH     256   // CADRG subframe width
#define CADRG_SF_HEIGHT    256   // CADRG subframe height
#define CADRG_SF_SIZE    65536   // CADRG_SF_WIDTH * CADRG_SF_HEIGHT

#define CADRG_FRAME_WIDTH_IN_SF   6  // frame width in subframes
#define CADRG_FRAME_HEIGHT_IN_SF  6  // frame height in subframes
#define CADRG_FRAME_SIZE_IN_SF   36  // frame size in subframes (w x h)

#define MAX_NUM_CADRG_OR_CIB_SUBFRAMES CADRG_FRAME_SIZE_IN_SF

typedef struct 
{
   double ll_lat;
   double ll_lon;
   double ur_lat;
   double ur_lon;
} subframe_bounds_t;

#include "cdb_base.h"
#include "RpfZoneScales.h"
#include <map>
#include <list>
#ifdef _WIN32
#include <comutil.h>
#endif

class CRpfCoverageCache;
class CCoverageCacheEntry;

struct SUBFRAME_KEY
{
   int nServerID;
   int nTileID;
   int nSubFrameID;

   bool operator <(const struct SUBFRAME_KEY &lhs) const
   {
      if (nServerID == lhs.nServerID)
      {
         if (nTileID == lhs.nTileID)
         {
            return nSubFrameID < lhs.nSubFrameID;
         }
         else
            return nTileID < lhs.nTileID;
      }
      else
         return nServerID < lhs.nServerID;
   }
};

struct CIB_HISTOGRAM_ENTRY
{
   int nServerID;
   int nTileID;
   UINT4 num_color_records;
   BOOL bPrimaryLutFound;
   unsigned char lut[216*3];
   UINT4 histogram[216];
};

class RPFSubFrame
{
public:

   RPFSubFrame() { memset(this, 0, sizeof(*this)); }

   unsigned char *image;
   unsigned char lut[256*3];
   int num_lut_entries;
   BOOL subframe_contains_transparency;

   SUBFRAME_KEY hash_key;
};

#define MAX_NUM_CACHED_TILES 150 // (~10 Mb)
#define MAX_NUM_CACHED_HISTOGRAMS 10


class RPFRenderer
{
public:
   RPFRenderer() { m_auto_enhance = TRUE; }
   ~RPFRenderer();

//   int display(IActiveMapProj *map, CRpfCoverageCache *cov_list, CIdentitiesSet &out_of_sync_ID_set,
//      CIdentitiesSet &offline_ID_set);

   void set_auto_enhance(BOOL auto_enhance) { m_auto_enhance = auto_enhance; }
   BOOL get_auto_enhance() { return m_auto_enhance; }
   void calculate_bright_contrast(double *bright, double *contrast, int *midval);


//   RPFTileCache &GetTileCache() { return m_tile_cache; }

protected:
	BOOL m_auto_enhance;
	UINT4 m_composite_histogram[216];
	FILE *m_frame_fp;
	BYTE m_lut[216 * 3];
	int m_num_lut_entries;

	std::list<CIB_HISTOGRAM_ENTRY *> m_listCibHistogramEntries;
   
public:
	int get_frame_image(CString filename, BOOL is_cib, BYTE *img);
	int get_rgb_image(CString filename, BOOL is_cib, int startx, int starty, int width, int height, BYTE *img_data);
	int read_and_display_subframe(FILE* fp, BOOL is_cib, int subframe_index, cadrg_cib_base &cadrg, BYTE *frame_buf);


protected:
   int get_pixel_position_in_frame(CString source, double dScale,
      MapScaleUnitsEnum eScaleUnits, CArcZone primary_zone, double pixel_latitude, 
      double pixel_longitude, int* pixel_row_in_frame, int* pixel_column_in_frame);
   
   int get_cib_composite_histogram(CRpfCoverageCache *cov_list, 
      CString source, BOOL use_primary_zone_only, 
      CArcZone primary_zone, UINT4* num_entries, unsigned char lut[216*3], 
      UINT4 composite_histogram[216], double map_ll_lat, double map_ll_lon,
      double map_ur_lat, double map_ur_lon);

   void calculate_stretch_lut(UINT4 *histogram, unsigned char *orig_lut,
      unsigned char *lut);

	int get_primary_lut(cadrg_cib_base& frame, CString source, unsigned char lut[256*3], 
						RPF_color_table_and_histogram*& table,  BOOL* found);
   
   int get_primary_lut_table(cadrg_cib_base& frame, CString source, 
      RPF_color_table_and_histogram*& table, BOOL* found);
   
   int extract_lut_from_table(RPF_color_table_and_histogram*& table,
      CString source, unsigned char lut[256*3]);
   
   int get_uncompressed_image(cadrg_cib_base* cdb, int subframe_index, 
      FILE* fp, BOOL compressed, int compression_id, UINT4 image_width, 
      UINT4 image_height, unsigned char* image);
   
   int resize_pixmap(const int old_width, const int old_height, 
      const unsigned char* const old_image, const int new_width, const int new_height, 
      unsigned char* const new_image, const int new_padded_width,
      const int new_start_row, const int new_end_row);
};

class RPFHelper
{
public:
   BOOL region_intersect(double ll_A_lat, double ll_A_lon,
      double ur_A_lat, double ur_A_lon, double ll_B_lat, double ll_B_lon,
      double ur_B_lat, double ur_B_lon);
   
   BOOL region_enclose(double ll_A_lat, double ll_A_lon,
      double ur_A_lat, double ur_A_lon,
      double ll_B_lat, double ll_B_lon,
      double ur_B_lat, double ur_B_lon);
   
   BOOL is_east_of(double a, double b);

protected:
   BOOL lon_in_range(double left_lon, double right_lon, double point_lon);
};