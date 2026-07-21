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



#pragma once

// stdafx.h : include file for standard system include files.

#ifdef __STDAFX_H__
   #error Embedded include of "stdafx.h" is not allowed -- must be in top level compile code (.cpp)
#endif

#define __STDAFX_H__ 1

#pragma warning (disable:4786)

/*
#include <afx.h>
#include <afxdisp.h>
#include <afxtempl.h>
*/
//
// include the Debug Run-Time Library
//
#ifdef _DEBUG

//
// define _CRTDBG_MAP_ALLOC to enable recording of the source and line of
// allocation calls
//
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#endif

#ifndef _WIN32
// POSIX port (2026-07-20): the Vpf/ table reader compiles headless. This is
// the prefix header the Vpf sources actually get (a quoted include resolves
// against the including file's directory first, on MSVC and clang alike), so
// the MFC/Win32 stand-ins belong here, not in VpfMapServer/StdAfx.h. ATL/COM/
// OLEDB stay Windows-only.
#include "fv_compat.h"
#include "fv_cstring.h"          // CString
#include "fv_mfc_containers.h"   // CList/CArray/CMap/CStringList, POSITION
#include "fv_oledatetime.h"      // COleDateTime
#include "fv_win32_filemap.h"    // CreateFile/MapViewOfFile idiom -> mmap
#include "fv_win32_finddata.h"   // FindFirstFile/FindNextFile -> readdir

#include <string>

// Minimal _bstr_t: the Vpf sources use it only to funnel a narrow string into
// WriteToLogFile. The COM BSTR machinery (SysAllocString, wide storage,
// refcounted Data_t) stays Windows-only. Deliberately NOT in fv_compat.h --
// geo3/GEOTRANS.CPP already declares its own `typedef std::string _bstr_t`,
// and two definitions in one TU collide.
class _bstr_t {
 public:
  _bstr_t() {}
  _bstr_t(const char* s) : m_s(s ? s : "") {}
  operator const char*() const { return m_s.c_str(); }

 private:
  std::string m_s;
};

// Headless stub for FalconView's app-level log sink (POSIX policy, as with
// MessageBox in fv_compat.h): log lines go to stderr.
inline void WriteToLogFile(const char* msg) {
  fprintf(stderr, "[log] %s\n", msg ? msg : "");
}

#endif  // !_WIN32
