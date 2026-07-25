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

// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#if !defined(AFX_STDAFX_H__3F91F209_4E7C_434E_BC6B_80DE7C9D1627__INCLUDED_)
#define AFX_STDAFX_H__3F91F209_4E7C_434E_BC6B_80DE7C9D1627__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifdef _WIN32
// Modify the following defines if you have to target a platform prior to the ones specified below.
// Refer to MSDN for the latest info on corresponding values for different platforms.
#ifndef WINVER  // Allow use of features specific to Windows XP or later.
#define WINVER 0x0501  // Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_WINNT  // Allow use of features specific to Windows XP or later.                   
#define _WIN32_WINNT 0x0501  // Change this to the appropriate value to target other versions of Windows.
#endif

#define _ATL_APARTMENT_THREADED

#define VC_EXTRALEAN  // Exclude rarely-used stuff from Windows headers

//#include <afx.h>
#include <afxwin.h>
#include <afxdlgs.h>
#include <afxdisp.h>  // MFC OLE automation classes
#include <afxcmn.h>  // MFC common controls classes
#include <afxmt.h>  // MFC multi-threading control
#include <afxtempl.h>  // MFC templates and collections

#include <atlbase.h>
//You may derive a class from CComModule and use it if you want to override
//something, but do not change the name of _Module
extern CComModule _Module;
#include <atlcom.h>
#include <comdef.h>
//#include <atldbcli.h>

// This is needed for advanced graphics
#include <wingdi.h>
//#pragma comment(lib, "msimg32.lib")

#import "VPFDataRenderServer.tlb" no_namespace, named_guids, raw_interfaces_only
#import "GeoSymServer.tlb" no_namespace, named_guids, raw_interfaces_only

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#else  // !_WIN32
// POSIX port (2026-07-21, vpf-geosym plan phase V3): the GeoSym rule-table
// layer (DelimitedParser, AttributeExpressions, SymColors) compiles headless.
// This is the prefix header those sources get (a quoted "stdafx.h" resolves
// against the including file's directory first). The GDI symbol-drawing files
// (SanSymbol, SymText, SymLayeredDisplay, ...) stay Windows-only; ATL/COM/GDI
// headers above are skipped.
//
// 2026-07-23 (phase V4): CGMFile.cpp joins the POSIX build — its CGM element
// PARSER is portable, only the Draw/pen/brush/font half is severed.
#include "fv_compat.h"           // CRT/Win32 shims incl. CStdioFile, strtok_s
#include "fv_cstring.h"          // CString
#include "fv_mfc_containers.h"   // CList/CArray/CMap, POSITION
#include "fv_win32_filemap.h"    // CreateFile/MapViewOfFile idiom -> mmap
                                 // (CCGMFile::LoadCGM maps the .cgm file)

// Win32 min/max macros (windef.h). GeoSym's SymColors.cpp uses them; scoped
// to this TU so it never shadows std::min/std::max elsewhere.
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#endif  // _WIN32

#endif // !defined(AFX_STDAFX_H__3F91F209_4E7C_434E_BC6B_80DE7C9D1627__INCLUDED_)
