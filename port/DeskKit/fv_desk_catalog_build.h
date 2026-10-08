// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_catalog_build.h — building a map catalog in the background.
///
/// `PlanScan` decides what an auto-detect scan of a directory tries (each
/// known format at the directory and at its conventional subdirectory, plus
/// every VPF database under it); `CatalogBuild` runs a plan on its own thread
/// over its own connection to the catalog file, reporting progress per data
/// source and stopping between sources when cancelled.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fv {
namespace desk {

/// One data source to try: `format` at the first of `dirs` that yields frames.
struct ScanStep {
  std::string format;
  std::vector<std::string> dirs;
  /// Keep the source registered when its scan finds nothing (a rescan of a
  /// source the user added); otherwise it is removed again.
  bool keep_if_empty = false;
};

/// A format and the subdirectory its data conventionally sits in ("" for none).
struct ScanFormat {
  std::string format;
  std::string subdir;
};

/// The auto-detect table: cadrg/rpf, tiros/tiros3, dted/dted, dted-shaded/dted,
/// geotiff/geotiff, gpkg, enc/enc, osm/OSM. DTED appears twice on purpose:
/// once as elevation, once as the shaded-relief raster.
const std::vector<ScanFormat>& DefaultScanFormats();

/// Directories under `root` holding a VPF database header table (`dht`), at
/// most `max_depth` levels down. Hidden directories are skipped and a
/// database is not searched for nested ones. Sorted.
std::vector<std::string> FindVpfDatabases(const std::string& root, int max_depth = 4);

/// The steps of an auto-detect scan of `root`, in `formats` order, then one
/// "vpf" step per VPF database. A format with no registered enumerator is
/// left out; a subdirectory that does not exist is not listed.
std::vector<ScanStep> PlanScan(const std::string& root,
                               const std::vector<ScanFormat>& formats = DefaultScanFormats());

/// What one step catalogued.
struct ScanResult {
  std::string format;
  std::string path;
  int frames = 0;
};

/// A snapshot of a running or finished build.
struct BuildProgress {
  int done = 0;          ///< steps finished
  int total = 0;         ///< steps planned
  std::string current;   ///< "cadrg  /data/rpf" while scanning; "" between steps
  bool finished = false;
  bool cancelled = false;
};

class CatalogBuild {
 public:
  /// Starts running `steps` against the catalog database at `catalog_path`
  /// (a file; a second connection cannot see a ":memory:" database).
  CatalogBuild(std::string catalog_path, std::vector<ScanStep> steps);
  /// Cancels and joins.
  ~CatalogBuild();
  CatalogBuild(const CatalogBuild&) = delete;
  CatalogBuild& operator=(const CatalogBuild&) = delete;

  BuildProgress Progress() const;
  /// Stops after the step in progress; a source's scan is not interrupted.
  void Cancel();
  /// Blocks until the worker has finished.
  void Wait();

  /// Valid once `Progress().finished`.
  std::vector<ScanResult> Results() const;
  std::vector<std::string> Errors() const;
  /// One line for the user: frames and sources catalogued, or why none were.
  std::string Summary() const;

 private:
  void Run();

  const std::string catalog_path_;
  const std::vector<ScanStep> steps_;
  std::atomic<bool> cancel_{false};

  mutable std::mutex mu_;
  BuildProgress progress_;
  std::vector<ScanResult> results_;
  std::vector<std::string> errors_;

  std::thread worker_;
};

}  // namespace desk
}  // namespace fv
