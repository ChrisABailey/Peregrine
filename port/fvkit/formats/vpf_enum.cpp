// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// VpfFrameEnumerator — see fvkit/formats/vpf.h. Bridges the ported VPF
// reader (CString/MFC world) to FvKit's std::string FrameInfo at the call
// boundary, exactly as geotiff_enum bridges CGeoTiffFrame.

#include "fvkit/formats/vpf.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

// Ported VPF reader (fv_vpf). Bring the same POSIX shims Vpf/StdAfx.h feeds
// the reader sources (that header self-guards against use outside a .cpp), so
// the VPF headers see CString/CMap/CStringList/POSITION/COleDateTime.
#include "fv_compat.h"
#include "fv_cstring.h"
#include "fv_mfc_containers.h"
#include "fv_oledatetime.h"  // vpf_d.h needs COleDateTime defined
#include "vpfdb.h"
#include "vpf_d.h"

namespace fs = std::filesystem;

namespace fv {

namespace {

// FrameInfo.path locator: '|' separates fields (VPF names never contain it).
constexpr char kSep = '|';

GeoRect RectFrom(const d_geo_rect_t& r) {
  return GeoRect{{r.ll.lat, r.ll.lon}, {r.ur.lat, r.ur.lon}};
}

// A VPF database root has a database header table 'dht' (the definitive
// marker; DNC discovers libraries from it, and 'lat' may be absent). Guard so
// scanning an arbitrary directory is a clean skip, not a reader crash.
bool LooksLikeVpfDatabase(const std::string& dir) {
  std::error_code ec;
  fs::directory_iterator it(dir, ec), end;
  if (ec) return false;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    std::string name = it->path().filename().string();
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    if (name == "dht" || name == "dht.") return true;
  }
  return false;
}

}  // namespace

std::string MakeVpfLocator(const std::string& db_root,
                           const std::string& library,
                           const std::string& tile) {
  return db_root + kSep + library + kSep + tile;
}

bool ParseVpfLocator(const std::string& locator, std::string* db_root,
                     std::string* library, std::string* tile) {
  size_t a = locator.find(kSep);
  if (a == std::string::npos) return false;
  size_t b = locator.find(kSep, a + 1);
  if (b == std::string::npos) return false;
  if (db_root) *db_root = locator.substr(0, a);
  if (library) *library = locator.substr(a + 1, b - a - 1);
  if (tile) *tile = locator.substr(b + 1);
  return true;
}

Status VpfFrameEnumerator::Begin(const std::string& dir) {
  frames_.clear();
  next_ = 0;

  std::error_code ec;
  if (!fs::exists(dir, ec) || ec)
    return Status::Error(kNotFound, "VPF dir not found: " + dir);
  if (!LooksLikeVpfDatabase(dir))
    return Status::Error(kUnsupported, "not a VPF database (no dht/lat): " + dir);

  // Reader wants the database name (last path component) and the root path.
  std::string db_name = fs::path(dir).filename().string();
  if (db_name.empty()) db_name = "vpf";
  std::string root_with_slash = dir;
  if (!root_with_slash.empty() && root_with_slash.back() != '/' &&
      root_with_slash.back() != '\\')
    root_with_slash += '\\';  // VPFLibrary composes path + name + '\\'

  VPFDatabase db(CString(db_name.c_str()));
  if (!db.open(CString(dir.c_str())))
    return Status::Error(kIoError, "VPFDatabase::open failed: " + dir);

  const CStringList* names = db.get_library_names();
  if (names == nullptr)
    return Status::Error(kIoError, "no library names in " + dir);

  // GetHeadPosition is const but GetNext is not, in the MFC-shim CList; the
  // list is ours (returned by value ownership), so const_cast to iterate.
  CStringList* mnames = const_cast<CStringList*>(names);
  POSITION pos = mnames->GetHeadPosition();
  while (pos != nullptr) {
    CString cname = mnames->GetNext(pos);
    std::string lib = (LPCSTR)cname;

    VPFLibrary library(CString(root_with_slash.c_str()), cname);
    if (!library.is_open()) continue;  // unreadable library: skip

    // Skip any degenerate (zero/inverted) rect: the DNC 'browse' thumbnail
    // library reports (0,0,0,0) tiles which, catalogued, would falsely match
    // queries near 0,0.
    auto push_if_real = [&](const GeoRect& rect, const std::string& tile) {
      if (tile.empty()) return;  // real DNC tiles are always named; browse's
                                 // garbage "tile" has none
      if (rect.ll.lat >= rect.ur.lat || rect.ll.lon >= rect.ur.lon) return;
      if (rect.ll.lat < -90 || rect.ur.lat > 90 || rect.ll.lon < -180 ||
          rect.ur.lon > 180)
        return;  // out-of-range = uninitialized reader extent
      FrameInfo f;
      f.path = MakeVpfLocator(dir, lib, tile);
      f.bounds = rect;
      f.series_key = lib;
      frames_.push_back(std::move(f));
    };

    // Only tiled libraries produce coverage rows. Tile grids are stable
    // (pinned in V1), so enumeration is deterministic. Untiled DNC libraries
    // (the 'browse' graphic and reference libraries) are not queryable base
    // coverage, and their VPFLibrary::bounds() reads an uninitialized extent
    // here (non-deterministic) — so they are intentionally skipped. Revisit
    // if a real single-cell untiled coverage ever appears.
    const std::vector<VPFTileBounds>* tiles =
        library.get_tile_boundaries_list();
    if (tiles == nullptr) continue;
    for (const VPFTileBounds& t : *tiles)
      push_if_real(RectFrom(t.m_geo_rect), t.tile_name);
  }
  delete names;

  std::sort(frames_.begin(), frames_.end(),
            [](const FrameInfo& a, const FrameInfo& b) { return a.path < b.path; });
  return Status::Ok();
}

bool VpfFrameEnumerator::Next(FrameInfo* info) {
  if (next_ >= frames_.size()) return false;
  *info = frames_[next_++];
  return true;
}

}  // namespace fv
