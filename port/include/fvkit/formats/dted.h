// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/formats/dted.h — DTED adapter over fv::DtedCell (contracts D6).
//
// Enumerator: walks the standard DTED tree <root>/<lon dir>/<lat file>
// (e.g. dted/w082/n31.dt1), case-insensitive like NTFS (TestData has both
// w083 and W084). The cell's 1x1-degree bounds come from the PATH, exactly
// as FalconView assigns them (never from the file header); nothing is
// opened during enumeration, so `edition` stays empty.
//
// Source: lazily opens cells on first query and caches them. Where a cell
// exists at several DTED levels, the highest level (finest posts) that
// opens wins. Meters/NaN conversion per D4: -32767 void posts -> NaN with
// Status ok. A cell whose file exists but won't open (e.g. the prototype
// .DT3 UHL this snapshot rejects) is kIoError, not kOutOfCoverage.
//
// The Windows COM layer above (CDted: block fills, level fallback across
// data sources, notify events) is NOT reproduced here; the catalog/engine
// layers take over those roles.

#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fvkit/formats/enumerate.h"
#include "fvkit/formats/source.h"

namespace fv {

class DtedCell;  // legacy reader (fv_dted_cell.h) — implementation detail

class DtedFrameEnumerator : public IFrameEnumerator {
 public:
  DtedFrameEnumerator() = default;

  Status Begin(const std::string& dir) override;
  bool Next(FrameInfo* info) override;

 private:
  std::vector<FrameInfo> frames_;  // sorted by path
  size_t next_ = 0;
};

class DtedElevationSource : public IElevationSource {
 public:
  // Indexes <root_dir> immediately (directory scan only; cells open lazily).
  explicit DtedElevationSource(const std::string& root_dir);
  ~DtedElevationSource() override;

  // Bounding box of all indexed cells — gaps included (see source.h).
  GeoRect Bounds() const override;

  // NOTE: a point exactly on the coverage's north/east outer edge (e.g.
  // lat 36.0 when the northernmost cell is n35) resolves by floor() to a
  // cell that doesn't exist -> kOutOfCoverage, although the edge post is
  // physically in the n35 file. Interior cell edges are unaffected
  // (adjacent cells share them). Revisit if a consumer ever cares.
  Status GetElevation(const GeoPoint& p, float* elevation_meters) override;

  // From the covering cell's own post counts, so a DTED2 cell inside a DTED1
  // tree answers 1 arcsecond where its neighbours answer 3. False when no
  // cell covers `p` or none of its files open.
  bool PostSpacing(const GeoPoint& p, double* lat_deg,
                   double* lon_deg) override;

 private:
  using CellKey = std::pair<int, int>;  // (sw_lat, sw_lon), whole degrees

  // The open cell covering `p`, opening it on first use. nullptr when no cell
  // is indexed there or every candidate file failed to open.
  DtedCell* CellFor(const GeoPoint& p);

  struct CellRef {
    int level;  // 0..3, from the file extension
    std::string path;
  };

  Status scan_status_;
  std::map<CellKey, std::vector<CellRef>> cells_;  // refs sorted level-desc
  std::map<CellKey, std::unique_ptr<DtedCell>> open_cells_;  // nullptr = all opens failed
  GeoRect bounds_;
  bool has_cells_ = false;
};

}  // namespace fv
