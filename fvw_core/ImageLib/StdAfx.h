// Copyright (c) 1994-2010 Georgia Tech Research Corporation, Atlanta, GA
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

/***************************************************************************/
// The preprocessor definition IMPLEMENTATION_VARIANT is used with software
// modules that may be compiled in more that one product, as follows:
//    IMPLEMENTATION_VARIANT = 0 is for the full FalconView distributions
//    IMPLEMENTATION_VARIANT = 1 is for JMPS and its offshoots
//    IMPLEMENTATION_VARIANT = 2 is for the public FalconView distributions
/***************************************************************************/


#pragma once

#define __STDAFX_H__

#ifndef WINVER
#  define WINVER 0x0501    /* XP */
#endif

#define STRICT

#ifndef _WIN32_WINNT
//#  define _WIN32_WINNT 0x0501 /* XP */
#endif
#define _ATL_APARTMENT_THREADED

#ifdef _WIN32

#include <afxwin.h>
#include <afxdisp.h>

#include <atlbase.h>
//You may derive a class from CComModule and use it if you want to override
//something, but do not change the name of _Module
extern CComModule _Module;
#include <atlcom.h>
#include <afxpriv.h>
#include <comdef.h>

#ifdef FVW_COMPILE
#  ifdef GOV_RELEASE
#     define IMPLEMENTATION_VARIANT 0     // Full support for 3rd party libraries
#  else
#     define IMPLEMENTATION_VARIANT 2     // No licensed 3rd party libraries
#  endif                                  // IMPLEMENTATION_VARIANT 1 is JMPS 1.3.3
#endif

#ifndef IMPLEMENTATION_VARIANT
   #define IMPLEMENTATION_VARIANT 0
#endif // IMPLEMENTATION_VARIANT

#if IMPLEMENTATION_VARIANT == 0
#  define KAKADU_SUPPORT
#endif   // IMPLEMENTATION_VARIANT == 0

#if IMPLEMENTATION_VARIANT != 1
#  define PDF_SUPPORT      // Until plugin is implemented
#endif   // IMPLEMENTATION_VARIANT != 1

#ifndef NO_MULTITHREADED_OVERVIEW_BUILD
   #define MULTITHREADED_OVERVIEW_BUILD
#endif

#if _MSC_VER >= 1400
   #define RtrnBSTR( bstr ) bstr.Detach()
   #define RtrnBSTRMsg( msg ) _bstr_t( msg ).Detach()
   #define ResetAutoPtr( ap, type, p ) ap.reset( p )
   #define BOOL2VAR
   #define VAR2BOOL
#else
   #define RtrnBSTR( bstr ) bstr.copy()
   #define RtrnBSTRMsg( msg ) _bstr_t( msg ).copy()
   #define ResetAutoPtr( ap, type, p ) ap.reset( p )  // was std::auto_ptr (removed in C++17)
   #define BOOL2VAR (LONG)
   #define VAR2BOOL (LONG)
#endif
#define ResetDOUBLEPtr( ap, p ) ResetAutoPtr( ap, DOUBLE, p );

#include "ImportMsADO15.h" // Ensure correct 32/64 bit version

#import <msxml6.dll> rename("DOMDocument", "_DOMDocument") exclude("ISequentialStream") exclude("_FILETIME")

typedef MSXML2::IXMLDOMDocument2Ptr XMLDocPtr;
typedef MSXML2::IXMLDOMDocument2Ptr XML2DocumentPtr;
typedef MSXML2::IXMLDOMElementPtr   XMLElementPtr;
typedef MSXML2::IXMLDOMNodePtr      XMLNodePtr;
typedef MSXML2::IXMLDOMNodeListPtr  XMLNodeListPtr; 

#import "ImageLib.tlb" no_namespace, named_guids, raw_interfaces_only, \
   exclude("IStream","ISequentialStream","_LARGE_INTEGER","_ULARGE_INTEGER","tagSTATSTG","_FILETIME")
#if defined EXTERNAL_KDULIB
#  import "ImageLibKDU.tlb" no_namespace, named_guids, \
      exclude("IImageLibCallback","IImageLib","EnumMappedImageInterpolationMode","EnumImageLibSpecFuncCode") \
      exclude("EnumImageLibSpecFuncTryCloseImageSourceResult" ), \
      exclude("IStream","ISequentialStream","_LARGE_INTEGER","_ULARGE_INTEGER","tagSTATSTG","_FILETIME")
#endif
#import "NITFDBServer.tlb" no_namespace, named_guids, exclude("EnumImageLibStatus")
#import "MrsidLib.tlb" no_namespace, named_guids
#import "PdfLib.tlb" no_namespace, named_guids
#import "FVDataSources.tlb" no_namespace, named_guids, rename("IFilter", "IFvFilter")
#import "GeodataDataSources.tlb" no_namespace, named_guids

#else

#include "fv_compat.h"
#include "fv_cstring.h"

#define ResetAutoPtr( ap, type, p ) ap.reset( p )
#define ResetDOUBLEPtr( ap, p ) ResetAutoPtr( ap, DOUBLE, p )

// COM types pass through headless code as opaque pointers
struct IImageLibCallback;

// From CustomInterfaces/ImageLib.idl (COM ABI value)
enum { IMAGELIB_ERROR_USER_ABORT = -999 };

#endif  // _WIN32

#include <memory>
// NOTE (port 2026-07-15): std::auto_ptr removed in C++17. These typedefs move
// to fvw_auto_ptr (fv_autoptr.h), a transfer-on-copy stand-in, so the many
// ImageLib call sites that rely on auto_ptr ownership transfer keep compiling
// unchanged on both platforms. See fv_autoptr.h.
#include "fv_autoptr.h"
typedef fvw_auto_ptr< BYTE >             BytePtr;
typedef fvw_auto_ptr< USHORT >           UShortPtr;
typedef fvw_auto_ptr< ULONG >            ULongPtr;
typedef fvw_auto_ptr< UINT >             UIntPtr;
typedef fvw_auto_ptr< FLOAT >            FloatPtr;
typedef fvw_auto_ptr< DOUBLE >           DoublePtr;
typedef struct { BYTE red, green, blue; } RGBTriplet;
typedef fvw_auto_ptr< RGBTriplet >       RGBTripletPtr;

#ifdef _WIN32
#include "ComErrorObject.h"
#endif

#define CLASS_SHARED_VARS     // Share global data in class variables

#ifndef CLASS_SHARED_VARS
   #define Thread __declspec( thread )    /* For thread-local storage */
#endif

#ifdef _WIN32
static const VARIANT VARIANT_NULL = { VT_NULL };
#endif

#if _MSC_VER <= 1400
#  define NULL_PTR NULL
#else
#  define NULL_PTR nullptr
#endif

#ifdef _UNICODE
   #define W2S_FMT _T("%s")   /* Wide string into wide string */
   #define A2S_FMT _T("%S")   /* ANSI to wide string */
   #define W2NS_FMT _T("%.*s")
#else
   #define W2S_FMT _T("%S")   /* Wide string into MBC string */
   #define A2S_FMT _T("%s")   /* ANSI to MBC string */
   #define W2NS_FMT _T("%.*S")
#endif

// End of ImageLib::stdafx.h
