// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_filemap.h — read-only memory-mapped file for the FalconView port.
// Replaces CreateFileMapping/MapViewOfFile pairs (playbook: DEDICATED
// HEADER). POSIX implementation maps the whole file; Windows callers used
// sliding 4-32MB windows purely as a 32-bit-era working-set optimization —
// byte content seen by readers is identical.
//
// Windows side unimplemented on purpose: shared sources keep their original
// CreateFileMapping code under #ifdef _WIN32; this class is used only by
// portable twins under port/.

#pragma once

#ifndef _WIN32

#include <cstddef>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fv {

class MappedFile {
 public:
  MappedFile() = default;
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  ~MappedFile() { Close(); }

  // Opens and maps the whole file read-only. Returns false (with the object
  // closed) on any failure.
  bool Open(const std::string& path) {
    Close();
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat sb;
    if (::fstat(fd, &sb) != 0 || sb.st_size <= 0) {
      ::close(fd);
      return false;
    }
    void* p = ::mmap(nullptr, (size_t)sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);  // mapping keeps its own reference
    if (p == MAP_FAILED) return false;
    m_data = static_cast<const unsigned char*>(p);
    m_size = (size_t)sb.st_size;
    return true;
  }

  void Close() {
    if (m_data != nullptr) {
      ::munmap(const_cast<unsigned char*>(m_data), m_size);
      m_data = nullptr;
      m_size = 0;
    }
  }

  bool IsOpen() const { return m_data != nullptr; }
  const unsigned char* Data() const { return m_data; }
  size_t Size() const { return m_size; }

 private:
  const unsigned char* m_data = nullptr;
  size_t m_size = 0;
};

}  // namespace fv

#endif  // !_WIN32
