// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * fv_enc_catalog.h — NOAA's ENC product catalog and its comparison with an
 * installed cell index.
 *
 * The catalog (https://www.charts.noaa.gov/ENCs/ENCProdCat.xml) lists every
 * NOAA cell with its status, edition, last update number and the states and
 * Coast Guard districts whose regional zips carry it. A regional zip holds
 * exactly the active cells naming that region.
 */
#ifndef FV_ENC_CATALOG_H_
#define FV_ENC_CATALOG_H_

#include <string>
#include <vector>

#include "fv_enc_index.h"

namespace fv {

/** One `<cell>` of the product catalog. */
struct EncCatalogCell {
  std::string name;        // e.g. "US5CHSDC"
  bool cancelled = false;  // status "Cancelled"
  int edition = 0;         // edtn
  int update = 0;          // updn: number of the last update issued
  /// Regional zip ids carrying the cell: state codes ("FL") and Coast Guard
  /// districts as NOAA names their zips ("07CGD").
  std::vector<std::string> regions;
};

/** The parsed catalog. */
struct EncCatalog {
  std::string valid;  // header dt_valid, ISO 8601
  std::vector<EncCatalogCell> cells;
};

/** Reads ENCProdCat.xml. Coverage panels and other elements are skipped.
 *  kIoError when the file cannot be read or is not well-formed XML;
 *  kNotFound when it lists no cells. */
Status ReadEncCatalog(const std::string& path, EncCatalog* out);

/** What the catalog says about an installed set. Lists hold cell names. */
struct EncCatalogComparison {
  std::string region;  // regional zip id compared against; empty if unknown
  std::vector<std::string> new_editions;  // catalog edition is newer
  std::vector<std::string> new_updates;   // same edition, later updates issued
  std::vector<std::string> cancelled;     // drawn here, cancelled by NOAA
  std::vector<std::string> added;         // active in the region, not installed
  std::vector<std::string> unlisted;      // installed, absent from the catalog

  /** True when re-downloading the region would change nothing drawn. */
  bool up_to_date() const {
    return new_editions.empty() && new_updates.empty() && cancelled.empty() &&
           added.empty();
  }
};

/** The regional zip whose active cells best match the installed ones
 *  (Jaccard similarity over cells not cancelled locally); empty when no
 *  region shares a cell. Ties go to the region listed first in the catalog. */
std::string InferEncRegion(const std::vector<EncIndexEntry>& installed,
                           const EncCatalog& catalog);

/** Compares `installed` with `catalog`. `region` scopes `added`; when empty
 *  it is inferred with InferEncRegion. Installed update files are counted,
 *  not applied ones: an update that failed to apply is not fixed by
 *  fetching the same files again. */
EncCatalogComparison CompareEncCatalog(const std::vector<EncIndexEntry>& installed,
                                       const EncCatalog& catalog,
                                       const std::string& region = {});

/** The region id in a NOAA regional zip's file name ("FL_ENCs.zip" → "FL",
 *  "07CGD_ENCs.zip" → "07CGD"); empty for any other name. */
std::string EncRegionFromZipName(const std::string& file_name);

}  // namespace fv

#endif  // FV_ENC_CATALOG_H_
