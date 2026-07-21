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

// util.h

#pragma once

#include <string>
#ifndef _WIN32
#include <string>
typedef std::string _bstr_t;
#endif
#include "geotiff.h"
#include "defines.h"

#define UTIL_COLOR_BLACK		   0
#define UTIL_COLOR_DARK_RED	   1
#define UTIL_COLOR_DARK_GREEN		2
#define UTIL_COLOR_DARK_YELLOW	3
#define UTIL_COLOR_DARK_BLUE		4 
#define UTIL_COLOR_DARK_MAGENTA	5
#define UTIL_COLOR_DARK_CYAN		6
#define UTIL_COLOR_LIGHT_GRAY	   7
#define UTIL_COLOR_MONEY_GREEN	8
#define UTIL_COLOR_SKY_BLUE		9
#define UTIL_COLOR_CREAM		   10
#define UTIL_COLOR_MEDIUM_GRAY	11
#define UTIL_COLOR_DARK_GRAY		12
#define UTIL_COLOR_RED		      13
#define UTIL_COLOR_GREEN		   14
#define UTIL_COLOR_YELLOW	      15
#define UTIL_COLOR_BLUE		      16
#define UTIL_COLOR_MAGENTA	      17
#define UTIL_COLOR_CYAN		      18
#define UTIL_COLOR_WHITE		   19

// fill types
#define UTIL_FILL_NONE			0
#define UTIL_FILL_HORZ			1
#define UTIL_FILL_VERT			2
#define UTIL_FILL_BDIAG			3
#define UTIL_FILL_FDIAG			4
#define UTIL_FILL_CROSS			5
#define UTIL_FILL_DIAGCROSS	6
#define UTIL_FILL_SOLID			7
#define UTIL_FILL_SHADE			8

// anchor postion type
#define UTIL_UPSIDE_DOWN           0
#define UTIL_ANCHOR_LOWER_LEFT     1
#define UTIL_ANCHOR_UPPER_LEFT     2
#define UTIL_ANCHOR_LOWER_CENTER   3
#define UTIL_ANCHOR_UPPER_CENTER   5
#define UTIL_ANCHOR_LOWER_RIGHT    6
#define UTIL_ANCHOR_UPPER_RIGHT    8
#define UTIL_ANCHOR_CENTER_LEFT   10
#define UTIL_ANCHOR_CENTER_RIGHT  11
#define UTIL_ANCHOR_CENTER_CENTER 12

// font attributes
#define UTIL_FONT_BOLD       1
#define UTIL_FONT_ITALIC     2
#define UTIL_FONT_UNDERLINE  4
#define UTIL_FONT_STRIKEOUT  8

// text background type
#define UTIL_BG_NONE         0
#define UTIL_BG_SHADOW       1
#define UTIL_BG_RECT         2
#define UTIL_BG_3D           3

// font names
#define UTIL_FONT_NAME_ARIAL			"Arial"
#define UTIL_FONT_NAME_ARIAL_BLACK	"Arial Black"
#define UTIL_FONT_NAME_COURIER		"Courier New"
#define UTIL_FONT_NAME_TIMES			"Times"

#define UTIL_FIL_READ_OK   4
#define UTIL_FIL_WRITE_OK  2
#define UTIL_FIL_EXISTS    0
#define UTIL_FIL_END_OF_FILE 99

#define DEG_TO_RAD(degrees)       (((double)(degrees)) * 1.7453292519943295e-2)



class CUtil
{
public:

	CUtil();

	CString m_error_message;

	int limit(int num, int min, int max);
	int limit_to_short(int num);
	// this functions computes the scaling factor to use when the actual bits-per-pixel is less that the storage bpp
	int compute_luminance_scale(int bpp, int abpp, double *scale);

	COLORREF code2color(int code);
	void line(CDC *dc, int x1, int y1, int x2, int y2, int width, int color);
	void ellipse(CDC *dc, int x1, int y1, int x2, int y2, int width, int color);
	void move_to(CDC* dc, int x, int y);
	void line_to(CDC* dc, int x, int y);
	void rectangle(CDC* dc, int x1, int y1, int x2, int y2);
	void rectangle(CDC* dc, LPCRECT rc);
	CString add_carets(CString num);
	CString get_default_source();
	CString get_fif_dir();

	CString get_default_destination();

	static BOOL m_abort_flag;
   static CString s_csDefaultImageryDataPath;
   static CString s_csDefaultOverviewPath;

	CString get_registry_string(CString value_name, CString default_value);
	BOOL set_registry_string(CString value_name, CString value);
	int write_registry(HKEY root_key, const char* sub_key, 
										const char* value_name, DWORD type, const BYTE* storage_loc, 
										DWORD storage_size);
	static INT read_registry( HKEY root_key, LPCSTR sub_key, LPCSTR value_name,
										 PDWORD pdwType, PBYTE storage_loc, PDWORD pdwStorageSize );

	int max_malloc();
	void draw_text(
					CDC *dc,            // pointer to DC to draw in
					CString text,       // text to draw
					int x, int y,       // screen x,y position
					int anchor_pos,     // reference position of text
					CString font,       // font name
					int font_size,      // font size in points
					int font_attrib,    // font attributes (bold, italic, etc)
					int background,     // background type
					int text_color,     // code for text color
					int back_color,     // code for background color
					double angle,       // angle of text
					POINT *cpt,         // 4 point array defining the text polygon corners
					BOOL pad_spaces     // default is TRUE
				);
	UINT anchor2textalign(int anchor);
	void compute_text_poly(	int tx, int ty,			// x/y of anchor point
								int anchor_pos,			// position of anchor point
                                int width, int height,	// height and width of text
								double text_angle,		// angle of text
                                POINT *cpt);					// OUT - corners of rectangle enclosing text
	int round(double val);
	int magnitude(int x1, int y1, int x2, int y2);
	BOOL geo_east_of(double a, double b);
	void convert_24_to_8_bit(BYTE *buf,   // 3 byte RGB value
						BYTE *index, // color table
						BYTE *color); // OUT - color index value

	static CString extract_path( const CString& csFilespec );
	static CString extract_filename( const CString& csFilespec );
	CString extract_extension(CString fullname);
	BOOL create_directory(const CString& dirname);
	double get_free_space(CString path);
	CString get_temp_path(); 
	CString get_data_path();
#if 0    // Don't want to create generic temp filenames
	CString get_temp_jpeg_name(); 
	CString get_temp_tga_name(); 
	int get_temp_jpeg_size(int *jpeg_size, CString & error_msg);
#endif

	BOOL is_valid_geo(double lat, double lon);
	void draw_shade_regn(CDC *dc, CRgn *rgn, COLORREF color);
	int create_pattern_brush_rgb(int *pattern, CBrush &or_brush, CBrush &and_brush);
	void rotate_pt(int oldx, int oldy,     // original point
                     int *newx, int *newy,   // new point
                     double ang,             // angle in degrees
                     int ctrx, int ctry);     // center point
	static INT create_directory( const CString& csDirName, CString& csErrorMsg );

	static INT file_access( LPCSTR path, int mode );

	double geo_width(double lon1, double lon2);
	int remap_image(int dst_width, int dst_height, BYTE *dst_img, int src_width, int src_height, BYTE *src_img);
	int remap_image(double dst_ul_lat, double dst_ul_lon, double dst_lr_lat, double dst_lr_lon,
				   int dst_width, int dst_height, BYTE *dst_img, 
				   double src_ul_lat, double src_ul_lon, double src_lr_lat, double src_lr_lon,
				   int src_width, int src_height, BYTE *src_img);

	int remap_image_interp(int dst_width, int dst_height, BYTE *dst_img, int src_width, int src_height, BYTE *src_img);


	int geo_to_distance (double pt1Lat, double pt1Lon, 
						   double pt2Lat, double pt2Lon,
						   double far *mag,  // distance in meters
						   double far *dir);  // bearing

	BOOL user_agrees_to_abort(IImageLibCallback *callback);
	BOOL escape_pressed(IImageLibCallback *callback);
	BOOL send_user_update_and_continue( IImageLibCallback *callback, double percent, const CString& label );
	BOOL send_user_update_and_continue( IImageLibCallback *callback, double percent, LPCSTR pszLabel );
	void clear_callback(IImageLibCallback *callback);

	static INT calc_overview_name( const CString& csImageFilespec, CString& csOverviewFilespec, CString& csErrorMsg );
	static INT calc_overview_write_name( const CString& csImageFilespec, CString& csOverviewFilespec, CString& csErrorMsg );
	VOID calc_overview_temp_base_file( const CString& csImageFilespec, CString& csTempBaseFilespec );
   static INT get_default_imagery_data_paths( CString& csErrorMsg );

	int get_file_size(CString filename, unsigned int *file_size, CString & error_msg);
	int get_file_date(CString filename, CString & date, CString & error_msg);

	int calc_file_1m_sum(CString filename, int *sum, CString & error_msg);

	void fix_lon(double * lon);

	static void set_abort_flag(BOOL abort);
	int median_cut( unsigned int *histogram, int num_colors, unsigned char *red,
                unsigned char *green, unsigned char *blue,
                unsigned char *histogram_indices );
	BOOL file_exists( CString filename );
	BOOL is_multifile( CString& filename, CStringArray& list );
	BOOL fix_multifile_name(CString & filename);
   BOOL GetRGBFormulaeFromXML( const _bstr_t& bstrDisplayParamsXML,
                     CString& csFormulaName, CString& csRedFormula,
                     CString& csGreenFormula, CString& csBlueFormula );
   VOID SetRGBFormulaeInXML( _bstr_t& bstrDisplayParamsXML,
                     const CString& csFormulaName, const CString& csRedFormula,
                     const CString& csGreenFormula, const CString& csBlueFormula );
   BOOL GetScaleAndOffsetFromXML( const _bstr_t& bstrDisplayParamsXML,
                     DOUBLE& dPixelScale, UINT& uiPixelOffset );
   VOID SetScaleAndOffsetInXML( _bstr_t& bstrDisplayParamsXML,
                     DOUBLE dPixelScale, UINT uiPixelOffset );
   static _bstr_t T2WFS( LPCTSTR pszFilespec ); // Return wide unicode in \\?\ format

	// find the bounding box from corner coords
	int get_geo_bounds(double lat1, double lon1, double lat2, double lon2, 
						double lat3, double lon3, double lat4, double lon4,
						double *ullat, double *ullon, double *lrlat, double *lrlon);

	BOOL georect_intersect(double ullat1, double ullon1, double lrlat1, double lrlon1,
							double ullat2, double ullon2, double lrlat2, double lrlon2);

	double calc_dot_prod(float Left, float Right, float Up, 
							float meters_lat_per_pixel, float meters_lon_per_pixel,
							float light_dir_x, float light_dir_y, float light_dir_z,
							float exaggeration_factor);
	file_type_t determine_file_type(CString filename);

	int read_file(HANDLE & file, void * buf, int bytes_to_read);
	int read_string(HANDLE & file, char * buf, int & bytes_to_read);



private:
   static INT get_overview_write_path( const CString& csImageFilespec, CString& csOverviewPath, 
                             BOOL& bDefaultPath, CString& csErrorMsg );

	static VOID AddDecoratedOverviewBaseName( const CString& csImageFilespec,
      CString& csDecoratedBaseFilespec, BOOL bDoDecoration );

#ifdef _WIN32
   BOOL InitDisplayParamsXML( const _bstr_t& bstrDisplayParamsXML );

   MSXML2::IXMLDOMNodePtr SetParamInXML( MSXML2::IXMLDOMNodePtr pxnParent,
                     const BSTR bsParamName, const CString& csParamText );

   MSXML2::IXMLDOMDocument2Ptr   m_pxdDisplayParamsXML;
   MSXML2::IXMLDOMNodePtr        m_pxnDisplayParamsRoot;
#endif  // _WIN32 (MSXML display params)

   DWORD                         m_dwLastCallbackTime;
}; // CUtil

// End of Util.h

