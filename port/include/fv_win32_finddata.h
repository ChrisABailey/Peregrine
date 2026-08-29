// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_win32_finddata.h — POSIX emulation of the Win32 FindFirstFile/
// FindNextFile/FindClose directory walk (playbook: DEDICATED HEADER).
//
// The VPF reader discovers databases, libraries and coverages by walking
// directories with the classic idiom:
//
//     WIN32_FIND_DATA d;
//     for (h = ::FindFirstFile(path + "\\*.*", &d);
//          b && h != INVALID_HANDLE_VALUE;
//          b = ::FindNextFile(h, &d))
//        ... d.cFileName, d.dwFileAttributes ...
//     ::FindClose(h);
//
// Emulating it keeps those loops in shared code. Only the fields the ported
// sources touch are provided (cFileName, dwFileAttributes).
//
// Deviations from Win32, all inherent to the platform:
//   * HIDDEN means a leading dot (the POSIX convention), which is what makes
//     macOS's .DS_Store get skipped exactly like a Windows hidden file.
//     "." and ".." are still returned, unhidden, as Win32 returns them.
//   * Enumeration order is readdir order (Win32's is also unspecified).
//   * The 8.3 alternate name, timestamps and sizes are not filled in; no
//     ported caller reads them.

#pragma once

#ifndef _WIN32

#include <dirent.h>
#include <sys/stat.h>

#include <cstring>
#include <string>

#include "fv_compat.h"
#include "fv_win32_path.h"

#ifndef FILE_ATTRIBUTE_READONLY
#define FILE_ATTRIBUTE_READONLY 0x00000001u
#endif
#ifndef FILE_ATTRIBUTE_HIDDEN
#define FILE_ATTRIBUTE_HIDDEN 0x00000002u
#endif
#ifndef FILE_ATTRIBUTE_DIRECTORY
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#endif
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFFu)
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#endif
#ifndef ERROR_NO_MORE_FILES
#define ERROR_NO_MORE_FILES 18
#endif
#ifndef NO_ERROR
#define NO_ERROR 0
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif

typedef struct _WIN32_FIND_DATA {
  DWORD dwFileAttributes;
  char cFileName[MAX_PATH];
} WIN32_FIND_DATA, *LPWIN32_FIND_DATA;

namespace fv_win32_find_detail {

struct Find {
  DIR* dir;
  std::string directory;  // resolved, no trailing slash
  std::string pattern;    // last component of the Win32 pattern
};

// Win32 wildcard match (case-insensitive, as on NTFS). '*' matches any run,
// '?' matches one char. "*.*" is Win32's "everything", including names with
// no dot at all — special-cased rather than matched literally.
inline bool WildcardMatch(const char* pat, const char* name) {
  if (strcmp(pat, "*.*") == 0 || strcmp(pat, "*") == 0) return true;

  const char* star_pat = nullptr;
  const char* star_name = nullptr;
  while (*name) {
    if (*pat == '?' ||
        (*pat && tolower((unsigned char)*pat) == tolower((unsigned char)*name))) {
      ++pat;
      ++name;
    } else if (*pat == '*') {
      star_pat = pat++;
      star_name = name;
    } else if (star_pat) {
      pat = star_pat + 1;
      name = ++star_name;
    } else {
      return false;
    }
  }
  while (*pat == '*') ++pat;
  return *pat == '\0';
}

// Fill `data` from the next matching entry; false at end of directory.
inline bool NextMatch(Find* f, WIN32_FIND_DATA* data) {
  while (struct dirent* e = ::readdir(f->dir)) {
    if (!WildcardMatch(f->pattern.c_str(), e->d_name)) continue;

    DWORD attrs = 0;
    struct stat sb;
    const std::string full = f->directory + "/" + e->d_name;
    if (::stat(full.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode))
      attrs |= FILE_ATTRIBUTE_DIRECTORY;
    // A leading dot is the POSIX spelling of "hidden"; "." and ".." are not.
    if (e->d_name[0] == '.' && strcmp(e->d_name, ".") != 0 &&
        strcmp(e->d_name, "..") != 0)
      attrs |= FILE_ATTRIBUTE_HIDDEN;

    data->dwFileAttributes = attrs;
    snprintf(data->cFileName, MAX_PATH, "%s", e->d_name);
    return true;
  }
  return false;
}

}  // namespace fv_win32_find_detail

inline HANDLE FindFirstFile(const char* win32_pattern,
                            WIN32_FIND_DATA* data) {
  if (win32_pattern == nullptr || data == nullptr)
    return INVALID_HANDLE_VALUE;

  std::string spec(win32_pattern);
  for (size_t i = 0; i < spec.size(); ++i)
    if (spec[i] == '\\') spec[i] = '/';

  const size_t slash = spec.rfind('/');
  const std::string dir_part =
      (slash == std::string::npos) ? std::string(".") : spec.substr(0, slash);
  const std::string pattern =
      (slash == std::string::npos) ? spec : spec.substr(slash + 1);

  const std::string resolved = fv::FvResolveWin32Path(dir_part.c_str());
  DIR* d = ::opendir(resolved.c_str());
  if (d == nullptr) return INVALID_HANDLE_VALUE;

  fv_win32_find_detail::Find* f = new fv_win32_find_detail::Find();
  f->dir = d;
  f->directory = resolved;
  f->pattern = pattern;

  if (!fv_win32_find_detail::NextMatch(f, data)) {
    ::closedir(d);
    delete f;
    return INVALID_HANDLE_VALUE;  // Win32 sets ERROR_FILE_NOT_FOUND here
  }
  return (HANDLE)f;
}

inline BOOL FindNextFile(HANDLE handle, WIN32_FIND_DATA* data) {
  if (handle == INVALID_HANDLE_VALUE || handle == nullptr || data == nullptr)
    return FALSE;
  fv_win32_find_detail::Find* f = (fv_win32_find_detail::Find*)handle;
  return fv_win32_find_detail::NextMatch(f, data) ? TRUE : FALSE;
}

inline BOOL FindClose(HANDLE handle) {
  if (handle == INVALID_HANDLE_VALUE || handle == nullptr) return FALSE;
  fv_win32_find_detail::Find* f = (fv_win32_find_detail::Find*)handle;
  ::closedir(f->dir);
  delete f;
  return TRUE;
}

// NOTE: GetLastError comes from fv_compat.h (errno). A finished enumeration
// leaves errno 0, not ERROR_NO_MORE_FILES, so a loop that distinguishes
// "no more files" from a real error by GetLastError() sees "no error" here.
// The ported VPF loops only ever treat that as "stop", which is correct.

inline DWORD GetFileAttributes(const char* path) {
  struct stat sb;
  const std::string resolved = fv::FvResolveWin32Path(path);
  if (::stat(resolved.c_str(), &sb) != 0) return INVALID_FILE_ATTRIBUTES;
  DWORD attrs = 0;
  if (S_ISDIR(sb.st_mode)) attrs |= FILE_ATTRIBUTE_DIRECTORY;
  if ((sb.st_mode & S_IWUSR) == 0) attrs |= FILE_ATTRIBUTE_READONLY;
  return attrs ? attrs : FILE_ATTRIBUTE_NORMAL;
}

#endif  // !_WIN32
