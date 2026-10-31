// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * PPChartStore.h — the app's installed ENC chart sets, filled from NOAA zips.
 *
 * Plain C++ (no Foundation), tested on the mac. Each import unpacks into its
 * own `set-<id>/` directory with a cell index (fv_enc_index.h); the text file
 * `current` names the installed set and is replaced by rename, so a reader
 * sees either the old set or the new one, never a half-written one.
 */
#ifndef PIPPIN_PPCHARTSTORE_H_
#define PIPPIN_PPCHARTSTORE_H_

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "fv_enc_catalog.h"
#include "fvkit/geo.h"

namespace pippin {

/** What an import installed. */
struct ChartImportResult {
  std::string set_dir;  // the new set's directory, now current
  size_t cell_count = 0;  // cells drawn: readable and not cancelled
  std::vector<int> bands;  // usage bands present, ascending
  std::vector<std::string> unreadable;  // cells skipped, relative to set_dir
  std::vector<std::string> cancelled;   // cell names withdrawn by an update
  std::vector<std::string> outdated;    // cell names drawn without some updates
  size_t updates_applied = 0;  // update files applied, over every cell
  size_t updates_failed = 0;   // update files that did not apply
  fv::GeoRect bounds;  // union of the drawn cells' bounds
};

class ChartStore {
 public:
  /** `root` holds the sets and the `current` pointer; created on import. */
  explicit ChartStore(std::string root);

  const std::string& root() const { return root_; }

  /** The S-57 catalogue used to build each set's named-object table
   *  (fv_enc_names.h) at import. Without one the table is built on the
   *  set's first search instead. */
  void SetCatalogDir(std::string dir) { catalog_dir_ = std::move(dir); }

  /** The installed set's directory, or empty when none is installed or the
   *  named set is missing its index. */
  std::string CurrentSet() const;

  /** The NOAA region id recorded when the installed set was imported from a
   *  zip named like `FL_ENCs.zip`; empty otherwise. */
  std::string CurrentRegion() const;

  /** Compares the installed set with NOAA's product catalog (ENCProdCat.xml)
   *  at `catalog_path`. The region is the recorded one, else inferred from
   *  the catalog. kNotFound when no set is installed. */
  fv::Status CheckForUpdates(const std::string& catalog_path,
                             fv::EncCatalogComparison* out) const;

  /** Unpacks `zip_path` into a new set, indexes it and makes it current.
   *  `progress` gets a fraction in [0, 1]; returning false cancels with
   *  kInterrupted. On any failure the new set is removed and the current
   *  one is untouched. */
  fv::Status Import(const std::string& zip_path, ChartImportResult* result,
                    const std::function<bool(double)>& progress = {});

  /** Removes every set other than the current one. Call once nothing reads
   *  the previous set any more. */
  void RemoveStaleSets() const;

 private:
  std::string root_;
  std::string catalog_dir_;
};

}  // namespace pippin

#endif  // PIPPIN_PPCHARTSTORE_H_
