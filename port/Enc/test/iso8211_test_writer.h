// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * iso8211_test_writer.h — a minimal ISO 8211 writer for building reader and
 * S-57 fixtures byte by byte. Header-only.
 */
#ifndef FV_ISO8211_TEST_WRITER_H_
#define FV_ISO8211_TEST_WRITER_H_

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace fv_test {

struct FieldDef {
  std::string tag;
  std::string name;
  std::string labels;  // '!'-separated; leading '*' = repeating
  std::string format;  // "(b11,A)"
};

inline void AppendNum(std::string* s, size_t value, int width) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%0*llu", width, static_cast<unsigned long long>(value));
  *s += buf;
}

// Builds one record (DDR when `is_ddr`) from tag/body pairs. `len_width` and
// `pos_width` go into the leader's entry map, so a test can give each record a
// different one — real cells do, which is exactly the trap this catches.
inline std::string BuildRecord(bool is_ddr,
                        const std::vector<std::pair<std::string, std::string>>& fields,
                        int len_width = 3, int pos_width = 4) {
  std::string directory, area;
  for (const auto& f : fields) {
    const std::string body = f.second + std::string(1, 0x1e);  // FT
    directory += f.first;
    AppendNum(&directory, body.size(), len_width);
    AppendNum(&directory, area.size(), pos_width);
    area += body;
  }
  directory += std::string(1, 0x1e);  // end of directory

  const size_t base = 24 + directory.size();
  const size_t total = base + area.size();
  std::string leader;
  AppendNum(&leader, total, 5);
  leader += "3";                 // interchange level
  leader += is_ddr ? "L" : "D";  // leader identifier
  leader += "E";                 // inline code extension
  leader += "1";                 // version
  leader += " ";                 // application indicator
  leader += "09";                // field control length
  AppendNum(&leader, base, 5);
  leader += "   ";  // extended character set indicator
  AppendNum(&leader, static_cast<size_t>(len_width), 1);
  AppendNum(&leader, static_cast<size_t>(pos_width), 1);
  leader += "0";  // reserved
  leader += "4";  // size of field tag
  return leader + directory + area;
}

inline std::string DdrBody(const FieldDef& d) {
  const char ut = 0x1f;
  return "1600;&   " + d.name + ut + d.labels + ut + d.format;
}

// A whole file: DDR built from `defs`, then the data records.
inline std::vector<uint8_t> BuildFile(const std::vector<FieldDef>& defs,
                               const std::vector<std::string>& records) {
  std::vector<std::pair<std::string, std::string>> ddr_fields;
  std::string tree = "0000;&   ";
  ddr_fields.emplace_back("0000", tree);
  for (const FieldDef& d : defs) ddr_fields.emplace_back(d.tag, DdrBody(d));
  std::string out = BuildRecord(true, ddr_fields);
  for (const std::string& r : records) out += r;
  return std::vector<uint8_t>(out.begin(), out.end());
}

inline std::string LE(uint64_t v, int width) {
  std::string s;
  for (int i = 0; i < width; ++i) s += static_cast<char>((v >> (8 * i)) & 0xff);
  return s;
}

}  // namespace fv_test

#endif  // FV_ISO8211_TEST_WRITER_H_
