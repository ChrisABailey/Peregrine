// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// OsmFrameEnumerator — see fv_osm_format.h.

#include "fv_osm_format.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "fv_mbtiles.h"
#include "fvkit/formats/registry.h"

namespace fs = std::filesystem;

namespace fv {

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool IsMbtiles(const fs::path& p) {
  return Lower(p.extension().string()) == ".mbtiles";
}

}  // namespace

Status OsmFrameEnumerator::Begin(const std::string& dir) {
  frames_.clear();
  next_ = 0;
  skipped_raster_ = 0;
  skipped_unreadable_ = 0;

  std::error_code ec;
  if (!fs::exists(dir, ec) || ec)
    return Status::Error(kNotFound, "OSM dir not found: " + dir);

  std::vector<std::string> files;
  if (fs::is_regular_file(dir, ec) && !ec) {
    if (!IsMbtiles(fs::path(dir)))
      return Status::Error(kUnsupported, dir + " is not an .mbtiles file");
    files.push_back(dir);
  } else {
    fs::recursive_directory_iterator it(
        dir, fs::directory_options::skip_permission_denied, ec),
        end;
    if (ec) return Status::Error(kIoError, "cannot walk " + dir);
    for (; it != end; it.increment(ec)) {
      if (ec) break;
      if (!it->is_regular_file(ec) || ec) continue;
      if (IsMbtiles(it->path())) files.push_back(it->path().string());
    }
  }
  if (files.empty())
    return Status::Error(kUnsupported, "no .mbtiles files under " + dir);

  for (const std::string& path : files) {
    // The file has to be opened: unlike a CADRG frame or an ENC cell, an
    // .mbtiles name says nothing about what is inside it — vector or raster,
    // and which zooms — and the coverage a catalog row needs is derived from
    // the tile index rather than declared (an MBTiles `bounds` value cannot be
    // trusted; O1's finding, and MbtilesFile::Bounds() is the answer).
    MbtilesFile f;
    if (!f.Open(path).ok()) {
      ++skipped_unreadable_;
      continue;
    }
    const std::string& fmt = f.format();
    if (!fmt.empty() && fmt != "pbf" && fmt != "mvt") {
      ++skipped_raster_;
      continue;
    }

    FrameInfo info;
    info.path = path;
    info.bounds = f.Bounds();
    info.series_key = fs::path(path).stem().string();
    if (info.series_key.empty()) info.series_key = "osm";
    info.edition = f.version();
    info.scale = 0.0;  // a pyramid is a scale RANGE — see the header
    info.scale_units = 0;
    info.size_bytes = static_cast<int64_t>(fs::file_size(path, ec));
    if (ec) {
      info.size_bytes = 0;
      ec.clear();
    }
    frames_.push_back(std::move(info));
  }

  if (frames_.empty())
    return Status::Error(kUnsupported,
                         "no readable vector .mbtiles under " + dir);

  std::sort(frames_.begin(), frames_.end(),
            [](const FrameInfo& a, const FrameInfo& b) {
              return a.path < b.path;
            });
  return Status::Ok();
}

bool OsmFrameEnumerator::Next(FrameInfo* info) {
  if (next_ >= frames_.size()) return false;
  *info = frames_[next_++];
  return true;
}

void RegisterOsmFormat() {
  if (FindFormat("osm") != nullptr) return;  // idempotent
  FormatFactories osm;
  osm.format_key = "osm";
  osm.make_enumerator = [] { return std::make_shared<OsmFrameEnumerator>(); };
  RegisterFormat(osm);
}

}  // namespace fv
