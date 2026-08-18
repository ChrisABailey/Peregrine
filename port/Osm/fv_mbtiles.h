// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_mbtiles.h — the MBTiles container (OSM phase O1).
//
// An MBTiles file is pure SQLite: a `metadata` name/value table and a `tiles`
// table of blobs keyed by (zoom_level, tile_column, tile_row). That is the
// whole format, which is exactly why the plan picked it ahead of PMTiles —
// the port already had the SQLite RAII layer the L2 catalog runs on.
//
// This class reads the container ONLY. It does not decode a tile (fv_mvt.h
// does) and it does not know what a feature is (fv_osm_vector_source.h does).
//
// TWO THINGS THE DELIVERED DATA TAUGHT, both handled here rather than by
// every caller:
//
//   * `tile_row` is TMS (row 0 = south). The rest of the port is XYZ. The
//     flip happens at this boundary and nowhere else.
//   * The `bounds` metadata value CANNOT BE TRUSTED. The 2026-08-04 cut of
//     TestData's us-south file (Tilemaker) declared an east edge of exactly
//     0.000000 degrees, a thousand miles into the Atlantic past its
//     easternmost tile. (The 2026-08-17 re-cut declares the same 0.000000 and
//     is now telling the truth — its ocean tiles really do run to the
//     Greenwich meridian — which is exactly why the reader must derive rather
//     than believe: the same number was wrong in one file and right in the
//     next.) So the tile pyramid is authoritative — `Bounds()` derives
//     coverage from the tiles that exist — and the declared value is kept
//     separately, for a caller that wants to report the discrepancy.

#ifndef FV_MBTILES_H_
#define FV_MBTILES_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fv_web_mercator.h"
#include "fvkit/geo.h"

namespace fv {

// One entry of the `json` metadata value's `vector_layers` array — the
// TileJSON inventory of what the tiles contain. It is the only place a layer's
// own zoom range and field list is written down, so it is what
// IVectorSource::Layers() should answer from: a layer list scraped from
// whichever tile happened to be read first would change as the map is panned.
struct MbtilesLayerInfo {
  std::string id;
  std::string description;
  int minzoom = 0;
  int maxzoom = 0;
  // (field name, declared type) as TileJSON writes them: "String", "Number",
  // "Boolean". Advisory — a tile is free to store a number as a string.
  std::vector<std::pair<std::string, std::string>> fields;
};

class MbtilesFile {
 public:
  MbtilesFile();
  ~MbtilesFile();
  MbtilesFile(const MbtilesFile&) = delete;
  MbtilesFile& operator=(const MbtilesFile&) = delete;

  // Opens READ-ONLY. An MBTiles file is a published artefact — nothing in
  // this port should be able to write to one by accident, and a read-only
  // handle also opens a file on read-only media.
  Status Open(const std::string& path);
  void Close();
  bool IsOpen() const;
  const std::string& path() const;

  // --- metadata, verbatim ---------------------------------------------------
  const std::string& name() const;
  const std::string& format() const;        // "pbf" for vector tiles
  const std::string& description() const;
  const std::string& attribution() const;   // ODbL for OSM data — display it
  const std::string& version() const;
  const std::string& type() const;          // "baselayer" / "overlay"
  int min_zoom() const;
  int max_zoom() const;
  GeoPoint center() const;
  int center_zoom() const;

  // Any metadata key, including ones this class does not model.
  const std::string* Metadata(const std::string& key) const;

  // The `bounds` value as the file declares it, and whether it parsed at all.
  // See the header comment: this is NOT the coverage to query against.
  bool has_declared_bounds() const;
  GeoRect declared_bounds() const;

  // Coverage derived from the tiles that actually exist, at the deepest zoom
  // present (computed once, on demand, then cached — the scan is an
  // index-only min/max over one zoom level: ~100 ms for 594,419 tiles).
  GeoRect Bounds() const;

  // True when the declared bounds disagree with the pyramid by more than one
  // tile at the zoom the pyramid was measured at. Diagnostic only.
  bool declared_bounds_disagree() const;

  const std::vector<MbtilesLayerInfo>& layers() const;
  // Non-fatal complaints from Open (an unparseable `json`, a bad `bounds`).
  // Loud enough to surface, never enough to refuse the file.
  const std::vector<std::string>& warnings() const;

  // --- tiles ---------------------------------------------------------------

  // XYZ coordinates in, raw stored blob out (still compressed — MvtTile
  // handles that). kNotFound when the pyramid has no such tile, which is the
  // normal answer outside the data's coverage and is NOT an error.
  Status ReadTile(const webmerc::TileId& t, std::string* out) const;
  bool HasTile(const webmerc::TileId& t) const;

  // Extent of one zoom level in XYZ tile indices, inclusive. `count` is the
  // number of tiles stored at that zoom (which is <= the box's area: a
  // pyramid is not required to be dense, and this one is not).
  struct ZoomExtent {
    int min_x = 0, max_x = -1;
    int min_y = 0, max_y = -1;
    int64_t count = 0;
    bool empty() const { return count == 0; }
  };
  Status ZoomExtentOf(int z, ZoomExtent* out) const;

  // Every zoom that holds at least one tile, ascending.
  Status ZoomLevels(std::vector<int>* out) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fv

#endif  // FV_MBTILES_H_
