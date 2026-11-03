// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_enc_import.h"

#include <algorithm>
#include <filesystem>
#include <set>

#include "fv_enc_index.h"
#include "fv_enc_names.h"
#include "fv_zip_reader.h"

namespace fs = std::filesystem;

namespace fv {

namespace {

/// Share of the progress bar given to unpacking or copying; indexing gets the rest.
constexpr double kUnpackShare = 0.8;
// With a catalogue the name table takes this much of the indexing share.
constexpr double kNamesShare = 0.6;

/// Total size of the regular files under `dir`, and their paths.
uint64_t ListFiles(const fs::path& dir, std::vector<fs::path>* files) {
  uint64_t total = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || ec) continue;
    files->push_back(it->path());
    total += it->file_size(ec);
  }
  return total;
}

/// Copies `files` from under `from` to the same relative paths under `to`.
Status CopyTree(const fs::path& from, const fs::path& to, const std::vector<fs::path>& files,
                uint64_t total, const std::function<bool(uint64_t, uint64_t)>& progress) {
  uint64_t done = 0;
  for (const fs::path& f : files) {
    if (progress && !progress(done, total)) return Status::Error(kInterrupted, "import cancelled");
    const fs::path dest = to / f.lexically_relative(from);
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (!ec) fs::copy_file(f, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) return Status::Error(kIoError, "cannot copy " + f.string() + ": " + ec.message());
    done += fs::file_size(f, ec);
  }
  if (progress) progress(total, total);
  return Status::Ok();
}

}  // namespace

Status ImportEncSet(const std::string& source, const std::string& dest_dir,
                    const std::string& catalog_dir, EncImportResult* result,
                    const std::function<bool(double)>& progress) {
  *result = EncImportResult();
  const fs::path set_dir(dest_dir);
  std::error_code ec;
  if (fs::exists(set_dir, ec)) return Status::Error(kInvalidArg, dest_dir + " already exists");

  const bool from_dir = fs::is_directory(source, ec);
  if (from_dir) {
    const fs::path rel = fs::weakly_canonical(set_dir, ec).lexically_relative(
        fs::weakly_canonical(source, ec));
    if (!rel.empty() && *rel.begin() != "..")
      return Status::Error(kInvalidArg, "cannot import a folder into itself");
  }
  ZipReader zip;
  std::vector<fs::path> files;
  uint64_t need = 0;
  if (from_dir) {
    need = ListFiles(source, &files);
  } else {
    Status s = zip.Open(source);
    if (!s.ok()) return s;
    need = zip.total_uncompressed_size();
  }

  fs::create_directories(set_dir.parent_path(), ec);
  if (ec) return Status::Error(kIoError, "cannot create " + set_dir.parent_path().string());
  const fs::space_info space = fs::space(set_dir.parent_path(), ec);
  if (!ec && space.available < need + need / 20)
    return Status::Error(kIoError, "not enough free space: the charts need " +
                                       std::to_string(need / (1024 * 1024)) + " MB");

  auto fail = [&](Status st) {
    std::error_code rm;
    fs::remove_all(set_dir, rm);
    return st;
  };
  fs::create_directories(set_dir, ec);
  if (ec) return Status::Error(kIoError, "cannot create " + dest_dir);

  const auto unpacked = [&](uint64_t done, uint64_t total) {
    return !progress || progress(total ? kUnpackShare * done / total : kUnpackShare);
  };
  Status s = from_dir ? CopyTree(source, set_dir, files, need, unpacked)
                      : zip.ExtractAll(dest_dir, unpacked);
  zip.Close();
  if (!s.ok()) return fail(s);

  std::vector<EncIndexEntry> entries;
  std::vector<std::string> unreadable;
  const double index_share = (1.0 - kUnpackShare) * (catalog_dir.empty() ? 1.0 : 1.0 - kNamesShare);
  s = BuildEncIndex(dest_dir, &entries, &unreadable, [&](size_t done, size_t total) {
    return !progress ||
           progress(kUnpackShare + index_share * done / std::max<size_t>(total, 1));
  });
  if (s.code == kNotFound)
    return fail(Status::Error(kNotFound, (from_dir ? "the folder" : "the zip") +
                                             std::string(" holds no ENC cells")));
  if (!s.ok()) return fail(s);
  s = WriteEncIndex((set_dir / kEncIndexFileName).string(), entries);
  if (!s.ok()) return fail(s);

  if (!catalog_dir.empty()) {
    std::vector<std::string> cells;
    for (const EncIndexEntry& e : entries)
      if (!e.cancelled) cells.push_back((set_dir / e.path).string());
    std::vector<EncNameEntry> names;
    const double start = kUnpackShare + index_share;
    s = BuildEncNames(cells, catalog_dir, &names, [&](size_t done, size_t total) {
      return !progress || progress(start + (1.0 - start) * done / std::max<size_t>(total, 1));
    });
    if (s.code == kInterrupted) return fail(s);
    // A failed table is not a failed import: the set draws, and search builds it later.
    if (s.ok()) WriteEncNames((set_dir / kEncNamesFileName).string(), names);
  }

  result->set_dir = dest_dir;
  std::set<int> bands;
  GeoRect b{{90.0, 180.0}, {-90.0, -180.0}};
  for (const EncIndexEntry& e : entries) {
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
  return Status::Ok();
}

}  // namespace fv
