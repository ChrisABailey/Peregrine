// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_osm_vector_source.h — OSM vector tiles at the FvKit vector seam
// (OSM phase O1). The port's THIRD IVectorSource, after DNC (V5a) and ENC
// (E3a), and the one that had the least to invent: the seam, the rule layer,
// the retained scene and the renderer were all already there.
//
// WHAT IS DIFFERENT ABOUT A TILE PYRAMID, and therefore what this file owns:
//
//   * Scale does not thin features, it CHOOSES A ZOOM. DNC picks a library
//     and ENC picks a usage band; both are "which data to read" (plan
//     section 5.4) and so is this — the difference is that a pyramid's levels
//     are pre-generalized by the cutter, so the answer is arithmetic
//     (webmerc::ZoomForScale) rather than a catalog lookup.
//   * The unit of I/O is a tile, so a query is a tile RANGE, and a query with
//     no scale at all would be the whole world at maxzoom. That is 594,419
//     tiles in the delivered file, so an automatic zoom is capped by tile
//     count rather than trusted (see SetMaxTilesPerQuery).
//   * A tile is decoded once and cached; panning re-reads almost nothing.
//
// THE TILE BUFFER, and what O3 did about it. MVT tiles carry a BUFFER of their
// neighbours' geometry so that a renderer can draw a wide line across a tile
// seam. A feature lying entirely inside another tile's box is dropped here
// (exact for points, and what stops every edge label being drawn twice). A
// line or area that STRADDLES a seam is in BOTH tiles, and used to be emitted
// whole by both — the same ink drawn twice in the overlap, which a semi-
// transparent fill shows as a bright cross-hatch of tile edges. O3 clips such
// geometry to its own tile's box (SetClipToTile, on by default), so each tile
// contributes exactly its own share and the two halves meet on the seam.
// Points are never clipped: for them the bounds test above is already exact.
//
// OVERZOOM. The pyramid stops at its maxzoom (z14 in the delivered file); the
// display does not. Past that the read zoom is CLAMPED and the style's is NOT:
// z14 geometry is drawn under z15, z16, … rules, which is what every slippy
// map does when it runs out of levels and is why the picture keeps getting
// bigger instead of stopping (or, if the style were clamped too, freezing at
// z14 widths while the map kept zooming). The only thing this class owes that
// arrangement is honesty about it — last_query_overzoom() reports the gap.

#ifndef FV_OSM_VECTOR_SOURCE_H_
#define FV_OSM_VECTOR_SOURCE_H_

#include <memory>
#include <string>
#include <vector>

#include "fv_mbtiles.h"  // also brings fv_web_mercator.h (TileId)
#include "fvkit/vector/vector.h"

namespace fv {

class OsmVectorSource : public IVectorSource {
 public:
  OsmVectorSource();
  ~OsmVectorSource() override;

  // `path` is an .mbtiles file. Fails on a raster pyramid (format != "pbf"):
  // a raster MBTiles is a perfectly good data source, but it is an
  // IRasterSource, not this.
  Status Open(const std::string& path) override;
  bool IsOpen() const override;

  GeoRect Bounds() const override;
  std::vector<std::string> Layers() const override;
  Status Query(const VectorQuery& q, std::vector<VectorFeature>* out) override;
  Status Describe(const FeatureRef& ref, FeatureDescription* out) override;

  // --- the name index (search-plan-COMPLETE.md, S3) ------------------------
  //
  // A pack built by port/Osm/tools/fvnames.cpp carries a `search_names` table
  // of everything it holds a name for, and these two are how a search reaches
  // it: `HasNameIndex` is true from Open, and `SearchNames` is one SELECT
  // whatever the size of the pyramid. A pack without one answers false and
  // kUnsupported, and VectorMapOverlay falls back to reading tiles.
  //
  // THE REFS COME BACK LIVE. The index stores z/x/y and a layer NAME, because
  // FeatureRef::tile is an index this object assigns as it goes and means
  // nothing in another process; interning them here is what lets Describe()
  // work on a row that was found without a single tile being read.
  bool HasNameIndex() const override;
  Status SearchNames(const VectorNameQuery& q,
                     std::vector<VectorNameHit>* out) override;

  // The pyramid tile and layer name a ref came from — the inverse of the
  // interning above, and what the index BUILDER writes down (it gets refs out
  // of an ordinary query and has to store something durable). False for a ref
  // this source never minted.
  bool ResolveRef(const FeatureRef& ref, webmerc::TileId* tile,
                  std::string* layer) const;

  // --- knobs ---------------------------------------------------------------

  // Display pixel pitch used to turn a map scale into a zoom level. Defaults
  // to the port's reference pitch (proj.h's kNativeDisplayMmPerPixel, 0.25),
  // and an application that knows its real pitch should pass the SAME value
  // it gave MapProjection — E5's lesson about one physical property having
  // one owner.
  void SetDisplayMmPerPixel(double mm_per_pixel);
  double display_mm_per_pixel() const;

  // Force a zoom regardless of the query's scale. < 0 restores automatic
  // choice. An override is honoured EXACTLY — it is not tile-capped, because
  // a caller who names a zoom has said what it wants.
  void SetZoomOverride(int z);
  int zoom_override() const;

  // Guard for an automatic zoom: the derived level is stepped coarser until
  // the query's tile range fits, so a scale-less bulk query costs a readable
  // number of tiles instead of the whole pyramid. Default 64.
  void SetMaxTilesPerQuery(size_t n);

  // Decoded tiles kept in an LRU. Default 64; 0 is read as 1, since a cache
  // that cannot hold the tile currently being read is not a cache setting.
  void SetTileCacheCapacity(size_t n);

  // Clip line and area geometry to the tile it came from, so a feature that
  // straddles a seam is not drawn once per tile it appears in (see the header
  // comment). On by default. Turning it off restores the O1 behaviour, which
  // is the one to use when a caller wants a feature's WHOLE geometry — the
  // O4 road graph will, since a routable edge cut at a tile seam is two edges.
  void SetClipToTile(bool on);
  bool clip_to_tile() const;

  // Tag names consulted, in order, for a feature's style_key; the first one
  // present wins and the layer name is the fallback. Default {"class"}, which
  // is the OpenMapTiles dispatch key. (The seam's style_key is a string on
  // purpose: VPF puts a FACC code there, ENC an S-57 acronym, OSM a tag.)
  void SetStyleKeyTags(std::vector<std::string> tags);

  // --- diagnostics ---------------------------------------------------------
  int last_query_zoom() const;
  size_t last_query_tiles_read() const;     // tiles the pyramid had
  size_t last_query_tiles_missing() const;  // tiles it did not (normal)
  bool last_query_zoom_capped() const;      // the tile guard stepped in
  bool last_query_truncated() const;        // max_features stopped it
  // Features dropped because they lay entirely in another tile's buffer.
  size_t last_query_buffer_dropped() const;
  // Features whose geometry was cut down to its tile's box.
  size_t last_query_clipped() const;
  // Emissions clipping removed entirely: geometry whose BOX overlapped this
  // tile (or the query) while none of its ink did — an L-shaped road hugging
  // the seam is the everyday case. Counted per emitted feature, so one
  // multipolygon can contribute several.
  size_t last_query_clipped_away() const;
  // How far the display is zoomed past what the pyramid holds: the zoom the
  // query's scale asked for, minus the zoom actually read. 0 normally;
  // positive means z14 geometry is being drawn at z15+ (see the header).
  double last_query_overzoom() const;
  // Layer names met in tiles but absent from the file's `json` inventory.
  const std::vector<std::string>& undeclared_layers() const;

  const MbtilesFile& file() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fv

#endif  // FV_OSM_VECTOR_SOURCE_H_
