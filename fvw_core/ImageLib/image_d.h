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

// image_d.h



#pragma once

#include "MetadataStructs.h"
#include "transfrm.h"
#include "fv_autoptr.h"  // fvw_auto_ptr (CImagePtr; std::auto_ptr removed in C++17)
#ifdef _WIN32
#include <afxtempl.h>  // for CList
#else
#include "fv_mfc_containers.h"
// COM types appear only as pass-through pointers off Windows
struct IImageLibCallback;
typedef void* IStreamPtr;
#endif

#define IMAGE_TYPE_MONO			1	// black is zero
#define IMAGE_TYPE_MONO_INV		2	// white is zero
#define IMAGE_TYPE_PALETTED		3	// uses lut
#define IMAGE_TYPE_RGB			4
#define IMAGE_TYPE_CMYK			5

#if 0 // The following defines have been moved to EnumImageFormatCode in NITFDBServer.idl
#define IMAGE_FORMAT_BMP		1
#define IMAGE_FORMAT_JPEG		2
#define IMAGE_FORMAT_JPEG2		3	// jpeg 2000
#define IMAGE_FORMAT_TIF		4
#define IMAGE_FORMAT_MRSID		5
#define IMAGE_FORMAT_PNG		6
#define IMAGE_FORMAT_GIF		7
#define IMAGE_FORMAT_NITF		8
#define IMAGE_FORMAT_TGA		9
#define IMAGE_FORMAT_LSAT		10
#define IMAGE_FORMAT_SPOT		11
#define IMAGE_FORMAT_ADRG		12
#define IMAGE_FORMAT_RAW		13  // image data only, requires input by user for width, height, bpp, etc.
#endif

#define IMAGE_COMPRESS_NONE			1
#define IMAGE_COMPRESS_PACKBITS		2
#define IMAGE_COMPRESS_RLE			3
#define IMAGE_COMPRESS_LZW			4
#define IMAGE_COMPRESS_JPEG			5
#define IMAGE_COMPRESS_CCITT_1D		6
#define IMAGE_COMPRESS_GROUP_3_FAX	7
#define IMAGE_COMPRESS_GROUP_4_FAX	8
#define IMAGE_COMPRESS_ZIP			9
#define IMAGE_COMPRESS_VQ			10
#define IMAGE_COMPRESS_JPEG2000		11

// image error codes
#define IMAGE_ERROR_NONE					0
#define IMAGE_ERROR_FILE_NOT_FOUND			1
#define IMAGE_ERROR_FILE_READ_ERROR			2
#define IMAGE_ERROR_FILE_WRITE_ERROR		3
#define IMAGE_ERROR_FILE_OPEN_ERROR			4
#define IMAGE_ERROR_INVALID_FILE_FORMAT		5
#define IMAGE_ERROR_IMAGE_NOT_LOADED		6
#define IMAGE_ERROR_MEMORY_ALLOCATION		7
#define IMAGE_ERROR_IMAGE_LOAD_FAILED		8
#define IMAGE_ERROR_SUBROUTINE_ERROR		9
#define IMAGE_ERROR_MISC_EXCEPTION_ERROR	10
#define IMAGE_ERROR_USER_ABORT = IMAGELIB_ERROR_USER_ABORT;

// NITF specific defines
// defines for image parameters

//#define UNKNOWN		0

#define NITF_VER_11		1
#define NITF_VER_20		2
#define NITF_VER_21		3

#define NITF_IMAGE_REP_MONO		1
#define NITF_IMAGE_REP_RGB		2
#define NITF_IMAGE_REP_PALETTE	3
#define NITF_IMAGE_REP_1D		4
#define NITF_IMAGE_REP_2D		5
#define NITF_IMAGE_REP_ND		6
#define NITF_IMAGE_REP_MULTI		7
#define NITF_IMAGE_REP_YCBCR		8

#define NITF_IMAGE_COORD_UNK		1
#define NITF_IMAGE_COORD_NONE	1
#define NITF_IMAGE_COORD_GEO		2
#define NITF_IMAGE_COORD_UTM_N	3
#define NITF_IMAGE_COORD_UTM_S	4
#define NITF_IMAGE_COORD_DD		5

#define NITF_IMAGE_COMPRESS_NONE		1
#define NITF_IMAGE_COMPRESS_BILEV		2
#define NITF_IMAGE_COMPRESS_VQ			3
#define NITF_IMAGE_COMPRESS_JPEG		4
#define NITF_IMAGE_COMPRESS_LLJPEG		5
#define NITF_IMAGE_COMPRESS_DSJPEG		6
#define NITF_IMAGE_COMPRESS_M_BILEV		7
#define NITF_IMAGE_COMPRESS_M_VQ		8
#define NITF_IMAGE_COMPRESS_M_JPEG		9
#define NITF_IMAGE_COMPRESS_M_LLJPEG	10
#define NITF_IMAGE_COMPRESS_M_TRANS		11
#define NITF_IMAGE_COMPRESS_J2K	    	12
 
#define NITF_IMAGE_MODE_BLOCK	1
#define NITF_IMAGE_MODE_PIXEL	2
#define NITF_IMAGE_MODE_ROW		3
#define NITF_IMAGE_MODE_SEQ		4

#define NITF_IMAGE_PIXEL_VALUE_TYPE_INT		1
#define NITF_IMAGE_PIXEL_VALUE_TYPE_BILEV	2
#define NITF_IMAGE_PIXEL_VALUE_TYPE_2COMP	3
#define NITF_IMAGE_PIXEL_VALUE_TYPE_REAL		4
#define NITF_IMAGE_PIXEL_VALUE_TYPE_COMPLEX	5

#define NITF_IMAGE_GEO_TYPE_LAT_LONG	1
#define NITF_IMAGE_GEO_TYPE_MGRS		2
#define NITF_IMAGE_GEO_TYPE_UTM_N		3
#define NITF_IMAGE_GEO_TYPE_UTM_S		4

// error_codes
#define NITF_ERROR_NO_IMAGE               IMAGELIB_ERROR_NO_IMAGE
#define NITF_ERROR_UNSUPPORTED_IMAGE_TYPE	IMAGELIB_ERROR_UNSUPPORTED_IMAGE_TYPE
#define NITF_ERROR_FILE_ERROR				   IMAGELIB_ERROR_FILE_ERROR

// scale defines
#define WORLD 100000
#define ONE_TO_80M 80000
#define ONE_TO_40M 40000
#define ONE_TO_20M 20000
#define ONE_TO_10M 10000 
#define ONE_TO_5M 5000
#define ONE_TO_4M 4000
#define ONE_TO_2M 2000
#define ONE_TO_1M 1000           
#define ONE_TO_800K 800
#define ONE_TO_500K 500
#define ONE_TO_400K 400
#define ONE_TO_250K 250
#define ONE_TO_200K 200 
#define ONE_TO_100K 100 
#define ONE_TO_80K 80
#define ONE_TO_50K 50
#define ONE_TO_40K 40 
#define ONE_TO_63360 63
#define ONE_TO_30K 30
#define ONE_TO_25K 25
#define ONE_TO_24K 24
#define ONE_TO_20K 20
#define ONE_TO_10K 10
#define ONE_TO_8K 8
#define ONE_TO_5K 5 
#define ONE_TO_4K 4
#define ONE_TO_2K 2
#define ONE_TO_1K 1
#define NULL_SCALE 0

// ****************************************************************
// ****************************************************************

class CGcp
{
public:
	CGcp()
	{
		m_image_x = 0;
		m_image_y = 0;
		m_lat = 0.0;
		m_lon = 0.0;
		m_error_val = 0.0;
		m_checked = TRUE;
		m_selected = FALSE;
		m_computed = FALSE;
		m_num = 0;
	}

	int m_image_x;
	int m_image_y;
	double m_lat;
	double m_lon;
	CString m_location;
	double m_error_val;
	BOOL m_checked;
	BOOL m_selected;
	BOOL m_computed;
	int m_num;
};



// ****************************************************************
// ****************************************************************

class C_band
{
public:
	C_band();
	~C_band();

	int m_image_width;
	int m_image_height;
	int m_bpp;			// storage bits per pixel
	int m_abpp;			// actual bits per pixel (e.g. 11 of 16 bits actually used)
	BOOL m_8bit;
	CString m_label;	// text describing this band
#if 0    // Not currently referenced
	PBYTE m_img8bit;		// image data 
#endif
	PUSHORT m_img16bit;  // image data 
	PBYTE m_hist;		// histogram 
	double m_pct_red;	// percent of the pixmap to be assigned to red
	double m_pct_grn;	// percent of the pixmap to be assigned to green
	double m_pct_blu;	// percent of the pixmap to be assigned to blue
};

class CBandsPtr
{
public:
   CBandsPtr( INT cBands )
      : m_cBands( cBands ), m_pBands( new C_band[ cBands ] ) {}
   CBandsPtr( INT cBands, INT iWidth, INT iHeight )
      : m_cBands( cBands ), m_pBands( new C_band[ cBands ] )
   {
      if ( m_pBands != NULL )
      {
         for ( INT k = 0; k < cBands; k++ )
         {
            m_pBands[ k ].m_image_width = iWidth;
            m_pBands[ k ].m_image_height = iHeight;
            m_pBands[ k ].m_img16bit = new USHORT[ iWidth * iHeight ];
         }
      }
   }
   ~CBandsPtr()
   {
      delete [] m_pBands;
   }
   operator C_band*()
   {
      return m_pBands;
   }
   BOOL Ready()
   {
      if ( m_cBands <= 0 || m_pBands == NULL )
         return FALSE;

      for ( INT k = 0; k < m_cBands; k++ )
      {
         if ( m_pBands[ k ].m_image_width == 0
               || m_pBands[ k ].m_image_height == 0
               || m_pBands[ k ].m_img16bit == NULL )
            return FALSE;
      }
      return TRUE;
   }
private:
   C_band* m_pBands;
   INT m_cBands;
};


// ****************************************************************
// ****************************************************************

class C_group_item
{
public:
	C_group_item();

	~C_group_item();

	int xy2geo(int x, int y, double *lat, double *lon);
	int geo2xy(double lat, double lon, int *x, int *y);

	// file reference - there will be one of these but not both
	IStreamPtr m_pIStream;     // Smart pointer will keep a reference
	CString m_geoinfo;

	CTransform m_transform;

	CString m_filename;

	int m_width;
	int m_height;
	double m_ul_lat;
	double m_ul_lon;
	double m_lr_lat;
	double m_lr_lon;
};


// ****************************************************************
// ****************************************************************

// base image class

class C_image
{
public:
	C_image();
	~C_image();

   INT GetHistogram( PUINT puiHistogram, PUINT puiFRed, PUINT puiFGreen, PUINT puiFBlue );


	BOOL m_initialized;
	int m_color_type;			// whether mono, rgb, etc.
	int m_image_width;			// width of image
	int m_image_height;			// height of image
   BytePtr m_apbImageData;
	int m_bpp;					// storage bits per pixel
	int m_abpp;					// actual bits per pixel (e.g. 11 of 16 bits actually used)
	BYTE *m_lut;				// look up table for paletted images stored in RGB format
	C_band *m_band;				// image data in bands
	int m_num_bands;
	int m_target_width;
	int m_target_height;
   INT m_lut_cnt;
	int m_row_bytes;			// number of bytes in one uncompressed image row
	int m_err_code;				// numerical code for error that may have occurred in process
	CString m_err_msg;			// text explaining error
	BOOL m_is_color;			// is the image a color image
	BOOL m_is_tiled;			// is the image tiled
	int m_tile_size_x;			// tile width
	int m_tile_size_y;			// tile height
	int m_tile_cnt_x;			// number of horizontal tiles
	int m_tile_cnt_y;			// number of vertical tiles
	PULONGLONG m_tile_index;   // location of tiles from beginning of file
	int m_num_tile_index;		// number of tile indexs;
	BOOL m_has_transparency;	// whether the image has transparent pixels
	BYTE m_trans_pixel_code[3]; // transparent color, for paletted images first byte is use, else RGB
	double m_pixel_size;		// pixel size in meters
	unsigned int m_trans_pix_code;   // Transparent Output pixel code 
	unsigned int m_trans_pix_code_len;   // Transparent Output pixel code length
	unsigned int *m_histogram;	// size = 32768
	unsigned int *m_freq_red;  // size = 256
	unsigned int *m_freq_grn;  // size = 256
	unsigned int *m_freq_blu;  // size = 256
   union
   {
      DOUBLE m_dCornerLats[ 4 ];
      struct
      {
         double m_ul_lat;
         double m_ur_lat;
         double m_lr_lat;
         double m_ll_lat;
      };
   };
   union
   {
      DOUBLE m_dCornerLons[ 4 ];
      struct
      {
         double m_ul_lon;
         double m_ur_lon;
         double m_lr_lon;
         double m_ll_lon;
      };
   };
	int m_old_width;
	int m_old_height;
	double m_old_ul_lat;
	double m_old_ul_lon;
	double m_old_lr_lat;
	double m_old_lr_lon;
	int m_old_startx;
	int m_old_starty;
	double m_old_factor;
	BYTE *m_buf_red;
	BYTE *m_buf_grn;
	BYTE *m_buf_blu;
	int m_buf_start_row;
	int m_buf_end_row;
	int m_img_offset_x;
	int m_img_offset_y;

	BOOL m_paletted;
	BYTE m_pal_red[256];
	BYTE m_pal_grn[256];
	BYTE m_pal_blu[256];
	BYTE *m_pal_img;
	BOOL m_image_type_supported;
	BOOL m_georeferenced;
	BOOL m_globally_georeferenced;
	int m_image_type;
	CString m_image_type_description;
	BOOL m_geodata_present;
	BOOL m_geodata_supported;
#ifdef _DEBUG
	// tiepoint vars
	double m_min_latitude, m_max_latitude, m_delta_latitude;
	double m_min_longitude, m_max_longitude, m_delta_longitude;
#endif
	CList<CGcp*, CGcp*> m_gcp_list;
//	int m_gcp_num;
   CTransform m_cttGeoTransform;
}; // End of class C_Image

#ifndef NO_AUTO_M_IMAGE
   // NOTE (port 2026-07-15): auto_ptr removed in C++17. CImagePtr is a member
   // of the (not-yet-ported) CImage class and relies on transfer-on-copy, so
   // it maps to fvw_auto_ptr rather than unique_ptr. See fv_autoptr.h.
   typedef fvw_auto_ptr< C_image > CImagePtr;
#endif

// ****************************************************************
// ****************************************************************

class C_nitf_image : public C_image
{
public:
	C_nitf_image();
	~C_nitf_image();

	int m_num_colors;
	CString m_irepband;
	CString m_subcat;
	int m_resolution;
	long m_err_num;
	int m_compress;
	ULONGLONG m_image_start;
	ULONGLONG m_image_len;
	CString m_pix_just;  // L or R
	CString m_comrat;
	int m_image_rep;
	int m_image_cat;
	int m_image_mode;
	int m_pixel_type;
	int m_image_offset_x;
	int m_image_offset_y;
	CString m_image_title;
	CString m_image_date;
	CString m_target_id;
	CString m_image_id;
	CString m_image_id2;
	CString m_image_class;  // classification of image T, S, C, R, or U
	CString m_image_codewords;
	CString m_image_source;
	CString m_image_comments;  // single string with "\n\r" between comments
	CString m_info;
	CString m_image_type_str;
	CString m_image_rep_str;
	CString m_image_cat_str;
	CString m_image_mode_str;
	CString m_compress_str;
	CString m_pixel_type_str;
	CString m_geo;
	BOOL m_has_geo;
	BOOL m_has_mask;
	int m_coord_type; // lat/long, MGRS, UTM (S or N), or blank
   CTREDataICHIPB m_tredICHIPB;     // Chipped image info
   CTREDataRPC00B m_tredRPC00B;     // Rapid Positioning Capability
   CTREDataCSCRNA m_tredCSCRNA;     // Corner lat/lon/elev
   CTREDataACCPOB m_tredACCPOB;     
   CTREDataSOURCB m_tredSOURCB;
   CTREDataGEOPSB m_tredGEOPSB;
	CString m_ul_loc;  // UTM, MGRS, or LAT/LON
	CString m_ur_loc;  // UTM, MGRS, or LAT/LON
	CString m_ll_loc;  // UTM, MGRS, or LAT/LON
	CString m_lr_loc;  // UTM, MGRS, or LAT/LON
	ULONGLONG      m_blocked_image_data_offset;	// Blocked Image Data Offset
	UINT           m_block_mask_rec_len;;        // Block Mask Record Length
	UINT           m_trans_pix_rec_len;          // Transparent Pixel mask record length
	BYTE m_trans_pix_red;
	BYTE m_trans_pix_grn;
	BYTE m_trans_pix_blu;
	BYTE m_back_pix_red;
	BYTE m_back_pix_grn;
	BYTE m_back_pix_blu;
//	double m_bounds_ul_lat;
//	double m_bounds_ul_lon;
//	double m_bounds_lr_lat;
//	double m_bounds_lr_lon;
//	CString m_info;
//	CString m_img_type_str;
//	CString m_img_rep_str;
//	CString m_img_cat_str;
//	CString m_img_mode_str;
//	CString m_compress_str;
	int m_vblock_cnt;
	int m_hblock_cnt;
	int m_block_pix_wide;
	int m_block_pix_high;
//	unsigned int   m_blocked_image_data_offset;	// Blocked Image Data Offset
//	unsigned int m_block_mask_rec_len;;   // Block Mask Record Length
//	unsigned int m_trans_pix_rec_len;   // Transparent Pixel mask record length
//	unsigned int m_trans_pix_code_len;   // Transparent Output pixel code length
//	unsigned int   m_trans_pix_code;	// Transparent Output pixel code

	BYTE *m_image_data;
	CString m_file_class;
	CString m_file_codeword;
	BYTE *m_buf_red;
	BYTE *m_buf_grn;
	BYTE *m_buf_blu;
	int m_buf_start_row;
	int m_buf_end_row;

	BOOL m_has_tile_mask;  // m_tile_mask_cnt is > 0
	int m_tile_mask_cnt;
	int *m_tile_mask;  // array of file offsets for each tile (-1 for no tile)
};



class C_bitmap
{
public:
	C_bitmap();
	~C_bitmap();

	int m_width;
	int m_height;
	int m_num_bands;
	unsigned short *m_img;

	int initialize(int width, int height, int numbands);
	int get_at(int x, int y, int bandnum, unsigned short *value);
	int set_at(int x, int y, int bandnum, unsigned short value);
	int fill(int bandnum, unsigned short value);
};

// End of image_d.h
