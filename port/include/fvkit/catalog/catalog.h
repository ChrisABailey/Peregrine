// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/catalog/catalog.h — FvKit L2 coverage catalog (SQLite + R-tree).
// Contracts: port/fvkit-contracts.md; schema mirrors the MDM's
// tblDataSources / tblMapSeries / tblCoverage (see the plan's ground truth):
// per-format enumerators feed coverage rows; viewport queries go through an
// R-tree exactly as the sample mosaic.db proved viable in this ecosystem.
//
// Antimeridian handling per D2: a crossing coverage rect is stored as TWO
// R-tree boxes (rtree id = coverage_id*2 + piece); queries split the same
// way and de-duplicate by coverage id.
//
// The catalog is an internal cache, not an interchange file: schema may
// change freely until the ledger says otherwise (version in meta table).
//
// SCHEMA 2 (2026-08-29): a map series is identified by (format, series_key,
// scale, scale_units) and NOT by the key alone, because FalconView's own
// tblMapSeries is keyed that way -- CCoverageCache::GetMapSeriesIdentity
// looks a frame up by SelectByScale(scale, units, series_name), so on Windows
// a 1 metre Color GeoTIFF and a 50 metre Color GeoTIFF are two map types.
// Keying on the key alone collapsed every generic GeoTIFF resolution into one
// series that reported whichever frame was catalogued first.  An older
// catalog is REBUILT on Open (NeedsRescan() then says so).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fvkit/detail/sqlite.h"
#include "fvkit/formats/enumerate.h"
#include "fvkit/geo.h"

namespace fv {

struct SeriesRow {
  int64_t id = 0;
  std::string format;      // registry key ("cadrg", "dted", ...)
  std::string series_key;  // ("GNC", "DTED1", "500M", ...)
  double scale = 0;        // as reported by the enumerator
  int scale_units = 0;     // MapScaleUnitsEnum value
  double scale_denom = 0;  // normalized denominator (0 = not comparable)
  // FalconView's FORMAT_SERIES_SCALE map-type label ("GNC 1:5 M",
  // "Color 1 meter"); the series_key alone when there is no scale to name.
  // Unique within a format, so it is the handle a CLI or a menu can use.
  std::string display_name;
};

struct CoverageRow {
  int64_t id = 0;
  int64_t series_id = 0;
  std::string format;      // registry key — lets consumers make sources
  std::string series_key;
  std::string path;
  GeoRect bounds;
  int64_t size_bytes = 0;
};

class Catalog {
 public:
  Catalog();
  ~Catalog();
  Catalog(const Catalog&) = delete;
  Catalog& operator=(const Catalog&) = delete;

  // db_path may be ":memory:". Creates/migrates the schema.
  Status Open(const std::string& db_path);

  // True when Open() had to rebuild a pre-schema-2 catalog: the data sources
  // survived, their coverage did not. The caller should Scan() each one.
  bool NeedsRescan() const { return needs_rescan_; }

  // Registers a data source (a directory tree of one format). Idempotent on
  // (path, format).
  Status AddDataSource(const std::string& path, const std::string& format_key,
                       int priority, int64_t* data_source_id);

  // Enumerates the source via the format registry (RegisterBuiltinFormats or
  // a custom RegisterFormat must have run) and replaces its coverage rows.
  Status Scan(int64_t data_source_id, int* frames_added);

  Status Series(std::vector<SeriesRow>* out) const;

  // Coverage intersecting rect (antimeridian-aware), optionally restricted
  // to one series. Rows ordered by coverage id.
  Status SelectByGeoRect(const GeoRect& rect, std::vector<CoverageRow>* out,
                         int64_t series_id = 0) const;

  // The series whose normalized scale denominator is closest (by ratio) to
  // the target; series with scale_denom == 0 (e.g. DTED) are excluded.
  Status BestSeriesForScale(double target_scale_denom, SeriesRow* out) const;

  // Removes the source and all its coverage.
  Status RemoveDataSource(int64_t data_source_id);

 private:
  Status InsertCoverage(int64_t data_source_id, int64_t series_id,
                        const FrameInfo& f);
  Status ResolveSeries(const std::string& format, const FrameInfo& f,
                       int64_t* series_id);

  mutable detail::SqliteDb db_;
  bool needs_rescan_ = false;
};

}  // namespace fv
