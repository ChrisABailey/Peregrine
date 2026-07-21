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

// map.h

#ifndef MAP_H
#define MAP_H

#include "geotiff.h"
#include "transfrm.h"
#include <map>

class CImageMap
{
   friend class CColorQuantizer;

public:
	CImageMap();
	~CImageMap();

   typedef std::map< CString, CGeoTiff* > GeoTiffMap;
   typedef GeoTiffMap::value_type GeoTiffMapVal;
   typedef GeoTiffMap::iterator GeoTiffMapIter;
   typedef std::pair< GeoTiffMapIter, bool > GeoTiffMapIterPair;

   int m_screen_width;
	int m_screen_height;
	double m_screen_ul_lat;
	double m_screen_ul_lon;
	double m_screen_lr_lat;
	double m_screen_lr_lon;
	CTransform m_tiepoint_transform;
	int m_transparency_mode;
	BYTE m_ul_pix_red;
	BYTE m_ul_pix_grn;
	BYTE m_ul_pix_blu;


	int set_transparency_mode(int mode);

	int geo_to_surface(double lat, double lon, int *x, int *y);
	int surface_to_geo(int x, int y, double *lat, double *lon);

	int get_paletted_map_image(CStringArray *files, double ul_lat, double ul_lon, double lr_lat, double lr_lon, 
									int width, int height, BYTE *screen_image, BYTE *color_table, 
									int *err, CString &err_msg);

	int get_rgb_map_image(CStringArray *files, double ul_lat, double ul_lon, double lr_lat, double lr_lon, 
									int width, int height, BYTE *screen_image, 
									int *err, CString &err_msg);


	int check_all_8bit( /*CStringArray *map_files,*/ BOOL &all_8bit,
                        CColorQuantizer &color_quantizer, unsigned char *pal_red,
                        unsigned char *pal_green, unsigned char *pal_blue );

   int display_element( CGeoTiff& geotiff, double ul_lat, double ul_lon, double lr_lat, double lr_lon,
						int width, int height, int num_bits, CColorQuantizer &color_quantizer, 
						unsigned char *screen_image, CString & err_msg );

	int palettize_image( int num_pixels, unsigned char *image_red,
                         unsigned char *image_green, unsigned char *image_blue,
                         int max_colors, unsigned char *pal_red,
                         unsigned char *pal_green, unsigned char *pal_blue,
                         unsigned char *palette_image );

	int fwd_transform( CGeoTiff &geotiff, CString datum_string,
                       double latitude, double longitude,
                       int &hpix, int &vpix );

	int inv_transform( CGeoTiff &geotiff, CString datum_string,
                       int hpix, int vpix,
                       double &latitude, double &longitude );

   IImageLibCallback *m_callback;

   BOOL InactiveGeoTiffCleanup();

   int get_rgb_group_map_image(CList<C_group_item*, C_group_item*> & group_list, double ul_lat, double ul_lon, 
							   double lr_lat, double lr_lon, int width, int height, unsigned char *img_data, 
							   int *err, CString & error_msg);

   int display_item(C_group_item *item, BYTE* img_data);


private:
   GeoTiffMap m_gtfmGeoTiffMap;   // Accessed by filespec


};

#endif   // ifndef MAP_H