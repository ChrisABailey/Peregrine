// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_win32_filemap.h — POSIX emulation of the Win32 read-only whole-file
// mapping idiom (playbook: DEDICATED HEADER).
//
// FalconView's VPF reader maps table and index files instead of reading them
// ("file mapping is over twice as fast as using a CFile" — vpfrcset.h), always
// with the same six calls in the same order:
//
//     h   = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
//                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
//     sz  = GetFileSize(h, NULL);
//     m   = CreateFileMapping(h, NULL, PAGE_READONLY, 0, 0, NULL);
//     ptr = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);   // whole file
//     ... UnmapViewOfFile(ptr); CloseHandle(m); CloseHandle(h);
//
// Emulating those six keeps the shared sources (vpfrcset.cpp, indexes.cpp)
// unmodified rather than forking their open paths. Only this idiom is
// supported: read-only, whole file, offset 0. Other flag combinations are
// asserted against, not silently reinterpreted.
//
// Unlike fv_filemap.h (RAII, used by new port/ code), this deliberately
// exposes the raw Win32 shapes because the call sites are legacy shared code.

#pragma once

#ifndef _WIN32

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>

#include "fv_compat.h"
#include "fv_win32_path.h"

// Win32 constants used by the idiom above. Values match <winnt.h> so that a
// site comparing/ORing them behaves identically on both platforms.
#ifndef GENERIC_READ
#define GENERIC_READ 0x80000000u
#endif
#ifndef FILE_SHARE_READ
#define FILE_SHARE_READ 0x00000001u
#endif
#ifndef OPEN_EXISTING
#define OPEN_EXISTING 3
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#endif
#ifndef PAGE_READONLY
#define PAGE_READONLY 0x02u
#endif
#ifndef FILE_MAP_READ
#define FILE_MAP_READ 0x0004u
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif
#ifndef INVALID_FILE_SIZE
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFFu)
#endif

namespace fv_win32_filemap_detail {

// Both a file HANDLE and a mapping HANDLE are this struct; the mapping one
// owns nothing (POSIX mmap needs only the fd at map time), so CloseHandle on
// either order works, as on Windows.
struct Handle {
  int fd;
  size_t size;
  bool owns_fd;
};

// MapViewOfFile hands back a bare pointer, but munmap needs the length, so
// keep the lengths aside. Maps are few (one per open VPF table).
inline std::mutex& view_mutex() {
  static std::mutex m;
  return m;
}
inline std::map<void*, size_t>& views() {
  static std::map<void*, size_t> v;
  return v;
}

}  // namespace fv_win32_filemap_detail

inline HANDLE CreateFile(const char* path, DWORD access, DWORD /*share*/,
                         void* /*security*/, DWORD creation, DWORD /*flags*/,
                         HANDLE /*template_file*/) {
  assert(access == GENERIC_READ && creation == OPEN_EXISTING &&
         "fv_win32_filemap emulates read-only opens of existing files only");
  (void)access;
  (void)creation;

  const std::string resolved = fv::FvResolveWin32Path(path);
  int fd = ::open(resolved.c_str(), O_RDONLY);
  if (fd < 0) return INVALID_HANDLE_VALUE;

  struct stat sb;
  if (::fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode)) {
    ::close(fd);
    return INVALID_HANDLE_VALUE;
  }

  fv_win32_filemap_detail::Handle* h = new fv_win32_filemap_detail::Handle();
  h->fd = fd;
  h->size = (size_t)sb.st_size;
  h->owns_fd = true;
  return (HANDLE)h;
}

inline DWORD GetFileSize(HANDLE handle, DWORD* high_order) {
  if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
    return INVALID_FILE_SIZE;
  const fv_win32_filemap_detail::Handle* h =
      (const fv_win32_filemap_detail::Handle*)handle;
  // VPF tables are far below 4 GB; the high word is always 0, as on Win32.
  if (high_order) *high_order = (DWORD)(h->size >> 32);
  return (DWORD)(h->size & 0xFFFFFFFFu);
}

inline HANDLE CreateFileMapping(HANDLE file, void* /*security*/,
                                DWORD protect, DWORD size_high, DWORD size_low,
                                const char* /*name*/) {
  assert(protect == PAGE_READONLY && size_high == 0 && size_low == 0 &&
         "fv_win32_filemap emulates PAGE_READONLY whole-file mappings only");
  (void)protect;
  (void)size_high;
  (void)size_low;

  if (file == INVALID_HANDLE_VALUE || file == nullptr) return nullptr;
  const fv_win32_filemap_detail::Handle* f =
      (const fv_win32_filemap_detail::Handle*)file;

  fv_win32_filemap_detail::Handle* m = new fv_win32_filemap_detail::Handle();
  m->fd = f->fd;  // borrowed: the mapping handle never closes it
  m->size = f->size;
  m->owns_fd = false;
  return (HANDLE)m;
}

inline void* MapViewOfFile(HANDLE mapping, DWORD access, DWORD offset_high,
                           DWORD offset_low, size_t bytes) {
  assert(access == FILE_MAP_READ && offset_high == 0 && offset_low == 0 &&
         bytes == 0 && "fv_win32_filemap maps whole files from offset 0 only");
  (void)access;
  (void)offset_high;
  (void)offset_low;
  (void)bytes;

  if (mapping == INVALID_HANDLE_VALUE || mapping == nullptr) return nullptr;
  const fv_win32_filemap_detail::Handle* m =
      (const fv_win32_filemap_detail::Handle*)mapping;
  if (m->size == 0) return nullptr;  // Win32 fails on zero-length mappings too

  void* p = ::mmap(nullptr, m->size, PROT_READ, MAP_SHARED, m->fd, 0);
  if (p == MAP_FAILED) return nullptr;

  std::lock_guard<std::mutex> lock(fv_win32_filemap_detail::view_mutex());
  fv_win32_filemap_detail::views()[p] = m->size;
  return p;
}

inline bool UnmapViewOfFile(const void* base) {
  if (base == nullptr) return false;
  void* p = const_cast<void*>(base);
  size_t size = 0;
  {
    std::lock_guard<std::mutex> lock(fv_win32_filemap_detail::view_mutex());
    std::map<void*, size_t>& v = fv_win32_filemap_detail::views();
    std::map<void*, size_t>::iterator it = v.find(p);
    if (it == v.end()) return false;
    size = it->second;
    v.erase(it);
  }
  return ::munmap(p, size) == 0;
}

inline bool CloseHandle(HANDLE handle) {
  if (handle == INVALID_HANDLE_VALUE || handle == nullptr) return false;
  fv_win32_filemap_detail::Handle* h =
      (fv_win32_filemap_detail::Handle*)handle;
  if (h->owns_fd) ::close(h->fd);
  delete h;
  return true;
}

#endif  // !_WIN32
