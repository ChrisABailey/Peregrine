// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "PPChartStore.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "fv_enc_import.h"
#include "fv_enc_index.h"

namespace fs = std::filesystem;

namespace pippin {

namespace {

constexpr char kCurrentFile[] = "current";
/// In a set directory: the NOAA region id the set was imported as.
constexpr char kRegionFile[] = "region";
constexpr char kSetPrefix[] = "set-";

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
  const fs::path root(root_);
  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec) return fv::Status::Error(fv::kIoError, "cannot create " + root_);

  const std::string set_name = NewSetName(root);
  const fs::path set_dir = root / set_name;
  fv::Status s = fv::ImportEncSet(zip_path, set_dir.string(), catalog_dir_, result, progress);
  if (!s.ok()) return s;
  auto fail = [&](fv::Status st) {
    std::error_code rm;
    fs::remove_all(set_dir, rm);
    *result = ChartImportResult();
    return st;
  };

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
