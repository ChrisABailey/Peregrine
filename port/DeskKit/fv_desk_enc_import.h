// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_enc_import.h — the ENC importer behind Map ▸ Options ▸ ENC.
///
/// Each source (a NOAA zip or a folder of cells) becomes one set directory
/// `<charts dir>/enc/<set name>`, where Generate Coverage's `enc` step finds
/// it. A set of the same name is replaced. Work runs on a thread of its own,
/// staged in `<charts dir>/.staging/` so a half-copied set is never scanned.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fv_enc_import.h"

namespace fv {
namespace desk {

/// The settings key naming the charts directory; empty uses `DefaultChartsDir()`.
inline constexpr char kEncChartsDirKey[] = "enc.charts_dir";

/// `charts/` beside the user packages directory (`DefaultUserPackageDir()`).
/// Empty when no home directory is known.
std::string DefaultChartsDir();

/// The set directory name for `source`: a zip's stem ("SC_ENCs"), a folder's
/// name, or the parent's name for a folder called ENC_ROOT. Characters other
/// than letters, digits, '-', '_' and '.' become '_'; a leading '.' is dropped.
std::string EncSetName(const std::string& source);

/// What one source did.
struct EncImportOutcome {
  std::string source;
  std::string set_name;
  Status status;
  bool replaced = false;  ///< an older set of the same name was replaced
  EncImportResult result;
};

/// A snapshot of a running or finished import.
struct EncImportProgress {
  int done = 0;          ///< sources finished
  int total = 0;
  double fraction = 0;   ///< over every source
  std::string current;   ///< the set being imported; "" between sources
  bool finished = false;
  bool cancelled = false;
};

class EncImportJob {
 public:
  /// Starts importing `sources` into `<charts_dir>/enc`. `catalog_dir` is the
  /// S-57 catalogue for each set's names table; may be empty.
  EncImportJob(std::vector<std::string> sources, std::string charts_dir,
               std::string catalog_dir);
  /// Cancels and joins.
  ~EncImportJob();
  EncImportJob(const EncImportJob&) = delete;
  EncImportJob& operator=(const EncImportJob&) = delete;

  EncImportProgress Progress() const;
  /// Stops the source in progress (its set is removed) and skips the rest.
  void Cancel();
  void Wait();

  /// Valid once `Progress().finished`; one per source attempted.
  std::vector<EncImportOutcome> Outcomes() const;
  /// True when at least one set was installed.
  bool AnyInstalled() const;
  /// Lines for the user: per set, cells and updates, or why it failed.
  std::string Summary() const;

 private:
  void Run();
  EncImportOutcome ImportOne(const std::string& source);

  const std::vector<std::string> sources_;
  const std::string charts_dir_;
  const std::string catalog_dir_;
  std::atomic<bool> cancel_{false};

  mutable std::mutex mu_;
  EncImportProgress progress_;
  std::vector<EncImportOutcome> outcomes_;

  std::thread worker_;
};

}  // namespace desk
}  // namespace fv
