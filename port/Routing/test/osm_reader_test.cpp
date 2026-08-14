// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Raw OSM reader tests (O4).
//
// The XML side runs against TestData/OSM/map*.osm — four adjacent API
// exports around Kiawah Island, SC.
//
// The PBF side is tested twice. First against a fixture this file encodes
// itself, byte by byte, with its own varint writer: that is deliberately a
// second implementation of the wire format, so a shared misunderstanding of
// zigzag or delta encoding cannot cancel out. Then against the real 4 GB
// us-south extract, briefly, to prove a genuine producer's output decodes —
// PBF puts every node before the first way, so a test that wanted to see a
// way there would have to read half the file.

#include "fv_osm_reader.h"

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

std::string OsmPath(const char* name) {
  return (fs::path(TestDataDir()) / "OSM" / name).string();
}

// map.osm is map TEST DATA and is not in the repository: a clone without it
// must SKIP rather than fail, which is the published tree's no-map-data
// guarantee. (The .osm.pbf tests below already skip this way.)
#define SKIP_WITHOUT_MAP_OSM()                                  \
  const std::string map_osm = OsmPath("map.osm");               \
  if (!fs::exists(map_osm)) GTEST_SKIP() << "no " << map_osm


using fv::routing::OsmSink;
using fv::routing::OsmWay;

// Counts everything and keeps the extremes of what it saw.
class CountingSink : public OsmSink {
 public:
  bool WantNodes() const override { return want_nodes; }
  bool WantWays() const override { return want_ways; }
  bool WantRelations() const override { return want_relations_flag; }
  bool Continue() const override { return ways < way_limit && nodes < node_limit; }

  void Node(int64_t id, double lat, double lon) override {
    if (nodes == 0) {
      min_lat = max_lat = lat;
      min_lon = max_lon = lon;
      first_node_id = id;
    }
    ++nodes;
    min_lat = std::min(min_lat, lat);
    max_lat = std::max(max_lat, lat);
    min_lon = std::min(min_lon, lon);
    max_lon = std::max(max_lon, lon);
    if (lat >= 24.0 && lat <= 41.0 && lon >= -107.0 && lon <= -75.0) ++nodes_in_us_south;
    all_nodes.emplace_back(id, std::make_pair(lat, lon));
  }

  void Way(const OsmWay& w) override {
    ++ways;
    refs += static_cast<int64_t>(w.refs.size());
    tags += static_cast<int64_t>(w.tags.size());
    if (w.Find("highway") != nullptr) ++highway_ways;
    all_ways.push_back(w);
  }

  void Relation(const fv::routing::OsmRelation& r) override {
    ++relations;
    all_relations.push_back(r);
  }

  bool want_nodes = true;
  bool want_ways = true;
  bool want_relations_flag = false;
  int64_t way_limit = std::numeric_limits<int64_t>::max();
  int64_t node_limit = std::numeric_limits<int64_t>::max();

  int64_t nodes = 0, ways = 0, refs = 0, tags = 0, highway_ways = 0, nodes_in_us_south = 0;
  int64_t relations = 0;
  int64_t first_node_id = 0;
  double min_lat = 0, max_lat = 0, min_lon = 0, max_lon = 0;
  std::vector<std::pair<int64_t, std::pair<double, double>>> all_nodes;
  std::vector<OsmWay> all_ways;
  std::vector<fv::routing::OsmRelation> all_relations;
};

// ---------------------------------------------------------------------------
// A hand-rolled OSM PBF encoder, independent of protozero
// ---------------------------------------------------------------------------

void PutVarint(std::string* out, uint64_t v) {
  while (v >= 0x80) {
    out->push_back(static_cast<char>((v & 0x7F) | 0x80));
    v >>= 7;
  }
  out->push_back(static_cast<char>(v));
}

uint64_t ZigZag(int64_t v) { return (static_cast<uint64_t>(v) << 1) ^ (v >> 63); }

void PutKey(std::string* out, uint32_t field, uint32_t wire) {
  PutVarint(out, (static_cast<uint64_t>(field) << 3) | wire);
}

void PutVarintField(std::string* out, uint32_t field, uint64_t v) {
  PutKey(out, field, 0);
  PutVarint(out, v);
}

void PutBytesField(std::string* out, uint32_t field, const std::string& bytes) {
  PutKey(out, field, 2);
  PutVarint(out, bytes.size());
  out->append(bytes);
}

std::string PackedZigZag(const std::vector<int64_t>& values) {
  std::string out;
  for (int64_t v : values) PutVarint(&out, ZigZag(v));
  return out;
}

std::string PackedVarint(const std::vector<uint32_t>& values) {
  std::string out;
  for (uint32_t v : values) PutVarint(&out, v);
  return out;
}

// Frames one blob: 4-byte big-endian BlobHeader length, BlobHeader, Blob.
void AppendBlob(std::string* file, const std::string& type, const std::string& payload,
                bool compress_it) {
  std::string blob;
  if (compress_it) {
    uLongf bound = compressBound(static_cast<uLong>(payload.size()));
    std::string z(bound, '\0');
    ASSERT_EQ(compress(reinterpret_cast<Bytef*>(&z[0]), &bound,
                       reinterpret_cast<const Bytef*>(payload.data()),
                       static_cast<uLong>(payload.size())),
              Z_OK);
    z.resize(bound);
    PutVarintField(&blob, 2, payload.size());  // raw_size
    PutBytesField(&blob, 3, z);                // zlib_data
  } else {
    PutBytesField(&blob, 1, payload);  // raw
  }

  std::string header;
  PutBytesField(&header, 1, type);
  PutVarintField(&header, 3, blob.size());

  const uint32_t n = static_cast<uint32_t>(header.size());
  const char be[4] = {static_cast<char>((n >> 24) & 0xFF), static_cast<char>((n >> 16) & 0xFF),
                      static_cast<char>((n >> 8) & 0xFF), static_cast<char>(n & 0xFF)};
  file->append(be, 4);
  file->append(header);
  file->append(blob);
}

// Two nodes and one way, with a string table shared between them. Node
// coordinates are exact multiples of 1e-7 so the granularity arithmetic is
// checkable to the last digit.
std::string SyntheticOsmData() {
  std::string strings;
  // s[0] is always the empty string by convention.
  for (const char* s : {"", "highway", "residential", "name", "Test Street", "oneway", "yes",
                        "type", "restriction", "no_left_turn", "from", "via", "to"}) {
    PutBytesField(&strings, 1, std::string(s));
  }

  // DenseNodes: ids 100, 101, 102; delta-encoded ids and coordinates.
  std::string dense;
  PutBytesField(&dense, 1, PackedZigZag({100, 1, 1}));  // 100, 101, 102
  // 32.7 -> 327000000 raw (granularity 100, so degrees * 1e7)
  PutBytesField(&dense, 8, PackedZigZag({327000000, 100000, -50000}));
  PutBytesField(&dense, 9, PackedZigZag({-800000000, -30000, -40000}));

  std::string group_nodes;
  PutBytesField(&group_nodes, 2, dense);

  // Way 500: refs 100 -> 101 -> 102, tags highway=residential, name=Test
  // Street, oneway=yes.
  std::string way;
  PutVarintField(&way, 1, 500);
  PutBytesField(&way, 2, PackedVarint({1, 3, 5}));  // keys
  PutBytesField(&way, 3, PackedVarint({2, 4, 6}));  // vals
  PutBytesField(&way, 8, PackedZigZag({100, 1, 1}));

  std::string group_ways;
  PutBytesField(&group_ways, 3, way);

  // Relation 900: type=restriction, restriction=no_left_turn, from way 500
  // via node 102 to way 501. memids are delta-encoded across ALL members
  // whatever their type, which is the part of the schema most easily got
  // wrong — 500, then -398 to reach node 102, then +399 to reach way 501.
  std::string relation;
  PutVarintField(&relation, 1, 900);
  PutBytesField(&relation, 2, PackedVarint({7, 8}));   // keys: type, restriction
  PutBytesField(&relation, 3, PackedVarint({8, 9}));   // vals: restriction, no_left_turn
  PutBytesField(&relation, 8, PackedVarint({10, 11, 12}));  // roles: from, via, to
  PutBytesField(&relation, 9, PackedZigZag({500, -398, 399}));
  PutBytesField(&relation, 10, PackedVarint({1, 0, 1}));  // way, node, way

  std::string group_relations;
  PutBytesField(&group_relations, 4, relation);

  std::string block;
  PutBytesField(&block, 1, strings);
  PutBytesField(&block, 2, group_nodes);
  PutBytesField(&block, 2, group_ways);
  PutBytesField(&block, 2, group_relations);
  return block;
}

std::string WriteSyntheticPbf(const char* name, bool compress_it) {
  fs::path dir = fs::temp_directory_path() / "fv_routing_test";
  fs::create_directories(dir);
  const fs::path path = dir / name;

  std::string file;
  std::string header_block;  // an empty HeaderBlock; the reader must skip it
  AppendBlob(&file, "OSMHeader", header_block, compress_it);
  AppendBlob(&file, "OSMData", SyntheticOsmData(), compress_it);

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(file.data(), static_cast<std::streamsize>(file.size()));
  out.close();
  return path.string();
}

// ---------------------------------------------------------------------------
// XML
// ---------------------------------------------------------------------------

TEST(OsmXmlReader, ReadsNodesWaysAndTags) {
  SKIP_WITHOUT_MAP_OSM();
  CountingSink sink;
  const fv::Status s = fv::routing::ReadOsmFile(OsmPath("map.osm"), &sink);
  ASSERT_EQ(s.code, fv::kOk) << s.message;

  // Pinned against the file: 30,799 <node> and 1,605 <way>, 493 of them with a
  // highway tag. (A grep for k="highway" counts 541 — the other 48 are on
  // nodes, crossings and the like, which is why the count is taken here from
  // ways only.)
  //
  // RE-PINNED 2026-08-12 when Chris refreshed the extract: was 14,450 / 747 /
  // 245 over a smaller box. The counts are on ONE NAMED FILE, which is what
  // the ledger's rule allows — but the file is one a person replaces, so if
  // this fails again check the data before the reader.
  EXPECT_EQ(sink.nodes, 30799);
  EXPECT_EQ(sink.ways, 1605);
  EXPECT_EQ(sink.highway_ways, 493);
  EXPECT_GT(sink.refs, sink.ways);  // every way is a polyline
  EXPECT_GT(sink.tags, sink.ways);  // and carries tags

  // NOTE: the node set reaches well past the file's own <bounds> — the API
  // exports a way that crosses the bbox in full, nodes and all. That is why
  // the map*.osm exports share nodes at all, and why nothing here pins the
  // declared bounds.
  //
  // The 2026-08-12 refresh extended the extract SOUTH-WEST and nowhere else:
  // min moved 32.5591137 -> 32.5089149 and -80.1965704 -> -80.2401359 while
  // both maxima are the numbers this test has always carried.
  EXPECT_NEAR(sink.min_lat, 32.5089149, 1e-6);
  EXPECT_NEAR(sink.max_lat, 32.7179000, 1e-6);
  EXPECT_NEAR(sink.min_lon, -80.2401359, 1e-6);
  EXPECT_NEAR(sink.max_lon, -79.9666451, 1e-6);

  // Node IDs are real OSM identities, not indices.
  EXPECT_EQ(sink.first_node_id, 110069523);
}

TEST(OsmXmlReader, WantNodesFalseSkipsNodeRecords) {
  SKIP_WITHOUT_MAP_OSM();
  CountingSink sink;
  sink.want_nodes = false;
  const fv::Status s = fv::routing::ReadOsmFile(OsmPath("map.osm"), &sink);
  ASSERT_EQ(s.code, fv::kOk) << s.message;
  EXPECT_EQ(sink.nodes, 0);
  EXPECT_EQ(sink.ways, 1605);
}

TEST(OsmXmlReader, ContinueFalseStopsEarlyAndIsNotAnError) {
  SKIP_WITHOUT_MAP_OSM();
  CountingSink sink;
  sink.want_nodes = false;
  sink.way_limit = 10;
  const fv::Status s = fv::routing::ReadOsmFile(OsmPath("map.osm"), &sink);
  ASSERT_EQ(s.code, fv::kOk) << s.message;
  EXPECT_EQ(sink.ways, 10);
}

TEST(OsmXmlReader, MissingFileIsAnIoError) {
  CountingSink sink;
  EXPECT_EQ(fv::routing::ReadOsmFile(OsmPath("no-such-file.osm"), &sink).code, fv::kIoError);
}

TEST(OsmXmlReader, MalformedXmlIsReportedNotIgnored) {
  fs::path dir = fs::temp_directory_path() / "fv_routing_test";
  fs::create_directories(dir);
  const fs::path path = dir / "broken.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "<osm><way><nd ref=\"1\"/>";  // never closed
  }
  CountingSink sink;
  EXPECT_EQ(fv::routing::ReadOsmFile(path.string(), &sink).code, fv::kIoError);
}

TEST(OsmXmlReader, ReadsRelationMembersAndKeepsThemOffTheWay) {
  fs::path dir = fs::temp_directory_path() / "fv_routing_test";
  fs::create_directories(dir);
  const fs::path path = dir / "relation.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.70" lon="-80.00"/>
 <way id="500"><nd ref="1"/><tag k="highway" v="residential"/></way>
 <relation id="900">
  <member type="way" ref="500" role="from"/>
  <member type="node" ref="1" role="via"/>
  <member type="way" ref="501" role="to"/>
  <tag k="type" v="restriction"/>
  <tag k="restriction" v="no_left_turn"/>
 </relation>
</osm>
)";
  }

  CountingSink sink;
  sink.want_relations_flag = true;
  const fv::Status s = fv::routing::ReadOsmFile(path.string(), &sink);
  ASSERT_EQ(s.code, fv::kOk) << s.message;

  ASSERT_EQ(sink.ways, 1);
  // A <tag> inside a <relation> must not land on the way that came before it:
  // the two element bodies look identical to a handler that only tracks one.
  EXPECT_EQ(sink.all_ways[0].tags.size(), 1u);

  ASSERT_EQ(sink.relations, 1);
  const fv::routing::OsmRelation& r = sink.all_relations[0];
  EXPECT_EQ(r.id, 900);
  ASSERT_EQ(r.members.size(), 3u);
  EXPECT_EQ(r.members[1].ref, 1);
  EXPECT_EQ(r.members[1].type, fv::routing::OsmMemberType::kNode);
  EXPECT_EQ(r.members[1].role, "via");
  ASSERT_NE(r.Find("restriction"), nullptr);
  EXPECT_EQ(*r.Find("restriction"), "no_left_turn");
}

TEST(OsmXmlReader, RelationsAreSkippedUnlessAskedFor) {
  fs::path dir = fs::temp_directory_path() / "fv_routing_test";
  fs::create_directories(dir);
  const fs::path path = dir / "relation-skip.osm";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "<osm version=\"0.6\"><relation id=\"9\">"
           "<member type=\"node\" ref=\"1\" role=\"via\"/></relation></osm>";
  }
  CountingSink sink;  // want_relations_flag stays false
  ASSERT_EQ(fv::routing::ReadOsmFile(path.string(), &sink).code, fv::kOk);
  EXPECT_EQ(sink.relations, 0);
}

// ---------------------------------------------------------------------------
// PBF
// ---------------------------------------------------------------------------

class OsmPbfReaderTest : public ::testing::TestWithParam<bool> {};

TEST_P(OsmPbfReaderTest, DecodesDenseNodesWaysAndTheStringTable) {
  const std::string path =
      WriteSyntheticPbf(GetParam() ? "synthetic-zlib.osm.pbf" : "synthetic-raw.osm.pbf",
                        GetParam());
  CountingSink sink;
  const fv::Status s = fv::routing::ReadOsmFile(path, &sink);
  ASSERT_EQ(s.code, fv::kOk) << s.message;

  // Dense IDs and coordinates are all delta-encoded against the previous
  // entry, and coordinates are (offset + granularity * value) nanodegrees.
  ASSERT_EQ(sink.nodes, 3);
  EXPECT_EQ(sink.all_nodes[0].first, 100);
  EXPECT_EQ(sink.all_nodes[1].first, 101);
  EXPECT_EQ(sink.all_nodes[2].first, 102);
  EXPECT_NEAR(sink.all_nodes[0].second.first, 32.70, 1e-9);
  EXPECT_NEAR(sink.all_nodes[0].second.second, -80.00, 1e-9);
  EXPECT_NEAR(sink.all_nodes[1].second.first, 32.71, 1e-9);
  EXPECT_NEAR(sink.all_nodes[1].second.second, -80.003, 1e-9);
  EXPECT_NEAR(sink.all_nodes[2].second.first, 32.705, 1e-9);
  EXPECT_NEAR(sink.all_nodes[2].second.second, -80.007, 1e-9);

  ASSERT_EQ(sink.ways, 1);
  const OsmWay& way = sink.all_ways[0];
  EXPECT_EQ(way.id, 500);
  ASSERT_EQ(way.refs.size(), 3u);
  EXPECT_EQ(way.refs[0], 100);
  EXPECT_EQ(way.refs[1], 101);
  EXPECT_EQ(way.refs[2], 102);

  ASSERT_EQ(way.tags.size(), 3u);
  ASSERT_NE(way.Find("highway"), nullptr);
  EXPECT_EQ(*way.Find("highway"), "residential");
  ASSERT_NE(way.Find("name"), nullptr);
  EXPECT_EQ(*way.Find("name"), "Test Street");
  ASSERT_NE(way.Find("oneway"), nullptr);
  EXPECT_EQ(*way.Find("oneway"), "yes");
  EXPECT_EQ(way.Find("surface"), nullptr);

  // A sink that did not ask for relations is not handed the one in the block.
  EXPECT_EQ(sink.relations, 0);
}

TEST_P(OsmPbfReaderTest, DecodesRelationMembersAndRoles) {
  const std::string path =
      WriteSyntheticPbf(GetParam() ? "synthetic-rel-zlib.osm.pbf" : "synthetic-rel-raw.osm.pbf",
                        GetParam());
  CountingSink sink;
  sink.want_relations_flag = true;
  const fv::Status s = fv::routing::ReadOsmFile(path, &sink);
  ASSERT_EQ(s.code, fv::kOk) << s.message;

  ASSERT_EQ(sink.relations, 1);
  const fv::routing::OsmRelation& r = sink.all_relations[0];
  EXPECT_EQ(r.id, 900);
  ASSERT_NE(r.Find("type"), nullptr);
  EXPECT_EQ(*r.Find("type"), "restriction");
  ASSERT_NE(r.Find("restriction"), nullptr);
  EXPECT_EQ(*r.Find("restriction"), "no_left_turn");

  // The member order is meaningful and the delta chain runs across types, so
  // this checks the refs one by one rather than as a set.
  ASSERT_EQ(r.members.size(), 3u);
  EXPECT_EQ(r.members[0].ref, 500);
  EXPECT_EQ(r.members[0].role, "from");
  EXPECT_EQ(r.members[0].type, fv::routing::OsmMemberType::kWay);
  EXPECT_EQ(r.members[1].ref, 102);
  EXPECT_EQ(r.members[1].role, "via");
  EXPECT_EQ(r.members[1].type, fv::routing::OsmMemberType::kNode);
  EXPECT_EQ(r.members[2].ref, 501);
  EXPECT_EQ(r.members[2].role, "to");
  EXPECT_EQ(r.members[2].type, fv::routing::OsmMemberType::kWay);
}

INSTANTIATE_TEST_SUITE_P(BlobEncoding, OsmPbfReaderTest, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "Zlib" : "Raw";
                         });

TEST(OsmPbfReader, WantWaysFalseStillDecodesNodes) {
  const std::string path = WriteSyntheticPbf("synthetic-nodes.osm.pbf", true);
  CountingSink sink;
  sink.want_ways = false;
  ASSERT_EQ(fv::routing::ReadOsmFile(path, &sink).code, fv::kOk);
  EXPECT_EQ(sink.nodes, 3);
  EXPECT_EQ(sink.ways, 0);
}

TEST(OsmPbfReader, TruncatedFileIsAnIoErrorNotACrash) {
  const std::string src = WriteSyntheticPbf("synthetic-trunc.osm.pbf", true);
  const auto size = fs::file_size(src);
  fs::resize_file(src, size - 8);
  CountingSink sink;
  EXPECT_EQ(fv::routing::ReadOsmFile(src, &sink).code, fv::kIoError);
}

TEST(OsmPbfReader, ReadsARealExtract) {
  const std::string path = OsmPath("us-south-260728.osm.pbf");
  if (!fs::exists(path)) GTEST_SKIP() << "us-south .osm.pbf not present";

  // PBF stores every node before the first way, so this reads the leading
  // blocks only — enough to prove dense decoding against a real producer.
  CountingSink sink;
  sink.node_limit = 200000;
  const fv::Status s = fv::routing::ReadOsmFile(path, &sink);
  ASSERT_EQ(s.code, fv::kOk) << s.message;
  EXPECT_GT(sink.nodes, 100000);

  for (const auto& n : sink.all_nodes) {
    ASSERT_GE(n.second.first, -90.0);
    ASSERT_LE(n.second.first, 90.0);
    ASSERT_GE(n.second.second, -180.0);
    ASSERT_LE(n.second.second, 180.0);
  }
  // NOTE: a handful of nodes in this extract really do sit outside the US
  // South (a few dozen out of half a billion, down to 9.5N). Assert the bulk,
  // not the extremes — a delta-decoding error would move the bulk, not a
  // fringe.
  EXPECT_GT(static_cast<double>(sink.nodes_in_us_south) / sink.nodes, 0.99);
}

}  // namespace
