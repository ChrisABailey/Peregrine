// Copyright (c) 1994-2009,2014 Georgia Tech Research Corporation, Atlanta, GA
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
//      or project specific include files that are used frequently,
//      but are changed infrequently

#if !defined(AFX_STDAFX_H__6A343685_92B0_4D6D_825A_585E2DB96BEF__INCLUDED_)
#define AFX_STDAFX_H__6A343685_92B0_4D6D_825A_585E2DB96BEF__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifdef _WIN32
#define STRICT
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define _ATL_APARTMENT_THREADED

#include <atlbase.h>
//You may derive a class from CComModule and use it if you want to override
//something, but do not change the name of _Module
extern CComModule _Module;
#include <atlcom.h>
#include <comdef.h>

// STL includes
#include <vector>
#include <algorithm>
#include <istream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <string>
#include <list>

#include <stdio.h>
#include <math.h>

#define PERSISTENT_IMAGELIB   /* Keep ImageLib copy between Render() calls */

#import "MapDataServer.tlb"   no_namespace, named_guids, exclude("IErrorInfo")
#import "GeoTIFFMapServer.tlb"   no_namespace, named_guids, exclude("IErrorInfo")
#import "DatumConvertServer.tlb" no_namespace named_guids
#import "MapRenderingEngine.tlb" no_namespace, named_guids exclude("MapScaleUnitsEnum")
#import "ImageLib.tlb" no_namespace named_guids, exclude("IStream","ISequentialStream","_LARGE_INTEGER","_ULARGE_INTEGER","tagSTATSTG","_FILETIME")
#import "MapDataServerUtil.tlb" no_namespace, named_guids, exclude("IErrorInfo")

#else

#include "fv_compat.h"
#include "fv_map_enums.h"

// STL includes (mirrors the Windows block above)
#include <vector>
#include <algorithm>
#include <istream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <string>
#include <list>

#include <stdio.h>
#include <math.h>

#endif  // _WIN32

#ifdef _WIN32
typedef std::basic_string<TCHAR> tstring;
typedef std::basic_stringstream<TCHAR> tstringstream;

#include <OleDBErr.h>
#else
typedef std::basic_string<char> tstring;
typedef std::basic_stringstream<char> tstringstream;
#endif  // _WIN32


//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STDAFX_H__6A343685_92B0_4D6D_825A_585E2DB96BEF__INCLUDED)
