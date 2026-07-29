// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// EncFrameEnumerator — see fv_enc_format.h.

#include "fv_enc_format.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <system_error>

#include "fv_s57.h"
#include "fvkit/formats/registry.h"

namespace fs = std::filesystem;

namespace fv {

namespace {

std::string Upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return s;
}

// The stem of a CATD FILE field. That field is a DOS-style relative path
// ("ENC_ROOT\US5CHSEC\US5CHSEC.000") whose resolution the reader deliberately
// leaves to the consumer, so cells are matched on the file NAME rather than
// on a path that may not exist as written on a POSIX box.
std::string StemOf(const std::string& path_or_file) {
  size_t cut = path_or_file.find_last_of("/\\");
  std::string name =
      cut == std::string::npos ? path_or_file : path_or_file.substr(cut + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return Upper(name);
}

bool RectIsUsable(const GeoRect& r) {
  return r.ll.lat < r.ur.lat && r.ll.lon < r.ur.lon && r.ll.lat >= -90.0 &&
         r.ur.lat <= 90.0 && r.ll.lon >= -180.0 && r.ur.lon <= 180.0;
}

// Every CATALOG.031 at or below `root`, merged into stem -> bounds. A set may
// carry several: TestData/enc holds four downloads whose catalogues stayed in
// their own ENC_ROOT-N shells, so the root catalogue describes ONE of the four
// cells and the other three are only in theirs. Reading all of them is what
// makes the shortcut cover the whole tree instead of a quarter of it.
std::map<std::string, GeoRect> CatalogBounds(const std::string& root) {
  std::map<std::string, GeoRect> out;
  std::error_code ec;
  fs::recursive_directory_iterator it(
      root, fs::directory_options::skip_permission_denied, ec),
      end;
  if (ec) return out;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec) || ec) continue;
    if (Upper(it->path().filename().string()) != "CATALOG.031") continue;
    std::vector<S57CatalogEntry> entries;
    if (!ReadS57Catalog(it->path().string(), &entries).ok()) continue;
    for (const S57CatalogEntry& e : entries) {
      if (!e.has_bounds || !RectIsUsable(e.bounds)) continue;
      out.emplace(StemOf(e.file), e.bounds);  // first catalogue wins
    }
  }
  return out;
}

}  // namespace

std::string EncBandName(int usage_band) {
  switch (usage_band) {
    case 1: return "Overview";
    case 2: return "General";
    case 3: return "Coastal";
    case 4: return "Approach";
    case 5: return "Harbour";
    case 6: return "Berthing";
    default: return std::string();
  }
}

// Representative compilation scales for the IHO navigational-purpose bands.
// Nominal by construction — see the header for why the cell's own CSCL is not
// used here.
double EncBandNominalScale(int usage_band) {
  switch (usage_band) {
    case 1: return 3000000.0;
    case 2: return 1000000.0;
    case 3: return 300000.0;
    case 4: return 50000.0;
    case 5: return 12000.0;
    case 6: return 4000.0;
    default: return 0.0;
  }
}

Status EncFrameEnumerator::Begin(const std::string& dir) {
  frames_.clear();
  next_ = 0;

  std::error_code ec;
  if (!fs::exists(dir, ec) || ec)
    return Status::Error(kNotFound, "ENC dir not found: " + dir);

  std::vector<std::string> cells;
  Status s = EnumerateEncCells(dir, &cells);
  if (!s.ok()) return s;
  if (cells.empty())
    return Status::Error(kUnsupported, "no S-57 base cells under " + dir);

  // Cheap bounds first; a cell missing from every catalogue is opened.
  const std::map<std::string, GeoRect> catalogued = CatalogBounds(dir);

  for (const std::string& cell_path : cells) {
    const S57CellName name = ParseCellName(cell_path);

    FrameInfo f;
    f.path = cell_path;
    f.series_key = name.valid ? EncBandName(name.usage_band) : std::string();
    if (f.series_key.empty()) f.series_key = "ENC";
    f.scale = name.valid ? EncBandNominalScale(name.usage_band) : 0.0;
    f.scale_units = 0;  // MAP_SCALE_DENOMINATOR
    f.size_bytes = static_cast<int64_t>(fs::file_size(cell_path, ec));
    if (ec) {
      f.size_bytes = 0;
      ec.clear();
    }

    auto hit = catalogued.find(StemOf(cell_path));
    if (hit != catalogued.end()) {
      f.bounds = hit->second;
    } else {
      // No catalogue row: parse the cell for its own extent. Correct but
      // expensive, which is exactly why the catalogue is tried first.
      S57Cell cell;
      if (!cell.Open(cell_path).ok()) continue;  // unreadable cell: skip
      f.bounds = cell.bounds();
      f.edition = cell.info().edition;
      if (!RectIsUsable(f.bounds)) continue;  // a cell with no geometry
    }
    frames_.push_back(std::move(f));
  }

  std::sort(frames_.begin(), frames_.end(),
            [](const FrameInfo& a, const FrameInfo& b) {
              return a.path < b.path;
            });
  return Status::Ok();
}

bool EncFrameEnumerator::Next(FrameInfo* info) {
  if (next_ >= frames_.size()) return false;
  *info = frames_[next_++];
  return true;
}

void RegisterEncFormat() {
  if (FindFormat("enc") != nullptr) return;  // idempotent
  FormatFactories enc;
  enc.format_key = "enc";
  enc.make_enumerator = [] { return std::make_shared<EncFrameEnumerator>(); };
  // No raster or elevation surface: ENC is vector data drawn through
  // EncVectorSource + S52StyleEngine + VectorRenderer, same as VPF.
  RegisterFormat(enc);
}

}  // namespace fv
