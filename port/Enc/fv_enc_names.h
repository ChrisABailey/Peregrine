// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * fv_enc_names.h — a persisted table of the named objects (OBJNAM) in an ENC
 * exchange set, so a text search can answer over the whole set without
 * parsing cells.
 *
 * One row per object. The same name and object class found in overlapping
 * cells or usage bands is one row, positioned from the finest band.
 */
#ifndef FV_ENC_NAMES_H_
#define FV_ENC_NAMES_H_

#include <functional>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {

/** The names file's name, beside chart-index.tsv in the set's root. */
extern const char kEncNamesFileName[];

struct EncNameEntry {
  std::string name;          // OBJNAM
  std::string object_class;  // S-57 acronym, e.g. "BOYLAT"
  int usage_band = 0;        // band of the cell the position came from
  GeoPoint position;         // the point, or the centre of a line or area
  GeoRect bounds;            // degenerate for a point
};

/** Reads every named object in `cell_paths` (base cells, `.000`, with their
 *  updates applied) using the
 *  S-57 catalogue in `catalog_dir`, and merges repeats: same name and class
 *  whose bounds come within `merge_m` metres. Unreadable cells are skipped.
 *  `progress` gets cells done and the total; returning false cancels with
 *  kInterrupted. */
Status BuildEncNames(const std::vector<std::string>& cell_paths,
                     const std::string& catalog_dir, std::vector<EncNameEntry>* out,
                     const std::function<bool(size_t, size_t)>& progress = {},
                     double merge_m = 200.0);

/** Writes `entries` as tab-separated text to `path`, through a temporary file
 *  renamed into place. */
Status WriteEncNames(const std::string& path, const std::vector<EncNameEntry>& entries);

/** Reads a file written by WriteEncNames. kUnsupported for another version,
 *  including the version-1 tables built from base editions only. */
Status ReadEncNames(const std::string& path, std::vector<EncNameEntry>* out);

}  // namespace fv

#endif  // FV_ENC_NAMES_H_
