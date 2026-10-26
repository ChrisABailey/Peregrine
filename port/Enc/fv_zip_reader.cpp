// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_zip_reader.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace fv {

namespace {

constexpr uint32_t kEocdSig = 0x06054b50;
constexpr uint32_t kEocd64Sig = 0x06064b50;
constexpr uint32_t kEocd64LocatorSig = 0x07064b50;
constexpr uint32_t kCentralSig = 0x02014b50;
constexpr uint32_t kLocalSig = 0x04034b50;
constexpr size_t kEocdSize = 22;
constexpr size_t kMaxComment = 0xFFFF;
constexpr size_t kChunk = 256 * 1024;

FILE* AsFile(void* f) { return static_cast<FILE*>(f); }

bool Seek(FILE* f, uint64_t pos) {
#ifdef _WIN32
  return _fseeki64(f, static_cast<__int64>(pos), SEEK_SET) == 0;
#else
  return fseeko(f, static_cast<off_t>(pos), SEEK_SET) == 0;
#endif
}

bool ReadAt(FILE* f, uint64_t pos, void* out, size_t n) {
  return Seek(f, pos) && fread(out, 1, n, f) == n;
}

uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t U64(const uint8_t* p) {
  return static_cast<uint64_t>(U32(p)) | (static_cast<uint64_t>(U32(p + 4)) << 32);
}

Status Corrupt(const std::string& path, const std::string& what) {
  return Status::Error(kIoError, "zip: " + what + " in " + path);
}

/// Replaces 0xFFFFFFFF sizes and offset with the zip64 extended-information
/// extra field values, which appear in that fixed order when present.
void ApplyZip64Extra(const uint8_t* extra, size_t len, ZipEntry* e) {
  size_t i = 0;
  while (i + 4 <= len) {
    const uint16_t id = U16(extra + i);
    const uint16_t size = U16(extra + i + 2);
    const uint8_t* d = extra + i + 4;
    if (i + 4 + size > len) return;
    if (id == 0x0001) {
      size_t k = 0;
      if (e->uncompressed_size == 0xFFFFFFFFu && k + 8 <= size) {
        e->uncompressed_size = U64(d + k);
        k += 8;
      }
      if (e->compressed_size == 0xFFFFFFFFu && k + 8 <= size) {
        e->compressed_size = U64(d + k);
        k += 8;
      }
      if (e->local_header_offset == 0xFFFFFFFFu && k + 8 <= size)
        e->local_header_offset = U64(d + k);
      return;
    }
    i += 4 + size;
  }
}

}  // namespace

bool IsSafeZipPath(const std::string& name) {
  if (name.empty() || name[0] == '/' || name[0] == '\\') return false;
  if (name.find('\\') != std::string::npos) return false;
  if (name.size() >= 2 && name[1] == ':') return false;  // drive letter
  std::stringstream ss(name);
  std::string part;
  bool any = false;
  while (std::getline(ss, part, '/')) {
    if (part.empty() || part == "." || part == "..") return false;
    any = true;
  }
  return any;
}

ZipReader::~ZipReader() { Close(); }

void ZipReader::Close() {
  if (file_ != nullptr) fclose(AsFile(file_));
  file_ = nullptr;
  file_size_ = 0;
  entries_.clear();
}

uint64_t ZipReader::total_uncompressed_size() const {
  uint64_t n = 0;
  for (const ZipEntry& e : entries_) n += e.uncompressed_size;
  return n;
}

Status ZipReader::Open(const std::string& path) {
  Close();
  path_ = path;
  FILE* f = fopen(path.c_str(), "rb");
  if (f == nullptr) return Status::Error(kNotFound, "zip: cannot open " + path);
  file_ = f;
  std::error_code ec;
  file_size_ = fs::file_size(path, ec);
  if (ec || file_size_ < kEocdSize) {
    Close();
    return Corrupt(path, "no end of central directory");
  }
  const Status s = ReadCentralDirectory();
  if (!s.ok()) Close();
  return s;
}

Status ZipReader::ReadCentralDirectory() {
  FILE* f = AsFile(file_);
  // The end record sits within the last 22 + 65535 bytes (trailing comment).
  const uint64_t tail = std::min<uint64_t>(file_size_, kEocdSize + kMaxComment);
  std::vector<uint8_t> buf(static_cast<size_t>(tail));
  if (!ReadAt(f, file_size_ - tail, buf.data(), buf.size()))
    return Corrupt(path_, "short read");
  size_t eocd = std::string::npos;
  for (size_t i = buf.size() - kEocdSize + 1; i-- > 0;) {
    if (U32(&buf[i]) == kEocdSig) {
      eocd = i;
      break;
    }
  }
  if (eocd == std::string::npos) return Corrupt(path_, "no end of central directory");
  const uint64_t eocd_pos = file_size_ - tail + eocd;
  const uint8_t* e = &buf[eocd];
  uint64_t count = U16(e + 10);
  uint64_t cd_size = U32(e + 12);
  uint64_t cd_offset = U32(e + 16);

  if (count == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_offset == 0xFFFFFFFFu) {
    uint8_t loc[20];
    if (eocd_pos < sizeof(loc) || !ReadAt(f, eocd_pos - sizeof(loc), loc, sizeof(loc)) ||
        U32(loc) != kEocd64LocatorSig)
      return Corrupt(path_, "missing zip64 locator");
    uint8_t e64[56];
    if (!ReadAt(f, U64(loc + 8), e64, sizeof(e64)) || U32(e64) != kEocd64Sig)
      return Corrupt(path_, "bad zip64 end record");
    count = U64(e64 + 32);
    cd_size = U64(e64 + 40);
    cd_offset = U64(e64 + 48);
  }
  if (cd_offset + cd_size > file_size_) return Corrupt(path_, "central directory past end");

  std::vector<uint8_t> cd(static_cast<size_t>(cd_size));
  if (!cd.empty() && !ReadAt(f, cd_offset, cd.data(), cd.size()))
    return Corrupt(path_, "short central directory");
  size_t p = 0;
  entries_.reserve(static_cast<size_t>(std::min<uint64_t>(count, 1 << 20)));
  for (uint64_t n = 0; n < count; ++n) {
    if (p + 46 > cd.size() || U32(&cd[p]) != kCentralSig)
      return Corrupt(path_, "bad central directory record");
    const uint8_t* r = &cd[p];
    ZipEntry z;
    z.flags = U16(r + 8);
    z.method = U16(r + 10);
    z.crc32 = U32(r + 16);
    z.compressed_size = U32(r + 20);
    z.uncompressed_size = U32(r + 24);
    const size_t name_len = U16(r + 28);
    const size_t extra_len = U16(r + 30);
    const size_t comment_len = U16(r + 32);
    z.local_header_offset = U32(r + 42);
    if (p + 46 + name_len + extra_len + comment_len > cd.size())
      return Corrupt(path_, "central directory record past end");
    z.name.assign(reinterpret_cast<const char*>(r + 46), name_len);
    ApplyZip64Extra(r + 46 + name_len, extra_len, &z);
    entries_.push_back(std::move(z));
    p += 46 + name_len + extra_len + comment_len;
  }
  return Status::Ok();
}

Status ZipReader::Extract(const ZipEntry& entry, const std::string& dest_path) {
  if (file_ == nullptr) return Status::Error(kInvalidArg, "zip: not open");
  if (entry.flags & 0x0001)
    return Status::Error(kUnsupported, "zip: " + entry.name + " is encrypted");
  if (entry.method != 0 && entry.method != 8)
    return Status::Error(kUnsupported, "zip: " + entry.name + " uses method " +
                                           std::to_string(entry.method));
  FILE* f = AsFile(file_);
  uint8_t local[30];
  if (!ReadAt(f, entry.local_header_offset, local, sizeof(local)) ||
      U32(local) != kLocalSig)
    return Corrupt(path_, "bad local header for " + entry.name);
  const uint64_t data_pos =
      entry.local_header_offset + 30 + U16(local + 26) + U16(local + 28);
  if (data_pos + entry.compressed_size > file_size_)
    return Corrupt(path_, entry.name + " runs past the end");
  if (!Seek(f, data_pos)) return Corrupt(path_, "seek failed");

  FILE* out = fopen(dest_path.c_str(), "wb");
  if (out == nullptr) return Status::Error(kIoError, "zip: cannot write " + dest_path);
  auto fail = [&](Status s) {
    fclose(out);
    std::error_code ec;
    fs::remove(dest_path, ec);
    return s;
  };

  std::vector<uint8_t> in(kChunk), buf(kChunk);
  uint64_t remaining = entry.compressed_size;
  uint64_t written = 0;
  uLong crc = crc32(0L, Z_NULL, 0);

  if (entry.method == 0) {
    while (remaining > 0) {
      const size_t n = static_cast<size_t>(std::min<uint64_t>(remaining, kChunk));
      if (fread(in.data(), 1, n, f) != n) return fail(Corrupt(path_, "short read"));
      crc = crc32(crc, in.data(), static_cast<uInt>(n));
      if (fwrite(in.data(), 1, n, out) != n)
        return fail(Status::Error(kIoError, "zip: write failed " + dest_path));
      remaining -= n;
      written += n;
    }
  } else {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)  // raw deflate, no zlib header
      return fail(Status::Error(kInternal, "zip: inflateInit2 failed"));
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
      if (zs.avail_in == 0) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(remaining, kChunk));
        if (n == 0) break;
        if (fread(in.data(), 1, n, f) != n) {
          inflateEnd(&zs);
          return fail(Corrupt(path_, "short read"));
        }
        remaining -= n;
        zs.next_in = in.data();
        zs.avail_in = static_cast<uInt>(n);
      }
      zs.next_out = buf.data();
      zs.avail_out = static_cast<uInt>(buf.size());
      rc = inflate(&zs, Z_NO_FLUSH);
      if (rc != Z_OK && rc != Z_STREAM_END) {
        inflateEnd(&zs);
        return fail(Corrupt(path_, "deflate error in " + entry.name));
      }
      const size_t n = buf.size() - zs.avail_out;
      crc = crc32(crc, buf.data(), static_cast<uInt>(n));
      if (fwrite(buf.data(), 1, n, out) != n) {
        inflateEnd(&zs);
        return fail(Status::Error(kIoError, "zip: write failed " + dest_path));
      }
      written += n;
    }
    inflateEnd(&zs);
    if (rc != Z_STREAM_END) return fail(Corrupt(path_, entry.name + " is truncated"));
  }
  if (written != entry.uncompressed_size)
    return fail(Corrupt(path_, "size mismatch in " + entry.name));
  if (static_cast<uint32_t>(crc) != entry.crc32)
    return fail(Corrupt(path_, "CRC mismatch in " + entry.name));
  if (fclose(out) != 0) {
    std::error_code ec;
    fs::remove(dest_path, ec);
    return Status::Error(kIoError, "zip: write failed " + dest_path);
  }
  return Status::Ok();
}

Status ZipReader::ExtractAll(const std::string& dest_dir,
                             const std::function<bool(uint64_t, uint64_t)>& progress) {
  for (const ZipEntry& e : entries_) {
    if (!IsSafeZipPath(e.is_directory() ? e.name.substr(0, e.name.size() - 1) : e.name))
      return Status::Error(kInvalidArg, "zip: unsafe entry name " + e.name);
  }
  const uint64_t total = total_uncompressed_size();
  uint64_t done = 0;
  std::error_code ec;
  for (const ZipEntry& e : entries_) {
    const fs::path target = fs::path(dest_dir) / fs::path(e.name);
    if (e.is_directory()) {
      fs::create_directories(target, ec);
      if (ec) return Status::Error(kIoError, "zip: cannot create " + target.string());
      continue;
    }
    fs::create_directories(target.parent_path(), ec);
    if (ec) return Status::Error(kIoError, "zip: cannot create " + target.parent_path().string());
    const Status s = Extract(e, target.string());
    if (!s.ok()) return s;
    done += e.uncompressed_size;
    if (progress && !progress(done, total))
      return Status::Error(kInterrupted, "zip: cancelled");
  }
  return Status::Ok();
}

}  // namespace fv
