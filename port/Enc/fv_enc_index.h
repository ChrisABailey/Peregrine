// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * fv_enc_index.h — a persisted index of the base cells in an ENC exchange set.
 *
 * One row per base cell: path, name, usage band, edition, update files found
 * and applied, cancellation, compilation scale and bounds. Built once when a set is installed so that
 * opening it later reads one small file instead of scanning the tree.
 */
#ifndef FV_ENC_INDEX_H_
#define FV_ENC_INDEX_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {

/** The index file's name inside the directory it describes. */
extern const char kEncIndexFileName[];

struct EncIndexEntry {
  std::string path;     // relative to the indexed root, '/'-separated
  std::string name;     // cell name, e.g. "US5CHSDC"
  int usage_band = 0;   // from the cell name; 1 overview … 6 berthing
  std::string edition;  // DSID EDTN
  int update_count = 0;  // update files beside the cell (applied when it opens)
  int applied_updates = 0;  // how many of them apply; a cancellation counts
  bool cancelled = false;   // withdrawn by an update (EDTN 0); not drawn
  int32_t compilation_scale = 0;  // DSPM CSCL
  GeoRect bounds;

  /** Update files that do not apply (CH-D5: the cell is drawn without them). */
  int pending_updates() const { return update_count - applied_updates; }
};

/** Indexes every base cell under `root`. Bounds come from CATALOG.031 where
 *  it lists the cell, otherwise from parsing the cell. A cell with update
 *  files has them applied (without geometry) to record how far they get.
 *  A cell whose records cannot be read is skipped and its path appended to `unreadable`
 *  (may be null); kNotFound when no cell reads. `progress` gets cells done
 *  and the total; returning false cancels with kInterrupted. */
Status BuildEncIndex(const std::string& root, std::vector<EncIndexEntry>* out,
                     std::vector<std::string>* unreadable = nullptr,
                     const std::function<bool(size_t, size_t)>& progress = {});

/** Writes `entries` as tab-separated text to `path`, through a temporary file
 *  renamed into place. */
Status WriteEncIndex(const std::string& path, const std::vector<EncIndexEntry>& entries);

/** Reads a file written by WriteEncIndex. kUnsupported for another version,
 *  including the version-1 files written before updates were applied. */
Status ReadEncIndex(const std::string& path, std::vector<EncIndexEntry>* out);

}  // namespace fv

#endif  // FV_ENC_INDEX_H_
