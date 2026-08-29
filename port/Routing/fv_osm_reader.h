// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_osm_reader.h — raw OpenStreetMap data readers (O4).
//
// This is deliberately NOT the MVT pyramid in port/Osm. Vector tiles are a
// *rendering* product: geometry is simplified, clipped at tile edges and
// carries no node identity, so two tiles never agree on where a junction is.
// A routable graph needs the raw planet extract, where a way is a list of
// node IDs and a shared node ID *is* the junction.
//
// Both readers push into one OsmSink so the graph builder does not care which
// container it was handed. Neither reader keeps state beyond the current
// element, so a 4 GB extract streams in constant memory — what the caller
// then chooses to retain is the caller's problem (see RoadGraphBuilder, which
// makes two passes precisely so it never holds every node in the file).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace routing {

// A tag as it appears on an OSM element. Kept as raw strings: the graph
// builder is the only thing that knows which keys matter.
struct OsmTag {
  std::string key;
  std::string value;
};

struct OsmWay {
  int64_t id = 0;
  std::vector<int64_t> refs;   // node IDs, in order along the way
  std::vector<OsmTag> tags;

  // Returns the value of `key`, or `fallback` when the tag is absent.
  const std::string* Find(const std::string& key) const {
    for (const OsmTag& t : tags) {
      if (t.key == key) return &t.value;
    }
    return nullptr;
  }
};

// What a relation member points at. The values are OSM's own (and the PBF
// schema's) member-type enumeration, in its order.
enum class OsmMemberType : uint8_t { kNode = 0, kWay = 1, kRelation = 2 };

struct OsmMember {
  OsmMemberType type = OsmMemberType::kNode;
  int64_t ref = 0;
  std::string role;  // "from", "via", "to", ... — "" is legal
};

struct OsmRelation {
  int64_t id = 0;
  std::vector<OsmMember> members;  // in order; order is meaningful to OSM
  std::vector<OsmTag> tags;

  const std::string* Find(const std::string& key) const {
    for (const OsmTag& t : tags) {
      if (t.key == key) return &t.value;
    }
    return nullptr;
  }
};

// Receives elements in file order. A reader calls WantNodes()/WantWays()/
// WantRelations() before decoding an element so a pass that only cares about
// ways can skip the (far more numerous) node records cheaply.
class OsmSink {
 public:
  virtual ~OsmSink() = default;

  virtual bool WantNodes() const { return true; }
  virtual bool WantWays() const { return true; }
  // Unlike the other two this defaults to FALSE. Relations arrived with O5
  // and almost no sink wants them; defaulting off means every existing pass
  // keeps skipping the records exactly as it did before they were decoded at
  // all, rather than paying for members it will throw away.
  virtual bool WantRelations() const { return false; }

  // Polled between elements (XML) and between blocks (PBF). Returning false
  // stops the read early and is reported as success — a sink that has seen
  // everything it needs should not have to wait out a continent.
  virtual bool Continue() const { return true; }

  virtual void Node(int64_t id, double lat, double lon) = 0;
  virtual void Way(const OsmWay& way) = 0;
  // Not pure: a sink that leaves WantRelations() false never sees one.
  virtual void Relation(const OsmRelation& relation) { (void)relation; }
};

// `.osm` — the XML form the OSM API serves. Parsed with expat.
Status ReadOsmXml(const std::string& path, OsmSink* sink);

// `.osm.pbf` — the Protocol Buffers form osmium/Geofabrik distribute.
// Supports the two blob encodings that exist in the wild: raw and zlib.
Status ReadOsmPbf(const std::string& path, OsmSink* sink);

// Dispatches on the file extension: `.pbf` (including `.osm.pbf`) goes to the
// PBF reader, everything else to the XML one.
Status ReadOsmFile(const std::string& path, OsmSink* sink);

}  // namespace routing
}  // namespace fv
