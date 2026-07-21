// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_win32_path.h — resolve a Win32-style path on a POSIX filesystem
// (playbook: DEDICATED HEADER).
//
// Two Windows filesystem assumptions are baked into FalconView's readers, and
// both are cheaper to absorb at the file-open boundary than to chase through
// the sources:
//
//   1. Backslash separators. The VPF reader builds paths as
//      m_path + _T("\\") + name + _T("\\cat"). Rewriting every literal would
//      touch shared code for no behavioral gain.
//   2. Case-insensitive lookup. VPF/DNC data mixes cases freely — TestData's
//      dnc17 has lowercase coverage directories (hyd/, por/) holding
//      uppercase tables (CAT, LHT) — and MIL-STD-2407 permits either. macOS
//      is usually case-insensitive so this is invisible here, but on Linux
//      every uppercase table would silently fail to open.
//
// FvResolveWin32Path normalizes separators, then (only if the direct path
// misses) walks the path component by component matching case-insensitively.
// Resolution is per-component, so a hit is a real file — never a guess.

#pragma once

#ifndef _WIN32

#include <dirent.h>
#include <sys/stat.h>

#include <string>

namespace fv {

// Case-insensitive match for a single path component within `dir`.
// Returns the on-disk spelling, or an empty string if nothing matches.
inline std::string MatchComponentIgnoringCase(const std::string& dir,
                                              const std::string& want) {
  DIR* d = ::opendir(dir.empty() ? "." : dir.c_str());
  if (d == nullptr) return std::string();

  std::string found;
  while (struct dirent* e = ::readdir(d)) {
    if (::strcasecmp(e->d_name, want.c_str()) == 0) {
      found = e->d_name;
      break;
    }
  }
  ::closedir(d);
  return found;
}

inline std::string FvResolveWin32Path(const char* win32_path) {
  if (win32_path == nullptr) return std::string();

  std::string path(win32_path);
  for (size_t i = 0; i < path.size(); ++i)
    if (path[i] == '\\') path[i] = '/';

  // Fast path: the normalized name exists as spelled (always true on a
  // case-insensitive filesystem, and on Linux whenever the case happens to
  // match). Everything below is the Linux fallback.
  struct stat sb;
  if (::stat(path.c_str(), &sb) == 0) return path;

  const bool absolute = !path.empty() && path[0] == '/';
  std::string resolved = absolute ? "/" : "";

  size_t pos = absolute ? 1 : 0;
  while (pos <= path.size()) {
    size_t slash = path.find('/', pos);
    if (slash == std::string::npos) slash = path.size();
    const std::string component = path.substr(pos, slash - pos);

    if (component.empty() || component == ".") {
      // keep resolved as-is
    } else {
      std::string candidate = resolved + component;
      if (::stat(candidate.c_str(), &sb) == 0) {
        resolved = candidate;
      } else {
        // Strip the trailing slash for opendir(); "" means the cwd.
        std::string parent = resolved;
        if (parent.size() > 1 && parent[parent.size() - 1] == '/')
          parent.erase(parent.size() - 1);
        const std::string actual = MatchComponentIgnoringCase(parent, component);
        if (actual.empty()) return path;  // no match: report the original
        resolved += actual;
      }
    }

    if (slash == path.size()) break;
    if (resolved.empty() || resolved[resolved.size() - 1] != '/')
      resolved += '/';
    pos = slash + 1;
  }

  return resolved;
}

}  // namespace fv

#endif  // !_WIN32
