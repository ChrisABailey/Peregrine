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

// tiffdata.h

#ifndef TIFFDATA_H
#define TIFFDATA_H

//#include "geotiff.h"

// ****************************************************************
// ****************************************************************

class CTiffData
{
public:

	CTiffData();
	~CTiffData();

	CString m_filename;
	CString m_image_data_file_name;
	CString m_image_description;
	CString m_software;

	int m_width;
	int m_height;
	int m_output_bits_pixel;
	int m_compress_type;
	int m_bpp;

	// tile data
	BOOL m_image_is_tiled;
	int m_tile_width;
	int m_tile_height;
	int m_tile_cnt_x;
	int m_tile_cnt_y;
	int m_num_tiles;
	int *m_tile_offset;
	
	// strip data
	int m_rows_per_strip;
	int m_num_strips;
	int *m_strip_offset;

	BYTE m_color_map_red[256];
	BYTE m_color_map_grn[256];
	BYTE m_color_map_blu[256];

	unsigned short m_compression_scheme;

	// type of image (monochrome, color palette, rgb, etc.)
	unsigned short m_photometric_interpretation;

	// resolution unit (none, inch, centimeter)
	unsigned short m_resolution_unit;

	// planar configuration
	unsigned short m_planar_configuration;

	// samples per pixel
	unsigned short m_samples_per_pixel;

	// bits per sample
	unsigned short m_bits_per_sample;

	// fill order
	unsigned short m_fill_order;

	// orientation
	unsigned short m_orientation;

	  // geokey directory number of keys
	unsigned short m_num_geokeys;

	// geokeys
//	CGeoKey *m_geokeys;

	int m_image_type;
	int m_linear_units;

	double m_tiepoint_lat;
	double m_tiepoint_lon;
	int m_tiepoint_x;
	int m_tiepoint_y;

	double m_output_deg_per_pixel_lat;
	double m_output_deg_per_pixel_lon;

};

// ****************************************************************
// ****************************************************************

#endif  // ifndef TIFFDATA_H
