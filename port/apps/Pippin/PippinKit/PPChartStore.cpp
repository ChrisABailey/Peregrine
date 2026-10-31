// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "PPChartStore.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

#include "fv_enc_index.h"
#include "fv_enc_names.h"
#include "fv_zip_reader.h"

namespace fs = std::filesystem;

namespace pippin {

namespace {

constexpr char kCurrentFile[] = "current";
/// In a set directory: the NOAA region id the set was imported as.
constexpr char kRegionFile[] = "region";
constexpr char kSetPrefix[] = "set-";
/// Share of the progress bar given to unpacking; indexing gets the rest.
constexpr double kUnzipShare = 0.8;
// With a catalogue the name table takes this much of the indexing share.
constexpr double kNamesShare = 0.6;

/// A new set directory name that does not exist yet under `root`.
std::string NewSetName(const fs::path& root) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
  for (int n = 0;; ++n) {
    std::string name = kSetPrefix + std::to_string(ms);
    if (n > 0) name += "-" + std::to_string(n);
    std::error_code ec;
    if (!fs::exists(root / name, ec)) return name;
  }
}

}  // namespace

ChartStore::ChartStore(std::string root) : root_(std::move(root)) {}

std::string ChartStore::CurrentSet() const {
  std::ifstream is(fs::path(root_) / kCurrentFile);
  std::string name;
  if (!is || !std::getline(is, name) || name.rfind(kSetPrefix, 0) != 0 ||
      name.find('/') != std::string::npos)
    return {};
  const fs::path dir = fs::path(root_) / name;
  std::error_code ec;
  if (!fs::is_regular_file(dir / fv::kEncIndexFileName, ec)) return {};
  return dir.string();
}

std::string ChartStore::CurrentRegion() const {
  const std::string set = CurrentSet();
  if (set.empty()) return {};
  std::ifstream is(fs::path(set) / kRegionFile);
  std::string region;
  if (!is || !std::getline(is, region)) return {};
  return region;
}

fv::Status ChartStore::CheckForUpdates(const std::string& catalog_path,
                                       fv::EncCatalogComparison* out) const {
  *out = fv::EncCatalogComparison();
  const std::string set = CurrentSet();
  if (set.empty()) return fv::Status::Error(fv::kNotFound, "no chart set is installed");
  std::vector<fv::EncIndexEntry> entries;
  fv::Status s = fv::ReadEncIndex((fs::path(set) / fv::kEncIndexFileName).string(), &entries);
  if (!s.ok()) return s;
  fv::EncCatalog catalog;
  s = fv::ReadEncCatalog(catalog_path, &catalog);
  if (!s.ok()) return s;
  *out = fv::CompareEncCatalog(entries, catalog, CurrentRegion());
  return fv::Status::Ok();
}

fv::Status ChartStore::Import(const std::string& zip_path, ChartImportResult* result,
                              const std::function<bool(double)>& progress) {
  *result = ChartImportResult();
  fv::ZipReader zip;
  fv::Status s = zip.Open(zip_path);
  if (!s.ok()) return s;

  const fs::path root(root_);
  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec) return fv::Status::Error(fv::kIoError, "cannot create " + root_);
  const uint64_t need = zip.total_uncompressed_size();
  const fs::space_info space = fs::space(root, ec);
  if (!ec && space.available < need + need / 20)
    return fv::Status::Error(fv::kIoError,
                             "not enough free space: the charts need " +
                                 std::to_string(need / (1024 * 1024)) + " MB");

  const std::string set_name = NewSetName(root);
  const fs::path set_dir = root / set_name;
  auto fail = [&](fv::Status st) {
    std::error_code rm;
    fs::remove_all(set_dir, rm);
    return st;
  };
  fs::create_directories(set_dir, ec);
  if (ec) return fv::Status::Error(fv::kIoError, "cannot create " + set_dir.string());

  s = zip.ExtractAll(set_dir.string(), [&](uint64_t done, uint64_t total) {
    return !progress || progress(total ? kUnzipShare * done / total : kUnzipShare);
  });
  zip.Close();
  if (!s.ok()) return fail(s);

  std::vector<fv::EncIndexEntry> entries;
  std::vector<std::string> unreadable;
  const double index_share = (1.0 - kUnzipShare) * (catalog_dir_.empty() ? 1.0 : 1.0 - kNamesShare);
  s = fv::BuildEncIndex(set_dir.string(), &entries, &unreadable,
                        [&](size_t done, size_t total) {
                          return !progress ||
                                 progress(kUnzipShare +
                                          index_share * done / std::max<size_t>(total, 1));
                        });
  if (s.code == fv::kNotFound)
    return fail(fv::Status::Error(fv::kNotFound, "the zip holds no ENC cells"));
  if (!s.ok()) return fail(s);
  s = fv::WriteEncIndex((set_dir / fv::kEncIndexFileName).string(), entries);
  if (!s.ok()) return fail(s);

  if (!catalog_dir_.empty()) {
    std::vector<std::string> cells;
    for (const fv::EncIndexEntry& e : entries)
      if (!e.cancelled) cells.push_back((set_dir / e.path).string());
    std::vector<fv::EncNameEntry> names;
    const double start = kUnzipShare + index_share;
    s = fv::BuildEncNames(cells, catalog_dir_, &names, [&](size_t done, size_t total) {
      return !progress ||
             progress(start + (1.0 - start) * done / std::max<size_t>(total, 1));
    });
    if (s.code == fv::kInterrupted) return fail(s);
    // A failed table is not a failed import: the set draws, and search builds it later.
    if (s.ok()) fv::WriteEncNames((set_dir / fv::kEncNamesFileName).string(), names);
  }

  const std::string region = fv::EncRegionFromZipName(fs::path(zip_path).filename().string());
  if (!region.empty()) {
    std::ofstream os(set_dir / kRegionFile, std::ios::trunc);
    os << region << "\n";
  }

  // The pointer file is the commit: written beside, then renamed over.
  const fs::path tmp = root / (std::string(kCurrentFile) + ".tmp");
  {
    std::ofstream os(tmp, std::ios::trunc);
    os << set_name << "\n";
    if (!os.flush()) return fail(fv::Status::Error(fv::kIoError, "cannot write " + tmp.string()));
  }
  fs::rename(tmp, root / kCurrentFile, ec);
  if (ec) return fail(fv::Status::Error(fv::kIoError, "cannot install the chart set"));

  result->set_dir = set_dir.string();
  std::set<int> bands;
  fv::GeoRect b{{90.0, 180.0}, {-90.0, -180.0}};
  for (const fv::EncIndexEntry& e : entries) {
    result->updates_applied += e.applied_updates;
    if (e.cancelled) {
      result->cancelled.push_back(e.name);
      continue;
    }
    result->updates_failed += e.pending_updates();
    if (e.pending_updates() > 0) result->outdated.push_back(e.name);
    ++result->cell_count;
    bands.insert(e.usage_band);
    b.ll.lat = std::min(b.ll.lat, e.bounds.ll.lat);
    b.ll.lon = std::min(b.ll.lon, e.bounds.ll.lon);
    b.ur.lat = std::max(b.ur.lat, e.bounds.ur.lat);
    b.ur.lon = std::max(b.ur.lon, e.bounds.ur.lon);
  }
  result->bands.assign(bands.begin(), bands.end());
  result->bounds = b;
  for (const std::string& u : unreadable)
    result->unreadable.push_back(fs::path(u).lexically_relative(set_dir).generic_string());
  return fv::Status::Ok();
}

void ChartStore::RemoveStaleSets() const {
  const std::string current = CurrentSet();
  const std::string keep = current.empty() ? std::string() : fs::path(current).filename().string();
  std::error_code ec;
  if (current.empty() && fs::exists(fs::path(root_) / kCurrentFile, ec))
    return;  // a pointer names a broken set; leave everything for inspection
  for (fs::directory_iterator it(root_, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (name.rfind(kSetPrefix, 0) != 0 || name == keep) continue;
    std::error_code rm;
    fs::remove_all(it->path(), rm);
  }
}

}  // namespace pippin
