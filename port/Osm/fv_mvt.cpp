// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fv_mvt.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <vtzero/vector_tile.hpp>

namespace fv {
namespace {

// --- property values -------------------------------------------------------

// A number as short a decimal as still reads back to the same value. MVT
// stores floats and doubles, and the seam carries attributes as TEXT, so this
// is a lossy-looking step that must not actually lose anything: a pinned test
// value has to survive a round trip, and "%f" would turn 1e-7 into "0.000000"
// while "%.17g" would turn 0.1 into "0.10000000000000001". Shortest
// round-tripping is the only representation that is both stable and honest.
template <typename T>
std::string ShortestRoundTrip(T v) {
  if (std::isnan(v)) return "nan";
  if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
  char buf[64];
  const int max_digits = std::numeric_limits<T>::max_digits10;
  for (int prec = 1; prec < max_digits; ++prec) {
    std::snprintf(buf, sizeof(buf), "%.*g", prec, static_cast<double>(v));
    if (static_cast<T>(std::strtod(buf, nullptr)) == v) return buf;
  }
  std::snprintf(buf, sizeof(buf), "%.*g", max_digits, static_cast<double>(v));
  return buf;
}

std::string ValueToString(const vtzero::property_value& v) {
  switch (v.type()) {
    case vtzero::property_value_type::string_value: {
      const auto s = v.string_value();
      return std::string(s.data(), s.size());
    }
    case vtzero::property_value_type::float_value:
      return ShortestRoundTrip(v.float_value());
    case vtzero::property_value_type::double_value:
      return ShortestRoundTrip(v.double_value());
    case vtzero::property_value_type::int_value:
      return std::to_string(v.int_value());
    case vtzero::property_value_type::uint_value:
      return std::to_string(v.uint_value());
    case vtzero::property_value_type::sint_value:
      return std::to_string(v.sint_value());
    case vtzero::property_value_type::bool_value:
      // "true"/"false", which is what the rule layer's predicate parser and
      // every OpenMapTiles style expression expect to compare against.
      return v.bool_value() ? "true" : "false";
  }
  return {};
}

// --- geometry --------------------------------------------------------------

// Tile-local integer -> degrees. `extent` is the layer's own grid resolution
// (4096 by convention, but the layer declares it and Tilemaker/Planetiler are
// both free to differ), so the vertex is a fraction of the tile and the tile
// is a fraction of the world.
class VertexMapper {
 public:
  VertexMapper(const webmerc::TileId& tile, uint32_t extent)
      : tile_(tile),
        inv_extent_(extent > 0 ? 1.0 / static_cast<double>(extent)
                               : 1.0 / 4096.0) {}

  GeoPoint operator()(const vtzero::point& p) const {
    GeoPoint g;
    g.lon = webmerc::TileToLon(tile_.x + p.x * inv_extent_, tile_.z);
    g.lat = webmerc::TileToLat(tile_.y + p.y * inv_extent_, tile_.z);
    return g;
  }

 private:
  webmerc::TileId tile_;
  double inv_extent_;
};

// vtzero calls back into these while walking the command integers. Each one
// appends whole parts to the feature it was handed.
struct PointHandler {
  MvtFeature* f;
  const VertexMapper* map;

  void points_begin(uint32_t count) { f->parts.reserve(count); }
  void points_point(const vtzero::point p) { f->parts.push_back({(*map)(p)}); }
  void points_end() const {}
};

struct LineHandler {
  MvtFeature* f;
  const VertexMapper* map;

  void linestring_begin(uint32_t count) {
    f->parts.emplace_back();
    f->parts.back().reserve(count);
  }
  void linestring_point(const vtzero::point p) {
    f->parts.back().push_back((*map)(p));
  }
  void linestring_end() const {}
};

struct RingHandler {
  MvtFeature* f;
  const VertexMapper* map;
  size_t* degenerate;
  size_t* orphan;
  bool seen_outer = false;

  void ring_begin(uint32_t count) {
    f->parts.emplace_back();
    f->parts.back().reserve(count);
  }
  void ring_point(const vtzero::point p) { f->parts.back().push_back((*map)(p)); }

  void ring_end(vtzero::ring_type type) {
    // A zero-area ring encloses nothing; a hole with no outer ring yet has
    // nothing to be a hole IN (spec 4.3.4.4). Both are dropped and counted.
    if (type == vtzero::ring_type::invalid) {
      ++*degenerate;
      f->parts.pop_back();
      return;
    }
    if (type == vtzero::ring_type::inner && !seen_outer) {
      ++*orphan;
      f->parts.pop_back();
      return;
    }
    if (type == vtzero::ring_type::outer) seen_outer = true;
    f->ring_outer.push_back(type == vtzero::ring_type::outer);
  }
};

void ComputeBounds(MvtFeature* f) {
  bool first = true;
  for (const auto& part : f->parts) {
    for (const GeoPoint& p : part) {
      if (first) {
        f->bounds.ll = f->bounds.ur = p;
        first = false;
        continue;
      }
      if (p.lat < f->bounds.ll.lat) f->bounds.ll.lat = p.lat;
      if (p.lat > f->bounds.ur.lat) f->bounds.ur.lat = p.lat;
      if (p.lon < f->bounds.ll.lon) f->bounds.ll.lon = p.lon;
      if (p.lon > f->bounds.ur.lon) f->bounds.ur.lon = p.lon;
    }
  }
  // NOTE: a tile never spans the antimeridian internally — the grid cuts
  // there — so a plain min/max box is correct here and needs none of GeoRect's
  // crossing machinery. Column 0 and column 2^z-1 abut it from either side.
}

}  // namespace

// ---------------------------------------------------------------------------
// Inflate
// ---------------------------------------------------------------------------

Status MvtTile::Inflate(const void* data, size_t size, std::string* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "Inflate: null out");
  out->clear();
  if (data == nullptr || size == 0) return Status::Ok();

  const unsigned char* bytes = static_cast<const unsigned char*>(data);

  // Sniff the framing. MBTiles' own spec says tile_data "SHOULD be gzipped"
  // for pbf; Tilemaker and Planetiler both do, but a hand-built tile or a
  // tile lifted out of a .pbf directory is raw, and some writers use zlib.
  // A protobuf tile always starts with a field tag whose low 3 bits are a
  // wire type <= 5, so 0x1f (gzip) and 0x78 (zlib's usual CMF) cannot be
  // confused with the start of a Tile message in practice.
  const bool gzip = size >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b;
  const bool zlib_framed =
      size >= 2 && (bytes[0] & 0x0f) == 8 &&
      ((static_cast<unsigned>(bytes[0]) << 8 | bytes[1]) % 31) == 0;
  if (!gzip && !zlib_framed) {
    out->assign(reinterpret_cast<const char*>(bytes), size);
    return Status::Ok();
  }

  z_stream zs;
  std::memset(&zs, 0, sizeof(zs));
  // 15 = the largest window; +32 auto-detects gzip vs zlib framing.
  if (inflateInit2(&zs, 15 + 32) != Z_OK)
    return Status::Error(kIoError, "mvt: inflateInit2 failed");

  zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(bytes));
  zs.avail_in = static_cast<uInt>(size);

  // A ceiling, because the expansion ratio of a deflate stream is attacker-
  // controlled and a tile is not. The densest tile in the delivered pyramid
  // inflates to 257 KB; 64 MB is four orders of magnitude of headroom and
  // still turns a decompression bomb into an error message.
  constexpr size_t kMaxInflated = 64u << 20;

  // Tiles run from 23 bytes (an empty one) to a few hundred KB; grow from a
  // guess rather than asking zlib twice.
  std::string buf;
  buf.resize(size * 4 + 1024);
  size_t written = 0;
  int rc = Z_OK;
  for (;;) {
    zs.next_out = reinterpret_cast<Bytef*>(&buf[written]);
    zs.avail_out = static_cast<uInt>(buf.size() - written);
    rc = inflate(&zs, Z_NO_FLUSH);
    written = buf.size() - zs.avail_out;
    if (rc == Z_STREAM_END) break;
    if (rc != Z_OK) {
      const char* msg = zs.msg != nullptr ? zs.msg : "corrupt deflate stream";
      inflateEnd(&zs);
      return Status::Error(kIoError, std::string("mvt: inflate: ") + msg);
    }
    if (zs.avail_out == 0) {
      if (buf.size() >= kMaxInflated) {
        inflateEnd(&zs);
        return Status::Error(kIoError, "mvt: tile inflates past 64 MB");
      }
      buf.resize(std::min(buf.size() * 2, kMaxInflated));
      continue;
    }
    if (zs.avail_in == 0) {
      // Z_OK with input exhausted and output to spare = truncated stream.
      inflateEnd(&zs);
      return Status::Error(kIoError, "mvt: truncated deflate stream");
    }
  }
  inflateEnd(&zs);
  buf.resize(written);
  out->swap(buf);
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------------

Status MvtTile::Decode(const void* data, size_t size,
                       const webmerc::TileId& tile) {
  layers_.clear();
  degenerate_rings_ = 0;
  orphan_holes_ = 0;
  skipped_features_ = 0;
  first_skip_reason_.clear();
  tile_ = tile;

  if (tile.z < 0 || tile.z > 30)
    return Status::Error(kInvalidArg, "mvt: zoom out of range");

  std::string pbf;
  const Status s = Inflate(data, size, &pbf);
  if (!s.ok()) return s;
  if (pbf.empty()) return Status::Ok();  // an empty tile is a valid tile

  // Everything below can throw: protozero for a malformed message, vtzero for
  // a malformed geometry. Nothing else in the port throws, so the seam is
  // here and it is total.
  try {
    vtzero::vector_tile vt{vtzero::data_view{pbf.data(), pbf.size()}};
    while (vtzero::layer layer = vt.next_layer()) {
      MvtLayer out;
      out.name.assign(layer.name().data(), layer.name().size());
      out.version = layer.version();
      out.extent = layer.extent();
      out.features.reserve(layer.num_features());

      const VertexMapper map(tile, out.extent);

      while (vtzero::feature feature = layer.next_feature()) {
        MvtFeature f;
        f.id = feature.id();
        f.has_id = feature.has_id();

        try {
          switch (feature.geometry_type()) {
            case vtzero::GeomType::POINT:
              f.type = VectorGeometryType::kPoint;
              vtzero::decode_point_geometry(feature.geometry(),
                                            PointHandler{&f, &map});
              break;
            case vtzero::GeomType::LINESTRING:
              f.type = VectorGeometryType::kLine;
              vtzero::decode_linestring_geometry(feature.geometry(),
                                                 LineHandler{&f, &map});
              break;
            case vtzero::GeomType::POLYGON: {
              f.type = VectorGeometryType::kArea;
              RingHandler h{&f, &map, &degenerate_rings_, &orphan_holes_};
              vtzero::decode_polygon_geometry(feature.geometry(), h);
              break;
            }
            default:
              throw std::runtime_error("unknown geometry type");
          }
        } catch (const std::exception& e) {
          // One broken feature must not cost the whole tile: the rest of the
          // chart is still worth drawing, and the count says how much was
          // lost. (This is why the sweep test asserts the count is zero over
          // real data rather than trusting the absence of an error.)
          ++skipped_features_;
          if (first_skip_reason_.empty()) first_skip_reason_ = e.what();
          continue;
        }
        if (f.parts.empty()) continue;

        feature.for_each_property([&f](const vtzero::property& p) {
          f.tags.emplace_back(std::string(p.key().data(), p.key().size()),
                              ValueToString(p.value()));
          return true;
        });

        ComputeBounds(&f);
        out.features.push_back(std::move(f));
      }
      layers_.push_back(std::move(out));
    }
  } catch (const std::exception& e) {
    layers_.clear();
    return Status::Error(kIoError, std::string("mvt: ") + e.what());
  }
  return Status::Ok();
}

const MvtLayer* MvtTile::Layer(const std::string& name) const {
  for (const MvtLayer& l : layers_)
    if (l.name == name) return &l;
  return nullptr;
}

size_t MvtTile::feature_count() const {
  size_t n = 0;
  for (const MvtLayer& l : layers_) n += l.features.size();
  return n;
}

}  // namespace fv
