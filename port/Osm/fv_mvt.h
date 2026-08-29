// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_mvt.h — one Mapbox Vector Tile, decoded (OSM phase O1).
//
// MVT 2.1 is protobuf; the wire reading is protozero + vtzero (BSD-2, fetched
// in port/third_party — see the decision there). What this file owns is
// everything ABOVE the wire format:
//
//   * gzip/zlib/raw sniffing, because MBTiles stores the blob compressed and
//     the spec does not say which framing;
//   * turning tile-local integers into WGS-84 degrees ONCE, per vertex, so no
//     downstream code ever sees a tile coordinate (fv_web_mercator.h);
//   * turning the property-value variant into text, since the vector seam
//     carries attributes as strings;
//   * turning vtzero's exceptions into fv::Status, since nothing else in the
//     port throws.
//
// A decoded tile OWNS its geometry, so the compressed blob and the inflated
// buffer are both free to go when Decode returns.
//
// NOT bit-faithful to anything: there is no FalconView MVT reader to be
// faithful TO. This is a spec-driven reader like the ISO 8211 one (E1), and
// the deviations it does make are listed on MvtTile's diagnostics.

#ifndef FV_MVT_H_
#define FV_MVT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "fv_web_mercator.h"
#include "fvkit/geo.h"
#include "fvkit/vector/vector.h"

namespace fv {

// One feature as the tile authored it, already in degrees.
struct MvtFeature {
  // MVT feature ids are optional and are NOT unique across a layer in
  // practice (OpenMapTiles reuses OSM ids, and a way split across tiles keeps
  // its id). `has_id` distinguishes "id 0" from "no id"; identify uses the
  // FeatureRef the source assigns, never this.
  uint64_t id = 0;
  bool has_id = false;

  VectorGeometryType type = VectorGeometryType::kLine;

  // kPoint: one point per part. kLine: one part per connected run.
  // kArea: rings, in the order the tile wrote them — see `ring_outer`.
  std::vector<std::vector<GeoPoint>> parts;

  // kArea only, parallel to `parts`: true where the ring's signed area says
  // OUTER (MVT 2.1 section 4.3.4.4 winding), false where it says a hole. A
  // POLYGON geometry may hold SEVERAL outer rings — that is a multipolygon —
  // so this cannot collapse into the seam's "part[0] outer, rest holes" until
  // someone splits it. OsmVectorSource is that someone.
  std::vector<bool> ring_outer;

  // Tags in the order the tile listed them, decoded to text.
  std::vector<std::pair<std::string, std::string>> tags;

  GeoRect bounds;

  const std::string* Tag(const std::string& key) const {
    for (const auto& kv : tags)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
};

struct MvtLayer {
  std::string name;
  uint32_t version = 0;
  uint32_t extent = 4096;
  std::vector<MvtFeature> features;
};

class MvtTile {
 public:
  MvtTile() = default;

  // Decode one tile blob. `data` may be gzip-framed (MBTiles' convention),
  // zlib-framed, or a raw protobuf; all three are accepted.
  //
  // An EMPTY payload is not an error: tile cutters write a zero-byte tile for
  // a cell inside the pyramid's box that has no features (594,419 of the
  // us-south file's z14 tiles are a 23-byte gzip of nothing), and a caller
  // that treated that as a failure would report an error over half of the
  // ocean. It decodes to zero layers.
  Status Decode(const void* data, size_t size, const webmerc::TileId& tile);

  // Decompression on its own, exposed because it is the half a caller might
  // want without the parse (and because it is worth testing directly).
  static Status Inflate(const void* data, size_t size, std::string* out);

  const webmerc::TileId& tile() const { return tile_; }
  const std::vector<MvtLayer>& layers() const { return layers_; }
  const MvtLayer* Layer(const std::string& name) const;

  size_t feature_count() const;

  // --- diagnostics: what the decoder dropped, and why ---------------------
  // Counted rather than silently tolerated (E3a's rule for CS procedures).

  // Rings whose signed area was exactly zero. MVT calls that winding
  // "invalid"; such a ring encloses nothing, so it is dropped.
  size_t degenerate_rings() const { return degenerate_rings_; }
  // Holes that appeared before any outer ring in the same polygon geometry.
  // Malformed per section 4.3.4.4; dropped, since there is nothing to hole.
  size_t orphan_holes() const { return orphan_holes_; }
  // Features vtzero refused (bad geometry commands, unknown geometry type).
  // The rest of the tile is kept: one broken feature must not cost a chart.
  size_t skipped_features() const { return skipped_features_; }
  const std::string& first_skip_reason() const { return first_skip_reason_; }

 private:
  webmerc::TileId tile_;
  std::vector<MvtLayer> layers_;
  size_t degenerate_rings_ = 0;
  size_t orphan_holes_ = 0;
  size_t skipped_features_ = 0;
  std::string first_skip_reason_;
};

}  // namespace fv

#endif  // FV_MVT_H_
