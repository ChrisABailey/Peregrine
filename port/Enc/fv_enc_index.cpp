// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_enc_index.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "fv_enc_format.h"
#include "fv_s57.h"

namespace fs = std::filesystem;

namespace fv {

const char kEncIndexFileName[] = "chart-index.tsv";

namespace {

constexpr char kHeader[] = "# fv-enc-index 2";

std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    const size_t tab = line.find('\t', start);
    out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos
                                                              : tab - start));
    if (tab == std::string::npos) break;
    start = tab + 1;
  }
  return out;
}

}  // namespace

Status BuildEncIndex(const std::string& root, std::vector<EncIndexEntry>* out,
                     std::vector<std::string>* unreadable,
                     const std::function<bool(size_t, size_t)>& progress) {
  out->clear();
  EncFrameEnumerator cells;
  Status s = cells.Begin(root);
  if (!s.ok() && s.code != kUnsupported) return s;
  std::vector<FrameInfo> frames;
  FrameInfo f;
  while (s.ok() && cells.Next(&f)) frames.push_back(f);

  const fs::path base(root);
  for (size_t i = 0; i < frames.size(); ++i) {
    const FrameInfo& frame = frames[i];
    S57DatasetInfo info;
    S57UpdateState state;
    const S57CellName name = ParseCellName(frame.path);
    bool readable = name.valid && ReadS57DatasetInfo(frame.path, &info).ok();
    if (readable && !FindS57UpdateFiles(frame.path).empty())
      readable = ReadS57UpdateState(frame.path, &state).ok();
    if (!readable) {
      if (unreadable) unreadable->push_back(frame.path);
    } else {
      EncIndexEntry e;
      e.path = fs::path(frame.path).lexically_relative(base).generic_string();
      e.name = fs::path(frame.path).stem().string();
      e.usage_band = name.usage_band;
      e.edition = info.edition;
      e.update_count = state.update_files;
      e.applied_updates = state.applied;
      e.cancelled = state.info.cancelled;
      e.compilation_scale = info.compilation_scale;
      e.bounds = frame.bounds;
      out->push_back(std::move(e));
    }
    if (progress && !progress(i + 1, frames.size()))
      return Status::Error(kInterrupted, "ENC index: cancelled");
  }
  if (out->empty()) return Status::Error(kNotFound, "no readable ENC cells under " + root);
  return Status::Ok();
}

Status WriteEncIndex(const std::string& path, const std::vector<EncIndexEntry>& entries) {
  const std::string tmp = path + ".tmp";
  {
    std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
    if (!os) return Status::Error(kIoError, "ENC index: cannot write " + tmp);
    os << kHeader << "\n";
    os << "# path\tname\tband\tedition\tupdates\tapplied\tcancelled\tcscl\tsouth\twest"
          "\tnorth\teast\n";
    char num[200];
    for (const EncIndexEntry& e : entries) {
      snprintf(num, sizeof(num), "%d\t%s\t%d\t%d\t%d\t%d\t%.9f\t%.9f\t%.9f\t%.9f",
               e.usage_band, e.edition.c_str(), e.update_count, e.applied_updates,
               e.cancelled ? 1 : 0, e.compilation_scale,
               e.bounds.ll.lat, e.bounds.ll.lon, e.bounds.ur.lat, e.bounds.ur.lon);
      os << e.path << '\t' << e.name << '\t' << num << '\n';
    }
    if (!os.flush()) return Status::Error(kIoError, "ENC index: write failed " + tmp);
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) return Status::Error(kIoError, "ENC index: cannot rename to " + path);
  return Status::Ok();
}

Status ReadEncIndex(const std::string& path, std::vector<EncIndexEntry>* out) {
  out->clear();
  std::ifstream is(path, std::ios::binary);
  if (!is) return Status::Error(kNotFound, "ENC index: cannot open " + path);
  std::string line;
  if (!std::getline(is, line) || line != kHeader)
    return Status::Error(kUnsupported, "ENC index: unknown format in " + path);
  size_t line_no = 1;
  while (std::getline(is, line)) {
    ++line_no;
    if (line.empty() || line[0] == '#') continue;
    const std::vector<std::string> c = SplitTabs(line);
    if (c.size() != 12)
      return Status::Error(kIoError, "ENC index: bad row " + std::to_string(line_no) +
                                         " in " + path);
    EncIndexEntry e;
    e.path = c[0];
    e.name = c[1];
    e.usage_band = atoi(c[2].c_str());
    e.edition = c[3];
    e.update_count = atoi(c[4].c_str());
    e.applied_updates = atoi(c[5].c_str());
    e.cancelled = c[6] == "1";
    e.compilation_scale = static_cast<int32_t>(atol(c[7].c_str()));
    e.bounds.ll.lat = strtod(c[8].c_str(), nullptr);
    e.bounds.ll.lon = strtod(c[9].c_str(), nullptr);
    e.bounds.ur.lat = strtod(c[10].c_str(), nullptr);
    e.bounds.ur.lon = strtod(c[11].c_str(), nullptr);
    out->push_back(std::move(e));
  }
  return Status::Ok();
}

}  // namespace fv
