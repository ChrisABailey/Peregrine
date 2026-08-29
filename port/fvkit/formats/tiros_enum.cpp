// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// TirosFrameEnumerator implementation — see fvkit/formats/tiros.h. C++17 TU:
// only fv::TirosFrame (clean header), never CJpeg.

#include "fvkit/formats/tiros.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "fv_tiros_frame.h"

namespace fs = std::filesystem;

namespace fv {

namespace {

bool HasWldExtension(const fs::path& p) {
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return ext == ".wld";
}

}  // namespace

Status TirosFrameEnumerator::Begin(const std::string& dir) {
  frames_.clear();
  next_ = 0;

  std::error_code ec;
  if (!fs::exists(dir, ec) || ec)
    return Status::Error(kNotFound, "TIROS dir not found: " + dir);

  fv::TirosFrame frame;
  fs::recursive_directory_iterator it(dir, ec), end;
  if (ec) return Status::Error(kNotFound, "TIROS dir not readable: " + dir);
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec) || ec) continue;
    if (!HasWldExtension(it->path())) continue;

    TirosFrameProperties props;
    if (!frame.GetFrameProperties(it->path().string(), &props) ||
        !props.supported || props.whole_world)
      continue;

    FrameInfo info;
    info.path = it->path().string();
    info.bounds = GeoRect{{props.ll_lat, props.ll_lon},
                          {props.ur_lat, props.ur_lon}};
    info.series_key = props.series;
    info.scale = props.scale;
    info.scale_units = props.scale_is_meters ? 4 : 3;  // MAP_SCALE_METERS : _KILOMETER
    info.size_bytes = (int64_t)it->file_size(ec);
    if (ec) info.size_bytes = 0;
    frames_.push_back(std::move(info));
  }

  std::sort(frames_.begin(), frames_.end(),
            [](const FrameInfo& a, const FrameInfo& b) { return a.path < b.path; });
  return Status::Ok();
}

bool TirosFrameEnumerator::Next(FrameInfo* info) {
  if (next_ >= frames_.size()) return false;
  *info = frames_[next_++];
  return true;
}

}  // namespace fv
