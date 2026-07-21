// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

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
