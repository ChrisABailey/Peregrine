// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_compat.h — minimal portability shim for the FalconView cross-platform port.
//
// Rules (see port/PORTING.md):
//  * Prefer rewriting code to standard C++ over adding to this header.
//  * Add a mapping here only when a construct is too widespread to rewrite in
//    one module-sized session (mechanical-risk rule).
//  * The active body is inside #ifndef _WIN32, so Windows builds never see
//    it; implementations may use modern C++/POSIX freely.

#pragma once

#ifndef _WIN32

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <strings.h>  // strcasecmp / strncasecmp

#include "fv_win32_path.h"  // fv::FvResolveWin32Path (backslashes, case)

// MSVC fixed-width integer spellings. A macro (not typedef) because MSVC's
// __int64 is a keyword that combines with 'unsigned'.
#define __int64 long long

// Core Win32 typedefs (subset; grow only on demand).
// Win32 LONG/DWORD are exactly 32 bits; on LP64 'long' is 64 bits, which
// breaks sign-dependent code (FAILED(hr), wraparound), so use fixed-width.
typedef int BOOL;
typedef unsigned char BYTE;
typedef char CHAR;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int INT;
typedef unsigned int UINT;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef char* LPTSTR;
typedef const char* LPCTSTR;
typedef unsigned short USHORT;
typedef unsigned short WORD;
typedef float FLOAT;
typedef double DOUBLE;
typedef void* HANDLE;
typedef void VOID;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef unsigned char UCHAR;
typedef unsigned char* PUCHAR;
typedef BYTE* PBYTE;
typedef char* PCHAR;
typedef short SHORT;
typedef short* PSHORT;
typedef unsigned short* PUSHORT;
typedef unsigned int* PUINT;
typedef uint64_t* PULONGLONG;
typedef intptr_t INT_PTR;
typedef int* PINT;
typedef ULONG* PULONG;
typedef DWORD* PDWORD;
typedef DWORD COLORREF;
typedef void* HKEY;
typedef const void* LPCRECT;
// COLORREF helpers
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | (((DWORD)(BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)(((c) >> 8) & 0xFF))
#define GetBValue(c) ((BYTE)(((c) >> 16) & 0xFF))

// GetSystemInfo subset (page size / allocation granularity)
#include <unistd.h>
typedef struct _SYSTEM_INFO {
  DWORD dwPageSize;
  DWORD dwAllocationGranularity;
} SYSTEM_INFO;
inline void GetSystemInfo(SYSTEM_INFO* si) {
  long ps = sysconf(_SC_PAGESIZE);
  si->dwPageSize = (DWORD)(ps > 0 ? ps : 4096);
  si->dwAllocationGranularity = si->dwPageSize;
}

// GDI types: opaque or minimal — headless code passes pointers through or
// uses the plain geometry structs
class CDC;
class CWnd;
inline CWnd* AfxGetMainWnd() { return nullptr; }

// GlobalMemoryStatus subset (available physical memory)
typedef struct _MEMORYSTATUS {
  DWORD dwLength;
  DWORD dwMemoryLoad;
  size_t dwTotalPhys;
  size_t dwAvailPhys;
  size_t dwTotalPageFile;
  size_t dwAvailPageFile;
  size_t dwTotalVirtual;
  size_t dwAvailVirtual;
} MEMORYSTATUS;
inline void GlobalMemoryStatus(MEMORYSTATUS* ms) {
  long pages = sysconf(_SC_PHYS_PAGES);
  long psize = sysconf(_SC_PAGESIZE);
  size_t total = (pages > 0 && psize > 0) ? (size_t)pages * (size_t)psize : 0;
  ms->dwTotalPhys = total;
  ms->dwAvailPhys = total / 2;  // conservative; Windows reported true avail
}
class CBrush;
class CRgn;
typedef struct tagPOINT { LONG x; LONG y; } POINT;
typedef struct tagRECT { LONG left; LONG top; LONG right; LONG bottom; } RECT;
typedef struct tagSIZE { LONG cx; LONG cy; } SIZE;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

// SAL annotations
#define __in
#define __out
#define __inout
#define __in_opt
#define __out_opt

// 16-bit segmented-memory keywords, empty on Win32 too (windows.h does this)
#define far
#define near
#define FAR
#define NEAR

// MFC-style assert
#include <cassert>
#ifndef ASSERT
#define ASSERT assert
#endif
#ifndef ATLASSERT
#define ATLASSERT assert
#endif

// MSVC CRT string functions
// _tcsupr_s: MBCS build, uppercases a buffer in place (size arg unused here)
inline int _tcsupr_s(char* s_, size_t /*size*/) {
  for (char* p = s_; *p; ++p)
    *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
  return 0;
}
// Code pages: the port is a narrow (ANSI) build, so CP_ACP is the only one
// that appears; conversions collapse to copies.
#ifndef CP_ACP
#define CP_ACP 0
#endif
#define _access access
// _taccess: narrow build, and Win32-style paths need resolving first.
inline int _taccess(const char* path, int mode) {
  return ::access(fv::FvResolveWin32Path(path).c_str(), mode);
}
#define _stricmp strcasecmp
// _tcs* : narrow (MBCS) build, so the TCHAR forms are the char forms.
#define _tcscmp strcmp
#define _tcsicmp strcasecmp
#define _tcsncmp strncmp
#define _tcslen strlen
#define _stprintf_s snprintf
#define _strnicmp strncasecmp
#define _snprintf snprintf

// MSVC "secure" CRT variants. The size argument maps onto the standard
// bounded functions; behavior on truncation differs (MSVC aborts, these
// truncate) but no caller in ported code relies on that.
#define sprintf_s snprintf
inline int strcpy_s(char* dst, size_t size, const char* src) {
  if (dst == nullptr || src == nullptr || size == 0) return 22;  // EINVAL
  snprintf(dst, size, "%s", src);
  return 0;
}
template <size_t N>
inline int strcpy_s(char (&dst)[N], const char* src) {
  return strcpy_s(dst, N, src);
}
// _tcscpy_s: narrow (MBCS) build, so it is strcpy_s — like _tcsupr_s above.
#define _tcscpy_s strcpy_s

// Path conventions
#ifndef MAX_PATH
#define MAX_PATH 1024
#endif

// Bounded copy of at most `count` chars plus a null terminator (C11 Annex K
// truncation semantics are NOT emulated; result is always null-terminated).
inline int strncpy_s(char* dst, size_t size, const char* src, size_t count) {
  if (dst == nullptr || size == 0) return 22;  // EINVAL
  if (src == nullptr) {
    dst[0] = '\0';
    return 22;
  }
  size_t n = strlen(src);
  if (n > count) n = count;
  if (n >= size) n = size - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
  return 0;
}
template <size_t N>
inline int strncpy_s(char (&dst)[N], const char* src, size_t count) {
  return strncpy_s(dst, N, src, count);
}

inline int strcat_s(char* dst, size_t size, const char* src) {
  size_t a = strlen(dst);
  if (a >= size) return 34;  // ERANGE
  snprintf(dst + a, size - a, "%s", src);
  return 0;
}
template <size_t N>
inline int strcat_s(char (&dst)[N], const char* src) {
  return strcat_s(dst, N, src);
}

// Win32 limit macros (32-bit LONG semantics preserved)
#ifndef MAXLONG
#define MAXLONG ((LONG)0x7fffffff)
#endif

#define ZeroMemory(dst, len) memset((dst), 0, (len))

// DIB byte layouts (file-format data, not GDI — playbook "DIB structs").
// Field widths/packing must match Windows exactly.
#define WINAPI
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER {
  WORD bfType;
  DWORD bfSize;
  WORD bfReserved1;
  WORD bfReserved2;
  DWORD bfOffBits;
} BITMAPFILEHEADER;
#pragma pack(pop)
typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  int32_t biWidth;
  int32_t biHeight;
  WORD biPlanes;
  WORD biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  int32_t biXPelsPerMeter;
  int32_t biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER;
typedef DWORD* LPDWORD;
#pragma pack(push, 2)
typedef struct tagBITMAPCOREHEADER {
  DWORD bcSize;
  WORD bcWidth;
  WORD bcHeight;
  WORD bcPlanes;
  WORD bcBitCount;
} BITMAPCOREHEADER, *LPBITMAPCOREHEADER;
#pragma pack(pop)
#pragma pack(push, 1)
typedef struct tagRGBTRIPLE {
  BYTE rgbtBlue;
  BYTE rgbtGreen;
  BYTE rgbtRed;
} RGBTRIPLE;
#pragma pack(pop)
typedef struct tagRGBQUAD {
  BYTE rgbBlue;
  BYTE rgbGreen;
  BYTE rgbRed;
  BYTE rgbReserved;
} RGBQUAD;
typedef struct tagBITMAPINFO {
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO;
#define FillMemory(dst, len, fill) memset((dst), (fill), (len))

// _itoa_s (radix 10 only — assert otherwise)
inline int _itoa_s(int value, char* buf, size_t size, int radix) {
  if (radix != 10) return 22;
  snprintf(buf, size, "%d", value);
  return 0;
}
template <size_t N>
inline int _itoa_s(int value, char (&buf)[N], int radix) {
  return _itoa_s(value, buf, N, radix);
}

// MSVC min/max macros
#ifndef __max
#define __max(a, b) (((a) > (b)) ? (a) : (b))
#define __min(a, b) (((a) < (b)) ? (a) : (b))
#endif

// CRITICAL_SECTION -> recursive pthread mutex (matches Win32 reentrancy)
typedef pthread_mutex_t CRITICAL_SECTION;
inline void InitializeCriticalSection(CRITICAL_SECTION* cs) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(cs, &attr);
  pthread_mutexattr_destroy(&attr);
}
inline void DeleteCriticalSection(CRITICAL_SECTION* cs) {
  pthread_mutex_destroy(cs);
}
inline void EnterCriticalSection(CRITICAL_SECTION* cs) {
  pthread_mutex_lock(cs);
}
inline void LeaveCriticalSection(CRITICAL_SECTION* cs) {
  pthread_mutex_unlock(cs);
}
inline DWORD GetCurrentThreadId() {
  return static_cast<DWORD>(reinterpret_cast<uintptr_t>(pthread_self()));
}

// user32 string helpers
inline LPSTR CharUpper(LPSTR s) {
  for (char* p = s; *p; ++p)
    *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
  return s;
}

#define _ftelli64 ftello
#define _fseeki64 fseeko

// MFC exception classes: minimal throwable stubs (catch sites call Delete())
class CException {
 public:
  virtual ~CException() {}
  void Delete() { delete this; }
};
class COleException : public CException {};
class CMemoryException : public CException {};
class CFileException : public CException {};

// Minimal MFC CFile (grow on demand; currently only dead locals need it)
class CFile {
 public:
  enum OpenFlags {
    modeRead = 0x0000,
    modeWrite = 0x0001,
    modeReadWrite = 0x0002,
    shareCompat = 0x0000,
    shareExclusive = 0x0010,
    shareDenyWrite = 0x0020,
    shareDenyRead = 0x0030,
    shareDenyNone = 0x0040,
    typeBinary = 0x8000
  };
  CFile() : m_fp(nullptr) {}
  ~CFile() { Close(); }
  BOOL Open(const char* path, UINT flags) {
    Close();
    // Win32-style paths (backslashes, any case) resolve to the real file.
    const std::string resolved = fv::FvResolveWin32Path(path);
    m_fp = fopen(resolved.c_str(),
                 (flags & (modeWrite | modeReadWrite)) ? "r+b" : "rb");
    return m_fp != nullptr;
  }
  UINT Read(void* buf, UINT count) {
    return m_fp ? (UINT)fread(buf, 1, count, m_fp) : 0;
  }
  void Close() {
    if (m_fp) {
      fclose(m_fp);
      m_fp = nullptr;
    }
  }

 private:
  FILE* m_fp;
};

// GetTickCount: milliseconds since boot as 32-bit DWORD (wraps ~49.7 days,
// preserved semantics)
#include <time.h>
inline DWORD GetTickCount() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (DWORD)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

inline void OutputDebugString(const char* s) { fprintf(stderr, "%s", s); }

inline BOOL DeleteFile(const char* path) { return remove(path) == 0; }
inline BOOL MoveFile(const char* from, const char* to) {
  return rename(from, to) == 0;
}
#include <sys/stat.h>
inline BOOL CreateDirectory(const char* path, void* /*sa*/) {
  return mkdir(path, 0777) == 0;
}

inline int fopen_s(FILE** fp, const char* path, const char* mode) {
  if (fp == nullptr) return 22;  // EINVAL
  *fp = fopen(path, mode);
  return *fp == nullptr ? 2 : 0;  // ENOENT-ish nonzero on failure
}

// Headless stub: message boxes become stderr lines, "OK" is returned.
#define MB_OK 0x0000
#define MB_TASKMODAL 0x2000
#define IDOK 1
inline int MessageBox(void* /*hwnd*/, const char* text, const char* caption,
                      unsigned /*type*/) {
  fprintf(stderr, "[%s] %s\n", caption ? caption : "", text ? text : "");
  return IDOK;
}

// _T / trace macros
#define _T(x) x
typedef char TCHAR;    // narrow builds only, like the rest of this shim
#ifndef ATLTRACE
#define ATLTRACE(...) ((void)0)
#endif
#ifndef TRACE
#define TRACE(...) ((void)0)
#endif

#include <cerrno>
inline DWORD GetLastError() { return (DWORD)errno; }
inline void SetLastError(DWORD e) { errno = (int)e; }

inline int AfxMessageBox(const char* text, unsigned /*type*/ = 0,
                         unsigned /*help*/ = 0) {
  fprintf(stderr, "[AfxMessageBox] %s\n", text ? text : "");
  return 1;  // IDOK
}

// _controlfp_s: only the rounding-control bits are emulated (all FalconView
// call sites touch _MCW_RC only); other mask bits are ignored.
#include <cfenv>
#define _MCW_RC 0x00030000
#define _RC_NEAR 0x00000000
#define _RC_DOWN 0x01000000
#define _RC_UP 0x02000000
#define _RC_CHOP 0x03000000
inline int _controlfp_s(unsigned int* current, unsigned int new_value,
                        unsigned int mask) {
  unsigned int prev;
  switch (fegetround()) {
    case FE_DOWNWARD: prev = _RC_DOWN; break;
    case FE_UPWARD: prev = _RC_UP; break;
    case FE_TOWARDZERO: prev = _RC_CHOP; break;
    default: prev = _RC_NEAR; break;
  }
  if (current != nullptr) *current = prev;
  if (mask != 0) {
    switch (new_value & (_RC_DOWN | _RC_UP | _RC_CHOP)) {
      case _RC_DOWN: fesetround(FE_DOWNWARD); break;
      case _RC_UP: fesetround(FE_UPWARD); break;
      case _RC_CHOP: fesetround(FE_TOWARDZERO); break;
      default: fesetround(FE_TONEAREST); break;
    }
  }
  return 0;
}

#include "fv_sscanf_s.h"

// HRESULT-shaped code that is not worth rewriting yet.
// Add only when a module actually needs it. Must be exactly 32 bits so
// error codes (0x8xxxxxxx) are negative and FAILED() works — see above.
typedef int32_t HRESULT;
#ifndef S_OK
#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif

#ifndef HRESULT_FROM_WIN32
#define HRESULT_FROM_WIN32(x) \
  ((HRESULT)(x) <= 0 ? (HRESULT)(x) \
                     : (HRESULT)(((x)&0x0000FFFF) | (7 << 16) | 0x80000000))
#endif

// FalconView COM error-handling macros (Common/ComErrorHandler.h): POSIX
// equivalents preserving the control flow — THROW_ERROR_MSG raises, the
// CATCH blocks convert to an HRESULT with the high bit forced on (matching
// `err.Error() | 0x80000000` in the original).
struct fv_com_error {
  HRESULT hr;
};
#define TRY_BLOCK try
#define THROW_ERROR_MSG(hr_, fn_, msg_)                          \
  {                                                              \
    fprintf(stderr, "[%s] %s\n", (const char*)(fn_),            \
            (const char*)(msg_));                                \
    throw fv_com_error{(HRESULT)(hr_)};                          \
  }
#define THROW_ERROR_MSG2(hr_, msg_) THROW_ERROR_MSG(hr_, "", msg_)
#define CATCH_BLOCK_RET                     \
  catch (fv_com_error & err) {              \
    return (HRESULT)(err.hr | 0x80000000);  \
  }
#define CATCH_BLOCK_ALL_RET(src)           \
  CATCH_BLOCK_RET                          \
  catch (...) { return (HRESULT)(E_FAIL | 0x80000000); }

#endif  // !_WIN32
