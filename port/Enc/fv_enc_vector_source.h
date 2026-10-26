// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::EncVectorSource — S-57 ENC as an FvKit IVectorSource (ENC phase E3a,
// plan §7). The LEFT-hand side of the vector seam for the port's second real
// vector product; DNC's VpfVectorSource is the sibling, and the renderer and
// rule layer in the middle are shared with it, not forked.
//
// Rides E1's `S57Cell` reader and E2's `S57ObjectCatalog`:
//
//   S57Cell         one base cell (`*.000`), features with assembled geometry
//                   and NUMERIC object/attribute codes
//   S57ObjectCatalog  those numbers -> the acronyms everything above dispatches
//                   on (OBJL 42 -> "DEPARE", ATTL 87 -> "DRVAL1")
//
// The catalogue is REQUIRED, not optional. S-52's lookup tables are keyed on
// `DEPARE`, so a source without acronyms could only emit numbers, and every
// feature would fall through the style engine unsymbolized — a chart that
// looks like a styling bug. Open() fails instead.
//
// ONE CELL = ONE TILE. `FeatureRef::tile` is the index of the cell in the set,
// `feature` the S-57 record id (FRID.RCID, unique within a cell), so a ref
// survives a re-query and Describe() can go straight back to the row.
//
// STYLE KEY = the object-class acronym; LAYER = the same string. S-57 has no
// separate table-per-class the way VPF does (every feature lives in one cell),
// so the "layer" a rule matches on is the object class itself.
//
// SCALE: a cell's usage band (US5CHSDC -> 5, harbour) is S-57's scale band,
// the role DNC's library plays. Per-feature thinning is S-57's own SCAMIN
// attribute — "minimum scale at which this feature may be shown" — which is
// applied here rather than in the style engine, because it is a property of
// the DATA, not of the symbology (S-52 §8.4.4). SCAMIN is honoured only when
// the query names a scale; a bulk query (scale 0) returns everything.

#ifndef FV_ENC_VECTOR_SOURCE_H_
#define FV_ENC_VECTOR_SOURCE_H_

#include <memory>
#include <string>
#include <vector>

#include "fvkit/vector/vector.h"

namespace fv {

/** A cell to open by path with its extent known in advance (a CATALOG.031 row
 *  or a cell index), so it need not be parsed until a query reaches it. */
struct EncCellEntry {
  std::string path;
  GeoRect bounds;
};

class EncVectorSource : public IVectorSource {
 public:
  EncVectorSource();
  ~EncVectorSource() override;

  EncVectorSource(const EncVectorSource&) = delete;
  EncVectorSource& operator=(const EncVectorSource&) = delete;

  // `path` is either one base cell (`US5CHSDC.000`) or a directory holding
  // cells anywhere below it (an exchange set root, or a hand-made folder —
  // EnumerateEncCells finds them by extension, never by the ENC_ROOT layout).
  //
  // The Appendix A catalogue is looked for in `catalog_dir`; when that is
  // empty it is looked for beside the cells, walking up to three parents (the
  // delivered NOAA sets put the CSVs at the exchange-set root while cells sit
  // in per-cell folders). Failing to find it is an error — see the note above.
  Status Open(const std::string& path) override;
  Status Open(const std::string& path, const std::string& catalog_dir);

  // Opens an EXPLICIT set of cells, in the order given, instead of everything
  // under a directory.
  //
  // This is what a caller with a catalog wants, and the directory forms above
  // are not a substitute for it: an exchange set holds several USAGE BANDS
  // over the same water (Charleston ships bands 2, 3, 4 and 5), so a source
  // opened on the shared root serves all of them at once and a chart drawn
  // from it stacks a 1:1,000,000 general cell under a 1:12,000 harbour one.
  // A band is a scale, not a place; picking cells by name is the only way to
  // say which one is wanted.
  //
  // A cell that cannot be read is skipped and listed in unreadable_cells();
  // Open fails only when no cell is readable.
  Status OpenCells(const std::vector<std::string>& cell_paths,
                   const std::string& catalog_dir);

  /** Opens `cells` without parsing any. A cell is parsed when a Query area
   *  first meets its bounds; cells beyond SetMaxResidentCells are dropped
   *  least recently used first. Layers() is every acronym in the catalogue.
   *  Not thread-safe: Query and Describe may parse or drop cells. */
  Status OpenIndexed(const std::vector<EncCellEntry>& cells,
                     const std::string& catalog_dir);

  /** Cap on parsed cells kept between queries; 0 (the default) keeps all.
   *  A single query that needs more cells keeps them until the next one. */
  void SetMaxResidentCells(size_t n);
  size_t resident_cell_count() const;
  /** Drops every parsed cell of an OpenIndexed source; no-op otherwise. */
  void ReleaseCells();
  /** Cells skipped because they could not be read, each "path: reason". */
  const std::vector<std::string>& unreadable_cells() const;

  bool IsOpen() const override;

  GeoRect Bounds() const override;
  std::vector<std::string> Layers() const override;
  Status Query(const VectorQuery& q, std::vector<VectorFeature>* out) override;
  Status Describe(const FeatureRef& ref, FeatureDescription* out) override;

  // Cells opened, in the order their tile ids are numbered.
  size_t cell_count() const;
  const std::string& cell_path(size_t index) const;
  // Non-empty when any parsed cell has an update that could not be applied or
  // was cancelled (S57Cell::StalenessWarning, one line per cell). An
  // OpenIndexed source reports only the cells resident now.
  std::string StalenessWarning() const;

  // Features skipped by the last Query because their SCAMIN was smaller than
  // the query scale. Diagnostics; proves the thinning actually fires.
  size_t last_query_scamin_skipped() const;

 private:
  Status QueryCells(const VectorQuery& q, std::vector<VectorFeature>* out);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fv

#endif  // FV_ENC_VECTOR_SOURCE_H_
