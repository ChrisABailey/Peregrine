// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// CadrgFrameEnumerator implementation — see fvkit/formats/cadrg.h. C++17 TU:
// only fv::CadrgFrame (clean header), never the decoder.

#include "fvkit/formats/cadrg.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "fv_cadrg_frame.h"

namespace fs = std::filesystem;

namespace fv {

namespace {

// RPF frame extension: dot + 2 alpha (series) + 1 zone char (1-9 or a-j).
bool LooksLikeRpfFrame(const fs::path& p) {
  std::string ext = p.extension().string();  // includes the dot
  if (ext.size() != 4) return false;
  char a = (char)std::tolower((unsigned char)ext[1]);
  char b = (char)std::tolower((unsigned char)ext[2]);
  char z = (char)std::tolower((unsigned char)ext[3]);
  if (a < 'a' || a > 'z' || b < 'a' || b > 'z') return false;
  return (z >= '1' && z <= '9') || (z >= 'a' && z <= 'j');
}

}  // namespace

Status CadrgFrameEnumerator::Begin(const std::string& dir) {
  frames_.clear();
  next_ = 0;

  std::error_code ec;
  if (!fs::exists(dir, ec) || ec)
    return Status::Error(kNotFound, "CADRG dir not found: " + dir);

  fv::CadrgFrame frame;
  fs::recursive_directory_iterator it(dir, ec), end;
  if (ec) return Status::Error(kNotFound, "CADRG dir not readable: " + dir);
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec) || ec) continue;
    if (!LooksLikeRpfFrame(it->path())) continue;

    CadrgFrameProperties props;
    if (!frame.GetFrameProperties(it->path().string(), &props) ||
        !props.supported)
      continue;  // unknown series/zone: skip (as Windows would fall through)

    FrameInfo info;
    info.path = it->path().string();
    info.bounds = GeoRect{{props.ll_lat, props.ll_lon},
                          {props.ur_lat, props.ur_lon}};
    info.series_key = props.series_name;
    info.scale = props.scale_denom;
    info.scale_units = 0;  // MAP_SCALE_DENOMINATOR
    info.size_bytes = (int64_t)it->file_size(ec);
    if (ec) info.size_bytes = 0;
    frames_.push_back(std::move(info));
  }

  std::sort(frames_.begin(), frames_.end(),
            [](const FrameInfo& a, const FrameInfo& b) { return a.path < b.path; });
  return Status::Ok();
}

bool CadrgFrameEnumerator::Next(FrameInfo* info) {
  if (next_ >= frames_.size()) return false;
  *info = frames_[next_++];
  return true;
}

}  // namespace fv
