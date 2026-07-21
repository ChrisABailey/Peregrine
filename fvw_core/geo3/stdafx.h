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

#ifdef _WIN32

#define WINVER 0x0400   // Support Windows 2000 and later

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#include <windows.h>

#else

#include "fv_compat.h"

#endif  // _WIN32

#include <assert.h>