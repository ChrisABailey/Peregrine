// Copyright (c) 1994-2012 Georgia Tech Research Corporation, Atlanta, GA
// This file is part of FalconView(R).

// FalconView(R) is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// FalconView(R) is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public License
// along with FalconView(R).  If not, see <http://www.gnu.org/licenses/>.

// FalconView(R) is a registered trademark of Georgia Tech Research Corporation.

// util.cpp



#include "stdafx.h"
#include "util.h"
#include "common.h"
#include "geo_tool_d.h"
#include <string>
#include <math.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#ifdef _WIN32
#include "ComErrorObject.h"
#endif
#ifdef _WIN32
#include "NITFUtilities.h"
#include "JmpsRegistryConstants.h"
#endif

using namespace std;

//#include "refresh.h"    // for FVW_is_draw_interrupted()

#define IMAGE_SOURCE_PATH_TABLE L"ImageSourceRootPaths"
static const INT MIN_CALLBACK_INTERVAL = 500;      // 0.5 second intervals

BOOL CUtil::m_abort_flag = FALSE;
CString CUtil::s_csDefaultImageryDataPath = _T("");
CString CUtil::s_csDefaultOverviewPath = _T("");

CUtil::CUtil()
{
   m_dwLastCallbackTime = GetTickCount();    // To limit callback frequency
}


// ********************************************************************
// ********************************************************************

// restrict an integer to a range of values

int CUtil::limit(int num, int min, int max)
{
   int k;

   k = num;
   if (k < min)
      k = min;
   if (k > max)
      k = max;
   return k;
}
// end of limit

// ********************************************************************
// ********************************************************************

// restrict an integer to a range of values

int CUtil::limit_to_short(int num)
{
   int k;

   k = limit(num, -32000, 32000);
   return k;
}
// end of limit_to_short

// ********************************************************************
// ********************************************************************

// this functions computes the scaling factor to use when the actual bits-per-pixel is less that the storage bpp
int CUtil::compute_luminance_scale(int bpp, int abpp, double *scale)
{
   double max_value;

   // set default
   *scale = 1.0;

   // do some error checking
   if ((bpp < 1) || (abpp < 1))
      return FAILURE;

   if (abpp > bpp)
      return FAILURE;

   // quick return, use default
   if (bpp == abpp)
      return SUCCESS;

   max_value = pow(2.0, (double) abpp);

   *scale = 255.0 / max_value;

   return SUCCESS;
}

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // line: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
void CUtil::line(CDC *dc, int x1, int y1, int x2, int y2, int width, int color)
{
   CPen pen, *oldpen;
   int oldrop = R2_COPYPEN;

   if (color < 0)  // XOR
   {
      pen.CreatePen(PS_SOLID, width, code2color(UTIL_COLOR_WHITE));
      oldrop = dc->SetROP2(R2_XORPEN); 
   }
   else
   {
      pen.CreatePen(PS_SOLID, width, code2color(color));
   }
   oldpen = (CPen*) dc->SelectObject(&pen);
   dc->MoveTo(x1, y1);
   dc->LineTo(x2, y2);
   dc->SelectObject(oldpen);
   pen.DeleteObject(); 
   if (color < 0)
      dc->SetROP2(oldrop);
}
#endif  // _WIN32 (line)

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // ellipse: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
void CUtil::ellipse(CDC *dc, int x1, int y1, int x2, int y2, int width, int color)
{
   CPen pen, *oldpen;
   CBrush brush, *oldbrush;
   int oldrop = R2_COPYPEN;

   brush.CreateStockObject(NULL_BRUSH);
   oldbrush = (CBrush*) dc->SelectObject(&brush);

   if (color < 0)  // XOR
   {
      pen.CreatePen(PS_SOLID, width, code2color(UTIL_COLOR_WHITE));
      oldrop = dc->SetROP2(R2_XORPEN); 
   }
   else
   {
      pen.CreatePen(PS_SOLID, width, code2color(color));
   }
   oldpen = (CPen*) dc->SelectObject(&pen);
   dc->Ellipse(x1, y1, x2, y2);
   dc->SelectObject(oldpen);
   dc->SelectObject(oldbrush);
   pen.DeleteObject(); 
   brush.DeleteObject();
   if (color < 0)
      dc->SetROP2(oldrop);
}
#endif  // _WIN32 (ellipse)

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // move_to: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
void CUtil::move_to(CDC* dc, int x, int y)
{
   int tx, ty;

   tx = limit(x, -32000, 32000);
   ty = limit(y, -32000, 32000);
   dc->MoveTo(tx, ty);
}
#endif  // _WIN32 (move_to)
// end of move_to

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // line_to: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
void CUtil::line_to(CDC* dc, int x, int y)
{
   int tx, ty;

   tx = limit(x, -32000, 32000);
   ty = limit(y, -32000, 32000);
   dc->LineTo(tx, ty);
}
#endif  // _WIN32 (line_to)
// end of line_to

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // rectangle: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
void CUtil::rectangle(CDC* dc, int x1, int y1, int x2, int y2)
{
   int tx1, ty1, tx2, ty2;

   tx1 = limit_to_short(x1);
   ty1 = limit_to_short(y1);
   tx2 = limit_to_short(x2);
   ty2 = limit_to_short(y2);
   dc->Rectangle(tx1, ty1, tx2, ty2);
}
#endif  // _WIN32 (rectangle)
// end of rectangle

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // rectangle: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
void CUtil::rectangle(CDC* dc, LPCRECT rc)
{
   RECT trc;

   trc.top = limit_to_short(rc->top);
   trc.bottom = limit_to_short(rc->bottom);
   trc.left = limit_to_short(rc->left);
   trc.right = limit_to_short(rc->right);
   dc->Rectangle(&trc);
}
#endif  // _WIN32 (rectangle)
// end of rectangle

// ********************************************************************
// ********************************************************************

COLORREF CUtil::code2color(int code)
{
   switch(code)
   {
   case UTIL_COLOR_BLACK: return RGB(0,0,0); break;
   case UTIL_COLOR_DARK_RED: return RGB(128,   0,   0); break;
   case UTIL_COLOR_DARK_GREEN: return RGB(0, 128,   0); break;
   case UTIL_COLOR_DARK_YELLOW: return RGB(128, 128,   0); break;
   case UTIL_COLOR_DARK_BLUE: return RGB(0,   0, 128); break;
   case UTIL_COLOR_DARK_MAGENTA: return RGB(128,   0, 128); break;
   case UTIL_COLOR_DARK_CYAN: return RGB(0, 128, 128); break;
   case UTIL_COLOR_LIGHT_GRAY: return RGB(192, 192, 192); break;
   case UTIL_COLOR_MONEY_GREEN: return RGB(192, 220, 192); break;
   case UTIL_COLOR_SKY_BLUE: return RGB(166, 202, 240); break;
   case UTIL_COLOR_CREAM: return RGB(255, 251, 240); break;
   case UTIL_COLOR_MEDIUM_GRAY: return RGB(160, 160, 164); break;
   case UTIL_COLOR_DARK_GRAY: return RGB(128, 128, 128); break;
   case UTIL_COLOR_RED: return RGB(255,   0,   0); break;
   case UTIL_COLOR_GREEN: return RGB(0, 255,   0); break;
   case UTIL_COLOR_YELLOW: return RGB(255, 255,   0); break;
   case UTIL_COLOR_BLUE: return RGB(0,   0, 255); break;
   case UTIL_COLOR_MAGENTA: return RGB(255,   0, 255); break;
   case UTIL_COLOR_CYAN: return RGB(  0, 255, 255); break;
   case UTIL_COLOR_WHITE: return RGB(255, 255, 255); break;
   default: return RGB(255, 255, 255); break;
   }
}
// end of code2color

// ********************************************************************
// ********************************************************************
// add carets to a number string

CString CUtil::add_carets(CString num)
{
   int pos, len;
   CString tstr, whole, dec;

   tstr = num;
   tstr.TrimRight();
   tstr.TrimLeft();

   len = tstr.GetLength();
   dec = "";

   // see if there is a decimal
   pos = num.Find('.');
   if (pos > 0)
   {
      whole = tstr.Left(pos);
      if  (len > pos)
         dec = tstr.Right(len - pos);
      tstr = whole;
   }
   len = tstr.GetLength();
   if (len > 4)
   {
      if (len < 7)
      {
         whole = tstr.Left(len - 3);
         whole += ",";
         whole += tstr.Right(3);
      }
      else if (len < 10)
      {
         whole = tstr.Left(len - 6);
         whole += ",";
         whole += tstr.Mid(len - 6, 3);
         whole += ",";
         whole += tstr.Right(3);
      }
      else if (len < 13)
      {
         whole = tstr.Left(len - 9);
         whole += ",";
         whole += tstr.Mid(len - 9, 3);
         whole += ",";
         whole += tstr.Mid(len - 6, 3);
         whole += ",";
         whole += tstr.Right(3);
      }
      tstr = whole;
   }

   if (pos > 0)
      tstr += dec;

   return tstr;
}
// end of add_carets

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // get_default_source: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
CString CUtil::get_default_source()
{
   CString result;
   CString sub_key;
   unsigned char buffer[256];
   DWORD buffer_size = 256;
   DWORD type;

   sub_key = "Software\\" FVW_REG_PRODUCT "\\FALCONVIEW\\MAIN";


   if (read_registry(HKEY_LOCAL_MACHINE, sub_key, "HD_DATA", &type, 
      (unsigned char*) &buffer, &buffer_size) == SUCCESS)
   {
      //check type to see that it was a string
      if (type != REG_SZ)
      {
         m_error_message = "get_registry_string() returned non-string type.";
         result = "";
      }
      else
      {
         result = buffer;
         result += "\\GeoRect\\";
      }
   }
   else
      result = "c:\\Program Files\\" FVW_REG_PRODUCT "\\data\\georect\\";

   return result;
}
#endif  // _WIN32 (get_default_source)

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // get_fif_dir: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
CString CUtil::get_fif_dir()
{
   CString csResult;
   HANDLE hFind = INVALID_HANDLE_VALUE;

   do
   {
      CString csTemp;
      WIN32_FIND_DATA FindFileData; // Where file info gets put

      // Make sure standard paths are set up
      if ( SUCCESS == get_default_imagery_data_paths( csTemp ) )
      {
         csResult = s_csDefaultImageryDataPath + _T("\\fif\\");

         // See if there are any .fif files in the nitf\fif data subdirectory

         // Search for .fif files
         if ( INVALID_HANDLE_VALUE != ( hFind = FindFirstFileEx(
            csResult + _T("\\*.fif" ),    // Wildcard file name
            FindExInfoStandard,           // Information level
            &FindFileData,                // Information buffer
            FindExSearchNameMatch,        // Filtering type
            NULL,                         // Search criteria
            0 ) ) )                         // Additional search control
            break;   // Found one or more .fif files
      }

      // No .fif files in the default data directory.  Try the pfps location
      do
      {
         static const INT BUF_SIZE = 256;
         BYTE buffer[ BUF_SIZE ];
         DWORD buffer_size = BUF_SIZE;
         DWORD type;

         static const LPCSTR sub_key = "Software\\" FVW_REG_PRODUCT "\\FALCONVIEW\\MAIN";
         if ( read_registry( HKEY_LOCAL_MACHINE, sub_key, "USER_DATA", &type, 
            (PBYTE) &buffer, &buffer_size ) == SUCCESS )
         {
            // Check type to see that it was a string
            if ( type == REG_SZ)
            {
               csTemp = buffer;
               csTemp += "\\GeoRect\\fif\\";
               break;
            }
            m_error_message = "get_registry_string() returned non-string type.";
         }
         csTemp = "c:\\Program Files\\" FVW_REG_PRODUCT "\\data\\georect\\fif\\";
      } while ( FALSE );

      // Search for .fif files
      if ( INVALID_HANDLE_VALUE != ( hFind = FindFirstFileEx(
         csTemp + _T("\\*.fif" ),      // Wildcard file name
         FindExInfoStandard,           // Information level
         &FindFileData,                // Information buffer
         FindExSearchNameMatch,        // Filtering type
         NULL,                         // Search criteria
         0 ) ) )                       // Additional search control
         csResult = csTemp;               // If no pfps .fif files, return default data dir

   } while ( FALSE );
   if ( hFind != INVALID_HANDLE_VALUE )
      FindClose( hFind );
   return csResult;
}
#endif  // _WIN32 (get_fif_dir)

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // get_default_destination: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
CString CUtil::get_default_destination()
{
   CString result;
   CString sub_key;
   unsigned char buffer[256];
   DWORD buffer_size = 256;
   DWORD type;

   sub_key = "Software\\" FVW_REG_PRODUCT "\\FALCONVIEW\\MAIN";


   if (read_registry(HKEY_LOCAL_MACHINE, sub_key, "HD_DATA", &type, (unsigned char*) &buffer, &buffer_size) == SUCCESS)
   {
      //check type to see that it was a string
      if (type != REG_SZ)
      {
         m_error_message = "get_registry_string() returned non-string type.";
         result = "";
      }
      else
      {
         result = buffer;
         result += "\\GeoTiff";
      }
   }
   else
      result = "";

   return result;
}
#endif  // _WIN32 (get_default_destination)

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // get_registry_string: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
CString CUtil::get_registry_string(CString value_name, CString default_value)
{
   CString result;
   CString sub_key;
   unsigned char buffer[256];
   DWORD buffer_size = 256;
   DWORD type;

   sub_key = "Software\\" FVW_REG_PRODUCT "\\FalconView\\ImageLib";


   if (read_registry(HKEY_CURRENT_USER, sub_key, value_name, &type, (unsigned char*) &buffer, &buffer_size) == SUCCESS)
   {
      result = buffer;
      //check type to see that it was a string
      if (type != REG_SZ)
         m_error_message = "get_registry_string() returned non-string type.";
   }
   else
      result = default_value;

   return result;
}
#endif  // _WIN32 (get_registry_string)

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // set_registry_string: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
BOOL CUtil::set_registry_string(CString value_name, CString value)
{
   CString sub_key = "Software\\" FVW_REG_PRODUCT "\\FalconView\\ImageLib";
   const int BUF_LEN = 201;
   char buf[BUF_LEN];

   strcpy_s(buf, BUF_LEN, value);

   return write_registry(HKEY_CURRENT_USER, sub_key, value_name,
      REG_SZ, (const BYTE*) buf, (DWORD) strlen(value)+1); 
}
#endif  // _WIN32 (set_registry_string)


// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // write_registry: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
int CUtil::write_registry(HKEY root_key, const char* sub_key, 
   const char* value_name, DWORD type, const BYTE* storage_loc, 
   DWORD storage_size)
{

   HKEY key;
   DWORD dwDisposition;

   //open key
   if (RegOpenKeyEx(root_key, sub_key, 0, KEY_WRITE, &key)
      != ERROR_SUCCESS) 
   {
      if (RegCreateKeyEx(root_key, sub_key, 0, "", REG_OPTION_NON_VOLATILE,
         KEY_WRITE, NULL, &key, &dwDisposition) != ERROR_SUCCESS)
      {
         m_error_message = "RegCreateKeyEx failed.";
         return FAILURE;
      }
   }

   //set key
   if (RegSetValueEx(key, value_name, 0, type, storage_loc,
      storage_size) != ERROR_SUCCESS)
   {
      m_error_message = "RegSetValueEx failed.";
      return FAILURE;
   }

   //close key
   if (RegCloseKey(key) != ERROR_SUCCESS)
      m_error_message = "RegCloseKey failed.";
   return SUCCESS;
}   
#endif  // _WIN32 (write_registry)

// ********************************************************************
// ********************************************************************

// Note: failure may occur simply because the value_name does not exist
#ifdef _WIN32  // read_registry: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
int CUtil::read_registry(HKEY root_key, const char* sub_key, const char* value_name,
   DWORD* type, BYTE* storage_loc, DWORD* storage_size)
{   
   HKEY key;

   //open key
   if (RegOpenKeyExA(root_key, sub_key, 0, KEY_READ, &key) != 
      ERROR_SUCCESS)
   {
      //INFO_report("RegOpenKeyEx failed.");
      return FAILURE;
   }

   //query value  
   if (RegQueryValueExA(key, value_name, 0, type, storage_loc, 
      storage_size) != ERROR_SUCCESS)
   {
      RegCloseKey(key);
      //INFO_report("RegOpenKeyEx failed.");
      return FAILURE; 
   }

   //close key
   if ( RegCloseKey( key ) != ERROR_SUCCESS )
      WriteToLogFile( L"ImageLib::read_registry() - RegCloseKey failed." );

   return SUCCESS;   //success
}
#endif  // _WIN32 (read_registry)

// ********************************************************************
// ********************************************************************

int CUtil::max_malloc()
{
   BOOL notdone = TRUE;
   BYTE *tb;
   int msize = 1000000;
   while (notdone)
   {
      tb = (BYTE*) malloc(msize);
      if (tb == NULL)
      {
         notdone = FALSE;
         msize -= 1000000;
         continue;
      }
      if (msize > 10000000)
         notdone = FALSE;
      free(tb);
      msize += 1000000;
   }
   return msize;
}

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // draw_text: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
void CUtil::draw_text(
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
   )
{
   int oldmode, oldalign;
   COLORREF color, bkcolor, oldcolor, oldbkcolor;
   UINT align_type;
   int tx, ty;
   int width, height;
   CString drawtext;
   CFont cfont;

   // Allocate memory for a LOGFONT structure.
   PLOGFONT plf = (PLOGFONT) LocalAlloc(LPTR, sizeof(LOGFONT));
   //   PLOGFONT plf = (PLOGFONT) malloc(sizeof(LOGFONT));

   // Specify a font typeface name and weight.
   lstrcpyn(plf->lfFaceName, font.GetBuffer(sizeof(plf->lfFaceName)), LF_FACESIZE);
   plf->lfWeight = FW_NORMAL;

   oldmode = dc->SetBkMode(TRANSPARENT);

   plf->lfHeight = font_size;
   plf->lfWidth = 0;

   // set the font attributes
   if (font_attrib & UTIL_FONT_BOLD)
      plf->lfWeight = 800;
   else
      plf->lfWeight = 400;

   if (font_attrib & UTIL_FONT_ITALIC)
      plf->lfItalic = TRUE;
   else
      plf->lfItalic = FALSE;

   if (font_attrib & UTIL_FONT_UNDERLINE)
      plf->lfUnderline = TRUE;
   else
      plf->lfUnderline = FALSE;

   if (font_attrib & UTIL_FONT_STRIKEOUT)
      plf->lfStrikeOut = TRUE;
   else
      plf->lfStrikeOut = FALSE;

   //plf->lfQuality = PROOF_QUALITY;

   plf->lfEscapement = (long) (- (double) angle * 10.0);
   plf->lfOrientation = plf->lfEscapement;

   lstrcpyn(plf->lfFaceName, font.GetBuffer(sizeof(plf->lfFaceName)), LF_FACESIZE);
   if (!cfont.CreateFontIndirect(plf))
   {
      lstrcpyn(plf->lfFaceName, "Arial", LF_FACESIZE);
      if (!cfont.CreateFontIndirect(plf))
      {
         LocalFree((LOCALHANDLE) plf);
         return;
      }
   }

   CFont* cfntPrev = (CFont*) dc->SelectObject(&cfont);
   color = code2color(text_color);
   bkcolor = code2color(back_color);
   oldcolor = dc->SetTextColor(color);
   oldbkcolor = dc->SetBkColor(bkcolor);
   align_type = anchor2textalign(anchor_pos);
   CRect trc(x, y, x + 200, y + 75);

   oldalign = dc->SetTextAlign(align_type);
   dc->SetTextCharacterExtra(1);

   tx = x;
   ty = y;

   if ((background == UTIL_BG_RECT) || (background == UTIL_BG_RECT))
   {
      tx += 1;
   }

   if (pad_spaces)
   {
      // put spaces before and after the text
      drawtext = " ";
      drawtext += text;
      drawtext += " ";
   }
   else
      drawtext = text;

   CSize size = dc->GetTextExtent(drawtext);

   width = size.cx;
   height = size.cy;

   // compute the new anchor point if not a standard one
   if ((anchor_pos == UTIL_ANCHOR_CENTER_CENTER) ||
      (anchor_pos == UTIL_ANCHOR_CENTER_LEFT) ||
      (anchor_pos == UTIL_ANCHOR_CENTER_RIGHT))
   {
      double tang, newangle;
      int dx, dy;

      ty += height / 2;
      tang = (double) angle + 90.0;

      if (fabs(tang - 90.0) > 0.0001)
      {
         newangle = DEG_TO_RAD(tang - 90.0);
         // rotate the starting point about the origin
         dx = tx - x;
         dy = ty - y;
         tx = round((dx * cos(newangle)) - (dy * sin(newangle)) + x);
         ty = round((dy * cos(newangle)) + (dx * sin(newangle)) + y);
      }
   }

   compute_text_poly(x, y, anchor_pos,   width, height, angle, cpt);

   switch(background)
   {
   case UTIL_BG_RECT:
   case UTIL_BG_3D:
      if ((background == UTIL_BG_3D) && (angle == 0.0))
      {
         CBrush brush;
         CPen pen;
         brush.CreateSolidBrush(bkcolor);
         pen.CreatePen(PS_SOLID, 1, color);
         CPen* oldpen = (CPen*) dc->SelectObject(&pen);
         CBrush* oldbrush = (CBrush*) dc->SelectObject(&brush);
         dc->Polygon(cpt, 4);
         CPen lightpen, darkpen, blackpen;
         lightpen.CreatePen(PS_SOLID, 1, code2color(UTIL_COLOR_WHITE));
         darkpen.CreatePen(PS_SOLID, 1, code2color(UTIL_COLOR_MEDIUM_GRAY));
         blackpen.CreatePen(PS_SOLID, 1, code2color(UTIL_COLOR_BLACK));
         dc->SelectObject(&darkpen);
         dc->MoveTo(cpt[0].x, cpt[0].y);
         dc->LineTo(cpt[1].x, cpt[1].y);
         dc->LineTo(cpt[2].x, cpt[2].y);
         dc->LineTo(cpt[3].x, cpt[3].y);
         dc->LineTo(cpt[0].x, cpt[0].y);
         dc->SelectObject(&lightpen);
         dc->MoveTo(cpt[3].x - 1, cpt[3].y);
         dc->LineTo(cpt[0].x - 1, cpt[0].y - 1);
         dc->LineTo(cpt[1].x + 1, cpt[1].y  - 1);
         dc->SelectObject(&blackpen);
         dc->MoveTo(cpt[1].x+1, cpt[1].y  - 1);
         dc->LineTo(cpt[2].x+1, cpt[2].y+1);
         dc->LineTo(cpt[3].x - 2, cpt[3].y+1);
         dc->SelectObject(oldpen);
         dc->SelectObject(oldbrush);
         pen.DeleteObject();
         brush.DeleteObject();
      }
      else
      {
         CBrush brush;
         CPen pen;
         brush.CreateSolidBrush(bkcolor);
         pen.CreatePen(PS_SOLID, 1, color);
         CPen* oldpen = (CPen*) dc->SelectObject(&pen);
         CBrush* oldbrush = (CBrush*) dc->SelectObject(&brush);
         dc->Polygon(cpt, 4);
         dc->SelectObject(oldpen);
         dc->SelectObject(oldbrush);
         pen.DeleteObject();
         brush.DeleteObject();
      }
      break;
   case UTIL_BG_SHADOW:
      {
         int j, k;
         dc->SetTextColor(bkcolor);
         for (k=-1; k<=1; k++)
            for (j=-1; j<=1; j++)
               dc->TextOut(tx+j, ty+k, drawtext);
         dc->SetTextColor(color);
      }
   }

   dc->TextOut(tx, ty, drawtext);
   dc->SetTextAlign(oldalign);
   dc->SetTextColor(oldcolor);
   dc->SetBkColor(oldbkcolor);
   dc->SelectObject(cfntPrev);
   cfont.DeleteObject();

   // Reset the background mode to its default.
   dc->SetBkMode(oldmode);

   // Free the memory allocated for the LOGFONT structure.
   LocalFree((LOCALHANDLE) plf);
}
#endif  // _WIN32 (draw_text)
// end of draw_text

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // GDI text-alignment flags
UINT CUtil::anchor2textalign(int anchor)
{
   switch(anchor)
   {
   case UTIL_ANCHOR_LOWER_LEFT     :
      return TA_BOTTOM | TA_LEFT;
      break;
   case UTIL_ANCHOR_UPPER_LEFT     :
      return TA_TOP | TA_LEFT;
      break;
   case UTIL_ANCHOR_LOWER_CENTER   :
      return TA_BOTTOM | TA_CENTER;
      break;
   case UTIL_ANCHOR_UPPER_CENTER   :
      return TA_TOP | TA_CENTER;
      break;
   case UTIL_ANCHOR_LOWER_RIGHT    :
      return TA_BOTTOM | TA_RIGHT;
      break;
   case UTIL_ANCHOR_UPPER_RIGHT    :
      return TA_TOP | TA_RIGHT;
      break;
   case UTIL_ANCHOR_CENTER_LEFT    :
      return TA_BOTTOM | TA_LEFT;
      break;
   case UTIL_ANCHOR_CENTER_RIGHT    :
      return TA_BOTTOM | TA_RIGHT;
      break;
   case UTIL_ANCHOR_CENTER_CENTER   :
      return TA_BOTTOM | TA_CENTER;
      break;
   }
   return 0;
}
#endif  // _WIN32 (anchor2textalign)
// end of anchor2textalign

// ********************************************************************
// ********************************************************************

// compute the corners of a polygon (rotated rectangle) that encloses a block of text

void CUtil::compute_text_poly(int tx, int ty, // x/y of anchor point
   int anchor_pos,// position of anchor point
   int width, int height,// height and width of text
   double text_angle, // angle of text
   POINT *cpt)     // OUT - corners of rectangle enclosing text
{
   // define the bounding rectangle
   double angle, newangle, cosang, sinang;
   int k, dx, dy;
   int x, y, cx, cy;

   x = tx;
   y = ty;
   cx = width;
   cy = height;

   dx = cx / 2;
   dy = cy / 2;

   switch(anchor_pos)
   {
   case UTIL_ANCHOR_LOWER_LEFT     :
      cpt[0].x = tx;
      cpt[0].y = ty - cy;
      cpt[1].x = tx + cx;
      cpt[1].y = ty - cy;
      cpt[2].x = tx + cx;
      cpt[2].y = ty;
      cpt[3].x = tx;
      cpt[3].y = ty;
      break;
   case UTIL_ANCHOR_UPPER_LEFT     :
      cpt[0].x = tx;
      cpt[0].y = ty;
      cpt[1].x = tx + cx;
      cpt[1].y = ty;
      cpt[2].x = tx + cx;
      cpt[2].y = ty + cy;
      cpt[3].x = tx;
      cpt[3].y = ty + cy;
      break;
   case UTIL_ANCHOR_LOWER_CENTER   :
      cpt[0].x = tx - dx;
      cpt[0].y = ty - cy;
      cpt[1].x = tx + dx;
      cpt[1].y = ty - cy;
      cpt[2].x = tx + dx;
      cpt[2].y = ty;
      cpt[3].x = tx - dx;
      cpt[3].y = ty;
      break;
   case UTIL_ANCHOR_UPPER_CENTER   :
      cpt[0].x = tx - dx;
      cpt[0].y = ty;
      cpt[1].x = tx + dx;
      cpt[1].y = ty;
      cpt[2].x = tx + dx;
      cpt[2].y = ty + cy;
      cpt[3].x = tx - dx;
      cpt[3].y = ty + cy;
      break;
   case UTIL_ANCHOR_LOWER_RIGHT    :
      cpt[0].x = tx - cx;
      cpt[0].y = ty - cy;
      cpt[1].x = tx;
      cpt[1].y = ty - cy;
      cpt[2].x = tx;
      cpt[2].y = ty;
      cpt[3].x = tx - cx;
      cpt[3].y = ty;
      break;
   case UTIL_ANCHOR_UPPER_RIGHT    :
      cpt[0].x = tx - cx;
      cpt[0].y = ty;
      cpt[1].x = tx;
      cpt[1].y = ty;
      cpt[2].x = tx;
      cpt[2].y = ty + cy;
      cpt[3].x = tx - cx;
      cpt[3].y = ty + cy;
      break;
   case UTIL_ANCHOR_CENTER_LEFT    :
      cpt[0].x = tx;
      cpt[0].y = ty - dy;
      cpt[1].x = tx + cx;
      cpt[1].y = ty - dy;
      cpt[2].x = tx + cx;
      cpt[2].y = ty + dy;
      cpt[3].x = tx;
      cpt[3].y = ty + dy;
      break;
   case UTIL_ANCHOR_CENTER_RIGHT    :
      cpt[0].x = tx - cx;
      cpt[0].y = ty - dy;
      cpt[1].x = tx;
      cpt[1].y = ty - dy;
      cpt[2].x = tx;
      cpt[2].y = ty + dy;
      cpt[3].x = tx - cx;
      cpt[3].y = ty + dy;
      break;
   case UTIL_ANCHOR_CENTER_CENTER   :
      cpt[0].x = tx - dx;
      cpt[0].y = ty - dy;
      cpt[1].x = tx + dx;
      cpt[1].y = ty - dy;
      cpt[2].x = tx + dx;
      cpt[2].y = ty + dy;
      cpt[3].x = tx - dx;
      cpt[3].y = ty + dy;
      break;
   default:
      cpt[0].x = tx;
      cpt[0].y = ty;
      cpt[1].x = tx;
      cpt[1].y = ty;
      cpt[2].x = tx;
      cpt[2].y = ty;
      cpt[3].x = tx;
      cpt[3].y = ty;
      break;
   }


   angle = (double) text_angle + 90.0;

   //  rotate the string vector list
   if (fabs(angle - 90.0) > 0.0001)
   {
      newangle = DEG_TO_RAD(angle - 90.0);
      cosang = cos(newangle);
      sinang = sin(newangle);
      // rotate the bounding rectangle about the origin
      for (k=0; k< 4; k++)
      {
         dx = cpt[k].x - tx;
         dy = cpt[k].y - ty;
         cpt[k].x = round((dx * cosang) - (dy * sinang) + tx);
         cpt[k].y = round((dy * cosang) + (dx * sinang) + ty);
      }
   }
}
// end of compute_text_poly


// ********************************************************************
// ********************************************************************

// round to nearest integer
int CUtil::round(double val)
{
   return (val > 0.0 ? ((int) (val + 0.5)) : ((int) (val - 0.5)));
}

// ********************************************************************
// ********************************************************************

int CUtil::magnitude(int x1, int y1, int x2, int y2)
{
   double mag_sqr;

   mag_sqr = ((double)(x1 - x2) * (double)(x1 - x2)) +
      ((double)(y1 - y2) * (double)(y1 - y2));

   return (int)(sqrt(mag_sqr) + 0.5);
}
// end of magnitude

// ********************************************************************
// ********************************************************************

BOOL CUtil::geo_east_of(double a, double b)
{
   double diff;

   diff = a - b;

   if (diff < 0.0) /* convert diff to eastward distance from b to a */
      diff += 360.0;

   if (0.0 < diff && diff <= 180.0)
      return TRUE;

   return FALSE;
} 

// ********************************************************************
// ********************************************************************

void CUtil::convert_24_to_8_bit(BYTE *buf,   // 3 byte RGB value
   BYTE *index, // color table
   BYTE *color) // OUT - color index value
{
   int ti, val;

   *color = 0;

   ti = buf[0];
   ti = ti >> 3;
   val = ti << 10;
   ti = buf[1];
   ti = ti >> 3;
   val += ti << 5;
   ti = buf[2];
   ti = ti >> 3;
   val += ti;
   if (val < 32768)
      *color = index[val];
}


// ********************************************************************
// ********************************************************************

CString CUtil::extract_path( const CString& csFilespec )
{
   INT iPos;
   iPos = csFilespec.ReverseFind( '\\' );    // Last backslash
   return ( iPos >= 0 ) ? csFilespec.Left( iPos + 1 ) : _T("");
}

// ********************************************************************
// ********************************************************************

CString CUtil::extract_filename( const CString& csFilespec )
{
   INT iPos;
   iPos = csFilespec.ReverseFind('\\');
   return ( iPos >= 0 ) ? csFilespec.Mid( iPos + 1 ) : _T("");
}

// ********************************************************************
// ********************************************************************

CString CUtil::extract_extension(CString fullname)
{
   int len, pos;
   CString ext;

   ext = "";
   len = fullname.GetLength();
   pos = fullname.ReverseFind('.');
   if (pos >= 0)
      ext = fullname.Right(len-pos-1);
   return ext;
}

// ********************************************************************
// ************************************************************

#ifdef _WIN32  // create_directory: SECURITY_ATTRIBUTES
BOOL CUtil::create_directory(const CString& dirname)
{
   // check for existence of the directory, create is necessary
   if (_access(dirname, 0) == -1)
   {
      SECURITY_ATTRIBUTES security;

      security.nLength = sizeof(SECURITY_ATTRIBUTES);
      security.lpSecurityDescriptor = NULL;
      security.bInheritHandle = TRUE;

      if (!CreateDirectory(dirname, &security))
      {
         // try to create the subdirectory
         CString subdir;
         int k;
         k = dirname.ReverseFind('\\');
         if (k > 0)
         {
            subdir = dirname.Left(k);
            if (CreateDirectory(subdir, &security))
            {
               if (CreateDirectory(dirname, &security))
                  return TRUE;
            }
         }

         const CString msg = "Could not create directory -- " + dirname;
         AfxMessageBox(msg);
         return FALSE;
      }
   }
   return TRUE;
}
#endif  // _WIN32 (create_directory)

// ************************************************************
// ********************************************************************

#ifdef _WIN32  // Win32 file/disk APIs; POSIX in fv_imagelib_util_posix.cpp
double CUtil::get_free_space(CString path)
{
   double mega_bytes;
   double upper;
   ULARGE_INTEGER free_bytes, total_bytes, total_free_bytes;

   // get the free space on the drive
   GetDiskFreeSpaceEx(path, &free_bytes, &total_bytes, &total_free_bytes);

   upper = (double) free_bytes.HighPart;

   mega_bytes = (double) free_bytes.LowPart;
   mega_bytes = mega_bytes / (1024.0 * 1024.0);
   mega_bytes += upper * 4096.0;
   return mega_bytes;
}
#endif  // _WIN32

// ********************************************************************
// ****************************************************************

#ifdef _WIN32  // get_data_path: Win32 file APIs; POSIX version in fv_imagelib_util_posix.cpp
CString CUtil::get_data_path() 
{
   CString csTempPath, tpath;

   CString result;
   CString sub_key, value_name;
   unsigned char buffer[256];
   DWORD buffer_size = 256;
   DWORD type;

   sub_key = "Software\\" FVW_REG_PRODUCT "\\FalconView\\MAIN";
   value_name = "HD_DATA";

   if (read_registry(HKEY_LOCAL_MACHINE, sub_key, value_name, &type, (unsigned char*) &buffer, &buffer_size) == SUCCESS)
   {
      result = buffer;
      //check type to see that it was a string
      if (type != REG_SZ)
         return "C:\\Program Files\\" FVW_REG_PRODUCT "\\FalconView\\data\\";
   }

   tpath = buffer;

   if (tpath.GetLength() > 0)
   {
      if ( tpath[ tpath.GetLength() - 1 ] != _T('\\') )
         tpath += _T("\\");
      return tpath;
   }

   return "c:\\Program Files\\" FVW_REG_PRODUCT "\\FalconView\\data\\";
}
#endif  // _WIN32 (get_data_path)
// end of get_data_path

// ****************************************************************
// ****************************************************************

#ifdef _WIN32  // get_temp_path: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
CString CUtil::get_temp_path() 
{
   TCHAR tchBuf[ MAX_PATH + 1 ];
   CString csTempPath, tpath;

   if ( GetTempPath( MAX_PATH, tchBuf ) <= 0 )
   {
      m_error_message = "Cannot get temporary file path";
      return _T("");
   }

   csTempPath = tchBuf;
   tpath = get_registry_string( "TempPath", csTempPath );
   if (tpath.GetLength() > 0)
   {
      if ( tpath[ tpath.GetLength() - 1 ] != _T('\\') )
         tpath += _T("\\");
      return tpath;
   }

   return csTempPath;
}
#endif  // _WIN32 (get_temp_path)
// end of get_temp_path

// ****************************************************************
// ****************************************************************
#if 0    // Don't want to use generic temp file names

CString CUtil::get_temp_jpeg_name() 
{
   return get_temp_path() + _T("temp.jpg");
}

// ****************************************************************
// ****************************************************************

CString CUtil::get_temp_tga_name() 
{
   return get_temp_path() + _T("temp.tga");
}

// ****************************************************************
// ****************************************************************

// get the size of a section JPEG of the current input image

int CUtil::get_temp_jpeg_size(int *jpeg_size, CString & error_msg)
{
   return get_file_size( get_temp_jpeg_name(), (PUINT) jpeg_size, error_msg );
}
// End of CUtil::get_temp_jpeg_size

#endif

// ****************************************************************
// ****************************************************************

// get the date of a file

#ifdef _WIN32  // get_file_date: Win32 file APIs; POSIX version in fv_imagelib_util_posix.cpp
int CUtil::get_file_date(CString filename, CString & date, CString & error_msg)
{
   WIN32_FILE_ATTRIBUTE_DATA fad;
   if ( !GetFileAttributesExW( T2WFS( filename ), GetFileExInfoStandard, &fad ) )
   {
      m_error_message.Format( _T("Unable to get file date, error code = %u -- %s"),
         GetLastError(), filename );
      error_msg = m_error_message;
      return FAILURE;
   }

   SYSTEMTIME st;
   FileTimeToSystemTime( &fad.ftLastWriteTime, &st );
   date.Format( _T("%04d%02d%02d%02d%02d%02d"),
      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );

   return SUCCESS;
}
#endif  // _WIN32 (get_file_date)
// end of CUtil::get_file_date


// ****************************************************************
// ****************************************************************


// Get the size of a file

#ifdef _WIN32  // get_file_size: Win32 file APIs; POSIX version in fv_imagelib_util_posix.cpp
int CUtil::get_file_size(CString filename, unsigned int *file_size, CString & error_msg)
{
   WIN32_FILE_ATTRIBUTE_DATA fad;
   if ( !GetFileAttributesExW( T2WFS( filename ), GetFileExInfoStandard, &fad ) )
   {
      m_error_message.Format( _T("Unable to get file size, error code = %u -- %s"),
         GetLastError(), filename );
      error_msg = m_error_message;
      return FAILURE;
   }

   *file_size = (UINT) ( fad.nFileSizeHigh != 0 ? UINT_MAX : fad.nFileSizeLow );
   return SUCCESS;
}  // End of CUtil::get_file_size()
#endif  // _WIN32 (get_file_size)



// ****************************************************************
// ****************************************************************

int CUtil::calc_file_1m_sum(CString filename, int *sum, CString & error_msg)
{
   int sig, rslt, j, k;
   unsigned int filesize;
   FILE *fp = NULL;
   char buf[1001];

   rslt = get_file_size(filename, &filesize, error_msg);
   if (rslt != SUCCESS)
      return FAILURE;

   if (filesize < 1000000U)
      return FAILURE;

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   if ( 0 != fopen_s(&fp, filename, "rb")
      || fp == NULL )
      return FAILURE;

   sig = 0;
   for (k=0; k<1000; k++)
   {
      if ( fread(buf, 1000, 1, fp) < 1 )
         return FAILURE;
      for (j=0; j<1000; j++)
         sig += (int) buf[j];
   }
   fclose(fp);
   *sum = sig;
   return SUCCESS;
}
// end of calc_file_1m_sum

//
// T2WFS() - Make "\\?\" format filespec
//
#ifdef _WIN32  // Win32 path decoration / security attributes
_bstr_t CUtil::T2WFS( LPCTSTR pszFilespec )
{
   if ( pszFilespec[0] == _T('.') || 0 == _tcsncmp( pszFilespec, _T("\\\\?\\"), 4 ) )
      return pszFilespec;     // Relative or predecorated form, don't change

   // If starts with "\\", it's a UNC name
   if ( 0 == _tcsncmp( pszFilespec, _T("\\\\"), 2 ) )
      return _bstr_t( L"\\\\?\\UNC\\" ) + ( pszFilespec + 2 );

   // If not relative and not UNC, better be disk format
   ASSERT( pszFilespec[1] == _T(':') );
   return _bstr_t( L"\\\\?\\" ) + pszFilespec;

}
#endif  // _WIN32


// ****************************************************************
// ****************************************************************

BOOL CUtil::is_valid_geo(double lat, double lon)
{
   if ((lat < -90.0) || (lat > 90.0))
      return FALSE;
   if ((lon < -180.0) || (lon > 180.0))
      return FALSE;

   return TRUE;
}

// ****************************************************************
// *************************************************************

// draw a shaped region

#ifdef _WIN32  // draw_shade_regn: GDI region drawing
void CUtil::draw_shade_regn(CDC *dc, CRgn *rgn, COLORREF color)
{
   CRect tmp_rect, *rect;
   LPRGNDATA lpRgnData;

   // Select a brush with the given background color
   CBrush brush;
   brush.CreateSolidBrush(color);

   // dwCount is the size, in bytes, of the region data
   DWORD dwCount = rgn->GetRegionData(NULL,0);

   BYTE *pData = new BYTE[dwCount];

   memset(pData, 0, dwCount);
   lpRgnData = reinterpret_cast<LPRGNDATA>(pData);
   lpRgnData->rdh.dwSize = sizeof(lpRgnData->rdh);
   DWORD dwResult = rgn->GetRegionData(lpRgnData, dwCount);

   if (dwResult == 0)
      return;

   // loop through the rectangles making up the region
   char *buffer_ptr = lpRgnData->Buffer;
   for(int rindex = 0; rindex < (int)lpRgnData->rdh.nCount; rindex++)
   {
      int x,y;

      // get the next CRect in the buffer
      rect = (CRect *)buffer_ptr;
      x = rect->TopLeft().x;
      y = rect->TopLeft().y;

      // loop through this rectangle from top left to bottom right
      for(int j=0;j<rect->Height();j++)
         for(int i=0;i<rect->Width();i++)
         {
            if( ((i + x) % 2 == 0) && ( (j + y) % 2 != 0) ||
               ((i + x) % 2 != 0) && ( (j + y) % 2 == 0))
            {
               // create a one pixel wide rectangle
               tmp_rect.SetRect(x+i,y+j,x+i+1,y+j+1);

               // fill it in with the brush created above
               dc->FillRect(tmp_rect, &brush);
            }
         }

         // increment the pointer to the list of CRects
         buffer_ptr += sizeof(CRect);
   }

   // free the memory used by the region data
   delete lpRgnData;

   // draw the polygon's border
   CPen pen;
   pen.CreatePen(PS_SOLID, 1, color);

   CBrush* pbrushOld = (CBrush*)dc->SelectStockObject(NULL_BRUSH);
   CPen* oldpen = (CPen*) dc->SelectObject(&pen);

   // dc->Polygon(cpt, 4);

   dc->SelectObject(oldpen);
   dc->SelectObject(pbrushOld);
   pen.DeleteObject();
}
#endif  // _WIN32 (draw_shade_regn)

// *************************************************************
// *************************************************************

// creates a pattern brush, user must delete the brush after use
// use SetTextColor and SetBkColor to set the color of the bits
// before using the pattern
#ifdef _WIN32  // create_pattern_brush_rgb: GDI brushes
int CUtil::create_pattern_brush_rgb(int *pattern, CBrush &or_brush, CBrush &and_brush)
{
   // Create a hatched bit pattern.
   WORD HatchBits[4];

   int index = 0;
   int bit_count = 4;
   WORD hatch = 0;
   int *ptr = pattern;
   for(int j=0;j<4;j++)
   {
      for(int i=0;i<4;i++)
      {
         if (*ptr++)
            hatch |= (1 << (bit_count - 1));
         bit_count--;

         if (bit_count == 0)
         {
            HatchBits[index++] = (WORD) ~hatch;
            bit_count = 4;
            hatch = 0;
         }
      }
   }

   CBitmap shade_bits, and_bits;
   if (shade_bits.CreateBitmap(4,4,1,1, HatchBits) == 0)
      return FAILURE;

   if (and_bits.CreateBitmap(4,4,1,1, HatchBits) == 0)
      return FAILURE;

   if (or_brush.CreatePatternBrush(&shade_bits) == 0)
      return FAILURE;

   if (and_brush.CreatePatternBrush(&and_bits) == 0)
      return FAILURE;

   shade_bits.DeleteObject();
   and_bits.DeleteObject();
   return SUCCESS;
}
#endif  // _WIN32 (create_pattern_brush_rgb)
// end of create_pattern_brush_rgb

// ********************************************************************
// ********************************************************************

void CUtil::rotate_pt(int oldx, int oldy,     // original point
   int *newx, int *newy,   // new point
   double ang,             // angle in degrees
   int ctrx, int ctry)     // center point
{
   double sinang, cosang;
   double x, y;
   double rang;

   rang = DEG_TO_RAD(ang);
   sinang = sin(rang);
   cosang = cos(rang);

   x = (double) (oldx - ctrx);
   y = (double) (oldy - ctry);

   *newx =  round((x * cosang) - (y * sinang) + 0.0) + ctrx;
   *newy = round((x * sinang) + (y * cosang) + 0.0) + ctry;
}

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // Win32 file/disk APIs; POSIX in fv_imagelib_util_posix.cpp
int CUtil::create_directory(const CString& dirname, CString & error_msg)
{
   // check for existence of the directory, create is necessary
   if (_access(dirname, 0) == -1)
   {
      SECURITY_ATTRIBUTES security;

      security.nLength = sizeof(SECURITY_ATTRIBUTES);
      security.lpSecurityDescriptor = NULL;
      security.bInheritHandle = TRUE;

      if (!CreateDirectory(dirname, &security))
      {
         error_msg = "Could not create directory -- " + dirname;
         return FAILURE;
      }
   }
   return SUCCESS;
}
#endif  // _WIN32

// ********************************************************************
// ********************************************************************

double CUtil::geo_width(double lon1, double lon2)
{
   double tlon1, tlon2, width;

   tlon1 = lon1;
   tlon2 = lon2;

   if (geo_east_of(lon1, lon2))
   {
      tlon1 = lon2;
      tlon2 = lon1;
   }


   width = tlon2 - tlon1;
   if ((tlon1 >= 0) && (tlon2 < 0.0))
      width += 360.0;

   return width;
}

// ********************************************************************
// ********************************************************************

// nearest pixel remapping of one 24-bit image space into another
// transparent pixels are black
int CUtil::remap_image(int dst_width, int dst_height, BYTE *dst_img, int src_width, int src_height, BYTE *src_img)
{
   int x, y, tx, ty, spos, dpos;
   double vscale, hscale;
   BYTE r, g, b;

   hscale = (double) dst_width / (double) src_width;
   vscale = (double) dst_height / (double) src_height;

   dpos = 0;
   for (y=0; y< dst_height; y++)
   {
      ty = (int) ((double) y / vscale);
      for (x=0; x < dst_width; x++)
      {
         tx = (int) ((double) x / hscale);
         spos = ((ty * src_width) + tx) * 3;
         r = src_img[spos+0];
         g = src_img[spos+1];
         b = src_img[spos+2];
         //   if ((r == 0) && (g == 0) && (b == 0))  // make sure no valid pixel is black
         //    r = g = b = 1;
         dst_img[dpos+0] = r;
         dst_img[dpos+1] = g;
         dst_img[dpos+2] = b;
         dpos += 3;
      }
   }

   return SUCCESS;
}
// remap_image

// ********************************************************************
// ********************************************************************

// interpolated pixel remapping of one 24-bit image space into another
// transparent pixels are black
int CUtil::remap_image_interp(int dst_width, int dst_height, BYTE *dst_img, int src_width, int src_height, BYTE *src_img)
{
   double vscale, hscale, factor;
   int row, col, index, icol, irow;
   int pos, pos_t, pos_b, pos_l, pos_r;
   int val, val_t, val_b, val_l, val_r;
   // int nsp, ewp;
   double nsp, ewp;
   double nso, nsn, ewo, ewn;
   double inso, rdif, iewo;
   int size, r, g, b, width, height, iwidth, iheight;

   hscale = (double) dst_width / (double) src_width;
   vscale = (double) dst_height / (double) src_height;

   size = src_width * src_height;

   factor = hscale;
   width = dst_width;
   height = dst_height;
   iwidth = src_width;
   iheight = src_height;

   index = 0;
   if (hscale > 1.0)  // supersamping
   {
      for ( row = 0; row < height; row++ )
      {
         irow = (int) ((double) row / factor);
         if (irow >= iheight)
         {
            index += width;
            continue;
         }
         inso = (double) (row % (int) factor) / factor;
         rdif = row - (irow * (int) factor);
         for ( col = 0; col < width; col++ )
         {
            icol = col;
            icol = (int) ((double) icol / factor);
            if (icol >= iwidth)
            {
               index++;
               continue;
            }
            iewo = (double) (col % (int) factor) / factor;
            pos = (irow * iwidth) + icol;
            ASSERT(pos < size);
            pos_l = pos - 1;
            if (icol == 0)
               pos_l = pos;
            pos_r = pos + 1;
            if (icol >= iwidth - 1)
               pos_r = pos;
            pos_t = pos - iwidth;
            if (irow == 0)
               pos_t = pos;
            pos_b = pos + iwidth;
            if (irow >= iheight - 1)
               pos_b = pos;
            // red
            val = src_img[pos*3+0];
            val_l = src_img[pos_l*3+0];
            val_r = src_img[pos_r*3+0];
            val_t = src_img[pos_t*3+0];
            val_b = src_img[pos_b*3+0];
            if (inso < 0.5)
            {
               nso = inso + 0.5;
               nsn = 1.0 - nso;
               nsp = ((double) val * nso) + ((double) val_t * nsn);
            }
            else
            {
               nso = inso;
               nsn = 1.0 - nso;
               nsp = ((double) val * nso) + ((double) val_b * nsn);
            }
            if (iewo < 0.5)
            {
               ewo = iewo + 0.5;
               ewn = 1.0 - ewo;
               ewp = ((double) val * ewo) + ((double) val_l * ewn);
            }
            else
            {
               ewo = iewo;
               ewn = 1.0 - ewo;
               ewp = ((double) val * ewo) + ((double) val_r * ewn);
            }
            r = (int) ((nsp + ewp) / 2.0);

            // grn
            val = src_img[pos*3+1];
            val_l = src_img[pos_l*3+1];
            val_r = src_img[pos_r*3+1];
            val_t = src_img[pos_t*3+1];
            val_b = src_img[pos_b*3+1];
            if (inso < 0.5)
            {
               nso = inso + 0.5;
               nsn = 1.0 - nso;
               nsp = ((double) val * nso) + ((double) val_t * nsn);
            }
            else
            {
               nso = inso;
               nsn = 1.0 - nso;
               nsp = ((double) val * nso) + ((double) val_b * nsn);
            }
            if (iewo < 0.5)
            {
               ewo = iewo + 0.5;
               ewn = 1.0 - ewo;
               ewp = ((double) val * ewo) + ((double) val_l * ewn);
            }
            else
            {
               ewo = iewo;
               ewn = 1.0 - ewo;
               ewp = ((double) val * ewo) + ((double) val_r * ewn);
            }
            g = int((nsp + ewp) / 2.0);
            // blu
            val = src_img[pos*3+2];
            val_l = src_img[pos_l*3+2];
            val_r = src_img[pos_r*3+2];
            val_t = src_img[pos_t*3+2];
            val_b = src_img[pos_b*3+2];
            if (inso < 0.5)
            {
               nso = inso + 0.5;
               nsn = 1.0 - nso;
               nsp = ((double) val * nso) + ((double) val_t * nsn);
            }
            else
            {
               nso = inso;
               nsn = 1.0 - nso;
               nsp = ((double) val * nso) + ((double) val_b * nsn);
            }
            if (iewo < 0.5)
            {
               ewo = iewo + 0.5;
               ewn = 1.0 - ewo;
               ewp = ((double) val * ewo) + ((double) val_l * ewn);
            }
            else
            {
               ewo = iewo;
               ewn = 1.0 - ewo;
               ewp = ((double) val * ewo) + ((double) val_r * ewn);
            }
            b = (int) ((nsp + ewp) / 2.0);


            pos = (irow * src_width) + icol;
            if ((icol <= src_width) && (irow <= src_height))
            {
               dst_img[index*3+0] = (BYTE) r;
               dst_img[index*3+1] = (BYTE) g;
               dst_img[index*3+2] = (BYTE) b;
            }
            else
            {
               dst_img[index*3+0] = 0;
               dst_img[index*3+1] = 0;
               dst_img[index*3+2] = 0;
            }

            index++;
         }
      }
   }
   else  // subsampling
   {
      INT
         iSubsizeX = (INT) ( ( 1.0 / hscale ) + 0.5 ),
         iSubsizeY = (INT) ( ( 1.0 / vscale ) + 0.5 ),
         iSYStep = 3 * src_width,
         iDPos = 0;
      DOUBLE
         dSXStep = 1.0 / hscale,
         dSYStep = 1.0 / vscale,
         dSY = 0.0;
      for ( INT iDY = 0; iDY < dst_height; dSY += dSYStep, iDY++ )
      {
         INT iSPosY = src_width * (INT) dSY;
         DOUBLE dSX = 0.0;
         for ( INT iDX = 0; iDX < dst_width; dSX += dSXStep, iDX++ )
         {
            INT iSPos0 = 3 * ( iSPosY + (INT) dSX );

            // Find the average pixel value in the source block
            INT red = 0, grn = 0, blu = 0, c = 0;
            for ( INT iSY = 0; iSY < iSubsizeY; iSPos0 += iSYStep, iSY++ )
            {
               INT iSPos = iSPos0;
               for ( INT iSX = 0; iSX < iSubsizeX; iSPos += 3, iSX++ )
               {
                  // Include only if not transparent
                  if ( 0 != ( 0x00FFFFFF & *( (PULONG) &src_img[ iSPos ] ) ) )
                  {
                     red += src_img[ iSPos + 0 ];
                     grn += src_img[ iSPos + 1 ];
                     blu += src_img[ iSPos + 2 ];
                     c++;
                  }
               }  // X loop
            }  // Y loop

            // Transparent output if all inputs tranparent
            if ( c == 0 )
            {
               dst_img[ iDPos + 0 ] = 0;
               dst_img[ iDPos + 1 ] = 0;
               dst_img[ iDPos + 2 ] = 0;
            }
            else
            {
               INT cRound = c / 2;
               dst_img[ iDPos + 0 ] = (BYTE) ( ( red + cRound ) / c );
               dst_img[ iDPos + 1 ] = (BYTE) ( ( grn + cRound ) / c );
               dst_img[ iDPos + 2 ] = (BYTE) ( ( blu + cRound ) / c );
            }
            iDPos += 3;
         }  // Destination X loop
      }  // Destination Y loop
   }  // Subsampling

   return SUCCESS;
}
// remap_image_interp


// ********************************************************************
// ********************************************************************

// nearest pixel remapping of one 24-bit image space into another
// src and dest are equal-arc, north-up
// transparent pixels are black

int CUtil::remap_image(double dst_ul_lat, double dst_ul_lon, double dst_lr_lat, double dst_lr_lon,
   int dst_width, int dst_height, BYTE *dst_img, 
   double src_ul_lat, double src_ul_lon, double src_lr_lat, double src_lr_lon,
   int src_width, int src_height, BYTE *src_img)
{
   int x, y, sx, sy, spos, dpos;
   double dst_latwidth, dst_lonwidth, src_latwidth, src_lonwidth;
   double dst_lat_inc, dst_lon_inc, src_lat_inc, src_lon_inc;
   double dlat, dlon;
   BYTE r, g, b;

   dst_latwidth = dst_ul_lat - dst_lr_lat;
   dst_lonwidth = geo_width(dst_ul_lon, dst_lr_lon);
   dst_lat_inc = dst_latwidth / (double) dst_height;
   dst_lon_inc = dst_lonwidth / (double) dst_width;
   src_latwidth = src_ul_lat - src_lr_lat;
   src_lonwidth = geo_width(src_ul_lon, src_lr_lon);
   src_lat_inc = src_latwidth / (double) src_height;
   src_lon_inc = src_lonwidth / (double) src_width;

   dpos = 0;
   for (y=0; y<dst_height; y++)
   {
      dlat = dst_ul_lat - ((double) y * dst_lat_inc);
      for (x=0; x < dst_width; x++)
      {
         dlon = dst_ul_lon + ((double) x * dst_lon_inc);
         sx = (int) (((dlon - src_ul_lon) * src_lon_inc) + 0.5);
         sy = (int) (((src_ul_lat - dlat) * src_lat_inc) + 0.5);
         if ((sx >= 0) && (sy >= 0) && (sx < src_width) && (sy < src_height))
         {
            spos = ((sy * src_width) + sx) * 3;
            r = src_img[spos+0];
            g = src_img[spos+1];
            b = src_img[spos+2];
            //    if ((r == 0) && (g == 0) && (b == 0))  // make sure no valid pixel is black
            //     r = g = b = 1;
            dst_img[dpos+0] = r;
            dst_img[dpos+1] = g;
            dst_img[dpos+2] = b;
         }
         else
         {
            dst_img[dpos+0] = 0;
            dst_img[dpos+1] = 0;
            dst_img[dpos+2] = 0;
         }
         dpos += 3;
      }
   }

   return SUCCESS;
}
// remap_image

// ********************************************************************
/*************************************************************************************/
/* From latitude & longitude of two points (p1 & p2) at point 1,                     */
/* compute bearing(=azimuth) & distance.                                             */
/*                                                                                   */
/* The algorithm was published by T.Vincenty in Survey Review, NO 176, 1975          */
/* VOL XXIII Pages 88-93.                                                            */
/* It has also been used by Defense Mapping Agency Systems Center                    */
/* 6500 Brookes Lane Washington, D.C. 20315-0030 (POC: Bradford W. Drew - SC/EG)     */
/* DMA's version is written in Fortran.                                              */
/*************************************************************************************/

int CUtil::geo_to_distance (double pt1Lat, double pt1Lon, 
   double pt2Lat, double pt2Lon,
   double far *mag,  // distance in meters
   double far *dir)  // bearing
{
   const long double A  = 6378137.0;  /* semi-major axis of ellipsoid */
   const long double RECF = 298.257223563; /* reciprocal of flattening (1/F) */

   const long double TESTV = 1.0E-11;

   long double /* intermediate values used in the calculation */
      B, C2SIGM, CDLAMS, COSSAZ, COSSIG, COSU1, COSU2,
      DENOM, DLAM, DLAMS, FL, RNUMER,
      SDLAMS, SIG, SINAZ, SINSIG, SINU1, SINU2,
      TA, TANU1, TANU2, TB, TC, TEMP,US;
   int ITER, cnt;

   // set default values for return params
   *mag = 0.0;
   *dir = 0.0;

   // check for valid input values
   if ((pt1Lat < -90.0) || (pt1Lat > 90.0))
      return FAILURE;
   if ((pt2Lat < -90.0) || (pt2Lat > 90.0))
      return FAILURE;
   if ((pt1Lon < -180.0) || (pt1Lon > 180.0))
      return FAILURE;
   if ((pt2Lon < -180.0) || (pt2Lon > 180.0))
      return FAILURE;

   // convert to radians 
   pt1Lat = DEG_TO_RAD(pt1Lat);
   pt1Lon = DEG_TO_RAD(pt1Lon);
   pt2Lat = DEG_TO_RAD(pt2Lat);
   pt2Lon = DEG_TO_RAD(pt2Lon);

   /* adjust values */
   if (pt1Lon > PI) 
      pt1Lon = pt1Lon - 2.0 * PI;
   if (pt1Lon < -PI) 
      pt1Lon = pt1Lon + 2.0 * PI;
   if (pt2Lon > PI) 
      pt2Lon = pt2Lon - 2.0 * PI;
   if (pt2Lon < -PI) 
      pt2Lon = pt2Lon + 2.0 * PI;

   if( fabs( pt1Lat - pt2Lat ) < TESTV     &&
      fabs( pt1Lon - pt2Lon ) < TESTV )
   {
      /* TWO STATIONS ARE IDENTICAL 
      SET DISTANCE & AZIMUTHS TO ZERO */
      *mag = 0.00;
      *dir = 0.00;
      return SUCCESS;
   }

   /* SEMI MAJOR AXIS - B */
   B = A*( RECF -1.0 ) / RECF;

   /* FLATTENING (F) */
   FL = 1.0 / RECF;

   /* ITERATION COUNTER */
   ITER = 0;

   /* TANGENT OF REDUCED LATITUDE (U) OF POINT 1 & 2 */
   TANU1 = ( 1.0 -FL ) * sin( pt1Lat ) / cos( pt1Lat );
   TANU2 = ( 1.0 -FL ) * sin( pt2Lat ) / cos( pt2Lat );

   /* COSINE & SINE OF U1 & U2 FROM TRIG IDENTITIES */
   COSU1 = (long double) (1.0 / sqrt( 1.0 + pow((double) TANU1, 2.0) ));
   SINU1 = TANU1 * COSU1;
   COSU2 = (long double) (1.0 / sqrt( 1.0 + pow((double) TANU2, 2.0) ));
   SINU2 = TANU2 * COSU2;

   /* SET SINU1 TO ZERO IF COSU1 IS ONE */
   if( COSU1 == 1.0 ) 
      SINU1 = 0.00;

   /* SET SINU2 TO ZERO IF COSU2 IS ONE */
   if( COSU2 == 1.0 ) 
      SINU2 = 0.00;

   /* DIFFERENCE IN LO->GITUDE */
   DLAM = pt2Lon - pt1Lon;

   /* 1ST ESTIMATE DLAMS - DIFF IN LONGITUDE ON AUXILIARY SPHERE */
   DLAMS = DLAM;

   do {

      /* SINE & COSINE OF DLAMS */
      SDLAMS = sin( (double) DLAMS );
      CDLAMS = cos( (double) DLAMS );

      /* SINE & COSINE OF SIGMA */
      SINSIG = sqrt(   pow(( (double) COSU2 * (double) SDLAMS ), 2.0)
         + pow(( (double) COSU1 * (double) SINU2  - (double)(SINU1*COSU2*CDLAMS) ), 2.0 ));
      COSSIG = SINU1*SINU2 +COSU1*COSU2*CDLAMS;

      /* SIGMA */
      SIG = atan2( (double) SINSIG, (double) COSSIG );

      /* SINE OF AZIMUTH (AZ) OF GEODESIC AT EQUATOR */
      SINAZ = COSU1*COSU2*SDLAMS/SINSIG;

      /* COSINE SQUARED OF AZ USING TRIG IDENTITY */
      COSSAZ = 1.0 - pow((double) SINAZ, 2.0);

      /* COSINE OF 2 SIGMA-SUB-M */
      if ( SINU1 == 0.0  || SINU2 == 0.0 )
         C2SIGM = COSSIG;
      else
         C2SIGM = COSSIG -2.0*SINU1*SINU2/COSSAZ;

      /* TERM C */
      TC = FL*COSSAZ*( 4.0 +FL*( 4.0 -3.0*COSSAZ ) )/16.0;

      /* SAVE PREVIOUS DIFF IN LONGITUDE ON AUXILIARY SPHERE */
      TEMP = DLAMS;

      /* NEWEST DIFFERENCE IN LONGITUDE ON AUXILIARY SPHERE */
      DLAMS = DLAM  +( 1.0 -TC )*FL*SINAZ*( SIG
         +TC*SINSIG*( C2SIGM +TC*COSSIG*( -1.0
         +2.0*pow((double) C2SIGM,2) ) ) );

      /* ITERATION COUNTER */
      ITER = ITER +1;

      /* TEST FOR NEARLY ANTIPODAL LINE CONDITION */
      if( fabs( (double) DLAMS ) > PI && ITER > 50 )
      {   /* ANTIPODAL CONDITION */
         *mag = 0.0;
         *dir = 0.0;
         return FAILURE;
      }

   } while ( fabs((double) TEMP-(double) DLAMS) > TESTV );


   /* SMALL u SQUARED (US) */
   US = COSSAZ*( pow((double) A, 2.0) -pow((double) B, 2.0))/pow((double) B, 2.0);

   /* FORWARD AZIMUTH FROM NORTH */
   /* NUMERATOR & DENOMINATOR */
   RNUMER =  COSU2*SDLAMS;
   DENOM =  COSU1*SINU2 -SINU1*COSU2*CDLAMS;
   *dir = atan2( (double) RNUMER, (double) DENOM );
   if ( *dir < 0.0 ) 
      *dir = *dir + 2.0 * PI;

   /* TERM A */
   TA = 1.0 +US*( 4096.0 + US *( -768.0
      + US *( 320.0 -175.0*US ) ) ) / 16384.0;

   /* TERM B */
   TB = US*( 256.0 +US*( -128.0 +US*( 74.0 -47.0*US ) ) )/1024.0;

   /* GEODETIC DISTANCE */
   *mag = (double) (B * TA * ( SIG  - TB * SINSIG * ( C2SIGM
      + TB * ( COSSIG * ( -1.0 +2.0 * pow((double) C2SIGM, 2.0))
      - TB * C2SIGM * (   -3.0 +4.0 * pow((double) SINSIG, 2.0)) * (
      -3.0 + 4.0 * pow((double) C2SIGM, 2.0))/6.0 )/4.0  )   ));

   // *mag *= FT_per_METER;  /* convert to feet */

   cnt = 0;
   while (*dir < 0.0)
   {
      *dir += (2*PI);
      cnt++;
      // too many iterations here indicate a bad value of *dir
      if (cnt > 10)
         return FAILURE;
   }
   cnt = 0;
   while (*dir > (2*PI))
   {
      *dir -= (2*PI);
      cnt++;
      // too many iterations here indicate a bad value of *dir
      if (cnt > 10)
         return FAILURE;
   }

   if (*mag < 0.000000001) 
      *dir = 0.0;

   /* convert to degrees */
   *dir = RAD_TO_DEG(*dir);

   return SUCCESS;
}
// end of GEO_geo_to_distance

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // user_agrees_to_abort: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
BOOL CUtil::user_agrees_to_abort(IImageLibCallback *callback)
{
   int rslt;

   if (callback == NULL)
      return FALSE;

   _bstr_t label = "Image Operation Aborted by User";


   double percent = -999.0;
   rslt = callback->imagelib_progress(percent, label);
   if (rslt != 0)  // S_FALSE
      return TRUE;

   return FALSE;
}
#endif  // _WIN32 (user_agrees_to_abort)

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // escape_pressed: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
BOOL CUtil::escape_pressed(IImageLibCallback *callback)
{
   MSG msg;

   // check for WM_KEYDOWN  
   if (PeekMessage(&msg, NULL, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE))
   {
      // The Esc key is the only one that will cause an interrupt.      
      if (msg.wParam == VK_ESCAPE)
      {
         // Since it is being used to interrupt the draw, it must be removed
         // from the message queue.  Otherwise you will get to actions from
         // one input.
         PeekMessage(&msg, NULL, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE);

         if (user_agrees_to_abort(callback))
            return TRUE;
      }
      else
      {
         CWinThread* thread = AfxGetThread();
         if (thread)
            thread->PumpMessage();
      }
   }

   return FALSE;
}
#endif  // _WIN32 (escape_pressed)

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // send_user_update_and_continue: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
BOOL CUtil::send_user_update_and_continue( IImageLibCallback* pCallback, double dPercent, LPCSTR pszLabel )
{
   return send_user_update_and_continue( pCallback, dPercent, CString( pszLabel ) );
}
#endif  // _WIN32 (send_user_update_and_continue)

#ifdef _WIN32  // send_user_update_and_continue: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
BOOL CUtil::send_user_update_and_continue(IImageLibCallback *callback, double percent, const CString& label)
{
   if (m_abort_flag)
   {
      m_abort_flag = FALSE;
      return FALSE;
   }

   // if (FVW_is_draw_interrupted())
   //  return FALSE;

   if (callback != NULL)
   {
      if (escape_pressed(callback))
         return FALSE;

      DWORD dwT = GetTickCount();
      if ( (INT) ( dwT - m_dwLastCallbackTime ) > MIN_CALLBACK_INTERVAL )
      {
         m_dwLastCallbackTime = dwT;
         _bstr_t blabel = label;

         if ( S_OK != callback->imagelib_progress( percent, blabel ) )
            return FALSE;
      }
   }
   else
   {
      // pump a message if one is waiting
      //  MSG msg;
      //  if ( ::PeekMessage( &msg,  NULL, 0, 0, PM_NOREMOVE ) )
      //   AfxGetThread()->PumpMessage();
   }

   return TRUE;
}
#endif  // _WIN32 (send_user_update_and_continue)
// end of send_user_update_and_continue

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // clear_callback: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
void CUtil::clear_callback(IImageLibCallback *callback)
{
   if (callback != NULL)
   {
      double percent = -1.0;

      _bstr_t label = " ";

      callback->imagelib_progress(percent, label);
   }
}
#endif  // _WIN32 (clear_callback)

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // file_access: Win32 attribute checks
int CUtil::file_access(const char *path, int mode)
{
   if (strlen(path) && _access(path, mode) == 0)
   {
      DWORD attributes;

      /* Use the files attributes to handle the fact that access() does not
      handle directories on Windows NT. */
      attributes = GetFileAttributesW( T2WFS( path ) );
      if ( attributes == 0xFFFFFFFF )
      {
         WriteToLogFile( L"ImageLib::file_access() - GetFileAttributes() failed." );
         return FAILURE;
      }

      /* If the path is a file (not a directory) or it is only being tested for
      existance (not for read or write access), then the value returned by
      access() is valid. */
      if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || mode == UTIL_FIL_EXISTS) 
         return SUCCESS;

      /* test for write access if it was requested */
      if (mode & UTIL_FIL_WRITE_OK)
      {
         char test[MAX_PATH];
         int i;
         boolean_t end_with_backslash;
         FILE *tmp = NULL;

         /* detect the presence of the trailing backslash */
         if (path[strlen(path) - 1] == '\\')
            end_with_backslash = TRUE;
         else
            end_with_backslash = FALSE;

         i = 0;
         while (i < 99999999)
         {
            TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
            /* construct specification for a temporary file */
            if (end_with_backslash)
               sprintf_s(test, MAX_PATH, "%s%d.tmp", path, i);
            else
               sprintf_s(test, MAX_PATH, "%s\\%d.tmp", path, i);

            /* if the files does not already exists try to create on */
            if (_access(test, UTIL_FIL_EXISTS) != 0)
            {
               /* if you can create this file then you have write access */
               if ( 0 == fopen_s(&tmp, test, "wb")
                  && tmp != NULL )
               {
                  fclose(tmp);

                  remove(test);

                  /* assume read and execute access */
                  return SUCCESS;
               }
               else
                  return FAILURE;
            }

            i++;
         }

         return FAILURE;
      }

      /* assume read and execute access */
      return SUCCESS;
   }
   else
      return FAILURE;
}
#endif  // _WIN32 (file_access)
// end of file_access

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // get_default_imagery_data_paths: Windows-bound; POSIX version (if needed) in fv_imagelib_util_posix.cpp
INT CUtil::get_default_imagery_data_paths( CString& csErrorMsg ) 
{
   // Look up default path only if hasn't been done yet
   if ( s_csDefaultImageryDataPath.IsEmpty() )
   {
      do    // Once
      {
         // Create the NITF DB access agent
         INITFDBAgentPtr pNITFDBAgent;
         HRESULT hr = pNITFDBAgent.CreateInstance( CLSID_NITFDBAgent,
            NULL, CLSCTX_INPROC_SERVER ); 
         if ( SUCCEEDED( hr ) )
         {

            // Create a recordset for the default overview directory
            _RecordsetPtr pRecordset;
            hr = pRecordset.CreateInstance( __uuidof(Recordset) );
            if ( SUCCEEDED( hr ) )
            {

               // Open the connection
               hr = pNITFDBAgent->raw_Open( NULL, NULL, NULL, &pRecordset );
               if ( SUCCEEDED( hr ) )
               {

                  // Query for the default overview directory.
                  static const int TEMP_BUF_LEN = 256;
                  WCHAR wchTemp[ TEMP_BUF_LEN ];

                  TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
                  if ( 0 >= _snwprintf_s( wchTemp, TEMP_BUF_LEN, TEMP_BUF_LEN,
                     L"SELECT SourceRootPath\n"
                     L"FROM " IMAGE_SOURCE_PATH_TABLE L"\n"
                     L"WHERE Priority = %d\n",
                     PRIORITY_DEFAULT_OVERVIEWS_FOLDER ) )
                  {
                     WriteToLogFile( L"ImageLib::get_default_imagery_data_path::_snwprintf() failed." );
                  }
                  else  // _snwprintf succeeded
                  {
                     hr = pNITFDBAgent->raw_QuerySQL( wchTemp, NULL, &pRecordset );
                     if ( FAILED( hr ) )
                     {
                        _snwprintf_s( wchTemp, TEMP_BUF_LEN, TEMP_BUF_LEN,
                           L"NITFDBAgent::QuerySQL() failed, hr = 0x%08x", hr );
                        WriteToLogFile( wchTemp );
                     }
                     else if ( !pRecordset->adoEOF )  // Non-empty recordset
                     {
                        s_csDefaultImageryDataPath = (PTCHAR) _bstr_t( pRecordset->Fields
                           ->GetItem( L"SourceRootPath" )->Value );
                        break;                        // Check for writeable directory
                     }

                  }  // _snwprintf() succeeded
               }  // NITFDBAgent::open() succeeded
            }  // Create _recordset succeeded
         }  // Create NITFDBAgent succeeded

         TCHAR tchTemp[ MAX_PATH + 1 ];
         _bstr_t JMPS_REG_PATH(JMPS_REGISTRY_PATH);
         _bstr_t JMPS_DATA_DIR(KEY_JMPS_DATA_DIR);

         // Couldn't get overview path from the NITFImagery database.  Try the registry
         struct
         {
            LPCTSTR pszKey;             // Directory key
            LPCTSTR pszValue;           // Value name
         } static const RegLocations[] =
         {
            { _T("Software\\XPlan\\FALCONVIEW\\MAIN"), _T("HD_DATA") },
            { _T("Software\\PFPS\\FALCONVIEW\\MAIN"), _T("HD_DATA") },
            { JMPS_REG_PATH                         , JMPS_DATA_DIR },
            { _T("Software\\XPlan\\FALCONVIEW\\MAIN"), _T("USER_DATA") },
            { _T("Software\\PFPS\\FALCONVIEW\\MAIN"), _T("USER_DATA") },
            { NULL } // Stopper
         }, *pRegLocation = RegLocations;

         while ( pRegLocation->pszKey != NULL )
         {
            DWORD dwTempSize = sizeof(tchTemp);
            DWORD dwType;

            if ( read_registry( HKEY_LOCAL_MACHINE, pRegLocation->pszKey, pRegLocation->pszValue,
               &dwType, (PBYTE) tchTemp, &dwTempSize ) == SUCCESS )
            {
               // Check type to see that it was a string
               if ( dwType == REG_SZ )
               {
                  s_csDefaultImageryDataPath = tchTemp;
                  s_csDefaultImageryDataPath += _T("\\NITF");
                  goto AccessCheck;
               }

               WriteToLogFile( L"ImageLib::get_default_imagery_data_path() returned non-string type" );
            }

            pRegLocation++;
         }  // All registry keys

         WriteToLogFile( L"ImageLib::get_default_imagery_data_path() -"
            L" Failed to find default overviews directory from any standard source" );

         INT n = ExpandEnvironmentStrings( _T("%USERPROFILE%"), tchTemp, sizeof(tchTemp) / sizeof(tchTemp[0]) );
         if ( n <= 0 || n > MAX_PATH + 1 )
         {
            WriteToLogFile( L"ImageLib::get_default_imagery_data_path() -"
               L" Failed to find default overviews directory from any source" );
            csErrorMsg = _T("Failed to find default overviews directory from any source");
            return FALSE;
         }
         s_csDefaultImageryDataPath = tchTemp;
         s_csDefaultImageryDataPath += _T("\\Application Data\\MissionPlanning\\NITF");

      } while ( FALSE );

      // See if the subpaths exist
AccessCheck:
      if ( s_csDefaultOverviewPath.IsEmpty() )  // May have been overridden
      {
         s_csDefaultOverviewPath = s_csDefaultImageryDataPath + _T("\\OverViews\\");
         if ( file_access( s_csDefaultOverviewPath, UTIL_FIL_EXISTS ) != SUCCESS )
         {
            if ( create_directory( s_csDefaultOverviewPath, csErrorMsg ) != SUCCESS )
            {
               csErrorMsg = "FIL_create_directory() failed.";
               WriteToLogFile( _bstr_t( L"ImageLib::get_default_imagery_data_paths()"
                  L" - Failed to create writable default imagery overviews directory\n" )
                  + s_csDefaultOverviewPath.GetBuffer() );
               return FAILURE;
            }
            WriteToLogFile( _bstr_t( L"Created default imagery overviews directory!\n" )
               + s_csDefaultOverviewPath.GetBuffer() );
         }
      }

      CString csDefaultFIFPath = s_csDefaultImageryDataPath + _T("\\fif\\");
      if ( file_access( csDefaultFIFPath, UTIL_FIL_EXISTS ) != SUCCESS )
      {
         if ( create_directory( csDefaultFIFPath, csErrorMsg ) != SUCCESS )
         {
            csErrorMsg = "FIL_create_directory() failed.";
            WriteToLogFile( _bstr_t( L"ImageLib::get_default_imagery_data_paths()"
               L" - Failed to create writable default imagery FIF files directory\n" )
               + csDefaultFIFPath.GetBuffer() );
            return FAILURE;
         }
         WriteToLogFile( _bstr_t( L"Created default imagery FIF files directory!\n" )
            + csDefaultFIFPath.GetBuffer() );
      }

   }  // No default path yet

   csErrorMsg = s_csDefaultImageryDataPath;

   return SUCCESS;
}
#endif  // _WIN32 (get_default_imagery_data_paths)

// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // get_overview_write_path: GetDiskFreeSpaceEx
INT CUtil::get_overview_write_path( const CString& csImageFilespec, CString& csOverviewPath, 
   BOOL& bDefaultPath, CString& csErrorMsg )
{
   INT iResult;

   bDefaultPath = FALSE;         // Assume using source path
   csOverviewPath = extract_path( csImageFilespec );     // Assume writeable or overview exists

   iResult = file_access( csOverviewPath, UTIL_FIL_WRITE_OK );
   if ( iResult == SUCCESS )     // Writeable, check for enough free space
   {
      ULARGE_INTEGER uliAvailableBytes, uliFileSize;
      GetDiskFreeSpaceEx( csOverviewPath, &uliAvailableBytes, &uliFileSize, &uliFileSize );
      //      HANDLE h = CreateFile( csImageFilespec, 0,
      //         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
      //         FILE_ATTRIBUTE_NORMAL, NULL );
      //
      //      iResult = FAILURE;      // Assumption
      //      if ( h != INVALID_HANDLE_VALUE )
      {
         //         uliFileSize.LowPart = GetFileSize( h, &uliFileSize.HighPart );
         //         CloseHandle( h );

         // Can write if there's space for a new file of the original size
         // to keep the system from plugging up
         if ( 2 * uliAvailableBytes.QuadPart >= uliFileSize.QuadPart )
            iResult = SUCCESS;
      }
   }
   if ( iResult != SUCCESS )     // If can't write to source directory
   {
      bDefaultPath = TRUE;
      iResult = get_default_imagery_data_paths( csErrorMsg );
      csOverviewPath = s_csDefaultOverviewPath;
   }

   return iResult;
}
#endif  // _WIN32 (get_overview_write_path)

#if 0
// ********************************************************************
// ********************************************************************

CString CUtil::get_overview_name(CString filename)
{
   int len, pos;
   CString tstr, ext, name;

   len = filename.GetLength();

   pos = filename.ReverseFind('\\');
   if (pos <= 0)
      name = filename;
   else
      name = filename.Right(len - pos - 1);

   len = name.GetLength();
   ext = name.Right(len - pos - 1);
   pos = name.ReverseFind('.');

   if (pos <= 0)
   {
      ASSERT(0);
      return "";
   }

   ext = name.Right(len - pos - 1);

   tstr = name.Left(pos);

   tstr += "_";
   tstr += ext;
   tstr += "_ov.tif";
   return tstr;
}
#endif


// ********************************************************************
// ********************************************************************

// AddDecoratedOverviewBaseName()
//
// Adds a filespec-encoded overview base file name to a path string
//
#ifdef _WIN32  // AddDecoratedOverviewBaseName: NITFUtilities dependency
VOID CUtil::AddDecoratedOverviewBaseName( const CString& csImageFilespec,
   CString& csDecoratedBaseFilespec, BOOL bDoDecoration )
{
   // Convert the full filespec to a decorated file name
   if ( bDoDecoration )
   {
      string strDecoratedFileName( "" );
      NITFAppendDecoratedFileName( csImageFilespec, strDecoratedFileName );
      csDecoratedBaseFilespec += CA2T( strDecoratedFileName.c_str() );
   }
   else
      csDecoratedBaseFilespec = csImageFilespec;

   // Convert xxx.yyy to xxx_yyy_ov.tif
   csDecoratedBaseFilespec.SetAt( csDecoratedBaseFilespec.ReverseFind( _T('.') ), _T('_') );
   csDecoratedBaseFilespec += _T("_ov");

}  // End of CUtil::AddDecoratedOverviewBaseName()
#endif  // _WIN32 (AddDecoratedOverviewBaseName)


// ********************************************************************
// ********************************************************************

VOID CUtil::calc_overview_temp_base_file( const CString& csImageFilespec, CString& csTempBaseFilespec )
{
   csTempBaseFilespec = get_temp_path();
   AddDecoratedOverviewBaseName( csImageFilespec, csTempBaseFilespec, TRUE );
}


// ********************************************************************
// ********************************************************************

// calc_overview_name() - Overview filespec with intent to read
INT CUtil::calc_overview_name( const CString& csImageFilespec,
   CString& csOverviewFilespec, CString& csErrorMsg )
{
   INT iResult;

   // Done if in-folder overview exists
   csOverviewFilespec = csImageFilespec;
   csOverviewFilespec.SetAt( csOverviewFilespec.ReverseFind( _T('.') ), _T('_') );
   csOverviewFilespec += _T("_ov.tif");

   iResult = file_access( csOverviewFilespec, UTIL_FIL_EXISTS );
   if ( iResult == SUCCESS )     // If already exists
   {
      csErrorMsg = _T("");
   }
   else                          // Doesn't already exist
   {
      // Depends now upon whether full image file's directory is writeable
      iResult = calc_overview_write_name( csImageFilespec, csOverviewFilespec, csErrorMsg );
   }
   return iResult;
}


// ********************************************************************
// ********************************************************************

// calc_overview_write_name() - Overview filespec with intent to write
INT CUtil::calc_overview_write_name( const CString& csImageFilespec,
   CString& csOverviewFilespec, CString& csErrorMsg )
{
   INT iResult;
   BOOL bDefaultPath;

   iResult = get_overview_write_path( csImageFilespec, csOverviewFilespec, bDefaultPath, csErrorMsg );
   if ( iResult == SUCCESS )
   {
      // If default path, decorate filename with a translated version of the image path
      AddDecoratedOverviewBaseName( csImageFilespec, csOverviewFilespec, bDefaultPath );
      csOverviewFilespec += _T(".tif");
   }
   return iResult;
}


// ********************************************************************
// ********************************************************************

void CUtil::fix_lon(double * lon)
{
   if (*lon < -180.0)
      *lon += 360.0;
   if (*lon > 180.0)
      *lon -= 360.0;
}

// ********************************************************************
// ********************************************************************

void CUtil::set_abort_flag(BOOL abort)
{
   m_abort_flag = abort;
}

// ********************************************************************
// ********************************************************************

BOOL CUtil::file_exists( CString filename ) 
{
   int rslt;

   rslt = file_access(filename, UTIL_FIL_EXISTS);
   if (rslt == SUCCESS)
      return TRUE;

   return FALSE;
}

// ********************************************************************
// ********************************************************************

BOOL CUtil::is_multifile( CString& filename, CStringArray& list )
{
   CString path, tpath, name, tname, root_left, root_right;
   BOOL found = FALSE;
   int pos, len;

   list.RemoveAll();

   path = extract_path(filename);
   name = extract_filename(filename);
   name.MakeLower();
   len = name.GetLength();

   pos = name.Find("_blu_");
   if (pos > 0)
   {
      found = TRUE;
      root_left = name.Left(pos);
      root_right = name.Right(len - pos - 5);
   }

   pos = name.Find("_grn_");
   if (pos > 0)
   {
      found = TRUE;
      root_left = name.Left(pos);
      root_right = name.Right(len - pos - 5);
   }
   pos = name.Find("_nir_");
   if (pos > 0)
   {
      found = TRUE;
      root_left = name.Left(pos);
      root_right = name.Right(len - pos - 5);
   }
   pos = name.Find("_red_");
   if (pos > 0)
   {
      found = TRUE;
      root_left = name.Left(pos);
      root_right = name.Right(len - pos - 5);
   }
   pos = name.Find("_xxx_");
   if (pos > 0)
   {
      found = TRUE;
      root_left = name.Left(pos);
      root_right = name.Right(len - pos - 5);
   }

   if (!found)
      return FALSE;

   tname = root_left;
   tname += "_blu_";
   tname += root_right;
   tpath = path + tname;
   if (!file_exists(tpath))
      return FALSE;
   list.Add(tpath);

   tname = root_left;
   tname += "_grn_";
   tname += root_right;
   tpath = path + tname;
   if (!file_exists(tpath))
      return FALSE;
   list.Add(tpath);

   tname = root_left;
   tname += "_red_";
   tname += root_right;
   tpath = path + tname;
   if (!file_exists(tpath))
      return FALSE;
   list.Add(tpath);

   tname = root_left;
   tname += "_nir_";
   tname += root_right;
   tpath = path + tname;
   if (!file_exists(tpath))
      return FALSE;
   list.Add(tpath);

   filename = list[0];     // Insure that working file name is not "_xxx_" form
   return TRUE;
}

// ********************************************************************
// ********************************************************************

BOOL CUtil::fix_multifile_name(CString & filename)
{
   CString path, tpath, name, tname, root_left, root_right;
   BOOL found = FALSE;
   int pos, len;

   path = extract_path(filename);
   name = extract_filename(filename);
   name.MakeLower();
   len = name.GetLength();

   pos = name.Find("_blu_");
   if (pos > 0)
   {
      found = TRUE;
      root_left = name.Left(pos);
      root_right = name.Right(len - pos - 5);
   }

   pos = name.Find("_grn_");
   if (pos > 0)
   {
      found = TRUE;
      root_left = name.Left(pos);
      root_right = name.Right(len - pos - 5);
   }
   pos = name.Find("_nir_");
   if (pos > 0)
   {
      found = TRUE;
      root_left = name.Left(pos);
      root_right = name.Right(len - pos - 5);
   }
   pos = name.Find("_red_");
   if (pos > 0)
   {
      found = TRUE;
      root_left = name.Left(pos);
      root_right = name.Right(len - pos - 5);
   }

   if (!found)
      return FALSE;

   tname = root_left;
   tname += "_xxx_";
   tname += root_right;
   filename = path + tname;

   return TRUE;
}
// end of fix_multifile_name()


// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // GetRGBFormulaeFromXML: MSXML
BOOL CUtil::GetRGBFormulaeFromXML( const _bstr_t& bstrDisplayParamsXML, CString& csFormulaName,
   CString& csRedFormula, CString& csGreenFormula, CString& csBlueFormula )
{
   try
   {
      do
      {
         if ( !InitDisplayParamsXML( bstrDisplayParamsXML ) )
            break;

         XMLNodePtr pxnName, pxnRed, pxnGreen, pxnBlue;

         if ( NULL == ( pxnName = m_pxnDisplayParamsRoot->selectSingleNode( L"./RGBFormulae/Name" ) ) )
            break;

         if ( NULL == ( pxnRed = m_pxnDisplayParamsRoot->selectSingleNode( L"./RGBFormulae/Red" ) ) )
            break;

         if ( NULL == ( pxnGreen = m_pxnDisplayParamsRoot->selectSingleNode( L"./RGBFormulae/Green" ) ) )
            break;

         if ( NULL == ( pxnBlue = m_pxnDisplayParamsRoot->selectSingleNode( L"./RGBFormulae/Blue" ) ) )
            break;

         csFormulaName = (LPCSTR) pxnName->text;
         csRedFormula = (LPCSTR) pxnRed->text;
         csGreenFormula = (LPCSTR) pxnGreen->text;
         csBlueFormula = (LPCSTR) pxnBlue->text;

         return TRUE;

      } while ( FALSE );
   }
   catch ( ... ) {}

   // Reset to Enhanced True Color
   csFormulaName = "Enhanced True Color";
   csRedFormula = "b3 + (0.2 * b2)";
   csGreenFormula = "(0.8 * b2) + (0.2 * b1)";
   csBlueFormula = "b1 + (0.2 * b2)";

   return FALSE;

}  // End of CUtil::GetRGBFormulaeFromXML()
#endif  // _WIN32 (GetRGBFormulaeFromXML)


// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // SetRGBFormulaeInXML: MSXML
VOID CUtil::SetRGBFormulaeInXML( _bstr_t& bstrDisplayParamsXML,
   const CString& csFormulaName, const CString& csRedFormula,
   const CString& csGreenFormula, const CString& csBlueFormula )
{
   try
   {
      if ( !InitDisplayParamsXML( bstrDisplayParamsXML ) )
         return;

      XMLNodePtr pxnFormulae = m_pxnDisplayParamsRoot->selectSingleNode( L"RGBFormulae" );
      if ( pxnFormulae == NULL )
         pxnFormulae =
         m_pxnDisplayParamsRoot->appendChild( m_pxdDisplayParamsXML->createElement( L"RGBFormulae" ) );

      SetParamInXML( pxnFormulae, BSTR( L"Name" ), csFormulaName );
      SetParamInXML( pxnFormulae, BSTR( L"Red" ), csRedFormula );
      SetParamInXML( pxnFormulae, BSTR( L"Green" ), csGreenFormula );
      SetParamInXML( pxnFormulae, BSTR( L"Blue" ), csBlueFormula );
      bstrDisplayParamsXML = m_pxdDisplayParamsXML->xml;
   }
   catch ( ... ){}
}  // End of CUtil::SetRGBFormulaeInXML()
#endif  // _WIN32 (SetRGBFormulaeInXML)


// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // GetScaleAndOffsetFromXML: MSXML
BOOL CUtil::GetScaleAndOffsetFromXML( const _bstr_t& bstrDisplayParamsXML,
   DOUBLE& dPixelScale, UINT& uiPixelOffset )
{
   try
   {
      do
      {
         if ( !InitDisplayParamsXML( bstrDisplayParamsXML ) )
            break;

         XMLNodePtr pxnScale, pxnOffset;

         if ( NULL == ( pxnScale = m_pxnDisplayParamsRoot->selectSingleNode( L"./ScaleAndOffset/Scale" ) ) )
            break;

         if ( NULL == ( pxnOffset = m_pxnDisplayParamsRoot->selectSingleNode( L"./ScaleAndOffset/Offset" ) ) )
            break;

         _variant_t varScale = pxnScale->text;
         varScale.ChangeType( VT_R8 );

         variant_t varOffset = pxnOffset->text;
         varOffset.ChangeType( VT_UI4 );

         dPixelScale = varScale.dblVal;
         uiPixelOffset = varOffset.uintVal;

         return TRUE;

      } while ( FALSE );
   }
   catch ( ... ) {}

   uiPixelOffset = 0;
   dPixelScale = 0.0;    // Invalid

   return FALSE;

}  // End of CUtil::GetRGBFormulaeFromXML()
#endif  // _WIN32 (GetScaleAndOffsetFromXML)


// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // SetScaleAndOffsetInXML: MSXML
VOID CUtil::SetScaleAndOffsetInXML( _bstr_t& bstrDisplayParamsXML,
   DOUBLE dPixelScale, UINT uiPixelOffset )
{
   try
   {
      if ( !InitDisplayParamsXML( bstrDisplayParamsXML ) )
         return;

      XMLNodePtr pxnScaleAndOffset = m_pxnDisplayParamsRoot->selectSingleNode( L"ScaleAndOffset" );
      if ( pxnScaleAndOffset == NULL )
         pxnScaleAndOffset =
         m_pxnDisplayParamsRoot->appendChild( m_pxdDisplayParamsXML->createElement( L"ScaleAndOffset" ) );

      CString cs;

      cs.Format( _T("%.8g"), dPixelScale );
      SetParamInXML( pxnScaleAndOffset, BSTR( L"Scale" ), cs );

      cs.Format( _T("%u"), uiPixelOffset );
      SetParamInXML( pxnScaleAndOffset, BSTR( L"Offset" ), cs );

      bstrDisplayParamsXML = m_pxdDisplayParamsXML->xml;
   }
   catch ( ... ){}
}  // End of CUtil::SetRGBFormulaeInXML()
#endif  // _WIN32 (SetScaleAndOffsetInXML)


// ********************************************************************
// ********************************************************************

#ifdef _WIN32  // InitDisplayParamsXML: MSXML
BOOL CUtil::InitDisplayParamsXML( const _bstr_t& bstrDisplayParamsXML )
{
   if ( m_pxdDisplayParamsXML == NULL )
   {
      if ( S_OK != m_pxdDisplayParamsXML.CreateInstance( __uuidof( MSXML2::DOMDocument60 ) ) )
         return FALSE;

      do
      {
         if ( bstrDisplayParamsXML.length() > 0 )
         {
            if ( m_pxdDisplayParamsXML->loadXML( bstrDisplayParamsXML ) )
               break;         // Previous string is valid XML
         }
         m_pxdDisplayParamsXML->loadXML(
            BSTR(
            L"<?xml version=\"1.0\"?>"
            L"<Params/>" ) );
      } while ( FALSE );

   }
   return NULL != ( m_pxnDisplayParamsRoot = m_pxdDisplayParamsXML->documentElement );

}  // InitDisplayParamsXML()
#endif  // _WIN32 (InitDisplayParamsXML)


#ifdef _WIN32  // SetParamInXML: MSXML
XMLNodePtr CUtil::SetParamInXML( MSXML2::IXMLDOMNodePtr pxnParent,
   const BSTR bsParamName, const CString& csParamText )
{
   XMLNodePtr pxn;
   if ( NULL == ( pxn = pxnParent->selectSingleNode( bsParamName ) ) )
      pxn = pxnParent->appendChild( m_pxdDisplayParamsXML->createElement( bsParamName ) );

   pxn->text = _bstr_t( csParamText );
   return pxn;

}  // End of CUtil::SetParamInXML()
#endif  // _WIN32 (SetParamInXML)


// ********************************************************************
// ********************************************************************

/*
BOOL CUtil::is_draw_interrupted()
{
//
//  this stuff messes up print preview, so ignore it if printing
//
//   if (m_printing)
//      return FALSE;

if (!AfxGetMainWnd())
return FALSE;  //prevent access violation

int result;

if (GetInputState())
{
BOOL interrupted = FALSE;

MSG msg;

//check for WM_KEYDOWN  
if (PeekMessage(&msg, AfxGetMainWnd()->m_hWnd, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE))
{
// WM_KEYDOWN
// nVirtKey = (int) wParam;    // virtual-key code 
// lKeyData = lParam;          // key data 

if ( msg.wParam == VK_ESCAPE || msg.wParam == VK_PRIOR || 
msg.wParam == VK_NEXT || msg.wParam == VK_LEFT || 
msg.wParam == VK_RIGHT || msg.wParam == VK_UP || 
msg.wParam == VK_DOWN || 
msg.wParam == VK_NUMPAD7 || msg.wParam == VK_NUMPAD8 ||
msg.wParam == VK_NUMPAD9 || msg.wParam == VK_NUMPAD4 ||
msg.wParam == VK_NUMPAD6 || msg.wParam == VK_NUMPAD1 ||
msg.wParam == VK_NUMPAD2 || msg.wParam == VK_NUMPAD3) 
{
interrupted=TRUE;
//if ESC, remove message (because all escape does is abort drawing
//and we've done that)
if(msg.wParam == VK_ESCAPE) 
PeekMessage(&msg, AfxGetMainWnd()->m_hWnd, WM_KEYDOWN, WM_KEYDOWN, 
PM_REMOVE);
}
}

//check for WM_MOUSEWHEEL
if (!interrupted && PeekMessage(&msg, AfxGetMainWnd()->m_hWnd, WM_MOUSEWHEEL, WM_MOUSEWHEEL, PM_NOREMOVE))
interrupted = TRUE;

CRect rect;
CToolBar *t;
t =  (CToolBar*) &((CMainFrame*)AfxGetMainWnd())->m_MainFrameToolBar;
ASSERT(t);
if (!t) 
return FALSE;

// check for click on panning buttons
if (!interrupted)
if (PeekMessage(&msg, t->m_hWnd, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_NOREMOVE))
{
// get the x and y pos of cursor from message
CPoint p(LOWORD(msg.lParam), HIWORD(msg.lParam)); 

static UINT panning_buttons[] = {
ID_MAP_PAN_RIGHT,
ID_MAP_PAN_LEFT,
ID_MAP_PAN_UP,
ID_MAP_PAN_DOWN,
ID_MAP_SCALEIN,
ID_MAP_SCALEOUT,
-1
};

int i = 0;
while (panning_buttons[i] != -1)
{
t->GetItemRect(t->CommandToIndex(ID_MAP_PAN_RIGHT), &rect);
if (rect.PtInRect(p))
{
interrupted = TRUE;
break;
}

i++;
}
}






if (interrupted)     
{
// we've been interrupted

m_user_interrupted_draw=TRUE;  
}
} 


result = m_user_interrupted_draw;
}

return result;
}
// end of is_draw_interrupted

*/

// ********************************************************************************************
// ********************************************************************************************

int CUtil::median_cut( unsigned int *histogram, int num_colors, unsigned char *red,
   unsigned char *green, unsigned char *blue,
   unsigned char *histogram_indices )
{
   // this function implements the median cut method of color quantization
   // the histogram argument contains the cumulative pixel counts for the 32768
   // colors in the reduced 32k color space. the num_colors argument specifies
   // the number of colors after quantization (must be 1 to 256). the red, 
   // green and blue arrays should be dimensioned [num_colors] and will be
   // loaded with the quantized colors. the histogram_indices argument should
   // be dimensioned [32768] and will contain the indices for the quantized
   // colors for each of the colors in the histogram
   // returns SUCCESS or FAILURE

   int i, color_index, iparent, ichild, num_parents, num_children, generation;
   int num_colors_used;
   unsigned int red_counts[256], green_counts[256], blue_counts[256];
   unsigned int count, half_count;
   rgb_box parent_boxes[128], child_boxes[256];
   unsigned char histogram_red[32768], histogram_green[32768];
   unsigned char histogram_blue[32768];
   unsigned char min_red, max_red, min_green, max_green, min_blue, max_blue;
   unsigned char mid_red, mid_green, mid_blue;
   unsigned char red_range, green_range, blue_range;
   unsigned char median_red, median_green, median_blue;

   // check for invalid number of quantized colors
   if( num_colors < 1 || num_colors > 256 ) 
      return FAILURE;

   // initialize histogram colors and find number of colors used
   num_colors_used = 0;
   for( i = 0; i < 32768; i++ )
   {
      if( histogram[i] != 0 ) num_colors_used++;
      histogram_red[i] = (BYTE) (i >> 10);
      histogram_green[i] = (BYTE) ((i >> 5) & 31);
      histogram_blue[i] = (BYTE) (i & 31);
   }

   // if the number of colors used is <= the palette size, just copy them
   if( num_colors_used <= num_colors )
   {
      color_index = 0;
      for( i = 0; i < 32768; i++ )
      {
         if( histogram[i] == 0 ) 
            continue;
         // convert from 31 to 255 range
         red[color_index] = (unsigned char)( (int)histogram_red[i] *
            255 / 31 );
         green[color_index] = (unsigned char)( (int)histogram_green[i] * 
            255 / 31 );
         blue[color_index] = (unsigned char)( (int)histogram_blue[i] *
            255 / 31 );
         histogram_indices[i] = (BYTE) (color_index);
         color_index++;
      }
      return SUCCESS;
   }

   // create first parent box containing entire color space and all pixels
   num_parents = 1;
   parent_boxes[0].num_colors = 0;
   parent_boxes[0].small_enough = FALSE;
   parent_boxes[0].min_red = 0;
   parent_boxes[0].max_red = 31;
   parent_boxes[0].min_green = 0;
   parent_boxes[0].max_green = 31;
   parent_boxes[0].min_blue = 0;
   parent_boxes[0].max_blue = 31;

   generation = 1;

CREATE_CHILDREN:
   num_children = 0;
   for( iparent = 0; iparent < num_parents; iparent++ )
   {
      // check for limit on number of colors
      if( num_children + num_parents - iparent + 1 > num_colors )
      {
         // copy remaining parents to children
         for( i = iparent; i < num_parents; i++ )
         {
            child_boxes[num_children] = parent_boxes[i];
            num_children++;
         }
         goto QUANTIZE;
      }

      // check for small enough without subdividing
      if( parent_boxes[iparent].small_enough )
      {
         child_boxes[num_children] = parent_boxes[iparent];
         num_children++;
         continue;
      }

      // find max/min red, green, and blue for colors in box
      min_red = 31;
      max_red = 0;
      min_green = 31;
      max_green = 0;
      min_blue = 31;
      max_blue = 0;
      for( i = 0; i < 256; i++ )
         red_counts[i] = green_counts[i] = blue_counts[i] = 0;
      for( i = 0; i < 32768; i++ )
      {
         if( histogram[i] == 0 ) 
            continue;
         if(
            histogram_red[i] >= parent_boxes[iparent].min_red &&
            histogram_red[i] <= parent_boxes[iparent].max_red &&
            histogram_green[i] >= parent_boxes[iparent].min_green &&
            histogram_green[i] <= parent_boxes[iparent].max_green &&
            histogram_blue[i] >= parent_boxes[iparent].min_blue &&
            histogram_blue[i] <= parent_boxes[iparent].max_blue )
         {
            parent_boxes[iparent].num_colors++;
            if( histogram_red[i] < min_red ) 
               min_red = histogram_red[i];
            if( histogram_red[i] > max_red ) 
               max_red = histogram_red[i];
            if( histogram_green[i] < min_green ) 
               min_green = histogram_green[i];
            if( histogram_green[i] > max_green ) 
               max_green = histogram_green[i];
            if( histogram_blue[i] < min_blue ) 
               min_blue = histogram_blue[i];
            if( histogram_blue[i] > max_blue ) 
               max_blue = histogram_blue[i];
            red_counts[histogram_red[i]]++;
            green_counts[histogram_green[i]]++;
            blue_counts[histogram_blue[i]]++;
         }
      }
      parent_boxes[iparent].min_red = min_red;
      parent_boxes[iparent].max_red = max_red;
      parent_boxes[iparent].min_green = min_green;
      parent_boxes[iparent].max_green = max_green;
      parent_boxes[iparent].min_blue = min_blue;
      parent_boxes[iparent].max_blue = max_blue;

      // check for no colors in box -> no children
      if( parent_boxes[iparent].num_colors == 0 )
         continue;

      red_range = (BYTE) (max_red - min_red);
      green_range = (BYTE) (max_green - min_green);
      blue_range = (BYTE) (max_blue - min_blue);

      // check for too small to bother subdividing
      if( red_range < 2 && green_range < 2 && blue_range < 2 )
      {
         child_boxes[num_children] = parent_boxes[iparent];
         num_children++;
         continue;
      }

      // create two children

      // split in red direction
      if( red_range >= green_range && red_range >= blue_range )
      {
         // find median red value
         median_red = 0;
         count = 0;
         for( i = 0; i < 256; i++ )
            count += red_counts[i];
         half_count = count / 2;
         count = 0;
         for( i = 0; i < 256; i++ )
         {
            count += red_counts[i];
            if( count >= half_count )
            {
               median_red = (BYTE) i;
               break;
            }
         }
         mid_red = median_red;
         //         mid_red = (unsigned char)( ( (int)min_red + (int)max_red ) / 2 );
         if( mid_red == max_red ) 
            mid_red--;
         // first child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = mid_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         red_range = (BYTE) (mid_red - min_red);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         // second child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = (BYTE) (mid_red+1);
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         red_range = (BYTE) (max_red - mid_red - 1);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         continue;
      }

      // split in green direction
      if( green_range >= red_range && green_range >= blue_range )
      {
         // find median green value
         median_green = 0;
         count = 0;
         for( i = 0; i < 256; i++ )
            count += green_counts[i];
         half_count = count / 2;
         count = 0;
         for( i = 0; i < 256; i++ )
         {
            count += green_counts[i];
            if( count >= half_count )
            {
               median_green = (BYTE) i;
               break;
            }
         }
         mid_green = median_green;
         //         mid_green = (unsigned char)( ( (int)min_green + (int)max_green ) / 
         //             2 );
         if( mid_green == max_green ) 
            mid_green--;
         // first child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = mid_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         green_range = (BYTE) (mid_green - min_green);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         // second child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = (BYTE) (mid_green+1);
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         green_range = (BYTE) (max_green - mid_green - 1);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         continue;
      }

      // split in blue direction
      if( blue_range >= red_range && blue_range >= green_range )
      {
         // find median blue value
         median_blue = 0;
         count = 0;
         for( i = 0; i < 256; i++ )
            count += blue_counts[i];
         half_count = count / 2;
         count = 0;
         for( i = 0; i < 256; i++ )
         {
            count += blue_counts[i];
            if( count >= half_count )
            {
               median_blue = (BYTE) i;
               break;
            }
         }
         mid_blue = median_blue;
         //         mid_blue = (unsigned char)( ( (int)min_blue + (int)max_blue ) / 2 );
         if( mid_blue == max_blue ) 
            mid_blue--;
         // first child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = mid_blue;
         blue_range = (BYTE) (mid_blue - min_blue);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         // second child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = (BYTE) (mid_blue+1);
         child_boxes[num_children].max_blue = max_blue;
         blue_range = (BYTE) (max_blue - mid_blue - 1);
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         continue;
      }
   }

   generation++;
   if( generation == 9 )
      goto QUANTIZE;
   else
   {
      // make parents of children
      for( i = 0; i < num_children; i++ )
         parent_boxes[i] = child_boxes[i];
      num_parents = num_children;
      goto CREATE_CHILDREN;
   }

QUANTIZE:
   // find midpoint of each child box as the color map color
   for( i = 0; i < num_children; i++ )
   {
      red[i] = (unsigned char)( ( (int)child_boxes[i].min_red +
         (int)child_boxes[i].max_red ) / 2 );
      green[i] = (unsigned char)( ( (int)child_boxes[i].min_green +
         (int)child_boxes[i].max_green ) / 2 );
      blue[i] = (unsigned char)( ( (int)child_boxes[i].min_blue +
         (int)child_boxes[i].max_blue ) / 2 );
      // convert from 31 to 255 range
      red[i] = (unsigned char)( (int)red[i] * 255 / 31 );
      green[i] = (unsigned char)( (int)green[i] * 255 / 31 );
      blue[i] = (unsigned char)( (int)blue[i] * 255 / 31 );
   }

   // find the child box for each color
   for( i = 0; i < 32768; i++ )
   {
      histogram_indices[i] = 0;
      if( histogram[i] == 0 ) 
         continue;
      for( ichild = 0; ichild < num_children; ichild++ )
      {
         if(
            histogram_red[i] >= child_boxes[ichild].min_red &&
            histogram_red[i] <= child_boxes[ichild].max_red &&
            histogram_green[i] >= child_boxes[ichild].min_green &&
            histogram_green[i] <= child_boxes[ichild].max_green &&
            histogram_blue[i] >= child_boxes[ichild].min_blue &&
            histogram_blue[i] <= child_boxes[ichild].max_blue )
         {
            histogram_indices[i] = (BYTE) ichild;
            break;
         }
      }
   }

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

// find the bounding box from corner coords
int CUtil::get_geo_bounds(double lat1, double lon1, double lat2, double lon2, 
   double lat3, double lon3, double lat4, double lon4,
   double *ullat, double *ullon, double *lrlat, double *lrlon)
{
   double lat[4], lon[4];
   int k;

   lat[0] = lat1;
   lat[1] = lat2;
   lat[2] = lat3;
   lat[3] = lat4;
   lon[0] = lon1;
   lon[1] = lon2;
   lon[2] = lon3;
   lon[3] = lon4;

   *ullat = *lrlat = *ullon = *lrlon = 0.0;

   // check validity of inputs
   for (k=0; k<4; k++)
   {
      if (!is_valid_geo(lat[k], lon[k]))
         return FAILURE;
   }

   *ullat = *lrlat = lat[0];
   *ullon = *lrlon = lon[0];

   for (k=1; k<4; k++)
   {
      if (*ullat < lat[k])
         *ullat = lat[k];
      if (*lrlat > lat[k])
         *lrlat = lat[k];
      if (geo_east_of(*ullon, lon[k]))
         *ullon = lon[k];
      if (geo_east_of(lon[k], *lrlon))
         *lrlon = lon[k];
   }

   return SUCCESS;
}

// ********************************************************************************************
// ********************************************************************************************

BOOL CUtil::georect_intersect(double ullat1, double ullon1, double lrlat1, double lrlon1,
   double ullat2, double ullon2, double lrlat2, double lrlon2)
{
   if ((lrlat1 > ullat2) || (lrlat2 > ullat1))
      return FALSE;
   if (geo_east_of(ullon1, lrlon2)) 
      return FALSE; 
   if (geo_east_of(ullon2, lrlon1)) 
      return FALSE;

   return TRUE;
}

// ********************************************************************************************
// ********************************************************************************************

double CUtil::calc_dot_prod(float Left, float Right, float Up, 
   float meters_lat_per_pixel, float meters_lon_per_pixel,
   float light_dir_x, float light_dir_y, float light_dir_z,
   float exaggeration_factor)
{
   // See get_image() for full calculations

   float vb_z, vr_z;
   float norm_x, norm_y, norm_z, mag;

   double dot_prod;

   // Make references
   float yb = -meters_lat_per_pixel;
   float xr = meters_lon_per_pixel;
   float &vb_y=yb, &vr_x=xr;


   // Vector from point to point below.
   vb_z = exaggeration_factor * (Up - Left);

   // Vector from point to point to the right.
   vr_z = exaggeration_factor * (Right - Left);

   // Form normal unit vector by cross product.
   norm_x = vb_y*vr_z;
   norm_y = vb_z*vr_x;
   norm_z = - vb_y*vr_x;

   mag = (float)sqrt( norm_x*norm_x + norm_y*norm_y + norm_z*norm_z );
   norm_x /= mag;
   norm_y /= mag;
   norm_z /= mag;

   // Form dot product of normal unit vector and light direction vector to 
   // calculate brightness.
   dot_prod = norm_x*light_dir_x + norm_y*light_dir_y + norm_z*light_dir_z;
   dot_prod = -dot_prod;

   return dot_prod;
}
// end of calc_dot_prod

// ********************************************************************************************
// ********************************************************************************************

file_type_t CUtil::determine_file_type(CString filename)
{
   CFile file;
   const int BUF_LEN = 20;
   char buf[BUF_LEN], tbuf[BUF_LEN];
   CString tstr;
   int rslt;

   rslt = file.Open(filename, CFile::modeRead | CFile::shareDenyNone );
   if (!rslt)
      return FILE_NOT_DEFINED;

   file.Read(&tbuf, 19);
   file.Close();

   strncpy_s(buf, BUF_LEN, tbuf, 2);
   buf[2] = '\0';
   tstr = buf;
   if (!tstr.Compare("BM"))
      return FILE_BMP;

   if (!tstr.Compare("II") || !tstr.Compare("MM"))
      return FILE_TIF;

   strncpy_s(buf, BUF_LEN, tbuf, 3);
   buf[3] = '\0';
   tstr = buf;
   if (!tstr.Compare("GIF"))
      return FILE_GIF;

   strncpy_s(buf+1, BUF_LEN-1, tbuf, 3);
   buf[3] = '\0';
   tstr = buf;
   if (!tstr.Compare("PNG"))
      return FILE_PNG;

   strncpy_s(buf, BUF_LEN, tbuf, 4);
   buf[4] = '\0';
   tstr = buf;
   if (!tstr.Compare("%PDF"))
      return FILE_PDF;

   if (!tstr.Compare("NITF"))
      return FILE_NITF;

   if (!tstr.Compare("msid"))
      return FILE_MRSID;

   strncpy_s(buf+5, BUF_LEN-5, tbuf, 4);
   buf[4] = '\0';
   tstr = buf;
   if (!tstr.Compare("JFIF"))
      return FILE_JPG;

   if ((tbuf[4] == 'j') && (tbuf[5] == 'P'))
      return FILE_J2K;

   // don't know
   return FILE_NOT_DEFINED;  
}
// end of determine_file_type

// ********************************************************************************************
// ********************************************************************************************

#ifdef _WIN32  // read_file: Win32 HANDLE I/O
int CUtil::read_file(HANDLE & file, void * buf, int bytes_to_read)
{
   BOOL rslt;
   DWORD nBytesRead = 0;
   DWORD dwSize = bytes_to_read;

   if (file == INVALID_HANDLE_VALUE)
      return FAILURE;

   rslt = ReadFile( file, buf, dwSize, &nBytesRead, NULL );

   if (!rslt)
   {
      CloseHandle(file);
      file = INVALID_HANDLE_VALUE;
      return FAILURE;
   }

   if (nBytesRead == 0)
      return UTIL_FIL_END_OF_FILE;

   if ( nBytesRead != dwSize )
   {
      CloseHandle(file);
      file = INVALID_HANDLE_VALUE;
      return FAILURE;
   }

   return SUCCESS;
}
#endif  // _WIN32 (read_file)
// end of read_file

// ********************************************************************************************
// ********************************************************************************************

#ifdef _WIN32  // read_string: Win32 HANDLE I/O
int CUtil::read_string(HANDLE & file, char * buf, int & bytes_to_read)
{
   int rslt, pos;
   char ch;

   if (file == INVALID_HANDLE_VALUE)
      return FAILURE;

   rslt = read_file( file, &ch, 1 );
   if (rslt != SUCCESS)
   {
      CloseHandle(file);
      file = INVALID_HANDLE_VALUE;
      return FAILURE;
   }

   pos = 0;
   while ((ch != 0x0a) && (rslt == SUCCESS) && (pos <= bytes_to_read))
   {
      buf[pos] = ch;
      pos++;
      rslt = read_file( file, &ch, 1 );
   }

   buf[pos] = '\0';
   if (buf[pos-1] == 0x0d)
   {
      pos--;
      buf[pos] = '\0';
   }

   bytes_to_read = pos;

   return rslt;
}
#endif  // _WIN32 (read_string)
// end of read_string

// ********************************************************************************************
// ********************************************************************************************

// End of Util.cpp
