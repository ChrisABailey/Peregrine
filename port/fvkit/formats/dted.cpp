// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// DTED adapter implementation — see fvkit/formats/dted.h.

#include "fvkit/formats/dted.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <system_error>

#include "fv_dted_cell.h"

namespace fs = std::filesystem;

namespace fv {

namespace {

// "<w|e>DDD" (case-insensitive) -> SW-corner longitude. w082 -> -82.
bool ParseLonDir(const std::string& name, int* sw_lon) {
  if (name.size() != 4) return false;
  char h = (char)std::tolower((unsigned char)name[0]);
  if (h != 'e' && h != 'w') return false;
  for (int i = 1; i < 4; ++i)
    if (!std::isdigit((unsigned char)name[i])) return false;
  int v = std::atoi(name.c_str() + 1);
  // SW corners run w180..e179; e180 would put the cell outside (-180, 180]
  if (v > 180 || (h == 'e' && v > 179)) return false;
  *sw_lon = (h == 'w') ? -v : v;
  return true;
}

// "<n|s>DD.dtL" (case-insensitive) -> SW-corner latitude + DTED level.
bool ParseCellFile(const std::string& name, int* sw_lat, int* level) {
  if (name.size() != 7) return false;
  char h = (char)std::tolower((unsigned char)name[0]);
  if (h != 'n' && h != 's') return false;
  if (!std::isdigit((unsigned char)name[1]) ||
      !std::isdigit((unsigned char)name[2]))
    return false;
  if (name[3] != '.' || std::tolower((unsigned char)name[4]) != 'd' ||
      std::tolower((unsigned char)name[5]) != 't')
    return false;
  if (name[6] < '0' || name[6] > '3') return false;
  int v = (name[1] - '0') * 10 + (name[2] - '0');
  if (v > 89) return false;
  *sw_lat = (h == 's') ? -v : v;
  *level = name[6] - '0';
  return true;
}

GeoRect CellBounds(int sw_lat, int sw_lon) {
  return GeoRect{{(double)sw_lat, (double)sw_lon},
                 {(double)sw_lat + 1.0, (double)sw_lon + 1.0}};
}

// Shared scan: one FrameInfo per cell file under root.
Status ScanTree(const std::string& root, std::vector<FrameInfo>* out) {
  std::error_code ec;
  fs::directory_iterator dirs(root, ec);
  if (ec)
    return Status::Error(kNotFound, "DTED root not readable: " + root + " (" +
                                        ec.message() + ")");
  for (const auto& d : dirs) {
    int sw_lon = 0;
    if (!d.is_directory(ec) || ec) continue;
    if (!ParseLonDir(d.path().filename().string(), &sw_lon)) continue;
    fs::directory_iterator files(d.path(), ec);
    if (ec) continue;
    for (const auto& f : files) {
      int sw_lat = 0, level = 0;
      if (!f.is_regular_file(ec) || ec) continue;
      if (!ParseCellFile(f.path().filename().string(), &sw_lat, &level))
        continue;
      FrameInfo info;
      info.path = f.path().string();
      info.bounds = CellBounds(sw_lat, sw_lon);
      info.series_key = "DTED" + std::string(1, (char)('0' + level));
      info.size_bytes = (int64_t)f.file_size(ec);
      if (ec) info.size_bytes = 0;
      out->push_back(std::move(info));
    }
  }
  std::sort(out->begin(), out->end(),
            [](const FrameInfo& a, const FrameInfo& b) { return a.path < b.path; });
  return Status::Ok();
}

}  // namespace

// ---------------------------------------------------------------------------
// DtedFrameEnumerator
// ---------------------------------------------------------------------------

Status DtedFrameEnumerator::Begin(const std::string& dir) {
  frames_.clear();
  next_ = 0;
  return ScanTree(dir, &frames_);
}

bool DtedFrameEnumerator::Next(FrameInfo* info) {
  if (next_ >= frames_.size()) return false;
  *info = frames_[next_++];
  return true;
}

// ---------------------------------------------------------------------------
// DtedElevationSource
// ---------------------------------------------------------------------------

DtedElevationSource::DtedElevationSource(const std::string& root_dir) {
  std::vector<FrameInfo> frames;
  scan_status_ = ScanTree(root_dir, &frames);
  if (!scan_status_.ok()) return;
  for (const FrameInfo& f : frames) {
    int sw_lat = (int)std::lround(f.bounds.ll.lat);
    int sw_lon = (int)std::lround(f.bounds.ll.lon);
    int level = f.series_key.back() - '0';
    cells_[{sw_lat, sw_lon}].push_back(CellRef{level, f.path});
    if (!has_cells_) {
      bounds_ = f.bounds;
      has_cells_ = true;
    } else {
      bounds_.ll.lat = std::min(bounds_.ll.lat, f.bounds.ll.lat);
      bounds_.ll.lon = std::min(bounds_.ll.lon, f.bounds.ll.lon);
      bounds_.ur.lat = std::max(bounds_.ur.lat, f.bounds.ur.lat);
      bounds_.ur.lon = std::max(bounds_.ur.lon, f.bounds.ur.lon);
    }
  }
  for (auto& kv : cells_)  // highest level (finest posts) first
    std::sort(kv.second.begin(), kv.second.end(),
              [](const CellRef& a, const CellRef& b) { return a.level > b.level; });
}

DtedElevationSource::~DtedElevationSource() = default;

GeoRect DtedElevationSource::Bounds() const { return bounds_; }

Status DtedElevationSource::GetElevation(const GeoPoint& p,
                                         float* elevation_meters) {
  if (elevation_meters == nullptr)
    return Status::Error(kInvalidArg, "elevation_meters is null");
  if (!scan_status_.ok()) return scan_status_;

  CellKey key{(int)std::floor(p.lat), (int)std::floor(p.lon)};
  auto refs = cells_.find(key);
  if (refs == cells_.end())
    return Status::Error(kOutOfCoverage, "no DTED cell covers the point");

  auto opened = open_cells_.find(key);
  if (opened == open_cells_.end()) {
    auto cell = std::make_unique<DtedCell>();
    bool ok = false;
    for (const CellRef& ref : refs->second) {
      if (cell->Open(ref.path, (double)key.first, (double)key.second)) {
        ok = true;
        break;
      }
    }
    if (!ok) cell.reset();
    opened = open_cells_.emplace(key, std::move(cell)).first;
  }
  if (opened->second == nullptr)
    return Status::Error(kIoError,
                         "cell file(s) exist but none opened: " +
                             refs->second.front().path);

  long elev = 0;
  if (!opened->second->GetElevation(p.lat, p.lon, DTED_ELEVATION_METERS, elev))
    return Status::Error(kIoError, "elevation read failed in " +
                                       refs->second.front().path);
  if (elev == PARTIAL_DTED_ELEVATION)  // void post -> NaN, Status ok (D4)
    *elevation_meters = std::numeric_limits<float>::quiet_NaN();
  else
    *elevation_meters = (float)elev;
  return Status::Ok();
}

}  // namespace fv
