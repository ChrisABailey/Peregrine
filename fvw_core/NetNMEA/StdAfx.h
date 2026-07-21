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
//      or project specific include files that are used frequently,
//      but are changed infrequently

#if !defined(AFX_STDAFX_H__78FC6D9E_EC0D_4AAF_9F48_398157387CA5__INCLUDED_)
#define AFX_STDAFX_H__78FC6D9E_EC0D_4AAF_9F48_398157387CA5__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifdef _WIN32

#define STRICT
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0501    /* XP */
#endif
#define _ATL_APARTMENT_THREADED

#include <afxwin.h>
#include <afxdisp.h>

#include <atlbase.h>
//You may derive a class from CComModule and use it if you want to override
//something, but do not change the name of _Module
extern CComModule _Module;
#include <atlcom.h>

#include <afxdisp.h>
#include <afxcmn.h>
#include <afxtempl.h>
#include <afxmt.h>
#include <afxdlgs.h>

#import "fvw.tlb" no_namespace, named_guids

#else

#include "fv_compat.h"
#include "fv_oledatetime.h"

#endif  // _WIN32

// STL
#include <string>
#include <vector>

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STDAFX_H__78FC6D9E_EC0D_4AAF_9F48_398157387CA5__INCLUDED)
