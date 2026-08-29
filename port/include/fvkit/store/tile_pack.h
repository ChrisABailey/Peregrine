// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/store/tile_pack.h — FvKit L5 store: GeoPackage raster tile pyramids
// (OGC GeoPackage 1.2 "tiles" profile, EPSG:4326 geodetic tile matrix).
// The iOS deployment vehicle from the plan, and the pre-render target for
// the VPF/OSM tracks. Uses fvkit/detail/sqlite.h; tiles are PNG blobs.
//
// Writer: define the pack over a GeoRect, then render levels from a
// MapEngine (resolution mode: every level has one exact, latitude-
// independent dpp; level k+1 halves level k's dpp). Tile (0,0) is the
// pack's north-west corner, per the GeoPackage tile convention.
//
// Reader: TilePackRasterSource exposes ONE zoom level as a flat
// IRasterSource (ReadBlock stitches tiles; exact linear transforms from
// the level's dpp). The "gpkg" format registration enumerates one
// FrameInfo per level with path "<file>#z=<k>", so the catalog treats
// pyramid levels as series and BestSeriesForScale picks the level.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/detail/sqlite.h"
#include "fvkit/engine.h"
#include "fvkit/formats/enumerate.h"
#include "fvkit/formats/source.h"

namespace fv {

struct TileLevelInfo {
  int zoom = 0;
  int matrix_width = 0, matrix_height = 0;  // tiles
  double dpp_lat = 0, dpp_lon = 0;          // degrees per pixel
};

class TilePackWriter {
 public:
  TilePackWriter() = default;

  // Creates/overwrites a GeoPackage at path covering bounds (non-crossing
  // rect for now — packs are regional). tile_size is square, default 256.
  Status Create(const std::string& path, const std::string& table_name,
                const GeoRect& bounds, int tile_size = 256);

  // Renders one pyramid level from the engine (which must have its catalog
  // scanned; series_id 0 = all). Level 0 dpp is chosen so the whole pack
  // fits ~one tile; each level halves dpp. Skips fully-background tiles.
  Status WriteLevel(MapEngine& engine, int zoom, int64_t series_id = 0,
                    int* tiles_written = nullptr);

  Status Close();  // finalizes gpkg_contents

 private:
  double LevelDpp(int zoom) const;

  detail::SqliteDb db_;
  std::string table_;
  GeoRect bounds_;
  int tile_size_ = 256;
  double base_dpp_ = 0;  // zoom 0 dpp
};

class TilePackRasterSource : public IRasterSource {
 public:
  TilePackRasterSource();
  ~TilePackRasterSource() override;

  // path is "<file.gpkg>#z=<zoom>"; bare "<file.gpkg>" opens the deepest
  // (finest) level.
  Status Open(const std::string& path) override;
  GeoRect Bounds() const override;
  Status Info(ImageInfo* info) const override;
  Status ReadBlock(const PixelRect& px_rect, PixelBuffer* out) override;
  Status PixelToGeo(double px, double py, GeoPoint* p) const override;
  Status GeoToPixel(const GeoPoint& p, double* px, double* py) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class TilePackEnumerator : public IFrameEnumerator {
 public:
  TilePackEnumerator() = default;
  Status Begin(const std::string& dir) override;  // *.gpkg in dir (recursive)
  bool Next(FrameInfo* info) override;

 private:
  std::vector<FrameInfo> frames_;
  size_t next_ = 0;
};

// Reads level metadata from a pack (shared by reader/enumerator/tests).
Status ReadTilePackLevels(const std::string& gpkg_path, std::string* table,
                          GeoRect* bounds, std::vector<TileLevelInfo>* levels);

}  // namespace fv
