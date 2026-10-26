// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * enc_dump.cpp — prints every feature of an S-57 cell as one tab-separated
 * line, for port/tools/enc_update_diff.py to compare with GDAL's reading.
 *
 * Usage: fv_enc_dump [--base-only] <cell.000>
 * Columns: AGEN:FIDN:FIDS, OBJL, PRIM, attributes as ATTL=value joined by
 * U+001F, vertices as lat,lon[,depth] joined by ';' over every part.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include "fv_s57.h"

namespace {

/** `s` with tabs, line breaks and the separator replaced by spaces. */
std::string Clean(const std::string& s) {
  std::string out = s;
  for (char& c : out)
    if (c == '\t' || c == '\n' || c == '\r' || c == '\x1f') c = ' ';
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  fv::S57Updates updates = fv::S57Updates::kApply;
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--base-only") == 0) updates = fv::S57Updates::kBaseOnly;
    else path = argv[i];
  }
  if (path == nullptr) {
    std::fprintf(stderr, "usage: fv_enc_dump [--base-only] <cell.000>\n");
    return 2;
  }
  fv::S57Cell cell;
  const fv::Status s = cell.Open(path, updates);
  if (!s.ok()) {
    std::fprintf(stderr, "%s\n", s.message.c_str());
    return 1;
  }
  if (!cell.unapplied_updates().empty() && updates == fv::S57Updates::kApply)
    std::fprintf(stderr, "%s\n", cell.StalenessWarning().c_str());

  std::string line;
  char num[96];
  for (const fv::S57Feature& f : cell.features()) {
    std::snprintf(num, sizeof(num), "%d:%u:%d\t%d\t%d\t", f.object_id.agency,
                  f.object_id.feature_id, f.object_id.subdivision, f.object_class,
                  static_cast<int>(f.primitive));
    line = num;
    bool first = true;
    for (const fv::S57Attribute& a : f.attributes) {
      if (!first) line += '\x1f';
      first = false;
      line += std::to_string(a.code) + "=" + Clean(a.value);
    }
    line += '\t';
    first = true;
    for (const auto& part : f.geometry.parts)
      for (const fv::S57Vertex& v : part) {
        if (!first) line += ';';
        first = false;
        if (v.has_depth)
          std::snprintf(num, sizeof(num), "%.9f,%.9f,%.3f", v.lat, v.lon, v.depth);
        else
          std::snprintf(num, sizeof(num), "%.9f,%.9f", v.lat, v.lon);
        line += num;
      }
    std::printf("%s\n", line.c_str());
  }
  return 0;
}
