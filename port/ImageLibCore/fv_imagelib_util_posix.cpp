// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_imagelib_util_posix.cpp - POSIX replacements for the CUtil methods
// guarded _WIN32 in ImageLib/Util.cpp (registry lookups, COM progress
// callbacks, GDI drawing, GetTempPath). Policies per PORTING.md: registry
// reads -> env vars / settable statics; COM progress -> no-op (headless);
// GDI drawing is not replaced (SEVER: Phase 5).

#include "stdafx.h"

#include <cstdlib>

#include "common.h"  // SUCCESS/FAILURE
#include "util.h"

// --- data-path discovery (registry on Windows) ---
CString CUtil::get_default_source() { return s_csDefaultImageryDataPath; }
CString CUtil::get_fif_dir() {
  const char* p = getenv("FVW_FIF_DIR");
  return CString(p ? p : "");
}
CString CUtil::get_default_destination() {
  const char* p = getenv("FVW_DATA_DEST");
  return CString(p ? p : "");
}
CString CUtil::get_registry_string(CString /*value_name*/,
                                   CString default_value) {
  return default_value;  // no preference store headless
}
BOOL CUtil::set_registry_string(CString /*value_name*/, CString /*value*/) {
  return TRUE;  // accepted, not persisted
}
int CUtil::write_registry(HKEY, const char*, const char*, DWORD, const BYTE*,
                          DWORD) {
  return FAILURE;
}
INT CUtil::read_registry(HKEY, LPCSTR, LPCSTR, PDWORD, PBYTE, PDWORD) {
  return FAILURE;
}

CString CUtil::get_temp_path() {
  const char* p = getenv("TMPDIR");
  CString cs(p ? p : "/tmp/");
  if (cs.GetLength() > 0 && cs.GetAt(cs.GetLength() - 1) != '/') cs += '/';
  return cs;
}

CString CUtil::get_data_path() {
  const char* p = getenv("FVW_DATA_PATH");
  CString cs(p ? p : "");
  if (cs.GetLength() > 0 && cs.GetAt(cs.GetLength() - 1) != '/') cs += '/';
  return cs;
}

// stat-based; same "YYYYMMDDhhmmss" format as the Windows original
#include <cerrno>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
int CUtil::get_file_date(CString filename, CString& date,
                         CString& error_msg) {
  struct stat sb;
  if (stat((LPCSTR)filename, &sb) != 0) {
    error_msg = "Unable to get file date";
    return FAILURE;
  }
  struct tm t;
  gmtime_r(&sb.st_mtime, &t);
  date.Format("%04d%02d%02d%02d%02d%02d", t.tm_year + 1900, t.tm_mon + 1,
              t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  return SUCCESS;
}

int CUtil::get_file_size(CString filename, unsigned int* file_size,
                         CString& error_msg) {
  struct stat sb;
  if (stat((LPCSTR)filename, &sb) != 0) {
    error_msg = "Unable to get file size";
    return FAILURE;
  }
  *file_size =
      (unsigned int)(sb.st_size > 0xFFFFFFFFll ? 0xFFFFFFFFu : sb.st_size);
  return SUCCESS;
}

int CUtil::create_directory(const CString& dirname, CString& error_msg) {
  if (mkdir((LPCSTR)dirname, 0777) == 0 || errno == EEXIST) return SUCCESS;
  error_msg = "Unable to create directory";
  return FAILURE;
}

// POSIX access(): directories behave correctly, unlike Windows _access
int CUtil::file_access(const char* path, int mode) {
  int m = 0;  // UTIL_FIL_EXISTS
  if (mode & 2 /*UTIL_FIL_WRITE_OK*/) m |= W_OK;
  if (mode & 4 /*UTIL_FIL_READ_OK*/) m |= R_OK;
  return (strlen(path) && access(path, m) == 0) ? SUCCESS : FAILURE;
}

INT CUtil::get_overview_write_path(const CString& csImageFilespec,
                                   CString& csOverviewPath, BOOL& bDefaultPath,
                                   CString& csErrorMsg) {
  csOverviewPath = extract_path(csImageFilespec);
  bDefaultPath = FALSE;
  if (file_access(csOverviewPath, 2 /*UTIL_FIL_WRITE_OK*/) == SUCCESS)
    return SUCCESS;  // free-space check (GetDiskFreeSpaceEx) not replicated
  csErrorMsg = "Overview path not writeable";
  return FAILURE;
}

BOOL CUtil::create_directory(const CString& dirname) {
  return mkdir((LPCSTR)dirname, 0777) == 0 || errno == EEXIST;
}
double CUtil::get_free_space(CString /*path*/) {
  return 1.0e12;  // headless: assume plenty (Windows queried the volume)
}

// Windows decorates via NITFUtilities (registry-known names); headless uses
// the undecorated path (bDoDecoration was a NITF-DB feature)
VOID CUtil::AddDecoratedOverviewBaseName(const CString& csImageFilespec,
                                         CString& csDecoratedBaseFilespec,
                                         BOOL /*bDoDecoration*/) {
  csDecoratedBaseFilespec = csImageFilespec;
  csDecoratedBaseFilespec.SetAt(csDecoratedBaseFilespec.ReverseFind('.'), '_');
  csDecoratedBaseFilespec += "_ov";
}

// --- COM progress callbacks: headless no-ops (never user-interrupted) ---
BOOL CUtil::user_agrees_to_abort(IImageLibCallback*) { return FALSE; }
BOOL CUtil::escape_pressed(IImageLibCallback*) { return FALSE; }
BOOL CUtil::send_user_update_and_continue(IImageLibCallback* cb,
                                          double percent, LPCSTR label) {
  return send_user_update_and_continue(cb, percent, CString(label));
}
BOOL CUtil::send_user_update_and_continue(IImageLibCallback*, double,
                                          const CString&) {
  if (m_abort_flag) {
    m_abort_flag = FALSE;
    return FALSE;
  }
  return TRUE;
}
void CUtil::clear_callback(IImageLibCallback*) {}

// --- NITF DB metadata lookup (COM NITFDBAgent on Windows) ---
INT CUtil::get_default_imagery_data_paths(CString& csErrorMsg) {
  csErrorMsg = "no MDS database headless";  // documented delta
  return FAILURE;
}
