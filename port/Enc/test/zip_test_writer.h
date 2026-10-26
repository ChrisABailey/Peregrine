// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * zip_test_writer.h — writes small .zip fixtures for tests (stored or raw
 * deflate entries, no zip64). Header-only; links zlib.
 */
#ifndef FV_ZIP_TEST_WRITER_H_
#define FV_ZIP_TEST_WRITER_H_

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace fv_test {

class ZipTestWriter {
 public:
  /** Adds a file entry; `deflate` compresses it with raw deflate. */
  void Add(const std::string& name, const std::string& data, bool deflate = true) {
    Entry e;
    e.name = name;
    e.crc = static_cast<uint32_t>(
        crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(data.data()),
              static_cast<uInt>(data.size())));
    e.size = static_cast<uint32_t>(data.size());
    e.method = deflate ? 8 : 0;
    e.payload = deflate ? Deflate(data) : data;
    entries_.push_back(std::move(e));
  }

  /** Adds a directory entry ("name/"). */
  void AddDirectory(const std::string& name) {
    Entry e;
    e.name = name.back() == '/' ? name : name + "/";
    entries_.push_back(std::move(e));
  }

  /** The finished archive. */
  std::string Bytes() const {
    std::string out, cd;
    for (const Entry& e : entries_) {
      const uint32_t offset = static_cast<uint32_t>(out.size());
      Put32(&out, 0x04034b50);
      Put16(&out, 20);
      Put16(&out, 0);
      Put16(&out, e.method);
      Put32(&out, 0);  // time, date
      Put32(&out, e.crc);
      Put32(&out, static_cast<uint32_t>(e.payload.size()));
      Put32(&out, e.size);
      Put16(&out, static_cast<uint16_t>(e.name.size()));
      Put16(&out, 0);
      out += e.name;
      out += e.payload;

      Put32(&cd, 0x02014b50);
      Put16(&cd, 20);
      Put16(&cd, 20);
      Put16(&cd, 0);
      Put16(&cd, e.method);
      Put32(&cd, 0);
      Put32(&cd, e.crc);
      Put32(&cd, static_cast<uint32_t>(e.payload.size()));
      Put32(&cd, e.size);
      Put16(&cd, static_cast<uint16_t>(e.name.size()));
      Put16(&cd, 0);  // extra
      Put16(&cd, 0);  // comment
      Put16(&cd, 0);  // disk
      Put16(&cd, 0);  // internal attributes
      Put32(&cd, 0);  // external attributes
      Put32(&cd, offset);
      cd += e.name;
    }
    const uint32_t cd_offset = static_cast<uint32_t>(out.size());
    out += cd;
    Put32(&out, 0x06054b50);
    Put16(&out, 0);
    Put16(&out, 0);
    Put16(&out, static_cast<uint16_t>(entries_.size()));
    Put16(&out, static_cast<uint16_t>(entries_.size()));
    Put32(&out, static_cast<uint32_t>(cd.size()));
    Put32(&out, cd_offset);
    Put16(&out, 0);
    return out;
  }

  bool Write(const std::string& path) const {
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    const std::string b = Bytes();
    os.write(b.data(), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(os);
  }

 private:
  struct Entry {
    std::string name;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint32_t size = 0;
    std::string payload;
  };

  static std::string Deflate(const std::string& data) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    std::vector<unsigned char> buf(deflateBound(&zs, static_cast<uLong>(data.size())) + 16);
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    zs.next_out = buf.data();
    zs.avail_out = static_cast<uInt>(buf.size());
    deflate(&zs, Z_FINISH);
    const size_t n = buf.size() - zs.avail_out;
    deflateEnd(&zs);
    return std::string(reinterpret_cast<const char*>(buf.data()), n);
  }

  static void Put16(std::string* s, uint16_t v) {
    s->push_back(static_cast<char>(v & 0xFF));
    s->push_back(static_cast<char>(v >> 8));
  }
  static void Put32(std::string* s, uint32_t v) {
    Put16(s, static_cast<uint16_t>(v & 0xFFFF));
    Put16(s, static_cast<uint16_t>(v >> 16));
  }

  std::vector<Entry> entries_;
};

}  // namespace fv_test

#endif  // FV_ZIP_TEST_WRITER_H_
