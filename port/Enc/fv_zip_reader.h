// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * fv_zip_reader.h — a read-only .zip archive reader on zlib.
 *
 * Reads the central directory (zip64 included) and extracts stored and
 * deflated entries with a CRC-32 check. Encrypted entries and other
 * compression methods are reported as kUnsupported.
 */
#ifndef FV_ZIP_READER_H_
#define FV_ZIP_READER_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {

/** One central-directory record. */
struct ZipEntry {
  std::string name;  // as stored, '/'-separated
  uint16_t method = 0;  // 0 stored, 8 deflate
  uint16_t flags = 0;
  uint32_t crc32 = 0;
  uint64_t compressed_size = 0;
  uint64_t uncompressed_size = 0;
  uint64_t local_header_offset = 0;

  bool is_directory() const { return !name.empty() && name.back() == '/'; }
};

/** True when `name` is relative and has no ".." or empty components, so it
 *  cannot be extracted outside the destination directory. */
bool IsSafeZipPath(const std::string& name);

class ZipReader {
 public:
  ZipReader() = default;
  ~ZipReader();
  ZipReader(const ZipReader&) = delete;
  ZipReader& operator=(const ZipReader&) = delete;

  /** Opens `path` and reads its central directory. */
  Status Open(const std::string& path);
  void Close();

  const std::vector<ZipEntry>& entries() const { return entries_; }
  /** Sum of the uncompressed sizes of every entry. */
  uint64_t total_uncompressed_size() const;

  /** Writes `entry` to `dest_path` (created or truncated). A short or
   *  corrupt stream, or a CRC mismatch, is kIoError and removes the file. */
  Status Extract(const ZipEntry& entry, const std::string& dest_path);

  /** Extracts every entry under `dest_dir`, creating directories. An unsafe
   *  name aborts with kInvalidArg before anything is written. `progress`
   *  gets uncompressed bytes done and the total after each file; returning
   *  false cancels with kInterrupted. */
  Status ExtractAll(const std::string& dest_dir,
                    const std::function<bool(uint64_t, uint64_t)>& progress = {});

 private:
  Status ReadCentralDirectory();

  std::string path_;
  void* file_ = nullptr;  // FILE*
  uint64_t file_size_ = 0;
  std::vector<ZipEntry> entries_;
};

}  // namespace fv

#endif  // FV_ZIP_READER_H_
