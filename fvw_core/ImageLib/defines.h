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

// defines.h

#ifndef DEFINES_H
#define DEFINES_H 1

#define MODE_SELECT     1
#define MODE_BOXZOOM    2
#define MODE_HAND       3
#define MODE_GCP        4
#define MODE_BOUND      5
#define MODE_TRANS_TOOL 6

#define TRANS_MODE_NONE         0
#define TRANS_MODE_RECTANGLE    1
#define TRANS_MODE_LASSO        2
#define TRANS_MODE_POLYGON      3
#define TRANS_MODE_BRUSH        4
#define TRANS_MODE_COLOR_SELECT 5

#define FORMAT_DEGREES						1
#define FORMAT_DEGREES_MINUTES			2
#define FORMAT_DEGREES_MINUTES_SECONDS 3
#define FORMAT_COORD_MILGRID				4
#define FORMAT_COORD_UTM				5

#define QUANT_MEDIAN_CUT 1
#define QUANT_VARIANCE	 2

#define OUTPUT_FILE_TIFF 1
#define OUTPUT_FILE_JPEG 2

#define TIFF_COMPRESS_NONE 1
#define TIFF_COMPRESS_PACKBITS 2
#define TIFF_COMPRESS_JPEG 7

#define TILE_SINGLE_IMAGE 1
#define TILE_MULTIPLE_IMAGES 2

#define TRANSPARENCY_NONE  0
#define TRANSPARENCY_BLACK 1
#define TRANSPARENCY_UPPER_LEFT_PIXEL 2

#define USER_ABORT IMAGELIB_ERROR_USER_ABORT    // In ImageLib.idl

typedef enum
{
	FILE_NOT_DEFINED = 0,
	FILE_BMP    = 1,
	FILE_TIF    = 2,
	FILE_JPG    = 3,
	FILE_RCT    = 4,
	FILE_ADRG   = 5,
	FILE_MRSID  = 6,
	FILE_TGA    = 7,
	FILE_PNG    = 8,
	FILE_GIF    = 9,
	FILE_FV     = 10,
	FILE_NITF   = 11,
	FILE_TIFLIB = 12,
	FILE_J2K    = 13,
	FILE_GDIPLUS = 14,
	FILE_RAW    = 15,
	FILE_PDF    = 16,
   FILE_JPIP   = 17,    // From JPIP server; may be NITF, GEOTIFF, JP2 or ?? derived
   FILE_GDAL_TIF = 18,
   FILE_GDAL_JPIP = 19,
   FILE_RDTED	= 20,
   FILE_HRDTED = 21,
	FILE_PLUGIN = 22,
   FILE_GDAL_NITF = 23,
   FILE_GDAL_MISC = 100,
   FILE_OGR_MISC = 101
} file_type_t;


#endif  // ifndef DEFINES_H