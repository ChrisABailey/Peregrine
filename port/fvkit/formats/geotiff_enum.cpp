// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// GeoTiffFrameEnumerator implementation — see fvkit/formats/geotiff.h.
// C++17 TU: only needs fv::GeoTiffFrame (header reads), never CGeoTiff.

#include "fvkit/formats/geotiff.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "fv_geotiff_frame.h"

namespace fs = std::filesystem;

namespace fv {

namespace {

bool HasTiffExtension(const fs::path& p) {
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return ext == ".tif" || ext == ".tiff";
}

// Series names are ASCII ("B&W", "CIR", ...); anything wider becomes '?'.
std::string Narrow(const std::wstring& w) {
  std::string s;
  s.reserve(w.size());
  for (wchar_t c : w) s += (c > 0 && c < 128) ? (char)c : '?';
  return s;
}

}  // namespace

Status GeoTiffFrameEnumerator::Begin(const std::string& dir) {
  frames_.clear();
  next_ = 0;

  std::error_code ec;
  fs::directory_iterator files(dir, ec);
  if (ec)
    return Status::Error(kNotFound, "GeoTIFF dir not readable: " + dir +
                                        " (" + ec.message() + ")");

  std::string dir_with_sep = dir;
  if (!dir_with_sep.empty() && dir_with_sep.back() != '/' &&
      dir_with_sep.back() != '\\')
    dir_with_sep += '/';

  GeoTiffFrame frame;
  for (const auto& f : files) {
    if (!f.is_regular_file(ec) || ec) continue;
    if (!HasTiffExtension(f.path())) continue;
    std::string name = f.path().filename().string();

    GeoTiffFrameProperties props;
    HRESULT hr = frame.GetFrameProperties(dir_with_sep, name, &props);
    if (FAILED(hr) || !props.supported) {
      // Windows fell back to the ImageLib COM object / MrSID here (D6)
      fprintf(stderr, "fvkit geotiff: skipping unsupported frame %s\n",
              f.path().string().c_str());
      continue;
    }

    FrameInfo info;
    info.path = f.path().string();
    info.bounds = GeoRect{{props.ll_lat, props.ll_lon},
                          {props.ur_lat, props.ur_lon}};
    info.series_key = Narrow(props.series);
    if (info.series_key.empty()) info.series_key = "GeoTIFF";
    info.scale = props.scale_denom;
    info.scale_units = (int)props.scale_units;
    info.size_bytes = (int64_t)f.file_size(ec);
    if (ec) info.size_bytes = 0;
    frames_.push_back(std::move(info));
  }

  std::sort(frames_.begin(), frames_.end(),
            [](const FrameInfo& a, const FrameInfo& b) { return a.path < b.path; });
  return Status::Ok();
}

bool GeoTiffFrameEnumerator::Next(FrameInfo* info) {
  if (next_ >= frames_.size()) return false;
  *info = frames_[next_++];
  return true;
}

}  // namespace fv
