// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * fv_enc_import.h — installs one ENC exchange set into a directory of its own.
 *
 * Unpacks a zip (NOAA's regional downloads) or copies a directory of cells,
 * then writes the cell index (fv_enc_index.h) and, given the S-57 catalogue,
 * the named-object table (fv_enc_names.h). Shared by Osprey's chart store and
 * Peregrine's ENC importer; where the set goes and how it replaces an older
 * one is the caller's business.
 */
#ifndef FV_ENC_IMPORT_H_
#define FV_ENC_IMPORT_H_

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {

/** What an import installed. */
struct EncImportResult {
  std::string set_dir;  // the directory the set was installed into
  size_t cell_count = 0;  // cells drawn: readable and not cancelled
  std::vector<int> bands;  // usage bands present, ascending
  std::vector<std::string> unreadable;  // cells skipped, relative to set_dir
  std::vector<std::string> cancelled;   // cell names withdrawn by an update
  std::vector<std::string> outdated;    // cell names drawn without some updates
  size_t updates_applied = 0;  // update files applied, over every cell
  size_t updates_failed = 0;   // update files that did not apply
  GeoRect bounds;  // union of the drawn cells' bounds
};

/** Installs the set at `source` (a .zip, or a directory with base cells
 *  below it) into `dest_dir`, which must not exist yet. `catalog_dir` is the
 *  S-57 catalogue for the names table; without one the table is left for
 *  the set's first search. `progress` gets a fraction in [0, 1]; returning
 *  false cancels with kInterrupted. On any failure `dest_dir` is removed. */
Status ImportEncSet(const std::string& source, const std::string& dest_dir,
                    const std::string& catalog_dir, EncImportResult* result,
                    const std::function<bool(double)>& progress = {});

}  // namespace fv

#endif  // FV_ENC_IMPORT_H_
