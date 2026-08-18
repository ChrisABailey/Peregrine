// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// MVT decoder tests (OSM phase O1).
//
// TWO HALVES, deliberately:
//
//   * HERMETIC — tiles built BYTE BY BYTE in this file from the MVT 2.1
//     protobuf schema, exactly as the ISO 8211 tests (E1) build S-57 records.
//     Nothing here reads a file, and nothing uses vtzero's own builder: a
//     test that encodes with the same library it decodes with proves the two
//     agree, not that either matches the spec.
//   * REAL DATA — the Atlanta z14 tile of TestData's us-south pyramid, whose
//     expected values come from an independent Python oracle written against
//     the spec, not from this decoder's output.

#include "fv_mvt.h"

#include <gtest/gtest.h>

#include "fv_mbtiles.h"
#include <zlib.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// --- a minimal protobuf writer, spec-side ----------------------------------

void PutVarint(std::string* s, uint64_t v) {
  do {
    uint8_t byte = v & 0x7f;
    v >>= 7;
    if (v != 0) byte |= 0x80;
    s->push_back(static_cast<char>(byte));
  } while (v != 0);
}

void PutKey(std::string* s, int field, int wire_type) {
  PutVarint(s, (static_cast<uint64_t>(field) << 3) | wire_type);
}

void PutVarintField(std::string* s, int field, uint64_t v) {
  PutKey(s, field, 0);
  PutVarint(s, v);
}

void PutLenField(std::string* s, int field, const std::string& payload) {
  PutKey(s, field, 2);
  PutVarint(s, payload.size());
  s->append(payload);
}

void PutFixed32Field(std::string* s, int field, float v) {
  PutKey(s, field, 5);
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  for (int i = 0; i < 4; ++i) s->push_back(static_cast<char>(bits >> (8 * i)));
}

void PutFixed64Field(std::string* s, int field, double v) {
  PutKey(s, field, 1);
  uint64_t bits;
  std::memcpy(&bits, &v, 8);
  for (int i = 0; i < 8; ++i) s->push_back(static_cast<char>(bits >> (8 * i)));
}

std::string PackedVarints(const std::vector<uint32_t>& v) {
  std::string s;
  for (uint32_t x : v) PutVarint(&s, x);
  return s;
}

uint32_t ZigZag(int32_t v) {
  return (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31);
}

// MVT geometry command integer: id in the low 3 bits, repeat count above.
uint32_t Cmd(int id, int count) {
  return (static_cast<uint32_t>(count) << 3) | static_cast<uint32_t>(id);
}
constexpr int kMoveTo = 1;
constexpr int kLineTo = 2;
constexpr int kClosePath = 7;

// --- MVT message builders ---------------------------------------------------

struct FeatureSpec {
  uint64_t id = 0;
  bool has_id = false;
  int geom_type = 0;  // 1 point, 2 linestring, 3 polygon
  std::vector<uint32_t> tags;
  std::vector<uint32_t> geometry;
};

std::string BuildFeature(const FeatureSpec& f) {
  std::string s;
  if (f.has_id) PutVarintField(&s, 1, f.id);
  if (!f.tags.empty()) PutLenField(&s, 2, PackedVarints(f.tags));
  PutVarintField(&s, 3, static_cast<uint64_t>(f.geom_type));
  if (!f.geometry.empty()) PutLenField(&s, 4, PackedVarints(f.geometry));
  return s;
}

std::string StringValue(const std::string& v) {
  std::string s;
  PutLenField(&s, 1, v);
  return s;
}
std::string FloatValue(float v) {
  std::string s;
  PutFixed32Field(&s, 2, v);
  return s;
}
std::string DoubleValue(double v) {
  std::string s;
  PutFixed64Field(&s, 3, v);
  return s;
}
std::string IntValue(int64_t v) {
  std::string s;
  PutVarintField(&s, 4, static_cast<uint64_t>(v));
  return s;
}
std::string UintValue(uint64_t v) {
  std::string s;
  PutVarintField(&s, 5, v);
  return s;
}
std::string SintValue(int32_t v) {
  std::string s;
  PutVarintField(&s, 6, ZigZag(v));
  return s;
}
std::string BoolValue(bool v) {
  std::string s;
  PutVarintField(&s, 7, v ? 1 : 0);
  return s;
}

struct LayerSpec {
  std::string name = "test";
  uint32_t version = 2;
  uint32_t extent = 4096;
  std::vector<std::string> keys;
  std::vector<std::string> values;  // already-encoded Value messages
  std::vector<FeatureSpec> features;
};

std::string BuildLayer(const LayerSpec& l) {
  std::string s;
  PutVarintField(&s, 15, l.version);
  PutLenField(&s, 1, l.name);
  for (const FeatureSpec& f : l.features) PutLenField(&s, 2, BuildFeature(f));
  for (const std::string& k : l.keys) PutLenField(&s, 3, k);
  for (const std::string& v : l.values) PutLenField(&s, 4, v);
  PutVarintField(&s, 5, l.extent);
  return s;
}

std::string BuildTile(const std::vector<LayerSpec>& layers) {
  std::string s;
  for (const LayerSpec& l : layers) PutLenField(&s, 3, BuildLayer(l));
  return s;
}

// gzip the way an MBTiles writer does, so Inflate's sniffing is exercised.
std::string Gzip(const std::string& in) {
  z_stream zs;
  std::memset(&zs, 0, sizeof(zs));
  EXPECT_EQ(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                         Z_DEFAULT_STRATEGY),
            Z_OK);
  std::string out;
  out.resize(deflateBound(&zs, in.size()) + 32);
  zs.next_in =
      const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in.data()));
  zs.avail_in = static_cast<uInt>(in.size());
  zs.next_out = reinterpret_cast<Bytef*>(&out[0]);
  zs.avail_out = static_cast<uInt>(out.size());
  EXPECT_EQ(deflate(&zs, Z_FINISH), Z_STREAM_END);
  out.resize(out.size() - zs.avail_out);
  deflateEnd(&zs);
  return out;
}

// Any tile works for geometry-shape tests; z0 makes the arithmetic legible
// (the whole world is one 4096-unit grid).
const fv::webmerc::TileId kZeroTile{0, 0, 0};

fv::Status DecodeRaw(fv::MvtTile* tile, const std::string& pbf,
                     const fv::webmerc::TileId& id = kZeroTile) {
  return tile->Decode(pbf.data(), pbf.size(), id);
}

}  // namespace

// ===========================================================================
// Hermetic: framing
// ===========================================================================

TEST(MvtContainer, EmptyPayloadIsAValidEmptyTile) {
  // Not a corner case: 594,419 of the delivered pyramid's z14 tiles are a
  // 23-byte gzip of nothing, and treating that as an error would fail over
  // half the Gulf of Mexico.
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, "").ok());
  EXPECT_TRUE(tile.layers().empty());
  EXPECT_EQ(tile.feature_count(), 0u);

  const std::string empty_gz = Gzip("");
  ASSERT_TRUE(DecodeRaw(&tile, empty_gz).ok());
  EXPECT_TRUE(tile.layers().empty());
}

TEST(MvtContainer, GzipZlibAndRawAllDecodeToTheSameTile) {
  LayerSpec l;
  l.features.push_back(
      FeatureSpec{0, false, 1, {}, {Cmd(kMoveTo, 1), ZigZag(10), ZigZag(20)}});
  const std::string raw = BuildTile({l});

  fv::MvtTile a, b;
  ASSERT_TRUE(DecodeRaw(&a, raw).ok());
  ASSERT_TRUE(DecodeRaw(&b, Gzip(raw)).ok());
  ASSERT_EQ(a.layers().size(), 1u);
  ASSERT_EQ(b.layers().size(), 1u);
  ASSERT_EQ(a.layers()[0].features.size(), 1u);
  ASSERT_EQ(b.layers()[0].features.size(), 1u);
  EXPECT_EQ(a.layers()[0].features[0].parts[0][0].lat,
            b.layers()[0].features[0].parts[0][0].lat);
}

TEST(MvtContainer, TruncatedCompressedStreamIsAnError) {
  LayerSpec l;
  l.features.push_back(
      FeatureSpec{0, false, 1, {}, {Cmd(kMoveTo, 1), ZigZag(1), ZigZag(1)}});
  std::string gz = Gzip(BuildTile({l}));
  ASSERT_GT(gz.size(), 12u);
  gz.resize(gz.size() - 6);

  fv::MvtTile tile;
  const fv::Status s = DecodeRaw(&tile, gz);
  EXPECT_EQ(s.code, fv::kIoError) << s.message;
}

TEST(MvtContainer, GarbageIsRejectedRatherThanPartlyBelieved) {
  // 0xff repeated is not a valid protobuf key; the whole tile must fail, and
  // must not leave half-decoded layers behind.
  fv::MvtTile tile;
  const std::string junk(64, '\xff');
  const fv::Status s = DecodeRaw(&tile, junk);
  EXPECT_FALSE(s.ok());
  EXPECT_TRUE(tile.layers().empty());
}

// ===========================================================================
// Hermetic: geometry
// ===========================================================================

TEST(MvtGeometry, PointsLandWhereTheTileGridSaysTheyDo) {
  LayerSpec l;
  l.extent = 4096;
  // Tile-local (2048, 2048) at z0 is the centre of the world.
  l.features.push_back(FeatureSpec{
      0, false, 1, {}, {Cmd(kMoveTo, 1), ZigZag(2048), ZigZag(2048)}});
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l})).ok());

  ASSERT_EQ(tile.layers().size(), 1u);
  const fv::MvtFeature& f = tile.layers()[0].features.at(0);
  EXPECT_EQ(f.type, fv::VectorGeometryType::kPoint);
  ASSERT_EQ(f.parts.size(), 1u);
  ASSERT_EQ(f.parts[0].size(), 1u);
  EXPECT_NEAR(f.parts[0][0].lat, 0.0, 1e-9);
  EXPECT_NEAR(f.parts[0][0].lon, 0.0, 1e-9);
  EXPECT_FALSE(f.has_id);
}

TEST(MvtGeometry, ExtentIsTheLayersOwnAndNotAssumedToBe4096) {
  LayerSpec l;
  l.extent = 512;
  l.features.push_back(
      FeatureSpec{0, false, 1, {}, {Cmd(kMoveTo, 1), ZigZag(256), ZigZag(256)}});
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l})).ok());
  EXPECT_EQ(tile.layers()[0].extent, 512u);
  const fv::GeoPoint p = tile.layers()[0].features[0].parts[0][0];
  EXPECT_NEAR(p.lat, 0.0, 1e-9);
  EXPECT_NEAR(p.lon, 0.0, 1e-9);
}

TEST(MvtGeometry, AMultiPointIsSeveralPartsOfOnePointEach) {
  LayerSpec l;
  l.features.push_back(FeatureSpec{0,
                                   false,
                                   1,
                                   {},
                                   {Cmd(kMoveTo, 3), ZigZag(1024), ZigZag(1024),
                                    ZigZag(1024), ZigZag(0), ZigZag(0),
                                    ZigZag(1024)}});
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l})).ok());
  const fv::MvtFeature& f = tile.layers()[0].features.at(0);
  ASSERT_EQ(f.parts.size(), 3u);
  for (const auto& part : f.parts) EXPECT_EQ(part.size(), 1u);
  // Deltas accumulate: the second point is at (2048, 1024).
  EXPECT_GT(f.parts[1][0].lon, f.parts[0][0].lon);
  EXPECT_DOUBLE_EQ(f.parts[1][0].lat, f.parts[0][0].lat);
}

TEST(MvtGeometry, ALinestringIsOnePartPerRun) {
  LayerSpec l;
  l.features.push_back(FeatureSpec{
      0,
      false,
      2,
      {},
      {Cmd(kMoveTo, 1), ZigZag(0), ZigZag(0), Cmd(kLineTo, 2), ZigZag(100),
       ZigZag(0), ZigZag(0), ZigZag(100), Cmd(kMoveTo, 1), ZigZag(500),
       ZigZag(500), Cmd(kLineTo, 1), ZigZag(50), ZigZag(50)}});
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l})).ok());
  const fv::MvtFeature& f = tile.layers()[0].features.at(0);
  EXPECT_EQ(f.type, fv::VectorGeometryType::kLine);
  ASSERT_EQ(f.parts.size(), 2u);
  EXPECT_EQ(f.parts[0].size(), 3u);
  EXPECT_EQ(f.parts[1].size(), 2u);
  EXPECT_TRUE(f.ring_outer.empty());
}

// A closed square, wound so its signed area is positive (outer) or negative
// (inner) in MVT's y-down tile space.
std::vector<uint32_t> Square(int x, int y, int size, bool clockwise) {
  std::vector<uint32_t> g{Cmd(kMoveTo, 1), ZigZag(x), ZigZag(y),
                          Cmd(kLineTo, 3)};
  if (clockwise) {
    g.push_back(ZigZag(size));
    g.push_back(ZigZag(0));
    g.push_back(ZigZag(0));
    g.push_back(ZigZag(size));
    g.push_back(ZigZag(-size));
    g.push_back(ZigZag(0));
  } else {
    g.push_back(ZigZag(0));
    g.push_back(ZigZag(size));
    g.push_back(ZigZag(size));
    g.push_back(ZigZag(0));
    g.push_back(ZigZag(0));
    g.push_back(ZigZag(-size));
  }
  g.push_back(Cmd(kClosePath, 1));
  return g;
}

TEST(MvtGeometry, PolygonRingsAreClassifiedByTheirWinding) {
  std::vector<uint32_t> g = Square(100, 100, 800, true);
  // A hole inside it: opposite winding, and the cursor is where the outer
  // ring's LAST point left it (back at the start, thanks to ClosePath's
  // implicit return not moving the cursor — so this offset is from (100,100)).
  const std::vector<uint32_t> hole = Square(200, 200, 200, false);
  g.insert(g.end(), hole.begin(), hole.end());

  LayerSpec l;
  l.features.push_back(FeatureSpec{0, false, 3, {}, g});
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l})).ok());

  const fv::MvtFeature& f = tile.layers()[0].features.at(0);
  EXPECT_EQ(f.type, fv::VectorGeometryType::kArea);
  ASSERT_EQ(f.parts.size(), 2u);
  ASSERT_EQ(f.ring_outer.size(), 2u);
  EXPECT_TRUE(f.ring_outer[0]);
  EXPECT_FALSE(f.ring_outer[1]);
  // ClosePath repeats the first point, so a 3-corner LineTo run is 5 points.
  EXPECT_EQ(f.parts[0].size(), 5u);
  EXPECT_EQ(tile.degenerate_rings(), 0u);
  EXPECT_EQ(tile.orphan_holes(), 0u);
}

TEST(MvtGeometry, AZeroAreaRingIsDroppedAndCounted) {
  // Three collinear points: closes, encloses nothing.
  const std::vector<uint32_t> g{Cmd(kMoveTo, 1),  ZigZag(10),  ZigZag(10),
                                Cmd(kLineTo, 2),  ZigZag(100), ZigZag(0),
                                ZigZag(100),      ZigZag(0),   Cmd(kClosePath, 1)};
  LayerSpec l;
  l.features.push_back(FeatureSpec{0, false, 3, {}, g});
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l})).ok());
  EXPECT_EQ(tile.degenerate_rings(), 1u);
  // Every ring was dropped, so the feature has no geometry and is not kept.
  EXPECT_EQ(tile.feature_count(), 0u);
}

TEST(MvtGeometry, AHoleWithNoOuterRingIsDroppedAndCounted) {
  const std::vector<uint32_t> g = Square(100, 100, 400, false);
  LayerSpec l;
  l.features.push_back(FeatureSpec{0, false, 3, {}, g});
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l})).ok());
  EXPECT_EQ(tile.orphan_holes(), 1u);
  EXPECT_EQ(tile.feature_count(), 0u);
}

TEST(MvtGeometry, ABrokenFeatureCostsItselfAndNotTheTile) {
  LayerSpec l;
  // LineTo with no MoveTo first — malformed per spec 4.3.4.3.
  l.features.push_back(FeatureSpec{
      0, false, 2, {}, {Cmd(kLineTo, 1), ZigZag(5), ZigZag(5)}});
  l.features.push_back(
      FeatureSpec{0, false, 1, {}, {Cmd(kMoveTo, 1), ZigZag(1), ZigZag(1)}});
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l})).ok());
  EXPECT_EQ(tile.skipped_features(), 1u);
  EXPECT_FALSE(tile.first_skip_reason().empty());
  EXPECT_EQ(tile.feature_count(), 1u);  // the good one survived
}

TEST(MvtGeometry, GeometryOutsideTheTileIsKeptBecauseThatIsTheBuffer) {
  // MVT tiles carry their neighbours' geometry so a wide line can be drawn
  // across a seam. Negative and past-extent coordinates are legal, and
  // clipping them here would put a gap at every tile boundary.
  LayerSpec l;
  l.features.push_back(FeatureSpec{
      0, false, 1, {}, {Cmd(kMoveTo, 1), ZigZag(-256), ZigZag(4352)}});
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l}), fv::webmerc::TileId{2, 1, 1}).ok());
  ASSERT_EQ(tile.feature_count(), 1u);
  const fv::GeoPoint p = tile.layers()[0].features[0].parts[0][0];
  const fv::GeoRect box = fv::webmerc::TileBounds(fv::webmerc::TileId{2, 1, 1});
  EXPECT_LT(p.lon, box.ll.lon);
  EXPECT_LT(p.lat, box.ll.lat);
}

// ===========================================================================
// Hermetic: properties
// ===========================================================================

TEST(MvtProperties, EveryValueTypeBecomesTheTextTheSeamCarries) {
  LayerSpec l;
  l.keys = {"s", "f", "d", "i", "u", "sint", "b"};
  l.values = {StringValue("motorway"), FloatValue(12.5f),
              DoubleValue(0.1),        IntValue(-7),
              UintValue(42),           SintValue(-1234),
              BoolValue(true)};
  FeatureSpec f{0, false, 1, {}, {Cmd(kMoveTo, 1), ZigZag(0), ZigZag(0)}};
  for (uint32_t i = 0; i < 7; ++i) {
    f.tags.push_back(i);
    f.tags.push_back(i);
  }
  l.features.push_back(f);

  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l})).ok());
  const fv::MvtFeature& out = tile.layers()[0].features.at(0);
  ASSERT_EQ(out.tags.size(), 7u);
  EXPECT_EQ(*out.Tag("s"), "motorway");
  EXPECT_EQ(*out.Tag("f"), "12.5");
  // The shortest decimal that reads back as the same double — NOT
  // "0.100000" and NOT "0.10000000000000001".
  EXPECT_EQ(*out.Tag("d"), "0.1");
  EXPECT_EQ(*out.Tag("i"), "-7");
  EXPECT_EQ(*out.Tag("u"), "42");
  EXPECT_EQ(*out.Tag("sint"), "-1234");
  EXPECT_EQ(*out.Tag("b"), "true");
  EXPECT_EQ(out.Tag("nope"), nullptr);
}

TEST(MvtProperties, TagOrderIsTheTilesOrder) {
  LayerSpec l;
  l.keys = {"z", "a"};
  l.values = {StringValue("last"), StringValue("first")};
  FeatureSpec f{0, false, 1, {0, 0, 1, 1},
                {Cmd(kMoveTo, 1), ZigZag(0), ZigZag(0)}};
  l.features.push_back(f);
  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({l})).ok());
  const auto& tags = tile.layers()[0].features[0].tags;
  ASSERT_EQ(tags.size(), 2u);
  EXPECT_EQ(tags[0].first, "z");
  EXPECT_EQ(tags[1].first, "a");
}

TEST(MvtProperties, LayerLookupAndIdsAreReported) {
  LayerSpec a;
  a.name = "water";
  a.features.push_back(
      FeatureSpec{99, true, 1, {}, {Cmd(kMoveTo, 1), ZigZag(0), ZigZag(0)}});
  LayerSpec b;
  b.name = "roads";
  b.version = 1;
  b.features.push_back(
      FeatureSpec{0, false, 1, {}, {Cmd(kMoveTo, 1), ZigZag(0), ZigZag(0)}});

  fv::MvtTile tile;
  ASSERT_TRUE(DecodeRaw(&tile, BuildTile({a, b})).ok());
  ASSERT_EQ(tile.layers().size(), 2u);
  ASSERT_NE(tile.Layer("water"), nullptr);
  ASSERT_NE(tile.Layer("roads"), nullptr);
  EXPECT_EQ(tile.Layer("nothing"), nullptr);
  EXPECT_EQ(tile.Layer("water")->version, 2u);
  EXPECT_EQ(tile.Layer("roads")->version, 1u);
  EXPECT_TRUE(tile.Layer("water")->features[0].has_id);
  EXPECT_EQ(tile.Layer("water")->features[0].id, 99u);
  EXPECT_FALSE(tile.Layer("roads")->features[0].has_id);
}

// ===========================================================================
// Real data: the Atlanta tile
// ===========================================================================

namespace {

std::string MbtilesPath() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/OSM/mbtiles/us-south.mbtiles";
  return fs::is_regular_file(p) ? p : std::string();
}

}  // namespace

#define SKIP_WITHOUT_MBTILES()                 \
  const std::string mb_path = MbtilesPath();   \
  if (mb_path.empty()) GTEST_SKIP() << "no OSM mbtiles test data"

namespace {

// Downtown Atlanta. The layer inventory, the feature counts and the two
// pinned coordinates below all come from the Python oracle, which now lives
// beside this file as test/mvt_oracle.py — run it against the .mbtiles to
// re-derive every number in MvtRealData after a data re-cut.
const fv::webmerc::TileId kAtlanta{14, 4351, 6558};

fv::Status ReadAndDecode(const std::string& path, const fv::webmerc::TileId& id,
                         fv::MvtTile* out) {
  fv::MbtilesFile file;
  const fv::Status open = file.Open(path);
  if (!open.ok()) return open;
  std::string blob;
  const fv::Status read = file.ReadTile(id, &blob);
  if (!read.ok()) return read;
  return out->Decode(blob.data(), blob.size(), id);
}

}  // namespace

TEST(MvtRealData, TheAtlantaTileDecodesToWhatTheOracleSaw) {
  SKIP_WITHOUT_MBTILES();
  fv::MvtTile tile;
  ASSERT_TRUE(ReadAndDecode(mb_path, kAtlanta, &tile).ok());

  EXPECT_EQ(tile.layers().size(), 13u);      // 12 before the 2026-08-17 re-cut
  EXPECT_EQ(tile.feature_count(), 7552u);    // 7389 before it

  // Per-layer counts, straight from the oracle's listing. Re-run against the
  // 2026-08-17 re-cut: ten of the twelve layers came back with the same count
  // they had at O1, landuse went 79 -> 238, and man_made is new.
  const struct {
    const char* name;
    size_t count;
  } kExpected[] = {
      {"place", 5},        {"poi", 694},        {"housenumber", 377},
      {"transportation", 4363}, {"transportation_name", 728},
      {"building", 923},   {"water", 21},       {"water_name", 2},
      {"aeroway", 3},      {"park", 1},         {"landuse", 238},
      {"landcover", 193},  {"man_made", 4},
  };
  for (const auto& e : kExpected) {
    const fv::MvtLayer* l = tile.Layer(e.name);
    ASSERT_NE(l, nullptr) << e.name;
    EXPECT_EQ(l->features.size(), e.count) << e.name;
    EXPECT_EQ(l->version, 2u) << e.name;
    EXPECT_EQ(l->extent, 4096u) << e.name;
  }

  // Nothing was dropped: the counters exist so this can be an assertion
  // rather than an absence of complaint.
  EXPECT_EQ(tile.skipped_features(), 0u) << tile.first_skip_reason();
  EXPECT_EQ(tile.orphan_holes(), 0u);
}

TEST(MvtRealData, AtlantaLandsWhereAtlantaIs) {
  SKIP_WITHOUT_MBTILES();
  fv::MvtTile tile;
  ASSERT_TRUE(ReadAndDecode(mb_path, kAtlanta, &tile).ok());

  const fv::MvtLayer* place = tile.Layer("place");
  ASSERT_NE(place, nullptr);
  const fv::MvtFeature* city = nullptr;
  for (const fv::MvtFeature& f : place->features) {
    const std::string* cls = f.Tag("class");
    if (cls != nullptr && *cls == "city") city = &f;
  }
  ASSERT_NE(city, nullptr);
  const std::string* name = city->Tag("name:latin");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(*name, "Atlanta");
  // Oracle: tile-local (1334, 1438) -> 33.7544686, -84.3898165.
  ASSERT_EQ(city->parts.size(), 1u);
  ASSERT_EQ(city->parts[0].size(), 1u);
  EXPECT_NEAR(city->parts[0][0].lat, 33.7544686, 1e-6);
  EXPECT_NEAR(city->parts[0][0].lon, -84.3898165, 1e-6);
  // Tilemaker writes no feature ids; that is a property of the data, and
  // pinning it is what keeps a later "ids are unique" assumption honest.
  EXPECT_FALSE(city->has_id);
  EXPECT_EQ(*city->Tag("rank"), "4");
}

TEST(MvtRealData, WaterIsAreaGeometryWithClosedOuterRings) {
  SKIP_WITHOUT_MBTILES();
  fv::MvtTile tile;
  ASSERT_TRUE(ReadAndDecode(mb_path, kAtlanta, &tile).ok());

  const fv::MvtLayer* water = tile.Layer("water");
  ASSERT_NE(water, nullptr);
  ASSERT_EQ(water->features.size(), 21u);

  const fv::MvtFeature& first = water->features[0];
  EXPECT_EQ(first.type, fv::VectorGeometryType::kArea);
  ASSERT_EQ(first.parts.size(), 1u);
  EXPECT_EQ(first.parts[0].size(), 7u);  // oracle: 7 points
  EXPECT_EQ(*first.Tag("class"), "lake");
  ASSERT_EQ(first.ring_outer.size(), 1u);
  EXPECT_TRUE(first.ring_outer[0]);
  // Oracle: first vertex (824, 4) -> 33.7608642, -84.3925524.
  EXPECT_NEAR(first.parts[0][0].lat, 33.7608642, 1e-6);
  EXPECT_NEAR(first.parts[0][0].lon, -84.3925524, 1e-6);

  for (const fv::MvtFeature& f : water->features) {
    ASSERT_EQ(f.ring_outer.size(), f.parts.size());
    for (const auto& ring : f.parts) {
      ASSERT_GE(ring.size(), 4u);
      EXPECT_DOUBLE_EQ(ring.front().lat, ring.back().lat);
      EXPECT_DOUBLE_EQ(ring.front().lon, ring.back().lon);
    }
  }
}

TEST(MvtRealData, EveryVertexLandsNearTheTileItCameFrom) {
  SKIP_WITHOUT_MBTILES();
  // The single pinned coordinate above proves the projection for one point.
  // This proves it for all 42,888 of them, which is what catches an extent
  // that was assumed rather than read, or a y-axis flipped the wrong way:
  // either mistake moves vertices by whole tiles.
  //
  // The tolerance is one FULL tile, not a token epsilon, because the buffer
  // in this data is genuinely large — measured at 53% of a tile — and a
  // renderer needs that geometry. Anything past a whole tile is not a buffer.
  fv::MvtTile tile;
  ASSERT_TRUE(ReadAndDecode(mb_path, kAtlanta, &tile).ok());
  const fv::GeoRect box = fv::webmerc::TileBounds(kAtlanta);
  const double lon_span = box.ur.lon - box.ll.lon;
  const double lat_span = box.ur.lat - box.ll.lat;
  ASSERT_GT(lon_span, 0.0);
  ASSERT_GT(lat_span, 0.0);

  size_t vertices = 0;
  for (const fv::MvtLayer& layer : tile.layers()) {
    for (const fv::MvtFeature& f : layer.features) {
      for (const auto& part : f.parts) {
        for (const fv::GeoPoint& p : part) {
          ++vertices;
          ASSERT_GE(p.lon, box.ll.lon - lon_span) << layer.name;
          ASSERT_LE(p.lon, box.ur.lon + lon_span) << layer.name;
          ASSERT_GE(p.lat, box.ll.lat - lat_span) << layer.name;
          ASSERT_LE(p.lat, box.ur.lat + lat_span) << layer.name;
        }
      }
    }
  }
  // Exact, and it reconciles with the oracle's independent walk of the same
  // command integers — which also says this tile has no ring the decoder
  // dropped, since a drop would have made the two disagree.
  //
  // "Reconciles" and not "matches": the oracle counts the PARAMETER PAIRS the
  // command integers carry (MoveTo + LineTo = 8825 + 32572 = 41,397) while
  // this decoder closes each ring by re-emitting its first point, so it is
  // longer by exactly one vertex per ClosePath command (1491 of them). A
  // renderer needs the closing point; an oracle counting wire bytes has no
  // reason to invent it. 41,397 + 1491 = 42,888, and if that identity ever
  // stops holding, one of the two is dropping geometry.
  EXPECT_EQ(vertices, 42888u);  // 40,780 before the 2026-08-17 re-cut
}

TEST(MvtRealData, ASweepOfTilesDecodesWithNothingDropped) {
  SKIP_WITHOUT_MBTILES();
  fv::MbtilesFile file;
  ASSERT_TRUE(file.Open(mb_path).ok());

  size_t decoded = 0, empty = 0, features = 0;
  size_t skipped = 0, orphans = 0, degenerate = 0;
  // A 6x6 block around Atlanta plus the coarse levels above it: enough tiles
  // to meet every geometry kind the cutter emits, cheap enough to run always.
  for (int dx = -3; dx <= 2; ++dx) {
    for (int dy = -3; dy <= 2; ++dy) {
      const fv::webmerc::TileId id{14, kAtlanta.x + dx, kAtlanta.y + dy};
      std::string blob;
      if (!file.ReadTile(id, &blob).ok()) continue;
      fv::MvtTile tile;
      ASSERT_TRUE(tile.Decode(blob.data(), blob.size(), id).ok())
          << id.z << "/" << id.x << "/" << id.y;
      ++decoded;
      if (tile.layers().empty()) ++empty;
      features += tile.feature_count();
      skipped += tile.skipped_features();
      orphans += tile.orphan_holes();
      degenerate += tile.degenerate_rings();
    }
  }
  for (int z = 0; z <= 13; ++z) {
    const fv::webmerc::TileId id{
        z, static_cast<int>(fv::webmerc::LonToTileX(-84.388, z)),
        static_cast<int>(fv::webmerc::LatToTileY(33.749, z))};
    std::string blob;
    if (!file.ReadTile(id, &blob).ok()) continue;
    fv::MvtTile tile;
    ASSERT_TRUE(tile.Decode(blob.data(), blob.size(), id).ok())
        << id.z << "/" << id.x << "/" << id.y;
    ++decoded;
    features += tile.feature_count();
    skipped += tile.skipped_features();
    orphans += tile.orphan_holes();
    degenerate += tile.degenerate_rings();
  }

  EXPECT_GE(decoded, 40u);
  EXPECT_GT(features, 100000u);
  EXPECT_EQ(skipped, 0u);
  EXPECT_EQ(orphans, 0u);
  // Degenerate rings are a real thing a cutter emits at coarse zooms, where
  // generalization can collapse a small island. Recorded, not forbidden.
  EXPECT_LT(degenerate, decoded);
  (void)empty;
}
