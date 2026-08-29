// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_osm_reader.h"

#include <expat.h>
#include <protozero/pbf_reader.hpp>
#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace fv {
namespace routing {
namespace {

// ---------------------------------------------------------------------------
// XML (.osm)
// ---------------------------------------------------------------------------

struct XmlState {
  OsmSink* sink = nullptr;
  XML_Parser parser = nullptr;
  bool want_nodes = true;
  bool want_ways = true;
  bool want_relations = false;

  bool in_way = false;
  bool in_relation = false;
  OsmWay way;
  OsmRelation relation;
  std::string error;
};

OsmMemberType MemberTypeFromString(const char* type) {
  if (type == nullptr) return OsmMemberType::kNode;
  if (std::strcmp(type, "way") == 0) return OsmMemberType::kWay;
  if (std::strcmp(type, "relation") == 0) return OsmMemberType::kRelation;
  return OsmMemberType::kNode;
}

const char* Attr(const char** atts, const char* name) {
  for (int i = 0; atts[i] != nullptr; i += 2) {
    if (std::strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

void XMLCALL XmlStart(void* user, const char* name, const char** atts) {
  XmlState* st = static_cast<XmlState*>(user);
  if (!st->error.empty()) return;

  if (std::strcmp(name, "node") == 0) {
    if (!st->want_nodes) return;
    if (!st->sink->Continue()) { XML_StopParser(st->parser, XML_FALSE); return; }
    const char* id = Attr(atts, "id");
    const char* lat = Attr(atts, "lat");
    const char* lon = Attr(atts, "lon");
    // A deleted node in a full-history dump carries no lat/lon; skip it
    // rather than inventing 0,0 (which is a real place in the Atlantic).
    if (id == nullptr || lat == nullptr || lon == nullptr) return;
    st->sink->Node(std::strtoll(id, nullptr, 10), std::atof(lat), std::atof(lon));
    return;
  }

  if (std::strcmp(name, "way") == 0) {
    if (!st->want_ways) return;
    if (!st->sink->Continue()) { XML_StopParser(st->parser, XML_FALSE); return; }
    st->in_way = true;
    st->way.refs.clear();
    st->way.tags.clear();
    const char* id = Attr(atts, "id");
    st->way.id = (id != nullptr) ? std::strtoll(id, nullptr, 10) : 0;
    return;
  }

  if (std::strcmp(name, "relation") == 0) {
    if (!st->want_relations) return;
    if (!st->sink->Continue()) { XML_StopParser(st->parser, XML_FALSE); return; }
    st->in_relation = true;
    st->relation.members.clear();
    st->relation.tags.clear();
    const char* id = Attr(atts, "id");
    st->relation.id = (id != nullptr) ? std::strtoll(id, nullptr, 10) : 0;
    return;
  }

  if (st->in_way && std::strcmp(name, "nd") == 0) {
    const char* ref = Attr(atts, "ref");
    if (ref != nullptr) st->way.refs.push_back(std::strtoll(ref, nullptr, 10));
    return;
  }

  if (st->in_relation && std::strcmp(name, "member") == 0) {
    const char* ref = Attr(atts, "ref");
    if (ref == nullptr) return;
    const char* role = Attr(atts, "role");
    OsmMember m;
    m.type = MemberTypeFromString(Attr(atts, "type"));
    m.ref = std::strtoll(ref, nullptr, 10);
    if (role != nullptr) m.role = role;
    st->relation.members.push_back(std::move(m));
    return;
  }

  if ((st->in_way || st->in_relation) && std::strcmp(name, "tag") == 0) {
    const char* k = Attr(atts, "k");
    const char* v = Attr(atts, "v");
    if (k == nullptr || v == nullptr) return;
    // A <way> and a <relation> never nest, so whichever flag is set owns it.
    if (st->in_way) {
      st->way.tags.push_back(OsmTag{k, v});
    } else {
      st->relation.tags.push_back(OsmTag{k, v});
    }
    return;
  }
}

void XMLCALL XmlEnd(void* user, const char* name) {
  XmlState* st = static_cast<XmlState*>(user);
  if (st->in_way && std::strcmp(name, "way") == 0) {
    st->in_way = false;
    st->sink->Way(st->way);
    return;
  }
  if (st->in_relation && std::strcmp(name, "relation") == 0) {
    st->in_relation = false;
    st->sink->Relation(st->relation);
  }
}

// ---------------------------------------------------------------------------
// PBF (.osm.pbf)
// ---------------------------------------------------------------------------

// Field numbers from the OSMPBF schema (fileformat.proto, osmformat.proto).
// They are wire format, so they are fixed forever; naming them here beats
// vendoring two .proto files and a protobuf compiler for six messages.
enum : uint32_t {
  kBlobHeaderType = 1,
  kBlobHeaderDataSize = 3,

  kBlobRaw = 1,
  kBlobRawSize = 2,
  kBlobZlibData = 3,

  kBlockStringTable = 1,
  kBlockPrimitiveGroup = 2,
  kBlockGranularity = 17,
  kBlockLatOffset = 19,
  kBlockLonOffset = 20,

  kStringTableEntry = 1,

  kGroupNodes = 1,
  kGroupDense = 2,
  kGroupWays = 3,
  kGroupRelations = 4,

  kNodeId = 1,
  kNodeKeys = 2,
  kNodeVals = 3,
  kNodeLat = 8,
  kNodeLon = 9,

  kDenseId = 1,
  kDenseLat = 8,
  kDenseLon = 9,
  kDenseKeysVals = 10,

  kWayId = 1,
  kWayKeys = 2,
  kWayVals = 3,
  kWayRefs = 8,

  kRelationId = 1,
  kRelationKeys = 2,
  kRelationVals = 3,
  kRelationRolesSid = 8,  // packed int32, indices into the string table
  kRelationMemids = 9,    // packed sint64, delta-encoded like a way's refs
  kRelationTypes = 10,    // packed enum, 0 = node, 1 = way, 2 = relation
};

class PbfReader {
 public:
  PbfReader(std::ifstream& in, OsmSink* sink) : in_(in), sink_(sink) {}

  Status Run() {
    want_nodes_ = sink_->WantNodes();
    want_ways_ = sink_->WantWays();
    want_relations_ = sink_->WantRelations();
    for (;;) {
      uint32_t header_len = 0;
      if (!ReadBigEndian32(&header_len)) break;  // clean EOF
      if (header_len > (64u << 20)) {
        return Status::Error(kIoError, "OSM PBF: implausible blob header length");
      }
      header_.resize(header_len);
      if (!in_.read(header_.data(), static_cast<std::streamsize>(header_len))) {
        return Status::Error(kIoError, "OSM PBF: truncated blob header");
      }

      std::string type;
      uint32_t data_size = 0;
      protozero::pbf_reader hdr(header_.data(), header_.size());
      while (hdr.next()) {
        switch (hdr.tag()) {
          case kBlobHeaderType: type = hdr.get_string(); break;
          case kBlobHeaderDataSize: data_size = static_cast<uint32_t>(hdr.get_int32()); break;
          default: hdr.skip(); break;
        }
      }
      if (data_size > (256u << 20)) {
        return Status::Error(kIoError, "OSM PBF: implausible blob length");
      }
      blob_.resize(data_size);
      if (!in_.read(blob_.data(), static_cast<std::streamsize>(data_size))) {
        return Status::Error(kIoError, "OSM PBF: truncated blob");
      }

      if (type != "OSMData") continue;  // OSMHeader carries no elements
      if (!sink_->Continue()) break;

      Status s = InflateBlob();
      if (!s.ok()) return s;
      s = ReadPrimitiveBlock();
      if (!s.ok()) return s;
    }
    return Status::Ok();
  }

 private:
  bool ReadBigEndian32(uint32_t* out) {
    unsigned char b[4];
    if (!in_.read(reinterpret_cast<char*>(b), 4)) return false;
    *out = (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
    return true;
  }

  Status InflateBlob() {
    protozero::pbf_reader blob(blob_.data(), blob_.size());
    protozero::data_view zlib_data;
    uint32_t raw_size = 0;
    bool have_raw = false;
    bool have_zlib = false;
    while (blob.next()) {
      switch (blob.tag()) {
        case kBlobRaw: {
          protozero::data_view raw = blob.get_view();
          block_.assign(raw.data(), raw.data() + raw.size());
          have_raw = true;
          break;
        }
        case kBlobRawSize: raw_size = static_cast<uint32_t>(blob.get_int32()); break;
        case kBlobZlibData: zlib_data = blob.get_view(); have_zlib = true; break;
        default: blob.skip(); break;
      }
    }
    if (have_raw) return Status::Ok();
    // raw_size comes straight off the wire and is what gets allocated. The
    // format caps an uncompressed block at 32 MiB; without this a hostile or
    // corrupt file asks for 4 GB and the allocation throws past the Status.
    if (raw_size > (64u << 20)) {
      return Status::Error(kIoError, "OSM PBF: implausible uncompressed blob size");
    }
    if (!have_zlib) {
      // lzma / bzip2 / lz4 / zstd blobs are all legal in the schema and none
      // is produced by osmium's defaults. Say so instead of silently
      // dropping half a continent.
      return Status::Error(kUnsupported, "OSM PBF: blob is neither raw nor zlib-compressed");
    }
    block_.resize(raw_size);
    uLongf out_len = raw_size;
    int rc = uncompress(reinterpret_cast<Bytef*>(block_.data()), &out_len,
                        reinterpret_cast<const Bytef*>(zlib_data.data()),
                        static_cast<uLong>(zlib_data.size()));
    if (rc != Z_OK || out_len != raw_size) {
      return Status::Error(kIoError, "OSM PBF: zlib inflate failed");
    }
    return Status::Ok();
  }

  Status ReadPrimitiveBlock() {
    strings_.clear();
    granularity_ = 100;
    lat_offset_ = 0;
    lon_offset_ = 0;

    // The string table always precedes the groups in every producer's output,
    // but the schema does not require it, so scan for it first.
    protozero::pbf_reader scan(block_.data(), block_.size());
    while (scan.next()) {
      switch (scan.tag()) {
        case kBlockStringTable: {
          protozero::pbf_reader table = scan.get_message();
          while (table.next(kStringTableEntry)) {
            protozero::data_view v = table.get_view();
            strings_.emplace_back(v.data(), v.size());
          }
          break;
        }
        case kBlockGranularity: granularity_ = scan.get_int32(); break;
        case kBlockLatOffset: lat_offset_ = scan.get_int64(); break;
        case kBlockLonOffset: lon_offset_ = scan.get_int64(); break;
        default: scan.skip(); break;
      }
    }

    protozero::pbf_reader block(block_.data(), block_.size());
    while (block.next(kBlockPrimitiveGroup)) {
      protozero::pbf_reader group = block.get_message();
      while (group.next()) {
        switch (group.tag()) {
          case kGroupNodes: {
            if (!want_nodes_) { group.skip(); break; }
            protozero::pbf_reader node = group.get_message();
            ReadNode(node);
            break;
          }
          case kGroupDense: {
            if (!want_nodes_) { group.skip(); break; }
            protozero::pbf_reader dense = group.get_message();
            ReadDenseNodes(dense);
            break;
          }
          case kGroupWays: {
            if (!want_ways_) { group.skip(); break; }
            protozero::pbf_reader way = group.get_message();
            ReadWay(way);
            break;
          }
          case kGroupRelations: {
            if (!want_relations_) { group.skip(); break; }
            protozero::pbf_reader relation = group.get_message();
            ReadRelation(relation);
            break;
          }
          default: group.skip(); break;
        }
      }
    }
    return Status::Ok();
  }

  double Lat(int64_t raw) const { return 1e-9 * static_cast<double>(lat_offset_ + granularity_ * raw); }
  double Lon(int64_t raw) const { return 1e-9 * static_cast<double>(lon_offset_ + granularity_ * raw); }

  void ReadNode(protozero::pbf_reader& node) {
    int64_t id = 0, lat = 0, lon = 0;
    while (node.next()) {
      switch (node.tag()) {
        case kNodeId: id = node.get_sint64(); break;
        case kNodeLat: lat = node.get_sint64(); break;
        case kNodeLon: lon = node.get_sint64(); break;
        default: node.skip(); break;
      }
    }
    sink_->Node(id, Lat(lat), Lon(lon));
  }

  void ReadDenseNodes(protozero::pbf_reader& dense) {
    ids_.clear();
    lats_.clear();
    lons_.clear();
    while (dense.next()) {
      switch (dense.tag()) {
        case kDenseId: {
          auto range = dense.get_packed_sint64();
          for (int64_t v : range) ids_.push_back(v);
          break;
        }
        case kDenseLat: {
          auto range = dense.get_packed_sint64();
          for (int64_t v : range) lats_.push_back(v);
          break;
        }
        case kDenseLon: {
          auto range = dense.get_packed_sint64();
          for (int64_t v : range) lons_.push_back(v);
          break;
        }
        default: dense.skip(); break;
      }
    }
    // Every dense field is delta-encoded against the previous entry.
    size_t n = ids_.size();
    if (lats_.size() < n || lons_.size() < n) n = 0;  // malformed block; drop it
    int64_t id = 0, lat = 0, lon = 0;
    for (size_t i = 0; i < n; ++i) {
      id += ids_[i];
      lat += lats_[i];
      lon += lons_[i];
      sink_->Node(id, Lat(lat), Lon(lon));
    }
  }

  void ReadWay(protozero::pbf_reader& way) {
    scratch_way_.id = 0;
    scratch_way_.refs.clear();
    scratch_way_.tags.clear();
    keys_.clear();
    vals_.clear();
    while (way.next()) {
      switch (way.tag()) {
        case kWayId: scratch_way_.id = way.get_int64(); break;
        case kWayKeys: {
          auto range = way.get_packed_uint32();
          for (uint32_t v : range) keys_.push_back(v);
          break;
        }
        case kWayVals: {
          auto range = way.get_packed_uint32();
          for (uint32_t v : range) vals_.push_back(v);
          break;
        }
        case kWayRefs: {
          auto range = way.get_packed_sint64();
          int64_t ref = 0;
          for (int64_t d : range) {
            ref += d;  // delta-encoded
            scratch_way_.refs.push_back(ref);
          }
          break;
        }
        default: way.skip(); break;
      }
    }
    size_t n = keys_.size() < vals_.size() ? keys_.size() : vals_.size();
    for (size_t i = 0; i < n; ++i) {
      if (keys_[i] >= strings_.size() || vals_[i] >= strings_.size()) continue;
      scratch_way_.tags.push_back(OsmTag{strings_[keys_[i]], strings_[vals_[i]]});
    }
    sink_->Way(scratch_way_);
  }

  void ReadRelation(protozero::pbf_reader& relation) {
    scratch_relation_.id = 0;
    scratch_relation_.members.clear();
    scratch_relation_.tags.clear();
    keys_.clear();
    vals_.clear();
    roles_.clear();
    memids_.clear();
    types_.clear();
    while (relation.next()) {
      switch (relation.tag()) {
        case kRelationId: scratch_relation_.id = relation.get_int64(); break;
        case kRelationKeys: {
          auto range = relation.get_packed_uint32();
          for (uint32_t v : range) keys_.push_back(v);
          break;
        }
        case kRelationVals: {
          auto range = relation.get_packed_uint32();
          for (uint32_t v : range) vals_.push_back(v);
          break;
        }
        case kRelationRolesSid: {
          auto range = relation.get_packed_int32();
          for (int32_t v : range) roles_.push_back(v);
          break;
        }
        case kRelationMemids: {
          auto range = relation.get_packed_sint64();
          int64_t ref = 0;
          for (int64_t d : range) {
            ref += d;  // delta-encoded, like a way's refs
            memids_.push_back(ref);
          }
          break;
        }
        case kRelationTypes: {
          auto range = relation.get_packed_int32();
          for (int32_t v : range) types_.push_back(v);
          break;
        }
        default: relation.skip(); break;
      }
    }
    size_t tag_n = keys_.size() < vals_.size() ? keys_.size() : vals_.size();
    for (size_t i = 0; i < tag_n; ++i) {
      if (keys_[i] >= strings_.size() || vals_[i] >= strings_.size()) continue;
      scratch_relation_.tags.push_back(OsmTag{strings_[keys_[i]], strings_[vals_[i]]});
    }
    // The three member arrays are parallel; a producer that disagrees about
    // their length has written something this reader cannot interpret, so
    // take only the prefix all three cover rather than guessing.
    size_t mem_n = memids_.size();
    if (roles_.size() < mem_n) mem_n = roles_.size();
    if (types_.size() < mem_n) mem_n = types_.size();
    for (size_t i = 0; i < mem_n; ++i) {
      OsmMember m;
      m.ref = memids_[i];
      m.type = (types_[i] >= 0 && types_[i] <= 2) ? static_cast<OsmMemberType>(types_[i])
                                                  : OsmMemberType::kNode;
      const size_t sid = static_cast<size_t>(roles_[i]);
      if (roles_[i] >= 0 && sid < strings_.size()) m.role = strings_[sid];
      scratch_relation_.members.push_back(std::move(m));
    }
    sink_->Relation(scratch_relation_);
  }

  std::ifstream& in_;
  OsmSink* sink_;
  bool want_nodes_ = true;
  bool want_ways_ = true;
  bool want_relations_ = false;

  std::vector<char> header_;
  std::vector<char> blob_;
  std::vector<char> block_;
  std::vector<std::string> strings_;
  int32_t granularity_ = 100;
  int64_t lat_offset_ = 0;
  int64_t lon_offset_ = 0;

  std::vector<int64_t> ids_, lats_, lons_;
  std::vector<uint32_t> keys_, vals_;
  std::vector<int32_t> roles_, types_;
  std::vector<int64_t> memids_;
  OsmWay scratch_way_;
  OsmRelation scratch_relation_;
};

bool EndsWith(const std::string& s, const char* suffix) {
  size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

}  // namespace

Status ReadOsmXml(const std::string& path, OsmSink* sink) {
  if (sink == nullptr) return Status::Error(kInvalidArg, "ReadOsmXml: null sink");
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return Status::Error(kIoError, "cannot open " + path);

  XML_Parser parser = XML_ParserCreate(nullptr);
  if (parser == nullptr) return Status::Error(kInternal, "expat: parser creation failed");

  XmlState st;
  st.sink = sink;
  st.parser = parser;
  st.want_nodes = sink->WantNodes();
  st.want_ways = sink->WantWays();
  st.want_relations = sink->WantRelations();
  XML_SetUserData(parser, &st);
  XML_SetElementHandler(parser, XmlStart, XmlEnd);

  Status result = Status::Ok();
  std::vector<char> buf(1 << 16);
  for (;;) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    std::streamsize got = in.gcount();
    bool final_chunk = (got == 0) || in.eof();
    if (XML_Parse(parser, buf.data(), static_cast<int>(got), final_chunk ? 1 : 0) == XML_STATUS_ERROR) {
      // A sink that asked to stop is not an error; everything else is.
      if (XML_GetErrorCode(parser) == XML_ERROR_ABORTED) break;
      char msg[256];
      std::snprintf(msg, sizeof(msg), "%s: XML parse error at line %lu: %s", path.c_str(),
                    static_cast<unsigned long>(XML_GetCurrentLineNumber(parser)),
                    XML_ErrorString(XML_GetErrorCode(parser)));
      result = Status::Error(kIoError, msg);
      break;
    }
    if (final_chunk) break;
  }
  XML_ParserFree(parser);
  return result;
}

Status ReadOsmPbf(const std::string& path, OsmSink* sink) {
  if (sink == nullptr) return Status::Error(kInvalidArg, "ReadOsmPbf: null sink");
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return Status::Error(kIoError, "cannot open " + path);
  PbfReader reader(in, sink);
  return reader.Run();
}

Status ReadOsmFile(const std::string& path, OsmSink* sink) {
  return EndsWith(path, ".pbf") ? ReadOsmPbf(path, sink) : ReadOsmXml(path, sink);
}

}  // namespace routing
}  // namespace fv
